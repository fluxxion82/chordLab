#include "chordlab/document.h"

#include "chordlab/voicing.h"

#include <algorithm>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstddef>
#include <set>
#include <utility>

namespace chordlab {

// --- Document --------------------------------------------------------------------------

double Document::startBeatOf(int index) const {
    assert(index >= 0 && static_cast<std::size_t>(index) <= events.size());
    double beat = 0.0;
    for (int i = 0; i < index; ++i) {
        beat += events[static_cast<std::size_t>(i)].lengthBeats;
    }
    return beat;
}

double Document::endBeat() const {
    return startBeatOf(static_cast<int>(events.size()));
}

// --- DocumentStore ---------------------------------------------------------------------

namespace {

/// `lengthBeats` must be finite: `std::clamp` on NaN is meaningless.
double clampLength(double lengthBeats) {
    assert(std::isfinite(lengthBeats));
    return std::clamp(lengthBeats, kMinLengthBeats, kMaxLengthBeats);
}

int clampVelocity(int velocity) {
    return std::clamp(velocity, kMinVelocity, kMaxVelocity);
}

/// `chord` must be valid: `inversionCount` reads its type.
int clampInversion(const Chord& chord, int inversion) {
    return std::clamp(inversion, 0, inversionCount(chord) - 1);
}

int clampOctave(int octave) {
    return std::clamp(octave, kMinVoicingOctave, kMaxVoicingOctave);
}

/// Checks the `Chord` preconditions documented in chord.h.
void assertValidChord([[maybe_unused]] const Chord& chord) {
    assert(chord.type != nullptr);
    assert(chord.root >= 0 && chord.root < 12);
}

/// Checks the `Key` preconditions documented in pitch.h and scale.h.
void assertValidKey([[maybe_unused]] const Key& key) {
    assert(key.tonic.letter >= 'A' && key.tonic.letter <= 'G');
    assert(key.tonic.alter >= -1 && key.tonic.alter <= 1);
    assert(static_cast<int>(key.mode) >= 0 && static_cast<int>(key.mode) < kNumModes);
}

/// True when no two events of `doc` share an id. Debug-only, for `replace`'s precondition check.
[[maybe_unused]] bool idsAreDistinct(const Document& doc) {
    std::set<int> seen;
    for (const ChordEvent& e : doc.events) {
        if (!seen.insert(e.id).second) {
            return false;
        }
    }
    return true;
}

/// Copies the live (non-undoable) fields of `current` into `target`, so that restoring a snapshot keeps
/// the browsed key and the transport toggles as they are now. `autoVoiceLead` is deliberately absent: it
/// changes the notes `compile` produces, so it is restored with the progression (see `Document`).
void keepLiveState(Document& target, const Document& current) {
    target.browsedKey = current.browsedKey;
    target.progressionLoop = current.progressionLoop;
    target.followHost = current.followHost;
}

}  // namespace

DocumentStore::DocumentStore(int firstId) : nextId_(firstId) {
    assert(firstId >= 1);
}

const Document& DocumentStore::doc() const {
    return doc_;
}

void DocumentStore::replace(Document doc, int nextId) {
    assertValidKey(doc.projectKey);
    assertValidKey(doc.browsedKey);
    assert(nextId >= 1);
    for ([[maybe_unused]] const ChordEvent& e : doc.events) {
        assertValidChord(e.chord);
        assert(std::isfinite(e.lengthBeats) && e.lengthBeats >= kMinLengthBeats && e.lengthBeats <= kMaxLengthBeats);
        assert(e.velocity >= kMinVelocity && e.velocity <= kMaxVelocity);
        assert(e.inversion >= 0 && e.inversion < inversionCount(e.chord));
        assert(e.octave >= kMinVoicingOctave && e.octave <= kMaxVoicingOctave);
        assert(e.id >= 1 && e.id < nextId);
    }
    assert(idsAreDistinct(doc));
    assert(doc.events.size() <= static_cast<std::size_t>(kMaxChords));
    // Nothing below can throw: `doc` arrived fully built, and the moves and clears are noexcept.
    doc_ = std::move(doc);
    undoStack_.clear();
    redoStack_.clear();
    nextId_ = nextId;
}

int DocumentStore::nextId() const {
    return nextId_;
}

int DocumentStore::findIndex(int id) const {
    for (std::size_t i = 0; i < doc_.events.size(); ++i) {
        if (doc_.events[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool DocumentStore::commit(Document next) {
    if (next == doc_) {
        return false;
    }
    // Strong guarantee: the one allocating step comes first, while nothing has changed yet, so if it
    // throws the store is untouched. Everything after it is noexcept (vector moves, deque pop and clear).
    undoStack_.push_back(doc_);
    doc_ = std::move(next);
    if (undoStack_.size() > kMaxUndoDepth) {
        undoStack_.pop_front();
    }
    redoStack_.clear();
    return true;
}

int DocumentStore::appendChord(Chord chord, double lengthBeats, int velocity) {
    assertValidChord(chord);
    if (!std::isfinite(lengthBeats) || nextId_ == INT_MAX ||
        doc_.events.size() >= static_cast<std::size_t>(kMaxChords)) {
        return kNoEventId;
    }
    const int id = nextId_;
    Document next = doc_;
    next.events.push_back(ChordEvent{id, chord, clampLength(lengthBeats), clampVelocity(velocity)});
    [[maybe_unused]] const bool changed = commit(std::move(next));
    assert(changed);  // a fresh id always makes the sequence differ
    ++nextId_;        // only now: a throw above leaves the counter, like the rest of the store, untouched
    return id;
}

void DocumentStore::moveChord(int id, int newIndex) {
    const int from = findIndex(id);
    if (from < 0) {
        return;
    }
    const int to = std::clamp(newIndex, 0, static_cast<int>(doc_.events.size()) - 1);
    if (to == from) {
        return;
    }
    Document next = doc_;
    auto& events = next.events;
    const auto first = events.begin() + from;
    const auto target = events.begin() + to;
    if (from < to) {
        std::rotate(first, first + 1, target + 1);
    } else {
        std::rotate(target, first, first + 1);
    }
    commit(std::move(next));
}

void DocumentStore::setLength(int id, double lengthBeats) {
    const int index = findIndex(id);
    if (index < 0 || !std::isfinite(lengthBeats)) {
        return;
    }
    Document next = doc_;
    next.events[static_cast<std::size_t>(index)].lengthBeats = clampLength(lengthBeats);
    commit(std::move(next));
}

void DocumentStore::setVelocity(int id, int velocity) {
    const int index = findIndex(id);
    if (index < 0) {
        return;
    }
    Document next = doc_;
    next.events[static_cast<std::size_t>(index)].velocity = clampVelocity(velocity);
    commit(std::move(next));
}

void DocumentStore::setInversion(int id, int inversion) {
    const int index = findIndex(id);
    if (index < 0) {
        return;
    }
    Document next = doc_;
    ChordEvent& event = next.events[static_cast<std::size_t>(index)];
    event.inversion = clampInversion(event.chord, inversion);
    event.manualVoicing = true;
    commit(std::move(next));
}

void DocumentStore::setOctave(int id, int octave) {
    const int index = findIndex(id);
    if (index < 0) {
        return;
    }
    Document next = doc_;
    ChordEvent& event = next.events[static_cast<std::size_t>(index)];
    event.octave = clampOctave(octave);
    event.manualVoicing = true;
    commit(std::move(next));
}

void DocumentStore::resetVoicing(int id) {
    const int index = findIndex(id);
    if (index < 0) {
        return;
    }
    Document next = doc_;
    ChordEvent& event = next.events[static_cast<std::size_t>(index)];
    event.inversion = 0;
    event.octave = 0;
    event.manualVoicing = false;
    commit(std::move(next));
}

void DocumentStore::removeChord(int id) {
    const int index = findIndex(id);
    if (index < 0) {
        return;
    }
    Document next = doc_;
    next.events.erase(next.events.begin() + index);
    commit(std::move(next));
}

void DocumentStore::setProjectKey(Key key) {
    assertValidKey(key);
    Document next = doc_;
    next.projectKey = key;
    commit(std::move(next));
}

void DocumentStore::setAutoVoiceLead(bool on) {
    Document next = doc_;
    next.autoVoiceLead = on;
    commit(std::move(next));  // undoable: it changes the compiled progression
}

void DocumentStore::setBrowsedKey(Key key) {
    assertValidKey(key);
    doc_.browsedKey = key;  // live state: no history
}

void DocumentStore::setProgressionLoop(bool loop) {
    doc_.progressionLoop = loop;  // live state: no history
}

void DocumentStore::setFollowHost(bool follow) {
    doc_.followHost = follow;  // live state: no history
}

bool DocumentStore::undo() {
    if (undoStack_.empty()) {
        return false;
    }
    redoStack_.push_back(doc_);  // the allocating step first: if it throws, nothing has changed
    Document& restored = undoStack_.back();
    keepLiveState(restored, doc_);
    doc_ = std::move(restored);
    undoStack_.pop_back();
    return true;
}

bool DocumentStore::redo() {
    if (redoStack_.empty()) {
        return false;
    }
    undoStack_.push_back(doc_);  // the allocating step first: if it throws, nothing has changed
    Document& restored = redoStack_.back();
    keepLiveState(restored, doc_);
    doc_ = std::move(restored);
    redoStack_.pop_back();
    return true;
}

}  // namespace chordlab
