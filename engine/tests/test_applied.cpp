#include "doctest.h"

#include "chordlab/applied.h"
#include "chordlab/chord.h"
#include "chordlab/pitch.h"
#include "chordlab/scale.h"
#include "chordlab/symbol.h"

#include <cstddef>
#include <cstdlib>
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

// Pitch classes by name, so the cases below read like chord names.
constexpr PitchClass C = 0, Db = 1, D = 2, Eb = 3, E = 4, F = 5, Fs = 6, G = 7, Ab = 8, A = 9, Bb = 10,
                     B = 11;
constexpr PitchClass Cs = Db, Ds = Eb, Gs = Ab;

const Key kCIonian = key('C', 0, Mode::Ionian);
const Key kAAeolian = key('A', 0, Mode::Aeolian);

/// One expected entry: the chord, its target degree, label and Klimper-style symbol in its target key.
struct Expected {
    PitchClass root;
    const char* type;
    int targetDegree;
    const char* label;
    const char* symbol;
};

void checkList(const Key& k, const std::vector<Expected>& expected) {
    const std::vector<AppliedChord> applied = appliedChords(k);
    REQUIRE(applied.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CAPTURE(i);
        CAPTURE(expected[i].label);
        CHECK(applied[i].chord == chord(expected[i].root, expected[i].type));
        CHECK(applied[i].targetDegree == expected[i].targetDegree);
        CHECK(applied[i].label == expected[i].label);
        CHECK(chordSymbol(applied[i].chord, applied[i].spellingKey) == expected[i].symbol);
    }
}

/// Letter `steps` letters above `letter` in the cycle A..G.
char letterUp(char letter, int steps) {
    return static_cast<char>('A' + ((letter - 'A') + steps) % 7);
}

}  // namespace

// --- C major -------------------------------------------------------------------------------------

TEST_CASE("appliedChords: C major has V7 and vii°7 of ii, iii, IV, V and vi, in that order") {
    checkList(kCIonian, {
                            {A, "7", 1, "V7/ii", "A7"},
                            {Cs, "dim7", 1, "vii°7/ii", "C#dim7"},
                            {B, "7", 2, "V7/iii", "B7"},
                            {Ds, "dim7", 2, "vii°7/iii", "D#dim7"},
                            {C, "7", 3, "V7/IV", "C7"},
                            {E, "dim7", 3, "vii°7/IV", "Edim7"},
                            {D, "7", 4, "V7/V", "D7"},
                            {Fs, "dim7", 4, "vii°7/V", "F#dim7"},
                            {E, "7", 5, "V7/vi", "E7"},
                            {Gs, "dim7", 5, "vii°7/vi", "G#dim7"},
                        });
}

TEST_CASE("appliedChords: nothing tonicises the diminished vii° of C major") {
    for (const AppliedChord& a : appliedChords(kCIonian)) {
        CAPTURE(a.label);
        CHECK(a.targetDegree != 6);
        CHECK(a.targetDegree != 0);
    }
}

TEST_CASE("appliedChords: the leading-tone chord spells as the leading tone of its target") {
    const std::vector<AppliedChord> applied = appliedChords(kCIonian);
    REQUIRE(applied.size() == 10);
    CHECK(chordSymbol(applied[1].chord, applied[1].spellingKey, SymbolStyle::Standard) == "C#°7");
    CHECK(chordSymbol(applied[1].chord, applied[1].spellingKey, SymbolStyle::Klimper) == "C#dim7");
    CHECK(chordSymbol(applied[9].chord, applied[9].spellingKey, SymbolStyle::Standard) == "G#°7");
    // Spelled in C major itself the chromatic root is the lowered second: that is why spellingKey exists.
    CHECK(chordSymbol(applied[1].chord, kCIonian) == "Dbdim7");
    // The spelling key is the major key on the target's root, whatever the target's quality.
    CHECK(applied[1].spellingKey == key('D', 0, Mode::Ionian));
    CHECK(applied[5].spellingKey == key('F', 0, Mode::Ionian));
    CHECK(applied[0].spellingKey == key('D', 0, Mode::Ionian));
    CHECK(applied[9].spellingKey == key('A', 0, Mode::Ionian));
}

TEST_CASE("appliedChords: every applied chord of C major has a note outside the key") {
    for (const AppliedChord& a : appliedChords(kCIonian)) {
        CAPTURE(a.label);
        CHECK_FALSE(fullyInKey(a.chord, kCIonian));
    }
}

// --- A minor -------------------------------------------------------------------------------------

TEST_CASE("appliedChords: A Aeolian tonicises III, iv, v, VI and VII, with parallel-major numerals") {
    checkList(kAAeolian, {
                             {G, "7", 2, "V7/bIII", "G7"},
                             {B, "dim7", 2, "vii°7/bIII", "Bdim7"},
                             {A, "7", 3, "V7/iv", "A7"},
                             {Cs, "dim7", 3, "vii°7/iv", "C#dim7"},
                             {B, "7", 4, "V7/v", "B7"},
                             {Ds, "dim7", 4, "vii°7/v", "D#dim7"},
                             {C, "7", 5, "V7/bVI", "C7"},
                             {E, "dim7", 5, "vii°7/bVI", "Edim7"},
                             {D, "7", 6, "V7/bVII", "D7"},
                             {Fs, "dim7", 6, "vii°7/bVII", "F#dim7"},
                         });
}

