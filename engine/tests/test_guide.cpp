#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/document.h"
#include "chordlab/guide.h"
#include "chordlab/json_api.h"
#include "chordlab/palette.h"
#include "chordlab/pitch.h"
#include "chordlab/scale.h"
#include "chordlab/symbol.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using namespace chordlab;
using nlohmann::json;

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

const Key kCIonian = key('C', 0, Mode::Ionian);
const Key kAAeolian = key('A', 0, Mode::Aeolian);
const Key kAHarmonic = key('A', 0, Mode::HarmonicMinor);
const Key kAMelodic = key('A', 0, Mode::MelodicMinor);
const Key kDDorian = key('D', 0, Mode::Dorian);
const Key kDIonian = key('D', 0, Mode::Ionian);
const Key kFsIonian = key('F', 1, Mode::Ionian);
const Key kEbIonian = key('E', -1, Mode::Ionian);
const Key kCsIonian = key('C', 1, Mode::Ionian);

/// One letter per role: R root, c chord tone, t tension, a avoid, . outside.
char roleCode(GuideRole role) {
    switch (role) {
        case GuideRole::Root: return 'R';
        case GuideRole::ChordTone: return 'c';
        case GuideRole::Tension: return 't';
        case GuideRole::Avoid: return 'a';
        case GuideRole::Outside: return '.';
    }
    return '?';
}

/// The guide's roles as twelve role codes, starting at the root and rising by semitones.
std::string rolesFromRoot(const Guide& guide, PitchClass root) {
    std::string out;
    for (int k = 0; k < 12; ++k) {
        out += roleCode(guide.role[static_cast<std::size_t>(pitchClassOf(root + k))]);
    }
    return out;
}

/// Twelve strings of `field`, starting at the root and rising by semitones, joined by spaces.
std::string fromRoot(const std::array<std::string, 12>& field, PitchClass root) {
    std::string out;
    for (int k = 0; k < 12; ++k) {
        if (k > 0) out += ' ';
        out += field[static_cast<std::size_t>(pitchClassOf(root + k))];
    }
    return out;
}

std::uint16_t maskOf(const std::vector<PitchClass>& pcs) {
    std::uint16_t mask = 0;
    for (const PitchClass pc : pcs) mask = static_cast<std::uint16_t>(mask | (1u << pc));
    return mask;
}

bool inMask(std::uint16_t mask, PitchClass pc) {
    return (mask >> pc) & 1u;
}

/// Checks the whole guide of `c` in `k`: twelve role codes, degrees and names, each read from the root upwards
/// ("R.t.ca.c.t.t": C, Db, D, ... B over C major), so a failure prints the two rows side by side.
void checkGuide(const Key& k, const Chord& c, std::string_view roles, std::string_view degrees,
                std::string_view names) {
    CAPTURE(chordSymbol(c, k));
    CAPTURE(keyName(k));
    const Guide guide = harmonicGuide(c, k);
    CHECK(rolesFromRoot(guide, c.root) == roles);
    CHECK(fromRoot(guide.degree, c.root) == degrees);
    CHECK(fromRoot(guide.name, c.root) == names);
}

/// The pitch class an ASCII note name ("F#", "Bbb", "Fx", "E#") names, or -1 if it is not one.
int pitchClassOfName(const std::string& name) {
    if (name.empty() || name[0] < 'A' || name[0] > 'G') return -1;
    int alter = 0;
    for (std::size_t i = 1; i < name.size(); ++i) {
        if (name[i] == '#') {
            alter += 1;
        } else if (name[i] == 'b') {
            alter -= 1;
        } else if (name[i] == 'x') {
            alter += 2;
        } else {
            return -1;
        }
    }
    return Spelling{name[0], alter}.pitchClass();
}

bool isPrintableAscii(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](char ch) { return ch > ' ' && ch < 127; });
}

/// The dictionary type whose intervals are exactly `intervals`, or null.
const ChordType* typeWithIntervals(const std::vector<int>& intervals) {
    for (const ChordType& t : allChordTypes()) {
        if (t.intervals == intervals) return &t;
    }
    return nullptr;
}

