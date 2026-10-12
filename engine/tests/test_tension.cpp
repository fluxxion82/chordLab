#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/pitch.h"
#include "chordlab/scale.h"
#include "chordlab/tension.h"

#include <array>
#include <cstddef>
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

using Vec = std::array<int, 6>;

constexpr std::array<Mode, kNumModes> kAllModes{
    Mode::Ionian,     Mode::Dorian,  Mode::Phrygian,      Mode::Lydian,       Mode::Mixolydian,
    Mode::Aeolian,    Mode::Locrian, Mode::HarmonicMinor, Mode::MelodicMinor,
};

/// Interval class (1..6) between two pitch classes, computed independently of the module under test.
int intervalClass(PitchClass a, PitchClass b) {
    const int d = ((a - b) % 12 + 12) % 12;
    return d > 6 ? 12 - d : d;
}

}  // namespace

// --- intervalVector ----------------------------------------------------------------

TEST_CASE("intervalVector: C major triad {0,4,7} is [0,0,1,1,1,0]") {
    CHECK(intervalVector({0, 4, 7}) == Vec{0, 0, 1, 1, 1, 0});
}

TEST_CASE("intervalVector: diminished seventh {0,3,6,9} is [0,0,4,0,0,2]") {
    CHECK(intervalVector({0, 3, 6, 9}) == Vec{0, 0, 4, 0, 0, 2});
}

TEST_CASE("intervalVector: whole-tone sets") {
    CHECK(intervalVector({0, 4, 8}) == Vec{0, 0, 0, 3, 0, 0});                 // augmented triad
    CHECK(intervalVector({0, 2, 4, 6, 8, 10}) == Vec{0, 6, 0, 6, 0, 3});       // whole-tone hexachord
}

TEST_CASE("intervalVector: the seventh chords used by the tension ordering") {
    CHECK(intervalVector({0, 4, 7, 9}) == Vec{0, 1, 2, 1, 2, 0});   // C6
    CHECK(intervalVector({0, 4, 7, 11}) == Vec{1, 0, 1, 2, 2, 0});  // CΔ7
    CHECK(intervalVector({0, 4, 7, 10}) == Vec{0, 1, 2, 1, 1, 1});  // C7
}

TEST_CASE("intervalVector: does not depend on the order of the set") {
    CHECK(intervalVector({7, 0, 4}) == intervalVector({0, 4, 7}));
    CHECK(intervalVector({10, 7, 4, 0}) == intervalVector({0, 4, 7, 10}));
}

TEST_CASE("intervalVector: duplicate pitch classes are counted once") {
    CHECK(intervalVector({0, 4, 7, 0, 4}) == Vec{0, 0, 1, 1, 1, 0});
    CHECK(intervalVector({0, 0}) == Vec{0, 0, 0, 0, 0, 0});
    CHECK(intervalVector({5, 5, 5, 5}) == Vec{0, 0, 0, 0, 0, 0});
}

TEST_CASE("intervalVector: fewer than two distinct classes gives all zeros") {
    CHECK(intervalVector({}) == Vec{0, 0, 0, 0, 0, 0});
    CHECK(intervalVector({3}) == Vec{0, 0, 0, 0, 0, 0});
}

TEST_CASE("intervalVector: a single pair lands in its own interval class") {
    for (int ic = 1; ic <= 6; ++ic) {
        CAPTURE(ic);
        Vec expected{};
        expected[static_cast<std::size_t>(ic - 1)] = 1;
        CHECK(intervalVector({0, ic}) == expected);
        CHECK(intervalVector({0, 12 - ic}) == expected);  // the inversion is the same class
    }
}

TEST_CASE("intervalVector: entries sum to n(n-1)/2 and agree with a pairwise count, for every chord type") {
    for (const ChordType& t : allChordTypes()) {
        for (PitchClass root = 0; root < 12; ++root) {
            CAPTURE(t.id);
            CAPTURE(root);
            const auto pcs = pitchClasses(Chord{root, &t});
            const Vec vec = intervalVector(pcs);
            Vec expected{};
            for (std::size_t i = 0; i < pcs.size(); ++i) {
                for (std::size_t j = i + 1; j < pcs.size(); ++j) {
                    ++expected[static_cast<std::size_t>(intervalClass(pcs[i], pcs[j]) - 1)];
                }
            }
            CHECK(vec == expected);
            int total = 0;
            for (const int n : vec) {
                total += n;
            }
            const int n = static_cast<int>(pcs.size());
            CHECK(total == n * (n - 1) / 2);
        }
    }
}

// --- roughness ---------------------------------------------------------------------

TEST_CASE("roughness: C major triad is 3 (ic3 2 + ic4 1)") {
    CHECK(roughness({0, 4, 7}) == 3);
}

TEST_CASE("roughness: minor triad equals major (same interval vector)") {
    CHECK(roughness({0, 3, 7}) == 3);
}

TEST_CASE("roughness: Cdim7 is 18 (4 x ic3 + 2 x ic6)") {
    CHECK(roughness({0, 3, 6, 9}) == 4 * 2 + 2 * 5);
}

