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
    // Chromatic roots take the letter of the parallel-major degree they alter: Bb in D major is the
    // lowered sixth, not A#.
    CHECK(chordSymbol(chord(Bb, "maj"), key('D', 0, Mode::Ionian)) == "Bb");
    CHECK(chordSymbol(chord(Db, "maj"), kCIonian) == "Db");
    CHECK(chordSymbol(chord(Fs, "dim"), kCIonian) == "F#mb5");
    CHECK(chordSymbol(chord(Ab, "maj"), key('G', 0, Mode::Ionian)) == "Ab");
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

TEST_CASE("romanNumeral: A Aeolian is read against A major") {
    CHECK(romanNumeral(chord(A, "min"), kAAeolian, SymbolStyle::Standard) == "i");
    CHECK(romanNumeral(chord(G, "maj"), kAAeolian, SymbolStyle::Standard) == "bVII");
    CHECK(romanNumeral(chord(F, "maj"), kAAeolian, SymbolStyle::Standard) == "bVI");
    CHECK(romanNumeral(chord(E, "maj"), kAAeolian, SymbolStyle::Standard) == "V");
    CHECK(romanNumeral(chord(D, "min"), kAAeolian, SymbolStyle::Standard) == "iv");
    CHECK(romanNumeral(chord(B, "dim"), kAAeolian, SymbolStyle::Standard) == "ii°");
    CHECK(romanNumeral(chord(Ab, "dim"), kAAeolian, SymbolStyle::Standard) == "vii°");  // G# is in A major
    CHECK(romanNumeral(chord(C, "maj"), kAAeolian, SymbolStyle::Standard) == "bIII");
}

TEST_CASE("romanNumeral: C Aeolian is read against C major") {
    CHECK(romanNumeral(chord(C, "min"), kCAeolian, SymbolStyle::Standard) == "i");
    CHECK(romanNumeral(chord(D, "m7b5"), kCAeolian, SymbolStyle::Standard) == "iiø7");
    CHECK(romanNumeral(chord(Eb, "maj"), kCAeolian, SymbolStyle::Standard) == "bIII");
    CHECK(romanNumeral(chord(F, "min"), kCAeolian, SymbolStyle::Standard) == "iv");
    CHECK(romanNumeral(chord(G, "min"), kCAeolian, SymbolStyle::Standard) == "v");
    CHECK(romanNumeral(chord(Ab, "maj"), kCAeolian, SymbolStyle::Standard) == "bVI");
    CHECK(romanNumeral(chord(Bb, "maj"), kCAeolian, SymbolStyle::Standard) == "bVII");
    CHECK(romanNumeral(chord(Bb, "7"), kCAeolian, SymbolStyle::Standard) == "bVII7");
    CHECK(romanNumeral(chord(Ab, "maj7"), kCAeolian, SymbolStyle::Standard) == "bVIΔ7");
    // Raised degrees that are in C major carry no accidental, as in Klimper / Hooktheory.
    CHECK(romanNumeral(chord(A, "m7b5"), kCAeolian, SymbolStyle::Standard) == "viø7");
    CHECK(romanNumeral(chord(A, "m7b5"), kCAeolian, SymbolStyle::Klimper) == "vi7b5");
    CHECK(romanNumeral(chord(B, "dim"), kCAeolian, SymbolStyle::Standard) == "vii°");
    CHECK(romanNumeral(chord(B, "dim"), kCAeolian, SymbolStyle::Klimper) == "viib5");
}

