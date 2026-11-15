#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/document.h"
#include "chordlab/feel.h"
#include "chordlab/meter.h"
#include "chordlab/tracks.h"
#include "chordlab/transpose.h"

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using namespace chordlab;

namespace {

Chord chord(PitchClass root, std::string_view typeId) {
    return Chord{root, &chordType(typeId)};
}

const Chord kC = chord(0, "maj");
const Chord kF = chord(5, "maj");
const Chord kG = chord(7, "7");

constexpr std::int32_t kBar = 4 * kFeelTicksPerBeat;  // one four-beat chord: 3840 ticks

/// A store holding C (id 1), F (id 2), G7 (id 3), four beats each: boundaries 0, 3840, 7680, 11520.
DocumentStore threeChords() {
    DocumentStore store;
    store.appendChord(kC, 4.0, 100);
    store.appendChord(kF, 4.0, 100);
    store.appendChord(kG, 4.0, 100);
    return store;
}

/// `threeChords` plus one empty Bass track (id 1).
DocumentStore threeChordsAndBass() {
    DocumentStore store = threeChords();
    REQUIRE(store.addTrack(TrackRole::Bass) == 1);
    return store;
}

/// The longest document there can be: kMaxChords chords of kMaxLengthBeats, so it ends at kMaxNoteTick, plus
/// one empty Lead track (id 1).
DocumentStore fullDocumentAndLead() {
    DocumentStore store;
    for (int i = 0; i < kMaxChords; ++i) {
        REQUIRE(store.appendChord(kC, kMaxLengthBeats) == i + 1);
    }
    REQUIRE(store.addTrack(TrackRole::Lead) == 1);
    return store;
}

/// Whether `store`'s most recent history entry is `reference`'s: each undoes once (on copies) to the same
/// document. A refused call that wrongly pushed a snapshot would undo to the current document instead.
/// (The same helper as test_document.cpp's.)
bool sameHistoryTop(const DocumentStore& store, const DocumentStore& reference) {
    DocumentStore a = store;
    DocumentStore b = reference;
    const bool undoneA = a.undo();
    const bool undoneB = b.undo();
    return undoneA == undoneB && a.doc() == b.doc();
}

/// Whether `store`'s redo stack has `reference`'s top: each redoes once (on copies) to the same document. A
/// refused call that wrongly cleared the redo stack would no longer redo.
bool sameRedoTop(const DocumentStore& store, const DocumentStore& reference) {
    DocumentStore a = store;
    DocumentStore b = reference;
    const bool redoneA = a.redo();
    const bool redoneB = b.redo();
    return redoneA == redoneB && a.doc() == b.doc();
}

/// Runs `mutate`, checks it changed the document as exactly one undo step: undo restores the document from
/// before and redo the document from after. Leaves the store in the "after" state.
template <typename Mutate>
void checkOneStep(DocumentStore& store, Mutate mutate) {
    const Document before = store.doc();
    DocumentStore beforeStore = store;
    mutate(store);
    const Document after = store.doc();
    REQUIRE_FALSE(after == before);
    REQUIRE(store.undo());
    CHECK(store.doc() == before);
    CHECK(sameHistoryTop(store, beforeStore));  // one step back is where the store was: nothing else was pushed
    REQUIRE(store.redo());
    CHECK(store.doc() == after);
}

/// Checks that `store` refused an edit: unchanged, with the same history (undo and redo), counters and selections
/// (chords with their anchor, notes) as `reference`.
void checkRefused(const DocumentStore& store, const DocumentStore& reference) {
    CHECK(store.doc() == reference.doc());
    CHECK(sameHistoryTop(store, reference));
    CHECK(sameRedoTop(store, reference));
    CHECK(store.nextId() == reference.nextId());
    CHECK(store.nextSectionId() == reference.nextSectionId());
    CHECK(store.nextTrackId() == reference.nextTrackId());
    CHECK(store.nextNoteId() == reference.nextNoteId());
    CHECK(store.revision() == reference.revision());
    CHECK(store.selectedEventIds() == reference.selectedEventIds());
    CHECK(store.selectedEventId() == reference.selectedEventId());
    CHECK(store.noteSelection() == reference.noteSelection());
}

NoteSpec ns(int pitch, std::int64_t start, std::int64_t length, int velocity = 100) {
    return NoteSpec{pitch, start, length, velocity};
}

NoteEdit edit(int id) {
    NoteEdit e{};
    e.id = id;
    return e;
}

const Track& trackAt(const DocumentStore& store, std::size_t index) {
    return store.doc().tracks.at(index);
}

const std::vector<TrackNote>& notesOf(const DocumentStore& store, std::size_t trackIndex = 0) {
    return trackAt(store, trackIndex).notes;
}

std::vector<int> trackIdsOf(const DocumentStore& store) {
    std::vector<int> ids;
    for (const Track& t : store.doc().tracks) {
        ids.push_back(t.id);
    }
    return ids;
}

std::vector<int> noteIdsOf(const DocumentStore& store, std::size_t trackIndex = 0) {
    std::vector<int> ids;
    for (const TrackNote& n : notesOf(store, trackIndex)) {
        ids.push_back(n.id);
    }
    return ids;
}

using Ticks = std::vector<std::int32_t>;

}  // namespace

// --- model -----------------------------------------------------------------------------

TEST_CASE("tracks: the model's constants are D1's") {
    CHECK(kMaxTracks == 4);
    CHECK(kMaxNotesPerTrack == 1024);
    CHECK(kMinTrackNoteTicks == 30);
    CHECK(kMaxNoteTick == 256 * 64 * 960);
    CHECK(kMaxNoteTick == static_cast<std::int32_t>(kMaxChords * kMaxLengthBeats) * kFeelTicksPerBeat);
    CHECK(kMaxTrackNameBytes == 64);
    CHECK(kNoTrackId == -1);
}

TEST_CASE("TrackNote / Track: equality compares every field") {
    const TrackNote n{1, 60, 2, 100, 480, 90};
    CHECK(n == TrackNote{1, 60, 2, 100, 480, 90});
    CHECK(n != TrackNote{9, 60, 2, 100, 480, 90});
    CHECK(n != TrackNote{1, 61, 2, 100, 480, 90});
    CHECK(n != TrackNote{1, 60, 3, 100, 480, 90});
    CHECK(n != TrackNote{1, 60, 2, 101, 480, 90});
    CHECK(n != TrackNote{1, 60, 2, 100, 481, 90});
    CHECK(n != TrackNote{1, 60, 2, 100, 480, 91});

    const Track t{1, "Bass", TrackRole::Bass, false, false, {n}};
    Track u = t;
    CHECK(t == u);
    CHECK(Track{1, "Bass", TrackRole::Bass}.mute == false);  // the defaults
    CHECK(Track{1, "Bass", TrackRole::Bass}.solo == false);
    u.id = 2;
    CHECK(t != u);
    u = t;
    u.name = "Bass 2";
    CHECK(t != u);
    u = t;
    u.role = TrackRole::Lead;
    CHECK(t != u);
    u = t;
    u.mute = true;
    CHECK(t != u);
    u = t;
    u.solo = true;
    CHECK(t != u);
    u = t;
    u.notes.clear();
    CHECK(t != u);
}

// --- chord boundaries, ownership, derived starts -----------------------------------------

TEST_CASE("chordBoundaryTicks: size events + 1, cumulative beats rounded once; {0} for an empty progression") {
    CHECK(chordBoundaryTicks(Document{}) == Ticks{0});
    CHECK(chordBoundaryTicks(threeChords().doc()) == Ticks{0, 3840, 7680, 11520});
    // Cumulative 0.2506, 0.5012, 0.7518 beats -> 241, 481, 722 (rounding each length alone would give 723).
    Document doc;
    doc.events = {ChordEvent{1, kC, 0.2506, 100}, ChordEvent{2, kC, 0.2506, 100}, ChordEvent{3, kC, 0.2506, 100}};
    CHECK(chordBoundaryTicks(doc) == Ticks{0, 241, 481, 722});
}

TEST_CASE("chordBoundaryTicks: the feel's chordTicks, on block and performing documents alike") {
    Document fractional;
    fractional.events = {ChordEvent{1, kC, 0.2506, 100}, ChordEvent{2, kF, 0.2506, 100},
                         ChordEvent{3, kG, 0.2506, 100}, ChordEvent{4, kC, 1.3, 100},
                         ChordEvent{5, kF, 0.3, 100},    ChordEvent{6, kG, 63.999, 100}};
    Document performing = fractional;
    performing.feel.rhythm = Rhythm::Arpeggio;
    Document strummed = fractional;
    strummed.feel.strum = Strum::Down;
    for (const Document& doc : {Document{}, threeChords().doc(), fractional, performing, strummed}) {
        const std::vector<int> feelTicks = performDocument(doc).chordTicks;
        CHECK(chordBoundaryTicks(doc) == Ticks(feelTicks.begin(), feelTicks.end()));
    }
}

TEST_CASE("resolveStart: half-open ownership; a boundary tick belongs to the following chord; none at the end") {
    const DocumentStore store = threeChords();
    const Document& doc = store.doc();
    const ChordTimeline timeline = chordTimeline(doc);
    const auto expect = [&](std::int64_t tick, int index, int id, std::int32_t offset) {
        CAPTURE(tick);
        for (const std::optional<NoteAnchor>& anchor : {resolveStart(doc, tick), resolveStart(timeline, tick)}) {
            REQUIRE(anchor.has_value());
            CHECK(anchor->eventIndex == index);
            CHECK(anchor->eventId == id);
            CHECK(anchor->offsetTick == offset);
        }
    };
    expect(0, 0, 1, 0);
    expect(1, 0, 1, 1);
    expect(kBar - 1, 0, 1, kBar - 1);
    expect(kBar, 1, 2, 0);  // exactly on the boundary: the next chord
    expect(kBar + 1160, 1, 2, 1160);
    expect(2 * kBar, 2, 3, 0);
    expect(3 * kBar - 1, 2, 3, kBar - 1);
    for (const std::int64_t tick : {std::int64_t{3 * kBar}, std::int64_t{3 * kBar + 1}, std::int64_t{-1},
                                    std::numeric_limits<std::int64_t>::max(), std::numeric_limits<std::int64_t>::min()}) {
        CAPTURE(tick);
        CHECK_FALSE(resolveStart(doc, tick).has_value());  // at or after the end, or before the start: no owner
        CHECK_FALSE(resolveStart(timeline, tick).has_value());
    }
    CHECK_FALSE(resolveStart(Document{}, 0).has_value());  // an empty progression owns nothing
}

TEST_CASE("resolveStart: fractional chords own their rounded spans") {
    Document doc;
    doc.events = {ChordEvent{7, kC, 0.2506, 100}, ChordEvent{8, kC, 0.2506, 100}, ChordEvent{9, kC, 0.2506, 100}};
    REQUIRE(resolveStart(doc, 240).has_value());
    CHECK(resolveStart(doc, 240)->eventId == 7);
    CHECK(resolveStart(doc, 241)->eventId == 8);
    CHECK(resolveStart(doc, 241)->offsetTick == 0);
    CHECK(resolveStart(doc, 480)->eventId == 8);
    CHECK(resolveStart(doc, 480)->offsetTick == 239);
    CHECK(resolveStart(doc, 481)->eventId == 9);
    CHECK(resolveStart(doc, 721)->offsetTick == 240);
    CHECK_FALSE(resolveStart(doc, 722).has_value());
}

TEST_CASE("noteStart / noteActive: the anchor's boundary plus the offset; active while the offset is inside the chord") {
    Document doc;
    doc.events = {ChordEvent{1, kC, 4.0, 100}, ChordEvent{2, kF, 0.2506, 100}, ChordEvent{3, kG, 4.0, 100}};
    // Boundaries 0, 3840, 4081 (lround(4.2506 * 960) = 4081), 7921: chord 2 is 241 ticks wide.
    const ChordTimeline timeline = chordTimeline(doc);
    CHECK(timeline.boundaries == Ticks{0, 3840, 4081, 7921});
    CHECK(timeline.indexOf(2) == 1);
    CHECK(timeline.indexOf(99) == -1);
    const TrackNote first{1, 60, 1, 100, 480, 100};
    const TrackNote inside{2, 60, 2, 240, 480, 100};      // the last tick of chord 2
    const TrackNote overflow{3, 60, 2, 241, 480, 100};    // offset == chord length: past its end
    const TrackNote far{4, 60, 1, 9000, 30, 100};         // far past chord 1, even past the progression
    const TrackNote last{5, 60, 3, 3839, 30, 100};
    const auto check = [&](const TrackNote& n, std::int64_t start, bool active) {
        CAPTURE(n.id);
        CHECK(noteStart(doc, n) == start);
        CHECK(noteStart(timeline, n) == start);
        CHECK(noteActive(doc, n) == active);
        CHECK(noteActive(timeline, n) == active);
    };
    check(first, 100, true);
    check(inside, 4081 - 1, true);
    check(overflow, 4081, false);
    check(far, 9000, false);
    check(last, 4081 + 3839, true);
}

TEST_CASE("sortTrackNotes: by derived start, then pitch, then id") {
    Document doc;
    doc.events = {ChordEvent{1, kC, 4.0, 100}, ChordEvent{2, kF, 4.0, 100}};
    // Note 10 has the larger offset but the earlier chord; 11 and 12 share a start, 13 and 14 a start and a pitch;
    // 15 is an inactive note of chord 1 that lands after chord 2's notes.
    std::vector<TrackNote> notes{
        TrackNote{14, 50, 2, 0, 30, 100},    TrackNote{11, 64, 2, 0, 30, 100}, TrackNote{10, 70, 1, 3000, 30, 100},
        TrackNote{15, 40, 1, 5000, 30, 100}, TrackNote{13, 50, 2, 0, 30, 100}, TrackNote{12, 60, 1, 3840, 30, 100},
        TrackNote{16, 90, 1, 0, 30, 100}};
    sortTrackNotes(notes, chordTimeline(doc));
    std::vector<int> ids;
    for (const TrackNote& n : notes) {
        ids.push_back(n.id);
    }
    // Starts: 16 at 0; 10 at 3000; 13, 14 (50), 12 (60, chord 1 + 3840), 11 (64) at 3840; 15 at 5000.
    CHECK(ids == std::vector<int>{16, 10, 13, 14, 12, 11, 15});
}

TEST_CASE("note clamps: pitch 0..127, velocity 1..127, start first, then the length against the space left") {
    CHECK(clampNotePitch(-5) == 0);
    CHECK(clampNotePitch(64) == 64);
    CHECK(clampNotePitch(200) == 127);
    CHECK(clampNoteVelocity(0) == 1);
    CHECK(clampNoteVelocity(-40) == 1);
    CHECK(clampNoteVelocity(77) == 77);
    CHECK(clampNoteVelocity(300) == 127);
    constexpr std::int32_t lastStart = kMaxNoteTick - kMinTrackNoteTicks;
    CHECK(clampNoteStart(-1) == 0);
    CHECK(clampNoteStart(std::numeric_limits<std::int64_t>::min()) == 0);
    CHECK(clampNoteStart(12345) == 12345);
    CHECK(clampNoteStart(lastStart) == lastStart);
    CHECK(clampNoteStart(lastStart + 1) == lastStart);  // the last 30 ticks of the domain hold no start
    CHECK(clampNoteStart(kMaxNoteTick) == lastStart);
    CHECK(clampNoteStart(std::numeric_limits<std::int64_t>::max()) == lastStart);
    CHECK(clampNoteLength(5, 0) == kMinTrackNoteTicks);
    CHECK(clampNoteLength(std::numeric_limits<std::int64_t>::min(), 0) == kMinTrackNoteTicks);
    CHECK(clampNoteLength(480, 0) == 480);
    CHECK(clampNoteLength(std::numeric_limits<std::int64_t>::max(), 0) == kMaxNoteTick);
    CHECK(clampNoteLength(1000, lastStart) == kMinTrackNoteTicks);
    CHECK(clampNoteLength(1000, lastStart - 1) == kMinTrackNoteTicks + 1);
    CHECK(clampNoteLength(5, lastStart) == kMinTrackNoteTicks);
}

TEST_CASE("defaultTrackName: Bass, Lead, Track; a taken name steps on to \" 2\", \" 3\"") {
    Document doc;
    CHECK(defaultTrackName(doc, TrackRole::Bass) == "Bass");
    CHECK(defaultTrackName(doc, TrackRole::Lead) == "Lead");
    CHECK(defaultTrackName(doc, TrackRole::Other) == "Track");
    doc.tracks.push_back(Track{1, "Bass", TrackRole::Lead});  // the name is what counts, not the role
    doc.tracks.push_back(Track{2, "Bass 2", TrackRole::Bass});
    doc.tracks.push_back(Track{3, "Track 2", TrackRole::Other});
    CHECK(defaultTrackName(doc, TrackRole::Bass) == "Bass 3");
    CHECK(defaultTrackName(doc, TrackRole::Lead) == "Lead");
    CHECK(defaultTrackName(doc, TrackRole::Other) == "Track");
    doc.tracks.push_back(Track{4, "Track", TrackRole::Other});
    CHECK(defaultTrackName(doc, TrackRole::Other) == "Track 3");
}

// --- addTrack ----------------------------------------------------------------------------

TEST_CASE("addTrack: fresh ids from 1, default names, one undo step each, no notes and not muted or soloed") {
    DocumentStore store = threeChords();
    CHECK(store.nextTrackId() == 1);
    checkOneStep(store, [](DocumentStore& s) { CHECK(s.addTrack(TrackRole::Bass) == 1); });
    checkOneStep(store, [](DocumentStore& s) { CHECK(s.addTrack(TrackRole::Lead, std::string{}) == 2); });
    checkOneStep(store, [](DocumentStore& s) { CHECK(s.addTrack(TrackRole::Other, std::nullopt) == 3); });
    CHECK(store.nextTrackId() == 4);
    REQUIRE(store.doc().tracks.size() == 3);
    CHECK(trackAt(store, 0) == Track{1, "Bass", TrackRole::Bass, false, false, {}});
    CHECK(trackAt(store, 1) == Track{2, "Lead", TrackRole::Lead, false, false, {}});
    CHECK(trackAt(store, 2) == Track{3, "Track", TrackRole::Other, false, false, {}});
}

