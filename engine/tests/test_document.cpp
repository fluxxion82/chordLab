#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/document.h"
#include "chordlab/pitch.h"
#include "chordlab/scale.h"

#include <cstddef>
#include <random>
#include <string_view>
#include <vector>

using namespace chordlab;

namespace {

Key key(char letter, int alter, Mode mode) {
    return Key{Tonic{letter, alter}, mode};
}

Chord chord(PitchClass root, std::string_view typeId) {
    return Chord{root, &chordType(typeId)};
}

const Chord kC = chord(0, "maj");
const Chord kF = chord(5, "maj");
const Chord kG = chord(7, "7");

/// Ids of the store's events in playback order.
std::vector<int> idsOf(const DocumentStore& store) {
    std::vector<int> ids;
    for (const ChordEvent& e : store.doc().events) {
        ids.push_back(e.id);
    }
    return ids;
}

/// Start beat of every event in order, followed by the end beat.
std::vector<double> startsOf(const DocumentStore& store) {
    std::vector<double> starts;
    const int n = static_cast<int>(store.doc().events.size());
    for (int i = 0; i <= n; ++i) {
        starts.push_back(store.doc().startBeatOf(i));
    }
    return starts;
}

/// A store holding C (id 1), F (id 2), G7 (id 3), four beats each at velocity 100.
DocumentStore threeChords() {
    DocumentStore store;
    store.appendChord(kC, 4.0, 100);
    store.appendChord(kF, 4.0, 100);
    store.appendChord(kG, 4.0, 100);
    return store;
}

/// Runs `mutate`, checks it changed the document, then checks that undo restores the document from before
/// and redo restores the document from after. Leaves the store in the "after" state.
template <typename Mutate>
void checkRoundTrip(DocumentStore& store, Mutate mutate) {
    const Document before = store.doc();
    mutate(store);
    const Document after = store.doc();
    REQUIRE_FALSE(after == before);
    REQUIRE(store.undo());
    CHECK(store.doc() == before);
    REQUIRE(store.redo());
    CHECK(store.doc() == after);
}

}  // namespace

// --- Document ------------------------------------------------------------------------

TEST_CASE("Document: defaults to C Ionian, empty, looping and following the host") {
    const Document doc;
    CHECK(doc.projectKey == key('C', 0, Mode::Ionian));
    CHECK(doc.browsedKey == key('C', 0, Mode::Ionian));
    CHECK(doc.events.empty());
    CHECK(doc.progressionLoop == true);
    CHECK(doc.followHost == true);
}

TEST_CASE("Document: empty document has endBeat 0 and startBeatOf(0) 0") {
    const Document doc;
    CHECK(doc.endBeat() == 0.0);
    CHECK(doc.startBeatOf(0) == 0.0);
}

TEST_CASE("Document: startBeatOf is the sum of the lengths before the index; index == size is the end") {
    Document doc;
    doc.events.push_back(ChordEvent{1, kC, 4.0, 100});
    doc.events.push_back(ChordEvent{2, kF, 1.5, 100});
    doc.events.push_back(ChordEvent{3, kG, 0.25, 100});
    CHECK(doc.startBeatOf(0) == 0.0);
    CHECK(doc.startBeatOf(1) == 4.0);
    CHECK(doc.startBeatOf(2) == 5.5);
    CHECK(doc.startBeatOf(3) == 5.75);
    CHECK(doc.endBeat() == 5.75);
}

TEST_CASE("ChordEvent / Document: equality compares every field") {
    const ChordEvent a{1, kC, 4.0, 100};
    CHECK(a == ChordEvent{1, kC, 4.0, 100});
    CHECK(a != ChordEvent{2, kC, 4.0, 100});
    CHECK(a != ChordEvent{1, kF, 4.0, 100});
    CHECK(a != ChordEvent{1, kC, 2.0, 100});
    CHECK(a != ChordEvent{1, kC, 4.0, 99});

    Document x;
    Document y;
    CHECK(x == y);
    y.projectKey = key('D', 0, Mode::Dorian);
    CHECK(x != y);
    y = x;
    y.browsedKey = key('E', -1, Mode::Lydian);
    CHECK(x != y);
    y = x;
    y.events.push_back(a);
    CHECK(x != y);
    y = x;
    y.progressionLoop = false;
    CHECK(x != y);
    y = x;
    y.followHost = false;
    CHECK(x != y);
}

