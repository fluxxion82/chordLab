#pragma once

#include "chordlab/chord.h"
#include "chordlab/feel.h"
#include "chordlab/limits.h"
#include "chordlab/meter.h"
#include "chordlab/pitch.h"
#include "chordlab/relatedness.h"
#include "chordlab/scale.h"
#include "chordlab/tracks.h"
#include "chordlab/voicing.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace chordlab {

/// Shortest and longest chord length, in beats. `DocumentStore` clamps every finite length into this range.
constexpr double kMinLengthBeats = 0.25;
constexpr double kMaxLengthBeats = 64.0;

/// Softest and loudest velocity. `DocumentStore` clamps every velocity into this range.
constexpr int kMinVelocity = 1;
constexpr int kMaxVelocity = 127;

/// The id `DocumentStore::appendChord` returns when it appended nothing. No event ever has this id.
constexpr int kNoEventId = -1;

/// How many section colours there are: `DocumentStore` clamps every `Section::colourIndex` into
/// 0..kNumSectionColours-1. Which colour each index is, is the UI's business.
constexpr int kNumSectionColours = 8;

/// The id `DocumentStore::addSection` returns when it added nothing. No section ever has this id.
constexpr int kNoSectionId = -1;

/// Longest section name, in UTF-8 bytes. `DocumentStore::addSection` and `renameSection` cut a longer
/// name down to it at a character boundary; `loadStateJson` and `applyCommandJson` reject a longer one.
/// The bound is what keeps the largest state (`kMaxChords` chords, a section on each) far inside
/// `kMaxJsonBytes` (json_api.h) whatever the names hold; test_json_api.cpp pins that size.
constexpr int kMaxSectionNameBytes = 64;

/// The most chords a document may hold: `appendChord` refuses the next one (`kNoEventId`), `loadStateJson`
/// rejects a state with more, and `replace` asserts. The bound exists so that a compiled document always
/// fits one scheduler snapshot: kMaxChords chords x at most 8 voices (headroom; `voicing` has one note per
/// chord tone, at most 5 today, the ninths) x 2 events (on and off) = 4096 = `kMaxSnapshotEvents`. With
/// today's voicings a full document compiles to 2560 events. A feel (feel.h) that would perform more than
/// `kMaxSnapshotEvents` falls back to these block chords, so the bound holds for every feel.
constexpr int kMaxChords = 256;
static_assert(static_cast<std::size_t>(kMaxChords) * 8 * 2 <= kMaxSnapshotEvents,
              "a full document of eight-note chords must fit one scheduler snapshot");

/// One chord of the progression. There is no start field: the progression is a contiguous sequence (D9),
/// so an event starts where the previous one ends (see `Document::startBeatOf`).
/// `id` is stable for the life of the event and unique within a `DocumentStore`; it survives moves,
/// undo and redo, and is never reused after a remove.
/// `chord` holds only a pitch class and a dictionary type. Note spelling is derived at display time from
/// the browsed key, so whatever enharmonic spelling the chord was entered with (Gb vs F#) is not preserved
/// by the document: this is policy, not an omission.
/// `inversion` (0..`inversionCount(chord) - 1`, 0 = root position) and `octave` (`kMinVoicingOctave`..
/// `kMaxVoicingOctave`, 0 = the default register) are the chord's voicing as `voicing` in voicing.h reads
/// them, and `manualVoicing` says the user chose them. `DocumentStore::setInversion` and `setOctave` set
/// the flag along with the value (root position in the default register is a choice like any other), and
/// `resetVoicing` clears all three, so an event the store was never asked to voice is at 0, 0, not manual
/// until a transpose (`transposeEvents`, transpose.h) moves its `octave` along with its root, flag clear,
/// so that with auto voice-leading off its notes move by exactly the interval.
/// With `Document::autoVoiceLead` on, `compile` chooses the voicing of every chord whose flag is clear
/// except the first chord of the progression, which keeps its stored values like every chord whose flag is
/// set; with it off every chord keeps its stored values, flag or not (see compiler.h).
struct ChordEvent {
    int id;
    Chord chord;
    double lengthBeats;
    int velocity;
    int inversion = 0;
    int octave = 0;
    bool manualVoicing = false;
};

constexpr bool operator==(const ChordEvent& a, const ChordEvent& b) noexcept {
    return a.id == b.id && a.chord == b.chord && a.lengthBeats == b.lengthBeats && a.velocity == b.velocity &&
           a.inversion == b.inversion && a.octave == b.octave && a.manualVoicing == b.manualVoicing;
}

constexpr bool operator!=(const ChordEvent& a, const ChordEvent& b) noexcept {
    return !(a == b);
}

/// One chord for `DocumentStore::insertChords` (strip editing design D1): the chord; its length, the
/// document's new-chord length (`Document::newChordLength`) when absent; its velocity; and a voicing as
/// `ChordEvent` holds it. A given `inversion` or `octave` makes the chord manual (it sounds as given under auto
/// voice-leading) unless `manualVoicing` says otherwise, as `appendChord` does with an inversion;
/// `manualVoicing` alone sets the flag with root position in the default register. The store clamps every
/// value as its setters clamp it.
struct ChordSpec {
    Chord chord;
    std::optional<double> lengthBeats;
    int velocity = 100;
    std::optional<int> inversion;
    std::optional<int> octave;
    std::optional<bool> manualVoicing;
};

