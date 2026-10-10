#include "doctest.h"

#include "chordlab/pitch.h"
#include "chordlab/scale.h"

#include <array>
#include <cstdlib>
#include <set>
#include <string>

using namespace chordlab;

namespace {

Key key(char letter, int alter, Mode mode) {
    return Key{Tonic{letter, alter}, mode};
}

constexpr std::array<Mode, kNumModes> kAllModes{
    Mode::Ionian,     Mode::Dorian,  Mode::Phrygian,      Mode::Lydian,       Mode::Mixolydian,
    Mode::Aeolian,    Mode::Locrian, Mode::HarmonicMinor, Mode::MelodicMinor,
};

// Natural pitch class of each letter, indexed 'A'..'G'. Mirrors the engine's table so the
// round-trip tests do not depend on the implementation under test.
PitchClass naturalPc(char letter) {
    constexpr std::array<PitchClass, 7> table{9, 11, 0, 2, 4, 5, 7};
    return table[static_cast<std::size_t>(letter - 'A')];
}

}  // namespace

// --- Tonic / Key -------------------------------------------------------------

TEST_CASE("Tonic: equality compares letter and alter") {
    const Tonic fSharp{'F', +1};
    const Tonic gFlat{'G', -1};
    const Tonic alsoFSharp{'F', +1};
    CHECK(fSharp == alsoFSharp);
    CHECK_FALSE(fSharp == gFlat);
    CHECK(fSharp != gFlat);
}

TEST_CASE("Key: tonicPc derives the pitch class from the spelled tonic") {
    CHECK(key('C', 0, Mode::Ionian).tonicPc() == 0);
    CHECK(key('E', -1, Mode::Ionian).tonicPc() == 3);
    CHECK(key('F', +1, Mode::Ionian).tonicPc() == 6);
    CHECK(key('G', -1, Mode::Ionian).tonicPc() == 6);
    CHECK(key('B', +1, Mode::Ionian).tonicPc() == 0);
    CHECK(key('C', -1, Mode::Ionian).tonicPc() == 11);
}

TEST_CASE("Key: equality compares tonic and mode") {
    CHECK(key('A', 0, Mode::Aeolian) == key('A', 0, Mode::Aeolian));
    CHECK(key('A', 0, Mode::Aeolian) != key('A', 0, Mode::Ionian));
    CHECK(key('A', 0, Mode::Aeolian) != key('A', -1, Mode::Aeolian));
    CHECK(key('F', +1, Mode::Ionian) != key('G', -1, Mode::Ionian));
}

// --- signatureOf ---------------------------------------------------------------

TEST_CASE("signatureOf: Ionian keys follow the circle of fifths") {
    CHECK(signatureOf(key('C', 0, Mode::Ionian)) == 0);
    CHECK(signatureOf(key('G', 0, Mode::Ionian)) == 1);
    CHECK(signatureOf(key('D', 0, Mode::Ionian)) == 2);
    CHECK(signatureOf(key('A', 0, Mode::Ionian)) == 3);
    CHECK(signatureOf(key('E', 0, Mode::Ionian)) == 4);
    CHECK(signatureOf(key('B', 0, Mode::Ionian)) == 5);
    CHECK(signatureOf(key('F', +1, Mode::Ionian)) == 6);
    CHECK(signatureOf(key('C', +1, Mode::Ionian)) == 7);
    CHECK(signatureOf(key('F', 0, Mode::Ionian)) == -1);
    CHECK(signatureOf(key('B', -1, Mode::Ionian)) == -2);
    CHECK(signatureOf(key('E', -1, Mode::Ionian)) == -3);
    CHECK(signatureOf(key('A', -1, Mode::Ionian)) == -4);
    CHECK(signatureOf(key('D', -1, Mode::Ionian)) == -5);
    CHECK(signatureOf(key('G', -1, Mode::Ionian)) == -6);
    CHECK(signatureOf(key('C', -1, Mode::Ionian)) == -7);
}

TEST_CASE("signatureOf: the modes of C major share its empty signature") {
    CHECK(signatureOf(key('C', 0, Mode::Ionian)) == 0);
    CHECK(signatureOf(key('D', 0, Mode::Dorian)) == 0);
    CHECK(signatureOf(key('E', 0, Mode::Phrygian)) == 0);
    CHECK(signatureOf(key('F', 0, Mode::Lydian)) == 0);
    CHECK(signatureOf(key('G', 0, Mode::Mixolydian)) == 0);
    CHECK(signatureOf(key('A', 0, Mode::Aeolian)) == 0);
    CHECK(signatureOf(key('B', 0, Mode::Locrian)) == 0);
    CHECK(signatureOf(key('A', 0, Mode::HarmonicMinor)) == 0);
    CHECK(signatureOf(key('A', 0, Mode::MelodicMinor)) == 0);
}