TEST_CASE("kMinLengthBeats / kMaxLengthBeats: plan values") {
    CHECK(kMinLengthBeats == 0.25);
    CHECK(kMaxLengthBeats == 64.0);
}

// --- appendChord ---------------------------------------------------------------------

TEST_CASE("appendChord: three chords abut at 0, 4, 8 with endBeat 12 and ids 1, 2, 3") {
    const DocumentStore store = threeChords();
    CHECK(idsOf(store) == std::vector<int>{1, 2, 3});
    CHECK(startsOf(store) == std::vector<double>{0.0, 4.0, 8.0, 12.0});
    CHECK(store.doc().endBeat() == 12.0);
    CHECK(store.doc().events[0].chord == kC);
    CHECK(store.doc().events[1].chord == kF);
    CHECK(store.doc().events[2].chord == kG);
    CHECK(store.doc().events[0].velocity == 100);
    CHECK(store.doc().events[0].lengthBeats == 4.0);
}

TEST_CASE("appendChord: defaults to four beats at velocity 100") {
    DocumentStore store;
    const int id = store.appendChord(kC);
    REQUIRE(store.findIndex(id) == 0);
    CHECK(store.doc().events[0].lengthBeats == 4.0);
    CHECK(store.doc().events[0].velocity == 100);
}

TEST_CASE("appendChord: returns ids 1 then 2 for two appends") {
    DocumentStore store;
    CHECK(store.appendChord(kC, 4.0, 100) == 1);
    CHECK(store.appendChord(kC, 4.0, 100) == 2);
}

TEST_CASE("appendChord: clamps length to [kMin, kMax] and velocity to [1, 127]") {
    DocumentStore store;
    const int tooShort = store.appendChord(kC, 0.01, 300);
    const int tooLong = store.appendChord(kC, 1000.0, 0);
    const int negative = store.appendChord(kC, -3.0, -5);
    CHECK(store.doc().events[static_cast<std::size_t>(store.findIndex(tooShort))].lengthBeats == 0.25);
    CHECK(store.doc().events[static_cast<std::size_t>(store.findIndex(tooShort))].velocity == 127);
    CHECK(store.doc().events[static_cast<std::size_t>(store.findIndex(tooLong))].lengthBeats == 64.0);
    CHECK(store.doc().events[static_cast<std::size_t>(store.findIndex(tooLong))].velocity == 1);
    CHECK(store.doc().events[static_cast<std::size_t>(store.findIndex(negative))].lengthBeats == 0.25);
    CHECK(store.doc().events[static_cast<std::size_t>(store.findIndex(negative))].velocity == 1);
}

TEST_CASE("appendChord: ids strictly increase across remove + append") {
    DocumentStore store = threeChords();
    store.removeChord(2);
    CHECK(store.appendChord(kC) == 4);
    CHECK(idsOf(store) == std::vector<int>{1, 3, 4});
}

TEST_CASE("appendChord: the next append after undoing an append still gets a fresh id") {
    DocumentStore store;
    CHECK(store.appendChord(kC) == 1);
    REQUIRE(store.undo());
    CHECK(store.doc().events.empty());
    CHECK(store.appendChord(kC) == 2);
    CHECK(idsOf(store) == std::vector<int>{2});
}

TEST_CASE("appendChord: ids are never reused across undo/redo of removes") {
    DocumentStore store = threeChords();
    store.removeChord(3);
    REQUIRE(store.undo());
    CHECK(idsOf(store) == std::vector<int>{1, 2, 3});
    REQUIRE(store.redo());
    CHECK(idsOf(store) == std::vector<int>{1, 2});
    CHECK(store.appendChord(kC) == 4);
}

// --- findIndex -------------------------------------------------------------------------

TEST_CASE("findIndex: position in playback order, -1 when absent") {
    DocumentStore store = threeChords();
    CHECK(store.findIndex(1) == 0);
    CHECK(store.findIndex(2) == 1);
    CHECK(store.findIndex(3) == 2);
    CHECK(store.findIndex(0) == -1);
    CHECK(store.findIndex(4) == -1);
    CHECK(store.findIndex(-1) == -1);
    store.moveChord(3, 0);
    CHECK(store.findIndex(3) == 0);
    CHECK(store.findIndex(1) == 1);
    store.removeChord(1);
    CHECK(store.findIndex(1) == -1);
}

