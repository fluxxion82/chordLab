#include "chordlab/relations.h"

#include <cstddef>

namespace chordlab {

namespace {

constexpr int kFifth = 7;  // semitones in a perfect fifth

/// The canonical key on pitch class `pc` in `mode`.
Key canonicalKey(PitchClass pc, Mode mode) {
    return Key{canonicalTonic(pc, mode), mode};
}

}  // namespace

Key fifthsNeighbour(const Key& key, int steps) {
    // Only `steps` modulo 12 matters, so reduce it to 0..11 first: `kFifth * steps` (and the tonic
    // addition after it) would overflow for large |steps|. `steps % 12` is defined for every int,
    // INT_MIN included.
    const int s = ((steps % 12) + 12) % 12;
    return canonicalKey(pitchClassOf(key.tonicPc() + kFifth * s), key.mode);
}

std::vector<Key> parallelModes(const Key& key) {
    const PitchClass pc = key.tonicPc();
    std::vector<Key> modes;
    modes.reserve(static_cast<std::size_t>(kNumModes));
    for (int m = 0; m < kNumModes; ++m) {
        modes.push_back(canonicalKey(pc, static_cast<Mode>(m)));
    }
    return modes;
}

Key relativeKey(const Key& key) {
    constexpr int kMinorThird = 3;
    switch (key.mode) {
        case Mode::Ionian: return canonicalKey(pitchClassOf(key.tonicPc() - kMinorThird), Mode::Aeolian);
        case Mode::Aeolian: return canonicalKey(pitchClassOf(key.tonicPc() + kMinorThird), Mode::Ionian);
        default: return key;
    }
}

int fifthsDistance(const Key& from, const Key& to) {
    // Seven fifths span four octaves plus a semitone (7 * 7 = 49 = 4 * 12 + 1), so the inverse of
    // "times 7" modulo 12 is itself "times 7": the step count is the semitone difference times 7,
    // reduced to -5..6. Both tonics are pitch classes (0..11), so every intermediate value here stays
    // below 100 and nothing can overflow.
    const int semitones = pitchClassOf(to.tonicPc() - from.tonicPc());
    const int steps = pitchClassOf(semitones * kFifth);
    return steps > 6 ? steps - 12 : steps;
}

}  // namespace chordlab