TEST_CASE("signatureOf: mode offsets relative to Ionian on the same tonic") {
    CHECK(signatureOf(key('C', 0, Mode::Lydian)) == 1);
    CHECK(signatureOf(key('C', 0, Mode::Mixolydian)) == -1);
    CHECK(signatureOf(key('C', 0, Mode::Dorian)) == -2);
    CHECK(signatureOf(key('C', 0, Mode::Aeolian)) == -3);
    CHECK(signatureOf(key('C', 0, Mode::HarmonicMinor)) == -3);
    CHECK(signatureOf(key('C', 0, Mode::MelodicMinor)) == -3);
    CHECK(signatureOf(key('C', 0, Mode::Phrygian)) == -4);
    CHECK(signatureOf(key('C', 0, Mode::Locrian)) == -5);
}

TEST_CASE("signatureOf: is unbounded for non-canonical keys") {
    CHECK(signatureOf(key('E', -1, Mode::Locrian)) == -8);
    CHECK(signatureOf(key('D', +1, Mode::Locrian)) == 4);
    CHECK(signatureOf(key('F', -1, Mode::Lydian)) == -7);
    CHECK(signatureOf(key('B', +1, Mode::Ionian)) == 12);
}

// --- canonicalTonic ---------------------------------------------------------

TEST_CASE("canonicalTonic: plan cases") {
    const Tonic fSharp{'F', +1};
    const Tonic eFlat{'E', -1};
    const Tonic dSharp{'D', +1};
    CHECK(canonicalTonic(6, Mode::Ionian) == fSharp);
    CHECK(canonicalTonic(3, Mode::Ionian) == eFlat);
    CHECK(canonicalTonic(3, Mode::Locrian) == dSharp);  // sig +4, not Eb Locrian's -8
}

TEST_CASE("canonicalTonic: naturals win when their signature is the smallest") {
    const Tonic c{'C', 0};
    const Tonic d{'D', 0};
    const Tonic f{'F', 0};
    CHECK(canonicalTonic(0, Mode::Ionian) == c);
    CHECK(canonicalTonic(2, Mode::Locrian) == d);
    CHECK(canonicalTonic(5, Mode::Ionian) == f);
    CHECK(canonicalTonic(0, Mode::Locrian) == c);  // C Locrian (-5) over B# Locrian (+7)
}

TEST_CASE("canonicalTonic: flats win when the sharp spelling would overshoot") {
    const Tonic dFlat{'D', -1};
    const Tonic bFlat{'B', -1};
    const Tonic gFlat{'G', -1};
    const Tonic eFlat{'E', -1};
    CHECK(canonicalTonic(1, Mode::Ionian) == dFlat);    // Db (-5) over C# (+7)
    CHECK(canonicalTonic(10, Mode::Aeolian) == bFlat);  // Bb minor (-5) over A# minor (+7)
    CHECK(canonicalTonic(6, Mode::Lydian) == gFlat);    // Gb Lydian (-5) over F# Lydian (+7)
    CHECK(canonicalTonic(3, Mode::Dorian) == eFlat);    // Eb Dorian (-5) over D# Dorian (+7)
}

TEST_CASE("canonicalTonic: ties at six accidentals prefer the sharp spelling") {
    const Tonic fSharp{'F', +1};
    const Tonic cSharp{'C', +1};
    const Tonic gSharp{'G', +1};
    const Tonic dSharp{'D', +1};
    const Tonic aSharp{'A', +1};
    const Tonic eSharp{'E', +1};
    const Tonic b{'B', 0};
    CHECK(canonicalTonic(6, Mode::Ionian) == fSharp);         // F# (+6) over Gb (-6)
    CHECK(canonicalTonic(1, Mode::Mixolydian) == cSharp);     // C# (+6) over Db (-6)
    CHECK(canonicalTonic(8, Mode::Dorian) == gSharp);         // G# (+6) over Ab (-6)
    CHECK(canonicalTonic(3, Mode::Aeolian) == dSharp);        // D# (+6) over Eb (-6)
    CHECK(canonicalTonic(3, Mode::HarmonicMinor) == dSharp);
    CHECK(canonicalTonic(3, Mode::MelodicMinor) == dSharp);
    CHECK(canonicalTonic(10, Mode::Phrygian) == aSharp);      // A# (+6) over Bb (-6)
    CHECK(canonicalTonic(5, Mode::Locrian) == eSharp);        // E# (+6) over F (-6)
    CHECK(canonicalTonic(11, Mode::Lydian) == b);             // B (+6) over Cb (-6)
}

