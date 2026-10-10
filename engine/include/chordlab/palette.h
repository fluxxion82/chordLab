#pragma once

#include "chordlab/chord.h"
#include "chordlab/pitch.h"

#include <string>
#include <vector>

namespace chordlab {

/// Number of rows in a palette grid: tier 0 is the power chord, 1 the triad, 2 the sixth/seventh,
/// 3 the seventh/add9 and 4 the colour row (ninths, 6/9, suspended chords).
constexpr int kPaletteTiers = 5;

/// One chord of the palette grid: the chord built on scale degree `degree` (0..6) of the browsed key, in
/// row `tier` (0..kPaletteTiers-1).
/// `filled` is whether every note is in the project key (hollow cells are borrowed); `symbol` is the
/// Klimper-style chord symbol spelled in the browsed key; `roman` is the Standard-style numeral relative
/// to the project key.
struct PaletteCell {
    int degree;
    int tier;
    Chord chord;
    bool filled;
    std::string symbol;
    std::string roman;
};

/// The grid of chords for a browsed key, laid out as Klimper does. `cells` holds only the cells that
/// exist (a column may have gaps), ordered by tier then degree; `tiers` is one more than the highest
/// tier present (always >= 2, since every degree has a triad; kPaletteTiers when every row has a cell).
struct Palette {
    std::vector<PaletteCell> cells;
    int tiers;
};

/// Builds the palette of `browsedKey`, annotated against `projectKey`.
///
/// Per degree d of the browsed scale, with that degree as the root:
/// - Tier 1 is the stacked-third triad on degrees d, d+2, d+4 of the browsed scale, typed as whichever of
///   maj/min/dim/aug has exactly those intervals (every supported mode yields one of the four).
/// - Tier 0 is the power chord, present when the perfect fifth is in the browsed scale.
/// - Tier 2 is the first of [6, m6, m7, m7b5, 7, maj7, mMaj7, dim7, augMaj7] fully in the browsed key.
/// - Tier 3 is the first of [maj7, 7, m7, mMaj7, dim7, augMaj7, add9, madd9, m7b5] fully in the browsed
///   key and not already used in the column.
/// - Tier 4 is the first of [9, maj9, m9, 69, 7sus4, sus4, sus2] fully in the browsed key and not already
///   used in the column.
/// Tiers with no candidate are left out. Every cell is fully in the browsed key, and no chord type repeats
/// within a column.
Palette buildPalette(const Key& browsedKey, const Key& projectKey);

}  // namespace chordlab
