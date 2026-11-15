#include "chordlab/document.h"

#include "chordlab/transpose.h"
#include "chordlab/voicing.h"

#include <algorithm>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <set>
#include <string>
#include <unordered_map>
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

double Document::newChordLength() const {
    return newChordBeats.value_or(barTicks(meter) / static_cast<double>(kFeelTicksPerBeat));
}

int Document::sectionIndexOf(int eventIndex) const {
    assert(eventIndex >= 0 && static_cast<std::size_t>(eventIndex) < events.size());
    int found = -1;  // only when there are no sections: the first section starts at 0
    for (std::size_t i = 0; i < sections.size() && sections[i].firstEventIndex <= eventIndex; ++i) {
        found = static_cast<int>(i);  // sorted: the last start at or before the event wins
    }
    return found;
}

bool sectionsWellFormed(const Document& doc) {
    if (!doc.sections.empty() && doc.sections.front().firstEventIndex != 0) {
        return false;
    }
    std::set<int> seen;
    int previousIndex = -1;
    for (const Section& s : doc.sections) {
        if (s.firstEventIndex <= previousIndex || static_cast<std::size_t>(s.firstEventIndex) >= doc.events.size() ||
            s.colourIndex < 0 || s.colourIndex >= kNumSectionColours || s.id < 1 || s.id >= INT_MAX ||
            !seen.insert(s.id).second || s.name.size() > static_cast<std::size_t>(kMaxSectionNameBytes)) {
            return false;
        }
        previousIndex = s.firstEventIndex;
    }
    return true;
}

double snapNewChordBeats(double beats) {
    assert(std::isfinite(beats));
    return std::clamp(std::round(beats * 4.0) / 4.0, kMinLengthBeats, kMaxLengthBeats);
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

/// `value` reduced into 0..11 (also for negative values).
int wrapPitchClass(int value) {
    return ((value % 12) + 12) % 12;
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

/// One past the largest section id in `doc`, or 1 when it has no sections. `doc` must satisfy
/// `sectionsWellFormed`, whose id bound keeps the sum from overflowing.
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

/// Re-indexes `sections` for the removal of the chord at `removed` from a progression of `eventCount`
/// chords (the section invariants hold on entry and the chord is still present). Sections after it move
/// up one index. The section starting on it is dropped, except the first section (at 0), which is pinned
/// to the start: it stays there on the new first chord, unless there is none (the last chord goes) or
/// that chord already starts a section (which then becomes the first).
void reindexSectionsForRemove(std::vector<Section>& sections, int removed, int eventCount) {
    const bool nextStartsSection = sections.size() > 1 && sections[1].firstEventIndex == 1;
    const bool firstStays = removed == 0 && eventCount > 1 && !nextStartsSection;
    for (auto it = sections.begin(); it != sections.end();) {
        if (it->firstEventIndex > removed) {
            --it->firstEventIndex;
            ++it;
        } else if (it->firstEventIndex == removed && !firstStays) {
            it = sections.erase(it);
        } else {
            ++it;
        }
    }
}

/// `name` cut down to `bound` bytes without splitting a UTF-8 character: the cut backs off over continuation
/// bytes (10xxxxxx) to the start of the character that straddles the bound, which is then dropped whole. A
/// name within the bound is returned as it is. Section and track names share it.
std::string boundedName(std::string name, int maxBytes) {
    const std::size_t bound = static_cast<std::size_t>(maxBytes);
    if (name.size() <= bound) {
        return name;
    }
    std::size_t cut = bound;
    while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xC0) == 0x80) {
        --cut;
    }
    name.resize(cut);
    return name;
}

/// `name` cut down to `kMaxSectionNameBytes` bytes (`boundedName`).
std::string boundedSectionName(std::string name) {
    return boundedName(std::move(name), kMaxSectionNameBytes);
}

/// A section with `id`, `name` (never empty here: the callers resolve `defaultSectionName`) and the id's
/// turn in the colour cycle, starting at `firstEventIndex`.
Section makeSection(int id, std::string name, int firstEventIndex) {
    assert(!name.empty());
    return Section{id, boundedSectionName(std::move(name)), (id - 1) % kNumSectionColours, firstEventIndex};
}

/// The default name of a section about to join `sections`: "Section <n>", n counting the sections there
/// will be once it has (the first is "Section 1"), moved up past any name a section already has, so that
/// a progression whose sections came and went never shows a gap or a repeat (not the section id, which
/// never repeats and so ran on to "Section 9" in a document with one section).
std::string defaultSectionName(const std::vector<Section>& sections) {
    int n = static_cast<int>(sections.size());  // at most kMaxChords sections: no overflow below
    std::string name;
    do {
        name = "Section " + std::to_string(++n);
    } while (std::any_of(sections.begin(), sections.end(), [&](const Section& s) { return s.name == name; }));
    return name;
}

