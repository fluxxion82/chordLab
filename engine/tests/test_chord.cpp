#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/pitch.h"
#include "chordlab/scale.h"

#include <cstddef>
#include <set>
#include <stdexcept>
#include <string>
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

struct ExpectedType {
    const char* id;
    std::vector<int> intervals;
    const char* symbol;
    Family family;
};

// The dictionary from the plan, in order. Mirrored here so the test does not depend on the table under test.
const std::vector<ExpectedType> kExpected{
    {"5", {0, 7}, "5", Family::Other},
    {"maj", {0, 4, 7}, "", Family::Major},
    {"min", {0, 3, 7}, "m", Family::Minor},
    {"dim", {0, 3, 6}, "mb5", Family::Other},
    {"aug", {0, 4, 8}, "+", Family::Other},
    {"sus2", {0, 2, 7}, "sus2", Family::Other},
    {"sus4", {0, 5, 7}, "sus4", Family::Other},
    {"6", {0, 4, 7, 9}, "6", Family::Major},
    {"m6", {0, 3, 7, 9}, "m6", Family::Minor},
    {"maj7", {0, 4, 7, 11}, "Δ7", Family::Major},
    {"7", {0, 4, 7, 10}, "7", Family::Major},
    {"m7", {0, 3, 7, 10}, "m7", Family::Minor},
    {"m7b5", {0, 3, 6, 10}, "m7b5", Family::Other},
    {"dim7", {0, 3, 6, 9}, "dim7", Family::Other},
    {"mMaj7", {0, 3, 7, 11}, "mΔ7", Family::Minor},
    {"augMaj7", {0, 4, 8, 11}, "+Δ7", Family::Other},
    {"add9", {0, 4, 7, 14}, "add9", Family::Major},
    {"madd9", {0, 3, 7, 14}, "madd9", Family::Minor},
    {"9", {0, 4, 7, 10, 14}, "9", Family::Major},
    {"maj9", {0, 4, 7, 11, 14}, "Δ9", Family::Major},
    {"m9", {0, 3, 7, 10, 14}, "m9", Family::Minor},
    {"69", {0, 4, 7, 9, 14}, "6/9", Family::Major},
    {"7sus4", {0, 5, 7, 10}, "7sus4", Family::Other},
    // Phase 2 colour chords, appended so the ids above keep their positions.
    {"maj7#11", {0, 4, 7, 11, 18}, "Δ7#11", Family::Major},
    {"mMaj9", {0, 3, 7, 11, 14}, "mΔ9", Family::Minor},
    {"7#11", {0, 4, 7, 10, 18}, "7#11", Family::Major},
};

}  // namespace

// --- Dictionary ------------------------------------------------------------------

TEST_CASE("allChordTypes: has 26 entries with unique ids") {
    const auto& types = allChordTypes();
    CHECK(types.size() == 26);
    std::set<std::string> ids;
    for (const ChordType& t : types) {
        ids.insert(t.id);
    }
    CHECK(ids.size() == types.size());
}

TEST_CASE("allChordTypes: matches the plan's dictionary in order") {
    const auto& types = allChordTypes();
    REQUIRE(types.size() == kExpected.size());
    for (std::size_t i = 0; i < kExpected.size(); ++i) {
        CAPTURE(i);
        CAPTURE(kExpected[i].id);
        CHECK(std::string_view(types[i].id) == kExpected[i].id);
        CHECK(types[i].intervals == kExpected[i].intervals);
        CHECK(std::string_view(types[i].symbol) == kExpected[i].symbol);
        CHECK(types[i].family == kExpected[i].family);
    }
}

TEST_CASE("allChordTypes: every type's intervals start at 0, are strictly increasing and below 24") {
    for (const ChordType& t : allChordTypes()) {
        CAPTURE(t.id);
        REQUIRE_FALSE(t.intervals.empty());
        CHECK(t.intervals[0] == 0);
        for (std::size_t i = 1; i < t.intervals.size(); ++i) {
            CHECK(t.intervals[i] > t.intervals[i - 1]);
            CHECK(t.intervals[i] < 24);
        }
    }
}

TEST_CASE("allChordTypes: no type repeats a pitch class within the octave") {
    for (const ChordType& t : allChordTypes()) {
        CAPTURE(t.id);
        std::set<int> reduced;
        for (const int interval : t.intervals) {
            reduced.insert(interval % 12);
        }
        CHECK(reduced.size() == t.intervals.size());
    }
}

// --- chordType ---------------------------------------------------------------------

TEST_CASE("chordType: looks up by id") {
    CHECK(chordType("m7b5").family == Family::Other);
    CHECK(chordType("maj").family == Family::Major);
    CHECK(chordType("min").family == Family::Minor);
    CHECK(chordType("maj7").intervals == std::vector<int>{0, 4, 7, 11});
    CHECK(std::string_view(chordType("maj7").id) == "maj7");
}