TEST_CASE("addTrack: a track needs no chords; default names step over taken ones") {
    DocumentStore store;  // an empty progression may hold tracks
    CHECK(store.addTrack(TrackRole::Bass, std::string{"Bass 2"}) == 1);
    CHECK(store.addTrack(TrackRole::Bass) == 2);
    CHECK(store.addTrack(TrackRole::Bass) == 3);
    std::vector<std::string> names;
    for (const Track& t : store.doc().tracks) {
        names.push_back(t.name);
    }
    CHECK(names == std::vector<std::string>{"Bass 2", "Bass", "Bass 3"});
}

TEST_CASE("addTrack: a long name is cut to kMaxTrackNameBytes at a UTF-8 character boundary") {
    DocumentStore store;
    const std::string ascii(80, 'x');
    REQUIRE(store.addTrack(TrackRole::Lead, ascii) == 1);
    CHECK(trackAt(store, 0).name == std::string(64, 'x'));
    // 63 ASCII bytes then a two-byte character straddling the bound: the character is dropped whole.
    const std::string straddling = std::string(63, 'a') + "\xC3\xA9" + "b";
    REQUIRE(store.addTrack(TrackRole::Lead, straddling) == 2);
    CHECK(trackAt(store, 1).name == std::string(63, 'a'));
}

TEST_CASE("addTrack: a fifth track is refused with a reason, no history, no id consumed") {
    DocumentStore store = threeChords();
    for (int i = 1; i <= kMaxTracks; ++i) {
        REQUIRE(store.addTrack(TrackRole::Other) == i);
    }
    const DocumentStore reference = store;
    std::string refusal;
    CHECK(store.addTrack(TrackRole::Bass, std::nullopt, &refusal) == kNoTrackId);
    CHECK(refusal == "At most 4 tracks");
    CHECK(store.addTrack(TrackRole::Bass) == kNoTrackId);  // no refusal string asked for: still refused
    checkRefused(store, reference);
    REQUIRE(store.removeTrack(2));
    CHECK(store.addTrack(TrackRole::Bass) == kMaxTracks + 1);  // room again; ids are never reused
}

TEST_CASE("addTrack: refused once the track id counter reaches INT_MAX") {
    DocumentStore store;
    store.replace(Document{}, 1, 0, INT_MAX - 1);
    CHECK(store.nextTrackId() == INT_MAX - 1);
    CHECK(store.addTrack(TrackRole::Bass) == INT_MAX - 1);
    CHECK(store.nextTrackId() == INT_MAX);
    const DocumentStore reference = store;
    std::string refusal;
    CHECK(store.addTrack(TrackRole::Bass, std::nullopt, &refusal) == kNoTrackId);
    CHECK_FALSE(refusal.empty());
    checkRefused(store, reference);
}

// --- removeTrack, patchTrack, moveTrack -----------------------------------------------------

TEST_CASE("removeTrack: one undo step taking its notes along; an unknown id is refused without history") {
    DocumentStore store = threeChordsAndBass();
    REQUIRE(store.addTrack(TrackRole::Lead) == 2);
    REQUIRE(store.insertNotes(1, {ns(36, 0, 480)}) == std::vector<int>{1});
    checkOneStep(store, [](DocumentStore& s) { CHECK(s.removeTrack(1)); });
    CHECK(trackIdsOf(store) == std::vector<int>{2});
    const DocumentStore reference = store;
    std::string refusal;
    CHECK_FALSE(store.removeTrack(1, &refusal));
    CHECK(refusal == "No track with id 1");
    CHECK_FALSE(store.removeTrack(kNoTrackId));
    checkRefused(store, reference);
}

TEST_CASE("patchTrack: the name, the role, or both in ONE undo step") {
    DocumentStore store = threeChordsAndBass();
    checkOneStep(store, [](DocumentStore& s) { CHECK(s.patchTrack(1, std::string{"Walking"}, std::nullopt)); });
    CHECK(trackAt(store, 0).name == "Walking");
    CHECK(trackAt(store, 0).role == TrackRole::Bass);
    checkOneStep(store, [](DocumentStore& s) { CHECK(s.patchTrack(1, std::nullopt, TrackRole::Lead)); });
    CHECK(trackAt(store, 0).role == TrackRole::Lead);
    checkOneStep(store, [](DocumentStore& s) { CHECK(s.patchTrack(1, std::string{"Tune"}, TrackRole::Other)); });
    CHECK(trackAt(store, 0).name == "Tune");
    CHECK(trackAt(store, 0).role == TrackRole::Other);
    REQUIRE(store.undo());  // both fields came back with one undo
    CHECK(trackAt(store, 0).name == "Walking");
    CHECK(trackAt(store, 0).role == TrackRole::Lead);
}

TEST_CASE("patchTrack: names are cut like addTrack's; an empty name is stored empty, as renameSection stores it") {
    DocumentStore store = threeChordsAndBass();
    CHECK(store.patchTrack(1, std::string(70, 'n'), std::nullopt));
    CHECK(trackAt(store, 0).name == std::string(64, 'n'));
    CHECK(store.patchTrack(1, std::string{}, std::nullopt));
    CHECK(trackAt(store, 0).name.empty());
}

TEST_CASE("patchTrack: no field and an unknown id are refused; nothing to change is a no-op without a reason") {
    DocumentStore store = threeChordsAndBass();
    const DocumentStore reference = store;
    std::string refusal;
    CHECK_FALSE(store.patchTrack(1, std::nullopt, std::nullopt, &refusal));
    CHECK_FALSE(refusal.empty());
    refusal.clear();
    CHECK_FALSE(store.patchTrack(9, std::string{"X"}, std::nullopt, &refusal));
    CHECK(refusal == "No track with id 9");
    refusal = "stale text";
    CHECK_FALSE(store.patchTrack(1, std::string{"Bass"}, TrackRole::Bass, &refusal));  // already so
    CHECK(refusal.empty());
    CHECK_FALSE(store.patchTrack(1, std::string{"Bass"}, std::nullopt, &refusal));
    CHECK(refusal.empty());
    checkRefused(store, reference);
}

TEST_CASE("moveTrack: to an index clamped into the list, one undo step; the current index is a no-op") {
    DocumentStore store = threeChords();
    for (int i = 1; i <= 4; ++i) {
        REQUIRE(store.addTrack(TrackRole::Other) == i);
    }
    checkOneStep(store, [](DocumentStore& s) { CHECK(s.moveTrack(1, 2)); });
    CHECK(trackIdsOf(store) == std::vector<int>{2, 3, 1, 4});
    checkOneStep(store, [](DocumentStore& s) { CHECK(s.moveTrack(4, 0)); });
    CHECK(trackIdsOf(store) == std::vector<int>{4, 2, 3, 1});
    CHECK(store.moveTrack(4, 99));
    CHECK(trackIdsOf(store) == std::vector<int>{2, 3, 1, 4});
    CHECK(store.moveTrack(1, -5));
    CHECK(trackIdsOf(store) == std::vector<int>{1, 2, 3, 4});
    const DocumentStore reference = store;
    std::string refusal = "stale text";
    CHECK_FALSE(store.moveTrack(3, 2, &refusal));  // already there
    CHECK(refusal.empty());
    CHECK_FALSE(store.moveTrack(4, 50, &refusal));  // clamps to where it is
    CHECK(refusal.empty());
    CHECK_FALSE(store.moveTrack(7, 0, &refusal));
    CHECK(refusal == "No track with id 7");
    checkRefused(store, reference);
}

// --- insertNotes -----------------------------------------------------------------------------

TEST_CASE("insertNotes: fresh increasing ids in spec order, one undo step, never reused after undo") {
    DocumentStore store = threeChordsAndBass();
    CHECK(store.nextNoteId() == 1);
    checkOneStep(store, [](DocumentStore& s) {
        CHECK(s.insertNotes(1, {ns(40, 100, 480), ns(36, 0, 480), ns(43, 200, 480)}) == std::vector<int>{1, 2, 3});
    });
    CHECK(store.nextNoteId() == 4);
    CHECK(store.insertNotes(1, {ns(38, 4000, 240)}) == std::vector<int>{4});
    REQUIRE(store.undo());
    CHECK(store.insertNotes(1, {ns(38, 4000, 240)}) == std::vector<int>{5});  // 4 is never handed out again
    CHECK(store.nextNoteId() == 6);
}

TEST_CASE("insertNotes: an absolute start resolves to its owning chord and offset; a boundary belongs to the next chord") {
    DocumentStore store = threeChordsAndBass();
    REQUIRE(store.insertNotes(1, {ns(36, 0, 480), ns(41, kBar, 480), ns(43, kBar + 1160, 480, 90),
                                  ns(31, 3 * kBar - 1, 960)}) == std::vector<int>{1, 2, 3, 4});
    CHECK(notesOf(store) == std::vector<TrackNote>{TrackNote{1, 36, 1, 0, 480, 100}, TrackNote{2, 41, 2, 0, 480, 100},
                                                   TrackNote{3, 43, 2, 1160, 480, 90},
                                                   TrackNote{4, 31, 3, kBar - 1, 960, 100}});
}

TEST_CASE("insertNotes: a start at or after the end, or any refused spec, refuses the whole insert") {
    DocumentStore store = threeChordsAndBass();
    REQUIRE(store.insertNotes(1, {ns(36, 0, 480)}) == std::vector<int>{1});
    const DocumentStore reference = store;
    std::string refusal;
    CHECK(store.insertNotes(1, {ns(36, 3 * kBar, 480)}, &refusal).empty());
    CHECK(refusal == "A note must start before the end of the progression");
    refusal.clear();
    CHECK(store.insertNotes(1, {ns(36, 480, 480), ns(38, 3 * kBar + 5, 480)}, &refusal).empty());  // no partial insert
    CHECK(refusal == "A note must start before the end of the progression");
    checkRefused(store, reference);
}

TEST_CASE("insertNotes / updateNotes: a refusal leaves the redo stack intact") {
    DocumentStore store = threeChordsAndBass();
    REQUIRE(store.insertNotes(1, {ns(36, 0, 480)}) == std::vector<int>{1});
    REQUIRE(store.insertNotes(1, {ns(38, 480, 480)}) == std::vector<int>{2});
    const Document redone = store.doc();
    REQUIRE(store.undo());  // the redo stack now holds the second insert
    const DocumentStore reference = store;
    std::string refusal;
    CHECK(store.insertNotes(1, {ns(40, 3 * kBar, 480)}, &refusal).empty());
    CHECK(refusal == "A note must start before the end of the progression");
    NoteEdit pastEnd = edit(1);
    pastEnd.startTick = 3 * kBar;
    CHECK_FALSE(store.updateNotes(1, {pastEnd}, &refusal));
    CHECK(refusal == "A note must start before the end of the progression");
    checkRefused(store, reference);
    REQUIRE(store.redo());
    CHECK(store.doc() == redone);
}

TEST_CASE("insertNotes: an empty progression refuses with \"Add chords first\"") {
    DocumentStore store;
    REQUIRE(store.addTrack(TrackRole::Bass) == 1);
    const DocumentStore reference = store;
    std::string refusal;
    CHECK(store.insertNotes(1, {ns(36, 0, 480)}, &refusal).empty());
    CHECK(refusal == "Add chords first");
    checkRefused(store, reference);
}

TEST_CASE("insertNotes: empty specs and an unknown track are refused") {
    DocumentStore store = threeChordsAndBass();
    const DocumentStore reference = store;
    std::string refusal;
    CHECK(store.insertNotes(1, {}, &refusal).empty());
    CHECK_FALSE(refusal.empty());
    refusal.clear();
    CHECK(store.insertNotes(5, {ns(36, 0, 480)}, &refusal).empty());
    CHECK(refusal == "No track with id 5");
    checkRefused(store, reference);
}

TEST_CASE("insertNotes: at most kMaxNotesPerTrack per track; exactly the limit is allowed") {
    // One note per beat, so the onset and polyphony limits stay out of it: kMaxNotesPerTrack beats of chords.
    DocumentStore store;
    for (int i = 0; i < kMaxNotesPerTrack / 64; ++i) {
        REQUIRE(store.appendChord(kC, kMaxLengthBeats) == i + 1);
    }
    REQUIRE(store.addTrack(TrackRole::Bass) == 1);
    REQUIRE(store.addTrack(TrackRole::Lead) == 2);
    std::vector<NoteSpec> specs;
    for (int i = 0; i < kMaxNotesPerTrack - 1; ++i) {
        specs.push_back(ns(40 + i % 40, std::int64_t{i} * kFeelTicksPerBeat, 30));
    }
    REQUIRE(store.insertNotes(1, specs).size() == static_cast<std::size_t>(kMaxNotesPerTrack - 1));
    const DocumentStore reference = store;
    std::string refusal;
    CHECK(store.insertNotes(1, {ns(60, 0, 30), ns(61, 0, 30)}, &refusal).empty());
    CHECK(refusal == "At most 1024 notes per track");
    checkRefused(store, reference);
    CHECK(store.insertNotes(1, {ns(60, 0, 30)}).size() == 1);  // exactly 1024
    CHECK(notesOf(store).size() == static_cast<std::size_t>(kMaxNotesPerTrack));
    CHECK(store.insertNotes(1, {ns(61, 0, 30)}).empty());
    CHECK(store.insertNotes(2, {ns(61, 0, 30)}).size() == 1);  // the limit is per track
}

TEST_CASE("insertNotes: refused when the note id counter cannot hand out one id per note below INT_MAX") {
    DocumentStore store;
    Document doc;
    doc.events.push_back(ChordEvent{1, kC, 4.0, 100});
    doc.tracks.push_back(Track{1, "Bass", TrackRole::Bass});
    store.replace(doc, 2, 0, 0, INT_MAX - 2);
    CHECK(store.nextTrackId() == 2);
    CHECK(store.nextNoteId() == INT_MAX - 2);
    const DocumentStore reference = store;
    std::string refusal;
    CHECK(store.insertNotes(1, {ns(36, 0, 30), ns(37, 0, 30), ns(38, 0, 30)}, &refusal).empty());
    CHECK_FALSE(refusal.empty());
    checkRefused(store, reference);
    CHECK(store.insertNotes(1, {ns(36, 0, 30), ns(37, 0, 30)}) == std::vector<int>{INT_MAX - 2, INT_MAX - 1});
    CHECK(store.nextNoteId() == INT_MAX);
    CHECK(store.insertNotes(1, {ns(38, 0, 30)}).empty());
}

TEST_CASE("insertNotes: the track's notes are sorted by derived start, pitch, id") {
    DocumentStore store = threeChordsAndBass();
    REQUIRE(store.insertNotes(1, {ns(50, kBar, 30), ns(40, 500, 30), ns(45, kBar, 30), ns(45, kBar, 60)}) ==
            std::vector<int>{1, 2, 3, 4});
    CHECK(noteIdsOf(store) == std::vector<int>{2, 3, 4, 1});
    REQUIRE(store.insertNotes(1, {ns(45, kBar, 90), ns(99, 0, 30)}) == std::vector<int>{5, 6});
    CHECK(noteIdsOf(store) == std::vector<int>{6, 2, 3, 4, 5, 1});
}

TEST_CASE("insertNotes: pitch, velocity, start and length clamp, the start first") {
    DocumentStore store = threeChordsAndBass();
    REQUIRE(store.insertNotes(1, {ns(-5, -100, 5, 0), ns(200, 480, 480, 300), ns(60, 960, 480)}).size() == 3);
    CHECK(notesOf(store) == std::vector<TrackNote>{TrackNote{1, 0, 1, 0, kMinTrackNoteTicks, 1},
                                                   TrackNote{2, 127, 1, 480, 480, 127},
                                                   TrackNote{3, 60, 1, 960, 480, 100}});  // velocity defaults to 100
    // Extreme 64-bit values clamp, never overflow: the start to 0, the length to the whole domain.
    REQUIRE(store.insertNotes(1, {ns(60, std::numeric_limits<std::int64_t>::min(),
                                     std::numeric_limits<std::int64_t>::max())}) == std::vector<int>{4});
    CHECK(notesOf(store).at(1) == TrackNote{4, 60, 1, 0, kMaxNoteTick, 100});
}

TEST_CASE("insertNotes: the last 30 ticks of the domain hold no start; the length clamps to the space left") {
    DocumentStore store = fullDocumentAndLead();
    constexpr std::int32_t lastStart = kMaxNoteTick - kMinTrackNoteTicks;
    constexpr std::int32_t lastChordStart = kMaxNoteTick - 64 * kFeelTicksPerBeat;
    REQUIRE(store.doc().endBeat() == kMaxChords * kMaxLengthBeats);
    REQUIRE(store.insertNotes(1, {ns(60, kMaxNoteTick - 10, 1000), ns(61, std::numeric_limits<std::int64_t>::max(), 5),
                                  ns(62, lastStart - 1, 1000), ns(63, lastStart, 1000)}) ==
            std::vector<int>{1, 2, 3, 4});
    // Every start clamps into [0, kMaxNoteTick - 30] before it is resolved, so each lands in the last chord.
    CHECK(notesOf(store) == std::vector<TrackNote>{
                                TrackNote{3, 62, kMaxChords, lastStart - 1 - lastChordStart, kMinTrackNoteTicks + 1, 100},
                                TrackNote{1, 60, kMaxChords, lastStart - lastChordStart, kMinTrackNoteTicks, 100},
                                TrackNote{2, 61, kMaxChords, lastStart - lastChordStart, kMinTrackNoteTicks, 100},
                                TrackNote{4, 63, kMaxChords, lastStart - lastChordStart, kMinTrackNoteTicks, 100}});
}

// --- removeNotes -----------------------------------------------------------------------------

TEST_CASE("removeNotes: the listed notes that exist, as one undo step; unknown ids skipped") {
    DocumentStore store = threeChordsAndBass();
    REQUIRE(store.insertNotes(1, {ns(36, 0, 480), ns(38, 480, 480), ns(40, 960, 480)}).size() == 3);
    checkOneStep(store, [](DocumentStore& s) { CHECK(s.removeNotes(1, {3, 99, 1, 3})); });
    CHECK(noteIdsOf(store) == std::vector<int>{2});
    CHECK(store.nextNoteId() == 4);
}

