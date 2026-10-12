#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/document.h"
#include "chordlab/limits.h"
#include "chordlab/pitch.h"
#include "chordlab/scale.h"
#include "chordlab/voicing.h"

#include <climits>
#include <cstddef>
#include <limits>
#include <random>
#include <string_view>
#include <vector>

using namespace chordlab;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

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

/// The undoable part of a document: the progression, the project key and the auto voice-lead flag.
bool sameUndoableState(const Document& a, const Document& b) {
    return a.events == b.events && a.projectKey == b.projectKey && a.autoVoiceLead == b.autoVoiceLead;
}

/// The live (session) part of a document: browsed key and the two transport toggles.
bool sameLiveState(const Document& a, const Document& b) {
    return a.browsedKey == b.browsedKey && a.progressionLoop == b.progressionLoop && a.followHost == b.followHost;
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
    CHECK(doc.autoVoiceLead == false);
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
    CHECK(a.inversion == 0);  // the defaults: root position in the default register
    CHECK(a.octave == 0);
    CHECK(a.manualVoicing == false);
    CHECK(a == ChordEvent{1, kC, 4.0, 100, 0, 0, false});
    CHECK(a != ChordEvent{1, kC, 4.0, 100, 1, 0, false});
    CHECK(a != ChordEvent{1, kC, 4.0, 100, 0, -1, false});
    CHECK(a != ChordEvent{1, kC, 4.0, 100, 0, 0, true});

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
    y = x;
    y.autoVoiceLead = true;
    CHECK(x != y);
}

