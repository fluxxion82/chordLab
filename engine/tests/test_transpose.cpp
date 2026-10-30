#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/compiler.h"
#include "chordlab/document.h"
#include "chordlab/pitch.h"
#include "chordlab/scale.h"
#include "chordlab/transpose.h"
#include "chordlab/voicing.h"

#include <cstddef>
#include <string_view>
#include <vector>

using namespace chordlab;

namespace {

using Notes = std::vector<int>;

Key key(char letter, int alter, Mode mode) {
    return Key{Tonic{letter, alter}, mode};
}

Chord chord(PitchClass root, std::string_view typeId) {
    return Chord{root, &chordType(typeId)};
}

ChordEvent event(int id, Chord c, int inversion = 0, int octave = 0, bool manual = false) {
    return ChordEvent{id, c, 4.0, 100, inversion, octave, manual};
}

/// The notes event `e` sounds when its stored voicing is used as it is (auto voice-leading off).
Notes notesOf(const ChordEvent& e) {
    return voicing(e.chord, e.inversion, e.octave);
}

/// What the compiler plays for each chord of `store`'s document.
std::vector<Notes> heard(const DocumentStore& store) {
    return voicings(store.doc());
}

/// How many undo steps the store holds (counted on a copy).
int undoDepth(DocumentStore store) {
    int depth = 0;
    while (store.undo()) {
        ++depth;
    }
    return depth;
}

/// C - Am - F - G7 in C: ids 1..4, four beats each, root position, octave 0.
DocumentStore cAmFG7() {
    DocumentStore store;
    store.appendChord(chord(0, "maj"));
    store.appendChord(chord(9, "min"));
    store.appendChord(chord(5, "maj"));
    store.appendChord(chord(7, "7"));
    return store;
}

std::vector<ChordEvent> cAmFG7Events() {
    return cAmFG7().doc().events;
}

std::vector<int> rootsOf(const DocumentStore& store) {
    std::vector<int> roots;
    for (const ChordEvent& e : store.doc().events) {
        roots.push_back(e.chord.root);
    }
    return roots;
}

}  // namespace

// --- transposeEvents ------------------------------------------------------------------------------

TEST_CASE("transposeEvents: C major up a semitone is C# major, same octave") {
    std::vector<ChordEvent> events{event(1, chord(0, "maj"))};
    REQUIRE(notesOf(events[0]) == Notes{60, 64, 67});
    CHECK(transposeEvents(events, 1) == 0);
    CHECK(events[0].chord.root == 1);
    CHECK(events[0].octave == 0);
    CHECK(notesOf(events[0]) == Notes{61, 65, 68});
}

TEST_CASE("transposeEvents: B major up a semitone is C major an octave up (the root crossed B to C)") {
    std::vector<ChordEvent> events{event(1, chord(11, "maj"))};
    REQUIRE(notesOf(events[0]) == Notes{71, 75, 78});
    CHECK(transposeEvents(events, 1) == 0);
    CHECK(events[0].chord.root == 0);
    CHECK(events[0].octave == 1);
    CHECK(notesOf(events[0]) == Notes{72, 76, 79});
}

TEST_CASE("transposeEvents: C major down a semitone is B major an octave down (the root crossed C to B)") {
    std::vector<ChordEvent> events{event(1, chord(0, "maj"))};
    CHECK(transposeEvents(events, -1) == 0);
    CHECK(events[0].chord.root == 11);
    CHECK(events[0].octave == -1);
    CHECK(notesOf(events[0]) == Notes{59, 63, 66});
}

TEST_CASE("transposeEvents: an octave that would leave -1..1 is clamped, counted, and lands an octave off") {
    // B major at octave +1 sounds 83 87 90; up a semitone wants C major at octave +2, which does not exist.
    std::vector<ChordEvent> events{event(1, chord(11, "maj"), 0, 1)};
    REQUIRE(notesOf(events[0]) == Notes{83, 87, 90});
    CHECK(transposeEvents(events, 1) == 1);
    CHECK(events[0].chord.root == 0);
    CHECK(events[0].octave == 1);
    CHECK(notesOf(events[0]) == Notes{72, 76, 79});  // an octave below the wanted 84 88 91

    // The same at the bottom: C major at octave -1 (48 52 55) down a semitone wants B at octave -2.
    std::vector<ChordEvent> low{event(1, chord(0, "maj"), 0, -1)};
    REQUIRE(notesOf(low[0]) == Notes{48, 52, 55});
    CHECK(transposeEvents(low, -1) == 1);
    CHECK(low[0].chord.root == 11);
    CHECK(low[0].octave == -1);
    CHECK(notesOf(low[0]) == Notes{59, 63, 66});  // an octave above the wanted 47 51 54
}

