#pragma once

#include "chordlab/chord.h"
#include "chordlab/limits.h"
#include "chordlab/pitch.h"
#include "chordlab/scale.h"

#include <cstddef>
#include <deque>
#include <string>
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

/// The most chords a document may hold: `appendChord` refuses the next one (`kNoEventId`), `loadStateJson`
/// rejects a state with more, and `replace` asserts. The bound exists so that a compiled document always
/// fits one scheduler snapshot: kMaxChords chords x at most 8 voices (the Phase 2 voicings; `closeVoicing`
/// today has at most 5, the ninths) x 2 events (on and off) = 4096 = `kMaxSnapshotEvents`. With today's
/// voicings a full document compiles to 2560 events.
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
struct ChordEvent {
    int id;
    Chord chord;
    double lengthBeats;
    int velocity;
};

constexpr bool operator==(const ChordEvent& a, const ChordEvent& b) noexcept {
    return a.id == b.id && a.chord == b.chord && a.lengthBeats == b.lengthBeats && a.velocity == b.velocity;
}

constexpr bool operator!=(const ChordEvent& a, const ChordEvent& b) noexcept {
    return !(a == b);
}

/// A named, coloured stretch of the progression (P2-D6). It starts on the chord at `firstEventIndex` and
/// runs up to the next section's start (or the end of the progression), so a section is never empty.
/// The first section always starts at 0, so when there are sections they partition the whole sequence
/// and every chord is in exactly one; a document without sections behaves as one unnamed section
/// (`compileSection`). `id` is stable for the life of the section and unique within a `DocumentStore`;
/// it survives chord edits, undo and redo, and is never reused after a remove. It is in 1..INT_MAX-1:
/// the store derives its next id as the largest plus one (`replace`), which must not overflow.
/// `colourIndex` is in 0..kNumSectionColours-1.
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

/// The whole editable state of a project: the two keys, the progression, its sections and the transport
/// toggles. Plain data so that `DocumentStore` can snapshot it for undo; mutate it through the store, not
/// directly.
///
/// Undo scope: `events`, `sections` and `projectKey` are undoable (they are what the user composes).
/// `browsedKey`, `progressionLoop` and `followHost` are live session state: changing them never creates
/// history, and undo/redo leave their current values alone while restoring the rest.
struct Document {
    Key projectKey{{'C', 0}, Mode::Ionian};
    Key browsedKey{{'C', 0}, Mode::Ionian};
    std::vector<ChordEvent> events;  // playback order; chords abut, no gaps
    std::vector<Section> sections;   // sorted by firstEventIndex, strictly increasing from 0, each < events.size()
    bool progressionLoop = true;
    bool followHost = true;

    /// Beat at which the event at `index` starts: the sum of the lengths before it. `index` must be in
    /// 0..events.size(); `startBeatOf(events.size())` is `endBeat()`.
    double startBeatOf(int index) const;

    /// Beat at which the progression ends: the sum of every length (0 for an empty progression).
    double endBeat() const;

    /// Position in `sections` of the section the event at `eventIndex` belongs to: the last section that
    /// starts at or before it. -1 when there are no sections (the first section starts at 0, so otherwise
    /// every event is in one). `eventIndex` must be in 0..events.size()-1.
    int sectionIndexOf(int eventIndex) const;
};

/// Compares every field, live state included.
inline bool operator==(const Document& a, const Document& b) noexcept {
    return a.projectKey == b.projectKey && a.browsedKey == b.browsedKey && a.events == b.events &&
           a.sections == b.sections && a.progressionLoop == b.progressionLoop && a.followHost == b.followHost;
}

inline bool operator!=(const Document& a, const Document& b) noexcept {
    return !(a == b);
}

/// True when `doc.sections` satisfies the section invariants: sorted strictly by `firstEventIndex` with
/// the first at 0 and every index a position of the progression (so no sections on an empty one),
/// colours in 0..kNumSectionColours-1, ids in 1..INT_MAX-1 and distinct. What `DocumentStore::replace`
/// asserts, and what a loader taking a document from outside the process must check before calling it.
bool sectionsWellFormed(const Document& doc);

