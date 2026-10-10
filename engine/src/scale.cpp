#include "chordlab/scale.h"

#include <cstddef>

namespace chordlab {

namespace {

constexpr std::array<std::array<int, 7>, kNumModes> kModeIntervals{{
    {0, 2, 4, 5, 7, 9, 11},  // Ionian
    {0, 2, 3, 5, 7, 9, 10},  // Dorian
    {0, 1, 3, 5, 7, 8, 10},  // Phrygian
    {0, 2, 4, 6, 7, 9, 11},  // Lydian
    {0, 2, 4, 5, 7, 9, 10},  // Mixolydian
    {0, 2, 3, 5, 7, 8, 10},  // Aeolian
    {0, 1, 3, 5, 6, 8, 10},  // Locrian
    {0, 2, 3, 5, 7, 8, 11},  // HarmonicMinor
    {0, 2, 3, 5, 7, 9, 11},  // MelodicMinor
}};

}  // namespace

const std::array<int, 7>& modeIntervals(Mode mode) {
    return kModeIntervals[static_cast<std::size_t>(mode)];
}

std::array<PitchClass, 7> scalePitchClasses(PitchClass tonic, Mode mode) {
    const auto& intervals = modeIntervals(mode);
    std::array<PitchClass, 7> result{};
    for (std::size_t i = 0; i < result.size(); ++i) {
        result[i] = pitchClassOf(tonic + intervals[i]);
    }
    return result;
}

int degreeOf(PitchClass pc, PitchClass tonic, Mode mode) {
    const auto pcs = scalePitchClasses(tonic, mode);
    const PitchClass target = pitchClassOf(pc);
    for (std::size_t i = 0; i < pcs.size(); ++i) {
        if (pcs[i] == target) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool inScale(PitchClass pc, PitchClass tonic, Mode mode) {
    return degreeOf(pc, tonic, mode) >= 0;
}

PitchClass pitchClassOf(int midi) {
    return ((midi % 12) + 12) % 12;
}

}  // namespace chordlab
