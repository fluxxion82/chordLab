#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/function.h"
#include "chordlab/pitch.h"
#include "chordlab/scale.h"
#include "chordlab/symbol.h"

#include <array>
#include <cstddef>
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

constexpr Function T = Function::Tonic;
constexpr Function PD = Function::Predominant;
constexpr Function Dom = Function::Dominant;
constexpr Function O = Function::Other;

const Key kCIonian = key('C', 0, Mode::Ionian);
const Key kAAeolian = key('A', 0, Mode::Aeolian);

/// One pinned row of the table: a chord, the key, the label it must get and why.
struct Case {
    PitchClass root;
    const char* type;
    Function expected;
    const char* why;
};

void checkAll(const Key& k, const std::vector<Case>& cases) {
    for (const Case& c : cases) {
        const Chord ch = chord(c.root, c.type);
        CAPTURE(c.why);
        CAPTURE(chordSymbol(ch, k, SymbolStyle::Standard));
        CAPTURE(romanNumeral(ch, k));
        CHECK(functionOf(ch, k) == c.expected);
    }
}

}  // namespace

// --- Names ------------------------------------------------------------------------------------

TEST_CASE("functionShortName: T PD D O") {
    CHECK(std::string_view(functionShortName(Function::Tonic)) == "T");
    CHECK(std::string_view(functionShortName(Function::Predominant)) == "PD");
    CHECK(std::string_view(functionShortName(Function::Dominant)) == "D");
    CHECK(std::string_view(functionShortName(Function::Other)) == "O");
}

// --- Major family: C Ionian -----------------------------------------------------------------------

TEST_CASE("functionOf: diatonic chords of C major by degree") {
    checkAll(kCIonian, {
                           {C, "maj", T, "I"},
                           {D, "min", PD, "ii"},
                           {E, "min", T, "iii"},
                           {F, "maj", PD, "IV"},
                           {G, "maj", Dom, "V"},
                           {A, "min", T, "vi"},
                           {B, "dim", Dom, "vii°"},
                           // Sevenths, extensions and the quality-less chords follow the degree.
                           {C, "maj7", T, "IΔ7"},
                           {D, "m7", PD, "ii7"},
                           {E, "m7", T, "iii7"},
                           {F, "maj7", PD, "IVΔ7"},
                           {G, "7", Dom, "V7"},
                           {G, "9", Dom, "V9"},
                           {G, "7sus4", Dom, "V7sus4"},
                           {A, "m7", T, "vi7"},
                           {B, "m7b5", Dom, "viiø7"},
                           {C, "5", T, "I5"},
                           {C, "6", T, "I6"},
                           {C, "add9", T, "Iadd9"},
                           {D, "sus2", PD, "iisus2"},
                           {E, "sus4", T, "iiisus4"},
                       });
}

TEST_CASE("functionOf: borrowed major chords on flat degrees in C major") {
    checkAll(kCIonian, {
                           {Db, "maj", PD, "bII (Neapolitan)"},
                           {Eb, "maj", T, "bIII"},
                           {Ab, "maj", PD, "bVI"},
                           {Bb, "maj", T, "bVII"},
                           // Any quality without a minor third rides the same rule.
                           {Db, "7", PD, "bII7"},
                           {Eb, "7", T, "bIII7"},
                           {Ab, "maj7", PD, "bVIΔ7"},
                           {Bb, "7", T, "bVII7"},
                           {Bb, "sus4", T, "bVIIsus4"},
                           {Bb, "5", T, "bVII5"},
                           // Minor or diminished chords on a chromatic root lead nowhere: Other.
                           {Db, "min", O, "bii"},
                           {Eb, "min", O, "biii"},
                           {Ab, "min", O, "bvi"},
                           {Bb, "min", O, "bvii"},
                           {Bb, "dim", O, "bvii° (B° is diminished, so not a leading-tone target)"},
                           {Fs, "maj", O, "#IV"},
                           {Fs, "7", O, "V7/vii° (diminished target)"},
                       });
}

