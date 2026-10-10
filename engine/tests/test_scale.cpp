#include "doctest.h"

#include "chordlab/scale.h"

#include <array>

using namespace chordlab;

TEST_CASE("scalePitchClasses: C Ionian") {
    const std::array<PitchClass, 7> expected{0, 2, 4, 5, 7, 9, 11};
    CHECK(scalePitchClasses(0, Mode::Ionian) == expected);
}

TEST_CASE("scalePitchClasses: A Aeolian wraps past the octave") {
    const std::array<PitchClass, 7> expected{9, 11, 0, 2, 4, 5, 7};
    CHECK(scalePitchClasses(9, Mode::Aeolian) == expected);
}

TEST_CASE("scalePitchClasses: D Dorian") {
    const std::array<PitchClass, 7> expected{2, 4, 5, 7, 9, 11, 0};
    CHECK(scalePitchClasses(2, Mode::Dorian) == expected);
}

TEST_CASE("scalePitchClasses: A HarmonicMinor has a raised 7th") {
    const std::array<PitchClass, 7> expected{9, 11, 0, 2, 4, 5, 8};
    CHECK(scalePitchClasses(9, Mode::HarmonicMinor) == expected);
}

TEST_CASE("scalePitchClasses: A MelodicMinor has raised 6th and 7th") {
    const std::array<PitchClass, 7> expected{9, 11, 0, 2, 4, 6, 8};
    CHECK(scalePitchClasses(9, Mode::MelodicMinor) == expected);
}

TEST_CASE("inScale: F# is not in C Ionian") {
    CHECK(inScale(6, 0, Mode::Ionian) == false);
}

TEST_CASE("degreeOf: G is degree 4 of C Ionian") {
    CHECK(degreeOf(7, 0, Mode::Ionian) == 4);
}

TEST_CASE("pitchClassOf: MIDI note to pitch class") {
    CHECK(pitchClassOf(61) == 1);
    CHECK(pitchClassOf(71) == 11);
}

TEST_CASE("pitchClassOf: negative MIDI values wrap into 0..11") {
    CHECK(pitchClassOf(-1) == 11);
    CHECK(pitchClassOf(-12) == 0);
    CHECK(pitchClassOf(-13) == 11);
    CHECK(pitchClassOf(0) == 0);
    CHECK(pitchClassOf(12) == 0);
}

TEST_CASE("degreeOf: returns -1 for a pitch class outside the scale") {
    CHECK(degreeOf(6, 0, Mode::Ionian) == -1);
    CHECK(degreeOf(1, 0, Mode::Ionian) == -1);
}

TEST_CASE("degreeOf: every scale tone maps to its index") {
    const auto pcs = scalePitchClasses(9, Mode::Aeolian);
    for (int i = 0; i < 7; ++i) {
        CHECK(degreeOf(pcs[static_cast<std::size_t>(i)], 9, Mode::Aeolian) == i);
    }
}

TEST_CASE("inScale: scale tones are in, non-scale tones are out") {
    CHECK(inScale(0, 0, Mode::Ionian));
    CHECK(inScale(11, 0, Mode::Ionian));
    CHECK_FALSE(inScale(10, 0, Mode::Ionian));
    CHECK(inScale(10, 0, Mode::Mixolydian));
}

TEST_CASE("kNumModes is 9") {
    CHECK(kNumModes == 9);
}

TEST_CASE("modeIntervals: all nine tables") {
    using A = std::array<int, 7>;
    CHECK(modeIntervals(Mode::Ionian)        == A{0, 2, 4, 5, 7, 9, 11});
    CHECK(modeIntervals(Mode::Dorian)        == A{0, 2, 3, 5, 7, 9, 10});
    CHECK(modeIntervals(Mode::Phrygian)      == A{0, 1, 3, 5, 7, 8, 10});
    CHECK(modeIntervals(Mode::Lydian)        == A{0, 2, 4, 6, 7, 9, 11});
    CHECK(modeIntervals(Mode::Mixolydian)    == A{0, 2, 4, 5, 7, 9, 10});
    CHECK(modeIntervals(Mode::Aeolian)       == A{0, 2, 3, 5, 7, 8, 10});
    CHECK(modeIntervals(Mode::Locrian)       == A{0, 1, 3, 5, 6, 8, 10});
    CHECK(modeIntervals(Mode::HarmonicMinor) == A{0, 2, 3, 5, 7, 8, 11});
    CHECK(modeIntervals(Mode::MelodicMinor)  == A{0, 2, 3, 5, 7, 9, 11});
}

TEST_CASE("modeIntervals: every table starts at 0 and is strictly ascending below 12") {
    for (int m = 0; m < kNumModes; ++m) {
        const auto& iv = modeIntervals(static_cast<Mode>(m));
        CHECK(iv[0] == 0);
        for (std::size_t i = 1; i < iv.size(); ++i) {
            CHECK(iv[i] > iv[i - 1]);
            CHECK(iv[i] < 12);
        }
    }
}