TEST_CASE("removeNotes: none existing is a no-op without history or reason; an unknown track is refused") {
    DocumentStore store = threeChordsAndBass();
    REQUIRE(store.addTrack(TrackRole::Lead) == 2);
    REQUIRE(store.insertNotes(1, {ns(36, 0, 480)}) == std::vector<int>{1});
    const DocumentStore reference = store;
    std::string refusal = "stale text";
    CHECK_FALSE(store.removeNotes(1, {98, 99}, &refusal));
    CHECK(refusal.empty());
    CHECK_FALSE(store.removeNotes(1, {}, &refusal));
    CHECK(refusal.empty());
    CHECK_FALSE(store.removeNotes(2, {1}, &refusal));  // note 1 is in another track
    CHECK(refusal.empty());
    CHECK_FALSE(store.removeNotes(3, {1}, &refusal));
    CHECK(refusal == "No track with id 3");
    checkRefused(store, reference);
}

// --- updateNotes -----------------------------------------------------------------------------

TEST_CASE("updateNotes: absolute values, clamped, in one undo step") {
    DocumentStore store = threeChordsAndBass();
    REQUIRE(store.insertNotes(1, {ns(36, 0, 480), ns(38, 480, 480)}).size() == 2);
    NoteEdit a = edit(1);
    a.pitch = 300;
    a.velocity = 64;
    NoteEdit b = edit(2);
    b.lengthTicks = 5;
    b.startTick = 600;
    checkOneStep(store, [&](DocumentStore& s) { CHECK(s.updateNotes(1, {a, b})); });
    CHECK(notesOf(store) == std::vector<TrackNote>{TrackNote{1, 127, 1, 0, 480, 64},
                                                   TrackNote{2, 38, 1, 600, kMinTrackNoteTicks, 100}});
    NoteEdit c = edit(1);
    c.velocity = 0;
    c.pitch = -1;
    c.lengthTicks = 960;
    CHECK(store.updateNotes(1, {c}));
    CHECK(notesOf(store).at(0) == TrackNote{1, 0, 1, 0, 960, 1});
}

TEST_CASE("updateNotes: a start moved across a boundary re-anchors; onto a boundary it belongs to the next chord") {
    DocumentStore store = threeChordsAndBass();
    REQUIRE(store.insertNotes(1, {ns(36, 100, 480), ns(38, kBar + 100, 480)}).size() == 2);
    NoteEdit later = edit(1);
    later.startTick = kBar;  // exactly chord 2's start
    NoteEdit earlier = edit(2);
    earlier.startTick = kBar - 1;  // the last tick of chord 1
    CHECK(store.updateNotes(1, {later, earlier}));
    CHECK(notesOf(store) == std::vector<TrackNote>{TrackNote{2, 38, 1, kBar - 1, 480, 100},
                                                   TrackNote{1, 36, 2, 0, 480, 100}});
    NoteEdit far = edit(2);
    far.startTick = 2 * kBar + 7;
    CHECK(store.updateNotes(1, {far}));
    CHECK(notesOf(store).at(1) == TrackNote{2, 38, 3, 7, 480, 100});  // re-sorted after the edit
    NoteEdit negative = edit(2);
    negative.startTick = -50;  // clamps to 0, which chord 1 owns
    CHECK(store.updateNotes(1, {negative}));
    CHECK(notesOf(store).at(0) == TrackNote{2, 38, 1, 0, 480, 100});
}

TEST_CASE("updateNotes: a start moved across a boundary re-anchors as one undo step that undo and redo round-trip") {
    DocumentStore store = threeChordsAndBass();
    REQUIRE(store.insertNotes(1, {ns(36, kBar - 200, 480)}) == std::vector<int>{1});
    NoteEdit across = edit(1);
    across.pitch = 38;
    across.startTick = kBar + 300;
    checkOneStep(store, [&](DocumentStore& s) { CHECK(s.updateNotes(1, {across})); });
    CHECK(notesOf(store) == std::vector<TrackNote>{TrackNote{1, 38, 2, 300, 480, 100}});
    REQUIRE(store.undo());
    CHECK(notesOf(store) == std::vector<TrackNote>{TrackNote{1, 36, 1, kBar - 200, 480, 100}});
    REQUIRE(store.redo());
    CHECK(notesOf(store) == std::vector<TrackNote>{TrackNote{1, 38, 2, 300, 480, 100}});
}

TEST_CASE("updateNotes: an unchanged start keeps an inactive note's anchor and offset (D15)") {
    DocumentStore store;
    const int first = store.appendChord(kC, 4.0, 100);
    const int second = store.appendChord(kF, 4.0, 100);
    REQUIRE(store.addTrack(TrackRole::Bass) == 1);
    REQUIRE(store.insertNotes(1, {ns(36, 3000, 480)}) == std::vector<int>{1});
    store.setLength(first, 2.0);  // boundaries 0, 1920, 5760: tick 3000 now lies in the second chord
    REQUIRE(notesOf(store) == std::vector<TrackNote>{TrackNote{1, 36, first, 3000, 480, 100}});
    REQUIRE_FALSE(noteActive(store.doc(), notesOf(store).at(0)));
    NoteEdit pitch = edit(1);
    pitch.pitch = 40;
    pitch.startTick = 3000;  // the snapshot's start, re-sent unchanged
    checkOneStep(store, [&](DocumentStore& s) { CHECK(s.updateNotes(1, {pitch})); });
    CHECK(notesOf(store) == std::vector<TrackNote>{TrackNote{1, 40, first, 3000, 480, 100}});
    CHECK_FALSE(noteActive(store.doc(), notesOf(store).at(0)));
    NoteEdit length = edit(1);
    length.startTick = 3000;
    length.lengthTicks = 960;
    CHECK(store.updateNotes(1, {length}));
    CHECK(notesOf(store) == std::vector<TrackNote>{TrackNote{1, 40, first, 3000, 960, 100}});
    NoteEdit moved = edit(1);
    moved.startTick = 3001;  // a real move re-anchors (and so reactivates)
    CHECK(store.updateNotes(1, {moved}));
    CHECK(notesOf(store) == std::vector<TrackNote>{TrackNote{1, 40, second, 3001 - 1920, 960, 100}});
    CHECK(noteActive(store.doc(), notesOf(store).at(0)));
}

TEST_CASE("updateNotes: an unchanged start at or after the end is accepted; only a moved start must have an owner") {
    DocumentStore store;
    const int only = store.appendChord(kC, 4.0, 100);
    REQUIRE(store.addTrack(TrackRole::Bass) == 1);
    REQUIRE(store.insertNotes(1, {ns(36, 3000, 480)}) == std::vector<int>{1});
    store.setLength(only, 2.0);  // the progression ends at 1920, before the note's derived start
    REQUIRE(noteStart(store.doc(), notesOf(store).at(0)) == 3000);
    NoteEdit pitch = edit(1);
    pitch.pitch = 41;
    pitch.startTick = 3000;
    checkOneStep(store, [&](DocumentStore& s) { CHECK(s.updateNotes(1, {pitch})); });
    CHECK(notesOf(store) == std::vector<TrackNote>{TrackNote{1, 41, only, 3000, 480, 100}});
    NoteEdit length = edit(1);
    length.lengthTicks = 1000;
    checkOneStep(store, [&](DocumentStore& s) { CHECK(s.updateNotes(1, {length})); });
    CHECK(notesOf(store) == std::vector<TrackNote>{TrackNote{1, 41, only, 3000, 1000, 100}});
    const DocumentStore reference = store;
    NoteEdit moved = edit(1);
    moved.startTick = 2999;
    std::string refusal;
    CHECK_FALSE(store.updateNotes(1, {moved}, &refusal));
    CHECK(refusal == "A note must start before the end of the progression");
    checkRefused(store, reference);
}

TEST_CASE("a chord edit that would push a note past the note range is refused; the note keeps its place") {
    // 256 chords, the first one beat short: the last starts 960 ticks early, so a note 5 ticks before the end of
    // its 64 beats fits the start domain. Growing the first chord would move it to kMaxNoteTick - 5, past the start
    // domain: refused with the validator's message (Task 2a; before it, this was how a store document got there, and
    // `updateNotes`' clamp of such a derived start is now a defence no store document reaches). A note whose end
    // the growth would push past kMaxNoteTick is refused the same way.
    DocumentStore store;
    const int first = store.appendChord(kC, kMaxLengthBeats - 1.0);
    int last = first;
    for (int i = 1; i < kMaxChords; ++i) {
        last = store.appendChord(kC, kMaxLengthBeats);
    }
    REQUIRE(store.addTrack(TrackRole::Lead) == 1);
    constexpr std::int32_t offset = 64 * kFeelTicksPerBeat - 5;
    REQUIRE(store.insertNotes(1, {ns(60, kMaxNoteTick - kFeelTicksPerBeat - 5, 480)}) == std::vector<int>{1});
    REQUIRE(notesOf(store) == std::vector<TrackNote>{TrackNote{1, 60, last, offset, 480, 100}});
    const DocumentStore reference = store;
    std::string refusal;
    CHECK_FALSE(store.setLength(first, kMaxLengthBeats, &refusal));
    CHECK(refusal == "Note 1 starts past the end of the note range");
    checkRefused(store, reference);
    CHECK_FALSE(store.setLengths({first}, kMaxLengthBeats, &refusal));
    CHECK(refusal == "Note 1 starts past the end of the note range");
    checkRefused(store, reference);
    // A note sounding to the very end of the range, 960 ticks earlier: the same growth would push its end past it.
    NoteEdit earlier = edit(1);
    earlier.startTick = kMaxNoteTick - 2 * kFeelTicksPerBeat;
    earlier.lengthTicks = 2 * kFeelTicksPerBeat;
    REQUIRE(store.updateNotes(1, {earlier}));
    const DocumentStore toTheEnd = store;
    CHECK_FALSE(store.setLength(first, kMaxLengthBeats, &refusal));
    CHECK(refusal == "Note 1 has a length outside " + std::to_string(kMinTrackNoteTicks) + ".." +
                     std::to_string(kFeelTicksPerBeat) + " ticks");
    checkRefused(store, toTheEnd);
}

TEST_CASE("updateNotes: a start at or after the end, an unknown id or a repeated id refuses the whole edit") {
    DocumentStore store = threeChordsAndBass();
    REQUIRE(store.insertNotes(1, {ns(36, 0, 480), ns(38, 480, 480)}).size() == 2);
    const DocumentStore reference = store;
    NoteEdit pitchOnly = edit(1);
    pitchOnly.pitch = 50;
    NoteEdit pastEnd = edit(2);
    pastEnd.startTick = 3 * kBar;
    NoteEdit unknown = edit(99);
    unknown.pitch = 40;
    NoteEdit again = edit(1);
    again.pitch = 51;
    std::string refusal;
    CHECK_FALSE(store.updateNotes(1, {pitchOnly, pastEnd}, &refusal));
    CHECK(refusal == "A note must start before the end of the progression");
    refusal.clear();
    CHECK_FALSE(store.updateNotes(1, {pitchOnly, unknown}, &refusal));
    CHECK(refusal == "No note with id 99 in track 1");
    refusal.clear();
    CHECK_FALSE(store.updateNotes(1, {pitchOnly, again}, &refusal));
    CHECK_FALSE(refusal.empty());
    refusal.clear();
    CHECK_FALSE(store.updateNotes(4, {pitchOnly}, &refusal));
    CHECK(refusal == "No track with id 4");
    checkRefused(store, reference);
}

TEST_CASE("updateNotes: values equal to the current ones, or no fields, are a no-op without history or reason") {
    DocumentStore store = threeChordsAndBass();
    REQUIRE(store.insertNotes(1, {ns(36, kBar + 10, 480, 127)}).size() == 1);
    const DocumentStore reference = store;
    NoteEdit same = edit(1);
    same.pitch = 36;
    same.startTick = kBar + 10;
    same.lengthTicks = 480;
    same.velocity = 127;
    NoteEdit clampsToSame = edit(1);
    clampsToSame.velocity = 500;
    std::string refusal = "stale text";
    CHECK_FALSE(store.updateNotes(1, {same}, &refusal));
    CHECK(refusal.empty());
    CHECK_FALSE(store.updateNotes(1, {clampsToSame}, &refusal));
    CHECK(refusal.empty());
    CHECK_FALSE(store.updateNotes(1, {edit(1)}, &refusal));
    CHECK(refusal.empty());
    CHECK_FALSE(store.updateNotes(1, {}, &refusal));
    CHECK(refusal.empty());
    checkRefused(store, reference);
}

TEST_CASE("updateNotes: a length alone clamps against the note's current start; a new start re-clamps the length") {
    DocumentStore store = fullDocumentAndLead();
    constexpr std::int32_t lastChordStart = kMaxNoteTick - 64 * kFeelTicksPerBeat;
    REQUIRE(store.insertNotes(1, {ns(60, kMaxNoteTick - 100, 30), ns(62, lastChordStart, 3000)}) ==
            std::vector<int>{1, 2});
    NoteEdit longer = edit(1);
    longer.lengthTicks = 5000;
    CHECK(store.updateNotes(1, {longer}));
    CHECK(notesOf(store).at(1) == TrackNote{1, 60, kMaxChords, 64 * 960 - 100, 100, 100});
    NoteEdit moved = edit(2);
    moved.startTick = kMaxNoteTick - 40;  // its 3000-tick length no longer fits
    CHECK(store.updateNotes(1, {moved}));
    CHECK(notesOf(store).at(1) == TrackNote{2, 62, kMaxChords, 64 * 960 - 40, 40, 100});
}

// --- counters, replace --------------------------------------------------------------------

TEST_CASE("replace: derives the track and note counters one past the largest ids, or keeps explicit ones") {
    Document doc;
    doc.events.push_back(ChordEvent{1, kC, 4.0, 100});
    doc.tracks.push_back(Track{3, "Bass", TrackRole::Bass, false, false, {TrackNote{8, 36, 1, 0, 480, 100}}});
    doc.tracks.push_back(Track{2, "Lead", TrackRole::Lead, true, false, {TrackNote{11, 72, 1, 0, 480, 100}}});
    DocumentStore store = threeChordsAndBass();
    store.replace(doc, 2);
    CHECK(store.doc() == doc);
    CHECK(store.nextTrackId() == 4);
    CHECK(store.nextNoteId() == 12);
    CHECK(store.addTrack(TrackRole::Other) == 4);
    CHECK(store.insertNotes(2, {ns(74, 0, 30)}) == std::vector<int>{12});

    DocumentStore explicitCounters;
    explicitCounters.replace(doc, 2, 0, 40, 50);
    CHECK(explicitCounters.nextTrackId() == 40);
    CHECK(explicitCounters.nextNoteId() == 50);

    DocumentStore none = threeChordsAndBass();
    REQUIRE(none.insertNotes(1, {ns(36, 0, 30)}).size() == 1);
    none.replace(Document{}, 1);
    CHECK(none.doc().tracks.empty());
    CHECK(none.nextTrackId() == 1);
    CHECK(none.nextNoteId() == 1);
}

TEST_CASE("findTrackIndex: the position of a track, -1 for an unknown id") {
    DocumentStore store = threeChordsAndBass();
    REQUIRE(store.addTrack(TrackRole::Lead) == 2);
    CHECK(store.findTrackIndex(1) == 0);
    CHECK(store.findTrackIndex(2) == 1);
    CHECK(store.findTrackIndex(3) == -1);
    CHECK(store.findTrackIndex(kNoTrackId) == -1);
}

TEST_CASE("revision: every track and note edit moves it once; refusals and no-ops leave it") {
    DocumentStore store = threeChords();
    std::uint64_t r = store.revision();
    const auto bumped = [&](bool expectBump) {
        const std::uint64_t now = store.revision();
        CHECK(now == (expectBump ? r + 1 : r));
        r = now;
    };
    REQUIRE(store.addTrack(TrackRole::Bass) == 1);
    bumped(true);
    CHECK(store.patchTrack(1, std::string{"Low"}, TrackRole::Lead));
    bumped(true);
    CHECK_FALSE(store.patchTrack(1, std::string{"Low"}, std::nullopt));
    bumped(false);
    REQUIRE(store.insertNotes(1, {ns(36, 0, 480), ns(38, 480, 480)}).size() == 2);
    bumped(true);
    CHECK(store.insertNotes(1, {ns(36, 3 * kBar, 480)}).empty());
    bumped(false);
    NoteEdit e = edit(1);
    e.pitch = 40;
    CHECK(store.updateNotes(1, {e}));
    bumped(true);
    CHECK_FALSE(store.updateNotes(1, {e}));
    bumped(false);
    CHECK(store.removeNotes(1, {2}));
    bumped(true);
    CHECK_FALSE(store.removeNotes(1, {2}));
    bumped(false);
    REQUIRE(store.addTrack(TrackRole::Lead) == 2);
    bumped(true);
    CHECK(store.moveTrack(2, 0));
    bumped(true);
    CHECK(store.removeTrack(2));
    bumped(true);
    CHECK_FALSE(store.removeTrack(2));
    bumped(false);
}

// --- the track invariant (D7) and the playback limits (D10) ---------------------------------