TEST_CASE("chordType: symbols are Klimper-style suffixes") {
    CHECK(std::string_view(chordType("maj7").symbol) == "Δ7");
    CHECK(std::string_view(chordType("dim").symbol) == "mb5");
    CHECK(std::string_view(chordType("maj").symbol) == "");
    CHECK(std::string_view(chordType("mMaj7").symbol) == "mΔ7");
    CHECK(std::string_view(chordType("69").symbol) == "6/9");
    CHECK(std::string_view(chordType("maj7#11").symbol) == "Δ7#11");
    CHECK(std::string_view(chordType("mMaj9").symbol) == "mΔ9");
    CHECK(std::string_view(chordType("7#11").symbol) == "7#11");
}

TEST_CASE("chordType: throws std::out_of_range for an unknown id") {
    CHECK_THROWS_AS(chordType("nope"), std::out_of_range);
    CHECK_THROWS_AS(chordType(""), std::out_of_range);
    CHECK_THROWS_AS(chordType("MAJ"), std::out_of_range);  // ids are case-sensitive
}

TEST_CASE("chordType: returns a stable reference into allChordTypes") {
    CHECK(&chordType("maj") == &allChordTypes()[1]);
    CHECK(&chordType("5") == &allChordTypes()[0]);
    CHECK(&chordType("7sus4") == &allChordTypes()[22]);
    CHECK(&chordType("7#11") == &allChordTypes()[25]);
    for (const ChordType& t : allChordTypes()) {
        CAPTURE(t.id);
        CHECK(&chordType(t.id) == &t);
    }
    CHECK(&chordType("maj") == &chordType("maj"));
}

// --- Chord ----------------------------------------------------------------------------

TEST_CASE("Chord: equality compares root and type identity") {
    CHECK(chord(0, "maj") == chord(0, "maj"));
    CHECK(chord(0, "maj") != chord(7, "maj"));
    CHECK(chord(0, "maj") != chord(0, "min"));
    CHECK_FALSE(chord(0, "maj") == chord(0, "min"));
}

// --- pitchClasses --------------------------------------------------------------------

TEST_CASE("pitchClasses: plan case Cmaj7") {
    CHECK(pitchClasses(chord(0, "maj7")) == std::vector<PitchClass>{0, 4, 7, 11});
}

TEST_CASE("pitchClasses: wraps intervals past the octave, in interval order") {
    CHECK(pitchClasses(chord(9, "madd9")) == std::vector<PitchClass>{9, 0, 4, 11});  // 9+14=23 -> 11
    CHECK(pitchClasses(chord(7, "7")) == std::vector<PitchClass>{7, 11, 2, 5});
    CHECK(pitchClasses(chord(11, "dim")) == std::vector<PitchClass>{11, 2, 5});
}

TEST_CASE("pitchClasses: reduces an out-of-range root modulo 12") {
    CHECK(pitchClasses(chord(12, "maj")) == std::vector<PitchClass>{0, 4, 7});
    CHECK(pitchClasses(chord(-3, "maj")) == std::vector<PitchClass>{9, 1, 4});
}

TEST_CASE("pitchClasses: has one entry per interval for every type") {
    for (const ChordType& t : allChordTypes()) {
        CAPTURE(t.id);
        const Chord c{2, &t};
        CHECK(pitchClasses(c).size() == t.intervals.size());
        CHECK(pitchClasses(c)[0] == 2);
    }
}

// --- fullyInKey -----------------------------------------------------------------------

TEST_CASE("fullyInKey: plan cases in C Ionian") {
    const Key cMajor = key('C', 0, Mode::Ionian);
    CHECK(fullyInKey(chord(11, "dim"), cMajor) == true);   // Bmb5 = B D F
    CHECK(fullyInKey(chord(11, "min"), cMajor) == false);  // Bm = B D F#
    CHECK(fullyInKey(chord(7, "7"), cMajor) == true);      // G7 = G B D F
    CHECK(fullyInKey(chord(7, "maj7"), cMajor) == false);  // GΔ7 = G B D F#
}

TEST_CASE("fullyInKey: all seven diatonic triads of C Ionian are in key") {
    const Key cMajor = key('C', 0, Mode::Ionian);
    CHECK(fullyInKey(chord(0, "maj"), cMajor));
    CHECK(fullyInKey(chord(2, "min"), cMajor));
    CHECK(fullyInKey(chord(4, "min"), cMajor));
    CHECK(fullyInKey(chord(5, "maj"), cMajor));
    CHECK(fullyInKey(chord(7, "maj"), cMajor));
    CHECK(fullyInKey(chord(9, "min"), cMajor));
    CHECK(fullyInKey(chord(11, "dim"), cMajor));
}