TEST_CASE("transposeEvents: counts every clamped octave and moves the rest") {
    std::vector<ChordEvent> events{event(1, chord(11, "maj"), 0, 1), event(2, chord(2, "min")),
                                   event(3, chord(10, "7"), 0, 1)};
    // +2: B+1 -> C# at octave 2 (clamped); D -> E at 0; Bb+1 -> C at octave 2 (clamped).
    CHECK(transposeEvents(events, 2) == 2);
    CHECK(events[0].octave == 1);
    CHECK(events[1].chord.root == 4);
    CHECK(events[1].octave == 0);
    CHECK(events[2].chord.root == 0);
    CHECK(events[2].octave == 1);
}

TEST_CASE("transposeEvents: a manual inversion-1 chord keeps its inversion and moves exactly +1 across B to C") {
    std::vector<ChordEvent> events{event(7, chord(11, "maj"), 1, 0, true)};
    REQUIRE(notesOf(events[0]) == Notes{75, 78, 83});  // B major, first inversion: D# F# B
    CHECK(transposeEvents(events, 1) == 0);
    CHECK(events[0].chord.root == 0);
    CHECK(events[0].inversion == 1);
    CHECK(events[0].manualVoicing);
    CHECK(events[0].id == 7);
    CHECK(events[0].octave == 1);
    CHECK(notesOf(events[0]) == Notes{76, 79, 84});  // C major, first inversion, an octave up: E G C
}

TEST_CASE("transposeEvents: a manual inversion-1 chord keeps its inversion and moves exactly -1 across C to B") {
    std::vector<ChordEvent> events{event(7, chord(0, "maj"), 1, 0, true)};
    REQUIRE(notesOf(events[0]) == Notes{64, 67, 72});  // C major, first inversion: E G C
    CHECK(transposeEvents(events, -1) == 0);
    CHECK(events[0].chord.root == 11);
    CHECK(events[0].inversion == 1);
    CHECK(events[0].manualVoicing);
    CHECK(events[0].octave == -1);
    CHECK(notesOf(events[0]) == Notes{63, 66, 71});  // B major, first inversion, an octave down: D# F# B
}

TEST_CASE("transposeEvents: a seventh chord in second inversion moves by exactly the interval") {
    // G7 second inversion: close 67 71 74 77, the lowest two lifted: 74 77 79 83.
    std::vector<ChordEvent> events{event(1, chord(7, "7"), 2, 0, true)};
    REQUIRE(notesOf(events[0]) == Notes{74, 77, 79, 83});
    CHECK(transposeEvents(events, 6) == 0);  // G -> C#, crossing B to C
    CHECK(events[0].chord.root == 1);
    CHECK(events[0].octave == 1);
    CHECK(notesOf(events[0]) == Notes{80, 83, 85, 89});
}

TEST_CASE("transposeEvents: only root and octave change; zero moves nothing; every non-zero move is exact") {
    const std::vector<ChordEvent> original{event(1, chord(0, "maj")), event(2, chord(9, "min"), 1, 0, true),
                                           event(3, chord(5, "maj"), 0, -1), event(4, chord(7, "7"), 3, 1, true)};
    std::vector<ChordEvent> same = original;
    CHECK(transposeEvents(same, 0) == 0);
    CHECK(same == original);

    for (int s = -11; s <= 11; ++s) {
        std::vector<ChordEvent> moved = original;
        const int clamped = transposeEvents(moved, s);
        for (std::size_t i = 0; i < moved.size(); ++i) {
            CHECK(moved[i].id == original[i].id);
            CHECK(moved[i].chord.type == original[i].chord.type);
            CHECK(moved[i].inversion == original[i].inversion);
            CHECK(moved[i].manualVoicing == original[i].manualVoicing);
            CHECK(moved[i].lengthBeats == original[i].lengthBeats);
            CHECK(moved[i].velocity == original[i].velocity);
            CHECK(moved[i].chord.root == ((original[i].chord.root + s) % 12 + 12) % 12);
        }
        if (clamped == 0) {
            for (std::size_t i = 0; i < moved.size(); ++i) {
                const Notes before = notesOf(original[i]);
                const Notes after = notesOf(moved[i]);
                REQUIRE(before.size() == after.size());
                for (std::size_t n = 0; n < before.size(); ++n) {
                    CHECK(after[n] - before[n] == s);
                }
            }
        }
    }
}