/// The stacked-third chord of `size` tones (3 or 4) on scale degree `degree` of `k`.
Chord diatonicChord(const Key& k, int degree, int size) {
    const std::array<PitchClass, 7> pcs = scalePitchClasses(k.tonicPc(), k.mode);
    const PitchClass root = pcs[static_cast<std::size_t>(degree)];
    std::vector<int> intervals;
    for (int n = 0; n < size; ++n) {
        intervals.push_back(pitchClassOf(pcs[static_cast<std::size_t>((degree + 2 * n) % 7)] - root));
    }
    const ChordType* type = typeWithIntervals(intervals);
    REQUIRE_MESSAGE(type != nullptr, "no dictionary type for a diatonic chord");
    return Chord{root, type};
}

bool isDominant(const Chord& c) {
    const std::vector<int>& iv = c.type->intervals;
    return std::find(iv.begin(), iv.end(), 4) != iv.end() && std::find(iv.begin(), iv.end(), 10) != iv.end();
}

/// The rules on a chord whose tones all lie in the key: the chord scale is the key, the roles follow the
/// semitone-above rule (with the dominant exception), every name is the key's (`spellDegree`) or `spell`'s.
void checkDiatonic(const Key& k, const Chord& c) {
    CAPTURE(chordSymbol(c, k));
    CAPTURE(keyName(k));
    const Guide guide = harmonicGuide(c, k);
    const std::array<PitchClass, 7> scale = scalePitchClasses(k.tonicPc(), k.mode);
    CHECK(guide.scaleMask == maskOf(std::vector<PitchClass>(scale.begin(), scale.end())));
    const std::vector<PitchClass> tones = pitchClasses(c);
    const auto inChord = [&](PitchClass pc) { return std::find(tones.begin(), tones.end(), pc) != tones.end(); };
    for (PitchClass pc = 0; pc < 12; ++pc) {
        CAPTURE(pc);
        const GuideRole role = guide.role[static_cast<std::size_t>(pc)];
        const int degree = degreeOf(pc, k.tonicPc(), k.mode);
        if (pc == c.root) {
            CHECK(role == GuideRole::Root);
        } else if (inChord(pc)) {
            CHECK(role == GuideRole::ChordTone);
        } else if (degree >= 0) {
            const int above = pitchClassOf(pc - c.root);
            const bool rescued = isDominant(c) && (above == 1 || above == 3 || above == 8);
            const bool clash = inChord(pitchClassOf(pc - 1)) && !rescued;
            CHECK(role == (clash ? GuideRole::Avoid : GuideRole::Tension));
        } else {
            CHECK(role == GuideRole::Outside);
        }
        const std::string expectedName = degree >= 0 ? spellDegree(k, degree).text() : spell(pc, k).text();
        CHECK(guide.name[static_cast<std::size_t>(pc)] == expectedName);
    }
}

constexpr std::string_view kTriadDegrees = "R b2 2 #2 3 4 #4 5 b6 6 b7 7";      // a major triad
constexpr std::string_view kMinorDegrees = "R b2 2 b3 3 4 #4 5 b6 6 b7 7";      // a minor triad
constexpr std::string_view kMajorSeventhDegrees = "R b9 9 #9 3 11 #11 5 b13 13 b7 7";  // maj7 and 7
constexpr std::string_view kMinorSeventhDegrees = "R b9 9 b3 3 11 #11 5 b13 13 b7 7";
constexpr std::string_view kHalfDiminishedDegrees = "R b9 9 b3 3 11 b5 5 #5 13 b7 7";

}  // namespace

// --- chordToneDegree and guideRoleName -----------------------------------------------

