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

/// Tension of `chord` heard in `projectKey`, 0..100 (Decision P2-D4).
///
/// This is a colour/dissonance axis, not a "wants to resolve" axis: it measures how far the sound is
/// from a plain diatonic triad (interval roughness, chromaticism against the project key, density)
/// and says nothing about harmonic function, which is the function label's job (P2-D1). So in C major
/// the borrowed Ab triad (two foreign pitch classes) scores 37 while the diatonic dominant G7 scores
/// 27: Ab is the more colourful sound, G7 the one that pulls home.
///
/// The raw score is `roughness(pcs) + 15 * (pitch classes outside the key's scale) + 8 * max(0, pcs - 3)`,
/// scaled so that `kTensionRawCeiling` maps to 100 and clamped above it:
/// `min(100, round(raw * 100 / kTensionRawCeiling))`. In C major: C 3, C6 21, CΔ7 24, C7 43,
/// Cdim7 62, Ab 37, Am 3, Am7 21, Bdim 10, G7 27, G9 50, C#9 100. Deterministic. `chord.type` must
/// be non-null.
int tension(const Chord& chord, const Key& projectKey);

/// The raw score that maps to tension 100. Chosen so that the top end spreads rather than saturates:
/// a dominant seventh with a flat ninth (not in the dictionary; raw 81 in C major) lands at 90, the
/// dictionary's dominant ninths on chromatic roots reach the 80s to 100 depending on how many of their
/// tones are foreign, and only genuinely extreme sets clamp (a dominant ninth with four foreign pitch
/// classes, e.g. F#9 in C major at raw 105). A tuning constant, not a measurement; revisit once the
/// UI's tension bar has been looked at with real progressions.
constexpr int kTensionRawCeiling = 90;

}  // namespace chordlab