// --- setLength -------------------------------------------------------------------------

TEST_CASE("setLength: shortening the first chord shifts the later starts") {
    DocumentStore store = threeChords();
    store.setLength(1, 2.0);
    CHECK(startsOf(store) == std::vector<double>{0.0, 2.0, 6.0, 10.0});
    CHECK(store.doc().endBeat() == 10.0);
}

TEST_CASE("setLength: clamps to [kMinLengthBeats, kMaxLengthBeats]") {
    DocumentStore store = threeChords();
    store.setLength(2, 0.01);
    CHECK(store.doc().events[1].lengthBeats == 0.25);
    store.setLength(2, 1000.0);
    CHECK(store.doc().events[1].lengthBeats == 64.0);
    store.setLength(2, -1.0);
    CHECK(store.doc().events[1].lengthBeats == 0.25);
    store.setLength(2, 0.25);
    CHECK(store.doc().events[1].lengthBeats == 0.25);
    store.setLength(2, 64.0);
    CHECK(store.doc().events[1].lengthBeats == 64.0);
}

TEST_CASE("setLength: unknown id leaves the document unchanged and pushes no undo entry") {
    DocumentStore store = threeChords();
    const Document before = store.doc();
    store.setLength(99, 1.0);
    store.setLength(0, 1.0);
    store.setLength(-1, 1.0);
    CHECK(store.doc() == before);
    REQUIRE(store.undo());  // pops the third append, not a phantom setLength
    CHECK(idsOf(store) == std::vector<int>{1, 2});
}

TEST_CASE("setLength: setting the same length pushes no undo entry") {
    DocumentStore store;
    store.appendChord(kC, 4.0, 100);
    REQUIRE(store.undo());
    REQUIRE(store.redo());
    store.setLength(1, 4.0);
    CHECK(store.doc().events[0].lengthBeats == 4.0);
    REQUIRE(store.undo());  // undoes the append, not a phantom setLength
    CHECK(store.doc().events.empty());
    CHECK_FALSE(store.undo());
}

TEST_CASE("setLength: a value that clamps to the current length pushes no undo entry") {
    DocumentStore store;
    store.appendChord(kC, kMaxLengthBeats, 100);
    store.setLength(1, 1000.0);  // clamps to 64, already 64
    REQUIRE(store.undo());
    CHECK(store.doc().events.empty());
    CHECK_FALSE(store.undo());
}

// --- setVelocity -----------------------------------------------------------------------

TEST_CASE("setVelocity: clamps to [1, 127]") {
    DocumentStore store = threeChords();
    store.setVelocity(1, 300);
    CHECK(store.doc().events[0].velocity == 127);
    store.setVelocity(1, 0);
    CHECK(store.doc().events[0].velocity == 1);
    store.setVelocity(1, -42);
    CHECK(store.doc().events[0].velocity == 1);
    store.setVelocity(1, 64);
    CHECK(store.doc().events[0].velocity == 64);
}

TEST_CASE("setVelocity: unknown id and same value are no-ops with no undo entry") {
    DocumentStore store;
    store.appendChord(kC, 4.0, 100);
    store.setVelocity(42, 50);
    CHECK(store.doc().events[0].velocity == 100);
    store.setVelocity(1, 100);
    store.setVelocity(1, 999);  // clamps to 127: a real change
    CHECK(store.doc().events[0].velocity == 127);
    store.setVelocity(1, 127);  // same after clamping
    store.setVelocity(1, 200);  // same after clamping
    REQUIRE(store.undo());
    CHECK(store.doc().events[0].velocity == 100);
    REQUIRE(store.undo());
    CHECK(store.doc().events.empty());
    CHECK_FALSE(store.undo());
}

// --- moveChord -------------------------------------------------------------------------

TEST_CASE("moveChord: moving the last chord to the front gives order 3, 1, 2") {
    DocumentStore store = threeChords();
    store.moveChord(3, 0);
    CHECK(idsOf(store) == std::vector<int>{3, 1, 2});
    CHECK(store.doc().events[0].chord == kG);
    CHECK(startsOf(store) == std::vector<double>{0.0, 4.0, 8.0, 12.0});
}