TEST_CASE("appliedChords: in A Aeolian only V7/bIII happens to be diatonic") {
    for (const AppliedChord& a : appliedChords(kAAeolian)) {
        CAPTURE(a.label);
        CHECK(fullyInKey(a.chord, kAAeolian) == (a.label == "V7/bIII"));  // G7 = G B D F is VII7 too
    }
}

TEST_CASE("appliedChords: A harmonic minor skips both diminished degrees and tonicises III+") {
    checkList(key('A', 0, Mode::HarmonicMinor), {
                                                    {G, "7", 2, "V7/bIII+", "G7"},
                                                    {B, "dim7", 2, "vii°7/bIII+", "Bdim7"},
                                                    {A, "7", 3, "V7/iv", "A7"},
                                                    {Cs, "dim7", 3, "vii°7/iv", "C#dim7"},
                                                    {B, "7", 4, "V7/V", "B7"},
                                                    {Ds, "dim7", 4, "vii°7/V", "D#dim7"},
                                                    {C, "7", 5, "V7/bVI", "C7"},
                                                    {E, "dim7", 5, "vii°7/bVI", "Edim7"},
                                                });
}

// --- Other modes ------------------------------------------------------------------------------------

TEST_CASE("appliedChords: C Locrian tonicises every degree but its own diminished tonic") {
    checkList(key('C', 0, Mode::Locrian), {
                                              {Ab, "7", 1, "V7/bII", "Ab7"},
                                              {C, "dim7", 1, "vii°7/bII", "Cdim7"},
                                              {Bb, "7", 2, "V7/biii", "Bb7"},
                                              {D, "dim7", 2, "vii°7/biii", "Ddim7"},
                                              {C, "7", 3, "V7/iv", "C7"},
                                              {E, "dim7", 3, "vii°7/iv", "Edim7"},
                                              {Db, "7", 4, "V7/#IV", "Db7"},
                                              {F, "dim7", 4, "vii°7/#IV", "Fdim7"},
                                              {Eb, "7", 5, "V7/bVI", "Eb7"},
                                              {G, "dim7", 5, "vii°7/bVI", "Gdim7"},
                                              {F, "7", 6, "V7/bvii", "F7"},
                                              {A, "dim7", 6, "vii°7/bvii", "Adim7"},
                                          });
}

TEST_CASE("appliedChords: a target degree spelled with a double accidental falls back to a canonical key") {
    // Eb Locrian's fifth degree is Bbb (pc 9); its key is spelled A, not Bbb.
    const std::vector<AppliedChord> applied = appliedChords(key('E', -1, Mode::Locrian));
    bool found = false;
    for (const AppliedChord& a : applied) {
        if (a.targetDegree == 4) {
            found = true;
            CHECK(a.spellingKey.tonic == Tonic{'A', 0});
            CHECK(a.spellingKey.tonicPc() == A);
            if (a.label == "V7/#IV") {
                CHECK(chordSymbol(a.chord, a.spellingKey) == "E7");
            } else {
                CHECK(a.label == "vii°7/#IV");
                CHECK(chordSymbol(a.chord, a.spellingKey) == "G#dim7");
            }
        }
    }
    CHECK(found);
}

TEST_CASE("appliedChords: C# Lydian, a key of eight sharps, spells from minor or canonical keys where it must") {
    // C# D# E# Fx G# A# B#: II D# (D# major has nine sharps), iii E#m (eleven), #iv° skipped, V G#
    // (eight), vi A#m (ten), vii B#m (twelve).
    checkList(key('C', 1, Mode::Lydian), {
                                             {Bb, "7", 1, "V7/II", "A#7"},        // D# Aeolian (six sharps)
                                             {D, "dim7", 1, "vii°7/II", "Ddim7"},  // Cx folds to D
                                             {C, "7", 2, "V7/iii", "C7"},          // E# Aeolian has eight: canonical F
                                             {E, "dim7", 2, "vii°7/iii", "Edim7"},
                                             {Eb, "7", 4, "V7/V", "D#7"},          // G# Aeolian (five sharps)
                                             {G, "dim7", 4, "vii°7/V", "Gdim7"},   // Fx folds to G
                                             {F, "7", 5, "V7/vi", "E#7"},          // A# Aeolian (seven sharps)
                                             {A, "dim7", 5, "vii°7/vi", "Adim7"},  // Gx folds to A
                                             {G, "7", 6, "V7/vii", "G7"},          // B# Aeolian has nine: canonical C
                                             {B, "dim7", 6, "vii°7/vii", "Bdim7"},
                                         });
    const std::vector<AppliedChord> applied = appliedChords(key('C', 1, Mode::Lydian));
    REQUIRE(applied.size() == 10);
    CHECK(applied[0].spellingKey == key('D', 1, Mode::Aeolian));
    CHECK(applied[2].spellingKey == key('F', 0, Mode::Ionian));
    CHECK(applied[4].spellingKey == key('G', 1, Mode::Aeolian));
    CHECK(applied[6].spellingKey == key('A', 1, Mode::Aeolian));
    CHECK(applied[8].spellingKey == key('C', 0, Mode::Ionian));
}