TEST_CASE("chordToneDegree: letter steps by the interval's major degree, except b5, #5 and bb7 by structure") {
    CHECK(chordToneDegree(chordType("maj"), 0) == 0);
    CHECK(chordToneDegree(chordType("maj"), 4) == 2);
    CHECK(chordToneDegree(chordType("min"), 3) == 2);
    CHECK(chordToneDegree(chordType("sus2"), 2) == 1);
    CHECK(chordToneDegree(chordType("sus4"), 5) == 3);
    CHECK(chordToneDegree(chordType("6"), 9) == 5);
    CHECK(chordToneDegree(chordType("7"), 10) == 6);
    CHECK(chordToneDegree(chordType("add9"), 14) == 1);
    CHECK(chordToneDegree(chordType("7#11"), 18) == 3);  // keeps its fifth: a sharp fourth
    CHECK(chordToneDegree(chordType("dim"), 6) == 4);    // no perfect fifth: a diminished fifth
    CHECK(chordToneDegree(chordType("m7b5"), 6) == 4);
    CHECK(chordToneDegree(chordType("aug"), 8) == 4);    // an augmented fifth, not a flat sixth
    CHECK(chordToneDegree(chordType("dim7"), 9) == 6);   // a diminished seventh, not a sixth
}

TEST_CASE("guideRoleName: the five wire names") {
    CHECK(std::string(guideRoleName(GuideRole::Root)) == "root");
    CHECK(std::string(guideRoleName(GuideRole::ChordTone)) == "chord");
    CHECK(std::string(guideRoleName(GuideRole::Tension)) == "tension");
    CHECK(std::string(guideRoleName(GuideRole::Avoid)) == "avoid");
    CHECK(std::string(guideRoleName(GuideRole::Outside)) == "outside");
}

// --- pinned tables --------------------------------------------------------------------

TEST_CASE("harmonicGuide: arrays are indexed by absolute pitch class") {
    const Guide g = harmonicGuide(chord(G, "7"), kCIonian);
    CHECK(g.role[G] == GuideRole::Root);
    CHECK(g.role[B] == GuideRole::ChordTone);
    CHECK(g.role[C] == GuideRole::Avoid);
    CHECK(g.degree[C] == "11");
    CHECK(g.name[C] == "C");
    CHECK(g.degree[E] == "13");
    CHECK(g.role[Ab] == GuideRole::Outside);
}

TEST_CASE("harmonicGuide: C Ionian triads") {
    checkGuide(kCIonian, chord(C, "maj"), "R.t.ca.c.t.t", kTriadDegrees, "C Db D Eb E F F# G Ab A Bb B");
    checkGuide(kCIonian, chord(D, "min"), "R.tc.t.c.tt.", kMinorDegrees, "D Eb E F F# G Ab A Bb B C Db");
    checkGuide(kCIonian, chord(E, "min"), "Ra.c.t.ca.t.", kMinorDegrees, "E F F# G Ab A Bb B C Db D Eb");
    checkGuide(kCIonian, chord(F, "maj"), "R.t.c.tc.t.t", kTriadDegrees, "F F# G Ab A Bb B C Db D Eb E");
    checkGuide(kCIonian, chord(G, "maj"), "R.t.ca.c.tt.", kTriadDegrees, "G Ab A Bb B C Db D Eb E F F#");
    checkGuide(kCIonian, chord(A, "min"), "R.tc.t.ca.t.", kMinorDegrees, "A Bb B C Db D Eb E F F# G Ab");
    checkGuide(kCIonian, chord(B, "dim"), "Ra.c.tc.t.t.", "R b2 2 b3 3 4 b5 5 #5 6 b7 7",
               "B C Db D Eb E F F# G Ab A Bb");
}