/// `name`, or the default when it is empty.
std::string sectionNameOr(std::string name, const std::vector<Section>& sections) {
    return name.empty() ? defaultSectionName(sections) : std::move(name);
}

/// Copies the live (non-undoable) fields of `current` into `target`, so that restoring a snapshot keeps
/// the transport toggles, the relation mode, the key-transpose flags, MIDI Thru and the new-chord length as they are now, and the browsed key
/// too, with one exception: a browsed key that is the project key ("home") follows the project key `target`
/// restores, as it follows `setProjectKey`, so that undoing or redoing a key change leaves the palette on the
/// key the progression is in, not on the key it just left. A browse elsewhere is kept as it is.
/// `autoVoiceLead`, `feel` and `meter` are deliberately absent: they change the notes `compile`
/// produces, so they are restored with the progression (see `Document`).
void keepLiveState(Document& target, const Document& current) {
    target.browsedKey = current.browsedKey == current.projectKey ? target.projectKey : current.browsedKey;
    target.progressionLoop = current.progressionLoop;
    target.followHost = current.followHost;
    target.relationMode = current.relationMode;
    target.transposeWithKey = current.transposeWithKey;
    target.keyHintSeen = current.keyHintSeen;
    target.midiThru = current.midiThru;
    target.newChordBeats = current.newChordBeats;
    target.chordsMuted = current.chordsMuted;
    target.chordsSolo = current.chordsSolo;
}

/// One past the largest track id in `doc`, or 1 when it has none. Ids are below INT_MAX (`replace`'s
/// precondition), so the sum cannot overflow.
int trackIdAfter(const Document& doc) {
    int next = 1;
    for (const Track& t : doc.tracks) {
        next = std::max(next, t.id + 1);
    }
    return next;
}

/// One past the largest note id of any track in `doc`, or 1 when it has none (same bound as `trackIdAfter`).
int noteIdAfter(const Document& doc) {
    int next = 1;
    for (const Track& t : doc.tracks) {
        for (const TrackNote& n : t.notes) {
            next = std::max(next, n.id + 1);
        }
    }
    return next;
}

/// What `replace` asserts of the tracks: at most `kMaxTracks`, track and note ids in 1..INT_MAX-1 and unique
/// (note ids across all tracks), every anchor an existing chord. Debug-only; the full invariant (domain,
/// limits) is the loader's to check.
[[maybe_unused]] bool tracksFitStore(const Document& doc) {
    if (doc.tracks.size() > static_cast<std::size_t>(kMaxTracks)) {
        return false;
    }
    std::set<int> trackIds;
    std::set<int> noteIds;
    for (const Track& t : doc.tracks) {
        if (t.id < 1 || t.id >= INT_MAX || !trackIds.insert(t.id).second) {
            return false;
        }
        for (const TrackNote& n : t.notes) {
            if (n.id < 1 || n.id >= INT_MAX || !noteIds.insert(n.id).second ||
                indexOfEvent(doc.events, n.anchorEventId) < 0) {
                return false;
            }
        }
    }
    return true;
}

/// Writes `reason` to `refusal` when the caller asked for one (the refusal channel, see `DocumentStore`).
void refuse(std::string* refusal, std::string reason) {
    if (refusal != nullptr) {
        *refusal = std::move(reason);
    }
}

void clearRefusal(std::string* refusal) noexcept {
    if (refusal != nullptr) {
        refusal->clear();
    }
}

std::string noTrackReason(int id) {
    return "No track with id " + std::to_string(id);
}

/// Why a clamped start has no owner: no chords at all, or a start at or after the end.
std::string noOwnerReason(const Document& doc) {
    return doc.events.empty() ? "Add chords first" : "A note must start before the end of the progression";
}

}  // namespace

DocumentStore::DocumentStore(int firstId) : nextId_(firstId) {
    assert(firstId >= 1);
}

const Document& DocumentStore::doc() const {
    return doc_;
}

