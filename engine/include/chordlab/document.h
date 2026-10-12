#pragma once

#include "chordlab/chord.h"
#include "chordlab/limits.h"
#include "chordlab/pitch.h"
#include "chordlab/scale.h"
#include "chordlab/voicing.h"

#include <cstddef>
#include <deque>
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

/// The most chords a document may hold: `appendChord` refuses the next one (`kNoEventId`), `loadStateJson`
/// rejects a state with more, and `replace` asserts. The bound exists so that a compiled document always
/// fits one scheduler snapshot: kMaxChords chords x at most 8 voices (headroom; `voicing` has one note per
/// chord tone, at most 5 today, the ninths) x 2 events (on and off) = 4096 = `kMaxSnapshotEvents`. With
/// today's voicings a full document compiles to 2560 events.
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
/// `resetVoicing` clears all three, so an event the store was never asked to voice is at 0, 0, not manual.
/// With `Document::autoVoiceLead` on, `compile` chooses the voicing of every chord whose flag is clear and
/// keeps the stored values of every chord whose flag is set; with it off every chord keeps its stored
/// values, flag or not (see compiler.h).
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

/// The whole editable state of a project: the two keys, the progression, the voicing flag and the
/// transport toggles. Plain data so that `DocumentStore` can snapshot it for undo; mutate it through the
/// store, not directly.
///
/// Undo scope: `events`, `projectKey` and `autoVoiceLead` are undoable (they are what the user composes:
/// the flag changes which notes `compile` produces, so toggling it is an edit of the progression, not a
/// view of it). `browsedKey`, `progressionLoop` and `followHost` are live session state: changing them
/// never creates history, and undo/redo leave their current values alone while restoring the rest.
struct Document {
    Key projectKey{{'C', 0}, Mode::Ionian};
    Key browsedKey{{'C', 0}, Mode::Ionian};
    std::vector<ChordEvent> events;  // playback order; chords abut, no gaps
    bool autoVoiceLead = false;      // compile picks each non-manual chord's inversion and octave (compiler.h)
    bool progressionLoop = true;
    bool followHost = true;

    /// Beat at which the event at `index` starts: the sum of the lengths before it. `index` must be in
    /// 0..events.size(); `startBeatOf(events.size())` is `endBeat()`.
    double startBeatOf(int index) const;

    /// Beat at which the progression ends: the sum of every length (0 for an empty progression).
    double endBeat() const;
};

/// Compares every field, live state included.
inline bool operator==(const Document& a, const Document& b) noexcept {
    return a.projectKey == b.projectKey && a.browsedKey == b.browsedKey && a.events == b.events &&
           a.autoVoiceLead == b.autoVoiceLead && a.progressionLoop == b.progressionLoop &&
           a.followHost == b.followHost;
}

inline bool operator!=(const Document& a, const Document& b) noexcept {
    return !(a == b);
}

/// Owns a `Document` and its undo/redo history.
///
/// Undoable state is `Document::events`, `Document::projectKey` and `Document::autoVoiceLead`. Every
/// mutation that changes any of them pushes the previous document onto the undo stack and clears the redo
/// stack. A call that changes nothing
/// (an unknown id, a value equal to the current one after clamping, a move to the chord's current
/// position, a rejected argument) pushes nothing and leaves the redo stack intact.
/// `browsedKey`, `progressionLoop` and `followHost` are live state: their setters never touch the history,
/// and `undo()`/`redo()` keep their current values while restoring the undoable state.
/// The history holds at most `kMaxUndoDepth` snapshots; beyond that the oldest is dropped.
/// Event ids are handed out from a counter that starts at `firstId` (1 by default), only ever increases
/// and is not part of the undoable state, so an id is never reused even after undo or redo.
///
/// Exception safety: every mutation gives the strong guarantee. Each one does all of its allocating (the
/// new document, the history entry) before it changes any member, and finishes with steps that cannot
/// throw, so an allocation failure part-way through leaves the document, both history stacks and the id
/// counter exactly as they were. This holds by construction (see `commit`), not by test.
class DocumentStore {
public:
    /// Most recent changes that can be undone. Older snapshots are discarded.
    static constexpr std::size_t kMaxUndoDepth = 200;

    /// `firstId` is the id the first append will get; it must be >= 1.
    explicit DocumentStore(int firstId = 1);

    const Document& doc() const;

    /// Appends `chord` to the end of the progression and returns its new id. `chord.type` must be
    /// non-null and `chord.root` in 0..11. `lengthBeats` is clamped to [kMinLengthBeats, kMaxLengthBeats]
    /// and `velocity` to [kMinVelocity, kMaxVelocity].
    /// Returns `kNoEventId` and changes nothing (no event, no history, no id consumed) when `lengthBeats`
    /// is NaN or infinite, when the document already holds `kMaxChords` chords, or when the id counter
    /// has reached INT_MAX. The counter advances only once the append is committed.
    int appendChord(Chord chord, double lengthBeats = 4.0, int velocity = 100);

    /// Moves the event `id` so that it sits at `newIndex` in playback order; `newIndex` is clamped to
    /// 0..size-1. Unknown id: no-op.
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

    /// Removes event `id`; the events after it move up so the sequence stays contiguous. Unknown id: no-op.
    void removeChord(int id);

    /// Undoable. `key.tonic` must satisfy the `Tonic` preconditions of pitch.h and `key.mode` be a
    /// declared `Mode`.
    void setProjectKey(Key key);

    /// Undoable: the flag changes what `compile` produces (see `Document`).
    void setAutoVoiceLead(bool on);

    /// Live state, never recorded in the history. Same preconditions as `setProjectKey`.
    void setBrowsedKey(Key key);

    /// Live state, never recorded in the history.
    void setProgressionLoop(bool loop);
    void setFollowHost(bool follow);

    /// Restores the undoable state from before the most recent change, keeping the live state. Returns
    /// false, changing nothing, when there is nothing to undo.
    bool undo();

    /// Reapplies the most recently undone change, keeping the live state. Returns false, changing
    /// nothing, when there is nothing to redo.
    bool redo();

    /// Replaces the whole document, live state included, and restarts the id counter at `nextId`: what a
    /// load does. Clears the undo and redo history and changes nothing else.
    /// `doc` must satisfy the store's invariants (valid chords and keys, finite lengths within
    /// [kMinLengthBeats, kMaxLengthBeats], velocities within [kMinVelocity, kMaxVelocity], inversions
    /// within 0..`inversionCount(chord) - 1`, octaves within [kMinVoicingOctave, kMaxVoicingOctave],
    /// distinct ids, at most `kMaxChords` events) and `nextId` must be >= 1 and greater than every event
    /// id, so that ids stay unique. Callers that take documents from outside the process
    /// (`loadStateJson`) validate before calling.
    void replace(Document doc, int nextId);

    /// The id the next successful append will get. Persisted by `stateJson` so that a reloaded document
    /// keeps handing out fresh ids.
    int nextId() const;

    /// Position of event `id` in playback order, or -1 if no event has that id.
    int findIndex(int id) const;

private:
    /// Makes `next` the current document if it differs from the current one, recording the current one
    /// for undo (dropping the oldest snapshot past `kMaxUndoDepth`) and clearing the redo stack. Returns
    /// whether anything changed.
    bool commit(Document next);

    Document doc_;
    std::deque<Document> undoStack_;
    std::deque<Document> redoStack_;
    int nextId_;
};

}  // namespace chordlab