TEST_CASE("harmonicGuide: C Ionian sevenths (Cmaj7: D 9 Tension, F Avoid, A 13 Tension; G7: C Avoid)") {
    checkGuide(kCIonian, chord(C, "maj7"), "R.t.ca.c.t.c", kMajorSeventhDegrees, "C Db D Eb E F F# G Ab A Bb B");
    checkGuide(kCIonian, chord(D, "m7"), "R.tc.t.c.tc.", kMinorSeventhDegrees, "D Eb E F F# G Ab A Bb B C Db");
    checkGuide(kCIonian, chord(E, "m7"), "Ra.c.t.ca.c.", kMinorSeventhDegrees, "E F F# G Ab A Bb B C Db D Eb");
    checkGuide(kCIonian, chord(F, "maj7"), "R.t.c.tc.t.c", kMajorSeventhDegrees, "F F# G Ab A Bb B C Db D Eb E");
    checkGuide(kCIonian, chord(G, "7"), "R.t.ca.c.tc.", kMajorSeventhDegrees, "G Ab A Bb B C Db D Eb E F F#");
    checkGuide(kCIonian, chord(A, "m7"), "R.tc.t.ca.c.", kMinorSeventhDegrees, "A Bb B C Db D Eb E F F# G Ab");
    checkGuide(kCIonian, chord(B, "m7b5"), "Ra.c.tc.t.c.", kHalfDiminishedDegrees, "B C Db D Eb E F F# G Ab A Bb");

    const Guide g7 = harmonicGuide(chord(G, "7"), kCIonian);
    CHECK(g7.scaleMask == maskOf({C, D, E, F, G, A, B}));
    for (const PitchClass pc : {Ab, Bb, Eb}) {
        CHECK(g7.role[static_cast<std::size_t>(pc)] == GuideRole::Outside);  // chromatic b9, #9, b13 stay Outside
    }
}

TEST_CASE("harmonicGuide: A Aeolian, A harmonic and melodic minor, D Dorian, F# Ionian spot checks") {
    // Outside names follow `spell` in the key: against A major, pc 1, 3, 6, 8 are C#, D#, F#, G#.
    checkGuide(kAAeolian, chord(A, "m7"), "R.tc.t.ca.c.", kMinorSeventhDegrees, "A Bb B C C# D D# E F F# G G#");
    checkGuide(kAAeolian, chord(G, "maj"), "R.t.ca.c.tt.", kTriadDegrees, "G G# A Bb B C C# D D# E F F#");
    checkGuide(kAAeolian, chord(B, "m7b5"), "Ra.c.tc.t.c.", kHalfDiminishedDegrees, "B C C# D D# E F F# G G# A Bb");

    // V7 in harmonic minor: F (b9) and C (b13) are in the key, Avoid by the semitone rule, Tension by the dominant
    // exception; G, the #9, is chromatic there and stays Outside.
    checkGuide(kAHarmonic, chord(E, "7"), "Rt..ca.ct.c.", kMajorSeventhDegrees, "E F F# G G# A Bb B C C# D D#");
    checkGuide(kAHarmonic, chord(Ab, "dim7"), "Ra.ca.c.tc..", "R b9 9 b3 3 11 b5 5 #5 bb7 b7 7",
               "G# A Bb B C C# D D# E F F# G");
    checkGuide(kAHarmonic, chord(C, "aug"), "R.t.ca..ca.t", "R b2 2 #2 3 4 b5 5 #5 6 b7 7",
               "C C# D D# E F F# G G# A Bb B");

    checkGuide(kAMelodic, chord(D, "7"), "R.t.c.tc.tc.", kMajorSeventhDegrees, "D D# E F F# G G# A Bb B C C#");
    checkGuide(kAMelodic, chord(Fs, "m7b5"), "R.tc.tc.t.c.", kHalfDiminishedDegrees, "F# G G# A Bb B C C# D D# E F");

    checkGuide(kDDorian, chord(D, "m7"), "R.tc.t.c.tc.", kMinorSeventhDegrees, "D Eb E F F# G G# A Bb B C C#");
    checkGuide(kDDorian, chord(G, "7"), "R.t.ca.c.tc.", kMajorSeventhDegrees, "G G# A Bb B C C# D Eb E F F#");

    checkGuide(kFsIonian, chord(Fs, "maj7"), "R.t.ca.c.t.c", kMajorSeventhDegrees, "F# G G# A A# B C C# D D# E E#");
    checkGuide(kFsIonian, chord(Db, "7"), "R.t.ca.c.tc.", kMajorSeventhDegrees, "C# D D# E E# F# G G# A A# B C");
    checkGuide(kFsIonian, chord(F, "m7b5"), "Ra.c.tc.t.c.", kHalfDiminishedDegrees, "E# F# G G# A A# B C C# D D# E");
}

