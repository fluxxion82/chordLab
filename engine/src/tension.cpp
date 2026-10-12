#include "chordlab/tension.h"

#include <algorithm>
#include <cassert>
#include <cstddef>

namespace chordlab {

namespace {

/// Roughness weight per interval class, index 0 = ic1 ... index 5 = ic6: semitone/major seventh and
/// whole tone/minor seventh beat hardest, the tritone next, thirds and sixths are mild, and the
/// perfect fourth/fifth is free. Integer flavour of the Plomp-Levelt dissonance curve (P2-D4).
constexpr std::array<int, 6> kRoughnessWeights{10, 6, 2, 1, 0, 5};

constexpr int kForeignPcPenalty = 15;  // raw points per chord tone outside the project scale
constexpr int kDensityPenalty = 8;     // raw points per chord tone beyond the triad
constexpr int kTriadSize = 3;

/// The distinct pitch classes of `pcs`, in first-seen order. Asserts every entry is canonical.
std::vector<PitchClass> uniquePitchClasses(const std::vector<PitchClass>& pcs) {
    std::vector<PitchClass> unique;
    unique.reserve(pcs.size());
    for (const PitchClass pc : pcs) {
        assert(pc >= 0 && pc < 12);
        if (std::find(unique.begin(), unique.end(), pc) == unique.end()) {
            unique.push_back(pc);
        }
    }
    return unique;
}

/// Interval class (1..6) between two distinct canonical pitch classes.
int intervalClass(PitchClass a, PitchClass b) {
    const int d = pitchClassOf(a - b);
    return d > 6 ? 12 - d : d;
}

}  // namespace

std::array<int, 6> intervalVector(const std::vector<PitchClass>& pcs) {
    const std::vector<PitchClass> unique = uniquePitchClasses(pcs);
    std::array<int, 6> vec{};
    for (std::size_t i = 0; i < unique.size(); ++i) {
        for (std::size_t j = i + 1; j < unique.size(); ++j) {
            ++vec[static_cast<std::size_t>(intervalClass(unique[i], unique[j]) - 1)];
        }
    }
    return vec;
}

int roughness(const std::vector<PitchClass>& pcs) {
    const std::array<int, 6> vec = intervalVector(pcs);
    int sum = 0;
    for (std::size_t ic = 0; ic < vec.size(); ++ic) {
        sum += vec[ic] * kRoughnessWeights[ic];
    }
    return sum;
}

int tension(const Chord& chord, const Key& projectKey) {
    assert(chord.type != nullptr);
    const std::vector<PitchClass> pcs = uniquePitchClasses(pitchClasses(chord));
    const PitchClass tonic = projectKey.tonicPc();

    int foreign = 0;
    for (const PitchClass pc : pcs) {
        if (!inScale(pc, tonic, projectKey.mode)) {
            ++foreign;
        }
    }
    const int beyondTriad = std::max(0, static_cast<int>(pcs.size()) - kTriadSize);

    const int raw = roughness(pcs) + kForeignPcPenalty * foreign + kDensityPenalty * beyondTriad;
    // Scale so that the reference ceiling maps to 100, rounding half up; raw is never negative, so
    // the integer division rounds the way we want and nothing here can approach overflow.
    const int scaled = (raw * 100 + kTensionRawCeiling / 2) / kTensionRawCeiling;
    return std::min(100, scaled);
}

}  // namespace chordlab
