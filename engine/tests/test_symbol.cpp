#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/pitch.h"
#include "chordlab/scale.h"
#include "chordlab/symbol.h"

#include <array>
#include <string>
#include <string_view>

using namespace chordlab;

namespace {

Key key(char letter, int alter, Mode mode) {
    return Key{Tonic{letter, alter}, mode};
}

Chord chord(PitchClass root, std::string_view typeId) {
    return Chord{root, &chordType(typeId)};
}

constexpr std::array<Mode, kNumModes> kAllModes{
    Mode::Ionian,     Mode::Dorian,  Mode::Phrygian,      Mode::Lydian,       Mode::Mixolydian,
    Mode::Aeolian,    Mode::Locrian, Mode::HarmonicMinor, Mode::MelodicMinor,
};

// Pitch classes by name, so the cases below read like chord names.
constexpr PitchClass C = 0, Db = 1, D = 2, Eb = 3, E = 4, F = 5, Fs = 6, G = 7, Ab = 8, A = 9, Bb = 10,
                     B = 11;

const Key kCIonian = key('C', 0, Mode::Ionian);
const Key kCAeolian = key('C', 0, Mode::Aeolian);
const Key kAAeolian = key('A', 0, Mode::Aeolian);

bool isNumeralLetter(char c) {
    return c == 'I' || c == 'V' || c == 'i' || c == 'v';
}

}  // namespace

// --- chordSymbol ------------------------------------------------------------------

TEST_CASE("chordSymbol: Klimper style in C Ionian") {
    CHECK(chordSymbol(chord(C, "maj"), kCIonian, SymbolStyle::Klimper) == "C");
    CHECK(chordSymbol(chord(D, "min"), kCIonian, SymbolStyle::Klimper) == "Dm");
    CHECK(chordSymbol(chord(E, "min"), kCIonian, SymbolStyle::Klimper) == "Em");
    CHECK(chordSymbol(chord(F, "maj"), kCIonian, SymbolStyle::Klimper) == "F");
    CHECK(chordSymbol(chord(G, "maj"), kCIonian, SymbolStyle::Klimper) == "G");
    CHECK(chordSymbol(chord(A, "min"), kCIonian, SymbolStyle::Klimper) == "Am");
    CHECK(chordSymbol(chord(B, "dim"), kCIonian, SymbolStyle::Klimper) == "Bmb5");
    CHECK(chordSymbol(chord(C, "maj7"), kCIonian, SymbolStyle::Klimper) == "CΔ7");
    CHECK(chordSymbol(chord(G, "7"), kCIonian, SymbolStyle::Klimper) == "G7");
    CHECK(chordSymbol(chord(B, "m7b5"), kCIonian, SymbolStyle::Klimper) == "Bm7b5");
    CHECK(chordSymbol(chord(A, "madd9"), kCIonian, SymbolStyle::Klimper) == "Amadd9");
    CHECK(chordSymbol(chord(C, "5"), kCIonian, SymbolStyle::Klimper) == "C5");
    CHECK(chordSymbol(chord(C, "69"), kCIonian, SymbolStyle::Klimper) == "C6/9");
    CHECK(chordSymbol(chord(C, "aug"), kCIonian, SymbolStyle::Klimper) == "C+");
}

TEST_CASE("chordSymbol: Klimper style in C Aeolian spells flats") {
    CHECK(chordSymbol(chord(Eb, "maj"), kCAeolian, SymbolStyle::Klimper) == "Eb");
    CHECK(chordSymbol(chord(Ab, "6"), kCAeolian, SymbolStyle::Klimper) == "Ab6");
    CHECK(chordSymbol(chord(Bb, "7"), kCAeolian, SymbolStyle::Klimper) == "Bb7");
    CHECK(chordSymbol(chord(D, "m7b5"), kCAeolian, SymbolStyle::Klimper) == "Dm7b5");
}

TEST_CASE("chordSymbol: Standard style uses ° ø7 °7 and keeps the rest") {
    CHECK(chordSymbol(chord(B, "dim"), kCIonian, SymbolStyle::Standard) == "B°");
    CHECK(chordSymbol(chord(B, "m7b5"), kCIonian, SymbolStyle::Standard) == "Bø7");
    CHECK(chordSymbol(chord(B, "dim7"), kCIonian, SymbolStyle::Standard) == "B°7");
    CHECK(chordSymbol(chord(C, "mMaj7"), kCIonian, SymbolStyle::Standard) == "CmΔ7");
    CHECK(chordSymbol(chord(C, "augMaj7"), kCIonian, SymbolStyle::Standard) == "C+Δ7");
    CHECK(chordSymbol(chord(C, "aug"), kCIonian, SymbolStyle::Standard) == "C+");
}