namespace {

/// A store of `n` four-beat C chords (ids 1..n): boundaries at multiples of kBar.
DocumentStore bars(int n) {
    DocumentStore store;
    for (int i = 0; i < n; ++i) {
        REQUIRE(store.appendChord(kC, 4.0, 100) == i + 1);
    }
    return store;
}

/// `threeChords()`'s chords (ids 1..3, boundaries 0, 3840, 7680, 11520) with `tracks`: a hand-built document.
Document threeChordDoc(std::vector<Track> tracks) {
    Document doc = threeChords().doc();
    doc.tracks = std::move(tracks);
    return doc;
}

/// kMaxChords chords of kMaxLengthBeats (ids 1..kMaxChords), ending at kMaxNoteTick, with `tracks`.
Document fullDoc(std::vector<Track> tracks) {
    Document doc;
    for (int i = 0; i < kMaxChords; ++i) {
        doc.events.push_back(ChordEvent{i + 1, kC, kMaxLengthBeats, 100});
    }
    doc.tracks = std::move(tracks);
    return doc;
}

TrackNote tn(int id, int pitch, int anchor, std::int32_t offset, std::int32_t length, int velocity = 100) {
    return TrackNote{id, pitch, anchor, offset, length, velocity};
}

Track track(int id, std::vector<TrackNote> notes = {}) {
    return Track{id, "T", TrackRole::Other, false, false, std::move(notes)};
}

/// validateTracks with generous counters, returning the message ("" when valid).
std::string whyInvalid(const Document& doc, int nextTrackId = 1000, int nextNoteId = 100000) {
    std::string error = "stale";
    const bool ok = validateTracks(doc, nextTrackId, nextNoteId, error);
    CHECK(ok == error.empty());
    return error;
}

const std::string kPolyphonyMessage =
    "At most " + std::to_string(kMaxTrackPolyphony) + " track notes can sound at once";
const std::string kOnsetMessage =
    "At most " + std::to_string(kMaxTrackOnsetsPerBeat) + " notes can start within one beat across all tracks";

}  // namespace

TEST_CASE("validateTracks: a document the store built is valid with the store's counters, not with lower ones") {
    DocumentStore store = threeChordsAndBass();
    REQUIRE(store.addTrack(TrackRole::Lead) == 2);
    REQUIRE(store.insertNotes(1, {ns(36, 0, 480), ns(38, 480, 480)}) == std::vector<int>{1, 2});
    REQUIRE(store.insertNotes(2, {ns(72, 0, 480)}) == std::vector<int>{3});
    std::string error = "stale";
    CHECK(validateTracks(store.doc(), store.nextTrackId(), store.nextNoteId(), error));
    CHECK(error.empty());
    CHECK_FALSE(validateTracks(store.doc(), store.nextTrackId(), 3, error));  // note 3 at the counter
    CHECK_FALSE(error.empty());
    CHECK_FALSE(validateTracks(store.doc(), 2, store.nextNoteId(), error));  // track 2 at the counter
    CHECK_FALSE(error.empty());
    CHECK(whyInvalid(Document{}, 1, 1).empty());
    CHECK_FALSE(whyInvalid(Document{}, 0, 1).empty());  // counters start at 1
    CHECK_FALSE(whyInvalid(Document{}, 1, 0).empty());
}

TEST_CASE("addTrack / insertNotes: validate against the counters they will publish, not today's") {
    DocumentStore store = threeChords();
    const int firstTrack = store.nextTrackId();
    REQUIRE(store.addTrack(TrackRole::Bass) == firstTrack);  // id == today's counter: valid only with the next one
    CHECK(store.nextTrackId() == firstTrack + 1);
    const int firstNote = store.nextNoteId();
    REQUIRE(store.insertNotes(firstTrack, {ns(36, 0, 480), ns(38, 480, 480), ns(40, 960, 480)}) ==
            std::vector<int>{firstNote, firstNote + 1, firstNote + 2});
    CHECK(store.nextNoteId() == firstNote + 3);
    std::string error;
    CHECK(validateTracks(store.doc(), store.nextTrackId(), store.nextNoteId(), error));
    CHECK_FALSE(validateTracks(store.doc(), store.nextTrackId(), firstNote, error));  // today's counter refuses it
    CHECK_FALSE(validateTracks(store.doc(), firstTrack, store.nextNoteId(), error));
}

TEST_CASE("validateTracks: track and note ids from 1, unique (note ids across all tracks), below the counters") {
    CHECK(whyInvalid(threeChordDoc({track(1, {tn(1, 36, 1, 0, 480)}), track(2, {tn(2, 72, 1, 0, 480)})})).empty());
    CHECK_FALSE(
        whyInvalid(threeChordDoc({track(1, {tn(1, 36, 1, 0, 480)}), track(2, {tn(1, 72, 1, 0, 480)})})).empty());
    CHECK_FALSE(whyInvalid(threeChordDoc({track(1, {tn(1, 36, 1, 0, 480), tn(1, 38, 1, 0, 480)})})).empty());
    CHECK_FALSE(whyInvalid(threeChordDoc({track(1), track(1)})).empty());
    CHECK_FALSE(whyInvalid(threeChordDoc({track(0)})).empty());
    CHECK_FALSE(whyInvalid(threeChordDoc({track(-3)})).empty());
    CHECK_FALSE(whyInvalid(threeChordDoc({track(1, {tn(0, 36, 1, 0, 480)})})).empty());
    CHECK_FALSE(whyInvalid(threeChordDoc({track(1, {tn(-1, 36, 1, 0, 480)})})).empty());
    // At or past the counters.
    const Document doc = threeChordDoc({track(4, {tn(9, 36, 1, 0, 480)})});
    CHECK(whyInvalid(doc, 5, 10).empty());
    CHECK_FALSE(whyInvalid(doc, 4, 10).empty());
    CHECK_FALSE(whyInvalid(doc, 5, 9).empty());
    CHECK_FALSE(whyInvalid(doc, 5, 2).empty());
}

TEST_CASE("validateTracks: a dangling anchor is rejected without deriving a start; extreme values never overflow") {
    constexpr std::int32_t big = std::numeric_limits<std::int32_t>::max();
    CHECK_FALSE(whyInvalid(threeChordDoc({track(1, {tn(1, 36, 99, 0, 480)})})).empty());
    CHECK_FALSE(whyInvalid(threeChordDoc({track(1, {tn(1, 36, 99, big, big)})})).empty());
    CHECK_FALSE(whyInvalid(threeChordDoc({track(1, {tn(1, 36, kNoEventId, 0, 480)})})).empty());
    Document noChords;
    noChords.tracks.push_back(track(1, {tn(1, 36, 1, 0, 480)}));
    CHECK_FALSE(whyInvalid(noChords).empty());
    noChords.tracks.front().notes.clear();
    CHECK(whyInvalid(noChords).empty());  // tracks need no chords, notes do
    CHECK_FALSE(whyInvalid(threeChordDoc({track(1, {tn(1, 36, 3, big, 480)})})).empty());
    CHECK_FALSE(whyInvalid(threeChordDoc({track(1, {tn(1, 36, 3, 0, big)})})).empty());
    CHECK_FALSE(whyInvalid(threeChordDoc({track(1, {tn(1, 36, 3, big, big)})})).empty());
    CHECK_FALSE(whyInvalid(threeChordDoc({track(1, {tn(1, 36, 3, std::numeric_limits<std::int32_t>::min(), 480)})}))
                    .empty());
    CHECK_FALSE(whyInvalid(threeChordDoc({track(1, {tn(1, 36, 3, 0, std::numeric_limits<std::int32_t>::min())})}))
                    .empty());
    CHECK(trackPolyphony(threeChordDoc({track(1, {tn(1, 36, 99, 0, 480)})})) == 0);  // the helpers skip it too
    CHECK(densestOnsetWindow(threeChordDoc({track(1, {tn(1, 36, 99, 0, 480)})})) == 0);
}

TEST_CASE("validateTracks: notes outside D1's domain are rejected; its edges are accepted") {
    const auto one = [](TrackNote n) { return whyInvalid(threeChordDoc({track(1, {n})})); };
    CHECK(one(tn(1, 0, 1, 0, kMinTrackNoteTicks, 1)).empty());
    CHECK(one(tn(1, 127, 1, 0, kMinTrackNoteTicks, 127)).empty());
    CHECK_FALSE(one(tn(1, -1, 1, 0, 480)).empty());
    CHECK_FALSE(one(tn(1, 128, 1, 0, 480)).empty());
    CHECK_FALSE(one(tn(1, 60, 1, 0, 480, 0)).empty());
    CHECK_FALSE(one(tn(1, 60, 1, 0, 480, 128)).empty());
    CHECK_FALSE(one(tn(1, 60, 1, -1, 480)).empty());
    CHECK_FALSE(one(tn(1, 60, 1, 0, kMinTrackNoteTicks - 1)).empty());
    CHECK(one(tn(1, 60, 1, 0, kMaxNoteTick)).empty());  // the whole domain from 0
    CHECK_FALSE(one(tn(1, 60, 1, 0, kMaxNoteTick + 1)).empty());
    CHECK(one(tn(1, 60, 2, 3000, 480)).empty());  // an inactive note (past its chord) inside the domain is fine
    // The end of the domain, in the longest document: the last chord starts at kMaxNoteTick - 64 beats.
    constexpr std::int32_t lastChord = kMaxNoteTick - 64 * kFeelTicksPerBeat;
    const auto atEnd = [](std::int32_t offset, std::int32_t length) {
        return whyInvalid(fullDoc({track(1, {tn(1, 60, kMaxChords, offset, length)})}));
    };
    CHECK(atEnd(kMaxNoteTick - kMinTrackNoteTicks - lastChord, kMinTrackNoteTicks).empty());
    CHECK_FALSE(atEnd(kMaxNoteTick - kMinTrackNoteTicks - lastChord + 1, kMinTrackNoteTicks).empty());
    CHECK_FALSE(atEnd(kMaxNoteTick - kMinTrackNoteTicks - lastChord, kMinTrackNoteTicks + 1).empty());
    CHECK(atEnd(0, 64 * kFeelTicksPerBeat).empty());
    CHECK_FALSE(atEnd(0, 64 * kFeelTicksPerBeat + 1).empty());
}

TEST_CASE("validateTracks: at most kMaxTracks tracks, names within kMaxTrackNameBytes, notes in order") {
    std::vector<Track> tracks;
    for (int i = 1; i <= kMaxTracks; ++i) {
        tracks.push_back(track(i));
    }
    CHECK(whyInvalid(threeChordDoc(tracks)).empty());
    tracks.push_back(track(kMaxTracks + 1));
    CHECK_FALSE(whyInvalid(threeChordDoc(tracks)).empty());
    Track named = track(1);
    named.name = std::string(static_cast<std::size_t>(kMaxTrackNameBytes), 'x');
    CHECK(whyInvalid(threeChordDoc({named})).empty());
    named.name.push_back('x');
    CHECK_FALSE(whyInvalid(threeChordDoc({named})).empty());
    // Sorted by (derived start, pitch, id): a later chord with a smaller offset comes later.
    CHECK(whyInvalid(threeChordDoc({track(1, {tn(2, 40, 1, 100, 30), tn(1, 36, 2, 0, 30)})})).empty());
    CHECK_FALSE(whyInvalid(threeChordDoc({track(1, {tn(1, 36, 2, 0, 30), tn(2, 40, 1, 100, 30)})})).empty());
    CHECK(whyInvalid(threeChordDoc({track(1, {tn(2, 36, 1, 0, 30), tn(1, 40, 1, 0, 30)})})).empty());
    CHECK_FALSE(whyInvalid(threeChordDoc({track(1, {tn(1, 40, 1, 0, 30), tn(2, 36, 1, 0, 30)})})).empty());
    CHECK(whyInvalid(threeChordDoc({track(1, {tn(1, 36, 1, 0, 30), tn(2, 36, 1, 0, 30)})})).empty());
    CHECK_FALSE(whyInvalid(threeChordDoc({track(1, {tn(2, 36, 1, 0, 30), tn(1, 36, 1, 0, 30)})})).empty());
}

TEST_CASE("validateTracks: at most kMaxNotesPerTrack per track, inactive notes counted; the limits skip them") {
    // A one-bar chord and a long one; every note is anchored to the first past its end, so inactive.
    Document doc;
    doc.events.push_back(ChordEvent{1, kC, 4.0, 100});
    doc.events.push_back(ChordEvent{2, kF, kMaxLengthBeats, 100});
    std::vector<TrackNote> notes;
    for (int i = 0; i < kMaxNotesPerTrack; ++i) {
        notes.push_back(tn(i + 1, 60, 1, kBar + i, 960));  // all starting within a few beats, overlapping
    }
    doc.tracks.push_back(track(1, notes));
    CHECK(whyInvalid(doc).empty());
    CHECK(trackPolyphony(doc) == 0);
    CHECK(densestOnsetWindow(doc) == 0);
    doc.tracks.front().notes.push_back(tn(kMaxNotesPerTrack + 1, 60, 1, kBar + kMaxNotesPerTrack, 960));
    CHECK_FALSE(whyInvalid(doc).empty());

    // The store counts them too: a full track of inactive notes refuses one more.
    doc.tracks.front().notes.pop_back();
    DocumentStore store;
    store.replace(doc, 3);
    const DocumentStore reference = store;
    std::string refusal;
    CHECK(store.insertNotes(1, {ns(60, kBar, 30)}, &refusal).empty());
    CHECK(refusal == "At most " + std::to_string(kMaxNotesPerTrack) + " notes per track");
    checkRefused(store, reference);
}

TEST_CASE("polyphony: at the cap accepted, one more refused, across tracks and muted included; touching notes don't overlap") {
    // Note i starts at beat i and lasts kMaxTrackPolyphony beats: all of them sound in the last beat before the
    // first ends, at most one starts in any beat.
    constexpr int cap = kMaxTrackPolyphony;
    DocumentStore store = bars(cap / 4 + 4);
    REQUIRE(store.addTrack(TrackRole::Bass) == 1);
    REQUIRE(store.addTrack(TrackRole::Lead) == 2);
    for (int i = 0; i < cap; ++i) {
        REQUIRE(store.insertNotes(1 + i % 2, {ns(40 + i, i * kFeelTicksPerBeat, cap * kFeelTicksPerBeat)}).size() == 1);
    }
    CHECK(trackPolyphony(store.doc()) == cap);
    REQUIRE(store.setTrackMute(2, true));  // muted notes still count (un-muting must never break the limit)
    const DocumentStore reference = store;
    constexpr std::int32_t firstEnd = cap * kFeelTicksPerBeat;
    std::string refusal;
    CHECK(store.insertNotes(1, {ns(100, firstEnd - 1, 30)}, &refusal).empty());
    CHECK(refusal == kPolyphonyMessage);
    checkRefused(store, reference);
    CHECK(store.insertNotes(2, {ns(100, firstEnd - 1, 30)}, &refusal).empty());
    CHECK(refusal == kPolyphonyMessage);
    checkRefused(store, reference);
    // Starting exactly where the first note ends (end == start): no overlap, still at the cap.
    const std::vector<int> touching = store.insertNotes(1, {ns(100, firstEnd, 30)}, &refusal);
    REQUIRE(touching.size() == 1);
    CHECK(refusal.empty());
    CHECK(trackPolyphony(store.doc()) == cap);
    // updateNotes validates too: moving it one tick earlier, or the first note one tick longer, is refused.
    const DocumentStore touchingRef = store;
    NoteEdit earlier = edit(touching.front());
    earlier.startTick = firstEnd - 1;
    CHECK_FALSE(store.updateNotes(1, {earlier}, &refusal));
    CHECK(refusal == kPolyphonyMessage);
    checkRefused(store, touchingRef);
    NoteEdit longer = edit(1);
    longer.lengthTicks = firstEnd + 1;
    CHECK_FALSE(store.updateNotes(1, {longer}, &refusal));
    CHECK(refusal == kPolyphonyMessage);
    checkRefused(store, touchingRef);
}

TEST_CASE("onsets: at the cap within one beat accepted, one more refused; the window slides across a bar line") {
    constexpr int cap = kMaxTrackOnsetsPerBeat;
    constexpr std::int32_t step = kFeelTicksPerBeat / cap;
    static_assert(step >= kMinTrackNoteTicks, "the notes below must not overlap, so polyphony stays out of it");
    constexpr std::int32_t base = kBar - kFeelTicksPerBeat / 2;  // the beat straddles the first bar line
    DocumentStore store = threeChordsAndBass();
    REQUIRE(store.addTrack(TrackRole::Lead) == 2);
    for (int j = 0; j < cap; ++j) {
        REQUIRE(store.insertNotes(1 + j % 2, {ns(60 + j % 12, base + j * step, kMinTrackNoteTicks)}).size() == 1);
    }
    CHECK(densestOnsetWindow(store.doc()) == cap);
    CHECK(notesOf(store, 0).front().anchorEventId == 1);
    CHECK(notesOf(store, 0).back().anchorEventId == 2);  // both chords share the window
    REQUIRE(store.setTrackMute(2, true));
    const DocumentStore reference = store;
    std::string refusal;
    CHECK(store.insertNotes(1, {ns(90, base + kFeelTicksPerBeat - 1, 30)}, &refusal).empty());  // [base, base + 960)
    CHECK(refusal == kOnsetMessage);
    checkRefused(store, reference);
    CHECK(store.insertNotes(2, {ns(90, base - 1, 30)}, &refusal).empty());  // [base - 1, base + 959)
    CHECK(refusal == kOnsetMessage);
    checkRefused(store, reference);
    // One beat after the first start: the window [base + step, base + step + 960) holds the others and it.
    CHECK(store.insertNotes(1, {ns(90, base + kFeelTicksPerBeat, 30)}, &refusal).size() == 1);
    CHECK(refusal.empty());
    CHECK(densestOnsetWindow(store.doc()) == cap);
}

TEST_CASE("the limits count active notes only: notes a shorter chord made inactive leave room") {
    constexpr int cap = kMaxTrackPolyphony;
    DocumentStore store = threeChordsAndBass();
    std::vector<NoteSpec> chord;
    for (int i = 0; i < cap; ++i) {
        chord.push_back(ns(40 + i, 2000, 480));
    }
    REQUIRE(store.insertNotes(1, chord).size() == static_cast<std::size_t>(cap));
    std::string refusal;
    CHECK(store.insertNotes(1, {ns(90, 2000, 480)}, &refusal).empty());
    CHECK(refusal == kPolyphonyMessage);
    store.setLength(1, 2.0);  // boundaries 0, 1920, ...: offset 2000 is past the first chord now
    CHECK(trackPolyphony(store.doc()) == 0);
    CHECK(densestOnsetWindow(store.doc()) == 0);
    REQUIRE(store.insertNotes(1, chord, &refusal).size() == static_cast<std::size_t>(cap));  // tick 2000 again
    CHECK(refusal.empty());
    CHECK(trackPolyphony(store.doc()) == cap);
    std::string error;
    CHECK(validateTracks(store.doc(), store.nextTrackId(), store.nextNoteId(), error));
}