TEST_CASE("moveChord: an index past the end clamps to the last position") {
    DocumentStore store = threeChords();
    store.moveChord(1, 99);
    CHECK(idsOf(store) == std::vector<int>{2, 3, 1});
}

TEST_CASE("moveChord: a negative index clamps to the first position") {
    DocumentStore store = threeChords();
    store.moveChord(2, -5);
    CHECK(idsOf(store) == std::vector<int>{2, 1, 3});
}

TEST_CASE("moveChord: moving forward and backward within the sequence") {
    DocumentStore store = threeChords();
    store.moveChord(1, 1);
    CHECK(idsOf(store) == std::vector<int>{2, 1, 3});
    store.moveChord(3, 1);
    CHECK(idsOf(store) == std::vector<int>{2, 3, 1});
    store.moveChord(1, 0);
    CHECK(idsOf(store) == std::vector<int>{1, 2, 3});
}

TEST_CASE("moveChord: unknown id is a no-op with no undo entry") {
    DocumentStore store = threeChords();
    const Document before = store.doc();
    store.moveChord(7, 0);
    store.moveChord(0, 1);
    store.moveChord(-1, 2);
    CHECK(store.doc() == before);
    REQUIRE(store.undo());  // pops the third append, not a phantom move
    CHECK(idsOf(store) == std::vector<int>{1, 2});
}

TEST_CASE("moveChord: moving a chord to its current index pushes no undo entry") {
    DocumentStore store = threeChords();
    store.moveChord(2, 1);
    store.moveChord(3, 99);  // clamps to 2, where it already is
    store.moveChord(1, -1);  // clamps to 0, where it already is
    CHECK(idsOf(store) == std::vector<int>{1, 2, 3});
    REQUIRE(store.undo());  // pops the third append, not a phantom move
    CHECK(idsOf(store) == std::vector<int>{1, 2});
}

TEST_CASE("moveChord: on an empty document any id is unknown and nothing is pushed") {
    DocumentStore store;
    store.moveChord(1, 0);
    CHECK(store.doc().events.empty());
    CHECK_FALSE(store.undo());
}

// --- removeChord -----------------------------------------------------------------------

TEST_CASE("removeChord: removing the middle chord closes the gap") {
    DocumentStore store = threeChords();
    store.removeChord(2);
    CHECK(store.doc().events.size() == 2);
    CHECK(idsOf(store) == std::vector<int>{1, 3});
    CHECK(startsOf(store) == std::vector<double>{0.0, 4.0, 8.0});
    CHECK(store.doc().endBeat() == 8.0);
}

TEST_CASE("removeChord: removing every chord leaves an empty document") {
    DocumentStore store = threeChords();
    store.removeChord(1);
    store.removeChord(3);
    store.removeChord(2);
    CHECK(store.doc().events.empty());
    CHECK(store.doc().endBeat() == 0.0);
}

TEST_CASE("removeChord: unknown id is a no-op with no undo entry") {
    DocumentStore store = threeChords();
    const Document before = store.doc();
    store.removeChord(2);
    store.removeChord(2);  // already gone
    store.removeChord(0);
    store.removeChord(-1);
    CHECK(idsOf(store) == std::vector<int>{1, 3});
    REQUIRE(store.undo());  // the real remove
    CHECK(store.doc() == before);
    REQUIRE(store.undo());  // pops the third append, not a phantom remove
    CHECK(idsOf(store) == std::vector<int>{1, 2});
}

// --- Keys and toggles ------------------------------------------------------------------

TEST_CASE("setProjectKey / setBrowsedKey: set independently and are undoable") {
    DocumentStore store;
    store.setProjectKey(key('G', 0, Mode::Mixolydian));
    CHECK(store.doc().projectKey == key('G', 0, Mode::Mixolydian));
    CHECK(store.doc().browsedKey == key('C', 0, Mode::Ionian));
    store.setBrowsedKey(key('E', -1, Mode::Aeolian));
    CHECK(store.doc().browsedKey == key('E', -1, Mode::Aeolian));
    CHECK(store.doc().projectKey == key('G', 0, Mode::Mixolydian));
    REQUIRE(store.undo());
    CHECK(store.doc().browsedKey == key('C', 0, Mode::Ionian));
    REQUIRE(store.undo());
    CHECK(store.doc().projectKey == key('C', 0, Mode::Ionian));
    CHECK_FALSE(store.undo());
}

