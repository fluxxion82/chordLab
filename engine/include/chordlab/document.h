#pragma once

#include "chordlab/chord.h"
#include "chordlab/pitch.h"
#include "chordlab/scale.h"

#include <vector>

namespace chordlab {

/// Shortest and longest chord length, in beats. `DocumentStore` clamps every length into this range.
constexpr double kMinLengthBeats = 0.25;
constexpr double kMaxLengthBeats = 64.0;

/// Softest and loudest velocity. `DocumentStore` clamps every velocity into this range.
constexpr int kMinVelocity = 1;
constexpr int kMaxVelocity = 127;

/// One chord of the progression. There is no start field: the progression is a contiguous sequence (D9),
/// so an event starts where the previous one ends (see `Document::startBeatOf`).
/// `id` is stable for the life of the event and unique within a `DocumentStore`; it survives moves,
/// undo and redo, and is never reused after a remove.
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

/// The whole editable state of a project: the two keys, the progression and the transport toggles.
/// Plain data so that `DocumentStore` can snapshot it for undo; mutate it through the store, not directly.
struct Document {
    Key projectKey{{'C', 0}, Mode::Ionian};
    Key browsedKey{{'C', 0}, Mode::Ionian};
    std::vector<ChordEvent> events;  // playback order; chords abut, no gaps
    bool progressionLoop = true;
    bool followHost = true;

    /// Beat at which the event at `index` starts: the sum of the lengths before it. `index` must be in
    /// 0..events.size(); `startBeatOf(events.size())` is `endBeat()`.
    double startBeatOf(int index) const;

    /// Beat at which the progression ends: the sum of every length (0 for an empty progression).
    double endBeat() const;
};

inline bool operator==(const Document& a, const Document& b) noexcept {
    return a.projectKey == b.projectKey && a.browsedKey == b.browsedKey && a.events == b.events &&
           a.progressionLoop == b.progressionLoop && a.followHost == b.followHost;
}

inline bool operator!=(const Document& a, const Document& b) noexcept {
    return !(a == b);
}

/// Owns a `Document` and its undo/redo history.
///
/// Every mutation that changes the document pushes the previous document onto the undo stack and clears
/// the redo stack. A call that changes nothing (an unknown id, a value equal to the current one after
/// clamping, a move to the chord's current position) pushes nothing and leaves the redo stack intact.
/// Event ids are handed out from a counter that starts at 1, only ever increases and is not part of the
/// undoable state, so an id is never reused even after undo or redo.
class DocumentStore {
public:
    const Document& doc() const;

    /// Appends `chord` to the end of the progression and returns its new id. `chord.type` must be
    /// non-null and `chord.root` in 0..11. `lengthBeats` is clamped to [kMinLengthBeats, kMaxLengthBeats]
    /// and `velocity` to [kMinVelocity, kMaxVelocity].
    int appendChord(Chord chord, double lengthBeats = 4.0, int velocity = 100);

    /// Moves the event `id` so that it sits at `newIndex` in playback order; `newIndex` is clamped to
    /// 0..size-1. Unknown id: no-op.
    void moveChord(int id, int newIndex);

    /// Sets the length of event `id`, clamped to [kMinLengthBeats, kMaxLengthBeats]. Unknown id: no-op.
    void setLength(int id, double lengthBeats);

    /// Sets the velocity of event `id`, clamped to [kMinVelocity, kMaxVelocity]. Unknown id: no-op.
    void setVelocity(int id, int velocity);

    /// Removes event `id`; the events after it move up so the sequence stays contiguous. Unknown id: no-op.
    void removeChord(int id);

    /// `key.tonic` must satisfy the `Tonic` preconditions of pitch.h and `key.mode` be a declared `Mode`.
    void setProjectKey(Key key);
    void setBrowsedKey(Key key);

    void setProgressionLoop(bool loop);
    void setFollowHost(bool follow);

    /// Restores the document from before the most recent change. Returns false, changing nothing, when
    /// there is nothing to undo.
    bool undo();

    /// Reapplies the most recently undone change. Returns false, changing nothing, when there is nothing
    /// to redo.
    bool redo();

    /// Position of event `id` in playback order, or -1 if no event has that id.
    int findIndex(int id) const;

private:
    /// Makes `next` the current document if it differs from the current one, recording the current one
    /// for undo and clearing the redo stack. Returns whether anything changed.
    bool commit(Document next);

    Document doc_;
    std::vector<Document> undoStack_;
    std::vector<Document> redoStack_;
    int nextId_ = 1;
};

}  // namespace chordlab
