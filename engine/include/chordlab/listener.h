#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace chordlab {

/// One normalised incoming MIDI event, as the plug-in's input adapter hands it to the listener (design D1).
/// `kind` is a note-on, a note-off (a note-on with velocity 0 is already turned into one) or a sustain
/// pedal change; channels are merged. For a note `pitch` is the MIDI note (0-127; larger values are
/// ignored by `Listener`) and `velocity` its velocity; for `Sustain` `pitch` is unused and `velocity`
/// carries the CC64 value (0-127; 64 and above is pedal down). `sampleTime` is the audio thread's running
/// sample clock at the event, in samples, the same clock for every event.
struct HeardEvent {
    enum Kind : std::uint8_t { NoteOn, NoteOff, Sustain } kind;
    std::uint8_t pitch;
    std::uint8_t velocity;
    std::uint64_t sampleTime;
};

/// What the plug-in heard (design D4-D10): the notes held right now and a duration-weighted pitch-class
/// histogram of the last 32 beats.
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
/// Visible outputs: `active`, the held set and `onsetsInWindow`. `changed` tells whether any of them
/// differs from the ones recorded by the last `acknowledge`, which is how the shell decides to push the
/// heard state to the UI.
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

    Listener();

    /// Applies one event after moving time to its stamp (see the class comment), at tempo `bpm` and
    /// `sampleRate` Hz. Updates the held set and the notes and sets `changed`. A note event with a pitch
    /// of 128 or more is ignored (it still counts as an event and moves time). Hooks for the key estimate
    /// and chord latching (design D10) belong at the end of this function.
    void feed(const HeardEvent& e, double bpm, double sampleRate);

    /// Moves time to `nowSampleTime` (never backwards) at tempo `bpm` and `sampleRate` Hz and sets
    /// `changed` when a visible output now differs from the acknowledged one. The only evaluation tick for
    /// the key estimate (design D4); it never clears `changed`. An `advance` before any event only sets
    /// the reference stamp and does not make the listener active.
    void advance(std::uint64_t nowSampleTime, double bpm, double sampleRate);

    /// Forgets everything: notes, the held set, the pedal, time and its reference stamp. `active` becomes
    /// false and `changed` true, so the UI is told to hide the group. Used after a lost epoch (a ring
    /// overflow) so no note stays held by a lost note-off.
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

private:
    struct Note {
        std::uint8_t pc;
        double start;
        double end;  // meaningful once `open` is false
        bool open;
    };

    /// The outputs the UI shows, the set `changed` compares. Task 4 adds the key and the chord here.
    struct Visible {
        bool active = false;
        std::array<bool, 128> held{};
        int onsets = 0;
        bool operator==(const Visible& o) const noexcept {
            return active == o.active && held == o.held && onsets == o.onsets;
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
};

}  // namespace chordlab
