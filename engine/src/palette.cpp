#include "chordlab/palette.h"

#include "chordlab/scale.h"
#include "chordlab/symbol.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

namespace chordlab {

namespace {

constexpr int kPowerTier = 0;
constexpr int kTriadTier = 1;
constexpr int kSixthSeventhTier = 2;
constexpr int kSeventhTier = 3;
constexpr int kColourTier = 4;
static_assert(kColourTier + 1 == kPaletteTiers, "palette tiers out of step with kPaletteTiers");

/// Candidate types per tier, in preference order. The first candidate fully in the browsed key wins.
constexpr std::array<std::string_view, 9> kSixthSeventhCandidates{"6",    "m6",    "m7",   "m7b5",   "7",
                                                                   "maj7", "mMaj7", "dim7", "augMaj7"};
constexpr std::array<std::string_view, 9> kSeventhCandidates{"maj7",    "7",    "m7",    "mMaj7", "dim7",
                                                             "augMaj7", "add9", "madd9", "m7b5"};
constexpr std::array<std::string_view, 10> kColourCandidates{"9",    "maj9", "m9",    "mMaj9", "maj7#11",
                                                             "7#11", "69",   "7sus4", "sus4",  "sus2"};

/// The browsed mode's characteristic chord and the tier the tonic column offers it in first; `nullptr`
/// for modes whose characteristic chord the general order already picks (see palette.h).
struct TonicPreference {
    int tier;
    const char* typeId;
};

TonicPreference tonicPreferenceOf(Mode mode) {
    switch (mode) {
        case Mode::Mixolydian:
            return {kSixthSeventhTier, "7"};
        case Mode::Lydian:
            return {kColourTier, "maj7#11"};
        default:
            return {-1, nullptr};
    }
}

/// The chord types chosen for one degree, indexed by tier; nullptr where the tier is empty.
using Column = std::array<const ChordType*, kPaletteTiers>;

bool usedIn(const Column& column, const ChordType& type) {
    return std::find(column.begin(), column.end(), &type) != column.end();
}

/// The first candidate whose chord on `root` is fully in `key` and not already in `column`, or nullptr.
template <std::size_t N>
const ChordType* firstUnusedInKey(const std::array<std::string_view, N>& candidates, PitchClass root,
                                  const Key& key, const Column& column) {
    for (const std::string_view id : candidates) {
        const ChordType& type = chordType(id);
        if (!usedIn(column, type) && fullyInKey(Chord{root, &type}, key)) {
            return &type;
        }
    }
    return nullptr;
}

/// The preferred type for `tier`, when `preference` names it, is unused and is fully in `key`; else
/// the first candidate that is.
template <std::size_t N>
const ChordType* preferredOrFirstUnusedInKey(const TonicPreference& preference, int tier,
                                             const std::array<std::string_view, N>& candidates, PitchClass root,
                                             const Key& key, const Column& column) {
    if (preference.typeId != nullptr && preference.tier == tier) {
        const ChordType& type = chordType(preference.typeId);
        if (!usedIn(column, type) && fullyInKey(Chord{root, &type}, key)) {
            return &type;
        }
    }
    return firstUnusedInKey(candidates, root, key, column);
}

/// Chooses the chord type of every tier for the degree `degree` of the browsed scale.
Column buildColumn(const Key& browsedKey, const std::array<PitchClass, 7>& scale, std::size_t degree) {
    const PitchClass root = scale[degree];
    Column column{};
    column[kTriadTier] = diatonicTriad(browsedKey, static_cast<int>(degree)).type;
    if (inScale(root + 7, browsedKey.tonicPc(), browsedKey.mode)) {
        column[kPowerTier] = &chordType("5");
    }
    // Only the tonic column leads with the mode's characteristic chord.
    const TonicPreference preference =
        degree == 0 ? tonicPreferenceOf(browsedKey.mode) : TonicPreference{-1, nullptr};
    // The "unused" check is vacuous for tier 2 (none of its candidates is a triad or power chord) and
    // for tier 4 (none of its candidates appears in an earlier list, so its gaps come from key membership
    // alone); it is what gives tier 3 its gaps, including the one a Mixolydian tonic's 7 leaves there.
    column[kSixthSeventhTier] =
        preferredOrFirstUnusedInKey(preference, kSixthSeventhTier, kSixthSeventhCandidates, root, browsedKey, column);
    column[kSeventhTier] = firstUnusedInKey(kSeventhCandidates, root, browsedKey, column);
    column[kColourTier] =
        preferredOrFirstUnusedInKey(preference, kColourTier, kColourCandidates, root, browsedKey, column);
    return column;
}

}  // namespace

Palette buildPalette(const Key& browsedKey, const Key& projectKey) {
    const auto scale = scalePitchClasses(browsedKey.tonicPc(), browsedKey.mode);
    std::array<Column, 7> columns{};
    for (std::size_t d = 0; d < columns.size(); ++d) {
        columns[d] = buildColumn(browsedKey, scale, d);
    }

    Palette palette;
    palette.tiers = 0;
    for (int tier = 0; tier < kPaletteTiers; ++tier) {
        for (std::size_t d = 0; d < columns.size(); ++d) {
            const ChordType* type = columns[d][static_cast<std::size_t>(tier)];
            if (type == nullptr) {
                continue;
            }
            const Chord chord{scale[d], type};
            palette.cells.push_back(PaletteCell{static_cast<int>(d), tier, chord, fullyInKey(chord, projectKey),
                                                chordSymbol(chord, browsedKey, SymbolStyle::Klimper),
                                                romanNumeral(chord, projectKey, SymbolStyle::Standard)});
            palette.tiers = tier + 1;
        }
    }
    return palette;
}

}  // namespace chordlab