TEST_CASE("chordSymbol: defaults to Klimper style") {
    CHECK(chordSymbol(chord(B, "dim"), kCIonian) == "Bmb5");
    CHECK(chordSymbol(chord(B, "m7b5"), kCIonian) == "Bm7b5");
}

TEST_CASE("chordSymbol: root spelling follows the spelling key") {
    CHECK(chordSymbol(chord(Fs, "maj"), key('G', 0, Mode::Ionian)) == "F#");
    CHECK(chordSymbol(chord(Fs, "maj"), key('E', -1, Mode::Ionian)) == "Gb");
    CHECK(chordSymbol(chord(Fs, "dim"), key('E', -1, Mode::Ionian), SymbolStyle::Standard) == "Gb°");
    CHECK(chordSymbol(chord(Bb, "maj"), key('D', 0, Mode::Ionian)) == "A#");
}

// --- romanNumeral ------------------------------------------------------------------

TEST_CASE("romanNumeral: Standard style, diatonic chords of C Ionian") {
    CHECK(romanNumeral(chord(C, "maj"), kCIonian, SymbolStyle::Standard) == "I");
    CHECK(romanNumeral(chord(D, "min"), kCIonian, SymbolStyle::Standard) == "ii");
    CHECK(romanNumeral(chord(E, "min"), kCIonian, SymbolStyle::Standard) == "iii");
    CHECK(romanNumeral(chord(F, "maj"), kCIonian, SymbolStyle::Standard) == "IV");
    CHECK(romanNumeral(chord(G, "maj"), kCIonian, SymbolStyle::Standard) == "V");
    CHECK(romanNumeral(chord(A, "min"), kCIonian, SymbolStyle::Standard) == "vi");
    CHECK(romanNumeral(chord(B, "dim"), kCIonian, SymbolStyle::Standard) == "vii°");
    CHECK(romanNumeral(chord(G, "7"), kCIonian, SymbolStyle::Standard) == "V7");
    CHECK(romanNumeral(chord(B, "m7b5"), kCIonian, SymbolStyle::Standard) == "viiø7");
    CHECK(romanNumeral(chord(C, "maj7"), kCIonian, SymbolStyle::Standard) == "IΔ7");
    CHECK(romanNumeral(chord(C, "5"), kCIonian, SymbolStyle::Standard) == "I5");
    CHECK(romanNumeral(chord(C, "sus4"), kCIonian, SymbolStyle::Standard) == "Isus4");
}

TEST_CASE("romanNumeral: Standard style, chromatic and borrowed roots in C Ionian") {
    CHECK(romanNumeral(chord(Ab, "maj"), kCIonian, SymbolStyle::Standard) == "bVI");
    CHECK(romanNumeral(chord(Bb, "maj"), kCIonian, SymbolStyle::Standard) == "bVII");
    CHECK(romanNumeral(chord(D, "maj"), kCIonian, SymbolStyle::Standard) == "II");  // root in key
    CHECK(romanNumeral(chord(Fs, "dim"), kCIonian, SymbolStyle::Standard) == "#iv°");
    CHECK(romanNumeral(chord(Eb, "maj"), kCIonian, SymbolStyle::Standard) == "bIII");
    CHECK(romanNumeral(chord(A, "maj"), kCIonian, SymbolStyle::Standard) == "VI");
    CHECK(romanNumeral(chord(E, "7"), kCIonian, SymbolStyle::Standard) == "III7");
    CHECK(romanNumeral(chord(F, "min"), kCIonian, SymbolStyle::Standard) == "iv");
    CHECK(romanNumeral(chord(Db, "maj"), kCIonian, SymbolStyle::Standard) == "bII");
}

TEST_CASE("romanNumeral: A Aeolian") {
    CHECK(romanNumeral(chord(A, "min"), kAAeolian, SymbolStyle::Standard) == "i");
    CHECK(romanNumeral(chord(G, "maj"), kAAeolian, SymbolStyle::Standard) == "VII");
    CHECK(romanNumeral(chord(E, "maj"), kAAeolian, SymbolStyle::Standard) == "V");
    CHECK(romanNumeral(chord(D, "min"), kAAeolian, SymbolStyle::Standard) == "iv");
    CHECK(romanNumeral(chord(F, "maj"), kAAeolian, SymbolStyle::Standard) == "VI");
    CHECK(romanNumeral(chord(B, "dim"), kAAeolian, SymbolStyle::Standard) == "ii°");
    CHECK(romanNumeral(chord(Ab, "dim"), kAAeolian, SymbolStyle::Standard) == "#vii°");  // G# dim
}