// --- chooseKeyTranspose ---------------------------------------------------------------------------

TEST_CASE("chooseKeyTranspose: C - Am - F - G7 up a fifth goes down a fourth (-5)") {
    // Up 7: octaves 0, 1, 1, 1 (sum 3); down 5: octaves -1, 0, 0, 0 (sum 1).
    CHECK(chooseKeyTranspose(cAmFG7Events(), 7) == -5);
}

TEST_CASE("chooseKeyTranspose: C - Am - F - G7 up a whole tone stays up (+2)") {
    // Up 2 changes no octave; down 10 would lower all four.
    CHECK(chooseKeyTranspose(cAmFG7Events(), 2) == 2);
}

TEST_CASE("chooseKeyTranspose: a tritone ties on everything and goes up (+6)") {
    // Up 6: octaves 0, 1, 0, 1; down 6: octaves -1, 0, -1, 0. No clamp, largest 1, sum 2, size 6: up wins.
    CHECK(chooseKeyTranspose(cAmFG7Events(), 6) == 6);
}

TEST_CASE("chooseKeyTranspose: a perfect fourth up stays up (+5), the smaller move, after a tie on octaves") {
    // Up 5: octaves 0, 1, 0, 1; down 7: -1, 0, -1, 0. All tie; |5| < |7|.
    CHECK(chooseKeyTranspose(cAmFG7Events(), 5) == 5);
}

TEST_CASE("chooseKeyTranspose: with nothing to weigh, the smaller move wins and a tritone goes up") {
    const std::vector<ChordEvent> none;
    CHECK(chooseKeyTranspose(none, 0) == 0);
    CHECK(chooseKeyTranspose(none, 1) == 1);
    CHECK(chooseKeyTranspose(none, 5) == 5);
    CHECK(chooseKeyTranspose(none, 6) == 6);
    CHECK(chooseKeyTranspose(none, 7) == -5);
    CHECK(chooseKeyTranspose(none, 11) == -1);
}

TEST_CASE("chooseKeyTranspose: an interval of 0 (or a whole number of octaves) is 0") {
    CHECK(chooseKeyTranspose(cAmFG7Events(), 0) == 0);
    CHECK(chooseKeyTranspose(cAmFG7Events(), 12) == 0);
    CHECK(chooseKeyTranspose(cAmFG7Events(), -12) == 0);
}

TEST_CASE("chooseKeyTranspose: a progression already an octave low goes up a fifth (+7), no clamp") {
    std::vector<ChordEvent> events = cAmFG7Events();
    for (ChordEvent& e : events) {
        e.octave = -1;
    }
    // Down 5 would put C at octave -2 (clamped); up 7 lowers nothing: octaves -1, 0, 0, 0.
    CHECK(chooseKeyTranspose(events, 7) == 7);
}

TEST_CASE("chooseKeyTranspose: when both directions clamp, the one that clamps fewer wins") {
    // Up 7 raises the two F chords (octave +1) past +1: two clamps. Down 5 lowers the C (octave -1) past -1:
    // one clamp. After clamping both candidates end with the same octaves (largest 1, sum 3), so only the
    // clamp count separates them, and it must come first.
    const std::vector<ChordEvent> events{event(1, chord(5, "maj"), 0, 1), event(2, chord(5, "maj"), 0, 1),
                                         event(3, chord(0, "maj"), 0, -1)};
    std::vector<ChordEvent> up = events;
    std::vector<ChordEvent> down = events;
    REQUIRE(transposeEvents(up, 7) == 2);
    REQUIRE(transposeEvents(down, -5) == 1);
    CHECK(chooseKeyTranspose(events, 7) == -5);
}

TEST_CASE("chooseKeyTranspose: always interval or interval - 12, and never changes its input") {
    const std::vector<ChordEvent> events = cAmFG7Events();
    for (int interval = 1; interval <= 11; ++interval) {
        const int s = chooseKeyTranspose(events, interval);
        CHECK((s == interval || s == interval - 12));
    }
    CHECK(events == cAmFG7Events());
}

// --- setProjectKey with the switch -----------------------------------------------------------------

TEST_CASE("setProjectKey: with the switch off the chords stay exactly where they are") {
    DocumentStore store = cAmFG7();
    const std::vector<ChordEvent> before = store.doc().events;
    store.setProjectKey(key('G', 0, Mode::Ionian));
    CHECK(store.doc().projectKey == key('G', 0, Mode::Ionian));
    CHECK(store.doc().events == before);
    CHECK(heard(store) == std::vector<Notes>{{60, 64, 67}, {69, 72, 76}, {65, 69, 72}, {67, 71, 74, 77}});
}