TEST_CASE("kMinLengthBeats / kMaxLengthBeats / kMaxUndoDepth: plan values") {
    CHECK(kMinLengthBeats == 0.25);
    CHECK(kMaxLengthBeats == 64.0);
    CHECK(DocumentStore::kMaxUndoDepth == 200);
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

TEST_CASE("appendChord: a new chord is in root position at octave 0") {
    DocumentStore store;
    store.appendChord(kG, 4.0, 100);
    CHECK(store.doc().events[0].inversion == 0);
    CHECK(store.doc().events[0].octave == 0);
    CHECK(store.doc().events[0].manualVoicing == false);
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

TEST_CASE("appendChord: a non-finite length is rejected atomically") {
    DocumentStore store;
    store.appendChord(kC);
    store.appendChord(kF);
    REQUIRE(store.undo());  // leaves a pending redo of the second append
    const Document before = store.doc();

    CHECK(store.appendChord(kG, kNaN) == -1);
    CHECK(store.appendChord(kG, kInf) == -1);
    CHECK(store.appendChord(kG, -kInf) == -1);
    CHECK(store.appendChord(kG, kNaN, 300) == -1);
    CHECK(store.doc() == before);

    REQUIRE(store.redo());  // the pending redo survived the rejected appends
    CHECK(idsOf(store) == std::vector<int>{1, 2});
    REQUIRE(store.undo());  // pops the real second append, not a phantom
    CHECK(idsOf(store) == std::vector<int>{1});
    CHECK(store.appendChord(kG) == 3);  // the rejected appends consumed no ids
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

TEST_CASE("DocumentStore: ids start at the constructor's firstId") {
    DocumentStore store(10);
    CHECK(store.appendChord(kC) == 10);
    CHECK(store.appendChord(kF) == 11);
    CHECK(store.findIndex(1) == -1);
    CHECK(store.findIndex(10) == 0);
}

TEST_CASE("appendChord: returns -1 and changes nothing once the id counter reaches INT_MAX") {
    DocumentStore store(INT_MAX - 1);
    CHECK(store.appendChord(kC) == INT_MAX - 1);
    store.setLength(INT_MAX - 1, 2.0);
    REQUIRE(store.undo());  // leaves a pending redo of the setLength
    const Document before = store.doc();

    CHECK(store.appendChord(kF) == -1);
    CHECK(store.appendChord(kF) == -1);
    CHECK(store.doc() == before);
    CHECK(store.findIndex(-1) == -1);

    REQUIRE(store.redo());  // the pending redo survived
    CHECK(store.doc().events[0].lengthBeats == 2.0);
    REQUIRE(store.undo());
    CHECK(store.doc().events[0].lengthBeats == 4.0);
    REQUIRE(store.undo());  // pops the real append, not a phantom
    CHECK(store.doc().events.empty());
    CHECK_FALSE(store.undo());

    DocumentStore exhausted(INT_MAX);
    CHECK(exhausted.appendChord(kC) == -1);
    CHECK(exhausted.doc().events.empty());
    CHECK_FALSE(exhausted.undo());
}

TEST_CASE("kMaxChords: plan value, and a full document of eight-note chords still fits a snapshot") {
    CHECK(kMaxChords == 256);
    CHECK(static_cast<std::size_t>(kMaxChords) * 8 * 2 <= kMaxSnapshotEvents);
}

TEST_CASE("appendChord: the chord past kMaxChords is refused, changing nothing and keeping the history") {
    DocumentStore store;
    for (int i = 0; i < kMaxChords; ++i) {
        REQUIRE(store.appendChord(kC) == i + 1);
    }
    store.setLength(1, 2.0);
    REQUIRE(store.undo());  // leaves a pending redo that the refusal must not clear
    const Document before = store.doc();
    REQUIRE(before.events.size() == static_cast<std::size_t>(kMaxChords));

    CHECK(store.appendChord(kF) == kNoEventId);
    CHECK(store.appendChord(kF) == kNoEventId);
    CHECK(store.doc() == before);
    CHECK(store.nextId() == kMaxChords + 1);  // no id consumed

    REQUIRE(store.redo());  // the pending redo survived
    CHECK(store.doc().events[0].lengthBeats == 2.0);
    REQUIRE(store.undo());
    CHECK(store.doc() == before);
    REQUIRE(store.undo());  // pops a real append, not a phantom entry
    CHECK(store.doc().events.size() == static_cast<std::size_t>(kMaxChords) - 1);

    // Room for exactly one more once one is gone.
    REQUIRE(store.redo());
    store.removeChord(1);
    CHECK(store.appendChord(kF) == kMaxChords + 1);
    CHECK(store.appendChord(kF) == kNoEventId);
    CHECK(store.doc().events.size() == static_cast<std::size_t>(kMaxChords));
}

// --- findIndex -------------------------------------------------------------------------

TEST_CASE("appendChord: the id counter advances only for an append that happened") {
    DocumentStore store;
    CHECK(store.nextId() == 1);
    CHECK(store.appendChord(kC, kNaN) == kNoEventId);
    CHECK(store.appendChord(kC, kInf) == kNoEventId);
    CHECK(store.nextId() == 1);
    CHECK(store.appendChord(kC) == 1);
    CHECK(store.nextId() == 2);
    REQUIRE(store.undo());
    CHECK(store.nextId() == 2);  // not undoable state

    DocumentStore exhausted(INT_MAX);
    CHECK(exhausted.nextId() == INT_MAX);
    CHECK(exhausted.appendChord(kC) == kNoEventId);
    CHECK(exhausted.nextId() == INT_MAX);
    CHECK(exhausted.doc().events.empty());
}

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

TEST_CASE("setLength: a non-finite length is rejected atomically") {
    DocumentStore store = threeChords();
    store.setLength(1, 2.0);
    REQUIRE(store.undo());  // leaves a pending redo of the setLength
    const Document before = store.doc();

    store.setLength(1, kNaN);
    store.setLength(1, kInf);
    store.setLength(1, -kInf);
    store.setLength(99, kNaN);
    CHECK(store.doc() == before);
    CHECK(store.doc().events[0].lengthBeats == 4.0);

    REQUIRE(store.redo());  // the pending redo survived
    CHECK(store.doc().events[0].lengthBeats == 2.0);
    REQUIRE(store.undo());
    CHECK(store.doc().events[0].lengthBeats == 4.0);
    REQUIRE(store.undo());  // pops the third append, not a phantom setLength
    CHECK(idsOf(store) == std::vector<int>{1, 2});
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

// --- setInversion / setOctave / resetVoicing -------------------------------------------------

TEST_CASE("setInversion: sets one event's inversion, clamped to the chord's inversions, and marks it manual") {
    DocumentStore store = threeChords();  // C (3 inversions), F, G7 (4 inversions)
    store.setInversion(1, 2);
    CHECK(store.doc().events[0].inversion == 2);
    CHECK(store.doc().events[0].manualVoicing == true);
    CHECK(store.doc().events[1].inversion == 0);
    CHECK(store.doc().events[1].manualVoicing == false);
    CHECK(store.doc().events[2].inversion == 0);
    store.setInversion(1, 3);  // C major has inversions 0..2
    CHECK(store.doc().events[0].inversion == 2);
    store.setInversion(1, 99);
    CHECK(store.doc().events[0].inversion == 2);
    store.setInversion(1, -1);
    CHECK(store.doc().events[0].inversion == 0);
    CHECK(store.doc().events[0].manualVoicing == true);  // root position by choice stays manual
    store.setInversion(1, INT_MIN);
    CHECK(store.doc().events[0].inversion == 0);
    store.setInversion(3, 3);  // G7 has inversions 0..3
    CHECK(store.doc().events[2].inversion == 3);
    store.setInversion(3, INT_MAX);
    CHECK(store.doc().events[2].inversion == 3);
    CHECK(inversionCount(kG) == 4);
}

TEST_CASE("setInversion: setting the current value on a fresh event is a change: it marks the event manual") {
    DocumentStore store;
    store.appendChord(kC, 4.0, 100);
    store.setInversion(1, 0);
    CHECK(store.doc().events[0].inversion == 0);
    CHECK(store.doc().events[0].manualVoicing == true);
    REQUIRE(store.undo());
    CHECK(store.doc().events[0].manualVoicing == false);
    REQUIRE(store.redo());
    CHECK(store.doc().events[0].manualVoicing == true);
}

TEST_CASE("setInversion: unknown id, and the current value on a manual event, are no-ops with no undo entry") {
    DocumentStore store;
    store.appendChord(kC, 4.0, 100);
    store.setInversion(1, 2);
    const Document before = store.doc();
    REQUIRE(before.events[0].manualVoicing == true);
    store.setInversion(42, 1);
    store.setInversion(0, 1);
    store.setInversion(-1, 1);
    store.setInversion(1, 2);    // already 2
    store.setInversion(1, 100);  // clamps to 2
    CHECK(store.doc() == before);
    REQUIRE(store.undo());  // the real setInversion, not a phantom entry
    CHECK(store.doc().events[0].inversion == 0);
    CHECK(store.doc().events[0].manualVoicing == false);
    REQUIRE(store.undo());  // the append
    CHECK(store.doc().events.empty());
    CHECK_FALSE(store.undo());
}

TEST_CASE("setOctave: sets one event's octave, clamped to -1..1, and marks it manual") {
    DocumentStore store = threeChords();
    store.setOctave(2, 1);
    CHECK(store.doc().events[1].octave == 1);
    CHECK(store.doc().events[1].manualVoicing == true);
    CHECK(store.doc().events[0].octave == 0);
    CHECK(store.doc().events[0].manualVoicing == false);
    CHECK(store.doc().events[2].octave == 0);
    store.setOctave(2, 2);
    CHECK(store.doc().events[1].octave == 1);
    store.setOctave(2, INT_MAX);
    CHECK(store.doc().events[1].octave == 1);
    store.setOctave(2, -1);
    CHECK(store.doc().events[1].octave == -1);
    store.setOctave(2, INT_MIN);
    CHECK(store.doc().events[1].octave == -1);
    store.setOctave(2, 0);
    CHECK(store.doc().events[1].octave == 0);
    CHECK(store.doc().events[1].manualVoicing == true);
    CHECK(kMinVoicingOctave == -1);
    CHECK(kMaxVoicingOctave == 1);
}

TEST_CASE("setOctave: the current value on a fresh event marks it manual; on a manual event it is a no-op") {
    DocumentStore store;
    store.appendChord(kC, 4.0, 100);
    const Document fresh = store.doc();
    store.setOctave(42, 1);  // unknown id
    CHECK(store.doc() == fresh);
    store.setOctave(1, 0);   // the current value, but the event was not manual yet
    CHECK(store.doc().events[0].manualVoicing == true);
    const Document manual = store.doc();
    store.setOctave(1, 0);
    store.setOctave(1, 5);   // clamps to 1: a real change
    store.setOctave(1, 1);
    store.setOctave(1, 3);
    CHECK(store.doc().events[0].octave == 1);
    REQUIRE(store.undo());
    CHECK(store.doc() == manual);
    REQUIRE(store.undo());
    CHECK(store.doc().events[0].manualVoicing == false);
    REQUIRE(store.undo());
    CHECK(store.doc().events.empty());
    CHECK_FALSE(store.undo());
}

TEST_CASE("resetVoicing: returns an event to root position in the default register and clears the flag") {
    DocumentStore store = threeChords();
    store.setInversion(2, 1);
    store.setOctave(2, -1);
    store.setInversion(3, 0);  // manual root position
    store.resetVoicing(2);
    CHECK(store.doc().events[1] == ChordEvent{2, kF, 4.0, 100, 0, 0, false});
    CHECK(store.doc().events[2] == ChordEvent{3, kG, 4.0, 100, 0, 0, true});
    store.resetVoicing(3);
    CHECK(store.doc().events[2].manualVoicing == false);
    REQUIRE(store.undo());
    CHECK(store.doc().events[2].manualVoicing == true);
    REQUIRE(store.undo());
    CHECK(store.doc().events[1] == ChordEvent{2, kF, 4.0, 100, 1, -1, true});
    REQUIRE(store.redo());
    CHECK(store.doc().events[1].manualVoicing == false);
}

TEST_CASE("resetVoicing: unknown id and an event already at the defaults are no-ops with no undo entry") {
    DocumentStore store = threeChords();
    const Document before = store.doc();
    store.resetVoicing(99);
    store.resetVoicing(0);
    store.resetVoicing(-1);
    store.resetVoicing(1);  // fresh: already 0, 0, not manual
    CHECK(store.doc() == before);
    REQUIRE(store.undo());  // pops the third append, not a phantom reset
    CHECK(idsOf(store) == std::vector<int>{1, 2});
}

TEST_CASE("setInversion / setOctave: survive a move and a remove of another chord, and undo") {
    DocumentStore store = threeChords();
    store.setInversion(2, 1);
    store.setOctave(2, -1);
    store.moveChord(2, 0);
    store.removeChord(1);
    REQUIRE(idsOf(store) == std::vector<int>{2, 3});
    CHECK(store.doc().events[0].inversion == 1);
    CHECK(store.doc().events[0].octave == -1);
    CHECK(store.doc().events[0].manualVoicing == true);
    REQUIRE(store.undo());
    REQUIRE(store.undo());
    REQUIRE(store.undo());
    CHECK(store.doc().events[1].inversion == 1);
    CHECK(store.doc().events[1].octave == 0);
    CHECK(store.doc().events[1].manualVoicing == true);
    REQUIRE(store.undo());
    CHECK(store.doc().events[1].inversion == 0);
    CHECK(store.doc().events[1].manualVoicing == false);
}

// --- Auto voice-lead (undoable: it changes the compiled progression) ---------------------------

TEST_CASE("setAutoVoiceLead: undoable, and the same value pushes no undo entry") {
    DocumentStore store;
    store.setAutoVoiceLead(false);  // already off
    CHECK_FALSE(store.undo());
    store.setAutoVoiceLead(true);
    CHECK(store.doc().autoVoiceLead == true);
    store.setAutoVoiceLead(true);
    REQUIRE(store.undo());
    CHECK(store.doc().autoVoiceLead == false);
    CHECK_FALSE(store.undo());
    REQUIRE(store.redo());
    CHECK(store.doc().autoVoiceLead == true);
    CHECK_FALSE(store.redo());
}

TEST_CASE("undo / redo: autoVoiceLead is restored with the progression, unlike the live toggles") {
    DocumentStore store;
    store.appendChord(kC);
    store.setAutoVoiceLead(true);
    store.appendChord(kF);
    store.setFollowHost(false);

    REQUIRE(store.undo());  // the second append
    CHECK(idsOf(store) == std::vector<int>{1});
    CHECK(store.doc().autoVoiceLead == true);
    CHECK(store.doc().followHost == false);
    REQUIRE(store.undo());  // the flag
    CHECK(store.doc().autoVoiceLead == false);
    CHECK(store.doc().followHost == false);  // live: kept
    REQUIRE(store.undo());  // the first append
    CHECK(store.doc().events.empty());
    CHECK_FALSE(store.undo());
    REQUIRE(store.redo());
    REQUIRE(store.redo());
    CHECK(store.doc().autoVoiceLead == true);
    REQUIRE(store.redo());
    CHECK(idsOf(store) == std::vector<int>{1, 2});
    CHECK(store.doc().followHost == false);
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

// --- Project key (undoable) --------------------------------------------------------------

TEST_CASE("setProjectKey: sets the project key only, and is undoable") {
    DocumentStore store;
    store.setProjectKey(key('G', 0, Mode::Mixolydian));
    CHECK(store.doc().projectKey == key('G', 0, Mode::Mixolydian));
    CHECK(store.doc().browsedKey == key('C', 0, Mode::Ionian));
    store.setProjectKey(key('A', 0, Mode::Aeolian));
    REQUIRE(store.undo());
    CHECK(store.doc().projectKey == key('G', 0, Mode::Mixolydian));
    REQUIRE(store.undo());
    CHECK(store.doc().projectKey == key('C', 0, Mode::Ionian));
    CHECK_FALSE(store.undo());
    REQUIRE(store.redo());
    CHECK(store.doc().projectKey == key('G', 0, Mode::Mixolydian));
}

TEST_CASE("setProjectKey: the same key pushes no undo entry") {
    DocumentStore store;
    store.setProjectKey(key('C', 0, Mode::Ionian));
    CHECK_FALSE(store.undo());
    store.setProjectKey(key('D', 0, Mode::Dorian));
    store.setProjectKey(key('D', 0, Mode::Dorian));
    REQUIRE(store.undo());
    CHECK(store.doc().projectKey == key('C', 0, Mode::Ionian));
    CHECK_FALSE(store.undo());
}

// --- Live state: browsed key and toggles (never undoable) --------------------------------

TEST_CASE("setBrowsedKey: sets the browsed key only and never pushes history") {
    DocumentStore store;
    store.setBrowsedKey(key('E', -1, Mode::Aeolian));
    CHECK(store.doc().browsedKey == key('E', -1, Mode::Aeolian));
    CHECK(store.doc().projectKey == key('C', 0, Mode::Ionian));
    CHECK_FALSE(store.undo());
    store.setBrowsedKey(key('F', +1, Mode::Locrian));
    CHECK(store.doc().browsedKey == key('F', +1, Mode::Locrian));
    CHECK_FALSE(store.undo());
    CHECK_FALSE(store.redo());
}

TEST_CASE("setProgressionLoop / setFollowHost: toggle independently and never push history") {
    DocumentStore store;
    store.setProgressionLoop(false);
    CHECK(store.doc().progressionLoop == false);
    CHECK(store.doc().followHost == true);
    CHECK_FALSE(store.undo());
    store.setFollowHost(false);
    CHECK(store.doc().followHost == false);
    CHECK(store.doc().progressionLoop == false);
    CHECK_FALSE(store.undo());
    store.setProgressionLoop(true);
    store.setFollowHost(true);
    CHECK(store.doc().progressionLoop == true);
    CHECK(store.doc().followHost == true);
    CHECK_FALSE(store.undo());
    CHECK_FALSE(store.redo());
}

TEST_CASE("undo: browsing between two appends keeps the browsed key while the append is undone") {
    DocumentStore store;
    store.appendChord(kC);
    store.setBrowsedKey(key('E', -1, Mode::Aeolian));
    store.appendChord(kF);
    store.setBrowsedKey(key('F', +1, Mode::Locrian));

    REQUIRE(store.undo());
    CHECK(idsOf(store) == std::vector<int>{1});
    CHECK(store.doc().browsedKey == key('F', +1, Mode::Locrian));
    REQUIRE(store.undo());
    CHECK(store.doc().events.empty());
    CHECK(store.doc().browsedKey == key('F', +1, Mode::Locrian));
    CHECK_FALSE(store.undo());

    REQUIRE(store.redo());
    CHECK(idsOf(store) == std::vector<int>{1});
    CHECK(store.doc().browsedKey == key('F', +1, Mode::Locrian));
    REQUIRE(store.redo());
    CHECK(idsOf(store) == std::vector<int>{1, 2});
    CHECK(store.doc().browsedKey == key('F', +1, Mode::Locrian));
}

TEST_CASE("undo: toggling between two appends keeps the toggles while the append is undone") {
    DocumentStore store;
    store.appendChord(kC);
    store.setProgressionLoop(false);
    store.appendChord(kF);
    store.setFollowHost(false);

    REQUIRE(store.undo());
    CHECK(idsOf(store) == std::vector<int>{1});
    CHECK(store.doc().progressionLoop == false);
    CHECK(store.doc().followHost == false);
    REQUIRE(store.undo());
    CHECK(store.doc().events.empty());
    CHECK(store.doc().progressionLoop == false);
    CHECK(store.doc().followHost == false);
    CHECK_FALSE(store.undo());

    store.setProgressionLoop(true);  // live changes while undone...
    REQUIRE(store.redo());           // ...are kept across redo too
    CHECK(idsOf(store) == std::vector<int>{1});
    CHECK(store.doc().progressionLoop == true);
    CHECK(store.doc().followHost == false);
}

TEST_CASE("undo / redo: the project key is restored while the live state is kept") {
    DocumentStore store;
    store.setProjectKey(key('D', 0, Mode::Dorian));
    store.setBrowsedKey(key('A', 0, Mode::Aeolian));
    store.setProgressionLoop(false);
    REQUIRE(store.undo());
    CHECK(store.doc().projectKey == key('C', 0, Mode::Ionian));
    CHECK(store.doc().browsedKey == key('A', 0, Mode::Aeolian));
    CHECK(store.doc().progressionLoop == false);
    REQUIRE(store.redo());
    CHECK(store.doc().projectKey == key('D', 0, Mode::Dorian));
    CHECK(store.doc().browsedKey == key('A', 0, Mode::Aeolian));
    CHECK(store.doc().progressionLoop == false);
}

TEST_CASE("undo / redo: the redo stack survives a browse and a toggle") {
    DocumentStore store;
    store.appendChord(kC);
    store.appendChord(kF);
    REQUIRE(store.undo());
    store.setBrowsedKey(key('B', -1, Mode::Lydian));
    store.setProgressionLoop(false);
    store.setFollowHost(false);
    REQUIRE(store.redo());
    CHECK(idsOf(store) == std::vector<int>{1, 2});
    CHECK(store.doc().browsedKey == key('B', -1, Mode::Lydian));
    CHECK(store.doc().progressionLoop == false);
    CHECK(store.doc().followHost == false);
    CHECK_FALSE(store.redo());
}

// --- undo / redo -----------------------------------------------------------------------

TEST_CASE("undo / redo: return false on a fresh store") {
    DocumentStore store;
    CHECK_FALSE(store.undo());
    CHECK_FALSE(store.redo());
    CHECK(store.doc() == Document{});
}

TEST_CASE("undo / redo: every undoable operation round-trips") {
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
    checkRoundTrip(store, [](DocumentStore& s) { s.setInversion(3, 2); });
    checkRoundTrip(store, [](DocumentStore& s) { s.setInversion(1, 99); });
    checkRoundTrip(store, [](DocumentStore& s) { s.setOctave(1, -1); });
    checkRoundTrip(store, [](DocumentStore& s) { s.setOctave(3, 7); });
    checkRoundTrip(store, [](DocumentStore& s) { s.resetVoicing(1); });
    checkRoundTrip(store, [](DocumentStore& s) { s.setInversion(1, 0); });  // the flag alone changes
    checkRoundTrip(store, [](DocumentStore& s) { s.setAutoVoiceLead(true); });
    checkRoundTrip(store, [](DocumentStore& s) { s.setAutoVoiceLead(false); });
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

TEST_CASE("undo / redo: a new undoable mutation after undo clears the redo stack") {
    DocumentStore store;
    store.appendChord(kC);
    store.appendChord(kF);
    REQUIRE(store.undo());
    store.setProjectKey(key('D', 0, Mode::Dorian));
    CHECK_FALSE(store.redo());
    CHECK(idsOf(store) == std::vector<int>{1});
    CHECK(store.doc().projectKey == key('D', 0, Mode::Dorian));
}

TEST_CASE("undo / redo: a no-op mutation after undo keeps the redo stack") {
    DocumentStore store;
    store.appendChord(kC);
    store.appendChord(kF);
    REQUIRE(store.undo());
    store.setLength(99, 1.0);                     // unknown id
    store.setProjectKey(key('C', 0, Mode::Ionian));  // same value
    store.moveChord(1, 0);                        // already there
    REQUIRE(store.redo());
    CHECK(idsOf(store) == std::vector<int>{1, 2});
}

TEST_CASE("undo: history is capped at kMaxUndoDepth, dropping the oldest snapshots") {
    DocumentStore store;
    for (int i = 0; i < 250; ++i) {
        store.appendChord(kC);
    }
    REQUIRE(store.doc().events.size() == 250);
    int undos = 0;
    while (store.undo()) {
        ++undos;
    }
    CHECK(undos == 200);
    REQUIRE(store.doc().events.size() == 50);
    CHECK(store.doc().events.front().id == 1);  // the oldest 50 appends can no longer be undone
    CHECK(store.doc().events.back().id == 50);
    int redos = 0;
    while (store.redo()) {
        ++redos;
    }
    CHECK(redos == 200);
    CHECK(store.doc().events.size() == 250);
}

// --- replace ---------------------------------------------------------------------------

TEST_CASE("replace: swaps in a document, resets the id counter and clears the whole history") {
    DocumentStore store = threeChords();
    REQUIRE(store.undo());  // one undo entry left, one redo entry

    Document doc;
    doc.projectKey = key('D', 0, Mode::Dorian);
    doc.browsedKey = key('G', 0, Mode::Ionian);
    doc.progressionLoop = false;
    doc.autoVoiceLead = true;
    doc.events.push_back(ChordEvent{7, kC, 2.0, 90, 2, -1, true});
    doc.events.push_back(ChordEvent{5, kF, 2.0, 90, 0, 0, false});
    store.replace(doc, 8);

    CHECK(store.doc() == doc);  // live state included: replace is a load, not an edit
    CHECK(store.doc().events[0].inversion == 2);
    CHECK(store.doc().events[0].octave == -1);
    CHECK(store.doc().events[0].manualVoicing == true);
    CHECK(store.doc().events[1].manualVoicing == false);
    CHECK(store.doc().autoVoiceLead == true);
    CHECK_FALSE(store.undo());
    CHECK_FALSE(store.redo());
    CHECK(store.appendChord(kF) == 8);
    CHECK(store.findIndex(7) == 0);
}

// --- Property test ---------------------------------------------------------------------

// The engine is seeded, but what `std::uniform_int_distribution` / `std::uniform_real_distribution` make
// of it is implementation-defined, so the exact trace differs between standard libraries. The test
// therefore checks only invariants that must hold for every trace, never specific values.
TEST_CASE("DocumentStore: 200 random operations undo to the initial document and redo back") {
    std::mt19937 rng(20261010u);
    auto randInt = [&rng](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    auto randReal = [&rng](double lo, double hi) { return std::uniform_real_distribution<double>(lo, hi)(rng); };
    const auto& types = allChordTypes();

    DocumentStore store;
    const Document initial = store.doc();

    int appends = 0;
    int rejected = 0;
    for (int i = 0; i < 200; ++i) {
        // Every argument is drawn here, in this fixed order and on every iteration regardless of the
        // operation chosen, so the trace cannot depend on the (unspecified) evaluation order of function
        // arguments or on the current document.
        const int kind = randInt(0, 15);
        const int root = randInt(0, 11);
        const std::size_t typeIndex = static_cast<std::size_t>(randInt(0, static_cast<int>(types.size()) - 1));
        const int lengthKind = randInt(0, 11);  // 10 and 11 inject non-finite lengths
        const double finiteLength = randReal(-2.0, 100.0);
        const int velocity = randInt(-10, 200);
        const int index = randInt(-3, 12);
        const bool pickExisting = randInt(0, 4) != 0;
        const int pickPosition = randInt(0, 1000);
        const int rawId = randInt(-2, 400);
        const char letter = static_cast<char>('A' + randInt(0, 6));
        const int alter = randInt(-1, 1);
        const Mode mode = static_cast<Mode>(randInt(0, kNumModes - 1));
        const bool flag = randInt(0, 1) == 1;
        const int inversion = randInt(-2, 6);
        const int octave = randInt(-3, 3);

        const Chord chord{root, &types[typeIndex]};
        const double length = lengthKind == 10 ? kNaN : lengthKind == 11 ? kInf : finiteLength;
        const Key randomKey{Tonic{letter, alter}, mode};
        const auto& events = store.doc().events;
        const int id = (pickExisting && !events.empty())
                           ? events[static_cast<std::size_t>(pickPosition) % events.size()].id
                           : rawId;

        switch (kind) {
            case 0:
            case 1:
            case 2:
                if (store.appendChord(chord, length, velocity) == -1) {
                    ++rejected;
                } else {
                    ++appends;
                }
                break;
            case 3:
                store.moveChord(id, index);
                break;
            case 4:
                store.setLength(id, length);
                break;
            case 5:
                store.setVelocity(id, velocity);
                break;
            case 6:
                store.removeChord(id);
                break;
            case 7:
                store.setProjectKey(randomKey);
                break;
            case 8:
                store.setBrowsedKey(randomKey);
                break;
            case 9:
                store.setProgressionLoop(flag);
                break;
            case 10:
                store.setFollowHost(flag);
                break;
            case 11:
                store.setInversion(id, inversion);
                break;
            case 12:
                store.setOctave(id, octave);
                break;
            case 13:
                store.setAutoVoiceLead(flag);
                break;
            case 14:
                store.resetVoicing(id);
                break;
            default:
                store.undo();  // a neutral pair: exercises undo/redo mid-history
                store.redo();
                break;
        }
    }
    REQUIRE(appends > 20);  // the seed exercised the sequence
    CHECK(rejected >= 0);
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
            CHECK(e.inversion >= 0);
            CHECK(e.inversion < inversionCount(e.chord));
            CHECK(e.octave >= kMinVoicingOctave);
            CHECK(e.octave <= kMaxVoicingOctave);
            if (!e.manualVoicing) {  // what the store's own operations maintain
                CHECK(e.inversion == 0);
                CHECK(e.octave == 0);
            }
        }
    }

    // At most 200 snapshots were pushed, so the cap never truncates this history.
    int undos = 0;
    while (store.undo()) {
        ++undos;
    }
    CHECK(undos > 0);
    CHECK(undos <= 200);
    CHECK(sameUndoableState(store.doc(), initial));  // progression, project key and auto flag fully unwound
    CHECK(sameLiveState(store.doc(), final));         // browsed key and toggles are live: kept

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