TEST_CASE("trackPolyphony / densestOnsetWindow: 0 without notes; touching notes don't stack; coincident starts do") {
    CHECK(trackPolyphony(Document{}) == 0);
    CHECK(densestOnsetWindow(Document{}) == 0);
    const Document doc = threeChordDoc({track(1, {tn(1, 36, 1, 0, 480), tn(2, 38, 1, 480, 480), tn(3, 40, 1, 480, 30)}),
                                        track(2, {tn(4, 72, 1, 470, 20), tn(5, 74, 2, 0, 30)})});
    CHECK(trackPolyphony(doc) == 3);       // at 480..489: 2, 3 and 4 (1 has ended)
    CHECK(densestOnsetWindow(doc) == 4);   // [0, 960): 0, 470, 480, 480
    CHECK(densestOnsetWindow(threeChordDoc({track(1, {tn(1, 36, 1, 0, 30), tn(2, 36, 1, 960, 30)})})) == 1);
    CHECK(densestOnsetWindow(threeChordDoc({track(1, {tn(1, 36, 1, 0, 30), tn(2, 36, 1, 959, 30)})})) == 2);
}

TEST_CASE("a lengthen that would reactivate notes past the cap is refused; at the cap, edits that add no sound commit") {
    // Before Task 2a this lengthen was the one way to put a store's document past a cap, on which every track edit
    // was then shown to refuse. Chord timing edits validate now, so a store's document is always valid; addTrack,
    // patchTrack, moveTrack, removeTrack and removeNotes cannot make any note sound, so the limits can no longer
    // refuse them, and here they commit at the cap instead.
    constexpr int cap = kMaxTrackPolyphony;
    DocumentStore store = threeChordsAndBass();
    std::vector<NoteSpec> early;
    std::vector<NoteSpec> late;
    for (int i = 0; i < cap; ++i) {
        early.push_back(ns(40 + i, 2000, 3000));
        late.push_back(ns(60 + i, 2000, 480));
    }
    REQUIRE(store.addTrack(TrackRole::Lead) == 2);
    const std::vector<int> earlyIds = store.insertNotes(1, early);
    REQUIRE(earlyIds.size() == static_cast<std::size_t>(cap));
    std::string refusal;
    REQUIRE(store.setLength(1, 2.0, &refusal));  // the early notes go inactive
    REQUIRE(store.insertNotes(1, late).size() == static_cast<std::size_t>(cap));  // offset 80 in the second chord
    REQUIRE(trackPolyphony(store.doc()) == cap);
    const DocumentStore reference = store;
    CHECK_FALSE(store.setLength(1, 4.0, &refusal));  // the early ones would sound again, 2000..5000, over 3920
    CHECK(refusal == kPolyphonyMessage);
    checkRefused(store, reference);
    CHECK(store.addTrack(TrackRole::Lead, std::nullopt, &refusal) == 3);
    CHECK(refusal.empty());
    CHECK(store.patchTrack(1, std::string{"Low"}, std::nullopt, &refusal));
    CHECK(store.moveTrack(3, 0, &refusal));
    CHECK(store.removeTrack(3, &refusal));
    CHECK(store.removeNotes(1, {earlyIds.front()}, &refusal));
    CHECK(refusal.empty());
    CHECK(trackPolyphony(store.doc()) == cap);
    CHECK_FALSE(store.setLength(1, 4.0, &refusal));  // seven of them would still sound over the late ones
    CHECK(refusal == kPolyphonyMessage);
    CHECK(store.removeNotes(1, earlyIds, &refusal));
    CHECK(store.setLength(1, 4.0, &refusal));
    CHECK(refusal.empty());
    CHECK(trackPolyphony(store.doc()) == cap);
}

TEST_CASE("removeChord takes its notes along: insertNotes and updateNotes on the same track then succeed") {
    // Before Task 2a a removed chord left its notes behind with a dangling anchor, and the note edits refused
    // ("Note 1 belongs to no chord"). The removal now deletes them, so no store document holds a dangling anchor;
    // the note edits keep that guard as a defence, unreachable through the store.
    DocumentStore store;
    store.appendChord(kC, 4.0, 100);
    const int second = store.appendChord(kF, 4.0, 100);
    REQUIRE(store.addTrack(TrackRole::Bass) == 1);
    REQUIRE(store.insertNotes(1, {ns(36, 0, 480), ns(38, kBar, 480)}) == std::vector<int>{1, 2});
    REQUIRE(store.removeChord(1));
    CHECK(notesOf(store) == std::vector<TrackNote>{TrackNote{2, 38, second, 0, 480, 100}});
    std::string refusal;
    CHECK(store.insertNotes(1, {ns(40, 960, 480)}, &refusal) == std::vector<int>{3});
    CHECK(refusal.empty());
    NoteEdit pitchOnly = edit(2);
    pitchOnly.pitch = 50;
    CHECK(store.updateNotes(1, {pitchOnly}, &refusal));
    CHECK(refusal.empty());
    CHECK(notesOf(store) ==
          std::vector<TrackNote>{TrackNote{2, 50, second, 0, 480, 100}, TrackNote{3, 40, second, 960, 480, 100}});
}

// --- lane mute and solo (D8, live state) ----------------------------------------------------

TEST_CASE("laneAudible: no solo anywhere, every unmuted lane; any solo, only soloed lanes, mute ignored on them") {
    CHECK(laneAudible(false, false, false));
    CHECK_FALSE(laneAudible(true, false, false));
    CHECK_FALSE(laneAudible(false, false, true));
    CHECK_FALSE(laneAudible(true, false, true));
    CHECK(laneAudible(false, true, true));
    CHECK(laneAudible(true, true, true));
}

TEST_CASE("chordsAudible / trackAudible: the audibility table over chords and tracks") {
    DocumentStore store = threeChordsAndBass();
    REQUIRE(store.addTrack(TrackRole::Lead) == 2);
    REQUIRE(store.addTrack(TrackRole::Other) == 3);
    const auto audible = [&store]() {
        std::vector<bool> lanes{chordsAudible(store.doc())};
        for (std::size_t i = 0; i < store.doc().tracks.size(); ++i) {
            lanes.push_back(trackAudible(store.doc(), i));
        }
        return lanes;
    };
    using Lanes = std::vector<bool>;
    CHECK(audible() == Lanes{true, true, true, true});  // no solo, nothing muted
    REQUIRE(store.setTrackMute(1, true));
    CHECK(audible() == Lanes{true, false, true, true});
    REQUIRE(store.setTrackSolo(2, true));  // one solo: only it
    CHECK(audible() == Lanes{false, false, true, false});
    REQUIRE(store.setTrackMute(2, true));  // muted and soloed: the solo wins
    CHECK(audible() == Lanes{false, false, true, false});
    store.setChordsSolo(true);  // chords soloed too
    CHECK(audible() == Lanes{true, false, true, false});
    REQUIRE(store.setTrackSolo(2, false));  // chords alone soloed
    CHECK(audible() == Lanes{true, false, false, false});
    REQUIRE(store.setTrackSolo(1, true));  // a muted track soloed
    CHECK(audible() == Lanes{true, true, false, false});
    REQUIRE(store.setTrackSolo(1, false));
    store.setChordsSolo(false);
    store.setChordsMute(true);
    REQUIRE(store.setTrackMute(3, true));  // everything muted, no solo
    CHECK(audible() == Lanes{false, false, false, false});
}

TEST_CASE("setTrackMute / setTrackSolo: false for an unknown id or no change; live: no history, no revision") {
    DocumentStore store = threeChordsAndBass();
    REQUIRE(store.insertNotes(1, {ns(36, 0, 480)}).size() == 1);
    const DocumentStore reference = store;
    const Document before = store.doc();
    CHECK_FALSE(store.setTrackMute(7, true));
    CHECK_FALSE(store.setTrackSolo(7, true));
    CHECK_FALSE(store.setTrackMute(1, false));
    CHECK_FALSE(store.setTrackSolo(1, false));
    CHECK(store.doc() == before);
    CHECK(store.setTrackMute(1, true));
    CHECK_FALSE(store.setTrackMute(1, true));
    CHECK(store.setTrackSolo(1, true));
    CHECK(trackAt(store, 0).mute);
    CHECK(trackAt(store, 0).solo);
    CHECK(store.doc() != before);  // live changes count for equality (they dirty the host, D17)
    CHECK(store.revision() == reference.revision());
    // Not history: the one undo step is still the insert, and it keeps the flags.
    REQUIRE(store.undo());
    CHECK(notesOf(store).empty());
    CHECK(trackAt(store, 0).mute);
    CHECK(trackAt(store, 0).solo);
    REQUIRE(store.undo());  // the addTrack
    CHECK(store.doc().tracks.empty());
    REQUIRE(store.undo());
    REQUIRE(store.undo());
    REQUIRE(store.undo());  // the three chords
    CHECK_FALSE(store.undo());
}

TEST_CASE("track mute and solo survive undo and redo by track id; a track absent now keeps its stored flags") {
    DocumentStore store = threeChordsAndBass();
    REQUIRE(store.addTrack(TrackRole::Lead) == 2);
    REQUIRE(store.insertNotes(1, {ns(36, 0, 480)}).size() == 1);
    REQUIRE(store.moveTrack(2, 0));  // order Lead, Bass: flags follow ids, not positions
    REQUIRE(store.setTrackMute(1, true));
    REQUIRE(store.setTrackSolo(2, true));
    REQUIRE(store.undo());  // the move: Bass, Lead again
    CHECK(trackIdsOf(store) == std::vector<int>{1, 2});
    CHECK(trackAt(store, 0).mute);
    CHECK_FALSE(trackAt(store, 0).solo);
    CHECK_FALSE(trackAt(store, 1).mute);
    CHECK(trackAt(store, 1).solo);
    REQUIRE(store.setTrackMute(1, false));
    REQUIRE(store.redo());
    CHECK(trackIdsOf(store) == std::vector<int>{2, 1});
    CHECK_FALSE(trackAt(store, 1).mute);
    CHECK(trackAt(store, 0).solo);
    // Removing a muted track and undoing brings it back with the flags it had.
    REQUIRE(store.setTrackMute(2, true));
    REQUIRE(store.removeTrack(2));
    REQUIRE(store.undo());
    CHECK(trackIdsOf(store) == std::vector<int>{2, 1});
    CHECK(trackAt(store, 0).mute);
    CHECK(trackAt(store, 0).solo);
}

TEST_CASE("setChordsMute / setChordsSolo: live, kept by undo and redo, no history, no revision") {
    DocumentStore store = threeChordsAndBass();
    const std::uint64_t r = store.revision();
    const Document before = store.doc();
    CHECK(store.setChordsMute(true));
    CHECK(store.setChordsSolo(true));
    CHECK_FALSE(store.setChordsMute(true));  // no change
    CHECK_FALSE(store.setChordsSolo(true));
    CHECK(store.doc().chordsMuted);
    CHECK(store.doc().chordsSolo);
    CHECK(store.doc() != before);
    CHECK(store.revision() == r);
    REQUIRE(store.undo());  // the addTrack, not a flag
    CHECK(store.doc().tracks.empty());
    CHECK(store.doc().chordsMuted);
    CHECK(store.doc().chordsSolo);
    store.setChordsMute(false);
    REQUIRE(store.redo());
    CHECK(store.doc().tracks.size() == 1);
    CHECK_FALSE(store.doc().chordsMuted);
    CHECK(store.doc().chordsSolo);
}

// --- note selection (D5, live state) --------------------------------------------------------

TEST_CASE("insertNotes selects exactly the ids it inserted, in note order, replacing any selection") {
    DocumentStore store = threeChordsAndBass();
    REQUIRE(store.addTrack(TrackRole::Lead) == 2);
    CHECK(store.noteSelection().trackId == kNoTrackId);
    CHECK(store.noteSelection().ids.empty());
    REQUIRE(store.insertNotes(1, {ns(40, 960, 30), ns(36, 0, 30), ns(38, 480, 30)}) == std::vector<int>{1, 2, 3});
    CHECK(store.noteSelection().trackId == 1);
    CHECK(store.noteSelection().ids == std::vector<int>{2, 3, 1});
    REQUIRE(store.insertNotes(1, {ns(37, 240, 30)}) == std::vector<int>{4});
    CHECK(store.noteSelection().ids == std::vector<int>{4});
    REQUIRE(store.insertNotes(2, {ns(72, 0, 30)}) == std::vector<int>{5});
    CHECK(store.noteSelection().trackId == 2);  // one track at a time
    CHECK(store.noteSelection().ids == std::vector<int>{5});
    const DocumentStore reference = store;
    CHECK(store.insertNotes(2, {ns(72, 99999, 30)}).empty());  // a refusal leaves the selection
    CHECK(store.noteSelection().trackId == 2);
    CHECK(store.noteSelection().ids == std::vector<int>{5});
}

TEST_CASE("setNoteSelection: unknown ids dropped, repeats collapsed, note order; empty or kNoTrackId clears") {
    DocumentStore store = threeChordsAndBass();
    REQUIRE(store.addTrack(TrackRole::Lead) == 2);
    REQUIRE(store.insertNotes(1, {ns(40, 960, 30), ns(36, 0, 30), ns(38, 480, 30)}) == std::vector<int>{1, 2, 3});
    REQUIRE(store.insertNotes(2, {ns(72, 0, 30)}) == std::vector<int>{4});
    const std::uint64_t r = store.revision();
    store.setNoteSelection(1, {1, 99, 3, 1, 4, 2, 3});  // 4 is in another track
    CHECK(store.noteSelection().trackId == 1);
    CHECK(store.noteSelection().ids == std::vector<int>{2, 3, 1});
    store.setNoteSelection(5, {4});  // an unknown track: no-op
    CHECK(store.noteSelection().trackId == 1);
    CHECK(store.noteSelection().ids == std::vector<int>{2, 3, 1});
    store.setNoteSelection(2, {4});
    CHECK(store.noteSelection().trackId == 2);
    CHECK(store.noteSelection().ids == std::vector<int>{4});
    store.setNoteSelection(1, {99});  // nothing left: cleared
    CHECK(store.noteSelection().trackId == kNoTrackId);
    CHECK(store.noteSelection().ids.empty());
    store.setNoteSelection(1, {1});
    store.setNoteSelection(1, {});
    CHECK(store.noteSelection().trackId == kNoTrackId);
    store.setNoteSelection(1, {1});
    store.setNoteSelection(kNoTrackId, {1});
    CHECK(store.noteSelection().trackId == kNoTrackId);
    CHECK(store.noteSelection().ids.empty());
    CHECK(store.revision() == r);  // never history, never the revision
    REQUIRE(store.undo());
    CHECK(store.doc().tracks.at(1).notes.empty());
}

TEST_CASE("note selection: stale ids dropped after removeNotes, cleared after removeTrack, re-sorted after a move") {
    DocumentStore store = threeChordsAndBass();
    REQUIRE(store.addTrack(TrackRole::Lead) == 2);
    REQUIRE(store.insertNotes(1, {ns(36, 0, 30), ns(38, 480, 30), ns(40, 960, 30)}) == std::vector<int>{1, 2, 3});
    REQUIRE(store.removeNotes(1, {2}));
    CHECK(store.noteSelection().trackId == 1);
    CHECK(store.noteSelection().ids == std::vector<int>{1, 3});
    NoteEdit move = edit(1);
    move.startTick = 2000;  // now after note 3
    REQUIRE(store.updateNotes(1, {move}));
    CHECK(store.noteSelection().ids == std::vector<int>{3, 1});
    REQUIRE(store.removeNotes(1, {1, 3}));
    CHECK(store.noteSelection().trackId == kNoTrackId);  // nothing left
    CHECK(store.noteSelection().ids.empty());
    REQUIRE(store.undo());  // the notes come back, the selection does not
    CHECK(store.noteSelection().trackId == kNoTrackId);
    store.setNoteSelection(1, {1, 3});
    REQUIRE(store.removeTrack(1));
    CHECK(store.noteSelection().trackId == kNoTrackId);
    CHECK(store.noteSelection().ids.empty());
}

TEST_CASE("note selection: an undone insert drops its ids; replace clears it; changes leave the revision alone") {
    DocumentStore store = threeChordsAndBass();
    REQUIRE(store.insertNotes(1, {ns(36, 0, 30)}) == std::vector<int>{1});
    REQUIRE(store.insertNotes(1, {ns(38, 480, 30), ns(40, 960, 30)}) == std::vector<int>{2, 3});
    store.setNoteSelection(1, {1, 2, 3});
    const std::uint64_t r = store.revision();
    REQUIRE(store.undo());
    CHECK(store.revision() == r + 1);
    CHECK(store.noteSelection().trackId == 1);
    CHECK(store.noteSelection().ids == std::vector<int>{1});
    REQUIRE(store.redo());  // the ids exist again, but the selection is not restored
    CHECK(store.noteSelection().ids == std::vector<int>{1});
    REQUIRE(store.undo());
    REQUIRE(store.undo());
    CHECK(store.noteSelection().trackId == kNoTrackId);
    CHECK(store.noteSelection().ids.empty());
    REQUIRE(store.redo());
    store.setNoteSelection(1, {1});
    const Document doc = store.doc();
    store.replace(doc, store.nextId());
    CHECK(store.noteSelection().trackId == kNoTrackId);
    CHECK(store.noteSelection().ids.empty());
}

// --- chord timing edits carry their notes (Task 2a, D3 ripple, D4, D10) -----------------------------