TEST_CASE("setProjectKey: with the switch on, C - Am - F - G7 to G becomes G - Em - C - D7, each note 5 lower") {
    DocumentStore store = cAmFG7();
    store.addSection(0, "A");
    store.addSection(2, "B");
    store.setSelectedEvent(3);
    store.setTransposeWithKey(true);
    const std::vector<Section> sections = store.doc().sections;
    const int nextId = store.nextId();
    const int nextSectionId = store.nextSectionId();
    const int depth = undoDepth(store);

    store.setProjectKey(key('G', 0, Mode::Ionian));

    CHECK(store.doc().projectKey == key('G', 0, Mode::Ionian));
    CHECK(rootsOf(store) == std::vector<int>{7, 4, 0, 2});
    CHECK(heard(store) == std::vector<Notes>{{55, 59, 62}, {64, 67, 71}, {60, 64, 67}, {62, 66, 69, 72}});
    const std::vector<ChordEvent>& events = store.doc().events;
    const char* types[] = {"maj", "min", "maj", "7"};
    const int octaves[] = {-1, 0, 0, 0};  // only the C crossed below C
    for (std::size_t i = 0; i < events.size(); ++i) {
        CHECK(events[i].id == static_cast<int>(i) + 1);
        CHECK(events[i].chord.type == &chordType(types[i]));
        CHECK(events[i].inversion == 0);
        CHECK_FALSE(events[i].manualVoicing);
        CHECK(events[i].velocity == 100);
        CHECK(events[i].lengthBeats == 4.0);
        CHECK(events[i].octave == octaves[i]);
    }
    CHECK(store.doc().sections == sections);
    CHECK(store.selectedEventId() == 3);
    CHECK(store.nextId() == nextId);
    CHECK(store.nextSectionId() == nextSectionId);
    CHECK(store.doc().transposeWithKey);
    CHECK(undoDepth(store) == depth + 1);  // key and chords: one step
}

TEST_CASE("setProjectKey: the switch on keeps manual choices, inversion and manual flag included") {
    DocumentStore store = cAmFG7();
    store.setInversion(3, 1);  // F major first inversion: 69 72 77
    store.setOctave(4, 1);     // G7 an octave up: 79 83 86 89
    store.setTransposeWithKey(true);
    REQUIRE(heard(store)[2] == Notes{69, 72, 77});
    REQUIRE(heard(store)[3] == Notes{79, 83, 86, 89});
    store.setProjectKey(key('G', 0, Mode::Ionian));
    const std::vector<ChordEvent>& events = store.doc().events;
    CHECK(events[2].inversion == 1);
    CHECK(events[2].manualVoicing);
    CHECK(events[3].octave == 1);
    CHECK(events[3].manualVoicing);
    CHECK(heard(store)[2] == Notes{64, 67, 72});          // C first inversion: E G C
    CHECK(heard(store)[3] == Notes{74, 78, 81, 84});      // D7 at octave +1, five below 79 83 86 89
}

TEST_CASE("setProjectKey: an empty sequence changes the key only") {
    DocumentStore store;
    store.setTransposeWithKey(true);
    store.setProjectKey(key('G', 0, Mode::Ionian));
    CHECK(store.doc().projectKey == key('G', 0, Mode::Ionian));
    CHECK(store.doc().events.empty());
    CHECK(undoDepth(store) == 1);
}

// --- mode only, enharmonic, tonic and mode ----------------------------------------------------------

TEST_CASE("setProjectKey: a mode-only change relabels and moves no chord, switch on") {
    DocumentStore store = cAmFG7();
    store.setTransposeWithKey(true);
    const std::vector<ChordEvent> before = store.doc().events;
    store.setProjectKey(key('C', 0, Mode::Aeolian));
    CHECK(store.doc().projectKey == key('C', 0, Mode::Aeolian));
    CHECK(store.doc().events == before);
    CHECK(undoDepth(store) == 5);  // four appends, one key change
}

TEST_CASE("setProjectKey: an enharmonic respelling of the tonic moves no chord, switch on") {
    DocumentStore store = cAmFG7();
    store.setProjectKey(key('C', 1, Mode::Ionian));  // switch still off: the chords stay
    store.setTransposeWithKey(true);
    const std::vector<ChordEvent> before = store.doc().events;
    store.setProjectKey(key('D', -1, Mode::Ionian));  // Db: the same pitch class
    CHECK(store.doc().projectKey == key('D', -1, Mode::Ionian));
    CHECK(store.doc().events == before);
}