/// A named, coloured stretch of the progression (P2-D6). It starts on the chord at `firstEventIndex` and
/// runs up to the next section's start (or the end of the progression), so a section is never empty.
/// The first section always starts at 0, so when there are sections they partition the whole sequence
/// and every chord is in exactly one; a document without sections behaves as one unnamed section
/// (`compileSection`). `id` is stable for the life of the section and unique within a `DocumentStore`;
/// it survives chord edits, undo and redo, and is never reused after a remove. It is in 1..INT_MAX-1:
/// the store derives its next id as the largest plus one (`replace`), which must not overflow.
/// `colourIndex` is in 0..kNumSectionColours-1. `name` is any UTF-8 string of at most
/// `kMaxSectionNameBytes` bytes.
struct Section {
    int id;
    std::string name;
    int colourIndex;
    int firstEventIndex;
};

inline bool operator==(const Section& a, const Section& b) noexcept {
    return a.id == b.id && a.name == b.name && a.colourIndex == b.colourIndex &&
           a.firstEventIndex == b.firstEventIndex;
}

inline bool operator!=(const Section& a, const Section& b) noexcept {
    return !(a == b);
}

/// The whole editable state of a project: the two keys, the progression, its sections, the tracks of notes over
/// it (tracks.h), the voicing flag, the transport toggles, the palette's relation mode, the key-transpose switch
/// and hint flag, the MIDI Thru switch, the new-chord length and the chords lane's mute and solo. Plain data
/// so that `DocumentStore` can snapshot it for undo; mutate it through the store, not directly.
///
/// Undo scope: `events`, `sections`, `tracks`, `projectKey`, `autoVoiceLead`, `feel` and `meter` are undoable (they are what the user
/// composes: the flag changes which notes `compile` produces, so toggling it is an edit of the progression,
/// not a view of it). `browsedKey`, `progressionLoop`, `followHost`, `relationMode`, `transposeWithKey`,
/// `keyHintSeen`, `midiThru`, `newChordBeats`, `chordsMuted` and `chordsSolo` are live session state (each
/// track's own `mute` and `solo` are meant to be too; their setters and undo handling come with them): changing them never creates history, and undo/redo leave their
/// current values alone while restoring the rest, with one exception: a `browsedKey` equal to `projectKey`
/// (home) follows the project key wherever it changes, through `setProjectKey`, `undo` and `redo`, so the
/// palette never stays on a key the project just left.
struct Document {
    Key projectKey{{'C', 0}, Mode::Ionian};
    Key browsedKey{{'C', 0}, Mode::Ionian};
    std::vector<ChordEvent> events;  // playback order; chords abut, no gaps
    std::vector<Section> sections;   // sorted by firstEventIndex, strictly increasing from 0, each < events.size()
    bool autoVoiceLead = false;      // compile picks each non-manual chord's inversion and octave (compiler.h)
    Feel feel;                       // how the chords are played (feel.h); undoable
    Meter meter;                     // time signature, 4/4 by default; undoable
    bool progressionLoop = true;
    bool followHost = true;
    RelationMode relationMode = RelationMode::Blend;  // what sizes the palette cells (relatedness.h, R5)
    bool transposeWithKey = false;  // live, persisted: a tonic change of the project key transposes the sequence
    bool keyHintSeen = false;       // live, persisted: the one-time "chords kept their notes" hint has shown
    bool midiThru = true;           // live, persisted: the plug-in passes the MIDI it hears to its output
    /// live, persisted: how long a chord added without a length lasts, in beats, on the quarter-beat grid
    /// inside [kMinLengthBeats, kMaxLengthBeats] (`snapNewChordBeats`); nullopt: one bar of the meter (D3)
    std::optional<double> newChordBeats;
    /// tracks of notes anchored to the chords (tracks design D1, D3); undoable; at most `kMaxTracks`
    std::vector<Track> tracks;
    bool chordsMuted = false;  // live, persisted: the chords lane is muted (D8)
    bool chordsSolo = false;   // live, persisted: the chords lane is soloed (D8)

    /// Beat at which the event at `index` starts: the sum of the lengths before it. `index` must be in
    /// 0..events.size(); `startBeatOf(events.size())` is `endBeat()`.
    double startBeatOf(int index) const;

    /// Beat at which the progression ends: the sum of every length (0 for an empty progression).
    double endBeat() const;

    /// How long a chord added without a length lasts (`appendChord`, `insertChords`, D3): `newChordBeats` when
    /// set, else one bar of `meter`, `barTicks(meter) / kFeelTicksPerBeat` beats (4 in 4/4, 3 in 3/4 and 6/8,
    /// 3.5 in 7/8). A meter change moves the second, never the first.
    double newChordLength() const;