void DocumentStore::replace(Document doc, int nextId, int nextSectionId, int nextTrackId, int nextNoteId) {
    assertValidKey(doc.projectKey);
    assertValidKey(doc.browsedKey);
    assert(static_cast<int>(doc.relationMode) >= 0 && static_cast<int>(doc.relationMode) < kNumRelationModes);
    assert(!doc.newChordBeats ||
           (std::isfinite(*doc.newChordBeats) && *doc.newChordBeats == snapNewChordBeats(*doc.newChordBeats)));
    assert(nextId >= 1);
    assert(nextSectionId >= 0);
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
    assert(sectionsWellFormed(doc));
    assert(nextSectionId == 0 || nextSectionId >= sectionIdAfter(doc));
    assert(tracksFitStore(doc));
    assert(nextTrackId >= 0 && (nextTrackId == 0 || nextTrackId >= trackIdAfter(doc)));
    assert(nextNoteId >= 0 && (nextNoteId == 0 || nextNoteId >= noteIdAfter(doc)));
    // Nothing below can throw: `doc` arrived fully built, and the moves and clears are noexcept.
    nextSectionId_ = nextSectionId == 0 ? sectionIdAfter(doc) : nextSectionId;
    nextTrackId_ = nextTrackId == 0 ? trackIdAfter(doc) : nextTrackId;
    nextNoteId_ = nextNoteId == 0 ? noteIdAfter(doc) : nextNoteId;
    ++revision_;
    doc_ = std::move(doc);
    undoStack_.clear();
    redoStack_.clear();
    nextId_ = nextId;
    selectedEventIds_.clear();
    anchorEventId_ = kNoEventId;
}

int DocumentStore::nextId() const {
    return nextId_;
}

int DocumentStore::nextSectionId() const {
    return nextSectionId_;
}

int DocumentStore::nextTrackId() const {
    return nextTrackId_;
}

int DocumentStore::nextNoteId() const {
    return nextNoteId_;
}

std::uint64_t DocumentStore::revision() const {
    return revision_;
}