TEST_CASE("setProjectKey: a tonic and mode change together moves by the tonic interval (C Ionian to D Dorian: +2)") {
    DocumentStore store = cAmFG7();
    store.setTransposeWithKey(true);
    store.setProjectKey(key('D', 0, Mode::Dorian));
    CHECK(store.doc().projectKey == key('D', 0, Mode::Dorian));
    CHECK(rootsOf(store) == std::vector<int>{2, 11, 7, 9});
    CHECK(heard(store) == std::vector<Notes>{{62, 66, 69}, {71, 74, 78}, {67, 71, 74}, {69, 73, 76, 79}});
}

TEST_CASE("setProjectKey: the relative minor by tonic then mode transposes, then relabels") {
    DocumentStore store = cAmFG7();
    store.setTransposeWithKey(true);
    store.setProjectKey(key('A', 0, Mode::Ionian));  // C -> A: interval 9, down 3
    CHECK(rootsOf(store) == std::vector<int>{9, 6, 2, 4});
    const std::vector<ChordEvent> afterTonic = store.doc().events;
    store.setProjectKey(key('A', 0, Mode::Aeolian));  // mode only
    CHECK(store.doc().events == afterTonic);
}

// --- Auto voice-lead on, mixed manual and automatic ---------------------------------------------------

TEST_CASE("setProjectKey: with Auto voice-lead on, the first and manual chords move, the others are re-voiced") {
    DocumentStore store;
    store.appendChord(chord(0, "maj"));  // 1: first, never voiced by the user
    store.appendChord(chord(9, "min"));  // 2: automatic
    store.appendChord(chord(0, "maj"));  // 3: manual, first inversion
    store.appendChord(chord(7, "7"));    // 4: automatic
    store.setInversion(3, 1);
    store.setAutoVoiceLead(true);
    store.setTransposeWithKey(true);
    REQUIRE(heard(store)[0] == Notes{60, 64, 67});
    REQUIRE(heard(store)[2] == Notes{64, 67, 72});

    store.setProjectKey(key('G', 0, Mode::Ionian));  // down 5

    const std::vector<ChordEvent>& events = store.doc().events;
    CHECK(store.doc().autoVoiceLead);
    CHECK(events[0].chord.root == 7);
    CHECK(events[0].octave == -1);
    CHECK_FALSE(events[0].manualVoicing);
    CHECK(events[2].chord.root == 7);
    CHECK(events[2].inversion == 1);
    CHECK(events[2].octave == -1);
    CHECK(events[2].manualVoicing);
    const std::vector<Notes> notes = heard(store);
    CHECK(notes[0] == Notes{55, 59, 62});  // G, five below C
    CHECK(notes[2] == Notes{59, 62, 67});  // G first inversion, five below 64 67 72
    // Em: of the 15 candidates the one closest to G (55 59 62) is first inversion, an octave down: cost 2.
    CHECK(notes[1] == Notes{55, 59, 64});
    // D7: closest to the manual G first inversion (59 62 67, with the lowest note doubled up an octave to
    // match four voices) is third inversion, an octave down: 60 62 66 69, cost 1 + 0 + 1 + 2 = 4.
    CHECK(notes[3] == Notes{60, 62, 66, 69});
}

// --- undo and redo ---------------------------------------------------------------------------------

TEST_CASE("setProjectKey: undo restores key and chords in one step, redo applies both again") {
    DocumentStore store = cAmFG7();
    store.setTransposeWithKey(true);
    const Document before = store.doc();
    store.setProjectKey(key('G', 0, Mode::Ionian));
    const Document after = store.doc();
    REQUIRE(after != before);

    REQUIRE(store.undo());
    CHECK(store.doc() == before);
    REQUIRE(store.redo());
    CHECK(store.doc() == after);
    CHECK_FALSE(store.redo());
}

TEST_CASE("setProjectKey: toggling the switch off after a transposing change, then undo, restores key and chords") {
    DocumentStore store = cAmFG7();
    store.setTransposeWithKey(true);
    const Document before = store.doc();
    store.setProjectKey(key('G', 0, Mode::Ionian));
    const Document after = store.doc();
    store.setTransposeWithKey(false);
    CHECK(undoDepth(store) == 5);  // the toggle added nothing

    REQUIRE(store.undo());
    CHECK(store.doc().projectKey == before.projectKey);
    CHECK(store.doc().events == before.events);
    CHECK_FALSE(store.doc().transposeWithKey);  // live: kept, not restored to "on"

    REQUIRE(store.redo());
    CHECK(store.doc().projectKey == after.projectKey);
    CHECK(store.doc().events == after.events);
    CHECK_FALSE(store.doc().transposeWithKey);
}