    /// Position in `sections` of the section the event at `eventIndex` belongs to: the last section that
    /// starts at or before it. -1 when there are no sections (the first section starts at 0, so otherwise
    /// every event is in one). `eventIndex` must be in 0..events.size()-1.
    int sectionIndexOf(int eventIndex) const;
};

/// Compares every field, live state included.
inline bool operator==(const Document& a, const Document& b) noexcept {
    return a.projectKey == b.projectKey && a.browsedKey == b.browsedKey && a.events == b.events &&
           a.sections == b.sections && a.autoVoiceLead == b.autoVoiceLead &&
           a.feel == b.feel && a.meter == b.meter &&
           a.progressionLoop == b.progressionLoop && a.followHost == b.followHost &&
           a.relationMode == b.relationMode && a.transposeWithKey == b.transposeWithKey &&
           a.keyHintSeen == b.keyHintSeen && a.midiThru == b.midiThru && a.newChordBeats == b.newChordBeats &&
           a.tracks == b.tracks && a.chordsMuted == b.chordsMuted && a.chordsSolo == b.chordsSolo;
}

inline bool operator!=(const Document& a, const Document& b) noexcept {
    return !(a == b);
}

/// True when `doc.sections` satisfies the section invariants: sorted strictly by `firstEventIndex` with
/// the first at 0 and every index a position of the progression (so no sections on an empty one),
/// colours in 0..kNumSectionColours-1, ids in 1..INT_MAX-1 and distinct, names of at most
/// `kMaxSectionNameBytes` bytes. What `DocumentStore::replace` asserts, and what a loader taking a
/// document from outside the process must check before calling it.
bool sectionsWellFormed(const Document& doc);

/// `beats` on the new-chord length's grid (D3): rounded to the nearest quarter beat (halves away from zero),
/// then clamped to [kMinLengthBeats, kMaxLengthBeats]: 1.3 is 1.25, 1.375 is 1.5, 0.1 is 0.25, 100 is 64.
/// `beats` must be finite. What `DocumentStore::setNewChordLength` and `loadStateJson` store.
double snapNewChordBeats(double beats);

/// One note for `DocumentStore::insertNotes` (tracks design D4): an absolute start, which the store clamps and
/// then resolves to its owning chord and offset, and a length, pitch and velocity it clamps (D1). 64-bit ticks,
/// so every value clamps in the store without a narrowing step anywhere.
struct NoteSpec {
    int pitch;
    std::int64_t startTick;
    std::int64_t lengthTicks;
    int velocity = 100;
};

/// One note's change for `DocumentStore::updateNotes`: the note `id` and the fields to set, absolute values
/// clamped as `NoteSpec`'s; an absent field keeps the note's value.
struct NoteEdit {
    int id;
    std::optional<int> pitch;
    std::optional<std::int64_t> startTick;
    std::optional<std::int64_t> lengthTicks;
    std::optional<int> velocity;
};

/// How `DocumentStore::appendProgression` / `replaceProgression` would place a batch of chords: after the
/// last chord, or instead of every chord and section.
enum class ProgressionLoad { Append, Replace };

/// Why `DocumentStore::progressionRefusal` says a batch would be refused, `None` when it would be
/// accepted: an empty batch, a NaN or infinite length, more than `kMaxChords` chords in the result, an
/// event id counter that cannot hand out one id per chord, or a section id counter that cannot hand out
/// the one or two section ids the load needs.
enum class ProgressionRefusal { None, Empty, NonFiniteLength, DocumentFull, EventIdsExhausted, SectionIdsExhausted };