TEST_CASE("canonicalTonic: every pc x mode stays within six accidentals and round-trips the pc") {
    for (PitchClass pc = 0; pc < 12; ++pc) {
        for (const Mode mode : kAllModes) {
            const Key k{canonicalTonic(pc, mode), mode};
            CAPTURE(pc);
            CAPTURE(static_cast<int>(mode));
            CHECK(std::abs(signatureOf(k)) <= 6);
            CHECK(k.tonicPc() == pc);
            CHECK(k.tonic.letter >= 'A');
            CHECK(k.tonic.letter <= 'G');
            CHECK(k.tonic.alter >= -1);
            CHECK(k.tonic.alter <= 1);
        }
    }
}

TEST_CASE("canonicalTonic: reduces the pitch class modulo 12") {
    const Tonic fSharp{'F', +1};
    CHECK(canonicalTonic(18, Mode::Ionian) == fSharp);
    CHECK(canonicalTonic(-6, Mode::Ionian) == fSharp);
}

// --- Spelling ------------------------------------------------------------------

TEST_CASE("Spelling::text renders ASCII accidentals") {
    CHECK(Spelling{'C', 0}.text() == "C");
    CHECK(Spelling{'F', +1}.text() == "F#");
    CHECK(Spelling{'B', -1}.text() == "Bb");
    CHECK(Spelling{'F', +2}.text() == "Fx");
    CHECK(Spelling{'B', -2}.text() == "Bbb");
}

TEST_CASE("Spelling: equality compares letter and alter") {
    const Spelling a{'G', +1};
    const Spelling b{'G', +1};
    const Spelling c{'A', -1};
    CHECK(a == b);
    CHECK(a != c);
}

// --- spellDegree ----------------------------------------------------------------

TEST_CASE("spellDegree: plan cases") {
    CHECK(spellDegree(key('C', 0, Mode::HarmonicMinor), 6).text() == "B");
    CHECK(spellDegree(key('A', 0, Mode::HarmonicMinor), 6).text() == "G#");
    CHECK(spellDegree(key('F', 0, Mode::Ionian), 3).text() == "Bb");
    CHECK(spellDegree(key('C', 0, Mode::MelodicMinor), 5).text() == "A");
}

TEST_CASE("spellDegree: walks the letters from the tonic") {
    const Key eFlatMajor = key('E', -1, Mode::Ionian);
    CHECK(spellDegree(eFlatMajor, 0).text() == "Eb");
    CHECK(spellDegree(eFlatMajor, 1).text() == "F");
    CHECK(spellDegree(eFlatMajor, 2).text() == "G");
    CHECK(spellDegree(eFlatMajor, 3).text() == "Ab");
    CHECK(spellDegree(eFlatMajor, 4).text() == "Bb");
    CHECK(spellDegree(eFlatMajor, 5).text() == "C");
    CHECK(spellDegree(eFlatMajor, 6).text() == "D");

    const Key dSharpLocrian = key('D', +1, Mode::Locrian);
    CHECK(spellDegree(dSharpLocrian, 0).text() == "D#");
    CHECK(spellDegree(dSharpLocrian, 1).text() == "E");
    CHECK(spellDegree(dSharpLocrian, 2).text() == "F#");
    CHECK(spellDegree(dSharpLocrian, 3).text() == "G#");
    CHECK(spellDegree(dSharpLocrian, 4).text() == "A");
    CHECK(spellDegree(dSharpLocrian, 5).text() == "B");
    CHECK(spellDegree(dSharpLocrian, 6).text() == "C#");
}

TEST_CASE("spellDegree: double sharps in sharp-side minor keys") {
    CHECK(spellDegree(key('D', +1, Mode::HarmonicMinor), 6).text() == "Cx");
    CHECK(spellDegree(key('D', +1, Mode::MelodicMinor), 5).text() == "B#");
    CHECK(spellDegree(key('D', +1, Mode::MelodicMinor), 6).text() == "Cx");
}

