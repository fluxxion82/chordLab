#include "chordlab/identify.h"

#include <algorithm>
#include <cstddef>

namespace chordlab {

namespace {

constexpr int kOctave = 12;

/// Bit mask of the pitch classes of `chord`.
unsigned maskOf(const Chord& chord) {
    unsigned mask = 0;
    for (const PitchClass pc : pitchClasses(chord)) {
        mask |= 1u << static_cast<unsigned>(pc);
    }
    return mask;
}

}  // namespace

std::optional<HeardChord> identifyChord(const std::vector<int>& heldPitches) {
    if (heldPitches.empty()) {
        return std::nullopt;
    }
    const int bass = *std::min_element(heldPitches.begin(), heldPitches.end());
    const int bassClass = ((bass % kOctave) + kOctave) % kOctave;

    unsigned held = 0;
    for (const int pitch : heldPitches) {
        held |= 1u << static_cast<unsigned>(((pitch % kOctave) + kOctave) % kOctave);
    }
    int count = 0;
    for (unsigned m = held; m != 0; m &= m - 1) {
        ++count;
    }
    if (count < 2) {
        return std::nullopt;
    }

    // Rank key: (root differs from bass, tone count, dictionary index); smaller wins.
    bool found = false;
    Chord best{0, nullptr};
    int bestRootRank = 0;
    std::size_t bestTones = 0;
    std::size_t bestIndex = 0;

    const std::vector<ChordType>& types = allChordTypes();
    for (std::size_t index = 0; index < types.size(); ++index) {
        for (int root = 0; root < kOctave; ++root) {
            if ((held & (1u << static_cast<unsigned>(root))) == 0) {
                continue;
            }
            const Chord candidate{root, &types[index]};
            if (maskOf(candidate) != held) {
                continue;
            }
            const int rootRank = root == bassClass ? 0 : 1;
            const std::size_t tones = types[index].intervals.size();
            const bool better = !found || rootRank < bestRootRank ||
                                (rootRank == bestRootRank &&
                                 (tones < bestTones || (tones == bestTones && index < bestIndex)));
            if (better) {
                found = true;
                best = candidate;
                bestRootRank = rootRank;
                bestTones = tones;
                bestIndex = index;
            }
        }
    }
    if (!found) {
        return std::nullopt;
    }

    const std::vector<PitchClass> pcs = pitchClasses(best);
    const auto at = std::find(pcs.begin(), pcs.end(), bassClass);
    return HeardChord{best, bass, static_cast<int>(at - pcs.begin())};
}

}  // namespace chordlab