/// Owns a `Document` and its undo/redo history.
///
/// Undoable state is `Document::events`, `Document::sections`, `Document::tracks`, `Document::projectKey`,
/// `Document::autoVoiceLead`, `Document::feel` and `Document::meter`. Every mutation that changes any of them pushes the previous document onto the
/// undo stack and clears the redo stack. A call that changes nothing (an unknown id, a value equal to the
/// current one after clamping, a move to the chord's current position, a rejected argument) pushes nothing
/// and leaves the redo stack intact.
/// `browsedKey`, `progressionLoop`, `followHost`, `relationMode`, `transposeWithKey`, `keyHintSeen`,
/// `midiThru` and `newChordBeats` are live state: their setters never touch the history, and `undo()`/`redo()` keep their current values
/// while restoring the undoable state, except that a browsed key equal to the project key (home) follows the
/// project key they restore, as it follows `setProjectKey`, so that a key change, undone or redone, never
/// leaves the palette on the old key. So is the selection (strip editing design D2), which lives in the store
/// rather than the document: an ordered set of event ids in strip order (`selectedEventIds`) with one of them
/// the anchor (`selectedEventId`), kept across edits, undo and redo for as long as each selected event exists
/// (an id whose event goes is dropped; a dropped anchor moves to the nearest selected block that is left, the
/// next one in strip order, else the one before), never restored by undo and never persisted (`stateJson`
/// leaves it out).
/// The history holds at most `kMaxUndoDepth` snapshots; beyond that the oldest is dropped.
/// Event ids are handed out from a counter that starts at `firstId` (1 by default), only ever increases
/// and is not part of the undoable state, so an id is never reused even after undo or redo. Section ids,
/// track ids and note ids (unique across all tracks) come from counters of their own (each starting at 1) with
/// the same rules.
///
/// The content revision (`revision`, tracks design D4) counts changes of the undoable state: it starts at 1 and
/// moves by one after every commit that changed the document (chord, section, key, feel and track edits alike),
/// after every successful `undo` and `redo` and after every `replace`; a refused or no-op call, an undo or redo
/// with nothing to do, a live-state setter and a selection change leave it as it is. It is never reused within
/// a store, so a revision names one document state.
///
/// Track and note edits report a refusal through a trailing `std::string* refusal` (may be null). The string is
/// cleared on entry; on a refusal (invalid arguments, a limit, exhausted ids) the edit returns its failure value
/// (false, an empty vector or `kNoTrackId`), changes nothing (no history, no counter, no revision) and writes a
/// short human-readable reason; on a mere no-op (valid, but nothing would change) it returns false (or the empty
/// vector) and leaves the string empty. So a caller tells "refused" (non-empty reason) from "nothing to do".
///
/// Sections follow the chords they start on, except that the first section is pinned to the start of the
/// sequence: `moveChord` keeps every section on its start chord and re-sorts the list by the new indices,
/// but keeps the first section at 0 when nothing else starts there; `removeChord` moves the sections
/// after the removed chord up one index and drops a section whose start chord went, except the first,
/// which stays on the new first chord. Details on each method.
///
/// Exception safety: every mutation gives the strong guarantee. Each one does all of its allocating (the
/// new document, the history entry) before it changes any member, and finishes with steps that cannot
/// throw, so an allocation failure part-way through leaves the document, both history stacks and the id
/// counters exactly as they were. This holds by construction (see `commit`), not by test.
class DocumentStore {
public:
    /// Most recent changes that can be undone. Older snapshots are discarded.
    static constexpr std::size_t kMaxUndoDepth = 200;

    /// `firstId` is the id the first append will get; it must be >= 1.
    explicit DocumentStore(int firstId = 1);

    const Document& doc() const;

    /// Appends `chord` to the end of the progression and returns its new id: `insertChords` at the end with one
    /// entry, so its rules hold here too. `chord.type` must be non-null and `chord.root` in 0..11. The length is
    /// `lengthBeats`, or `Document::newChordLength()` without one (D3), clamped to [kMinLengthBeats,
    /// kMaxLengthBeats]; `velocity` is clamped to [kMinVelocity, kMaxVelocity]. The sections are untouched: the
    /// new chord joins the last section, if any. The new chord becomes the selection, alone, as the anchor (live
    /// state: an undo of the append drops it, like any removal), so the suggestions and relations read from it.
    /// Without `inversion` the chord is in root position and not manual (auto voice-leading applies). With one
    /// the event gets it, clamped to 0..`inversionCount(chord) - 1`, and `manualVoicing = true`, so it sounds as
    /// given (the inversion, at octave 0) even under auto voice-leading, as after `setInversion`. Returns
    /// `kNoEventId` and changes nothing (no event, no history, no id consumed) when `lengthBeats` is NaN or
    /// infinite, when the document already holds `kMaxChords` chords, or when the id counter has reached
    /// INT_MAX.
    int appendChord(Chord chord, std::optional<double> lengthBeats = std::nullopt, int velocity = 100,
                    std::optional<int> inversion = std::nullopt);

    /// Appends a whole progression, `steps` being each chord with its length, as ONE undoable step, and
    /// starts a section named `sectionName` on its first chord (the preset load of P4/P5, json_api.h's
    /// `loadPreset`). Each chord gets a fresh id, in order, velocity 100, inversion 0, octave 0 and no
    /// manual flag (so auto voice-leading applies), and its length clamped as `appendChord` clamps it.
    /// The new section is made as `addSection` makes one (an empty name takes the default "Section <n>",
    /// a long one is cut to `kMaxSectionNameBytes`, the colour is the id's turn in the cycle). Since the
    /// first section must start at 0, appending onto a non-empty progression that has no sections first
    /// gives the chords already there a section of their own (default name and colour, the lower of the
    /// two ids); onto an empty or an already sectioned progression only the new section is added. Keys,
    /// the voicing flag and live state are untouched, and the loaded chords become the selection, the last of them the anchor.
    /// Returns false and changes nothing (no chord, no section, no history, no id consumed, the selection
    /// as it was) when `progressionRefusal(steps, ProgressionLoad::Append)` is not `None`. Every chord must
    /// satisfy the `Chord` preconditions (chord.h), as for `appendChord`. Both counters advance only once
    /// the load is committed.
    bool appendProgression(const std::vector<std::pair<Chord, double>>& steps, std::string sectionName);

    /// Replaces every chord and section with `steps`, as ONE undoable step (never `replace`, which clears
    /// the history): the chords as `appendProgression` makes them, and one section named `sectionName`
    /// at 0 covering them all, so a single section id is needed. Keys, the voicing flag and live state are
    /// untouched; the loaded chords become the selection, the last of them the anchor (undo drops
    /// them again and restores no selection). Returns false and changes nothing when `progressionRefusal(steps,
    /// ProgressionLoad::Replace)` is not `None`.
    bool replaceProgression(const std::vector<std::pair<Chord, double>>& steps, std::string sectionName);

