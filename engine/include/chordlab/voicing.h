#pragma once

#include "chordlab/chord.h"

#include <vector>

namespace chordlab {

/// Root-position close voicing of `chord` as MIDI note numbers in `lowest`..`highest` (default 48..84).
/// The root is the first note at or above `lowest` with the chord's root pitch class; every interval is
/// stacked above it, and any note above `highest` drops by octaves until it fits. The result is sorted
/// ascending with duplicates removed, so a folded extension (a ninth landing a semitone above the root)
/// sits inside the chord rather than on top of it.
/// A range narrower than two octaves, or outside 0..127, is a programmer error: it is ignored and the
/// defaults are used. Two octaves guarantee that every folded note still lands at or above `lowest`.
std::vector<int> closeVoicing(const Chord& chord, int lowest = 48, int highest = 84);

}  // namespace chordlab
