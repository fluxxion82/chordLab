#pragma once

#include "chordlab/chord.h"
#include "chordlab/pitch.h"
#include "chordlab/scale.h"

#include <array>
#include <vector>

namespace chordlab {

/// Interval vector of a pitch-class set: the number of unordered pairs at each interval class 1..6
/// (index 0 is ic1, a semitone or major seventh; index 5 is ic6, the tritone). Duplicate pitch classes
/// are counted once, so the entries sum to n(n-1)/2 for n distinct classes, and the result does not
/// depend on the order of `pcs`. A set with fewer than two distinct classes gives all zeros.
/// Every element of `pcs` must be a canonical pitch class (0..11); anything else is a precondition
/// violation.
std::array<int, 6> intervalVector(const std::vector<PitchClass>& pcs);

/// Sensory roughness of a pitch-class set: the interval vector weighted per interval class with
/// Plomp-Levelt-flavoured integers (ic1 10, ic2 6, ic3 2, ic4 1, ic5 0, ic6 5). A major or minor
/// triad scores 3, a dominant seventh 16, a diminished seventh 18. Same precondition as
/// `intervalVector`.
int roughness(const std::vector<PitchClass>& pcs);

/// Tension of `chord` heard in `projectKey`, 0..100 (Decision P2-D4). The raw score is
/// `roughness(pcs) + 15 * (pitch classes outside the key's scale) + 8 * max(0, pcs - 3)`, so colour
/// tones, chromaticism and density each add up; it is then scaled so that `kTensionRawCeiling`
/// maps to 100 and anything above is clamped: `min(100, round(raw * 100 / 60))`. In C major this
/// gives C 5, C6 32, CΔ7 37, C7 65, Cdim7 93, Ab 55. Deterministic. `chord.type` must be non-null.
int tension(const Chord& chord, const Key& projectKey);

/// The raw score that maps to tension 100: a reference ceiling around a dominant seventh with a flat
/// ninth on a foreign root (roughly "C7b9 out of key"), chosen so that diatonic triads sit near the
/// bottom and chromatic seventh chords near the top. A tuning constant, not a measurement; revisit
/// once the UI's tension bar has been looked at with real progressions.
constexpr int kTensionRawCeiling = 60;

}  // namespace chordlab
