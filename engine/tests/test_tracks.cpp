#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/document.h"
#include "chordlab/feel.h"
#include "chordlab/meter.h"
#include "chordlab/tracks.h"

#include <climits>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
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

/// Checks that `store` refused an edit: unchanged, with the same history and counters as `reference`.
void checkRefused(const DocumentStore& store, const DocumentStore& reference) {
    CHECK(store.doc() == reference.doc());
    CHECK(sameHistoryTop(store, reference));
    CHECK(store.nextTrackId() == reference.nextTrackId());
    CHECK(store.nextNoteId() == reference.nextNoteId());
    CHECK(store.revision() == reference.revision());
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
    CHECK_FALSE(refusal.empty());
    refusal.clear();
    CHECK(store.insertNotes(1, {ns(36, 480, 480), ns(38, 3 * kBar + 5, 480)}, &refusal).empty());  // no partial insert
    CHECK_FALSE(refusal.empty());
    checkRefused(store, reference);
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
    DocumentStore store = threeChordsAndBass();
    REQUIRE(store.addTrack(TrackRole::Lead) == 2);
    std::vector<NoteSpec> specs;
    for (int i = 0; i < kMaxNotesPerTrack - 1; ++i) {
        specs.push_back(ns(40 + i % 40, i * 10, 30));
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
    CHECK_FALSE(refusal.empty());
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