TEST_CASE("romanNumeral: C Aeolian with chords browsed from G Aeolian") {
    CHECK(romanNumeral(chord(D, "min"), kCAeolian, SymbolStyle::Standard) == "ii");
    CHECK(romanNumeral(chord(G, "min"), kCAeolian, SymbolStyle::Standard) == "v");
    CHECK(romanNumeral(chord(Bb, "maj"), kCAeolian, SymbolStyle::Standard) == "VII");
    CHECK(romanNumeral(chord(Eb, "maj"), kCAeolian, SymbolStyle::Standard) == "III");
    CHECK(romanNumeral(chord(F, "maj"), kCAeolian, SymbolStyle::Standard) == "IV");
    // A natural is not in C Aeolian (degree VI is Ab), so like B natural below it carries a '#'.
    // The plan listed this as `viø7`; that is Klimper's major-relative convention, not this engine's.
    CHECK(romanNumeral(chord(A, "m7b5"), kCAeolian, SymbolStyle::Standard) == "#viø7");
    CHECK(romanNumeral(chord(A, "m7b5"), kCAeolian, SymbolStyle::Klimper) == "#vi7b5");
    CHECK(romanNumeral(chord(B, "dim"), kCAeolian, SymbolStyle::Standard) == "#vii°");
    CHECK(romanNumeral(chord(B, "dim"), kCAeolian, SymbolStyle::Klimper) == "#viib5");
}

TEST_CASE("romanNumeral: Klimper style replaces ° ø7 °7 with b5 7b5 dim7") {
    CHECK(romanNumeral(chord(B, "dim"), kCIonian, SymbolStyle::Klimper) == "viib5");
    CHECK(romanNumeral(chord(B, "m7b5"), kCIonian, SymbolStyle::Klimper) == "vii7b5");
    CHECK(romanNumeral(chord(B, "dim7"), kCIonian, SymbolStyle::Klimper) == "viidim7");
    CHECK(romanNumeral(chord(G, "7"), kCIonian, SymbolStyle::Klimper) == "V7");
}

TEST_CASE("romanNumeral: defaults to Standard style") {
    CHECK(romanNumeral(chord(B, "dim"), kCIonian) == "vii°");
    CHECK(romanNumeral(chord(B, "m7b5"), kCIonian) == "viiø7");
}

TEST_CASE("romanNumeral: tritone root is #iv in minor keys too") {
    CHECK(romanNumeral(chord(Fs, "dim"), kCAeolian, SymbolStyle::Standard) == "#iv°");
    CHECK(romanNumeral(chord(Eb, "dim"), kAAeolian, SymbolStyle::Standard) == "#iv°");  // D# dim
}

TEST_CASE("romanNumeral: chromatic roots are relative to the project mode, not the major scale") {
    CHECK(romanNumeral(chord(F, "maj"), key('C', 0, Mode::Lydian), SymbolStyle::Standard) == "bIV");
    CHECK(romanNumeral(chord(G, "maj"), key('C', 0, Mode::Locrian), SymbolStyle::Standard) == "#V");
    CHECK(romanNumeral(chord(G, "maj"), key('A', 0, Mode::HarmonicMinor), SymbolStyle::Standard) ==
          "bVII");
    CHECK(romanNumeral(chord(Ab, "maj"), key('A', 0, Mode::HarmonicMinor), SymbolStyle::Standard) ==
          "VII");  // G# is diatonic
}

// --- Suffix tables ---------------------------------------------------------------------