TEST_CASE("functionOf: applied dominants and leading-tone chords in C major") {
    checkAll(kCIonian, {
                           {A, "7", Dom, "V7/ii"},
                           {B, "7", Dom, "V7/iii"},
                           {C, "7", Dom, "V7/IV"},
                           {D, "7", Dom, "V7/V"},
                           {E, "7", Dom, "V7/vi"},
                           {A, "maj", Dom, "V/ii"},
                           {B, "maj", Dom, "V/iii"},
                           {D, "maj", Dom, "V/V"},
                           {E, "maj", Dom, "V/vi"},
                           {D, "9", Dom, "V9/V"},
                           {Cs, "dim7", Dom, "vii°7/ii"},
                           {Ds, "dim7", Dom, "vii°7/iii"},
                           {E, "dim7", Dom, "vii°7/IV"},
                           {Fs, "dim7", Dom, "vii°7/V"},
                           {Gs, "dim7", Dom, "vii°7/vi"},
                           {Cs, "dim", Dom, "vii°/ii"},
                           {Fs, "dim", Dom, "vii°/V"},
                           {Fs, "m7b5", Dom, "viiø7/V"},
                           {B, "dim7", Dom, "vii°7 (chromatic Ab, leading to I)"},
                       });
}

TEST_CASE("functionOf: diatonic roots with a foreign quality keep their degree in C major") {
    checkAll(kCIonian, {
                           {F, "min", PD, "iv"},
                           {F, "m6", PD, "iv6"},
                           {D, "m7b5", PD, "iiø7"},
                           {D, "dim", PD, "ii°"},
                           {C, "min", T, "i"},
                           {G, "min", Dom, "v"},
                           {B, "min", Dom, "vii"},
                           {C, "maj7", T, "IΔ7 (in key)"},
                           {A, "dim", T, "vi° (Aeolian)"},
                           {E, "maj", Dom, "III is V/vi"},
                           {C, "aug", Dom, "I+ is V+/IV"},
                           // A chord no parallel mode of C holds is not borrowed: Other.
                           {A, "maj7", O, "VIΔ7: no minor seventh, so not an applied dominant either"},
                           {E, "maj7", O, "IIIΔ7"},
                           {B, "maj7", O, "VIIΔ7"},
                       });
}

// --- Minor family: A Aeolian ----------------------------------------------------------------------

TEST_CASE("functionOf: diatonic chords of A Aeolian by degree") {
    checkAll(kAAeolian, {
                            {A, "min", T, "i"},
                            {B, "dim", PD, "ii°"},
                            {C, "maj", T, "III"},
                            {D, "min", PD, "iv"},
                            {E, "min", Dom, "v"},
                            {F, "maj", T, "VI"},
                            {G, "maj", Dom, "VII"},
                            {A, "m7", T, "i7"},
                            {B, "m7b5", PD, "iiø7"},
                            {C, "maj7", T, "IIIΔ7"},
                            {D, "m7", PD, "iv7"},
                            {E, "m7", Dom, "v7"},
                            {F, "maj7", T, "VIΔ7"},
                            {G, "7", Dom, "VII7 (fully in key)"},
                        });
}

TEST_CASE("functionOf: chromatic chords in A Aeolian") {
    checkAll(kAAeolian, {
                            {E, "maj", Dom, "V (harmonic-minor dominant)"},
                            {E, "7", Dom, "V7"},
                            {Gs, "dim", Dom, "vii°"},
                            {Gs, "dim7", Dom, "vii°7"},
                            {Bb, "maj", PD, "bII"},
                            {D, "maj", PD, "IV (Dorian), not V/VII: the subtonic exception"},
                            {D, "6", PD, "IV6 (Dorian)"},
                            {D, "add9", PD, "IVadd9 (Dorian)"},
                            {D, "7", Dom, "V7/VII: with the seventh it is applied"},
                            {D, "9", Dom, "V9/VII"},
                            {A, "7", Dom, "V7/iv"},
                            {B, "7", Dom, "V7/v"},
                            {C, "7", Dom, "V7/VI"},
                            {Cs, "dim7", Dom, "vii°7/iv"},
                            {Ds, "dim7", Dom, "vii°7/v"},
                            {E, "dim7", Dom, "vii°7/VI"},
                            {Fs, "dim7", Dom, "vii°7/VII"},
                            {B, "dim7", Dom, "vii°7/III"},
                            {Fs, "maj", O, "V/ii° (diminished target), and no parallel mode holds it"},
                            {Gs, "maj", O, "no parallel mode holds G# B# D#"},
                            {Cs, "min", T, "iii (borrowed from A major)"},
                            {Fs, "min", T, "vi (borrowed from A major)"},
                            {B, "min", PD, "ii (borrowed from A Dorian)"},
                            {Eb, "maj", O, "#IV"},
                        });
}

