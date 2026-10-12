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
#include <string>
#include <string_view>
#include <utility>
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

/// The undoable part of a document: the progression, its sections, the project key and the auto
/// voice-lead flag.
bool sameUndoableState(const Document& a, const Document& b) {
    return a.events == b.events && a.sections == b.sections && a.projectKey == b.projectKey &&
           a.autoVoiceLead == b.autoVoiceLead;
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
    CHECK(doc.sections.empty());
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
    y = x;
    y.sections.push_back(Section{1, "A", 0, 0});
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

TEST_CASE("setSelectedEvent: live state: selects an event, kNoEventId clears, an unknown id is a no-op, no history") {
    DocumentStore store = threeChords();
    CHECK(store.selectedEventId() == kNoEventId);
    store.setSelectedEvent(2);
    CHECK(store.selectedEventId() == 2);
    store.setSelectedEvent(99);  // nothing to select: the selection stays
    CHECK(store.selectedEventId() == 2);
    store.setSelectedEvent(0);
    CHECK(store.selectedEventId() == 2);
    store.setSelectedEvent(3);
    CHECK(store.selectedEventId() == 3);
    store.setSelectedEvent(kNoEventId);
    CHECK(store.selectedEventId() == kNoEventId);
    store.setSelectedEvent(1);
    CHECK(store.selectedEventId() == 1);

    // Live state: the three appends are the whole history, and undoing them does not restore a selection.
    REQUIRE(store.undo());
    CHECK(store.selectedEventId() == 1);
    REQUIRE(store.undo());
    REQUIRE(store.undo());
    CHECK_FALSE(store.undo());
    CHECK(store.selectedEventId() == kNoEventId);  // event 1 is gone
    REQUIRE(store.redo());
    CHECK(store.selectedEventId() == kNoEventId);  // and does not come back selected
}

TEST_CASE("selectedEventId: kept across edits, undo and redo while the event exists; cleared the moment it goes") {
    DocumentStore store = threeChords();
    store.setSelectedEvent(2);
    store.setLength(2, 1.0);
    store.moveChord(2, 0);
    store.setProjectKey(key('G', 0, Mode::Ionian));
    CHECK(store.selectedEventId() == 2);
    REQUIRE(store.undo());
    REQUIRE(store.undo());
    CHECK(store.selectedEventId() == 2);
    REQUIRE(store.redo());
    CHECK(store.selectedEventId() == 2);

    store.removeChord(2);
    CHECK(store.selectedEventId() == kNoEventId);
    REQUIRE(store.undo());  // the chord is back, the selection is not
    CHECK(store.findIndex(2) >= 0);
    CHECK(store.selectedEventId() == kNoEventId);

    store.setSelectedEvent(3);
    store.removeChord(1);  // another chord: the selection survives
    CHECK(store.selectedEventId() == 3);
    REQUIRE(store.undo());
    REQUIRE(store.undo());  // the removal of 1, then the move: event 3 is there throughout
    CHECK(store.selectedEventId() == 3);
}

TEST_CASE("replace: clears the selection, even when the new document has an event with the same id") {
    DocumentStore store = threeChords();
    store.setSelectedEvent(2);
    Document doc;
    doc.events.push_back(ChordEvent{2, kC, 4.0, 100});
    store.replace(doc, 3);
    CHECK(store.selectedEventId() == kNoEventId);
    store.setSelectedEvent(2);
    CHECK(store.selectedEventId() == 2);
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
        const int kind = randInt(0, 20);
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
        const int colour = randInt(-2, 10);
        const int nameNumber = randInt(0, 99);

        const Chord chord{root, &types[typeIndex]};
        const double length = lengthKind == 10 ? kNaN : lengthKind == 11 ? kInf : finiteLength;
        const Key randomKey{Tonic{letter, alter}, mode};
        const auto& events = store.doc().events;
        const auto& sections = store.doc().sections;
        const int id = (pickExisting && !events.empty())
                           ? events[static_cast<std::size_t>(pickPosition) % events.size()].id
                           : rawId;
        const int sectionId = (pickExisting && !sections.empty())
                                  ? sections[static_cast<std::size_t>(pickPosition) % sections.size()].id
                                  : rawId;
        const std::string name = nameNumber % 2 == 0 ? std::string() : "S" + std::to_string(nameNumber);

        switch (kind) {
            case 0:
            case 1:
            case 2:
            case 15:
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
            case 16:
                store.addSection(index, name);
                break;
            case 17:
                store.renameSection(sectionId, name);
                break;
            case 18:
                store.setSectionColour(sectionId, colour);
                break;
            case 19:
                store.removeSection(sectionId);
                break;
            case 20:
                store.moveSectionStart(sectionId, index);
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
    CHECK(sectionsWellFormed(final));

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
    CHECK(sameUndoableState(store.doc(), initial));  // progression, sections, key and auto flag unwound
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

// --- Sections ----------------------------------------------------------------------------

namespace {

using SectionStarts = std::vector<std::pair<int, int>>;

/// (id, firstEventIndex) of every section, in section order.
SectionStarts sectionStartsOf(const DocumentStore& store) {
    SectionStarts starts;
    for (const Section& s : store.doc().sections) {
        starts.emplace_back(s.id, s.firstEventIndex);
    }
    return starts;
}

/// Id of the chord each section starts on, in section order.
std::vector<int> sectionChordIdsOf(const DocumentStore& store) {
    std::vector<int> ids;
    for (const Section& s : store.doc().sections) {
        ids.push_back(store.doc().events[static_cast<std::size_t>(s.firstEventIndex)].id);
    }
    return ids;
}

/// A store holding C (1), F (2), G7 (3), C (4), four beats each, with "A" (id 1) at 0 and "B" (id 2) at 2.
DocumentStore twoSections() {
    DocumentStore store = threeChords();
    store.appendChord(kC, 4.0, 100);
    store.addSection(0, "A");
    store.addSection(2, "B");
    return store;
}

/// The section invariants: sorted strictly by `firstEventIndex` (so unique), every index a position of
/// the progression, the first section (if any) at 0, every colour in 0..kNumSectionColours-1, ids >= 1
/// and distinct.
void checkSectionInvariants(const Document& doc) {
    if (!doc.sections.empty()) {
        CHECK(doc.sections.front().firstEventIndex == 0);
    }
    int previousIndex = -1;
    std::vector<int> seenIds;
    for (const Section& s : doc.sections) {
        CAPTURE(s.id);
        CHECK(s.firstEventIndex > previousIndex);
        CHECK(s.firstEventIndex >= 0);
        CHECK(static_cast<std::size_t>(s.firstEventIndex) < doc.events.size());
        CHECK(s.colourIndex >= 0);
        CHECK(s.colourIndex < kNumSectionColours);
        CHECK(s.id >= 1);
        for (const int seen : seenIds) {
            CHECK(seen != s.id);
        }
        seenIds.push_back(s.id);
        previousIndex = s.firstEventIndex;
    }
}

}  // namespace

TEST_CASE("Section: equality compares every field; kNumSectionColours and kNoSectionId are the plan values") {
    const Section a{1, "A", 0, 0};
    CHECK(a == Section{1, "A", 0, 0});
    CHECK(a != Section{2, "A", 0, 0});
    CHECK(a != Section{1, "B", 0, 0});
    CHECK(a != Section{1, "A", 1, 0});
    CHECK(a != Section{1, "A", 0, 1});
    CHECK(kNumSectionColours == 8);
    CHECK(kNoSectionId == -1);
}

// --- addSection ----------------------------------------------------------------------------

TEST_CASE("addSection: inserts in index order, returns ids 1, 2, 3 and defaults the name and colour") {
    DocumentStore store = threeChords();
    CHECK(store.addSection(0, "Verse") == 1);
    CHECK(store.addSection(2, "Chorus") == 2);
    CHECK(store.addSection(1) == 3);
    REQUIRE(store.doc().sections.size() == 3);
    CHECK(store.doc().sections[0] == Section{1, "Verse", 0, 0});
    CHECK(store.doc().sections[1] == Section{3, "Section 3", 2, 1});
    CHECK(store.doc().sections[2] == Section{2, "Chorus", 1, 2});
    CHECK(store.doc().events.size() == 3);  // chords untouched
}

TEST_CASE("addSection: an empty name takes the default; the default colour cycles through the eight by id") {
    DocumentStore store;
    for (int i = 0; i < 9; ++i) {
        REQUIRE(store.appendChord(kC) == i + 1);
    }
    for (int i = 0; i < 9; ++i) {
        REQUIRE(store.addSection(i, "") == i + 1);
    }
    for (int i = 0; i < 9; ++i) {
        CAPTURE(i);
        CHECK(store.doc().sections[static_cast<std::size_t>(i)].name == "Section " + std::to_string(i + 1));
        CHECK(store.doc().sections[static_cast<std::size_t>(i)].colourIndex == i % kNumSectionColours);
    }
}

TEST_CASE("addSection: an index out of range, already used, or on an empty document is refused atomically") {
    DocumentStore empty;
    CHECK(empty.addSection(0, "A") == kNoSectionId);
    CHECK(empty.doc().sections.empty());
    CHECK_FALSE(empty.undo());
    CHECK(empty.nextSectionId() == 1);

    DocumentStore store = threeChords();
    REQUIRE(store.addSection(0, "A") == 1);
    store.renameSection(1, "B");
    REQUIRE(store.undo());  // leaves a pending redo of the rename
    const Document before = store.doc();

    CHECK(store.addSection(-1, "X") == kNoSectionId);
    CHECK(store.addSection(3, "X") == kNoSectionId);  // one past the last chord
    CHECK(store.addSection(99, "X") == kNoSectionId);
    CHECK(store.addSection(0, "X") == kNoSectionId);  // index 0 already starts a section
    CHECK(store.doc() == before);
    CHECK(store.nextSectionId() == 2);  // no id consumed

    REQUIRE(store.redo());  // the pending redo survived
    CHECK(store.doc().sections[0].name == "B");
}

TEST_CASE("addSection: ids never repeat across remove, undo and redo") {
    DocumentStore store = threeChords();
    CHECK(store.addSection(0) == 1);
    store.removeSection(1);
    CHECK(store.addSection(0) == 2);
    REQUIRE(store.undo());
    CHECK(store.doc().sections.empty());
    CHECK(store.nextSectionId() == 3);  // not undoable state
    CHECK(store.addSection(0) == 3);
    REQUIRE(store.undo());
    REQUIRE(store.redo());
    CHECK(store.addSection(1) == 4);
    CHECK(sectionStartsOf(store) == SectionStarts{{3, 0}, {4, 1}});
}

TEST_CASE("addSection: the first section added at an inner chord gets a leading section before it, in one step") {
    DocumentStore store = threeChords();
    store.appendChord(kC);  // four chords
    CHECK(store.addSection(2, "Chorus") == 2);
    REQUIRE(store.doc().sections.size() == 2);
    CHECK(store.doc().sections[0] == Section{1, "Section 1", 0, 0});  // the chords before it
    CHECK(store.doc().sections[1] == Section{2, "Chorus", 1, 2});
    CHECK(store.nextSectionId() == 3);
    REQUIRE(store.undo());  // one history entry for both
    CHECK(store.doc().sections.empty());
    REQUIRE(store.redo());
    CHECK(sectionStartsOf(store) == SectionStarts{{1, 0}, {2, 2}});
    CHECK(store.addSection(1, "Pre") == 3);  // with a section present, an inner add is just that one
    CHECK(sectionStartsOf(store) == SectionStarts{{1, 0}, {3, 1}, {2, 2}});
}

TEST_CASE("addSection: a two-section add needs two ids and is refused when only one is left") {
    Document doc;
    doc.events.push_back(ChordEvent{1, kC, 4.0, 100});
    doc.events.push_back(ChordEvent{2, kF, 4.0, 100});
    DocumentStore store;
    store.replace(doc, 3, INT_MAX - 1);
    CHECK(store.addSection(1, "X") == kNoSectionId);  // would need INT_MAX - 1 and INT_MAX
    CHECK(store.doc().sections.empty());
    CHECK(store.nextSectionId() == INT_MAX - 1);
    CHECK_FALSE(store.undo());
    CHECK(store.addSection(0, "A") == INT_MAX - 1);  // one id: fine
    CHECK(store.addSection(1, "X") == kNoSectionId);  // the counter is exhausted
    CHECK(store.doc().sections.size() == 1);
}

TEST_CASE("appendChord: keeps the sections where they are") {
    DocumentStore store = twoSections();
    store.appendChord(kF);
    CHECK(sectionStartsOf(store) == SectionStarts{{1, 0}, {2, 2}});
    CHECK(store.doc().sectionIndexOf(4) == 1);  // the new chord joins the last section
}

// --- Section names ----------------------------------------------------------------------------

TEST_CASE("kMaxSectionNameBytes: plan value") {
    CHECK(kMaxSectionNameBytes == 64);
}

TEST_CASE("addSection / renameSection: names longer than kMaxSectionNameBytes are cut at a UTF-8 boundary") {
    DocumentStore store = threeChords();
    const std::string sixtyFour(64, 'x');
    REQUIRE(store.addSection(0, std::string(100, 'x')) == 1);
    CHECK(store.doc().sections[0].name == sixtyFour);
    REQUIRE(store.addSection(1, sixtyFour) == 2);  // exactly the bound: kept whole
    CHECK(store.doc().sections[1].name == sixtyFour);

    // A two-byte character straddling the bound is dropped whole, never split.
    store.renameSection(1, std::string(63, 'a') + "\xC3\xA9");  // 63 a + e-acute = 65 bytes
    CHECK(store.doc().sections[0].name == std::string(63, 'a'));
    store.renameSection(1, std::string(62, 'a') + "\xC3\xA9");  // 64 bytes: kept
    CHECK(store.doc().sections[0].name == std::string(62, 'a') + "\xC3\xA9");
    // A four-byte character likewise.
    store.renameSection(1, std::string(61, 'a') + "\xF0\x9F\x8E\xB5");  // 65 bytes
    CHECK(store.doc().sections[0].name == std::string(61, 'a'));
    store.renameSection(1, std::string(60, 'a') + "\xF0\x9F\x8E\xB5");  // 64 bytes
    CHECK(store.doc().sections[0].name.size() == 64);
}

TEST_CASE("renameSection: a name that cuts down to the current one pushes no undo entry") {
    DocumentStore store = threeChords();
    REQUIRE(store.addSection(0, std::string(64, 'x')) == 1);
    REQUIRE(store.undo());
    REQUIRE(store.redo());
    store.renameSection(1, std::string(300, 'x'));  // cuts to the current name
    CHECK(store.doc().sections[0].name == std::string(64, 'x'));
    REQUIRE(store.undo());  // the add, not a rename
    CHECK(store.doc().sections.empty());
}

TEST_CASE("sectionsWellFormed: a name longer than kMaxSectionNameBytes breaks the invariants") {
    Document doc;
    doc.events.push_back(ChordEvent{1, kC, 4.0, 100});
    doc.sections.push_back(Section{1, std::string(64, 'n'), 0, 0});
    CHECK(sectionsWellFormed(doc));
    doc.sections[0].name.push_back('n');
    CHECK_FALSE(sectionsWellFormed(doc));
}

// --- renameSection / setSectionColour / removeSection / moveSectionStart -------------------

TEST_CASE("renameSection: sets the name; unknown id and the same name push no undo entry") {
    DocumentStore store = twoSections();
    store.renameSection(2, "Chorus");
    CHECK(store.doc().sections[1].name == "Chorus");
    CHECK(store.doc().sections[0].name == "A");
    const Document after = store.doc();
    store.renameSection(2, "Chorus");  // same name
    store.renameSection(99, "X");      // unknown
    store.renameSection(kNoSectionId, "X");
    CHECK(store.doc() == after);
    REQUIRE(store.undo());  // the real rename
    CHECK(store.doc().sections[1].name == "B");
    REQUIRE(store.undo());  // pops the add of B, not a phantom rename
    CHECK(store.doc().sections.size() == 1);
}

TEST_CASE("setSectionColour: clamps to 0..7; unknown id and an unchanged colour push no undo entry") {
    DocumentStore store = twoSections();
    store.setSectionColour(1, 5);
    CHECK(store.doc().sections[0].colourIndex == 5);
    store.setSectionColour(1, 99);
    CHECK(store.doc().sections[0].colourIndex == 7);
    store.setSectionColour(1, -3);
    CHECK(store.doc().sections[0].colourIndex == 0);
    CHECK(store.doc().sections[1].colourIndex == 1);  // the other section is untouched
    const Document after = store.doc();
    store.setSectionColour(1, 0);
    store.setSectionColour(1, -1);  // clamps to 0, already 0
    store.setSectionColour(99, 3);
    CHECK(store.doc() == after);
    REQUIRE(store.undo());
    CHECK(store.doc().sections[0].colourIndex == 7);
    REQUIRE(store.undo());
    CHECK(store.doc().sections[0].colourIndex == 5);
    REQUIRE(store.undo());
    CHECK(store.doc().sections[0].colourIndex == 0);
    REQUIRE(store.undo());  // pops the add of B, not a phantom colour change
    CHECK(store.doc().sections.size() == 1);
}

TEST_CASE("removeSection: the chords join the neighbouring section; removing the first hands index 0 to the next") {
    DocumentStore store = twoSections();  // A at 0, B at 2
    store.removeSection(1);
    REQUIRE(store.doc().sections.size() == 1);
    CHECK(store.doc().sections[0] == Section{2, "B", 1, 0});  // B now starts the sequence
    CHECK(store.doc().events.size() == 4);  // chords untouched
    CHECK(store.doc().sectionIndexOf(0) == 0);
    const Document after = store.doc();
    store.removeSection(1);  // already gone
    store.removeSection(99);
    CHECK(store.doc() == after);
    REQUIRE(store.undo());  // the real remove
    CHECK(sectionStartsOf(store) == SectionStarts{{1, 0}, {2, 2}});
    store.removeSection(2);  // a later section: the one before it absorbs its chords
    CHECK(sectionStartsOf(store) == SectionStarts{{1, 0}});
    store.removeSection(1);  // the only section
    CHECK(store.doc().sections.empty());
    REQUIRE(store.undo());
    REQUIRE(store.undo());
    CHECK(sectionStartsOf(store) == SectionStarts{{1, 0}, {2, 2}});
    REQUIRE(store.undo());  // pops the add of B, not a phantom remove
    CHECK(store.doc().sections.size() == 1);
}

TEST_CASE("moveSectionStart: moves the start and keeps the sections sorted by index") {
    DocumentStore store = twoSections();  // A at 0, B at 2
    store.appendChord(kC);                // five chords
    store.moveSectionStart(2, 1);
    CHECK(sectionStartsOf(store) == SectionStarts{{1, 0}, {2, 1}});
    store.moveSectionStart(2, 3);
    CHECK(sectionStartsOf(store) == SectionStarts{{1, 0}, {2, 3}});
    REQUIRE(store.addSection(4, "C") == 3);
    store.moveSectionStart(3, 2);  // C jumps before B
    CHECK(sectionStartsOf(store) == SectionStarts{{1, 0}, {3, 2}, {2, 3}});
    CHECK(store.doc().sections[1].name == "C");
    CHECK(store.doc().sectionIndexOf(1) == 0);
    CHECK(store.doc().sectionIndexOf(2) == 1);
    CHECK(store.doc().sectionIndexOf(4) == 2);
}

TEST_CASE("moveSectionStart: the first section is pinned; a collision, an index out of range, an unknown id and the same index are no-ops") {
    DocumentStore store = twoSections();
    const Document before = store.doc();
    store.moveSectionStart(1, 1);  // the first section stays at 0
    store.moveSectionStart(1, 3);
    store.moveSectionStart(2, 0);  // the first section is there
    store.moveSectionStart(2, -1);
    store.moveSectionStart(2, 4);  // one past the last chord
    store.moveSectionStart(99, 1);
    store.moveSectionStart(2, 2);  // already there
    CHECK(store.doc() == before);
    REQUIRE(store.undo());  // pops the add of B, not a phantom move
    CHECK(store.doc().sections.size() == 1);
}

TEST_CASE("findSectionIndex: position in section order, -1 when absent") {
    DocumentStore store = twoSections();
    CHECK(store.findSectionIndex(1) == 0);
    CHECK(store.findSectionIndex(2) == 1);
    CHECK(store.findSectionIndex(3) == -1);
    CHECK(store.findSectionIndex(0) == -1);
    CHECK(store.findSectionIndex(kNoSectionId) == -1);
    store.addSection(1, "C");  // id 3, between A and B
    CHECK(store.findSectionIndex(3) == 1);
    CHECK(store.findSectionIndex(2) == 2);
    store.removeSection(1);  // C takes the start
    CHECK(store.findSectionIndex(1) == -1);
    CHECK(store.findSectionIndex(3) == 0);
    CHECK(store.findSectionIndex(2) == 1);
}

// --- sectionIndexOf --------------------------------------------------------------------------

TEST_CASE("Document::sectionIndexOf: -1 without sections; else the section containing the event") {
    DocumentStore store = threeChords();
    store.appendChord(kC);
    for (int i = 0; i < 4; ++i) {
        CAPTURE(i);
        CHECK(store.doc().sectionIndexOf(i) == -1);
    }
    store.addSection(0, "A");
    store.addSection(3, "B");
    CHECK(store.doc().sectionIndexOf(0) == 0);
    CHECK(store.doc().sectionIndexOf(1) == 0);
    CHECK(store.doc().sectionIndexOf(2) == 0);
    CHECK(store.doc().sectionIndexOf(3) == 1);
    store.addSection(1, "Pre");
    CHECK(store.doc().sectionIndexOf(0) == 0);
    CHECK(store.doc().sectionIndexOf(1) == 1);
    CHECK(store.doc().sectionIndexOf(2) == 1);
    CHECK(store.doc().sectionIndexOf(3) == 2);
}

// --- Re-indexing on chord edits --------------------------------------------------------------

TEST_CASE("removeChord: a section after the removed chord moves up one index and keeps its start chord") {
    DocumentStore store = threeChords();
    store.appendChord(kC);     // ids 1 2 3 4
    store.addSection(2, "B");  // starts on chord 3; "Section 1" (id 1) is created at 0 before it
    store.removeChord(2);
    CHECK(idsOf(store) == std::vector<int>{1, 3, 4});
    CHECK(sectionStartsOf(store) == SectionStarts{{1, 0}, {2, 1}});
    CHECK(sectionChordIdsOf(store) == std::vector<int>{1, 3});
    REQUIRE(store.undo());
    CHECK(sectionStartsOf(store) == SectionStarts{{1, 0}, {2, 2}});
}

TEST_CASE("removeChord: removing a section's start chord removes the section") {
    DocumentStore store = twoSections();  // A at 0 on chord 1, B at 2 on chord 3
    store.removeChord(3);
    CHECK(idsOf(store) == std::vector<int>{1, 2, 4});
    CHECK(sectionStartsOf(store) == SectionStarts{{1, 0}});
    REQUIRE(store.undo());
    CHECK(sectionStartsOf(store) == SectionStarts{{1, 0}, {2, 2}});
    REQUIRE(store.redo());
    CHECK(sectionStartsOf(store) == SectionStarts{{1, 0}});
}

TEST_CASE("removeChord: the first chord's section stays at 0 on the new first chord when other sections follow") {
    DocumentStore store = twoSections();  // A at 0 on chord 1, B at 2 on chord 3
    store.removeChord(1);
    CHECK(idsOf(store) == std::vector<int>{2, 3, 4});
    CHECK(sectionStartsOf(store) == SectionStarts{{1, 0}, {2, 1}});
    CHECK(sectionChordIdsOf(store) == std::vector<int>{2, 3});
    CHECK(store.doc().sections[0].name == "A");
    REQUIRE(store.undo());
    CHECK(sectionChordIdsOf(store) == std::vector<int>{1, 3});
}

TEST_CASE("removeChord: the first chord's section goes when the next chord already starts a section") {
    DocumentStore store = threeChords();  // chords 1 2 3
    store.addSection(0, "A");
    store.addSection(1, "B");
    store.removeChord(1);  // A has no chord left to inherit: B's chord is B's
    CHECK(idsOf(store) == std::vector<int>{2, 3});
    CHECK(sectionStartsOf(store) == SectionStarts{{2, 0}});
    CHECK(store.doc().sections[0].name == "B");
    REQUIRE(store.undo());
    CHECK(sectionStartsOf(store) == SectionStarts{{1, 0}, {2, 1}});
}

TEST_CASE("removeChord: a lone section at 0 survives on the new first chord until no chord is left") {
    DocumentStore store = threeChords();
    store.addSection(0, "A");
    store.removeChord(1);
    CHECK(idsOf(store) == std::vector<int>{2, 3});
    CHECK(sectionStartsOf(store) == SectionStarts{{1, 0}});
    CHECK(store.doc().sections[0].name == "A");
    store.removeChord(2);
    CHECK(sectionStartsOf(store) == SectionStarts{{1, 0}});
    store.removeChord(3);  // the last chord: nothing left to be a section of
    CHECK(store.doc().events.empty());
    CHECK(store.doc().sections.empty());
    REQUIRE(store.undo());
    CHECK(sectionStartsOf(store) == SectionStarts{{1, 0}});
}

TEST_CASE("removeChord: a chord inside a section leaves the sections alone; the last chord takes its section with it") {
    DocumentStore store = twoSections();
    store.removeChord(4);  // the last chord, inside B but not its start
    CHECK(sectionStartsOf(store) == SectionStarts{{1, 0}, {2, 2}});
    store.removeChord(2);  // inside A, not its start
    CHECK(sectionStartsOf(store) == SectionStarts{{1, 0}, {2, 1}});
    CHECK(sectionChordIdsOf(store) == std::vector<int>{1, 3});

    DocumentStore one;
    one.appendChord(kC);
    one.addSection(0, "A");
    one.removeChord(1);
    CHECK(one.doc().events.empty());
    CHECK(one.doc().sections.empty());
}

TEST_CASE("moveChord: a section's start chord moved to the front makes that section the first") {
    DocumentStore store = twoSections();  // chords 1 2 3 4; A on 1 at 0, B on 3 at 2
    store.moveChord(3, 0);
    CHECK(idsOf(store) == std::vector<int>{3, 1, 2, 4});
    CHECK(sectionStartsOf(store) == SectionStarts{{2, 0}, {1, 1}});  // A follows its chord
    CHECK(sectionChordIdsOf(store) == std::vector<int>{3, 1});
    CHECK(store.doc().sections[0].name == "B");
    REQUIRE(store.undo());
    CHECK(sectionStartsOf(store) == SectionStarts{{1, 0}, {2, 2}});
    REQUIRE(store.redo());
    CHECK(sectionStartsOf(store) == SectionStarts{{2, 0}, {1, 1}});
}

TEST_CASE("moveChord: moving a chord that starts no section across a boundary shifts the later section") {
    DocumentStore store = twoSections();  // chords 1 2 3 4; A on 1 at 0, B on 3 at 2
    store.moveChord(2, 3);
    CHECK(idsOf(store) == std::vector<int>{1, 3, 4, 2});
    CHECK(sectionStartsOf(store) == SectionStarts{{1, 0}, {2, 1}});
    CHECK(sectionChordIdsOf(store) == std::vector<int>{1, 3});
    store.moveChord(4, 0);  // to the front: the first section is pinned there and takes it in
    CHECK(idsOf(store) == std::vector<int>{4, 1, 3, 2});
    CHECK(sectionStartsOf(store) == SectionStarts{{1, 0}, {2, 2}});
    CHECK(sectionChordIdsOf(store) == std::vector<int>{4, 3});
    CHECK(store.doc().sectionIndexOf(0) == 0);
}

TEST_CASE("moveChord: the first section is pinned to the start when its own chord moves away") {
    DocumentStore store = twoSections();  // chords 1 2 3 4; A on 1 at 0, B on 3 at 2
    store.moveChord(1, 99);
    CHECK(idsOf(store) == std::vector<int>{2, 3, 4, 1});
    CHECK(sectionStartsOf(store) == SectionStarts{{1, 0}, {2, 1}});  // A keeps the start, B follows chord 3
    CHECK(sectionChordIdsOf(store) == std::vector<int>{2, 3});
    REQUIRE(store.undo());
    store.moveChord(1, 1);  // within the first section
    CHECK(idsOf(store) == std::vector<int>{2, 1, 3, 4});
    CHECK(sectionStartsOf(store) == SectionStarts{{1, 0}, {2, 2}});

    DocumentStore lone = threeChords();
    lone.addSection(0, "A");
    lone.moveChord(1, 2);
    CHECK(idsOf(lone) == std::vector<int>{2, 3, 1});
    CHECK(sectionStartsOf(lone) == SectionStarts{{1, 0}});
}

TEST_CASE("moveChord: when the first chord moves away and the next section's start chord slides into 0, that section is first") {
    DocumentStore store = threeChords();  // chords 1 2 3
    store.addSection(0, "A");
    store.addSection(1, "B");
    store.moveChord(1, 2);
    CHECK(idsOf(store) == std::vector<int>{2, 3, 1});
    CHECK(sectionStartsOf(store) == SectionStarts{{2, 0}, {1, 2}});  // B's chord is first now; A follows its chord
    CHECK(sectionChordIdsOf(store) == std::vector<int>{2, 1});
    REQUIRE(store.undo());
    CHECK(sectionStartsOf(store) == SectionStarts{{1, 0}, {2, 1}});
    REQUIRE(store.redo());
    CHECK(sectionStartsOf(store) == SectionStarts{{2, 0}, {1, 2}});
}

// --- Undo / redo of sections -------------------------------------------------------------------

TEST_CASE("undo / redo: every section operation round-trips") {
    DocumentStore store = threeChords();
    store.appendChord(kC);  // ids 1 2 3 4
    checkRoundTrip(store, [](DocumentStore& s) { s.addSection(0, "A"); });
    checkRoundTrip(store, [](DocumentStore& s) { s.addSection(2); });
    checkRoundTrip(store, [](DocumentStore& s) { s.renameSection(2, "B"); });
    checkRoundTrip(store, [](DocumentStore& s) { s.setSectionColour(2, 99); });
    checkRoundTrip(store, [](DocumentStore& s) { s.moveSectionStart(2, 3); });  // B onto chord 4
    checkRoundTrip(store, [](DocumentStore& s) { s.removeChord(2); });          // B re-indexed to 2
    checkRoundTrip(store, [](DocumentStore& s) { s.moveChord(4, 0); });         // B to 0, A to 1
    checkRoundTrip(store, [](DocumentStore& s) { s.removeSection(1); });
    checkRoundTrip(store, [](DocumentStore& s) { s.removeChord(4); });          // B's start chord: B stays first
    CHECK(idsOf(store) == std::vector<int>{1, 3});
    CHECK(sectionStartsOf(store) == SectionStarts{{2, 0}});
    checkRoundTrip(store, [](DocumentStore& s) { s.removeSection(2); });
    CHECK(store.doc().sections.empty());
}

TEST_CASE("undo / redo: sections are undoable; the live state and the section id counter are not") {
    DocumentStore store = threeChords();
    store.addSection(0, "A");
    store.setBrowsedKey(key('E', -1, Mode::Aeolian));
    store.setFollowHost(false);
    REQUIRE(store.undo());
    CHECK(store.doc().sections.empty());
    CHECK(store.doc().browsedKey == key('E', -1, Mode::Aeolian));
    CHECK(store.doc().followHost == false);
    CHECK(store.nextSectionId() == 2);
    REQUIRE(store.redo());
    CHECK(sectionStartsOf(store) == SectionStarts{{1, 0}});
    CHECK(store.doc().browsedKey == key('E', -1, Mode::Aeolian));
    CHECK(store.doc().followHost == false);
}

// --- replace -----------------------------------------------------------------------------------

TEST_CASE("replace: takes the sections along and restarts the section id counter past the largest id") {
    DocumentStore store = twoSections();
    Document doc;
    doc.events.push_back(ChordEvent{7, kC, 2.0, 90});
    doc.events.push_back(ChordEvent{8, kF, 2.0, 90});
    doc.sections.push_back(Section{5, "X", 3, 0});
    doc.sections.push_back(Section{3, "Y", 1, 1});  // sorted by index; ids need not be
    store.replace(doc, 9);
    CHECK(store.doc() == doc);
    CHECK_FALSE(store.undo());
    CHECK_FALSE(store.redo());
    CHECK(store.nextSectionId() == 6);
    CHECK(store.appendChord(kG) == 9);
    CHECK(store.addSection(2, "Z") == 6);

    DocumentStore explicitCounter;
    explicitCounter.replace(doc, 9, 42);
    CHECK(explicitCounter.nextSectionId() == 42);
    explicitCounter.appendChord(kG);
    CHECK(explicitCounter.addSection(2) == 42);

    DocumentStore none = twoSections();
    none.replace(Document{}, 1);
    CHECK(none.doc().sections.empty());
    CHECK(none.nextSectionId() == 1);
}

TEST_CASE("sectionsWellFormed: the section invariants replace asserts, with ids bounded so the derived counter cannot overflow") {
    Document doc;
    CHECK(sectionsWellFormed(doc));  // no chords, no sections
    doc.events.push_back(ChordEvent{1, kC, 4.0, 100});
    doc.events.push_back(ChordEvent{2, kF, 4.0, 100});
    CHECK(sectionsWellFormed(doc));  // chords, no sections
    doc.sections.push_back(Section{1, "A", 0, 0});
    CHECK(sectionsWellFormed(doc));

    Document ok = doc;
    ok.sections.push_back(Section{INT_MAX - 1, "B", 7, 1});  // the largest id allowed
    CHECK(sectionsWellFormed(ok));

    Document bad = doc;
    bad.sections[0].id = INT_MAX;  // max(id) + 1 would overflow
    CHECK_FALSE(sectionsWellFormed(bad));
    bad = doc;
    bad.sections[0].id = 0;
    CHECK_FALSE(sectionsWellFormed(bad));
    bad = doc;
    bad.sections[0].id = kNoSectionId;
    CHECK_FALSE(sectionsWellFormed(bad));
    bad = doc;
    bad.sections[0].firstEventIndex = 1;  // the first section must start at 0
    CHECK_FALSE(sectionsWellFormed(bad));
    bad = doc;
    bad.sections.push_back(Section{2, "B", 0, 2});  // one past the last chord
    CHECK_FALSE(sectionsWellFormed(bad));
    bad = doc;
    bad.sections.push_back(Section{2, "B", 0, 0});  // not strictly increasing
    CHECK_FALSE(sectionsWellFormed(bad));
    bad = doc;
    bad.sections.push_back(Section{1, "B", 0, 1});  // duplicate id
    CHECK_FALSE(sectionsWellFormed(bad));
    bad = doc;
    bad.sections[0].colourIndex = kNumSectionColours;
    CHECK_FALSE(sectionsWellFormed(bad));
    bad = doc;
    bad.sections[0].colourIndex = -1;
    CHECK_FALSE(sectionsWellFormed(bad));
    bad = doc;
    bad.events.clear();  // a section with no chord to start on
    CHECK_FALSE(sectionsWellFormed(bad));
}

TEST_CASE("replace: a section id of INT_MAX - 1 derives the counter INT_MAX, and the next addSection is refused unchanged") {
    Document doc;
    doc.events.push_back(ChordEvent{1, kC, 4.0, 100});
    doc.events.push_back(ChordEvent{2, kF, 4.0, 100});
    doc.sections.push_back(Section{INT_MAX - 1, "A", 0, 0});
    REQUIRE(sectionsWellFormed(doc));
    DocumentStore store;
    store.replace(doc, 3);
    CHECK(store.nextSectionId() == INT_MAX);

    CHECK(store.addSection(1, "B") == kNoSectionId);
    CHECK(store.doc() == doc);
    CHECK_FALSE(store.undo());
    CHECK(store.nextSectionId() == INT_MAX);

    store.renameSection(INT_MAX - 1, "Z");  // the existing section is still editable
    CHECK(store.doc().sections[0].name == "Z");
    REQUIRE(store.undo());
    CHECK(store.doc() == doc);
}

// --- Property test -----------------------------------------------------------------------------

// As with the chord-only random walk: the trace is implementation-defined, so only invariants are checked.
TEST_CASE("DocumentStore: 200 random chord and section operations keep the section invariants and undo to empty") {
    std::mt19937 rng(20261011u);
    auto randInt = [&rng](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    const Chord chords[3] = {kC, kF, kG};

    DocumentStore store;
    int addedSections = 0;
    int removedStartChords = 0;
    for (int i = 0; i < 200; ++i) {
        // Every argument is drawn here, in a fixed order, on every iteration (see the chord-only walk).
        const int kind = randInt(0, 15);
        const int chordIndex = randInt(0, 2);
        const int index = randInt(-1, 12);
        const int colour = randInt(-2, 10);
        const bool pickExisting = randInt(0, 4) != 0;
        const int pickPosition = randInt(0, 1000);
        const int rawId = randInt(-1, 50);
        const int nameNumber = randInt(0, 99);
        const int inversion = randInt(-2, 6);
        const int octave = randInt(-3, 3);
        const bool flag = randInt(0, 1) == 1;

        const auto& events = store.doc().events;
        const auto& sections = store.doc().sections;
        const int eventId = (pickExisting && !events.empty())
                                ? events[static_cast<std::size_t>(pickPosition) % events.size()].id
                                : rawId;
        const int sectionId = (pickExisting && !sections.empty())
                                  ? sections[static_cast<std::size_t>(pickPosition) % sections.size()].id
                                  : rawId;
        const std::string name = nameNumber % 2 == 0 ? std::string() : "S" + std::to_string(nameNumber);

        switch (kind) {
            case 0:
            case 1:
                store.appendChord(chords[chordIndex]);
                break;
            case 2:
            case 3: {
                const int at = store.findIndex(eventId);
                bool startsSection = false;
                for (const Section& s : sections) {
                    startsSection = startsSection || s.firstEventIndex == at;
                }
                store.removeChord(eventId);
                if (at >= 0 && startsSection) {
                    ++removedStartChords;
                }
                break;
            }
            case 4:
                store.moveChord(eventId, index);
                break;
            case 5:
            case 6:
                if (store.addSection(index, name) != kNoSectionId) {
                    ++addedSections;
                }
                break;
            case 7:
                store.renameSection(sectionId, name);
                break;
            case 8:
                store.setSectionColour(sectionId, colour);
                break;
            case 9:
                store.removeSection(sectionId);
                break;
            case 10:
                store.moveSectionStart(sectionId, index);
                break;
            case 11:
                store.setInversion(eventId, inversion);
                break;
            case 12:
                store.setOctave(eventId, octave);
                break;
            case 13:
                store.resetVoicing(eventId);
                break;
            case 14:
                store.setAutoVoiceLead(flag);
                break;
            default:
                store.undo();  // a neutral pair: exercises undo/redo mid-history
                store.redo();
                break;
        }
        CAPTURE(i);
        checkSectionInvariants(store.doc());
    }
    REQUIRE(addedSections >= 5);        // the seed exercised the section ops
    REQUIRE(removedStartChords > 0);    // and the re-indexing on remove
    const Document final = store.doc();
    for (const ChordEvent& e : final.events) {  // the voicing invariants hold alongside the section ones
        CAPTURE(e.id);
        CHECK(e.inversion >= 0);
        CHECK(e.inversion < inversionCount(e.chord));
        CHECK(e.octave >= kMinVoicingOctave);
        CHECK(e.octave <= kMaxVoicingOctave);
        if (!e.manualVoicing) {
            CHECK(e.inversion == 0);
            CHECK(e.octave == 0);
        }
    }

    // At most 200 snapshots were pushed, so the cap never truncates this history.
    int undos = 0;
    while (store.undo()) {
        ++undos;
        checkSectionInvariants(store.doc());
    }
    CHECK(undos > 0);
    CHECK(undos <= 200);
    CHECK(store.doc().events.empty());
    CHECK(store.doc().sections.empty());
    CHECK(store.doc().autoVoiceLead == false);

    int redos = 0;
    while (store.redo()) {
        ++redos;
        checkSectionInvariants(store.doc());
    }
    CHECK(redos == undos);
    CHECK(store.doc() == final);

    // Section ids keep increasing after the whole history was replayed.
    for (const Section& s : final.sections) {
        CHECK(store.nextSectionId() > s.id);
    }
}