TEST_CASE("harmonicGuide: every diatonic triad and seventh in seven keys follows the in-key rules") {
    for (const Key& k : {kCIonian, kAAeolian, kAHarmonic, kAMelodic, kDDorian, kFsIonian, kEbIonian}) {
        for (int degree = 0; degree < 7; ++degree) {
            checkDiatonic(k, diatonicChord(k, degree, 3));
            checkDiatonic(k, diatonicChord(k, degree, 4));
        }
    }
}

TEST_CASE("harmonicGuide: applied E7 and D7 in C replace a scale tone on their letter") {
    checkGuide(kCIonian, chord(E, "7"), "Rt.tca.ct.c.", kMajorSeventhDegrees, "E F F# G G# A Bb B C Db D Eb");
    const Guide e7 = harmonicGuide(chord(E, "7"), kCIonian);
    CHECK(e7.scaleMask == maskOf({E, F, Ab, A, B, C, D}));  // G# replaces G
    CHECK_FALSE(inMask(e7.scaleMask, G));
    CHECK(e7.role[G] == GuideRole::Tension);  // a Tension by the dominant exception, yet not in the scale

    checkGuide(kCIonian, chord(D, "7"), "R.ttca.c.tc.", kMajorSeventhDegrees, "D Eb E F F# G Ab A Bb B C Db");
    const Guide d7 = harmonicGuide(chord(D, "7"), kCIonian);
    CHECK(d7.scaleMask == maskOf({C, D, E, Fs, G, A, B}));  // F# replaces F
}

TEST_CASE("harmonicGuide: the applied V7s in Eb, named in Eb's letters") {
    checkGuide(kEbIonian, chord(C, "7"), "R.ttca.ct.c.", kMajorSeventhDegrees, "C Db D Eb E F Gb G Ab A Bb B");  // V7/ii
    CHECK(harmonicGuide(chord(C, "7"), kEbIonian).scaleMask == maskOf({C, D, E, F, G, Ab, Bb}));
    checkGuide(kEbIonian, chord(D, "7"), "Rt.tcaact.c.", kMajorSeventhDegrees, "D Eb E F F# G Ab A Bb B C Db");  // V7/iii
    CHECK(harmonicGuide(chord(D, "7"), kEbIonian).scaleMask == maskOf({Eb, Fs, G, A, Bb, C, D}));
    checkGuide(kEbIonian, chord(Eb, "7"), "R.t.ca.c.tca", kMajorSeventhDegrees, "Eb E F Gb G Ab A Bb B C Db D");  // V7/IV
    CHECK(harmonicGuide(chord(Eb, "7"), kEbIonian).scaleMask == maskOf({Eb, F, G, Ab, Bb, C, Db}));
    checkGuide(kEbIonian, chord(F, "7"), "R.ttca.c.tc.", kMajorSeventhDegrees, "F Gb G Ab A Bb B C Db D Eb E");  // V7/V
    CHECK(harmonicGuide(chord(F, "7"), kEbIonian).scaleMask == maskOf({Eb, F, G, A, Bb, C, D}));
    checkGuide(kEbIonian, chord(G, "7"), "Rt.tca.ct.c.", kMajorSeventhDegrees, "G Ab A Bb B C Db D Eb E F Gb");  // V7/vi
    CHECK(harmonicGuide(chord(G, "7"), kEbIonian).scaleMask == maskOf({Eb, F, G, Ab, B, C, D}));
}

TEST_CASE("harmonicGuide: borrowed iv in C: Ab replaces A, which is Avoid") {
    checkGuide(kCIonian, chord(F, "min"), "R.tca.tc.t.t", kMinorDegrees, "F F# G Ab A Bb B C Db D Eb E");
    CHECK(harmonicGuide(chord(F, "min"), kCIonian).scaleMask == maskOf({C, D, E, F, G, Ab, B}));
}