    /// Why `appendProgression` (`how == Append`) or `replaceProgression` (`Replace`) would refuse `steps`
    /// now, checked in this order: `Empty`; `NonFiniteLength` for any NaN or infinite length;
    /// `DocumentFull` when the result would hold more than `kMaxChords` chords (the current ones plus
    /// `steps` for an append, `steps` alone for a replace); `EventIdsExhausted` when the event id
    /// counter cannot hand out `steps.size()` ids below INT_MAX; `SectionIdsExhausted` when the section
    /// id counter cannot hand out the ids the load needs (two for an append onto a non-empty progression
    /// without sections, one otherwise). `None` means the call would succeed. Lets a caller report the
    /// reason without repeating the rules.
    ProgressionRefusal progressionRefusal(const std::vector<std::pair<Chord, double>>& steps,
                                          ProgressionLoad how) const;

    /// Moves the event `id` so that it sits at `newIndex` in playback order; `newIndex` is clamped to
    /// 0..size-1. Every section keeps starting on the same chord as before (its index is recomputed from
    /// that chord's new position) and the list is re-sorted, so a section whose start chord is moved
    /// travels with it, with one exception: the first section is pinned to the start. When no section's
    /// start chord ends up at index 0 (the first section's own chord moved away, or a chord from inside a
    /// section was moved to the front), the section that was first stays at 0 and takes the new first
    /// chord in. When another section's start chord does end up at 0, whether it was moved there or slid
    /// there because the first chord moved away from in front of it, that section becomes the first and
    /// the old first follows its chord. Unknown id: no-op.
    void moveChord(int id, int newIndex);

    /// Sets the length of event `id`, clamped to [kMinLengthBeats, kMaxLengthBeats]. Unknown id, or a
    /// NaN or infinite `lengthBeats`: no-op.
    void setLength(int id, double lengthBeats);

    /// Sets the velocity of event `id`, clamped to [kMinVelocity, kMaxVelocity]. Unknown id: no-op.
    void setVelocity(int id, int velocity);

    /// Sets the inversion of event `id`, clamped to 0..`inversionCount(chord) - 1`, and marks its voicing
    /// manual, in one undoable step. Unknown id: no-op. The current value on an event already manual: no-op.
    void setInversion(int id, int inversion);

    /// Sets the octave of event `id`, clamped to [kMinVoicingOctave, kMaxVoicingOctave], and marks its
    /// voicing manual, in one undoable step. Unknown id: no-op. The current value on an event already
    /// manual: no-op.
    void setOctave(int id, int octave);

    /// Returns event `id` to root position in the default register and clears its manual flag, so that
    /// auto voice-leading decides for it again. Unknown id, or an event already so: no-op.
    void resetVoicing(int id);

    /// Removes event `id`; the events after it move up so the sequence stays contiguous, and so do the
    /// sections starting after it. A section starting on the removed chord is removed with it (its other
    /// chords join the section before it), except the first section, which is pinned to the start: it
    /// stays at 0 on the new first chord, unless that chord already starts a section (then that section
    /// becomes the first) or no chord is left (then no section is). Unknown id: no-op.
    void removeChord(int id);

    /// Inserts `chords` so that the first lands at `index` (clamped to 0..size) and the rest follow in order, as
    /// ONE undoable step, and returns their new ids in order (fresh and increasing, as `appendChord` hands them
    /// out). Each entry becomes an event as `ChordSpec` says: its length clamped to [kMinLengthBeats,
    /// kMaxLengthBeats] (`Document::newChordLength` without one), its velocity to [kMinVelocity, kMaxVelocity],
    /// its inversion to 0..`inversionCount(chord) - 1`, its octave to [kMinVoicingOctave, kMaxVoicingOctave].
    /// Sections: every section starting at or after `index` moves by `chords.size()`, except the first, which
    /// is pinned at 0; so chords inserted at a section's start end the section before it, and chords inserted
    /// at 0 join the first section. Returns an empty vector and changes nothing (no event, no history, no id
    /// consumed) when `chords` is empty, when any length is NaN or infinite, when the result would hold more
    /// than `kMaxChords` chords, or when the id counter cannot hand out `chords.size()` ids below INT_MAX.
    /// The inserted chords become the selection, the last of them the anchor (live state, set after the commit).
    /// Every chord must satisfy the `Chord` preconditions (chord.h). The counter advances only once the insert
    /// is committed.
    std::vector<int> insertChords(int index, const std::vector<ChordSpec>& chords);

    /// Removes every chord of `ids` that exists, as ONE undoable step: in index order, each with
    /// `removeChord`'s section rules (so when several start chords go, the order decides which section is left
    /// at 0, exactly as successive `removeChord` calls from the front would). Ids no event has are skipped and a
    /// repeated id counts once. Returns false and pushes nothing when no listed id exists.
    bool removeChords(const std::vector<int>& ids);