TEST_CASE("spellDegree: double flats in flat-side non-canonical keys") {
    CHECK(spellDegree(key('C', -1, Mode::Locrian), 1).text() == "Dbb");
    CHECK(spellDegree(key('F', -1, Mode::Ionian), 3).text() == "Bbb");
}

TEST_CASE("spellDegree: degree is taken modulo 7") {
    const Key cMajor = key('C', 0, Mode::Ionian);
    CHECK(spellDegree(cMajor, 7).text() == "C");
    CHECK(spellDegree(cMajor, 8).text() == "D");
    CHECK(spellDegree(cMajor, -1).text() == "B");
}

TEST_CASE("spellDegree: every canonical key uses each letter once and hits the scale's pitch classes") {
    for (PitchClass pc = 0; pc < 12; ++pc) {
        for (const Mode mode : kAllModes) {
            const Key k{canonicalTonic(pc, mode), mode};
            const auto pcs = scalePitchClasses(pc, mode);
            std::set<char> letters;
            CAPTURE(pc);
            CAPTURE(static_cast<int>(mode));
            for (int degree = 0; degree < 7; ++degree) {
                const Spelling s = spellDegree(k, degree);
                CAPTURE(degree);
                CHECK(s.letter >= 'A');
                CHECK(s.letter <= 'G');
                CHECK(s.alter >= -2);
                CHECK(s.alter <= 2);
                CHECK(pitchClassOf(naturalPc(s.letter) + s.alter) == pcs[static_cast<std::size_t>(degree)]);
                letters.insert(s.letter);
            }
            CHECK(letters.size() == 7);
            CHECK(spellDegree(k, 0).letter == k.tonic.letter);
            CHECK(spellDegree(k, 0).alter == k.tonic.alter);
        }
    }
}

// --- spell -------------------------------------------------------------------------

TEST_CASE("spell: plan cases") {
    CHECK(spell(10, key('D', 0, Mode::Ionian)).text() == "A#");
    CHECK(spell(6, key('E', -1, Mode::Ionian)).text() == "Gb");
}

TEST_CASE("spell: scale members use their degree spelling") {
    CHECK(spell(1, key('D', -1, Mode::Ionian)).text() == "Db");
    CHECK(spell(8, key('A', 0, Mode::HarmonicMinor)).text() == "G#");
    CHECK(spell(11, key('C', 0, Mode::HarmonicMinor)).text() == "B");
    CHECK(spell(3, key('D', +1, Mode::Locrian)).text() == "D#");
    CHECK(spell(9, key('D', +1, Mode::Locrian)).text() == "A");
    CHECK(spell(2, key('D', +1, Mode::HarmonicMinor)).text() == "Cx");
}

TEST_CASE("spell: chromatic black keys take a sharp in sharp-side keys and a flat in flat-side keys") {
    CHECK(spell(1, key('C', 0, Mode::Ionian)).text() == "C#");   // signature 0 counts as sharp-side
    CHECK(spell(1, key('F', 0, Mode::Ionian)).text() == "Db");
    CHECK(spell(8, key('G', 0, Mode::Ionian)).text() == "G#");
    CHECK(spell(8, key('B', -1, Mode::Ionian)).text() == "Ab");
    CHECK(spell(6, key('A', 0, Mode::HarmonicMinor)).text() == "F#");
    CHECK(spell(6, key('C', 0, Mode::HarmonicMinor)).text() == "Gb");
    CHECK(spell(10, key('C', 0, Mode::HarmonicMinor)).text() == "Bb");
}

TEST_CASE("spell: chromatic white keys keep their natural name") {
    CHECK(spell(5, key('G', 0, Mode::Ionian)).text() == "F");   // not E#
    CHECK(spell(0, key('D', 0, Mode::Ionian)).text() == "C");   // not B#
    CHECK(spell(4, key('E', -1, Mode::Ionian)).text() == "E");  // not Fb
    CHECK(spell(11, key('F', 0, Mode::Ionian)).text() == "B");  // not Cb
    CHECK(spell(7, key('A', 0, Mode::Ionian)).text() == "G");
    CHECK(spell(9, key('C', 0, Mode::HarmonicMinor)).text() == "A");
}

TEST_CASE("spell: reduces the pitch class modulo 12") {
    const Key cMajor = key('C', 0, Mode::Ionian);
    CHECK(spell(13, cMajor).text() == "C#");
    CHECK(spell(-1, cMajor).text() == "B");
}