TEST_CASE("setProjectKey: a new edit after undoing a transposing change clears redo") {
    DocumentStore store = cAmFG7();
    store.setTransposeWithKey(true);
    store.setProjectKey(key('G', 0, Mode::Ionian));
    REQUIRE(store.undo());
    store.appendChord(chord(2, "min"));
    CHECK_FALSE(store.redo());
    CHECK(store.doc().projectKey == key('C', 0, Mode::Ionian));
    CHECK(rootsOf(store) == std::vector<int>{0, 9, 5, 7, 2});
}

// --- live state --------------------------------------------------------------------------------------

TEST_CASE("Document: transposeWithKey and keyHintSeen default to false and each alone makes documents unequal") {
    const Document base;
    CHECK_FALSE(base.transposeWithKey);
    CHECK_FALSE(base.keyHintSeen);
    Document a = base;
    a.transposeWithKey = true;
    CHECK(a != base);
    Document b = base;
    b.keyHintSeen = true;
    CHECK(b != base);
    CHECK(a != b);
    b.transposeWithKey = true;
    a.keyHintSeen = true;
    CHECK(a == b);
}

TEST_CASE("setTransposeWithKey / setKeyHintSeen: live state, no history, independent") {
    DocumentStore store = cAmFG7();
    REQUIRE(store.undo());  // leave a redo entry to prove the setters do not clear it
    store.setTransposeWithKey(true);
    CHECK(store.doc().transposeWithKey);
    CHECK_FALSE(store.doc().keyHintSeen);
    store.setKeyHintSeen(true);
    CHECK(store.doc().keyHintSeen);
    store.setTransposeWithKey(false);
    CHECK_FALSE(store.doc().transposeWithKey);
    CHECK(store.doc().keyHintSeen);
    REQUIRE(store.redo());  // still there
    CHECK(rootsOf(store) == std::vector<int>{0, 9, 5, 7});
    CHECK(undoDepth(store) == 4);  // only the four appends
}

TEST_CASE("undo and redo keep the current values of transposeWithKey and keyHintSeen") {
    DocumentStore store;
    store.appendChord(chord(0, "maj"));
    store.appendChord(chord(7, "7"));
    store.setTransposeWithKey(true);
    store.setKeyHintSeen(true);
    REQUIRE(store.undo());
    CHECK(store.doc().transposeWithKey);
    CHECK(store.doc().keyHintSeen);
    store.setTransposeWithKey(false);
    REQUIRE(store.undo());
    CHECK_FALSE(store.doc().transposeWithKey);
    CHECK(store.doc().keyHintSeen);
    store.setKeyHintSeen(false);
    REQUIRE(store.redo());
    CHECK_FALSE(store.doc().transposeWithKey);
    CHECK_FALSE(store.doc().keyHintSeen);
}

TEST_CASE("replace takes transposeWithKey and keyHintSeen from the loaded document") {
    DocumentStore store;
    store.setTransposeWithKey(true);
    store.setKeyHintSeen(true);
    Document loaded;
    loaded.transposeWithKey = false;
    loaded.keyHintSeen = true;
    store.replace(loaded, 1);
    CHECK_FALSE(store.doc().transposeWithKey);
    CHECK(store.doc().keyHintSeen);
    loaded.transposeWithKey = true;
    loaded.keyHintSeen = false;
    store.replace(loaded, 1);
    CHECK(store.doc().transposeWithKey);
    CHECK_FALSE(store.doc().keyHintSeen);
}

// --- transposeFromKey ---------------------------------------------------------------------------------