TEST_CASE("roughness: weights per interval class are ic1 10, ic2 6, ic3 2, ic4 1, ic5 0, ic6 5") {
    CHECK(roughness({0, 1}) == 10);
    CHECK(roughness({0, 2}) == 6);
    CHECK(roughness({0, 3}) == 2);
    CHECK(roughness({0, 4}) == 1);
    CHECK(roughness({0, 5}) == 0);
    CHECK(roughness({0, 6}) == 5);
}

TEST_CASE("roughness: plan seventh chords") {
    CHECK(roughness({0, 4, 7, 9}) == 11);   // C6:  ic2 6 + 2 x ic3 4 + ic4 1
    CHECK(roughness({0, 4, 7, 11}) == 14);  // CΔ7: ic1 10 + ic3 2 + 2 x ic4 2
    CHECK(roughness({0, 4, 7, 10}) == 16);  // C7:  ic2 6 + 2 x ic3 4 + ic4 1 + ic6 5
}

TEST_CASE("roughness: empty, single and duplicated sets score 0") {
    CHECK(roughness({}) == 0);
    CHECK(roughness({7}) == 0);
    CHECK(roughness({7, 7, 7}) == 0);
}

TEST_CASE("roughness: is transposition-invariant for every chord type") {
    for (const ChordType& t : allChordTypes()) {
        CAPTURE(t.id);
        const int atC = roughness(pitchClasses(Chord{0, &t}));
        for (PitchClass root = 1; root < 12; ++root) {
            CAPTURE(root);
            CHECK(roughness(pitchClasses(Chord{root, &t})) == atC);
        }
    }
}

// --- tension -------------------------------------------------------------------------

TEST_CASE("tension: reference ceiling is documented as 90") {
    CHECK(kTensionRawCeiling == 90);
}

TEST_CASE("tension: ordering in C major C < C6 < CΔ7 < C7 < Cdim7") {
    const Key cMajor = key('C', 0, Mode::Ionian);
    const int c = tension(chord(0, "maj"), cMajor);
    const int c6 = tension(chord(0, "6"), cMajor);
    const int cMaj7 = tension(chord(0, "maj7"), cMajor);
    const int c7 = tension(chord(0, "7"), cMajor);
    const int cDim7 = tension(chord(0, "dim7"), cMajor);
    CAPTURE(c);
    CAPTURE(c6);
    CAPTURE(cMaj7);
    CAPTURE(c7);
    CAPTURE(cDim7);
    CHECK(c < c6);
    CHECK(c6 < cMaj7);
    CHECK(cMaj7 < c7);
    CHECK(c7 < cDim7);
}

TEST_CASE("tension: exact values from the P2-D4 formula with the 90 ceiling") {
    const Key cMajor = key('C', 0, Mode::Ionian);
    // raw = roughness + 15 x foreign pcs + 8 x max(0, size - 3); tension = round(raw x 100 / 90).
    CHECK(tension(chord(0, "maj"), cMajor) == 3);      // 3 + 0 + 0 = 3
    CHECK(tension(chord(0, "6"), cMajor) == 21);       // 11 + 0 + 8 = 19
    CHECK(tension(chord(0, "maj7"), cMajor) == 24);    // 14 + 0 + 8 = 22
    CHECK(tension(chord(0, "7"), cMajor) == 43);       // 16 + 15 (Bb) + 8 = 39
    CHECK(tension(chord(0, "dim7"), cMajor) == 62);    // 18 + 30 (Eb, F#) + 8 = 56
    CHECK(tension(chord(8, "maj"), cMajor) == 37);     // 3 + 30 (Ab, Eb) + 0 = 33
    CHECK(tension(chord(9, "min"), cMajor) == 3);      // Am: 3 + 0 + 0 = 3
    CHECK(tension(chord(9, "m7"), cMajor) == 21);      // Am7: 11 + 0 + 8 = 19
    CHECK(tension(chord(11, "dim"), cMajor) == 10);    // Bdim: 9 + 0 + 0 = 9
    CHECK(tension(chord(7, "7"), cMajor) == 27);       // G7: 16 + 0 + 8 = 24
    CHECK(tension(chord(7, "9"), cMajor) == 50);       // G9: 29 + 0 + 16 = 45
    CHECK(tension(chord(0, "7"), key('F', 0, Mode::Ionian)) == 27);  // C7 in F: 16 + 0 + 8 = 24
    CHECK(tension(chord(1, "9"), cMajor) == 100);      // C#9: 29 + 45 (C#, G#, D#) + 16 = 90, at the ceiling
}

TEST_CASE("tension: is a colour axis, not a resolution axis (borrowed Ab scores above diatonic G7)") {
    // Documented in tension.h: chromaticism counts, harmonic function does not (that is P2-D1's job).
    const Key cMajor = key('C', 0, Mode::Ionian);
    CHECK(tension(chord(8, "maj"), cMajor) > tension(chord(7, "7"), cMajor));
}