TEST_CASE("setProjectKey / setBrowsedKey: the same key pushes no undo entry") {
    DocumentStore store;
    store.setProjectKey(key('C', 0, Mode::Ionian));
    store.setBrowsedKey(key('C', 0, Mode::Ionian));
    CHECK_FALSE(store.undo());
}

TEST_CASE("setProgressionLoop / setFollowHost: toggle independently and are undoable") {
    DocumentStore store;
    store.setProgressionLoop(false);
    CHECK(store.doc().progressionLoop == false);
    CHECK(store.doc().followHost == true);
    store.setFollowHost(false);
    CHECK(store.doc().followHost == false);
    CHECK(store.doc().progressionLoop == false);
    REQUIRE(store.undo());
    CHECK(store.doc().followHost == true);
    REQUIRE(store.undo());
    CHECK(store.doc().progressionLoop == true);
    CHECK_FALSE(store.undo());
}

TEST_CASE("setProgressionLoop / setFollowHost: the same value pushes no undo entry") {
    DocumentStore store;
    store.setProgressionLoop(true);
    store.setFollowHost(true);
    CHECK_FALSE(store.undo());
}

// --- undo / redo -----------------------------------------------------------------------

TEST_CASE("undo / redo: return false on a fresh store") {
    DocumentStore store;
    CHECK_FALSE(store.undo());
    CHECK_FALSE(store.redo());
    CHECK(store.doc() == Document{});
}

TEST_CASE("undo / redo: every operation round-trips") {
    DocumentStore store;
    checkRoundTrip(store, [](DocumentStore& s) { s.appendChord(kC, 4.0, 100); });
    checkRoundTrip(store, [](DocumentStore& s) { s.appendChord(kF, 4.0, 100); });
    checkRoundTrip(store, [](DocumentStore& s) { s.appendChord(kG, 4.0, 100); });
    checkRoundTrip(store, [](DocumentStore& s) { s.setLength(1, 2.0); });
    checkRoundTrip(store, [](DocumentStore& s) { s.setLength(2, 0.01); });
    checkRoundTrip(store, [](DocumentStore& s) { s.setLength(3, 1000.0); });
    checkRoundTrip(store, [](DocumentStore& s) { s.setVelocity(1, 300); });
    checkRoundTrip(store, [](DocumentStore& s) { s.setVelocity(2, 0); });
    checkRoundTrip(store, [](DocumentStore& s) { s.moveChord(3, 0); });
    checkRoundTrip(store, [](DocumentStore& s) { s.moveChord(1, 99); });
    checkRoundTrip(store, [](DocumentStore& s) { s.removeChord(2); });
    checkRoundTrip(store, [](DocumentStore& s) { s.setProjectKey(key('A', 0, Mode::Aeolian)); });
    checkRoundTrip(store, [](DocumentStore& s) { s.setBrowsedKey(key('F', +1, Mode::Locrian)); });
    checkRoundTrip(store, [](DocumentStore& s) { s.setProgressionLoop(false); });
    checkRoundTrip(store, [](DocumentStore& s) { s.setFollowHost(false); });
    CHECK(idsOf(store) == std::vector<int>{3, 1});
}

TEST_CASE("undo / redo: undo walks back through the whole history and redo walks forward again") {
    DocumentStore store;
    std::vector<Document> history;
    history.push_back(store.doc());
    store.appendChord(kC);
    history.push_back(store.doc());
    store.appendChord(kF);
    history.push_back(store.doc());
    store.setLength(1, 1.0);
    history.push_back(store.doc());
    store.setProjectKey(key('D', 0, Mode::Dorian));
    history.push_back(store.doc());

    for (std::size_t i = history.size() - 1; i > 0; --i) {
        REQUIRE(store.undo());
        CHECK(store.doc() == history[i - 1]);
    }
    CHECK_FALSE(store.undo());
    CHECK(store.doc() == history[0]);
    for (std::size_t i = 1; i < history.size(); ++i) {
        REQUIRE(store.redo());
        CHECK(store.doc() == history[i]);
    }
    CHECK_FALSE(store.redo());
}