TEST_CASE("romanNumeral: chords of G Aeolian seen from C Aeolian match Klimper's palette") {
    // Klimper shows `v vi♭5 ♭VII i ii ♭III IV` for this browse.
    CHECK(romanNumeral(chord(G, "min"), kCAeolian, SymbolStyle::Klimper) == "v");
    CHECK(romanNumeral(chord(A, "m7b5"), kCAeolian, SymbolStyle::Klimper) == "vi7b5");
    CHECK(romanNumeral(chord(A, "dim"), kCAeolian, SymbolStyle::Klimper) == "vib5");
    CHECK(romanNumeral(chord(Bb, "maj"), kCAeolian, SymbolStyle::Klimper) == "bVII");
    CHECK(romanNumeral(chord(C, "min"), kCAeolian, SymbolStyle::Klimper) == "i");
    CHECK(romanNumeral(chord(D, "min"), kCAeolian, SymbolStyle::Klimper) == "ii");
    CHECK(romanNumeral(chord(Eb, "maj"), kCAeolian, SymbolStyle::Klimper) == "bIII");
    CHECK(romanNumeral(chord(F, "maj"), kCAeolian, SymbolStyle::Klimper) == "IV");
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

TEST_CASE("romanNumeral: every mode is read against the parallel major, not its own scale") {
    const Key cLydian = key('C', 0, Mode::Lydian);
    CHECK(romanNumeral(chord(F, "maj"), cLydian, SymbolStyle::Standard) == "IV");
    CHECK(romanNumeral(chord(Fs, "dim"), cLydian, SymbolStyle::Standard) == "#iv°");

    const Key gMixolydian = key('G', 0, Mode::Mixolydian);
    CHECK(romanNumeral(chord(F, "maj"), gMixolydian, SymbolStyle::Standard) == "bVII");
    CHECK(romanNumeral(chord(F, "min"), gMixolydian, SymbolStyle::Standard) == "bvii");

    const Key dDorian = key('D', 0, Mode::Dorian);
    CHECK(romanNumeral(chord(B, "min"), dDorian, SymbolStyle::Standard) == "vi");
    CHECK(romanNumeral(chord(B, "dim"), dDorian, SymbolStyle::Standard) == "vi°");
    CHECK(romanNumeral(chord(F, "maj"), dDorian, SymbolStyle::Standard) == "bIII");
    CHECK(romanNumeral(chord(C, "maj"), dDorian, SymbolStyle::Standard) == "bVII");

    const Key aHarmonicMinor = key('A', 0, Mode::HarmonicMinor);
    CHECK(romanNumeral(chord(G, "maj"), aHarmonicMinor, SymbolStyle::Standard) == "bVII");
    CHECK(romanNumeral(chord(Ab, "dim"), aHarmonicMinor, SymbolStyle::Standard) == "vii°");  // G# dim
    CHECK(romanNumeral(chord(E, "7"), aHarmonicMinor, SymbolStyle::Standard) == "V7");

    CHECK(romanNumeral(chord(G, "maj"), key('C', 0, Mode::Locrian), SymbolStyle::Standard) == "V");
}

// --- Symbol accidental vs numeral alteration ------------------------------------------------

TEST_CASE("chordSymbol and romanNumeral: the symbol's accidental is letter-relative, the numeral's key-relative") {
    // Both pick the root's letter and degree from the same parallel-major table, but the numeral
    // measures its alteration from the key's own degree (G in E major sits a semitone under G#: bIII)
    // while the symbol shows only the accidental on its letter (G, none), and white-key folding
    // (Cb -> B, Fb -> E, E# -> F) removes symbol accidentals the numeral keeps.
    const Key eMajor = key('E', 0, Mode::Ionian);
    CHECK(chordSymbol(chord(G, "maj"), eMajor, SymbolStyle::Standard) == "G");
    CHECK(romanNumeral(chord(G, "maj"), eMajor, SymbolStyle::Standard) == "bIII");
    CHECK(chordSymbol(chord(Bb, "dim"), eMajor, SymbolStyle::Standard) == "A#°");
    CHECK(romanNumeral(chord(Bb, "dim"), eMajor, SymbolStyle::Standard) == "#iv°");

    const Key aFlatMajor = key('A', -1, Mode::Ionian);
    CHECK(chordSymbol(chord(B, "maj"), aFlatMajor, SymbolStyle::Standard) == "B");  // Cb folded
    CHECK(romanNumeral(chord(B, "maj"), aFlatMajor, SymbolStyle::Standard) == "bIII");
    CHECK(chordSymbol(chord(E, "maj"), aFlatMajor, SymbolStyle::Standard) == "E");  // Fb folded
    CHECK(romanNumeral(chord(E, "maj"), aFlatMajor, SymbolStyle::Standard) == "bVI");

    const Key bMajor = key('B', 0, Mode::Ionian);
    CHECK(chordSymbol(chord(F, "dim"), bMajor, SymbolStyle::Standard) == "F°");  // E# folded
    CHECK(romanNumeral(chord(F, "dim"), bMajor, SymbolStyle::Standard) == "#iv°");

    // With a natural tonic every major degree is natural, so in C major the two coincide.
    CHECK(chordSymbol(chord(Db, "maj"), kCIonian, SymbolStyle::Standard) == "Db");
    CHECK(romanNumeral(chord(Db, "maj"), kCIonian, SymbolStyle::Standard) == "bII");
    CHECK(chordSymbol(chord(Fs, "dim"), kCIonian, SymbolStyle::Standard) == "F#°");
    CHECK(romanNumeral(chord(Fs, "dim"), kCIonian, SymbolStyle::Standard) == "#iv°");
}

// --- numeralDegree ---------------------------------------------------------------------

TEST_CASE("numeralDegree: major-scale roots are their degree with no alteration") {
    CHECK(numeralDegree(chord(C, "maj"), kCIonian) == NumeralDegree{0, 0});
    CHECK(numeralDegree(chord(D, "min"), kCIonian) == NumeralDegree{1, 0});
    CHECK(numeralDegree(chord(B, "dim"), kCIonian) == NumeralDegree{6, 0});
    CHECK(numeralDegree(chord(A, "7"), kCIonian) == NumeralDegree{5, 0});  // the quality does not matter
    CHECK(numeralDegree(chord(Ab, "dim"), kAAeolian) == NumeralDegree{6, 0});  // G# is in A major
}

TEST_CASE("numeralDegree: chromatic roots take the parallel-mode degree and a single alteration") {
    CHECK(numeralDegree(chord(Db, "maj"), kCIonian) == NumeralDegree{1, -1});
    CHECK(numeralDegree(chord(Eb, "maj"), kCIonian) == NumeralDegree{2, -1});
    CHECK(numeralDegree(chord(Fs, "dim"), kCIonian) == NumeralDegree{3, 1});
    CHECK(numeralDegree(chord(Ab, "maj"), kCIonian) == NumeralDegree{5, -1});
    CHECK(numeralDegree(chord(Bb, "maj"), kCIonian) == NumeralDegree{6, -1});
    CHECK(numeralDegree(chord(G, "maj"), kAAeolian) == NumeralDegree{6, -1});
    CHECK(numeralDegree(chord(C, "maj"), kAAeolian) == NumeralDegree{2, -1});
    CHECK(numeralDegree(chord(Eb, "dim"), kAAeolian) == NumeralDegree{3, 1});  // D#
    CHECK(numeralDegree(chord(F, "maj"), key('C', 0, Mode::Lydian)) == NumeralDegree{3, 0});  // mode ignored
}

TEST_CASE("numeralDegree: agrees with the prefix and numeral that romanNumeral writes") {
    constexpr std::array<std::string_view, 7> kUpper{"I", "II", "III", "IV", "V", "VI", "VII"};
    for (char letter = 'A'; letter <= 'G'; ++letter) {
        for (int alter = -1; alter <= 1; ++alter) {
            for (const Mode mode : kAllModes) {
                const Key k = key(letter, alter, mode);
                for (PitchClass root = 0; root < 12; ++root) {
                    CAPTURE(letter);
                    CAPTURE(alter);
                    CAPTURE(static_cast<int>(mode));
                    CAPTURE(root);
                    const Chord c = chord(root, "maj");
                    const NumeralDegree nd = numeralDegree(c, k);
                    REQUIRE(nd.degree >= 0);
                    REQUIRE(nd.degree < 7);
                    REQUIRE(nd.alter >= -1);
                    REQUIRE(nd.alter <= 1);
                    std::string expected = nd.alter < 0 ? "b" : nd.alter > 0 ? "#" : "";
                    expected += kUpper[static_cast<std::size_t>(nd.degree)];
                    CHECK(romanNumeral(c, k) == expected);
                }
            }
        }
    }
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
                        // The roman part is exactly `I`/`i`: only the suffix may follow, never another
                        // numeral letter (`II`, `IV`, `iii`...).
                        CHECK((numeral.size() == 1 || !isNumeralLetter(numeral[1])));
                        const std::string symbol = chordSymbol(c, k, style);
                        CHECK(symbol.compare(0, tonicText.size(), tonicText) == 0);
                    }
                }
            }
        }
    }
}

TEST_CASE("romanNumeral: every root in every key gets at most one accidental, read against the major scale") {
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
                        CHECK(inScale(root, k.tonicPc(), Mode::Ionian) == false);
                    } else {
                        CHECK(isNumeralLetter(numeral[0]));
                        CHECK(inScale(root, k.tonicPc(), Mode::Ionian));
                    }
                }
            }
        }
    }
}
