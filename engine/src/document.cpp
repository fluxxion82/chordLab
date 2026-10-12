#include "chordlab/document.h"

#include <algorithm>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstddef>
#include <set>
#include <string>
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

int Document::sectionIndexOf(int eventIndex) const {
    assert(eventIndex >= 0 && static_cast<std::size_t>(eventIndex) < events.size());
    int found = -1;
    for (std::size_t i = 0; i < sections.size() && sections[i].firstEventIndex <= eventIndex; ++i) {
        found = static_cast<int>(i);  // sorted: the last start at or before the event wins
    }
    return found;
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

int clampColour(int colourIndex) {
    return std::clamp(colourIndex, 0, kNumSectionColours - 1);
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

/// True when `doc.sections` satisfies the section invariants documented on `Document::sections` and
/// `DocumentStore::replace`. Debug-only, for `replace`'s precondition check.
[[maybe_unused]] bool sectionsWellFormed(const Document& doc) {
    std::set<int> seen;
    int previousIndex = -1;
    for (const Section& s : doc.sections) {
        if (s.firstEventIndex <= previousIndex || static_cast<std::size_t>(s.firstEventIndex) >= doc.events.size() ||
            s.colourIndex < 0 || s.colourIndex >= kNumSectionColours || s.id < 1 || !seen.insert(s.id).second) {
            return false;
        }
        previousIndex = s.firstEventIndex;
    }
    return true;
}

/// One past the largest section id in `doc`, or 1 when it has no sections.
int sectionIdAfter(const Document& doc) {
    int next = 1;
    for (const Section& s : doc.sections) {
        next = std::max(next, s.id + 1);
    }
    return next;
}

/// Position of the event with `id` in `events`, or -1.
int indexOfEvent(const std::vector<ChordEvent>& events, int id) {
    for (std::size_t i = 0; i < events.size(); ++i) {
        if (events[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/// True when `index` is a position of `doc`'s progression.
bool isEventIndex(const Document& doc, int index) {
    return index >= 0 && static_cast<std::size_t>(index) < doc.events.size();
}

/// True when a section of `doc` starts at `index`.
bool sectionStartsAt(const Document& doc, int index) {
    return std::any_of(doc.sections.begin(), doc.sections.end(),
                       [index](const Section& s) { return s.firstEventIndex == index; });
}

/// The section order: by start index. Starts are unique, so this is a total order and `std::sort` is
/// deterministic.
bool sectionLess(const Section& a, const Section& b) {
    return a.firstEventIndex < b.firstEventIndex;
}

/// Re-indexes `sections` for the removal of the chord at `removed` (the section invariants hold on entry
/// and the chord is still present). Sections after it move up one index. The section starting on it is
/// dropped, unless it is the first section, starts at 0, another section follows and the next chord does
/// not already start one: then it stays at 0 on the new first chord.
void reindexSectionsForRemove(std::vector<Section>& sections, int removed) {
    const bool firstInherits = removed == 0 && sections.size() > 1 && sections.front().firstEventIndex == 0 &&
                               sections[1].firstEventIndex > 1;
    for (auto it = sections.begin(); it != sections.end();) {
        if (it->firstEventIndex > removed) {
            --it->firstEventIndex;
            ++it;
        } else if (it->firstEventIndex == removed && !firstInherits) {
            it = sections.erase(it);
        } else {
            ++it;
        }
    }
}

/// Copies the live (non-undoable) fields of `current` into `target`, so that restoring a snapshot keeps
/// the browsed key and the transport toggles as they are now.
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

void DocumentStore::replace(Document doc, int nextId, int nextSectionId) {
    assertValidKey(doc.projectKey);
    assertValidKey(doc.browsedKey);
    assert(nextId >= 1);
    assert(nextSectionId >= 0);
    for ([[maybe_unused]] const ChordEvent& e : doc.events) {
        assertValidChord(e.chord);
        assert(std::isfinite(e.lengthBeats) && e.lengthBeats >= kMinLengthBeats && e.lengthBeats <= kMaxLengthBeats);
        assert(e.velocity >= kMinVelocity && e.velocity <= kMaxVelocity);
        assert(e.id >= 1 && e.id < nextId);
    }
    assert(idsAreDistinct(doc));
    assert(doc.events.size() <= static_cast<std::size_t>(kMaxChords));
    assert(sectionsWellFormed(doc));
    assert(nextSectionId == 0 || nextSectionId >= sectionIdAfter(doc));
    // Nothing below can throw: `doc` arrived fully built, and the moves and clears are noexcept.
    nextSectionId_ = nextSectionId == 0 ? sectionIdAfter(doc) : nextSectionId;
    doc_ = std::move(doc);
    undoStack_.clear();
    redoStack_.clear();
    nextId_ = nextId;
}

int DocumentStore::nextId() const {
    return nextId_;
}

int DocumentStore::nextSectionId() const {
    return nextSectionId_;
}

int DocumentStore::findIndex(int id) const {
    return indexOfEvent(doc_.events, id);
}

int DocumentStore::findSectionIndex(int id) const {
    for (std::size_t i = 0; i < doc_.sections.size(); ++i) {
        if (doc_.sections[i].id == id) {
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
    // Each section is pinned to the chord it starts on: remember that chord, move, then look it up again.
    std::vector<int> startIds;
    startIds.reserve(next.sections.size());
    for (const Section& s : next.sections) {
        startIds.push_back(next.events[static_cast<std::size_t>(s.firstEventIndex)].id);
    }
    auto& events = next.events;
    const auto first = events.begin() + from;
    const auto target = events.begin() + to;
    if (from < to) {
        std::rotate(first, first + 1, target + 1);
    } else {
        std::rotate(target, first, first + 1);
    }
    for (std::size_t i = 0; i < next.sections.size(); ++i) {
        next.sections[i].firstEventIndex = indexOfEvent(next.events, startIds[i]);
        assert(next.sections[i].firstEventIndex >= 0);  // a move loses no chord
    }
    std::sort(next.sections.begin(), next.sections.end(), sectionLess);
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

void DocumentStore::removeChord(int id) {
    const int index = findIndex(id);
    if (index < 0) {
        return;
    }
    Document next = doc_;
    reindexSectionsForRemove(next.sections, index);
    next.events.erase(next.events.begin() + index);
    commit(std::move(next));
}

void DocumentStore::setProjectKey(Key key) {
    assertValidKey(key);
    Document next = doc_;
    next.projectKey = key;
    commit(std::move(next));
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

int DocumentStore::addSection(int firstEventIndex, std::string name) {
    if (nextSectionId_ == INT_MAX || !isEventIndex(doc_, firstEventIndex) || sectionStartsAt(doc_, firstEventIndex)) {
        return kNoSectionId;
    }
    const int id = nextSectionId_;
    Section section{id, name.empty() ? "Section " + std::to_string(id) : std::move(name),
                    (id - 1) % kNumSectionColours, firstEventIndex};
    Document next = doc_;
    next.sections.insert(std::upper_bound(next.sections.begin(), next.sections.end(), section, sectionLess),
                         std::move(section));
    [[maybe_unused]] const bool changed = commit(std::move(next));
    assert(changed);  // a fresh id always makes the list differ
    ++nextSectionId_;  // only now: a throw above leaves the counter, like the rest of the store, untouched
    return id;
}

void DocumentStore::renameSection(int id, std::string name) {
    const int index = findSectionIndex(id);
    if (index < 0) {
        return;
    }
    Document next = doc_;
    next.sections[static_cast<std::size_t>(index)].name = std::move(name);
    commit(std::move(next));
}

void DocumentStore::setSectionColour(int id, int colourIndex) {
    const int index = findSectionIndex(id);
    if (index < 0) {
        return;
    }
    Document next = doc_;
    next.sections[static_cast<std::size_t>(index)].colourIndex = clampColour(colourIndex);
    commit(std::move(next));
}

void DocumentStore::removeSection(int id) {
    const int index = findSectionIndex(id);
    if (index < 0) {
        return;
    }
    Document next = doc_;
    next.sections.erase(next.sections.begin() + index);
    commit(std::move(next));
}

void DocumentStore::moveSectionStart(int id, int newFirstEventIndex) {
    const int index = findSectionIndex(id);
    if (index < 0 || !isEventIndex(doc_, newFirstEventIndex)) {
        return;
    }
    if (doc_.sections[static_cast<std::size_t>(index)].firstEventIndex == newFirstEventIndex ||
        sectionStartsAt(doc_, newFirstEventIndex)) {
        return;  // already there, or another section's start
    }
    Document next = doc_;
    next.sections[static_cast<std::size_t>(index)].firstEventIndex = newFirstEventIndex;
    std::sort(next.sections.begin(), next.sections.end(), sectionLess);
    commit(std::move(next));
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