namespace {

/// The derived starts of track `trackIndex`'s notes, in its note order.
std::vector<std::int64_t> startsOf(const DocumentStore& store, std::size_t trackIndex = 0) {
    std::vector<std::int64_t> starts;
    for (const TrackNote& n : notesOf(store, trackIndex)) {
        starts.push_back(noteStart(store.doc(), n));
    }
    return starts;
}

/// `notes` sorted by id: what the notes are, whatever order a chord edit left them in.
std::vector<TrackNote> byId(std::vector<TrackNote> notes) {
    std::sort(notes.begin(), notes.end(), [](const TrackNote& a, const TrackNote& b) { return a.id < b.id; });
    return notes;
}

/// The note of track `trackIndex` with `id`.
const TrackNote& noteWithId(const DocumentStore& store, int id, std::size_t trackIndex = 0) {
    const std::vector<TrackNote>& notes = notesOf(store, trackIndex);
    const auto it = std::find_if(notes.begin(), notes.end(), [id](const TrackNote& n) { return n.id == id; });
    REQUIRE(it != notes.end());
    return *it;
}

/// The notes of `oneNotePerChord`: one in each chord, at offsets 100, 200, 300.
const std::vector<TrackNote> kOneNotePerChord{TrackNote{1, 36, 1, 100, 480, 100}, TrackNote{2, 38, 2, 200, 480, 100},
                                              TrackNote{3, 40, 3, 300, 480, 100}};

/// `threeChordsAndBass` with `kOneNotePerChord` in the Bass track.
DocumentStore oneNotePerChord() {
    DocumentStore store = threeChordsAndBass();
    REQUIRE(store.insertNotes(1, {ns(36, 100, 480), ns(38, kBar + 200, 480), ns(40, 2 * kBar + 300, 480)}) ==
            std::vector<int>{1, 2, 3});
    REQUIRE(notesOf(store) == kOneNotePerChord);
    return store;
}

using Starts = std::vector<std::int64_t>;

/// `threeChordsAndBass` with kMaxTrackPolyphony notes (ids 1..cap) held from tick 0 to kBar + 3000, into the
/// second chord, and one note (id cap + 1) at the start of the third chord, after they end: at the cap.
DocumentStore heldIntoTheSecondChord() {
    DocumentStore store = threeChordsAndBass();
    std::vector<NoteSpec> held;
    for (int i = 0; i < kMaxTrackPolyphony; ++i) {
        held.push_back(ns(40 + i, 0, kBar + 3000));
    }
    REQUIRE(store.insertNotes(1, held).size() == static_cast<std::size_t>(kMaxTrackPolyphony));
    REQUIRE(store.insertNotes(1, {ns(70, 2 * kBar, 480)}).size() == 1);
    REQUIRE(trackPolyphony(store.doc()) == kMaxTrackPolyphony);
    return store;
}

}  // namespace

TEST_CASE("insertChords / appendChord: notes keep their chord and offset; the later chords' notes move with them") {
    DocumentStore store = oneNotePerChord();
    std::string refusal = "stale";
    checkOneStep(store, [&](DocumentStore& s) { CHECK(s.insertChords(1, {ChordSpec{kC, 2.0}}, &refusal) == std::vector<int>{4}); });
    CHECK(refusal.empty());
    CHECK(notesOf(store) == kOneNotePerChord);  // anchor, offset, length, pitch, velocity, order
    CHECK(startsOf(store) == Starts{100, kBar + 1920 + 200, 2 * kBar + 1920 + 300});
    refusal = "stale";
    checkOneStep(store, [&](DocumentStore& s) { CHECK(s.appendChord(kF, 4.0, 100, std::nullopt, &refusal) == 5); });
    CHECK(refusal.empty());
    CHECK(notesOf(store) == kOneNotePerChord);
    CHECK(startsOf(store) == Starts{100, kBar + 1920 + 200, 2 * kBar + 1920 + 300});
}

TEST_CASE("moveChord: the moved chord's notes go with it, the others shift; every track is re-sorted by derived start") {
    DocumentStore store = oneNotePerChord();
    REQUIRE(store.addTrack(TrackRole::Lead) == 2);
    REQUIRE(store.insertNotes(2, {ns(72, 50, 480), ns(74, 2 * kBar + 10, 480)}) == std::vector<int>{4, 5});
    const std::vector<TrackNote> lead = notesOf(store, 1);
    std::string refusal = "stale";
    checkOneStep(store, [&](DocumentStore& s) { CHECK(s.moveChord(1, 2, &refusal)); });  // later: F, G7, C
    CHECK(refusal.empty());
    CHECK(noteIdsOf(store) == std::vector<int>{2, 3, 1});
    CHECK(startsOf(store) == Starts{200, kBar + 300, 2 * kBar + 100});
    CHECK(noteIdsOf(store, 1) == std::vector<int>{5, 4});
    CHECK(startsOf(store, 1) == Starts{kBar + 10, 2 * kBar + 50});
    CHECK(byId(notesOf(store)) == kOneNotePerChord);  // the notes themselves are unchanged
    CHECK(byId(notesOf(store, 1)) == lead);
    refusal = "stale";
    checkOneStep(store, [&](DocumentStore& s) { CHECK(s.moveChord(3, 0, &refusal)); });  // earlier: G7, F, C
    CHECK(refusal.empty());
    CHECK(noteIdsOf(store) == std::vector<int>{3, 2, 1});
    CHECK(startsOf(store) == Starts{300, kBar + 200, 2 * kBar + 100});
    CHECK(noteIdsOf(store, 1) == std::vector<int>{5, 4});
    CHECK(startsOf(store, 1) == Starts{10, 2 * kBar + 50});
    CHECK(byId(notesOf(store)) == kOneNotePerChord);
    std::string error;
    CHECK(validateTracks(store.doc(), store.nextTrackId(), store.nextNoteId(), error));
}

TEST_CASE("setLength / setLengths: resizing an earlier chord shifts the later chords' notes; the notes are unchanged") {
    DocumentStore store = oneNotePerChord();
    std::string refusal = "stale";
    checkOneStep(store, [&](DocumentStore& s) { CHECK(s.setLength(1, 2.0, &refusal)); });
    CHECK(refusal.empty());
    CHECK(notesOf(store) == kOneNotePerChord);
    CHECK(startsOf(store) == Starts{100, 1920 + 200, 1920 + kBar + 300});
    refusal = "stale";
    checkOneStep(store, [&](DocumentStore& s) { CHECK(s.setLengths({1, 2, 99}, 1.0, &refusal)); });
    CHECK(refusal.empty());
    CHECK(notesOf(store) == kOneNotePerChord);
    CHECK(startsOf(store) == Starts{100, 960 + 200, 1920 + 300});
}

TEST_CASE("setLength: a resize that reorders derived starts re-sorts the track, inactive notes included") {
    DocumentStore store = threeChordsAndBass();
    REQUIRE(store.insertNotes(1, {ns(36, 3000, 480), ns(38, kBar, 480)}) == std::vector<int>{1, 2});
    REQUIRE(noteIdsOf(store) == std::vector<int>{1, 2});
    std::string refusal;
    checkOneStep(store, [&](DocumentStore& s) { CHECK(s.setLength(1, 2.0, &refusal)); });
    CHECK(noteIdsOf(store) == std::vector<int>{2, 1});  // 2 at 1920, 1 (inactive) still at 3000
    CHECK(startsOf(store) == Starts{1920, 3000});
    CHECK_FALSE(noteActive(store.doc(), noteWithId(store, 1)));
    CHECK(store.setLength(1, 4.0, &refusal));
    CHECK(noteIdsOf(store) == std::vector<int>{1, 2});
    CHECK(noteActive(store.doc(), noteWithId(store, 1)));
}

TEST_CASE("shortening a chord makes its notes past the new end inactive, kept and uncounted; lengthening revalidates them") {
    // The early notes sound 3000..5000 from the first chord; the late ones sit 1200 ticks into the second chord.
    constexpr int cap = kMaxTrackPolyphony;
    DocumentStore store = threeChordsAndBass();
    std::vector<NoteSpec> early;
    std::vector<NoteSpec> late;
    for (int i = 0; i < cap; ++i) {
        early.push_back(ns(40 + i, 3000, 2000));
        late.push_back(ns(60 + i, kBar + 1200, 480));
    }
    REQUIRE(store.insertNotes(1, early).size() == static_cast<std::size_t>(cap));
    REQUIRE(store.insertNotes(1, late).size() == static_cast<std::size_t>(cap));
    REQUIRE(trackPolyphony(store.doc()) == cap);
    const auto checkEarly = [&](bool active) {
        for (const TrackNote& n : notesOf(store)) {
            const bool isEarly = n.id <= cap;
            CHECK(noteActive(store.doc(), n) == (isEarly ? active : true));
            if (isEarly) {
                CHECK(n.anchorEventId == 1);
                CHECK(n.offsetTick == 3000);
                CHECK(n.lengthTicks == 2000);
            }
        }
    };
    // Two beats: the second chord's notes come to 3120, under the early ones' 3000..5000, which are inactive now
    // and so do not count: the shorten never fails on their account.
    std::string refusal = "stale";
    checkOneStep(store, [&](DocumentStore& s) { CHECK(s.setLength(1, 2.0, &refusal)); });
    CHECK(refusal.empty());
    REQUIRE(notesOf(store).size() == static_cast<std::size_t>(2 * cap));
    checkEarly(false);
    CHECK(trackPolyphony(store.doc()) == cap);
    // Exactly to their offset (3000 ticks): ownership is half-open, so they stay inactive.
    CHECK(store.setLength(1, 3000.0 / kFeelTicksPerBeat, &refusal));
    CHECK(refusal.empty());
    checkEarly(false);
    // 3120 ticks would bring them back over the second chord's notes (now at 4320): refused whole.
    const DocumentStore reference = store;
    CHECK_FALSE(store.setLength(1, 3.25, &refusal));
    CHECK(refusal == kPolyphonyMessage);
    checkRefused(store, reference);
    CHECK_FALSE(store.setLengths({1, 2}, 3.25, &refusal));
    CHECK(refusal == kPolyphonyMessage);
    checkRefused(store, reference);
    // Four beats: they sound again, clear of the second chord's notes.
    CHECK(store.setLength(1, 4.0, &refusal));
    CHECK(refusal.empty());
    checkEarly(true);
    CHECK(trackPolyphony(store.doc()) == cap);
}

TEST_CASE("a lengthen that would reactivate a note past the onset cap is refused with no history") {
    constexpr int cap = kMaxTrackOnsetsPerBeat;
    constexpr std::int32_t step = kFeelTicksPerBeat / cap;
    static_assert(step >= kMinTrackNoteTicks, "the run below must not overlap, so polyphony stays out of it");
    DocumentStore store = threeChordsAndBass();
    REQUIRE(store.insertNotes(1, {ns(36, kBar - 10, kMinTrackNoteTicks)}) == std::vector<int>{1});  // chord 1
    std::string refusal;
    REQUIRE(store.setLength(1, 2.0, &refusal));  // note 1 goes inactive
    std::vector<NoteSpec> run;
    for (int j = 0; j < cap; ++j) {
        run.push_back(ns(60, 2 * kFeelTicksPerBeat + j * step, kMinTrackNoteTicks));  // the second chord, from its start
    }
    REQUIRE(store.insertNotes(1, run).size() == static_cast<std::size_t>(cap));
    REQUIRE(densestOnsetWindow(store.doc()) == cap);
    const DocumentStore reference = store;
    CHECK_FALSE(store.setLength(1, 4.0, &refusal));  // note 1 at kBar - 10 and the run from kBar share a beat
    CHECK(refusal == kOnsetMessage);
    checkRefused(store, reference);
}

TEST_CASE("removeChord / moveChord: refused when the later chord's notes move up into the beat of an earlier chord's tail") {
    // Half the cap of short notes ending where chord 1 ends, one more than half from chord 3's start: removing chord
    // 2, or moving chord 3 into its place, packs them into one beat. Polyphony stays 1 (back to back).
    constexpr int cap = kMaxTrackOnsetsPerBeat;
    constexpr std::int32_t step = kMinTrackNoteTicks;
    static_assert((cap + 1) * step < kFeelTicksPerBeat, "the packed run must fit in one beat");
    DocumentStore store = threeChordsAndBass();
    std::vector<NoteSpec> notes;
    for (int j = 0; j < cap / 2; ++j) {
        notes.push_back(ns(60, kBar - (cap / 2 - j) * step, step));  // chord 1's tail
    }
    for (int j = 0; j < cap / 2 + 1; ++j) {
        notes.push_back(ns(62, 2 * kBar + j * step, step));  // chord 3's head
    }
    REQUIRE(store.insertNotes(1, notes).size() == notes.size());
    REQUIRE(densestOnsetWindow(store.doc()) == cap / 2 + 1);
    store.setSelectedEvents({2, 3}, 2);
    const DocumentStore reference = store;
    std::string refusal;
    CHECK_FALSE(store.removeChord(2, &refusal));
    CHECK(refusal == kOnsetMessage);
    checkRefused(store, reference);
    CHECK_FALSE(store.removeChords({2}, &refusal));
    CHECK(refusal == kOnsetMessage);
    checkRefused(store, reference);
    CHECK_FALSE(store.moveChord(3, 1, &refusal));
    CHECK(refusal == kOnsetMessage);
    checkRefused(store, reference);
    // One head note fewer fits exactly at the cap.
    REQUIRE(store.removeNotes(1, {cap + 1}));
    CHECK(store.removeChord(2, &refusal));
    CHECK(refusal.empty());
    CHECK(densestOnsetWindow(store.doc()) == cap);
}

TEST_CASE("removeChord / removeChords: the removed chord's notes go, inactive ones included; an earlier long note stays") {
    // Track 1: note 1 in chord 1, sounding on into chord 2; notes 2 and 3 in chord 2 (3 inactive once chord 2 is two
    // beats); note 4 in chord 3. Track 2: note 5 in chord 2.
    DocumentStore store = threeChordsAndBass();
    REQUIRE(store.addTrack(TrackRole::Lead) == 2);
    REQUIRE(store.insertNotes(1, {ns(36, 0, 6000), ns(38, kBar + 100, 480), ns(40, kBar + 3000, 480),
                                  ns(43, 2 * kBar, 480)}) == std::vector<int>{1, 2, 3, 4});
    REQUIRE(store.insertNotes(2, {ns(72, kBar + 10, 480)}) == std::vector<int>{5});
    std::string refusal = "stale";
    REQUIRE(store.setLength(2, 2.0, &refusal));
    REQUIRE_FALSE(noteActive(store.doc(), noteWithId(store, 3)));
    store.setNoteSelection(1, {1, 2, 3, 4});
    SUBCASE("removeChord") {
        checkOneStep(store, [&](DocumentStore& s) { CHECK(s.removeChord(2, &refusal)); });
    }
    SUBCASE("removeChords, an unknown id and a repeat skipped") {
        checkOneStep(store, [&](DocumentStore& s) { CHECK(s.removeChords({99, 2, 2}, &refusal)); });
    }
    CHECK(refusal.empty());
    CHECK(notesOf(store) == std::vector<TrackNote>{TrackNote{1, 36, 1, 0, 6000, 100}, TrackNote{4, 43, 3, 0, 480, 100}});
    CHECK(startsOf(store) == Starts{0, kBar});  // note 4 moved up with chord 3
    CHECK(notesOf(store, 1).empty());
    CHECK(store.noteSelection() == NoteSelection{1, {1, 4}});  // the removed ids dropped
    CHECK(store.undo());
    CHECK(noteIdsOf(store) == std::vector<int>{1, 2, 4, 3});  // undo restores everything (3, inactive, at 6840)
    CHECK(noteIdsOf(store, 1) == std::vector<int>{5});
}

TEST_CASE("removeChords: several chords take all their notes; removing every chord leaves the tracks empty") {
    DocumentStore store = oneNotePerChord();
    std::string refusal;
    CHECK(store.removeChords({1, 3}, &refusal));
    CHECK(notesOf(store) == std::vector<TrackNote>{TrackNote{2, 38, 2, 200, 480, 100}});
    CHECK(startsOf(store) == Starts{200});
    CHECK(store.removeChord(2, &refusal));
    CHECK(store.doc().events.empty());
    REQUIRE(store.doc().tracks.size() == 1);
    CHECK(notesOf(store).empty());
}

TEST_CASE("removeChord / removeChords / moveChord / setLength: refused when notes moved up or along would break a cap") {
    DocumentStore store = heldIntoTheSecondChord();
    const DocumentStore reference = store;
    std::string refusal;
    CHECK_FALSE(store.removeChord(2, &refusal));  // chord 3's note would start at kBar, under the held ones
    CHECK(refusal == kPolyphonyMessage);
    checkRefused(store, reference);
    CHECK_FALSE(store.removeChords({2}, &refusal));
    CHECK(refusal == kPolyphonyMessage);
    checkRefused(store, reference);
    CHECK_FALSE(store.moveChord(3, 1, &refusal));  // the same, by a move
    CHECK(refusal == kPolyphonyMessage);
    checkRefused(store, reference);
    CHECK_FALSE(store.setLength(2, 1.0, &refusal));  // a shorten that moves chord 3's note to 4800
    CHECK(refusal == kPolyphonyMessage);
    checkRefused(store, reference);
    CHECK(store.setLength(2, 3000.0 / kFeelTicksPerBeat + 0.25, &refusal));  // chord 3 at 7080, after 6840
    CHECK(refusal.empty());
}

TEST_CASE("fractional boundaries: a move that narrows a chord by one rounded tick keeps its last-offset note, inactive") {
    DocumentStore store;
    const int a = store.appendChord(kC, 0.2506);
    store.appendChord(kF, 0.2506);
    store.appendChord(kG, 0.2506);
    REQUIRE(chordBoundaryTicks(store.doc()) == Ticks{0, 241, 481, 722});
    REQUIRE(store.addTrack(TrackRole::Bass) == 1);
    REQUIRE(store.insertNotes(1, {ns(36, 240, 30)}) == std::vector<int>{1});  // the first chord's last tick
    const std::vector<TrackNote> note{TrackNote{1, 36, a, 240, 30, 100}};
    REQUIRE(notesOf(store) == note);
    REQUIRE(noteActive(store.doc(), notesOf(store).at(0)));
    std::string refusal = "stale";
    checkOneStep(store, [&](DocumentStore& s) { CHECK(s.moveChord(a, 1, &refusal)); });  // 241..481: 240 ticks wide
    CHECK(refusal.empty());
    CHECK(notesOf(store) == note);  // kept, not deleted
    CHECK_FALSE(noteActive(store.doc(), notesOf(store).at(0)));
    CHECK(startsOf(store) == Starts{481});
    CHECK(store.moveChord(a, 0, &refusal));
    CHECK(notesOf(store) == note);
    CHECK(noteActive(store.doc(), notesOf(store).at(0)));
    CHECK(startsOf(store) == Starts{240});
}