TEST_CASE("fullyInKey: respects the key's mode and spelled tonic") {
    CHECK(fullyInKey(chord(7, "maj"), key('A', 0, Mode::Aeolian)));          // G in A natural minor
    CHECK_FALSE(fullyInKey(chord(7, "maj"), key('A', 0, Mode::HarmonicMinor)));  // G# replaces G
    CHECK(fullyInKey(chord(4, "7"), key('A', 0, Mode::HarmonicMinor)));      // E7 = E G# B D
    CHECK(fullyInKey(chord(6, "maj"), key('G', -1, Mode::Ionian)));          // Gb major on Gb
    CHECK(fullyInKey(chord(6, "maj"), key('F', +1, Mode::Ionian)));          // same pcs on F#
}

TEST_CASE("fullyInKey: extensions above the octave are checked too") {
    const Key cMajor = key('C', 0, Mode::Ionian);
    CHECK(fullyInKey(chord(0, "maj9"), cMajor));        // C E G B D
    CHECK(fullyInKey(chord(4, "m9"), cMajor) == false); // Em9 = E G B D F# (F# out)
    CHECK(fullyInKey(chord(2, "m9"), cMajor));          // Dm9 = D F A C E
    CHECK(fullyInKey(chord(5, "maj7#11"), cMajor));                    // FΔ7#11 = F A C E B
    CHECK(fullyInKey(chord(0, "maj7#11"), cMajor) == false);           // F# out
    CHECK(fullyInKey(chord(0, "maj7#11"), key('C', 0, Mode::Lydian)));
    CHECK(fullyInKey(chord(0, "mMaj9"), key('C', 0, Mode::MelodicMinor)));   // C Eb G B D
    CHECK(fullyInKey(chord(0, "mMaj9"), key('C', 0, Mode::Aeolian)) == false);  // B out
    CHECK(fullyInKey(chord(5, "7#11"), key('C', 0, Mode::MelodicMinor)));     // F A C Eb B
    CHECK(fullyInKey(chord(0, "7#11"), key('C', 0, Mode::Mixolydian)) == false);  // F# out
}

// --- diatonicTriad --------------------------------------------------------------------

TEST_CASE("diatonicTriad: the seven triads of C major and A harmonic minor") {
    const Key cMajor = key('C', 0, Mode::Ionian);
    CHECK(diatonicTriad(cMajor, 0) == chord(0, "maj"));
    CHECK(diatonicTriad(cMajor, 1) == chord(2, "min"));
    CHECK(diatonicTriad(cMajor, 2) == chord(4, "min"));
    CHECK(diatonicTriad(cMajor, 3) == chord(5, "maj"));
    CHECK(diatonicTriad(cMajor, 4) == chord(7, "maj"));
    CHECK(diatonicTriad(cMajor, 5) == chord(9, "min"));
    CHECK(diatonicTriad(cMajor, 6) == chord(11, "dim"));

    const Key aHarmonic = key('A', 0, Mode::HarmonicMinor);
    CHECK(diatonicTriad(aHarmonic, 0) == chord(9, "min"));
    CHECK(diatonicTriad(aHarmonic, 1) == chord(11, "dim"));
    CHECK(diatonicTriad(aHarmonic, 2) == chord(0, "aug"));
    CHECK(diatonicTriad(aHarmonic, 3) == chord(2, "min"));
    CHECK(diatonicTriad(aHarmonic, 4) == chord(4, "maj"));
    CHECK(diatonicTriad(aHarmonic, 5) == chord(5, "maj"));
    CHECK(diatonicTriad(aHarmonic, 6) == chord(8, "dim"));
}

TEST_CASE("diatonicTriad: the degree wraps modulo 7") {
    const Key cMajor = key('C', 0, Mode::Ionian);
    CHECK(diatonicTriad(cMajor, 7) == chord(0, "maj"));
    CHECK(diatonicTriad(cMajor, -1) == chord(11, "dim"));
    CHECK(diatonicTriad(cMajor, 15) == chord(2, "min"));
}

TEST_CASE("diatonicTriad: every degree of every key is a stacked third fully in key") {
    for (PitchClass pc = 0; pc < 12; ++pc) {
        for (int m = 0; m < kNumModes; ++m) {
            const Key k{canonicalTonic(pc, Mode(m)), Mode(m)};
            const auto scale = scalePitchClasses(k.tonicPc(), k.mode);
            for (int d = 0; d < 7; ++d) {
                CAPTURE(pc);
                CAPTURE(m);
                CAPTURE(d);
                const Chord t = diatonicTriad(k, d);
                const std::string_view id = t.type->id;
                CHECK((id == "maj" || id == "min" || id == "dim" || id == "aug"));
                const auto sd = static_cast<std::size_t>(d);
                CHECK(pitchClasses(t) == std::vector<PitchClass>{scale[sd], scale[(sd + 2) % 7], scale[(sd + 4) % 7]});
                CHECK(fullyInKey(t, k));
            }
        }
    }
}