int DocumentStore::findTrackIndex(int id) const {
    for (std::size_t i = 0; i < doc_.tracks.size(); ++i) {
        if (doc_.tracks[i].id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
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
    ++revision_;
    dropStaleSelection();
    return true;
}

void DocumentStore::dropStaleSelection() noexcept {
    const auto exists = [this](int id) { return indexOfEvent(doc_.events, id) >= 0; };
    // The anchor first, while the list still has the strip order of the document before the change: the
    // nearest selected block that is left, the next one, else the one before.
    if (anchorEventId_ != kNoEventId && !exists(anchorEventId_)) {
        int replacement = kNoEventId;
        const auto at = std::find(selectedEventIds_.begin(), selectedEventIds_.end(), anchorEventId_);
        if (at != selectedEventIds_.end()) {
            const auto after = std::find_if(at + 1, selectedEventIds_.end(), exists);
            if (after != selectedEventIds_.end()) {
                replacement = *after;
            } else {
                const auto before = std::find_if(std::make_reverse_iterator(at), selectedEventIds_.rend(), exists);
                if (before != selectedEventIds_.rend()) {
                    replacement = *before;
                }
            }
        }
        anchorEventId_ = replacement;
    }
    selectedEventIds_.erase(std::remove_if(selectedEventIds_.begin(), selectedEventIds_.end(),
                                           [&](int id) { return !exists(id); }),
                            selectedEventIds_.end());
    // The strip order of the document as it is now (a move or an undo may have reordered the blocks). In place
    // and without allocating, so this stays noexcept.
    std::sort(selectedEventIds_.begin(), selectedEventIds_.end(),
              [this](int a, int b) { return indexOfEvent(doc_.events, a) < indexOfEvent(doc_.events, b); });
}

int DocumentStore::selectedEventId() const {
    return anchorEventId_;
}

const std::vector<int>& DocumentStore::selectedEventIds() const {
    return selectedEventIds_;
}

void DocumentStore::setSelectedEvent(int id) {
    if (id == kNoEventId) {
        selectedEventIds_.clear();
        anchorEventId_ = kNoEventId;
    } else if (indexOfEvent(doc_.events, id) >= 0) {
        std::vector<int> selection{id};  // allocates first: a throw leaves the selection as it was
        selectedEventIds_.swap(selection);
        anchorEventId_ = id;
    }
}

void DocumentStore::setSelectedEvents(const std::vector<int>& ids, int anchor) {
    const std::set<int> wanted(ids.begin(), ids.end());
    std::vector<int> selection;  // built aside: a throw leaves the selection as it was
    for (const ChordEvent& e : doc_.events) {
        if (wanted.count(e.id) != 0) {
            selection.push_back(e.id);
        }
    }
    const bool anchorSelected = std::find(selection.begin(), selection.end(), anchor) != selection.end();
    const int newAnchor = selection.empty() ? kNoEventId : (anchorSelected ? anchor : selection.back());
    selectedEventIds_.swap(selection);
    anchorEventId_ = newAnchor;
}

int DocumentStore::appendChord(Chord chord, std::optional<double> lengthBeats, int velocity, std::optional<int> inversion) {
    // One entry at the end: insertChords' clamps, refusals, selection and single commit. The entry is built
    // before anything changes, so a failed allocation leaves the store as it was.
    const std::vector<int> ids =
        insertChords(static_cast<int>(doc_.events.size()), {ChordSpec{chord, lengthBeats, velocity, inversion}});
    return ids.empty() ? kNoEventId : ids.front();
}

namespace {

/// Section ids a progression load needs: an append onto chords that have no sections also sections them
/// (the first section starts at 0), every other load needs only the loaded block's own.
int sectionIdsForLoad(const Document& doc, ProgressionLoad how) {
    return how == ProgressionLoad::Append && !doc.events.empty() && doc.sections.empty() ? 2 : 1;
}

}  // namespace

ProgressionRefusal DocumentStore::progressionRefusal(const std::vector<std::pair<Chord, double>>& steps,
                                                     ProgressionLoad how) const {
    if (steps.empty()) {
        return ProgressionRefusal::Empty;
    }
    for (const auto& step : steps) {
        if (!std::isfinite(step.second)) {
            return ProgressionRefusal::NonFiniteLength;
        }
    }
    const std::size_t kept = how == ProgressionLoad::Append ? doc_.events.size() : 0;
    if (steps.size() > static_cast<std::size_t>(kMaxChords) - kept) {
        return ProgressionRefusal::DocumentFull;
    }
    // Ids stop below INT_MAX (as `appendChord`'s do): the batch takes nextId_ .. nextId_ + n - 1.
    if (nextId_ > INT_MAX - static_cast<int>(steps.size())) {  // n <= kMaxChords: no overflow
        return ProgressionRefusal::EventIdsExhausted;
    }
    if (nextSectionId_ > INT_MAX - sectionIdsForLoad(doc_, how)) {
        return ProgressionRefusal::SectionIdsExhausted;
    }
    return ProgressionRefusal::None;
}

bool DocumentStore::loadProgression(const std::vector<std::pair<Chord, double>>& steps, std::string sectionName,
                                    ProgressionLoad how) {
    if (progressionRefusal(steps, how) != ProgressionRefusal::None) {
        return false;
    }
    const int sectionIds = sectionIdsForLoad(doc_, how);
    Document next = doc_;
    if (how == ProgressionLoad::Replace) {
        next.events.clear();
        next.sections.clear();
    }
    const int join = static_cast<int>(next.events.size());
    next.events.reserve(next.events.size() + steps.size());
    int id = nextId_;
    std::vector<int> selection;  // the loaded chords: the selection once committed (D2)
    selection.reserve(steps.size());
    for (const auto& [chord, lengthBeats] : steps) {
        assertValidChord(chord);
        selection.push_back(id);
        next.events.push_back(ChordEvent{id++, chord, clampLength(lengthBeats), 100});  // appendChord's defaults (P8)
    }
    int sectionId = nextSectionId_;
    if (sectionIds == 2) {
        next.sections.push_back(makeSection(sectionId++, defaultSectionName(next.sections), 0));  // the chords already there
    }
    next.sections.push_back(makeSection(sectionId, sectionNameOr(std::move(sectionName), next.sections), join));
    assert(sectionsWellFormed(next));
    [[maybe_unused]] const bool changed = commit(std::move(next));
    assert(changed);  // fresh ids always make the sequence differ
    // Only now: a throw above leaves both counters, like the rest of the store, untouched.
    nextId_ += static_cast<int>(steps.size());
    nextSectionId_ += sectionIds;
    anchorEventId_ = selection.back();  // live state, after the commit
    selectedEventIds_.swap(selection);
    return true;
}

bool DocumentStore::appendProgression(const std::vector<std::pair<Chord, double>>& steps, std::string sectionName) {
    return loadProgression(steps, std::move(sectionName), ProgressionLoad::Append);
}

bool DocumentStore::replaceProgression(const std::vector<std::pair<Chord, double>>& steps, std::string sectionName) {
    return loadProgression(steps, std::move(sectionName), ProgressionLoad::Replace);
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
    // Each section follows the chord it starts on: remember that chord, move, then look it up again.
    std::vector<int> startIds;
    startIds.reserve(next.sections.size());
    for (const Section& s : next.sections) {
        startIds.push_back(next.events[static_cast<std::size_t>(s.firstEventIndex)].id);
    }
    const int firstSectionId = next.sections.empty() ? kNoSectionId : next.sections.front().id;  // at 0
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
    // The first section is pinned to the start: unless another section's start chord now sits at 0, it
    // stays there (its own chord moved away, or a chord from inside a section came to the front).
    if (!sectionStartsAt(next, 0)) {
        for (Section& s : next.sections) {
            if (s.id == firstSectionId) {
                s.firstEventIndex = 0;  // free, and no other section has it: the list stays unique
            }
        }
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
    reindexSectionsForRemove(next.sections, index, static_cast<int>(next.events.size()));
    next.events.erase(next.events.begin() + index);
    commit(std::move(next));
}

std::vector<int> DocumentStore::insertChords(int index, const std::vector<ChordSpec>& chords) {
    if (chords.empty() || chords.size() > static_cast<std::size_t>(kMaxChords) - doc_.events.size() ||
        nextId_ > INT_MAX - static_cast<int>(chords.size())) {  // n <= kMaxChords here: no overflow
        return {};
    }
    for (const ChordSpec& c : chords) {
        assertValidChord(c.chord);
        if (c.lengthBeats.has_value() && !std::isfinite(*c.lengthBeats)) {
            return {};
        }
    }
    const int at = std::clamp(index, 0, static_cast<int>(doc_.events.size()));
    const int count = static_cast<int>(chords.size());
    Document next = doc_;
    std::vector<ChordEvent> added;
    added.reserve(chords.size());
    std::vector<int> ids;
    ids.reserve(chords.size());
    int id = nextId_;
    for (const ChordSpec& c : chords) {
        ChordEvent event{id, c.chord, clampLength(c.lengthBeats.value_or(doc_.newChordLength())),
                         clampVelocity(c.velocity)};
        if (c.inversion.has_value()) {
            event.inversion = clampInversion(c.chord, *c.inversion);
        }
        if (c.octave.has_value()) {
            event.octave = clampOctave(*c.octave);
        }
        event.manualVoicing = c.manualVoicing.value_or(c.inversion.has_value() || c.octave.has_value());
        added.push_back(event);
        ids.push_back(id++);
    }
    next.events.insert(next.events.begin() + at, added.begin(), added.end());
    for (Section& s : next.sections) {
        if (s.firstEventIndex >= at && s.firstEventIndex != 0) {  // the first section, at 0, is pinned
            s.firstEventIndex += count;
        }
    }
    assert(sectionsWellFormed(next));
    std::vector<int> selection = ids;  // before the commit, as appendChord
    [[maybe_unused]] const bool changed = commit(std::move(next));
    assert(changed);   // fresh ids always make the sequence differ
    nextId_ += count;  // only now: a throw above leaves the counter, like the rest of the store, untouched
    selectedEventIds_.swap(selection);  // live state: the inserted chords, the last the anchor (D2)
    anchorEventId_ = ids.back();
    return ids;
}

bool DocumentStore::removeChords(const std::vector<int>& ids) {
    const std::set<int> wanted(ids.begin(), ids.end());
    std::vector<int> indices;  // ascending: the chords that exist, once each
    for (std::size_t i = 0; i < doc_.events.size(); ++i) {
        if (wanted.count(doc_.events[i].id) != 0) {
            indices.push_back(static_cast<int>(i));
        }
    }
    if (indices.empty()) {
        return false;
    }
    Document next = doc_;
    for (std::size_t k = 0; k < indices.size(); ++k) {
        const int index = indices[k] - static_cast<int>(k);  // the k removals before it moved it up k places
        reindexSectionsForRemove(next.sections, index, static_cast<int>(next.events.size()));
        next.events.erase(next.events.begin() + index);
    }
    return commit(std::move(next));
}

bool DocumentStore::setLengths(const std::vector<int>& ids, double lengthBeats) {
    if (!std::isfinite(lengthBeats)) {
        return false;
    }
    const std::set<int> wanted(ids.begin(), ids.end());
    const double length = clampLength(lengthBeats);
    Document next = doc_;
    for (ChordEvent& e : next.events) {
        if (wanted.count(e.id) != 0) {
            e.lengthBeats = length;
        }
    }
    return commit(std::move(next));  // nothing changed: no history
}

void DocumentStore::setProjectKey(Key key) {
    assertValidKey(key);
    Document next = doc_;
    const int interval = wrapPitchClass(key.tonicPc() - doc_.projectKey.tonicPc());
    if (doc_.transposeWithKey && interval != 0 && !doc_.events.empty()) {
        // The chords move in the same commit as the key: one undo step restores both.
        transposeEvents(next.events, chooseKeyTranspose(doc_.events, interval));
    }
    if (doc_.browsedKey == doc_.projectKey) {
        next.browsedKey = key;  // a home browse follows the project key, as under undo and redo (keepLiveState)
    }
    next.projectKey = key;
    commit(std::move(next));
}

bool DocumentStore::transposeFromKey(PitchClass fromTonic) {
    const int interval = wrapPitchClass(doc_.projectKey.tonicPc() - fromTonic);
    if (doc_.events.empty() || interval == 0) {
        return false;
    }
    Document next = doc_;
    transposeEvents(next.events, chooseKeyTranspose(doc_.events, interval));
    return commit(std::move(next));
}

bool DocumentStore::transposeProgression(int semitones) {
    if (doc_.events.empty() || semitones == 0 || semitones < -11 || semitones > 11) {
        return false;
    }
    Document next = doc_;
    transposeEvents(next.events, semitones);
    return commit(std::move(next));
}

void DocumentStore::setAutoVoiceLead(bool on) {
    Document next = doc_;
    next.autoVoiceLead = on;
    commit(std::move(next));  // undoable: it changes the compiled progression
}

void DocumentStore::setFeel(Feel feel) {
    Document next = doc_;
    next.feel = feel;
    commit(std::move(next));  // undoable; commit ignores an unchanged document
}

void DocumentStore::setMeter(Meter meter) {
    assert(validMeter(meter));
    Document next = doc_;
    next.meter = meter;
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

void DocumentStore::setRelationMode(RelationMode mode) {
    assert(static_cast<int>(mode) >= 0 && static_cast<int>(mode) < kNumRelationModes);
    doc_.relationMode = mode;  // live state: no history
}

void DocumentStore::setTransposeWithKey(bool on) {
    doc_.transposeWithKey = on;  // live state: no history
}

void DocumentStore::setKeyHintSeen(bool seen) {
    doc_.keyHintSeen = seen;  // live state: no history
}

void DocumentStore::setMidiThru(bool on) {
    doc_.midiThru = on;  // live state: no history
}

void DocumentStore::setNewChordLength(std::optional<double> lengthBeats) {
    if (lengthBeats.has_value() && !std::isfinite(*lengthBeats)) {
        return;
    }
    // Live state: no history.
    doc_.newChordBeats = lengthBeats.has_value() ? std::optional<double>(snapNewChordBeats(*lengthBeats)) : std::nullopt;
}

int DocumentStore::addSection(int firstEventIndex, std::string name) {
    if (!isEventIndex(doc_, firstEventIndex) || sectionStartsAt(doc_, firstEventIndex)) {
        return kNoSectionId;
    }
    // The first section starts at 0: the first add at an inner chord also sections the chords before it.
    const bool leading = doc_.sections.empty() && firstEventIndex > 0;
    const int needed = leading ? 2 : 1;
    if (nextSectionId_ > INT_MAX - needed) {
        return kNoSectionId;  // the counter stops at INT_MAX
    }
    Document next = doc_;
    int id = nextSectionId_;
    if (leading) {
        next.sections.push_back(makeSection(id, defaultSectionName(next.sections), 0));
        ++id;
    }
    Section section = makeSection(id, sectionNameOr(std::move(name), next.sections), firstEventIndex);
    next.sections.insert(std::upper_bound(next.sections.begin(), next.sections.end(), section, sectionLess),
                         std::move(section));
    [[maybe_unused]] const bool changed = commit(std::move(next));
    assert(changed);          // a fresh id always makes the list differ
    nextSectionId_ += needed;  // only now: a throw above leaves the counter, like the rest of the store, untouched
    return id;
}

void DocumentStore::renameSection(int id, std::string name) {
    const int index = findSectionIndex(id);
    if (index < 0) {
        return;
    }
    Document next = doc_;
    next.sections[static_cast<std::size_t>(index)].name = boundedSectionName(std::move(name));
    commit(std::move(next));  // a name that cuts down to the current one changes nothing
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
    if (index == 0 && !next.sections.empty()) {
        next.sections.front().firstEventIndex = 0;  // the first section's chords join the next one
    }
    commit(std::move(next));
}

void DocumentStore::moveSectionStart(int id, int newFirstEventIndex) {
    const int index = findSectionIndex(id);
    if (index < 0 || !isEventIndex(doc_, newFirstEventIndex)) {
        return;
    }
    const int current = doc_.sections[static_cast<std::size_t>(index)].firstEventIndex;
    if (current == newFirstEventIndex || current == 0 || sectionStartsAt(doc_, newFirstEventIndex)) {
        return;  // already there, the pinned first section, or another section's start
    }
    Document next = doc_;
    next.sections[static_cast<std::size_t>(index)].firstEventIndex = newFirstEventIndex;
    std::sort(next.sections.begin(), next.sections.end(), sectionLess);
    commit(std::move(next));
}

// --- tracks and notes (tracks design D4) --------------------------------------------------
//
// Each edit builds its whole candidate document (and its new ids) first, then commits it, and only then
// publishes a counter with a noexcept step: the strong guarantee, as for the chord edits.

int DocumentStore::addTrack(TrackRole role, std::optional<std::string> name, std::string* refusal) {
    clearRefusal(refusal);
    assert(role == TrackRole::Bass || role == TrackRole::Lead || role == TrackRole::Other);
    if (doc_.tracks.size() >= static_cast<std::size_t>(kMaxTracks)) {
        refuse(refusal, "At most " + std::to_string(kMaxTracks) + " tracks");
        return kNoTrackId;
    }
    if (nextTrackId_ >= INT_MAX) {
        refuse(refusal, "No track ids are left");
        return kNoTrackId;
    }
    std::string trackName = !name.has_value() || name->empty() ? defaultTrackName(doc_, role)
                                                               : boundedName(std::move(*name), kMaxTrackNameBytes);
    Document next = doc_;
    next.tracks.push_back(Track{nextTrackId_, std::move(trackName), role, false, false, {}});
    [[maybe_unused]] const bool changed = commit(std::move(next));
    assert(changed);  // a fresh id always makes the list differ
    return nextTrackId_++;  // only now: a throw above leaves the counter untouched
}

bool DocumentStore::removeTrack(int id, std::string* refusal) {
    clearRefusal(refusal);
    const int index = findTrackIndex(id);
    if (index < 0) {
        refuse(refusal, noTrackReason(id));
        return false;
    }
    Document next = doc_;
    next.tracks.erase(next.tracks.begin() + index);
    return commit(std::move(next));
}

bool DocumentStore::patchTrack(int id, std::optional<std::string> name, std::optional<TrackRole> role,
                               std::string* refusal) {
    clearRefusal(refusal);
    const int index = findTrackIndex(id);
    if (index < 0) {
        refuse(refusal, noTrackReason(id));
        return false;
    }
    if (!name.has_value() && !role.has_value()) {
        refuse(refusal, "Give a name or a role");
        return false;
    }
    assert(!role || *role == TrackRole::Bass || *role == TrackRole::Lead || *role == TrackRole::Other);
    Document next = doc_;
    Track& track = next.tracks[static_cast<std::size_t>(index)];
    if (name.has_value()) {
        track.name = boundedName(std::move(*name), kMaxTrackNameBytes);  // empty stays empty, as renameSection
    }
    if (role.has_value()) {
        track.role = *role;
    }
    return commit(std::move(next));  // equal to the current track: no step
}

bool DocumentStore::moveTrack(int id, int newIndex, std::string* refusal) {
    clearRefusal(refusal);
    const int from = findTrackIndex(id);
    if (from < 0) {
        refuse(refusal, noTrackReason(id));
        return false;
    }
    const int to = std::clamp(newIndex, 0, static_cast<int>(doc_.tracks.size()) - 1);
    if (to == from) {
        return false;
    }
    Document next = doc_;
    auto& tracks = next.tracks;
    const auto first = tracks.begin() + from;
    const auto target = tracks.begin() + to;
    if (from < to) {
        std::rotate(first, first + 1, target + 1);
    } else {
        std::rotate(target, first, first + 1);
    }
    return commit(std::move(next));
}

std::vector<int> DocumentStore::insertNotes(int trackId, const std::vector<NoteSpec>& specs, std::string* refusal) {
    clearRefusal(refusal);
    const int index = findTrackIndex(trackId);
    if (index < 0) {
        refuse(refusal, noTrackReason(trackId));
        return {};
    }
    if (specs.empty()) {
        refuse(refusal, "No notes to insert");
        return {};
    }
    const std::size_t held = doc_.tracks[static_cast<std::size_t>(index)].notes.size();
    const auto limit = static_cast<std::size_t>(kMaxNotesPerTrack);
    if (held >= limit || specs.size() > limit - held) {
        refuse(refusal, "At most " + std::to_string(kMaxNotesPerTrack) + " notes per track");
        return {};
    }
    // Ids stop below INT_MAX (as the event ids do): the batch takes nextNoteId_ .. nextNoteId_ + n - 1.
    if (nextNoteId_ > INT_MAX - static_cast<int>(specs.size())) {  // n <= kMaxNotesPerTrack: no overflow
        refuse(refusal, "No note ids are left");
        return {};
    }
    if (doc_.events.empty()) {
        refuse(refusal, noOwnerReason(doc_));
        return {};
    }
    const ChordTimeline timeline = chordTimeline(doc_);
    Document next = doc_;
    std::vector<TrackNote>& notes = next.tracks[static_cast<std::size_t>(index)].notes;
    notes.reserve(held + specs.size());
    std::vector<int> ids;
    ids.reserve(specs.size());
    int id = nextNoteId_;
    for (const NoteSpec& spec : specs) {
        // D1's order: the start is clamped first, then resolved, then the length clamped against what it leaves.
        const std::int32_t start = clampNoteStart(spec.startTick);
        const std::optional<NoteAnchor> anchor = resolveStart(timeline, start);
        if (!anchor.has_value()) {
            refuse(refusal, noOwnerReason(doc_));
            return {};
        }
        notes.push_back(TrackNote{id, clampNotePitch(spec.pitch), anchor->eventId, anchor->offsetTick,
                                  clampNoteLength(spec.lengthTicks, start), clampNoteVelocity(spec.velocity)});
        ids.push_back(id++);
    }
    sortTrackNotes(notes, timeline);
    [[maybe_unused]] const bool changed = commit(std::move(next));
    assert(changed);  // fresh ids always make the track differ
    nextNoteId_ += static_cast<int>(specs.size());  // only now: a throw above leaves the counter untouched
    return ids;
}

bool DocumentStore::removeNotes(int trackId, const std::vector<int>& ids, std::string* refusal) {
    clearRefusal(refusal);
    const int index = findTrackIndex(trackId);
    if (index < 0) {
        refuse(refusal, noTrackReason(trackId));
        return false;
    }
    const std::set<int> wanted(ids.begin(), ids.end());
    Document next = doc_;
    std::vector<TrackNote>& notes = next.tracks[static_cast<std::size_t>(index)].notes;
    // Removal keeps the remaining notes in their sorted order.
    notes.erase(std::remove_if(notes.begin(), notes.end(), [&](const TrackNote& n) { return wanted.count(n.id) != 0; }),
                notes.end());
    return commit(std::move(next));  // none of them in the track: no step
}

bool DocumentStore::updateNotes(int trackId, const std::vector<NoteEdit>& edits, std::string* refusal) {
    clearRefusal(refusal);
    const int index = findTrackIndex(trackId);
    if (index < 0) {
        refuse(refusal, noTrackReason(trackId));
        return false;
    }
    const std::vector<TrackNote>& current = doc_.tracks[static_cast<std::size_t>(index)].notes;
    std::unordered_map<int, std::size_t> positionOf;
    positionOf.reserve(current.size());
    for (std::size_t i = 0; i < current.size(); ++i) {
        positionOf.emplace(current[i].id, i);
    }
    std::set<int> seen;
    for (const NoteEdit& e : edits) {
        if (positionOf.count(e.id) == 0) {
            refuse(refusal, "No note with id " + std::to_string(e.id) + " in track " + std::to_string(trackId));
            return false;
        }
        if (!seen.insert(e.id).second) {  // two edits of one note: which wins would be the caller's accident
            refuse(refusal, "Note " + std::to_string(e.id) + " is listed twice");
            return false;
        }
    }
    const ChordTimeline timeline = chordTimeline(doc_);
    Document next = doc_;
    std::vector<TrackNote>& notes = next.tracks[static_cast<std::size_t>(index)].notes;
    for (const NoteEdit& e : edits) {
        TrackNote& note = notes[positionOf.at(e.id)];
        if (e.pitch.has_value()) {
            note.pitch = clampNotePitch(*e.pitch);
        }
        if (e.velocity.has_value()) {
            note.velocity = clampNoteVelocity(*e.velocity);
        }
        // A start equal to the current derived one (as given or once clamped) counts as absent: a drag re-sends
        // the snapshot's start (D15), and resolving it again would move an inactive note to another chord.
        const std::int64_t derived = noteStart(timeline, note);
        const bool moved =
            e.startTick.has_value() && *e.startTick != derived && clampNoteStart(*e.startTick) != derived;
        if (!moved && !e.lengthTicks.has_value()) {
            continue;
        }
        std::int32_t start = 0;
        if (moved) {
            start = clampNoteStart(*e.startTick);
            const std::optional<NoteAnchor> anchor = resolveStart(timeline, start);
            if (!anchor.has_value()) {
                refuse(refusal, noOwnerReason(doc_));
                return false;
            }
            note.anchorEventId = anchor->eventId;  // across a boundary: the note changes its chord
            note.offsetTick = anchor->offsetTick;
        } else {
            // The derived start, clamped into the start domain: a chord edit can have pushed it past
            // kMaxNoteTick - kMinTrackNoteTicks (the offset stays), and then the length clamps to its minimum.
            start = clampNoteStart(derived);
        }
        // A kept length is clamped again too: a later start leaves less room.
        note.lengthTicks = clampNoteLength(e.lengthTicks.value_or(note.lengthTicks), start);
    }
    sortTrackNotes(notes, timeline);
    return commit(std::move(next));  // nothing changed: no step
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
    ++revision_;
    dropStaleSelection();
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
    ++revision_;
    dropStaleSelection();
    return true;
}

}  // namespace chordlab
