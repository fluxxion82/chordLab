#include "chordlab/identify.h"

#include <algorithm>
#include <cstddef>

namespace chordlab {

namespace {

constexpr int kOctave = 12;

/// Bit mask of the pitch classes of the chord of `type` on `root` (0..11).
unsigned maskOf(int root, const ChordType& type) {
    unsigned mask = 0;
    for (const int interval : type.intervals) {
        mask |= 1u << static_cast<unsigned>((((root + interval) % kOctave) + kOctave) % kOctave);
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
            if (maskOf(root, types[index]) != held) {
                continue;
            }
            const int rootRank = root == bassClass ? 0 : 1;
            // Exact matching makes the tone count a no-op today (every type has distinct classes, so
            // every match has popcount(held) tones); it goes live only if a type with duplicate classes is added.
            const std::size_t tones = types[index].intervals.size();
            const bool better = !found || rootRank < bestRootRank ||
                                (rootRank == bestRootRank &&
                                 (tones < bestTones || (tones == bestTones && index < bestIndex)));
            if (better) {
                found = true;
                best = Chord{root, &types[index]};
                bestRootRank = rootRank;
                bestTones = tones;
                bestIndex = index;
            }
        }
    }
    if (!found) {
        return std::nullopt;
    }

    int inversion = 0;
    for (const int interval : best.type->intervals) {
        if ((((best.root + interval) % kOctave) + kOctave) % kOctave == bassClass) {
            break;
        }
        ++inversion;
    }
    return HeardChord{best, bass, inversion};
}

}  // namespace chordlab