// --- Every mode's own scale ------------------------------------------------------------------------

TEST_CASE("functionOf: D Dorian follows the minor-family table") {
    checkAll(key('D', 0, Mode::Dorian), {
                                            {D, "min", T, "i"},
                                            {E, "min", PD, "ii"},
                                            {F, "maj", T, "III"},
                                            {G, "maj", PD, "IV (Dorian's major IV is a predominant)"},
                                            {G, "7", PD, "IV7 (fully in key)"},
                                            {A, "min", Dom, "v"},
                                            {B, "dim", T, "vi°"},
                                            {C, "maj", Dom, "VII"},
                                            {A, "maj", Dom, "V (chromatic C#)"},
                                            {Bb, "maj", PD, "bVI"},
                                        });
}

TEST_CASE("functionOf: C Phrygian follows the minor-family table") {
    checkAll(key('C', 0, Mode::Phrygian), {
                                              {C, "min", T, "i"},
                                              {Db, "maj", PD, "II"},
                                              {Eb, "maj", T, "III"},
                                              {F, "min", PD, "iv"},
                                              {G, "dim", Dom, "v°"},
                                              {Ab, "maj", T, "VI"},
                                              {Bb, "min", Dom, "vii"},
                                              {G, "maj", Dom, "V (chromatic B)"},
                                              {B, "dim", Dom, "vii° (chromatic)"},
                                              {F, "maj", PD, "IV (Aeolian), not V/vii: the subtonic exception"},
                                              {F, "7", Dom, "V7/vii"},
                                          });
}

TEST_CASE("functionOf: A harmonic and melodic minor") {
    checkAll(key('A', 0, Mode::HarmonicMinor), {
                                                   {A, "min", T, "i"},
                                                   {B, "dim", PD, "ii°"},
                                                   {C, "aug", T, "III+"},
                                                   {D, "min", PD, "iv"},
                                                   {E, "maj", Dom, "V"},
                                                   {E, "7", Dom, "V7 (fully in key)"},
                                                   {F, "maj", T, "VI"},
                                                   {Gs, "dim", Dom, "vii°"},
                                                   {Gs, "dim7", Dom, "vii°7 (fully in key)"},
                                                   {E, "min", Dom, "v (chromatic G natural)"},
                                                   {G, "maj", Dom, "G is V/III+: the applied rule wins over bVII"},
                                               });
    checkAll(key('A', 0, Mode::MelodicMinor), {
                                                  {A, "min", T, "i"},
                                                  {A, "mMaj7", T, "iΔ7"},
                                                  {B, "min", PD, "ii"},
                                                  {C, "aug", T, "III+"},
                                                  {D, "maj", PD, "IV"},
                                                  {E, "maj", Dom, "V"},
                                                  {Fs, "dim", T, "vi°"},
                                                  {Gs, "dim", Dom, "vii°"},
                                                  {F, "maj", PD, "bVI (chromatic)"},
                                                  {D, "min", PD, "iv (chromatic F natural)"},
                                              });
}