/// Owns a `Document` and its undo/redo history.
///
/// Undoable state is `Document::events`, `Document::sections` and `Document::projectKey`. Every mutation
/// that changes any of them pushes the previous document onto the undo stack and clears the redo stack. A
/// call that changes nothing (an unknown id, a value equal to the current one after clamping, a move to
/// the chord's current position, a rejected argument) pushes nothing and leaves the redo stack intact.
/// `browsedKey`, `progressionLoop` and `followHost` are live state: their setters never touch the history,
/// and `undo()`/`redo()` keep their current values while restoring the undoable state.
/// The history holds at most `kMaxUndoDepth` snapshots; beyond that the oldest is dropped.
/// Event ids are handed out from a counter that starts at `firstId` (1 by default), only ever increases
/// and is not part of the undoable state, so an id is never reused even after undo or redo. Section ids
/// come from a second counter of their own (starting at 1) with the same rules.
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

    /// Appends `chord` to the end of the progression and returns its new id. `chord.type` must be
    /// non-null and `chord.root` in 0..11. `lengthBeats` is clamped to [kMinLengthBeats, kMaxLengthBeats]
    /// and `velocity` to [kMinVelocity, kMaxVelocity]. The sections are untouched: the new chord joins the
    /// last section, if any.
    /// Returns `kNoEventId` and changes nothing (no event, no history, no id consumed) when `lengthBeats`
    /// is NaN or infinite, when the document already holds `kMaxChords` chords, or when the id counter
    /// has reached INT_MAX. The counter advances only once the append is committed.
    int appendChord(Chord chord, double lengthBeats = 4.0, int velocity = 100);

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

    /// Removes event `id`; the events after it move up so the sequence stays contiguous, and so do the
    /// sections starting after it. A section starting on the removed chord is removed with it (its other
    /// chords join the section before it), except the first section, which is pinned to the start: it
    /// stays at 0 on the new first chord, unless that chord already starts a section (then that section
    /// becomes the first) or no chord is left (then no section is). Unknown id: no-op.
    void removeChord(int id);

    /// Undoable. `key.tonic` must satisfy the `Tonic` preconditions of pitch.h and `key.mode` be a
    /// declared `Mode`.
    void setProjectKey(Key key);

    /// Live state, never recorded in the history. Same preconditions as `setProjectKey`.
    void setBrowsedKey(Key key);

    /// Live state, never recorded in the history.
    void setProgressionLoop(bool loop);
    void setFollowHost(bool follow);

    /// Starts a section on the chord at `firstEventIndex` and returns its new id; the list stays sorted.
    /// An empty `name` takes the default "Section <id>"; the colour is the id's turn in the cycle of
    /// `kNumSectionColours` (id 1 gets colour 0). The first section always starts at 0: when the document
    /// has no sections yet and `firstEventIndex` > 0, the chords before it get a section of their own
    /// first (default name and colour, the lower id) in the same undoable step, and the returned id is
    /// the requested section's. Undoable.
    /// Returns `kNoSectionId` and changes nothing (no section, no history, no id consumed) when
    /// `firstEventIndex` is not a position of the progression (so never on an empty document), when a
    /// section already starts there, or when the section id counter cannot supply the one or two ids
    /// needed (it stops at INT_MAX).
    int addSection(int firstEventIndex, std::string name = {});

    /// Sets the name of section `id` to `name`, any string. Unknown id: no-op.
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

    /// Restores the undoable state from before the most recent change, keeping the live state. Returns
    /// false, changing nothing, when there is nothing to undo.
    bool undo();

    /// Reapplies the most recently undone change, keeping the live state. Returns false, changing
    /// nothing, when there is nothing to redo.
    bool redo();

    /// Replaces the whole document, live state included, and restarts the id counters: what a load does.
    /// Clears the undo and redo history and changes nothing else.
    /// `doc` must satisfy the store's invariants (valid chords and keys, finite lengths within
    /// [kMinLengthBeats, kMaxLengthBeats], velocities within [kMinVelocity, kMaxVelocity], distinct ids,
    /// at most `kMaxChords` events; sections as `sectionsWellFormed` requires, in particular ids in
    /// 1..INT_MAX-1) and `nextId` must be >= 1 and greater than every event id, so that ids stay unique.
    /// `nextSectionId` must likewise be >= 1 and greater than every section id, or 0 (the default): one
    /// past the largest section id in `doc` (which the id bound keeps from overflowing), 1 when it has
    /// none; a derived INT_MAX means the next `addSection` is refused. Callers that take documents from
    /// outside the process (`loadStateJson`) validate before calling.
    void replace(Document doc, int nextId, int nextSectionId = 0);

    /// The id the next successful append will get. Persisted by `stateJson` so that a reloaded document
    /// keeps handing out fresh ids.
    int nextId() const;

    /// The id the next successful `addSection` will get. Not persisted yet: `replace` derives it.
    int nextSectionId() const;

    /// Position of event `id` in playback order, or -1 if no event has that id.
    int findIndex(int id) const;

    /// Position of section `id` in `doc().sections`, or -1 if no section has that id.
    int findSectionIndex(int id) const;

private:
    /// Makes `next` the current document if it differs from the current one, recording the current one
    /// for undo (dropping the oldest snapshot past `kMaxUndoDepth`) and clearing the redo stack. Returns
    /// whether anything changed.
    bool commit(Document next);

    Document doc_;
    std::deque<Document> undoStack_;
    std::deque<Document> redoStack_;
    int nextId_;
    int nextSectionId_ = 1;
};

}  // namespace chordlab