TEST_CASE("harmonicGuide: C# major letters are structural (F# on F over D major; E# on E over its own I)") {
    checkGuide(kCsIonian, chord(D, "maj"), "Ra.tc.tca.tt", kTriadDegrees, "D D# E E# F# G G# A A# B B# C#");
    CHECK(harmonicGuide(chord(D, "maj"), kCsIonian).scaleMask == maskOf({Db, D, F, Fs, Ab, A, C}));
    checkGuide(kCsIonian, chord(Db, "maj"), "R.t.ca.c.t.t", kTriadDegrees, "C# D D# E E# F# G G# A A# B B#");
    CHECK(harmonicGuide(chord(Db, "maj"), kCsIonian).name[F] == "E#");
}

TEST_CASE("harmonicGuide: power chord, aug, dim7") {
    // No third: the key's third is a plain Tension (no suspension to cancel).
    checkGuide(kCIonian, chord(C, "5"), "R.t.tt.c.t.t", kMinorDegrees, "C Db D Eb E F F# G Ab A Bb B");
    checkGuide(kCIonian, chord(C, "aug"), "R.t.ca.aca.t", "R b2 2 #2 3 4 b5 5 #5 6 b7 7",
               "C Db D Eb E F F# G G# A Bb B");
    CHECK(harmonicGuide(chord(C, "aug"), kCIonian).scaleMask == maskOf({C, D, E, F, Ab, A, B}));
    checkGuide(kCIonian, chord(B, "dim7"), "Ra.c.tc.tca.", "R b9 9 b3 3 11 b5 5 #5 bb7 b7 7",
               "B C Db D Eb E F F# G Ab A Bb");
    CHECK(harmonicGuide(chord(B, "dim7"), kCIonian).scaleMask == maskOf({C, D, E, F, G, Ab, B}));
}

TEST_CASE("harmonicGuide: suspended chords make the replaced third Avoid, in C and transposed") {
    checkGuide(kCIonian, chord(C, "sus4"), "R.t.ac.c.t.t", kMinorDegrees, "C Db D Eb E F F# G Ab A Bb B");
    checkGuide(kCIonian, chord(C, "sus2"), "R.c.at.c.t.t", kMinorDegrees, "C Db D Eb E F F# G Ab A Bb B");
    checkGuide(kDIonian, chord(D, "sus4"), "R.t.ac.c.t.t", kMinorDegrees, "D Eb E F F# G G# A Bb B C C#");
    checkGuide(kEbIonian, chord(Bb, "sus4"), "R.t.ac.c.tt.", kMinorDegrees, "Bb B C Db D Eb E F Gb G Ab A");
    checkGuide(kCIonian, chord(G, "7sus4"), "R.t.ac.c.tc.", "R b9 9 b3 3 4 #11 5 b13 13 b7 7",
               "G Ab A Bb B C Db D Eb E F F#");
}

TEST_CASE("harmonicGuide: compound chord tones (9, #11) and sixths") {
    checkGuide(kCIonian, chord(C, "9"), "R.c.ca.c.tca", kMajorSeventhDegrees, "C Db D Eb E F F# G Ab A Bb B");
    checkGuide(kCIonian, chord(C, "7#11"), "R.t.cacc.tca", kMajorSeventhDegrees, "C Db D Eb E F F# G Ab A Bb B");
    checkGuide(kCIonian, chord(C, "69"), "R.c.ca.c.c.t", "R b2 9 #2 3 4 #4 5 b6 6 b7 7",
               "C Db D Eb E F F# G Ab A Bb B");
    checkGuide(kCIonian, chord(C, "add9"), "R.c.ca.c.t.t", "R b2 9 #2 3 4 #4 5 b6 6 b7 7",
               "C Db D Eb E F F# G Ab A Bb B");
}

// --- invariants -----------------------------------------------------------------------