TEST_CASE("insertChords: refused when a chord pushed along gains a rounded tick and reactivates a note over the cap") {
    // X, A, B of 0.2506 beats and C of four: boundaries 0, 241, 481, 722, 4562. Note 1 sits on B's last tick
    // (offset 240 of 241); removing X narrows B to 240 ticks (note 1 inactive, at 481), where C's notes then start.
    // Inserting a 0.2506-beat chord at 0 again widens B and would make note 1 sound at 721, over C's notes at 722.
    DocumentStore store;
    const int x = store.appendChord(kC, 0.2506);
    store.appendChord(kF, 0.2506);
    const int b = store.appendChord(kG, 0.2506);
    store.appendChord(kC, 4.0);
    REQUIRE(chordBoundaryTicks(store.doc()) == Ticks{0, 241, 481, 722, 4562});
    REQUIRE(store.addTrack(TrackRole::Bass) == 1);
    REQUIRE(store.insertNotes(1, {ns(36, 721, 480)}) == std::vector<int>{1});
    REQUIRE(notesOf(store).at(0).anchorEventId == b);
    std::string refusal;
    REQUIRE(store.removeChord(x, &refusal));
    REQUIRE_FALSE(noteActive(store.doc(), notesOf(store).at(0)));
    std::vector<NoteSpec> onC;
    for (int i = 0; i < kMaxTrackPolyphony; ++i) {
        onC.push_back(ns(60 + i, 481, 480));
    }
    REQUIRE(store.insertNotes(1, onC).size() == static_cast<std::size_t>(kMaxTrackPolyphony));
    const DocumentStore reference = store;
    const int nextId = store.nextId();
    CHECK(store.insertChords(0, {ChordSpec{kC, 0.2506}}, &refusal).empty());
    CHECK(refusal == kPolyphonyMessage);
    checkRefused(store, reference);
    CHECK(store.nextId() == nextId);  // no event id consumed
    CHECK(store.selectedEventIds() == reference.selectedEventIds());
    CHECK(store.insertChords(0, {ChordSpec{kC, 0.25}}, &refusal) == std::vector<int>{nextId});  // 240 ticks: no change
    CHECK(refusal.empty());
}

TEST_CASE("chord timing edits: undo and redo restore the stored documents exactly; the revision moves once each") {
    DocumentStore store = oneNotePerChord();
    std::vector<Document> docs{store.doc()};
    std::uint64_t r = store.revision();
    std::string refusal;
    REQUIRE(store.moveChord(1, 2, &refusal));
    docs.push_back(store.doc());
    CHECK(store.revision() == ++r);
    REQUIRE(store.setLength(2, 0.25, &refusal));  // note 2 (offset 200 of 240) stays active; note 1 moves up
    docs.push_back(store.doc());
    CHECK(store.revision() == ++r);
    REQUIRE(store.setLength(3, 0.25, &refusal));  // note 3 (offset 300) goes inactive
    docs.push_back(store.doc());
    CHECK(store.revision() == ++r);
    REQUIRE(store.removeChord(2, &refusal));  // note 2 goes
    docs.push_back(store.doc());
    CHECK(store.revision() == ++r);
    REQUIRE(store.insertChords(0, {ChordSpec{kF, 1.0}}, &refusal).size() == 1);
    docs.push_back(store.doc());
    CHECK(store.revision() == ++r);
    for (std::size_t i = docs.size() - 1; i-- > 0;) {
        REQUIRE(store.undo());
        CHECK(store.doc() == docs[i]);  // notes, anchors, offsets and their order exactly
        CHECK(store.revision() == ++r);
    }
    for (std::size_t i = 1; i < docs.size(); ++i) {
        REQUIRE(store.redo());
        CHECK(store.doc() == docs[i]);
        CHECK(store.revision() == ++r);
    }
}

TEST_CASE("chord timing edits: refusals and no-ops leave the revision and the history alone") {
    DocumentStore store = heldIntoTheSecondChord();
    const DocumentStore reference = store;
    std::string refusal = "stale";
    CHECK_FALSE(store.moveChord(99, 0, &refusal));
    CHECK(refusal.empty());
    CHECK_FALSE(store.moveChord(1, 0, &refusal));
    CHECK(refusal.empty());
    CHECK_FALSE(store.setLength(1, 4.0, &refusal));
    CHECK(refusal.empty());
    CHECK_FALSE(store.removeChord(99, &refusal));
    CHECK(refusal.empty());
    CHECK_FALSE(store.removeChords({99}, &refusal));
    CHECK(refusal.empty());
    CHECK_FALSE(store.setLengths({1, 2, 3}, 4.0, &refusal));
    CHECK(refusal.empty());
    checkRefused(store, reference);
    CHECK_FALSE(store.removeChord(2, &refusal));
    CHECK_FALSE(refusal.empty());
    checkRefused(store, reference);
}

TEST_CASE("appendProgression keeps every note; replaceProgression removes every note and keeps the tracks and their flags") {
    DocumentStore store = oneNotePerChord();
    REQUIRE(store.addTrack(TrackRole::Lead) == 2);
    REQUIRE(store.insertNotes(2, {ns(72, 0, 480)}) == std::vector<int>{4});
    REQUIRE(store.setTrackMute(2, true));
    REQUIRE(store.setTrackSolo(1, true));
    const std::vector<std::pair<Chord, double>> steps{{kC, 4.0}, {kG, 2.0}};
    const std::vector<Track> tracks = store.doc().tracks;
    std::string refusal = "stale";
    checkOneStep(store, [&](DocumentStore& s) { CHECK(s.appendProgression(steps, "Verse", &refusal)); });
    CHECK(refusal.empty());
    CHECK(store.doc().tracks == tracks);  // notes, names, roles, flags
    CHECK(startsOf(store) == Starts{100, kBar + 200, 2 * kBar + 300});
    CHECK(store.noteSelection() == NoteSelection{2, {4}});
    refusal = "stale";
    checkOneStep(store, [&](DocumentStore& s) { CHECK(s.replaceProgression(steps, "Chorus", &refusal)); });
    CHECK(refusal.empty());
    REQUIRE(trackIdsOf(store) == std::vector<int>{1, 2});
    CHECK(notesOf(store, 0).empty());
    CHECK(notesOf(store, 1).empty());
    CHECK(trackAt(store, 0).name == "Bass");
    CHECK(trackAt(store, 1).name == "Lead");
    CHECK(trackAt(store, 0).solo);
    CHECK(trackAt(store, 1).mute);
    CHECK(store.noteSelection() == NoteSelection{});  // its notes are gone
    REQUIRE(store.undo());
    CHECK(store.doc().tracks == tracks);  // undo brings the notes back
}

// --- insertChords with notes (tracks design D21) -----------------------------------------

namespace {

LaneRef laneNamed(std::string name, TrackRole role, std::optional<int> trackId = std::nullopt) {
    return LaneRef{trackId, std::move(name), role};
}

ChordNoteSpec chordNote(LaneRef lane, std::int64_t offset, std::int64_t length, int pitch, int velocity = 100) {
    return ChordNoteSpec{std::move(lane), offset, length, pitch, velocity};
}

ChordSpec chordWith(Chord c, double beats, std::vector<ChordNoteSpec> notes) {
    ChordSpec spec{c, beats};
    spec.notes = std::move(notes);
    return spec;
}

/// `oneNotePerChord` (Bass id 1, notes 1..3) plus a Lead track (id 2) with no notes.
DocumentStore bassAndLead() {
    DocumentStore store = oneNotePerChord();
    REQUIRE(store.addTrack(TrackRole::Lead) == 2);
    return store;
}

}  // namespace

TEST_CASE("insertChords with notes: a lane naming an existing track id goes to that track, whatever its name and role") {
    DocumentStore store = bassAndLead();
    const int tracksBefore = store.nextTrackId();
    std::string refusal = "stale";
    // Inserted at index 1 (before F): the new chord has id 4 and starts at kBar.
    checkOneStep(store, [&](DocumentStore& s) {
        CHECK(s.insertChords(1, {chordWith(kC, 4.0, {chordNote(laneNamed("anything", TrackRole::Bass, 2), 480, 960, 72, 90)})},
                             &refusal) == std::vector<int>{4});
    });
    CHECK(refusal.empty());
    CHECK(store.nextTrackId() == tracksBefore);  // no track was created
    CHECK(trackIdsOf(store) == std::vector<int>{1, 2});
    CHECK(notesOf(store, 0) == kOneNotePerChord);  // the Bass track is untouched
    REQUIRE(notesOf(store, 1).size() == 1);
    CHECK(notesOf(store, 1).at(0) == TrackNote{4, 72, 4, 480, 960, 90});  // id from the counter, anchor the new chord
    CHECK(noteStart(store.doc(), notesOf(store, 1).at(0)) == kBar + 480);
    CHECK(store.nextNoteId() == 5);
}

TEST_CASE("insertChords with notes: no track id (or none in this document) resolves by name and role, uniquely") {
    DocumentStore store = bassAndLead();
    std::string refusal;
    SUBCASE("name and role of an existing track") {
        CHECK(store.insertChords(0, {chordWith(kC, 4.0, {chordNote(laneNamed("Lead", TrackRole::Lead), 0, 480, 72)})},
                                 &refusal) == std::vector<int>{4});
        CHECK(trackIdsOf(store) == std::vector<int>{1, 2});
        CHECK(noteIdsOf(store, 1) == std::vector<int>{4});
    }
    SUBCASE("a track id this document does not have falls back to the name") {
        CHECK(store.insertChords(0, {chordWith(kC, 4.0, {chordNote(laneNamed("Bass", TrackRole::Bass, 77), 0, 480, 40)})},
                                 &refusal) == std::vector<int>{4});
        CHECK(trackIdsOf(store) == std::vector<int>{1, 2});
        CHECK(notesOf(store, 0).size() == 4);
    }
    SUBCASE("a name longer than the track limit is cut before it is compared") {
        REQUIRE(store.patchTrack(2, std::string(kMaxTrackNameBytes, 'x'), std::nullopt));
        CHECK(store.insertChords(0, {chordWith(kC, 4.0, {chordNote(laneNamed(std::string(kMaxTrackNameBytes + 10, 'x'),
                                                                           TrackRole::Lead), 0, 480, 72)})},
                                 &refusal) == std::vector<int>{4});
        CHECK(trackIdsOf(store) == std::vector<int>{1, 2});
        CHECK(noteIdsOf(store, 1) == std::vector<int>{4});
    }
    CHECK(refusal.empty());
}

TEST_CASE("insertChords with notes: an absent lane becomes one new track at the end, shared by every note naming it") {
    DocumentStore store = bassAndLead();
    std::string refusal = "stale";
    const std::vector<ChordSpec> chords{
        chordWith(kC, 4.0, {chordNote(laneNamed("Pad", TrackRole::Other), 0, 480, 60), chordNote(laneNamed("Pad", TrackRole::Other), 960, 480, 64)}),
        chordWith(kF, 4.0, {chordNote(laneNamed("Pad", TrackRole::Other), 0, 480, 65), chordNote(laneNamed("Bass", TrackRole::Lead), 0, 480, 77)})};
    checkOneStep(store, [&](DocumentStore& s) { CHECK(s.insertChords(3, chords, &refusal) == std::vector<int>{4, 5}); });
    CHECK(refusal.empty());
    // "Bass" with role Lead is not the Bass track: it is absent too, a second new track.
    REQUIRE(trackIdsOf(store) == std::vector<int>{1, 2, 3, 4});
    CHECK(trackAt(store, 2).name == "Pad");
    CHECK(trackAt(store, 2).role == TrackRole::Other);
    CHECK_FALSE(trackAt(store, 2).mute);
    CHECK_FALSE(trackAt(store, 2).solo);
    CHECK(trackAt(store, 3).name == "Bass");
    CHECK(trackAt(store, 3).role == TrackRole::Lead);
    CHECK(notesOf(store, 2) == std::vector<TrackNote>{TrackNote{4, 60, 4, 0, 480, 100}, TrackNote{5, 64, 4, 960, 480, 100},
                                                      TrackNote{6, 65, 5, 0, 480, 100}});
    CHECK(notesOf(store, 3) == std::vector<TrackNote>{TrackNote{7, 77, 5, 0, 480, 100}});
    CHECK(store.nextTrackId() == 5);
    CHECK(store.nextNoteId() == 8);
}

TEST_CASE("insertChords with notes: notes sort into their track beside the existing ones, anchors on the new chords") {
    DocumentStore store = oneNotePerChord();  // notes at 100, kBar + 200, 2 kBar + 300
    // Two chords at index 1: ids 4 and 5, starting at kBar and kBar + 1920; the old second and third chords move later.
    REQUIRE(store.insertChords(1, {chordWith(kC, 2.0, {chordNote(laneNamed("Bass", TrackRole::Bass, 1), 50, 480, 30)}),
                                   chordWith(kF, 2.0, {chordNote(laneNamed("Bass", TrackRole::Bass, 1), 10, 480, 31)})}) ==
            std::vector<int>{4, 5});
    CHECK(noteIdsOf(store) == std::vector<int>{1, 4, 5, 2, 3});
    CHECK(startsOf(store) == Starts{100, kBar + 50, kBar + 1920 + 10, kBar + 1920 * 2 + 200, 2 * kBar + 1920 * 2 + 300});
    CHECK(noteWithId(store, 4).anchorEventId == 4);
    CHECK(noteWithId(store, 5).anchorEventId == 5);
    CHECK(noteWithId(store, 2).anchorEventId == 2);  // the existing notes keep their chords
}

TEST_CASE("insertChords with notes: notes past their chord's end are carried, inactive") {
    DocumentStore store = threeChordsAndBass();
    // A one-beat chord (960 ticks) and a note at offset 2000: inactive, kept, as the clipboard carries them.
    REQUIRE(store.insertChords(0, {chordWith(kC, 1.0, {chordNote(laneNamed("Bass", TrackRole::Bass, 1), 2000, 480, 40),
                                                       chordNote(laneNamed("Bass", TrackRole::Bass, 1), 100, 480, 41)})}) ==
            std::vector<int>{4});
    REQUIRE(notesOf(store).size() == 2);
    const TrackNote& active = noteWithId(store, 2);
    const TrackNote& inactive = noteWithId(store, 1);
    CHECK(inactive.offsetTick == 2000);
    CHECK(inactive.anchorEventId == 4);
    CHECK_FALSE(noteActive(store.doc(), inactive));
    CHECK(noteActive(store.doc(), active));
    // Lengthening the chord brings it back.
    REQUIRE(store.setLength(4, 4.0));
    CHECK(noteActive(store.doc(), noteWithId(store, 1)));
}

TEST_CASE("insertChords with notes: pitch, velocity, offset and length are clamped as the store clamps them") {
    DocumentStore store = threeChordsAndBass();
    const LaneRef bass = laneNamed("Bass", TrackRole::Bass, 1);
    REQUIRE(store.insertChords(0, {chordWith(kC, 4.0, {chordNote(bass, -50, 1, 200, 0),
                                                       chordNote(bass, 0, 1000, -3, 500),
                                                       chordNote(bass, std::numeric_limits<std::int64_t>::max(),
                                                                 std::numeric_limits<std::int64_t>::max(), 60, 100)})}) ==
            std::vector<int>{4});
    // Sorted by derived start: the two at the chord's start, then the clamped far one.
    REQUIRE(notesOf(store).size() == 3);
    const TrackNote& a = noteWithId(store, 1);
    const TrackNote& b = noteWithId(store, 2);
    const TrackNote& far = noteWithId(store, 3);
    CHECK(a.offsetTick == 0);
    CHECK(a.lengthTicks == kMinTrackNoteTicks);
    CHECK(a.pitch == 127);
    CHECK(a.velocity == 1);
    CHECK(b.pitch == 0);
    CHECK(b.velocity == 127);
    CHECK(b.lengthTicks == 1000);
    // The derived start is clamped to kMaxNoteTick - kMinTrackNoteTicks and the length to what is left (30).
    CHECK(noteStart(store.doc(), far) == kMaxNoteTick - kMinTrackNoteTicks);
    CHECK(far.lengthTicks == kMinTrackNoteTicks);
}

TEST_CASE("insertChords with notes: an ambiguous lane refuses the whole insert, naming the lane") {
    DocumentStore store = bassAndLead();
    REQUIRE(store.addTrack(TrackRole::Lead, std::string("Lead")) == 3);  // a second "Lead" Lead
    const DocumentStore reference = store;
    std::string refusal = "stale";
    CHECK(store.insertChords(0, {chordWith(kC, 4.0, {chordNote(laneNamed("Lead", TrackRole::Lead), 0, 480, 72)})},
                             &refusal).empty());
    CHECK(refusal.find("Lead") != std::string::npos);
    CHECK(refusal.find("more than one") != std::string::npos);
    checkRefused(store, reference);
    // A track id that exists settles it: no ambiguity to resolve.
    CHECK(store.insertChords(0, {chordWith(kC, 4.0, {chordNote(laneNamed("Lead", TrackRole::Lead, 3), 0, 480, 72)})},
                             &refusal) == std::vector<int>{4});
    CHECK(noteIdsOf(store, 2) == std::vector<int>{4});
}