TEST_CASE("functionOf: C Lydian and C Mixolydian follow the major-family table") {
    checkAll(key('C', 0, Mode::Lydian), {
                                            {C, "maj", T, "I"},
                                            {D, "maj", PD, "II"},
                                            {E, "min", T, "iii"},
                                            {Fs, "dim", PD, "#iv°"},
                                            {G, "maj", Dom, "V"},
                                            {A, "min", T, "vi"},
                                            {B, "min", Dom, "vii"},
                                            {D, "7", PD, "II7 (fully in key)"},
                                            {F, "maj", PD, "IV (chromatic F natural)"},
                                        });
    checkAll(key('C', 0, Mode::Mixolydian), {
                                                {C, "maj", T, "I"},
                                                {C, "7", T, "I7 (fully in key)"},
                                                {D, "min", PD, "ii"},
                                                {E, "dim", T, "iii°"},
                                                {F, "maj", PD, "IV"},
                                                {G, "min", Dom, "v"},
                                                {A, "min", T, "vi"},
                                                {Bb, "maj", Dom, "VII"},
                                                {G, "maj", Dom, "V (chromatic B)"},
                                                {G, "7", Dom, "V7 (chromatic B)"},
                                                {B, "dim", Dom, "vii° (chromatic B)"},
                                                // IV7 in Mixolydian is both the blues subdominant and V7/VII;
                                                // the applied reading wins, as for every seventh chord.
                                                {F, "7", Dom, "V7/VII"},
                                            });
}

TEST_CASE("functionOf: C Locrian has a tonic and nothing else") {
    checkAll(key('C', 0, Mode::Locrian), {
                                             {C, "dim", T, "i°"},
                                             {C, "m7b5", T, "iø7"},
                                             {Db, "maj", O, "II"},
                                             {Eb, "min", O, "iii"},
                                             {F, "min", O, "iv"},
                                             {Fs, "maj", O, "V"},
                                             {Ab, "maj", O, "VI"},
                                             {Bb, "min", O, "vii"},
                                             {G, "7", O, "a chromatic dominant"},
                                             {G, "maj", O, "chromatic"},
                                             {C, "min", O, "i minor is not in key"},
                                             {C, "5", O, "no perfect fifth in the key"},
                                             {B, "dim", O, "no leading-tone rule"},
                                         });
}

// --- Properties -----------------------------------------------------------------------------------

TEST_CASE("functionOf: every diatonic triad of every non-Locrian key gets the degree's label") {
    constexpr std::array<Function, 7> kByDegree{T, PD, T, PD, Dom, T, Dom};
    for (PitchClass pc = 0; pc < 12; ++pc) {
        for (int m = 0; m < kNumModes; ++m) {
            const Mode mode = Mode(m);
            const Key k{canonicalTonic(pc, mode), mode};
            for (int d = 0; d < 7; ++d) {
                CAPTURE(pc);
                CAPTURE(m);
                CAPTURE(d);
                const Chord triad = diatonicTriad(k, d);
                const Function expected =
                    mode == Mode::Locrian ? (d == 0 ? T : O) : kByDegree[static_cast<std::size_t>(d)];
                CHECK(functionOf(triad, k) == expected);
            }
        }
    }
}

TEST_CASE("functionOf: the tonic triad of every key is Tonic and every chord gets one of the four labels") {
    for (PitchClass pc = 0; pc < 12; ++pc) {
        for (int m = 0; m < kNumModes; ++m) {
            const Mode mode = Mode(m);
            const Key k{canonicalTonic(pc, mode), mode};
            CAPTURE(pc);
            CAPTURE(m);
            CHECK(functionOf(diatonicTriad(k, 0), k) == T);
            for (const ChordType& t : allChordTypes()) {
                for (PitchClass root = 0; root < 12; ++root) {
                    const Function f = functionOf(Chord{root, &t}, k);
                    CHECK((f == T || f == PD || f == Dom || f == O));
                }
            }
        }
    }
}

TEST_CASE("functionOf: is independent of the key's spelling") {
    // Gb major and F# major are the same scale, so every chord gets the same label.
    const Key gFlat = key('G', -1, Mode::Ionian);
    const Key fSharp = key('F', 1, Mode::Ionian);
    for (const ChordType& t : allChordTypes()) {
        for (PitchClass root = 0; root < 12; ++root) {
            CAPTURE(t.id);
            CAPTURE(root);
            CHECK(functionOf(Chord{root, &t}, gFlat) == functionOf(Chord{root, &t}, fSharp));
        }
    }
}