namespace {

struct ExpectedRendering {
    const char* id;
    const char* standardSymbol;
    const char* klimperSymbol;
    const char* standardNumeral;
    const char* klimperNumeral;
};

// Every dictionary type on a C root in C Ionian.
const std::array<ExpectedRendering, 23> kRenderings{{
    {"5", "C5", "C5", "I5", "I5"},
    {"maj", "C", "C", "I", "I"},
    {"min", "Cm", "Cm", "i", "i"},
    {"dim", "C°", "Cmb5", "i°", "ib5"},
    {"aug", "C+", "C+", "I+", "I+"},
    {"sus2", "Csus2", "Csus2", "Isus2", "Isus2"},
    {"sus4", "Csus4", "Csus4", "Isus4", "Isus4"},
    {"6", "C6", "C6", "I6", "I6"},
    {"m6", "Cm6", "Cm6", "i6", "i6"},
    {"maj7", "CΔ7", "CΔ7", "IΔ7", "IΔ7"},
    {"7", "C7", "C7", "I7", "I7"},
    {"m7", "Cm7", "Cm7", "i7", "i7"},
    {"m7b5", "Cø7", "Cm7b5", "iø7", "i7b5"},
    {"dim7", "C°7", "Cdim7", "i°7", "idim7"},
    {"mMaj7", "CmΔ7", "CmΔ7", "iΔ7", "iΔ7"},
    {"augMaj7", "C+Δ7", "C+Δ7", "I+Δ7", "I+Δ7"},
    {"add9", "Cadd9", "Cadd9", "Iadd9", "Iadd9"},
    {"madd9", "Cmadd9", "Cmadd9", "iadd9", "iadd9"},
    {"9", "C9", "C9", "I9", "I9"},
    {"maj9", "CΔ9", "CΔ9", "IΔ9", "IΔ9"},
    {"m9", "Cm9", "Cm9", "i9", "i9"},
    {"69", "C6/9", "C6/9", "I6/9", "I6/9"},
    {"7sus4", "C7sus4", "C7sus4", "I7sus4", "I7sus4"},
}};

}  // namespace

TEST_CASE("chordSymbol and romanNumeral: every dictionary type on C in C Ionian, both styles") {
    for (const ExpectedRendering& r : kRenderings) {
        CAPTURE(r.id);
        const Chord c = chord(C, r.id);
        CHECK(chordSymbol(c, kCIonian, SymbolStyle::Standard) == r.standardSymbol);
        CHECK(chordSymbol(c, kCIonian, SymbolStyle::Klimper) == r.klimperSymbol);
        CHECK(romanNumeral(c, kCIonian, SymbolStyle::Standard) == r.standardNumeral);
        CHECK(romanNumeral(c, kCIonian, SymbolStyle::Klimper) == r.klimperNumeral);
    }
}

// --- Properties ------------------------------------------------------------------------

TEST_CASE("romanNumeral and chordSymbol: the tonic chord is always I/i and starts with the spelled tonic") {
    for (char letter = 'A'; letter <= 'G'; ++letter) {
        for (int alter = -1; alter <= 1; ++alter) {
            for (const Mode mode : kAllModes) {
                const Key k = key(letter, alter, mode);
                const std::string tonicText = Spelling{letter, alter}.text();
                for (const ChordType& t : allChordTypes()) {
                    CAPTURE(letter);
                    CAPTURE(alter);
                    CAPTURE(static_cast<int>(mode));
                    CAPTURE(t.id);
                    const Chord c{k.tonicPc(), &t};
                    for (const SymbolStyle style : {SymbolStyle::Standard, SymbolStyle::Klimper}) {
                        const std::string numeral = romanNumeral(c, k, style);
                        REQUIRE_FALSE(numeral.empty());
                        CHECK((numeral[0] == 'I' || numeral[0] == 'i'));
                        CHECK(numeral.compare(0, 1, "V") != 0);
                        const std::string symbol = chordSymbol(c, k, style);
                        CHECK(symbol.compare(0, tonicText.size(), tonicText) == 0);
                    }
                }
            }
        }
    }
}

TEST_CASE("romanNumeral: every root in every key gets at most one accidental, then a numeral") {
    for (char letter = 'A'; letter <= 'G'; ++letter) {
        for (int alter = -1; alter <= 1; ++alter) {
            for (const Mode mode : kAllModes) {
                const Key k = key(letter, alter, mode);
                for (PitchClass root = 0; root < 12; ++root) {
                    CAPTURE(letter);
                    CAPTURE(alter);
                    CAPTURE(static_cast<int>(mode));
                    CAPTURE(root);
                    const std::string numeral = romanNumeral(chord(root, "maj"), k);
                    REQUIRE(numeral.size() >= 1);
                    if (numeral[0] == 'b' || numeral[0] == '#') {
                        REQUIRE(numeral.size() >= 2);
                        CHECK(isNumeralLetter(numeral[1]));
                        CHECK(inScale(root, k.tonicPc(), mode) == false);
                    } else {
                        CHECK(isNumeralLetter(numeral[0]));
                        CHECK(inScale(root, k.tonicPc(), mode));
                    }
                }
            }
        }
    }
}