TEST_CASE("insertChords with notes: no room for a new track refuses the whole insert") {
    DocumentStore store = bassAndLead();
    REQUIRE(store.addTrack(TrackRole::Other) == 3);
    std::string refusal = "stale";
    SUBCASE("three tracks, one new lane fits, two do not") {
        const DocumentStore reference = store;
        CHECK(store.insertChords(0, {chordWith(kC, 4.0, {chordNote(laneNamed("A", TrackRole::Other), 0, 480, 60),
                                                         chordNote(laneNamed("B", TrackRole::Other), 0, 480, 61)})},
                                 &refusal).empty());
        CHECK(refusal == "At most " + std::to_string(kMaxTracks) + " tracks");
        checkRefused(store, reference);
        CHECK(store.insertChords(0, {chordWith(kC, 4.0, {chordNote(laneNamed("A", TrackRole::Other), 0, 480, 60),
                                                         chordNote(laneNamed("A", TrackRole::Other), 960, 480, 61)})},
                                 &refusal) == std::vector<int>{4});
        CHECK(trackIdsOf(store) == std::vector<int>{1, 2, 3, 4});
    }
    SUBCASE("four tracks") {
        REQUIRE(store.addTrack(TrackRole::Other) == 4);
        const DocumentStore reference = store;
        CHECK(store.insertChords(0, {chordWith(kC, 4.0, {chordNote(laneNamed("New", TrackRole::Bass), 0, 480, 60)})},
                                 &refusal).empty());
        CHECK(refusal == "At most " + std::to_string(kMaxTracks) + " tracks");
        checkRefused(store, reference);
        // Notes that name existing lanes still go in.
        CHECK(store.insertChords(0, {chordWith(kC, 4.0, {chordNote(laneNamed("Bass", TrackRole::Bass), 0, 480, 60)})},
                                 &refusal) == std::vector<int>{4});
    }
}

TEST_CASE("insertChords with notes: a refusal by the track limits leaves no new track, no ids consumed, no history") {
    DocumentStore store = threeChordsAndBass();
    const DocumentStore reference = store;
    std::vector<ChordNoteSpec> nine;  // one past the polyphony cap, all sounding together, in a brand-new lane
    for (int i = 0; i <= kMaxTrackPolyphony; ++i) {
        nine.push_back(chordNote(laneNamed("Stack", TrackRole::Lead), 0, 480, 40 + i));
    }
    std::string refusal = "stale";
    CHECK(store.insertChords(0, {chordWith(kC, 4.0, nine)}, &refusal).empty());
    CHECK(refusal == kPolyphonyMessage);
    checkRefused(store, reference);
    CHECK(trackIdsOf(store) == std::vector<int>{1});
    CHECK(store.nextId() == reference.nextId());
    CHECK(store.selectedEventIds() == reference.selectedEventIds());
}

TEST_CASE("insertChords with notes: refused when the new notes push a track past its note limit") {
    // 14 chords of four beats (56 beats) hold 1024 notes at 20 starts per beat, under the onset cap.
    DocumentStore store;
    for (int i = 0; i < 14; ++i) {
        REQUIRE(store.appendChord(kC, 4.0) == i + 1);
    }
    REQUIRE(store.addTrack(TrackRole::Bass) == 1);
    std::vector<NoteSpec> many;
    for (int i = 0; i < kMaxNotesPerTrack; ++i) {
        many.push_back(ns(60, i * 48, 30));
    }
    REQUIRE(store.insertNotes(1, many).size() == static_cast<std::size_t>(kMaxNotesPerTrack));
    const DocumentStore reference = store;
    std::string refusal = "stale";
    CHECK(store.insertChords(14, {chordWith(kC, 4.0, {chordNote(laneNamed("Bass", TrackRole::Bass, 1), 0, 480, 60)})},
                             &refusal).empty());
    CHECK(refusal == "At most " + std::to_string(kMaxNotesPerTrack) + " notes per track");
    checkRefused(store, reference);
}

TEST_CASE("insertChords with notes: the chords become the selection, the note selection stays, a no-notes insert takes no ids") {
    DocumentStore store = oneNotePerChord();
    store.setNoteSelection(1, {2, 3});
    const int nextTrack = store.nextTrackId();
    const int nextNote = store.nextNoteId();
    REQUIRE(store.insertChords(0, {ChordSpec{kC, 2.0}}) == std::vector<int>{4});
    CHECK(store.nextTrackId() == nextTrack);
    CHECK(store.nextNoteId() == nextNote);
    CHECK(store.noteSelection() == NoteSelection{1, {2, 3}});
    REQUIRE(store.insertChords(0, {chordWith(kC, 2.0, {chordNote(laneNamed("Bass", TrackRole::Bass), 0, 480, 40)})}) ==
            std::vector<int>{5});
    CHECK(store.selectedEventIds() == std::vector<int>{5});
    CHECK(store.noteSelection() == NoteSelection{1, {2, 3}});  // untouched: the stale drop only
    CHECK(store.nextNoteId() == nextNote + 1);
}

TEST_CASE("insertChords with notes: an exhausted track or note id counter refuses; nothing is consumed") {
    DocumentStore store = threeChords();
    // A state whose counters sit at the top: replace() takes them as given.
    Document doc = store.doc();
    doc.tracks.push_back(Track{1, "Bass", TrackRole::Bass, false, false, {}});
    SUBCASE("note ids") {
        store.replace(doc, store.nextId(), store.nextSectionId(), 2, INT_MAX - 1);
        const DocumentStore reference = store;
        std::string refusal;
        CHECK(store.insertChords(0, {chordWith(kC, 4.0, {chordNote(laneNamed("Bass", TrackRole::Bass), 0, 480, 40),
                                                         chordNote(laneNamed("Bass", TrackRole::Bass), 0, 480, 41)})},
                                 &refusal).empty());
        CHECK(refusal == "No note ids are left");
        checkRefused(store, reference);
    }
    SUBCASE("track ids") {
        store.replace(doc, store.nextId(), store.nextSectionId(), INT_MAX, 1);
        const DocumentStore reference = store;
        std::string refusal;
        CHECK(store.insertChords(0, {chordWith(kC, 4.0, {chordNote(laneNamed("New", TrackRole::Lead), 0, 480, 40)})},
                                 &refusal).empty());
        CHECK(refusal == "No track ids are left");
        checkRefused(store, reference);
    }
}

// --- transpose on all three paths (tracks design D18) --------------------------------------------------

namespace {

Key keyOf(char letter, int alter, Mode mode) {
    return Key{Tonic{letter, alter}, mode};
}

/// `pitch` + `interval`, an octave off when that leaves 0..127 (D18), written out independently of the engine.
int expectedPitch(int pitch, int interval) {
    const int direct = pitch + interval;
    if (direct > 127) {
        return direct - 12;
    }
    if (direct < 0) {
        return direct + 12;
    }
    return direct;
}

/// C, F, G7 (ids 1..3, four beats) with a Bass track (id 1: notes 1..4) and a Lead track (id 2: note 5).
/// Bass: pitch 2 (id 1) and 6 (id 2) start together on C, 40 on F, 125 on G7; Lead: 60 on C. Under a move of
/// -5 the notes 2 and 6 cross (9 and 1), so the bass order changes.
DocumentStore transposeFixture() {
    DocumentStore store = threeChords();
    REQUIRE(store.addTrack(TrackRole::Bass) == 1);
    REQUIRE(store.addTrack(TrackRole::Lead) == 2);
    REQUIRE(store.insertNotes(1, {ns(2, 0, 480), ns(6, 0, 480), ns(40, kBar, 480, 90), ns(125, 2 * kBar, 480, 80)}) ==
            std::vector<int>{1, 2, 3, 4});
    REQUIRE(store.insertNotes(2, {ns(60, 0, 960, 70)}) == std::vector<int>{5});
    return store;
}

/// Every track note of `store`, id to pitch.
std::unordered_map<int, int> pitchById(const DocumentStore& store) {
    std::unordered_map<int, int> pitches;
    for (const Track& t : store.doc().tracks) {
        for (const TrackNote& n : t.notes) {
            pitches[n.id] = n.pitch;
        }
    }
    return pitches;
}

/// Checks `after` is `before` with every note pitch moved by `interval` (folded) and nothing else of a note or
/// a track changed, and that the notes of each track of `after` are in D4's order.
void checkNotesMoved(const Document& before, const Document& after, int interval) {
    REQUIRE(after.tracks.size() == before.tracks.size());
    const ChordTimeline timeline = chordTimeline(after);
    for (std::size_t t = 0; t < before.tracks.size(); ++t) {
        const Track& a = after.tracks[t];
        const Track& b = before.tracks[t];
        CHECK(a.id == b.id);
        CHECK(a.name == b.name);
        CHECK(a.role == b.role);
        CHECK(a.mute == b.mute);
        CHECK(a.solo == b.solo);
        REQUIRE(a.notes.size() == b.notes.size());
        for (const TrackNote& old : b.notes) {
            const auto it = std::find_if(a.notes.begin(), a.notes.end(), [&](const TrackNote& n) { return n.id == old.id; });
            REQUIRE(it != a.notes.end());
            CHECK(it->pitch == expectedPitch(old.pitch, interval));
            CHECK(it->anchorEventId == old.anchorEventId);
            CHECK(it->offsetTick == old.offsetTick);
            CHECK(it->lengthTicks == old.lengthTicks);
            CHECK(it->velocity == old.velocity);
        }
        std::vector<TrackNote> sorted = a.notes;
        sortTrackNotes(sorted, timeline);
        CHECK(sorted == a.notes);
    }
}

std::vector<ChordEvent> movedBy(std::vector<ChordEvent> events, int semitones) {
    transposeEvents(events, semitones);
    return events;
}

}  // namespace

TEST_CASE("setProjectKey with the switch on: the notes move by chooseKeyTranspose's interval with the chords, one undo step") {
    DocumentStore store = transposeFixture();
    store.setTransposeWithKey(true);
    const int interval = chooseKeyTranspose(store.doc().events, 7);
    REQUIRE(interval == -5);  // the direction the chords take, not +7
    const std::vector<ChordEvent> movedChords = movedBy(store.doc().events, interval);
    const Document before = store.doc();
    const std::uint64_t revision = store.revision();

    store.setProjectKey(keyOf('G', 0, Mode::Ionian));

    const Document after = store.doc();
    CHECK(after.events == movedChords);
    checkNotesMoved(before, after, interval);
    CHECK(pitchById(store) == std::unordered_map<int, int>{{1, 9}, {2, 1}, {3, 35}, {4, 120}, {5, 55}});
    CHECK(store.revision() == revision + 1);  // one successful commit
    std::string error;
    CHECK(validateTracks(after, store.nextTrackId(), store.nextNoteId(), error));

    REQUIRE(store.undo());
    CHECK(store.doc() == before);  // chords, key and notes, one step
    REQUIRE(store.redo());
    CHECK(store.doc() == after);
    CHECK_FALSE(store.redo());
}

TEST_CASE("setProjectKey with the switch on: the notes are re-sorted and the note selection names the same ids in the new order") {
    DocumentStore store = transposeFixture();
    store.setTransposeWithKey(true);
    REQUIRE(noteIdsOf(store) == std::vector<int>{1, 2, 3, 4});  // pitch 2 before pitch 6 on the first chord
    store.setNoteSelection(1, {1, 2, 4});
    REQUIRE(store.noteSelection() == NoteSelection{1, {1, 2, 4}});

    store.setProjectKey(keyOf('G', 0, Mode::Ionian));

    CHECK(noteIdsOf(store) == std::vector<int>{2, 1, 3, 4});  // pitch 1 (was 6) now before pitch 9 (was 2)
    CHECK(store.noteSelection() == NoteSelection{1, {2, 1, 4}});

    REQUIRE(store.undo());
    CHECK(noteIdsOf(store) == std::vector<int>{1, 2, 3, 4});
    CHECK(store.noteSelection() == NoteSelection{1, {1, 2, 4}});
}

TEST_CASE("setProjectKey with the switch off moves no note") {
    DocumentStore store = transposeFixture();
    const std::vector<Track> tracks = store.doc().tracks;
    const std::uint64_t revision = store.revision();
    store.setProjectKey(keyOf('G', 0, Mode::Ionian));
    CHECK(store.doc().projectKey == keyOf('G', 0, Mode::Ionian));
    CHECK(store.doc().tracks == tracks);
    CHECK(store.revision() == revision + 1);  // the key change itself
}

TEST_CASE("setProjectKey: a mode-only change and an enharmonic respelling move no note, switch on") {
    DocumentStore store = transposeFixture();
    store.setTransposeWithKey(true);
    const std::vector<Track> tracks = store.doc().tracks;
    const std::vector<ChordEvent> events = store.doc().events;
    store.setProjectKey(keyOf('C', 0, Mode::Aeolian));
    CHECK(store.doc().tracks == tracks);
    CHECK(store.doc().events == events);
    store.setProjectKey(keyOf('B', 1, Mode::Aeolian));  // B# is C again
    CHECK(store.doc().tracks == tracks);
    CHECK(store.doc().events == events);
}

TEST_CASE("setProjectKey: a tonic and mode change together moves the notes by the tonic interval") {
    DocumentStore store = transposeFixture();
    store.setTransposeWithKey(true);
    const Document before = store.doc();
    const int interval = chooseKeyTranspose(before.events, 2);
    store.setProjectKey(keyOf('D', 0, Mode::Dorian));
    checkNotesMoved(before, store.doc(), interval);
    CHECK(store.doc().events == movedBy(before.events, interval));
}

TEST_CASE("setProjectKey with no chords: the tracks are empty and stay so, only the key changes") {
    DocumentStore store;
    REQUIRE(store.addTrack(TrackRole::Bass) == 1);
    store.setTransposeWithKey(true);
    const std::vector<Track> tracks = store.doc().tracks;
    store.setProjectKey(keyOf('F', 0, Mode::Ionian));
    CHECK(store.doc().projectKey == keyOf('F', 0, Mode::Ionian));
    CHECK(store.doc().tracks == tracks);
}

TEST_CASE("transposeFromKey carries the notes by the chords' interval in the same step") {
    DocumentStore store = transposeFixture();
    store.setProjectKey(keyOf('G', 0, Mode::Ionian));  // switch off: nothing moved yet
    const int interval = chooseKeyTranspose(store.doc().events, 7);
    REQUIRE(interval == -5);
    const std::vector<ChordEvent> movedChords = movedBy(store.doc().events, interval);
    const Document before = store.doc();
    store.setNoteSelection(1, {1, 2});
    const std::uint64_t revision = store.revision();

    REQUIRE(store.transposeFromKey(0));

    CHECK(store.doc().events == movedChords);
    checkNotesMoved(before, store.doc(), interval);
    CHECK(noteIdsOf(store) == std::vector<int>{2, 1, 3, 4});
    CHECK(store.noteSelection() == NoteSelection{1, {2, 1}});
    CHECK(store.revision() == revision + 1);

    REQUIRE(store.undo());
    CHECK(store.doc() == before);
    CHECK(noteIdsOf(store) == std::vector<int>{1, 2, 3, 4});
}

TEST_CASE("transposeFromKey: the same tonic changes nothing, and neither does a document with no chords or notes") {
    DocumentStore store = transposeFixture();
    const DocumentStore reference = store;
    CHECK_FALSE(store.transposeFromKey(0));  // the project tonic is C
    checkRefused(store, reference);

    DocumentStore empty;
    empty.addTrack(TrackRole::Bass);
    empty.setProjectKey(keyOf('G', 0, Mode::Ionian));
    const DocumentStore emptyReference = empty;
    CHECK_FALSE(empty.transposeFromKey(0));
    checkRefused(empty, emptyReference);
}

TEST_CASE("transposeProgression moves chords and notes by exactly the supplied interval, +7 stays +7 and -7 stays -7") {
    for (const int semitones : {7, -7, 1, -1, 11, -11, 5}) {
        CAPTURE(semitones);
        DocumentStore store = transposeFixture();
        const Document before = store.doc();
        const std::vector<ChordEvent> movedChords = movedBy(before.events, semitones);
        const std::uint64_t revision = store.revision();

        REQUIRE(store.transposeProgression(semitones));

        CHECK(store.doc().events == movedChords);
        checkNotesMoved(before, store.doc(), semitones);
        CHECK(store.revision() == revision + 1);
    }
    // Where chooseKeyTranspose would go the other way, the supplied interval still wins.
    DocumentStore store = transposeFixture();
    REQUIRE(chooseKeyTranspose(store.doc().events, 7) == -5);
    REQUIRE(store.transposeProgression(7));
    CHECK(pitchById(store) == std::unordered_map<int, int>{{1, 9}, {2, 13}, {3, 47}, {4, 120}, {5, 67}});
}

TEST_CASE("transposeProgression: a note at 125 moved +7 lands at 120 and one at 2 moved -7 lands at 7") {
    DocumentStore up = transposeFixture();
    REQUIRE(up.transposeProgression(7));
    CHECK(pitchById(up).at(4) == 120);
    DocumentStore down = transposeFixture();
    REQUIRE(down.transposeProgression(-7));
    CHECK(pitchById(down).at(1) == 7);
    CHECK(pitchById(down).at(2) == 11);  // 6 - 7 = -1, folded up
}

TEST_CASE("transposeProgression: one undo step restores chords and notes, redo applies both, a refusal leaves everything") {
    DocumentStore store = transposeFixture();
    store.setNoteSelection(1, {1, 2});
    const Document before = store.doc();
    checkOneStep(store, [](DocumentStore& s) { REQUIRE(s.transposeProgression(-7)); });
    CHECK(store.doc().events != before.events);
    CHECK(store.doc().tracks != before.tracks);

    for (const int refused : {0, 12, -12, 100}) {
        CAPTURE(refused);
        const DocumentStore reference = store;
        CHECK_FALSE(store.transposeProgression(refused));
        checkRefused(store, reference);
    }
}

TEST_CASE("transposeProgression with no chords moves nothing and pushes no history, tracks or not") {
    DocumentStore store;
    REQUIRE(store.addTrack(TrackRole::Lead) == 1);
    const DocumentStore reference = store;
    CHECK_FALSE(store.transposeProgression(7));
    checkRefused(store, reference);
}

TEST_CASE("transpose: an inactive note (past its shortened chord) moves too, and only its pitch") {
    DocumentStore store = threeChordsAndBass();
    REQUIRE(store.insertNotes(1, {ns(50, 2000, 480)}) == std::vector<int>{1});
    REQUIRE(store.setLength(1, 1.0));  // the note now starts past its chord's end: inactive, kept (D3)
    const Document before = store.doc();
    REQUIRE(store.transposeProgression(3));
    checkNotesMoved(before, store.doc(), 3);
}