    /// Sets the length of every chord of `ids` that exists to `lengthBeats`, clamped as `setLength` clamps it,
    /// as ONE undoable step; unknown ids are skipped. Returns false and pushes nothing when `lengthBeats` is NaN
    /// or infinite, or when no length changed (no listed id exists, or each already has the clamped value).
    bool setLengths(const std::vector<int>& ids, double lengthBeats);

    /// Undoable. `key.tonic` must satisfy the `Tonic` preconditions of pitch.h and `key.mode` be a
    /// declared `Mode`. When `Document::transposeWithKey` is on and the tonic's pitch class changes (a
    /// mode-only change or an enharmonic respelling does not), every chord of the sequence moves by the
    /// interval between the two tonics in the same commit, so key and chords are one undo step: the
    /// direction is `chooseKeyTranspose`'s, the move `transposeEvents`' (transpose.h). A browsed key equal
    /// to the current project key (home) becomes `key` too, so the palette follows the project; any other
    /// browsed key is left alone. Sections, ids, the selection and the id counters are untouched.
    void setProjectKey(Key key);

    /// Moves every chord from `fromTonic` (a pitch class; reduced modulo 12) to the current project key's
    /// tonic, as a key change with the switch on would (direction by `chooseKeyTranspose`), as ONE
    /// undoable step. The key itself does not change. Returns false and changes nothing (no history)
    /// when the sequence is empty or the two tonics are the same pitch class. What the one-time hint's
    /// "Move once" does after a tonic change made with the switch off.
    bool transposeFromKey(PitchClass fromTonic);

    /// Moves every chord by `semitones` (-11..11, signed, exactly as given: no direction choice) as ONE
    /// undoable step; see `transposeEvents` (transpose.h) for how the octaves follow. The key does not
    /// change. Returns false and changes nothing (no history) when the sequence is empty, `semitones` is
    /// 0 or outside -11..11.
    bool transposeProgression(int semitones);

    /// Undoable: the flag changes what `compile` produces (see `Document`).
    void setAutoVoiceLead(bool on);

    /// Undoable: the feel changes how the progression is played. An equal value pushes no history.
    void setFeel(Feel feel);

    /// Undoable. `meter` must satisfy `validMeter`. An equal value pushes no history.
    void setMeter(Meter meter);

    /// Live state, never recorded in the history. Same preconditions as `setProjectKey`.
    void setBrowsedKey(Key key);

    /// Live state, never recorded in the history.
    void setProgressionLoop(bool loop);
    void setFollowHost(bool follow);

    /// Live state, never recorded in the history: which relation sizes the palette cells (relatedness.h).
    /// Persisted by `stateJson`. `mode` must be a declared `RelationMode`.
    void setRelationMode(RelationMode mode);

    /// Live state, never recorded in the history. Persisted by `stateJson`. Whether `setProjectKey`
    /// transposes the sequence when the tonic changes.
    void setTransposeWithKey(bool on);

    /// Live state, never recorded in the history. Persisted by `stateJson`. Whether the one-time hint
    /// offering to move the chords after a tonic change has been shown in this project.
    void setKeyHintSeen(bool seen);

    /// Live state, never recorded in the history. Persisted by `stateJson`. Whether the plug-in passes the
    /// MIDI it hears through to its output (on by default); the audio thread reads it from the snapshot
    /// slot (publication.h).
    void setMidiThru(bool on);

    /// Live state, never recorded in the history and kept by undo and redo; persisted by `stateJson` (D3): the
    /// length of a chord added without one. A value is stored as `snapNewChordBeats` gives it; nullopt returns
    /// to one bar of the meter. A NaN or infinite value is a no-op. A meter change leaves it alone.
    void setNewChordLength(std::optional<double> lengthBeats);

    /// The anchor of the selection (D2): the block the suggestions, the palette relations and the snapshot's
    /// `selectedEventId` follow, or `kNoEventId` when nothing is selected. Always one of `selectedEventIds()`
    /// when that is not empty. Live state (see the class comment): never restored by undo and not persisted.
    int selectedEventId() const;

    /// Every selected event's id, in strip (index) order, without repeats; empty when nothing is selected. Kept
    /// across edits, undo and redo for as long as each event exists: an id whose event goes is dropped, and when
    /// it was the anchor the anchor moves to the nearest selected block that is left, the next one in strip
    /// order as it was before the change, else the one before, else none.
    const std::vector<int>& selectedEventIds() const;

    /// Selects event `id` alone, as the anchor; `kNoEventId` clears the selection. An id no event has is a
    /// no-op (the UI may name an event that an edit just removed). Never recorded in the history.
    void setSelectedEvent(int id);

    /// Selects the events of `ids` that exist (unknown ids dropped, repeats collapsed), in strip order, with
    /// `anchor` the anchor when it is one of them, else the last of them in strip order. An empty result (an
    /// empty `ids`, or none that exists) clears the selection. Never recorded in the history.
    void setSelectedEvents(const std::vector<int>& ids, int anchor = kNoEventId);