TEST_CASE("undo / redo: a new mutation after undo clears the redo stack") {
    DocumentStore store;
    store.appendChord(kC);
    store.appendChord(kF);
    REQUIRE(store.undo());
    store.setProgressionLoop(false);
    CHECK_FALSE(store.redo());
    CHECK(idsOf(store) == std::vector<int>{1});
    CHECK(store.doc().progressionLoop == false);
}

TEST_CASE("undo / redo: a no-op mutation after undo keeps the redo stack") {
    DocumentStore store;
    store.appendChord(kC);
    store.appendChord(kF);
    REQUIRE(store.undo());
    store.setLength(99, 1.0);        // unknown id
    store.setProgressionLoop(true);  // same value
    REQUIRE(store.redo());
    CHECK(idsOf(store) == std::vector<int>{1, 2});
}

// --- Property test ---------------------------------------------------------------------

TEST_CASE("DocumentStore: 200 random operations undo to the initial document and redo back") {
    std::mt19937 rng(20261010u);
    auto randInt = [&rng](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    auto randLength = [&rng]() { return std::uniform_real_distribution<double>(-2.0, 100.0)(rng); };
    const auto& types = allChordTypes();
    auto randChord = [&]() {
        return Chord{randInt(0, 11), &types[static_cast<std::size_t>(randInt(0, static_cast<int>(types.size()) - 1))]};
    };
    auto randKey = [&]() {
        return Key{Tonic{static_cast<char>('A' + randInt(0, 6)), randInt(-1, 1)},
                   static_cast<Mode>(randInt(0, kNumModes - 1))};
    };

    DocumentStore store;
    const Document initial = store.doc();

    // Mostly existing ids, sometimes an invalid one (0, negative, or past the counter).
    auto randId = [&]() {
        const auto& events = store.doc().events;
        if (!events.empty() && randInt(0, 4) != 0) {
            return events[static_cast<std::size_t>(randInt(0, static_cast<int>(events.size()) - 1))].id;
        }
        return randInt(-2, 400);
    };

    int appends = 0;
    for (int i = 0; i < 200; ++i) {
        switch (randInt(0, 11)) {
            case 0:
            case 1:
            case 2:
                store.appendChord(randChord(), randLength(), randInt(-10, 200));
                ++appends;
                break;
            case 3:
                store.moveChord(randId(), randInt(-3, 12));
                break;
            case 4:
                store.setLength(randId(), randLength());
                break;
            case 5:
                store.setVelocity(randId(), randInt(-10, 200));
                break;
            case 6:
                store.removeChord(randId());
                break;
            case 7:
                store.setProjectKey(randKey());
                break;
            case 8:
                store.setBrowsedKey(randKey());
                break;
            case 9:
                store.setProgressionLoop(randInt(0, 1) == 1);
                break;
            case 10:
                store.setFollowHost(randInt(0, 1) == 1);
                break;
            default:
                store.undo();
                store.redo();
                break;
        }
    }
    REQUIRE(appends > 20);  // the seed exercised the sequence
    const Document final = store.doc();

    // Every event's id is unique and the sequence is well formed.
    {
        std::vector<int> seen;
        for (const ChordEvent& e : final.events) {
            CHECK(e.id >= 1);
            for (const int s : seen) {
                CHECK(s != e.id);
            }
            seen.push_back(e.id);
            CHECK(e.lengthBeats >= kMinLengthBeats);
            CHECK(e.lengthBeats <= kMaxLengthBeats);
            CHECK(e.velocity >= 1);
            CHECK(e.velocity <= 127);
        }
    }

    int undos = 0;
    while (store.undo()) {
        ++undos;
    }
    CHECK(undos > 0);
    CHECK(store.doc() == initial);

    int redos = 0;
    while (store.redo()) {
        ++redos;
    }
    CHECK(redos == undos);
    CHECK(store.doc() == final);

    // Ids keep increasing after the whole history was replayed.
    const int fresh = store.appendChord(kC);
    for (const ChordEvent& e : final.events) {
        CHECK(fresh > e.id);
    }
}