TEST_CASE("transposeFromKey: moves every chord from the old tonic to the current one, one undo step, key kept") {
    DocumentStore store = cAmFG7();
    store.addSection(0, "A");
    store.setSelectedEvent(2);
    store.setProjectKey(key('G', 0, Mode::Ionian));  // switch off: only the key
    REQUIRE(rootsOf(store) == std::vector<int>{0, 9, 5, 7});
    const std::vector<Section> sections = store.doc().sections;
    const int nextId = store.nextId();
    const int nextSectionId = store.nextSectionId();
    const int depth = undoDepth(store);

    CHECK(store.transposeFromKey(0));

    CHECK(store.doc().projectKey == key('G', 0, Mode::Ionian));
    CHECK(rootsOf(store) == std::vector<int>{7, 4, 0, 2});
    CHECK(heard(store) == std::vector<Notes>{{55, 59, 62}, {64, 67, 71}, {60, 64, 67}, {62, 66, 69, 72}});
    CHECK(store.doc().sections == sections);
    CHECK(store.selectedEventId() == 2);
    CHECK(store.nextId() == nextId);
    CHECK(store.nextSectionId() == nextSectionId);
    CHECK(undoDepth(store) == depth + 1);

    REQUIRE(store.undo());
    CHECK(rootsOf(store) == std::vector<int>{0, 9, 5, 7});
    CHECK(store.doc().projectKey == key('G', 0, Mode::Ionian));
    REQUIRE(store.redo());
    CHECK(rootsOf(store) == std::vector<int>{7, 4, 0, 2});
}

TEST_CASE("transposeFromKey: the same tonic, or no chords, is a no-op with no history") {
    DocumentStore store = cAmFG7();
    const int depth = undoDepth(store);
    const Document before = store.doc();
    CHECK_FALSE(store.transposeFromKey(0));  // the project tonic is C
    CHECK(store.doc() == before);
    CHECK(undoDepth(store) == depth);

    DocumentStore empty;
    empty.setProjectKey(key('G', 0, Mode::Ionian));
    CHECK_FALSE(empty.transposeFromKey(0));
    CHECK(undoDepth(empty) == 1);  // the key change only
    CHECK(empty.doc().projectKey == key('G', 0, Mode::Ionian));
}

TEST_CASE("transposeFromKey: a redo entry survives a no-op but not a move") {
    DocumentStore store = cAmFG7();
    store.setProjectKey(key('G', 0, Mode::Ionian));
    REQUIRE(store.undo());
    CHECK_FALSE(store.transposeFromKey(0));  // tonic is C again: nothing to move
    CHECK(store.redo());
}

// --- transposeProgression -------------------------------------------------------------------------------

TEST_CASE("transposeProgression: up a semitone moves every note by +1, one undo step, key unchanged") {
    DocumentStore store = cAmFG7();
    store.addSection(2, "B");
    store.setSelectedEvent(4);
    const std::vector<Section> sections = store.doc().sections;
    const int nextId = store.nextId();
    const int nextSectionId = store.nextSectionId();
    const int depth = undoDepth(store);

    CHECK(store.transposeProgression(1));

    CHECK(store.doc().projectKey == key('C', 0, Mode::Ionian));
    CHECK(rootsOf(store) == std::vector<int>{1, 10, 6, 8});
    CHECK(heard(store) == std::vector<Notes>{{61, 65, 68}, {70, 73, 77}, {66, 70, 73}, {68, 72, 75, 78}});
    CHECK(store.doc().sections == sections);
    CHECK(store.selectedEventId() == 4);
    CHECK(store.nextId() == nextId);
    CHECK(store.nextSectionId() == nextSectionId);
    CHECK(undoDepth(store) == depth + 1);

    REQUIRE(store.undo());
    CHECK(heard(store) == std::vector<Notes>{{60, 64, 67}, {69, 72, 76}, {65, 69, 72}, {67, 71, 74, 77}});
    REQUIRE(store.redo());
    CHECK(rootsOf(store) == std::vector<int>{1, 10, 6, 8});
}

TEST_CASE("transposeProgression: down a semitone moves every note by -1, the C taking an octave down") {
    DocumentStore store = cAmFG7();
    CHECK(store.transposeProgression(-1));
    CHECK(rootsOf(store) == std::vector<int>{11, 8, 4, 6});
    CHECK(heard(store) == std::vector<Notes>{{59, 63, 66}, {68, 71, 75}, {64, 68, 71}, {66, 70, 73, 76}});
    CHECK(store.doc().events[0].octave == -1);
}

TEST_CASE("transposeProgression: a manual inversion-1 chord crossing B to C and C to B moves by exactly one") {
    DocumentStore store;
    store.appendChord(chord(11, "maj"));
    store.appendChord(chord(0, "maj"));
    store.setInversion(1, 1);
    store.setInversion(2, 1);
    REQUIRE(heard(store) == std::vector<Notes>{{75, 78, 83}, {64, 67, 72}});
    CHECK(store.transposeProgression(1));
    CHECK(heard(store) == std::vector<Notes>{{76, 79, 84}, {65, 68, 73}});  // C first inv. up; C# first inv.
    REQUIRE(store.undo());
    CHECK(store.transposeProgression(-1));
    CHECK(heard(store) == std::vector<Notes>{{74, 77, 82}, {63, 66, 71}});  // A# first inv.; B first inv. down
    CHECK(store.doc().events[0].inversion == 1);
    CHECK(store.doc().events[1].inversion == 1);
    CHECK(store.doc().events[0].manualVoicing);
    CHECK(store.doc().events[1].manualVoicing);
}

