#pragma once

#include "chordlab/identify.h"
#include "chordlab/pitch.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace chordlab {

/// One normalised incoming MIDI event, as the plug-in's input adapter hands it to the listener (design D1).
/// `kind` is a note-on, a note-off (a note-on with velocity 0 is already turned into one) or a sustain
/// pedal change; channels are merged. For a note `pitch` is the MIDI note (0-127; larger values are
/// ignored by `Listener`) and `velocity` its velocity; for `Sustain` `pitch` is unused and `velocity`
/// carries the CC64 value (0-127; 64 and above is pedal down). `sampleTime` is the audio thread's running
/// sample clock at the event, in samples, the same clock for every event. `Reset` (pitch and velocity 0)
/// is not MIDI: the shell pushes it in-band when the host resets the plug-in, so it lands exactly between
/// the events before and after the reset; `heardEventOf` never produces it.
struct HeardEvent {
    enum Kind : std::uint8_t { NoteOn, NoteOff, Sustain, Reset } kind;
    std::uint8_t pitch;
    std::uint8_t velocity;
    std::uint64_t sampleTime;
};

/// The key the listener shows (design D8): `key` (Ionian for a major estimate, Aeolian for a minor one,
/// the tonic spelled by `canonicalTonic`), `r` the Pearson correlation of the histogram with that key's
/// profile as of the last evaluation tick, and `gap` the best candidate's `r` minus the runner-up's at
/// that tick. A tick on which the shown key's correlation is undefined (an empty or flat histogram, as
/// after a jump of more than `kWindowBeats` in one `advance`) keeps the previous `r` and `gap`, so the UI
/// does not flash 0 %; a defined correlation that fails the retain test is still reported.
struct HeardKey {
    Key key;
    double r;
    double gap;
};

/// The Krumhansl-Kessler key profiles as music21 ships them, C-rooted (index 0 = the tonic): the major
/// profile and the minor profile. A candidate key with tonic `t` compares `histogram()[(t + i) % 12]`
/// against `profile[i]`. Public for the tests.
extern const std::array<double, 12> kMajorProfile, kMinorProfile;

/// The Pearson correlation of two 12-vectors, in [-1, 1]; NaN when either has zero variance (all of its
/// elements equal). Public for the tests.
double pearson(const std::array<double, 12>& a, const std::array<double, 12>& b) noexcept;