    /// Starts a section on the chord at `firstEventIndex` and returns its new id; the list stays sorted.
    /// An empty `name` takes the default "Section <n>", n the number of sections there are once it is added,
    /// moved up past a name a section already has (so a document whose sections came and went shows no gap
    /// and no repeat); a name longer than `kMaxSectionNameBytes` bytes
    /// is cut down to the bound at a UTF-8 character boundary (a character straddling it is dropped
    /// whole); the colour is the id's turn in the cycle of
    /// `kNumSectionColours` (id 1 gets colour 0). The first section always starts at 0: when the document
    /// has no sections yet and `firstEventIndex` > 0, the chords before it get a section of their own
    /// first (default name and colour, the lower id) in the same undoable step, and the returned id is
    /// the requested section's. Undoable.
    /// Returns `kNoSectionId` and changes nothing (no section, no history, no id consumed) when
    /// `firstEventIndex` is not a position of the progression (so never on an empty document), when a
    /// section already starts there, or when the section id counter cannot supply the one or two ids
    /// needed (it stops at INT_MAX).
    int addSection(int firstEventIndex, std::string name = {});

    /// Sets the name of section `id` to `name`, any UTF-8 string, cut down to `kMaxSectionNameBytes`
    /// bytes at a character boundary as `addSection` does. Unknown id, or a name that cuts down to the
    /// current one: no-op.
    void renameSection(int id, std::string name);

    /// Sets the colour of section `id`, clamped to 0..kNumSectionColours-1. Unknown id: no-op.
    void setSectionColour(int id, int colourIndex);

    /// Removes section `id`; its chords stay and join the section before it. For the first section they
    /// join the next one instead, which then starts at 0 (the last section's removal leaves none).
    /// Unknown id: no-op.
    void removeSection(int id);

    /// Moves the start of section `id` to the chord at `newFirstEventIndex`; the list is re-sorted.
    /// No-op when the id is unknown, when the index is not a position of the progression, when another
    /// section already starts there, or when `id` is the first section, which is pinned to index 0.
    void moveSectionStart(int id, int newFirstEventIndex);

    /// Adds an empty track with `role` at the end of the list and returns its new id, as one undoable step (tracks
    /// design D4). An absent or empty `name` takes `defaultTrackName(doc, role)`; a longer name than
    /// `kMaxTrackNameBytes` is cut at a UTF-8 character boundary as `addSection` cuts one. A track needs no chords.
    /// Refused (`kNoTrackId`, see the class comment) when the document already holds `kMaxTracks` tracks or the
    /// track id counter has reached INT_MAX. `role` must be a declared `TrackRole`.
    int addTrack(TrackRole role, std::optional<std::string> name = std::nullopt, std::string* refusal = nullptr);

    /// Removes track `id` with its notes, as one undoable step. Refused (false) for an unknown id.
    bool removeTrack(int id, std::string* refusal = nullptr);

    /// Sets the name and/or the role of track `id` in ONE candidate document and one undoable step. The name is
    /// cut as `addTrack` cuts it; an empty name is stored empty, exactly as `renameSection` stores one (no
    /// default is substituted: that happens only when a track is added). Refused (false) for an unknown id or
    /// when neither field is given; a no-op (false, no reason) when the result equals the current track.
    bool patchTrack(int id, std::optional<std::string> name, std::optional<TrackRole> role,
                    std::string* refusal = nullptr);

    /// Moves track `id` to `newIndex`, clamped to 0..size-1 (as `moveChord`), as one undoable step. Refused
    /// (false) for an unknown id; a no-op (false, no reason) when it is already there.
    bool moveTrack(int id, int newIndex, std::string* refusal = nullptr);

    /// Inserts `specs` into track `trackId` as ONE undoable step and returns their new ids in spec order (fresh
    /// and increasing). Each spec is clamped in D1's order (pitch and velocity; the start into [0, kMaxNoteTick -
    /// kMinTrackNoteTicks] first; then the length against the space the start leaves) and its clamped start is
    /// resolved to the owning chord and offset (`resolveStart`: half-open, a boundary tick belongs to the next
    /// chord); the track's notes are then re-sorted by (derived start, pitch, id). Refused whole (empty vector, no
    /// id consumed) for an unknown track, empty `specs`, more than `kMaxNotesPerTrack` notes in the track
    /// afterwards, a note id counter that cannot hand out `specs.size()` ids below INT_MAX, an empty
    /// progression ("Add chords first"), or any start no chord owns (at or after the end).
    std::vector<int> insertNotes(int trackId, const std::vector<NoteSpec>& specs, std::string* refusal = nullptr);

    /// Removes the notes of `ids` that track `trackId` holds, as ONE undoable step; ids it does not hold are
    /// skipped and a repeat counts once. Refused (false) for an unknown track; a no-op (false, no reason) when
    /// none of `ids` is in the track.
    bool removeNotes(int trackId, const std::vector<int>& ids, std::string* refusal = nullptr);