TEST_CASE("tension: Am < Am7 in C major") {
    const Key cMajor = key('C', 0, Mode::Ionian);
    CHECK(tension(chord(9, "min"), cMajor) < tension(chord(9, "m7"), cMajor));
}

TEST_CASE("tension: C (in key) < Ab (two foreign pitch classes) in C major") {
    const Key cMajor = key('C', 0, Mode::Ionian);
    CHECK(tension(chord(0, "maj"), cMajor) < tension(chord(8, "maj"), cMajor));
}

TEST_CASE("tension: the out-of-key penalty depends on the project key") {
    // C7 carries Bb: foreign in C major, diatonic in F major.
    const Chord c7 = chord(0, "7");
    const int inC = tension(c7, key('C', 0, Mode::Ionian));
    const int inF = tension(c7, key('F', 0, Mode::Ionian));
    CAPTURE(inC);
    CAPTURE(inF);
    CHECK(inC > inF);
    CHECK(inC == 43);  // raw 39: one foreign pc adds 15 raw, about 17 tension points at the 90 ceiling
    CHECK(inF == 27);  // raw 24
}

TEST_CASE("tension: diatonic triads score the same regardless of root") {
    const Key cMajor = key('C', 0, Mode::Ionian);
    const int c = tension(chord(0, "maj"), cMajor);
    CHECK(tension(chord(2, "min"), cMajor) == c);
    CHECK(tension(chord(4, "min"), cMajor) == c);
    CHECK(tension(chord(5, "maj"), cMajor) == c);
    CHECK(tension(chord(7, "maj"), cMajor) == c);
    CHECK(tension(chord(9, "min"), cMajor) == c);
}

TEST_CASE("tension: enharmonic project keys agree") {
    const Chord gFlat = chord(6, "maj");
    CHECK(tension(gFlat, key('G', -1, Mode::Ionian)) == tension(gFlat, key('F', +1, Mode::Ionian)));
    CHECK(tension(gFlat, key('G', -1, Mode::Ionian)) == 3);
}

TEST_CASE("tension: clamps at 100 only for genuinely extreme sets") {
    const Key cMajor = key('C', 0, Mode::Ionian);
    // F#9 in C major: F# A# C# E G# -> four foreign pcs; raw = 29 + 60 + 16 = 105 -> 117 -> clamped.
    CHECK(tension(chord(6, "9"), cMajor) == 100);
    // Ab9: Ab C Eb Gb Bb -> four foreign pcs, raw 105 as well.
    CHECK(tension(chord(8, "9"), cMajor) == 100);
    // Just below the ceiling the scale still resolves: C#m9 (raw 89) is 99, Bb9 (two foreign, raw 75) 83.
    CHECK(tension(chord(1, "m9"), cMajor) == 99);
    CHECK(tension(chord(10, "9"), cMajor) == 83);
}

TEST_CASE("tension: deterministic and within 0..100 for every type x root x canonical key") {
    int evaluated = 0;
    for (const ChordType& t : allChordTypes()) {
        for (PitchClass root = 0; root < 12; ++root) {
            const Chord c{root, &t};
            for (PitchClass tonic = 0; tonic < 12; ++tonic) {
                for (const Mode mode : kAllModes) {
                    const Key k{canonicalTonic(tonic, mode), mode};
                    const int first = tension(c, k);
                    const int second = tension(c, k);
                    ++evaluated;
                    if (first != second || first < 0 || first > 100) {
                        CAPTURE(t.id);
                        CAPTURE(root);
                        CAPTURE(tonic);
                        CAPTURE(static_cast<int>(mode));
                        CHECK(first == second);
                        CHECK(first >= 0);
                        CHECK(first <= 100);
                    }
                }
            }
        }
    }
    CHECK(evaluated == static_cast<int>(allChordTypes().size()) * 12 * 12 * kNumModes);  // the loop only reports failures
}

TEST_CASE("tension: a fully diatonic chord never exceeds the roughness-plus-density score") {
    // Without the key penalty the roughest in-key chord is the dominant seventh with a sharp eleventh
    // (the Lydian dominant on the fourth degree of melodic minor): 38 + 16 = 54 raw -> 60. A dominant
    // ninth is 29 + 16 = 45 raw -> 50.
    int inKey = 0;
    int highest = 0;
    for (const ChordType& t : allChordTypes()) {
        for (PitchClass root = 0; root < 12; ++root) {
            const Chord c{root, &t};
            for (PitchClass tonic = 0; tonic < 12; ++tonic) {
                for (const Mode mode : kAllModes) {
                    const Key k{canonicalTonic(tonic, mode), mode};
                    if (fullyInKey(c, k)) {
                        ++inKey;
                        CAPTURE(t.id);
                        CAPTURE(root);
                        const int score = tension(c, k);
                        CHECK(score <= 60);
                        if (score > highest) {
                            highest = score;
                        }
                    }
                }
            }
        }
    }
    CHECK(inKey > 0);
    CHECK(highest == 60);
}