/// What the plug-in heard (design D4-D10): the notes held right now, a duration-weighted pitch-class
/// histogram of the last 32 beats, the key estimated from it and the last chord identified.
///
/// Threads: main thread only. The listener may allocate (it returns vectors and is not real time), but
/// its own state is fixed-size: a ring of the last `kNotes` notes and one slot per MIDI pitch.
///
/// Held set: a note-on holds its pitch, and a second note-on on a held pitch restarts it (one entry, one
/// new note in the histogram). A note-off releases the pitch unless the sustain pedal is down at that
/// moment (CC64 of 64 or more), in which case the pitch stays held until the pedal drops below 64. A
/// note-on after the pedal went up is held by its key alone: the pedal only holds notes released while it
/// was down.
///
/// Time: the listener counts beats, not samples. Each `feed` and `advance` converts the samples since the
/// previous call into beats with the tempo passed to that call, `Δbeats = Δsamples * bpm / (60 *
/// sampleRate)`, so it works with the transport stopped and follows tempo changes approximately. Time is
/// monotonic: the first call only sets the reference stamp, an event stamped before the last one is taken
/// as simultaneous with it (Δ = 0), and `advance` never moves time backwards. A stamp earlier than the
/// reference never lowers it. A call with an unusable tempo or sample rate (not positive and finite)
/// moves no time.
///
/// Histogram: the last `kNotes` notes are kept as `{pitch class, start beat, end beat}`; a held note's end
/// is "now". Each note adds to its pitch class `min(overlap, kNoteCapBeats)`, where `overlap` is the length
/// of `[max(start, now - kWindowBeats), min(end, now))` when positive, plus `kOnsetWeight` if
/// `start >= now - kWindowBeats` (an onset exactly at the window start counts). A note older than the
/// window drops out, and one straddling the window start counts its inside part and no onset.
/// `onsetsInWindow` counts the notes whose start is inside the window.
///
/// Key (design D8): evaluated only at a tick, `advance` or `clear`, never in `feed`. At a tick the
/// histogram is correlated (`pearson`) with the 24 rotated profiles, NaN candidates skipped; `best` and
/// `second` are the two highest, `gap = best.r - second.r`, `onsets = onsetsInWindow()`, and the shown
/// key `s` has its own `r_s` recomputed this tick:
///  - acquire (nothing shown): show `best` iff `best.r >= kMinR`, `onsets >= kMinOnsets` and
///    `gap >= kMinGap`;
///  - retain (`s` shown): `r_s >= kMinR` and `onsets >= kMinOnsets` (the gap need not persist);
///  - replace: `best != s`, `best` satisfies acquire and `best.r >= r_s + kHysteresis`, so a relative
///    major and minor do not flicker. While `kHysteresis == kMinGap` the last clause is implied by the
///    acquire gap (when `s` is not the best it is at most the runner-up, so `best.r - r_s >= gap >=
///    kMinGap`); it only matters if `kHysteresis > kMinGap`. The constant stays 0.05 until Live listening
///    says otherwise;
///  - remove: retain failed on two consecutive ticks (a retaining or replacing tick resets the count).
/// `key()` reports `r_s` and the gap as of the last tick.
///
/// Chord (design D10, latching): on a note-on (a valid pitch) that leaves at least two distinct pitch
/// classes held, `identifyChord(held())` runs; a match replaces the latched chord and no match keeps
/// it, so Am7 minus its A stays Am7. Note-offs and the pedal never change the latched chord.
/// `chordHeld` is recomputed on every feed: true iff every pitch class of the latched chord has a held
/// pitch (pedal-held included). The chord stays after the hands lift, until the next identified chord
/// or `clear`.
///
/// Visible outputs: `active`, the held set, `onsetsInWindow`, the shown key as tonic, mode and percent
/// (`round(100 * r)`, never the raw `r`), the latched chord and `chordHeld`. `changed` tells whether any
/// of them differs from the ones recorded by the last `acknowledge`, which is how the shell decides to
/// push the heard state to the UI.
class Listener {
public:
    /// Capacity of the note ring: the newest this many notes count towards the histogram.
    static constexpr std::size_t kNotes = 256;
    /// Length of the histogram window, in beats back from now.
    static constexpr double kWindowBeats = 32.0;
    /// The most one note's duration adds to its pitch class, in beats.
    static constexpr double kNoteCapBeats = 4.0;
    /// What each onset inside the window adds to its pitch class, in beats.
    static constexpr double kOnsetWeight = 0.25;
    /// The lowest correlation that acquires or retains a key.
    static constexpr double kMinR = 0.6;
    /// The lead over the runner-up that acquiring a key needs.
    static constexpr double kMinGap = 0.05;
    /// How much better than the shown key's own correlation a different key must be to replace it.
    static constexpr double kHysteresis = 0.05;
    /// The fewest onsets inside the window that acquire or retain a key.
    static constexpr int kMinOnsets = 6;

    /// An empty, inactive listener: no notes, nothing held, no key, no chord, `changed` false.
    Listener();

    /// Applies one event after moving time to its stamp (see the class comment), at tempo `bpm` and
    /// `sampleRate` Hz. Updates the held set, the notes and the latched chord (design D10), recomputes
    /// `chordHeld` and sets `changed`. Never evaluates the key. A note event with a pitch of 128 or more
    /// is ignored (it still counts as an event and moves time). A `Reset` event is `clear()` and nothing
    /// else: it moves no time and leaves the listener inactive.
    void feed(const HeardEvent& e, double bpm, double sampleRate);

    /// Moves time to `nowSampleTime` (never backwards) at tempo `bpm` and `sampleRate` Hz and sets
    /// `changed` when a visible output now differs from the acknowledged one. An evaluation tick for the
    /// key estimate (design D4, D8: acquire, retain, replace, remove); it never clears `changed`. An
    /// `advance` before any event only sets the reference stamp and does not make the listener active.
    void advance(std::uint64_t nowSampleTime, double bpm, double sampleRate);