TEST_CASE("appliedChords: Cb major, a key of seven flats, keeps its own spelling") {
    // Cb Db Eb Fb Gb Ab Bb: ii Dbm (Db major has five flats), iii Ebm (three), IV Fb (eight: Fb Aeolian
    // has eleven, so canonical E), V Gb (six), vi Abm (four).
    checkList(key('C', -1, Mode::Ionian), {
                                              {Ab, "7", 1, "V7/ii", "Ab7"},
                                              {C, "dim7", 1, "vii°7/ii", "Cdim7"},
                                              {Bb, "7", 2, "V7/iii", "Bb7"},
                                              {D, "dim7", 2, "vii°7/iii", "Ddim7"},
                                              {B, "7", 3, "V7/IV", "B7"},
                                              {Eb, "dim7", 3, "vii°7/IV", "D#dim7"},
                                              {Db, "7", 4, "V7/V", "Db7"},
                                              {F, "dim7", 4, "vii°7/V", "Fdim7"},
                                              {Eb, "7", 5, "V7/vi", "Eb7"},
                                              {G, "dim7", 5, "vii°7/vi", "Gdim7"},
                                          });
}

// --- Properties -------------------------------------------------------------------------------------

TEST_CASE("appliedChords: every entry of every key is well-formed, ordered and spelled from its target") {
    for (PitchClass pc = 0; pc < 12; ++pc) {
        for (int m = 0; m < kNumModes; ++m) {
            const Key k{canonicalTonic(pc, Mode(m)), Mode(m)};
            CAPTURE(pc);
            CAPTURE(m);
            const std::vector<AppliedChord> applied = appliedChords(k);
            CHECK(applied.size() % 2 == 0);
            CHECK(applied.size() >= 8);  // at most two of the six degrees are diminished
            CHECK(applied.size() <= 12);
            for (std::size_t i = 0; i < applied.size(); ++i) {
                const AppliedChord& a = applied[i];
                CAPTURE(i);
                CAPTURE(a.label);
                REQUIRE(a.targetDegree >= 1);
                REQUIRE(a.targetDegree <= 6);
                const Chord target = diatonicTriad(k, a.targetDegree);
                CHECK(std::string_view(target.type->id) != "dim");
                const std::string numeral = romanNumeral(target, k);
                const bool isDominant = i % 2 == 0;
                if (isDominant) {
                    CHECK(a.label == "V7/" + numeral);
                    CHECK(a.chord == chord(pitchClassOf(target.root + 7), "7"));
                } else {
                    CHECK(a.label == "vii°7/" + numeral);
                    CHECK(a.chord == chord(pitchClassOf(target.root - 1), "dim7"));
                    CHECK(applied[i - 1].targetDegree == a.targetDegree);
                }
                if (i >= 2) {
                    CHECK(applied[i - 2].targetDegree < a.targetDegree);
                }
                // The spelling key sits on the target's root, with a valid single-accidental tonic and a
                // scale with no double accidentals.
                CHECK(a.spellingKey.tonicPc() == target.root);
                CHECK(a.spellingKey.tonic.alter >= -1);
                CHECK(a.spellingKey.tonic.alter <= 1);
                CHECK(std::abs(signatureOf(a.spellingKey)) <= 7);
                CHECK((a.spellingKey.mode == Mode::Ionian || a.spellingKey.mode == Mode::Aeolian));
                // Spelled in that key, V7 sits on its fifth degree, exactly; vii°7 on the seventh of its
                // parallel major, exactly in a major spelling key and folded to the natural of the tonic's
                // letter in a minor one (chosen only when that seventh is a double sharp).
                const std::string symbol = chordSymbol(a.chord, a.spellingKey);
                if (isDominant) {
                    const std::string expected = spellDegree(a.spellingKey, 4).text();
                    CHECK(symbol == expected + "7");
                    CHECK(symbol[0] == letterUp(a.spellingKey.tonic.letter, 4));
                } else if (a.spellingKey.mode == Mode::Ionian) {
                    const std::string expected = spellDegree(a.spellingKey, 6).text();
                    CHECK(symbol == expected + "dim7");
                    CHECK(symbol[0] == letterUp(a.spellingKey.tonic.letter, 6));
                } else {
                    CHECK(spellDegree(Key{a.spellingKey.tonic, Mode::Ionian}, 6).alter == 2);
                    CHECK(symbol == std::string(1, a.spellingKey.tonic.letter) + "dim7");
                }
            }
        }
    }
}