TEST_CASE("harmonicGuide: invariants over every type, root and eight keys") {
    const std::array<Key, 8> keys{kCIonian, kAAeolian, kAHarmonic, kAMelodic, kDDorian, kFsIonian, kEbIonian,
                                  kCsIonian};
    for (const Key& k : keys) {
        const std::array<PitchClass, 7> scale = scalePitchClasses(k.tonicPc(), k.mode);
        for (const ChordType& type : allChordTypes()) {
            for (PitchClass root = 0; root < 12; ++root) {
                const Chord c{root, &type};
                CAPTURE(keyName(k));
                CAPTURE(type.id);
                CAPTURE(root);
                const Guide g = harmonicGuide(c, k);
                const std::vector<PitchClass> tones = pitchClasses(c);
                int roots = 0;
                for (PitchClass pc = 0; pc < 12; ++pc) {
                    CAPTURE(pc);
                    const std::size_t i = static_cast<std::size_t>(pc);
                    const GuideRole role = g.role[i];
                    const bool inChord = std::find(tones.begin(), tones.end(), pc) != tones.end();
                    const bool inKey = std::find(scale.begin(), scale.end(), pc) != scale.end();
                    roots += role == GuideRole::Root ? 1 : 0;
                    CHECK((role == GuideRole::Root || role == GuideRole::ChordTone) == inChord);
                    if (inChord) CHECK(inMask(g.scaleMask, pc));
                    if (role == GuideRole::Outside) {
                        CHECK_FALSE(inMask(g.scaleMask, pc));
                        CHECK_FALSE(inKey);  // every key tone is in the chord scale or was replaced
                    }
                    if (inMask(g.scaleMask, pc)) CHECK(role != GuideRole::Outside);
                    CHECK(isPrintableAscii(g.degree[i]));
                    CHECK(isPrintableAscii(g.name[i]));
                    CHECK(pitchClassOfName(g.name[i]) == pc);
                }
                CHECK(roots == 1);
                CHECK(g.role[static_cast<std::size_t>(root)] == GuideRole::Root);
                CHECK(g.degree[static_cast<std::size_t>(root)] == "R");
                CHECK(g.name[static_cast<std::size_t>(root)] == spell(root, k).text());
                int scaleSize = 0;
                for (PitchClass pc = 0; pc < 12; ++pc) scaleSize += inMask(g.scaleMask, pc) ? 1 : 0;
                CHECK(scaleSize >= 7);
                CHECK(g.scaleMask < 4096);
            }
        }
    }
}

// --- snapshot -------------------------------------------------------------------------

TEST_CASE("snapshot: each event's guide is harmonicGuide in the project key, not the browsed key") {
    DocumentStore store;
    store.setProjectKey(kEbIonian);
    store.appendChord(chord(C, "7"), 4.0, 100);
    store.appendChord(chord(F, "min"), 4.0, 100);
    store.appendChord(chord(Bb, "sus4"), 4.0, 100);
    const Key browsed = key('A', 0, Mode::Ionian);
    store.setBrowsedKey(browsed);
    const Document& d = store.doc();
    const json snap = json::parse(snapshotJson(d, buildPalette(d.browsedKey, d.projectKey)));

    const auto guideJson = [](const Guide& g) {
        json roles = json::array();
        json degrees = json::array();
        json names = json::array();
        for (std::size_t pc = 0; pc < 12; ++pc) {
            roles.push_back(guideRoleName(g.role[pc]));
            degrees.push_back(g.degree[pc]);
            names.push_back(g.name[pc]);
        }
        return json{{"roles", roles}, {"scale", g.scaleMask}, {"degrees", degrees}, {"names", names}};
    };

    const json& events = snap.at("events");
    REQUIRE(events.size() == 3);
    for (std::size_t i = 0; i < events.size(); ++i) {
        CAPTURE(i);
        const Chord& c = d.events[i].chord;
        CHECK(events[i].at("guide") == guideJson(harmonicGuide(c, kEbIonian)));
        CHECK(events[i].at("guide") != guideJson(harmonicGuide(c, browsed)));
    }
    const json& c7 = events[0].at("guide");
    CHECK(c7.at("roles")[E] == "chord");
    CHECK(c7.at("roles")[Eb] == "tension");  // the replaced key tone, rescued as the #9
    CHECK(c7.at("names")[Db] == "Db");       // Eb's spelling; A major would say C#
    CHECK(c7.at("scale") == maskOf({C, D, E, F, G, Ab, Bb}));
    CHECK(c7.at("degrees")[Ab] == "b13");
}