TEST_CASE("transposeProgression: 0, out of range and an empty sequence are no-ops with no history") {
    DocumentStore store = cAmFG7();
    REQUIRE(store.undo());
    store.redo();
    const Document before = store.doc();
    const int depth = undoDepth(store);
    CHECK_FALSE(store.transposeProgression(0));
    CHECK_FALSE(store.transposeProgression(12));
    CHECK_FALSE(store.transposeProgression(-12));
    CHECK(store.doc() == before);
    CHECK(undoDepth(store) == depth);

    DocumentStore empty;
    CHECK_FALSE(empty.transposeProgression(3));
    CHECK_FALSE(empty.undo());
}

TEST_CASE("transposeProgression: a no-op leaves the redo stack, a move clears it") {
    DocumentStore store = cAmFG7();
    REQUIRE(store.undo());
    CHECK_FALSE(store.transposeProgression(0));
    CHECK(store.redo());
    REQUIRE(store.undo());
    CHECK(store.transposeProgression(2));
    CHECK_FALSE(store.redo());
}

// --- capacity ---------------------------------------------------------------------------------------------

TEST_CASE("setProjectKey: 256 events transposed in one commit, capacity unchanged") {
    DocumentStore store;
    for (int i = 0; i < kMaxChords; ++i) {
        REQUIRE(store.appendChord(chord(i % 12, i % 2 == 0 ? "maj" : "7")) != kNoEventId);
    }
    REQUIRE(store.doc().events.size() == 256);
    store.setTransposeWithKey(true);
    const std::vector<Notes> before = heard(store);
    const std::vector<ChordEvent> eventsBefore = store.doc().events;
    REQUIRE(chooseKeyTranspose(eventsBefore, 7) == -5);

    store.setProjectKey(key('G', 0, Mode::Ionian));

    REQUIRE(store.doc().events.size() == 256);
    const std::vector<Notes> after = heard(store);
    for (std::size_t i = 0; i < after.size(); ++i) {
        REQUIRE(after[i].size() == before[i].size());
        CHECK(store.doc().events[i].id == eventsBefore[i].id);
        CHECK(store.doc().events[i].chord.root == ((eventsBefore[i].chord.root - 5) % 12 + 12) % 12);
        for (std::size_t n = 0; n < after[i].size(); ++n) {
            CHECK(after[i][n] == before[i][n] - 5);
        }
    }
    CHECK(store.appendChord(chord(0, "maj")) == kNoEventId);  // still full
    CHECK(compile(store.doc()).endBeat == 256 * 4.0);

    // One commit: a single undo (256 appends already filled the history, so its depth cannot show it)
    // brings every chord and the key back.
    REQUIRE(store.undo());
    CHECK(store.doc().events == eventsBefore);
    CHECK(store.doc().projectKey == key('C', 0, Mode::Ionian));
    REQUIRE(store.redo());
    CHECK(store.doc().projectKey == key('G', 0, Mode::Ionian));
    CHECK(heard(store) == after);
}

TEST_CASE("resetVoicing: a non-manual chord a transpose left at octave -1 returns to octave 0 in one undo step") {
    DocumentStore store = cAmFG7();
    store.setTransposeWithKey(true);
    store.setProjectKey(key('G', 0, Mode::Ionian));
    const ChordEvent& first = store.doc().events[0];
    REQUIRE(first.octave == -1);
    REQUIRE_FALSE(first.manualVoicing);
    CHECK(notesOf(first) == Notes{55, 59, 62});  // G major, an octave below the default register
    const int depth = undoDepth(store);

    store.resetVoicing(1);

    const ChordEvent& reset = store.doc().events[0];
    CHECK(reset.octave == 0);
    CHECK(reset.inversion == 0);
    CHECK_FALSE(reset.manualVoicing);
    CHECK(notesOf(reset) == Notes{67, 71, 74});  // the default register
    CHECK(undoDepth(store) == depth + 1);

    REQUIRE(store.undo());
    CHECK(store.doc().events[0].octave == -1);
    CHECK(notesOf(store.doc().events[0]) == Notes{55, 59, 62});
    CHECK_FALSE(store.doc().events[0].manualVoicing);
}