    /// Forgets everything: notes, the held set, the pedal, time and its reference stamp, the shown key and
    /// the latched chord. `active` becomes false and `changed` true, so the UI is told to hide the group.
    /// It resynchronises by forgetting: a key still physically down is forgotten and its later note-off
    /// ignored, and a pedal still down counts as up until the next CC64. Callers: the shell after a lost
    /// epoch (a heard-ring overflow), so no note stays held by a lost note-off, the user's ×, and `feed`
    /// on a `Reset` event (a host reset).
    void clear();

    /// Whether an event arrived since construction or the last `clear`.
    bool active() const noexcept;

    /// The held pitches (keys down, or released under the pedal), ascending.
    std::vector<int> held() const;

    /// Listened time in beats since construction or the last `clear` (the first stamp is beat 0).
    double nowBeats() const noexcept;

    /// The pitch-class histogram at `nowBeats()`, index 0 = C, by the rule in the class comment.
    std::array<double, 12> histogram() const;

    /// How many notes started inside the window `[nowBeats() - kWindowBeats, nowBeats()]`.
    int onsetsInWindow() const;

    /// True after `clear` or any `feed`, or when an `advance` found a visible output different from the
    /// acknowledged one, until the next `acknowledge`. False after construction.
    bool changed() const noexcept;

    /// Records the visible outputs as seen and clears `changed`.
    void acknowledge();

    /// The shown key (hysteresis applied) as of the last tick, or nullopt.
    std::optional<HeardKey> key() const noexcept;

    /// The latched chord (design D10): the last chord identified at a note-on, or nullopt.
    std::optional<HeardChord> chord() const noexcept;

    /// Whether every pitch class of the latched chord is still held (keys or pedal); false without one.
    bool chordHeld() const noexcept;

private:
    struct Note {
        std::uint8_t pc;
        double start;
        double end;  // meaningful once `open` is false
        bool open;
    };

    /// The outputs the UI shows, the set `changed` compares. The key is compared as shown (tonic, mode,
    /// percent), not by its raw `r`, which moves on every tick.
    struct Visible {
        bool active = false;
        std::array<bool, 128> held{};
        int onsets = 0;
        std::optional<Key> key;
        int keyPercent = 0;
        bool hasChord = false;
        Chord chord{0, nullptr};
        int chordBass = 0;
        int chordInversion = 0;
        bool chordHeld = false;
        bool operator==(const Visible& o) const noexcept {
            return active == o.active && held == o.held && onsets == o.onsets && key == o.key &&
                   keyPercent == o.keyPercent && hasChord == o.hasChord && chord == o.chord &&
                   chordBass == o.chordBass && chordInversion == o.chordInversion && chordHeld == o.chordHeld;
        }
        bool operator!=(const Visible& o) const noexcept { return !(*this == o); }
    };

    static constexpr std::int64_t kNoNote = -1;

    Visible visible() const;
    void moveTo(std::uint64_t sampleTime, double bpm, double sampleRate);
    Note& noteAt(std::uint64_t seq) noexcept { return notes_[seq % kNotes]; }
    const Note& noteAt(std::uint64_t seq) const noexcept { return notes_[seq % kNotes]; }
    /// Ends the open note of `pitch`, if its ring slot still holds it.
    void closeNote(int pitch);
    void release(int pitch);
    /// The key evaluation tick (class comment): acquire, retain, replace or remove.
    void evaluateKey();
    /// Re-identifies the chord from the held set (latching rule).
    void latchChord();
    /// Recomputes `chordHeld_` from the latched chord and the held set.
    void updateChordHeld();

    std::array<Note, kNotes> notes_{};
    std::uint64_t noteCount_ = 0;  // notes ever started; note `seq` lives in slot `seq % kNotes`
    std::array<std::int64_t, 128> openSeq_{};  // per pitch: sequence of its sounding note, or kNoNote
    std::array<bool, 128> keyDown_{};
    std::array<bool, 128> pedalHeld_{};
    bool pedalDown_ = false;
    bool active_ = false;
    bool hasStamp_ = false;
    std::uint64_t lastStamp_ = 0;
    double now_ = 0.0;
    bool changed_ = false;
    Visible acknowledged_{};
    std::optional<HeardKey> key_;
    int keyMisses_ = 0;  // consecutive ticks on which the shown key failed to retain
    std::optional<HeardChord> chord_;
    bool chordHeld_ = false;
};

}  // namespace chordlab