    /// Applies `edits` to track `trackId`'s notes as ONE undoable step: each given field is an absolute value,
    /// clamped as `insertNotes` clamps it. A new start is clamped and resolved again, so moving a note (or its left
    /// edge) across a chord boundary changes its anchor; the length, given or kept, is then clamped against the
    /// note's start (the new one, else its current derived start). The notes are re-sorted afterwards. Refused
    /// whole (false) for an unknown track, an id the track does not hold, an id listed twice (two edits of one
    /// note would make the result depend on their order, so the call is ambiguous), or a new start no chord owns
    /// ("Add chords first" when the progression is empty). A no-op (false, no reason) when nothing changes,
    /// `edits` empty included. Every note edited must anchor an existing chord.
    bool updateNotes(int trackId, const std::vector<NoteEdit>& edits, std::string* refusal = nullptr);

    /// Restores the undoable state from before the most recent change, keeping the live state (a browsed
    /// key that is the project key stays so: it follows the restored key). Returns false, changing nothing,
    /// when there is nothing to undo.
    bool undo();

    /// Reapplies the most recently undone change, keeping the live state (the browsed key as `undo`).
    /// Returns false, changing nothing, when there is nothing to redo.
    bool redo();

    /// Replaces the whole document, live state included, and restarts the id counters: what a load does.
    /// Clears the undo and redo history and the selection (a loaded document starts unselected, even
    /// when it holds an event with the selected id) and changes nothing else.
    /// `doc` must satisfy the store's invariants (valid chords and keys, finite lengths within
    /// [kMinLengthBeats, kMaxLengthBeats], velocities within [kMinVelocity, kMaxVelocity], inversions
    /// within 0..`inversionCount(chord) - 1`, octaves within [kMinVoicingOctave, kMaxVoicingOctave],
    /// `newChordBeats` absent or a value `snapNewChordBeats` leaves unchanged,
    /// distinct ids, at most `kMaxChords` events; sections as `sectionsWellFormed` requires, in particular
    /// ids in 1..INT_MAX-1) and `nextId` must be >= 1 and greater than every event id, so that ids stay
    /// unique. `nextSectionId` must likewise be >= 1 and greater than every section id, or 0 (the
    /// default): one past the largest section id in `doc` (which the id bound keeps from overflowing), 1
    /// when it has none; a derived INT_MAX means the next `addSection` is refused. Callers that take
    /// documents from outside the process (`loadStateJson`) validate before calling.
    /// `nextTrackId` and `nextNoteId` follow `nextSectionId`'s rule: >= 1 and greater than every track id
    /// (respectively every note id of every track), or 0 to derive one past the largest (1 when there is none).
    /// Track and note ids must be in 1..INT_MAX-1 and unique (note ids across all tracks), and every note must
    /// anchor an existing chord. Moves the revision.
    void replace(Document doc, int nextId, int nextSectionId = 0, int nextTrackId = 0, int nextNoteId = 0);

    /// The id the next successful append will get. Persisted by `stateJson` so that a reloaded document
    /// keeps handing out fresh ids.
    int nextId() const;

    /// The id the next successful `addSection` will get. Persisted by `stateJson` like `nextId`; `replace`
    /// derives it when the state carries none.
    int nextSectionId() const;

    /// The id the next successful `addTrack` will get; `replace` derives it when the state carries none.
    int nextTrackId() const;

    /// The id the next note `insertNotes` hands out; `replace` derives it when the state carries none.
    int nextNoteId() const;

    /// The content revision (see the class comment): 1 for a new store.
    std::uint64_t revision() const;

    /// Position of event `id` in playback order, or -1 if no event has that id.
    int findIndex(int id) const;

    /// Position of section `id` in `doc().sections`, or -1 if no section has that id.
    int findSectionIndex(int id) const;

    /// Position of track `id` in `doc().tracks`, or -1 if no track has that id.
    int findTrackIndex(int id) const;

private:
    /// Makes `next` the current document if it differs from the current one, recording the current one
    /// for undo (dropping the oldest snapshot past `kMaxUndoDepth`), clearing the redo stack and moving the
    /// revision. Returns whether anything changed.
    bool commit(Document next);

    /// The one body of `appendProgression` and `replaceProgression`: checks `progressionRefusal`, builds
    /// the next document (events and sections) and commits it once.
    bool loadProgression(const std::vector<std::pair<Chord, double>>& steps, std::string sectionName,
                         ProgressionLoad how);

    /// Drops the selected ids no event of the current document has, moving a dropped anchor as
    /// `selectedEventIds` says, and re-sorts the rest into the document's strip order. Called after every change
    /// of `doc_` (a commit, an undo, a redo); `replace` clears unconditionally.
    void dropStaleSelection() noexcept;

    Document doc_;
    std::deque<Document> undoStack_;
    std::deque<Document> redoStack_;
    int nextId_;
    int nextSectionId_ = 1;
    int nextTrackId_ = 1;
    int nextNoteId_ = 1;
    std::uint64_t revision_ = 1;
    std::vector<int> selectedEventIds_;  // strip order; empty: nothing selected
    int anchorEventId_ = kNoEventId;     // one of selectedEventIds_, or kNoEventId when it is empty
};

}  // namespace chordlab
