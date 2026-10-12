#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/palette.h"
#include "chordlab/pitch.h"
#include "chordlab/scale.h"
#include "chordlab/symbol.h"

#include <cstddef>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace chordlab;

namespace {

Key key(char letter, int alter, Mode mode) {
    return Key{Tonic{letter, alter}, mode};
}

/// Symbols of every cell on `tier`, in degree order.
std::vector<std::string> symbolsOnTier(const Palette& p, int tier) {
    std::vector<std::string> out;
    for (const PaletteCell& c : p.cells) {
        if (c.tier == tier) {
            out.push_back(c.symbol);
        }
    }
    return out;
}

/// The cell at (`degree`, `tier`). Throws if the palette has no such cell.
const PaletteCell& cell(const Palette& p, int degree, int tier) {
    for (const PaletteCell& c : p.cells) {
        if (c.degree == degree && c.tier == tier) {
            return c;
        }
    }
    throw std::out_of_range("no palette cell at degree " + std::to_string(degree) + ", tier " +
                            std::to_string(tier));
}

/// Symbols of every cell in the column of `degree`, in tier order.
std::vector<std::string> columnSymbols(const Palette& p, int degree) {
    std::vector<std::string> out;
    for (const PaletteCell& c : p.cells) {
        if (c.degree == degree) {
            out.push_back(c.symbol);
        }
    }
    return out;
}

/// Pitch classes of scale degrees d, d+2, d+4 of `k`: the diatonic triad on degree `d`.
std::vector<PitchClass> stackedThirds(const Key& k, int d) {
    const auto scale = scalePitchClasses(k.tonicPc(), k.mode);
    return {scale[static_cast<std::size_t>(d % 7)], scale[static_cast<std::size_t>((d + 2) % 7)],
            scale[static_cast<std::size_t>((d + 4) % 7)]};
}

const Key kCIonian = key('C', 0, Mode::Ionian);
const Key kCAeolian = key('C', 0, Mode::Aeolian);

}  // namespace

// --- Klimper screenshots ---------------------------------------------------------------

TEST_CASE("C major palette matches Klimper 2") {
    const Palette p = buildPalette(kCIonian, kCIonian);
    CHECK(symbolsOnTier(p, 0) == std::vector<std::string>{"C5", "D5", "E5", "F5", "G5", "A5"});  // no B5
    CHECK(symbolsOnTier(p, 1) == std::vector<std::string>{"C", "Dm", "Em", "F", "G", "Am", "Bmb5"});
    CHECK(symbolsOnTier(p, 2) == std::vector<std::string>{"C6", "Dm6", "Em7", "F6", "G6", "Am7", "Bm7b5"});
    CHECK(symbolsOnTier(p, 3) == std::vector<std::string>{"CΔ7", "Dm7", "FΔ7", "G7", "Amadd9"});  // gaps at iii and vii
    for (const PaletteCell& c : p.cells) {
        CAPTURE(c.symbol);
        CHECK(c.filled);
    }
}

TEST_CASE("C minor (Aeolian) palette matches Klimper 2") {
    const Palette p = buildPalette(kCAeolian, kCAeolian);
    CHECK(symbolsOnTier(p, 0) == std::vector<std::string>{"C5", "Eb5", "F5", "G5", "Ab5", "Bb5"});  // no D5
    CHECK(symbolsOnTier(p, 1) == std::vector<std::string>{"Cm", "Dmb5", "Eb", "Fm", "Gm", "Ab", "Bb"});
    CHECK(symbolsOnTier(p, 2) == std::vector<std::string>{"Cm7", "Dm7b5", "Eb6", "Fm6", "Gm7", "Ab6", "Bb6"});
    CHECK(symbolsOnTier(p, 3) == std::vector<std::string>{"Cmadd9", "EbΔ7", "Fm7", "AbΔ7", "Bb7"});  // gaps at ii° and v
}

// Derived by hand from the rule, not from a screenshot; if it fails, check the rule first.
TEST_CASE("C harmonic minor uses mMaj7, dim7 and the augmented III") {
    const Key cHarmonicMinor = key('C', 0, Mode::HarmonicMinor);
    const Palette p = buildPalette(cHarmonicMinor, cHarmonicMinor);
    CHECK(symbolsOnTier(p, 1) == std::vector<std::string>{"Cm", "Dmb5", "Eb+", "Fm", "G", "Ab", "Bmb5"});
    CHECK(symbolsOnTier(p, 2) == std::vector<std::string>{"CmΔ7", "Dm7b5", "Eb+Δ7", "Fm6", "G7", "Ab6", "Bdim7"});
}

// Derived by hand from the rule, not from a screenshot (Klimper's colour row has not been captured yet).
// The Phase 2 tier-4 order (ninths, mΔ9, the #11 chords, 6/9, then the suspended chords) leaves both rows
// as they were: no degree of either key holds a mΔ9 or a #11 chord that a ninth does not beat.
TEST_CASE("C major and C minor colour rows follow the tier-4 rule") {
    const Palette major = buildPalette(kCIonian, kCIonian);
    CHECK(symbolsOnTier(major, 4) == std::vector<std::string>{"CΔ9", "Dm9", "E7sus4", "FΔ9", "G9", "Am9"});  // gap at vii
    CHECK(major.tiers == 5);

    const Palette minor = buildPalette(kCAeolian, kCAeolian);
    CHECK(symbolsOnTier(minor, 4) == std::vector<std::string>{"Cm9", "EbΔ9", "Fm9", "G7sus4", "AbΔ9", "Bb9"});  // gap at ii°
    CHECK(minor.tiers == 5);
}

// The tonic column leads with the mode's characteristic chord in the tier that chord belongs to.
TEST_CASE("modal tonic columns show the mode's characteristic chord") {
    const Palette ionian = buildPalette(kCIonian, kCIonian);
    CHECK(columnSymbols(ionian, 0) == std::vector<std::string>{"C5", "C", "C6", "CΔ7", "CΔ9"});  // unchanged

    const Key cLydian = key('C', 0, Mode::Lydian);
    const Palette lydian = buildPalette(cLydian, cLydian);
    CHECK(columnSymbols(lydian, 0) == std::vector<std::string>{"C5", "C", "C6", "CΔ7", "CΔ7#11"});
    CHECK(cell(lydian, 0, 4).roman == "IΔ7#11");

    const Key cMixolydian = key('C', 0, Mode::Mixolydian);
    const Palette mixolydian = buildPalette(cMixolydian, cMixolydian);
    CHECK(columnSymbols(mixolydian, 0) == std::vector<std::string>{"C5", "C", "C7", "Cadd9", "C9"});
    CHECK(symbolsOnTier(mixolydian, 2) ==
          std::vector<std::string>{"C7", "Dm7", "Em7b5", "F6", "Gm6", "Am7", "Bb6"});  // only the tonic changes
    CHECK(cell(mixolydian, 0, 2).roman == "I7");

    const Key cMelodicMinor = key('C', 0, Mode::MelodicMinor);
    const Palette melodic = buildPalette(cMelodicMinor, cMelodicMinor);
    CHECK(columnSymbols(melodic, 0) == std::vector<std::string>{"C5", "Cm", "Cm6", "CmΔ7", "CmΔ9"});

    const Key cHarmonicMinor = key('C', 0, Mode::HarmonicMinor);
    const Palette harmonic = buildPalette(cHarmonicMinor, cHarmonicMinor);
    CHECK(columnSymbols(harmonic, 0) == std::vector<std::string>{"C5", "Cm", "CmΔ7", "Cmadd9", "CmΔ9"});

    // The Dorian tonic already led with its characteristic m6 and keeps doing so.
    const Key cDorian = key('C', 0, Mode::Dorian);
    const Palette dorian = buildPalette(cDorian, cDorian);
    CHECK(columnSymbols(dorian, 0) == std::vector<std::string>{"C5", "Cm", "Cm6", "Cm7", "Cm9"});
}

TEST_CASE("the characteristic chord is only preferred on the tonic column") {
    // F Lydian browsed from C major: its tonic F gets FΔ7#11, but the C column of C Lydian's
    // palette (degree 4) does not get a Mixolydian 7 or anything but the general order.
    const Key fLydian = key('F', 0, Mode::Lydian);
    const Palette p = buildPalette(fLydian, kCIonian);
    CHECK(cell(p, 0, 4).symbol == "FΔ7#11");
    CHECK(cell(p, 0, 4).filled);  // F A C E B: all in C major
    CHECK(cell(p, 4, 4).symbol == "CΔ9");
}

// --- Browsing a foreign key ---------------------------------------------------------------

TEST_CASE("browsing G major from a C major project marks F# chords hollow") {
    const Palette p = buildPalette(key('G', 0, Mode::Ionian), kCIonian);
    CHECK(cell(p, 0, 1).filled);        // G B D: all in C
    CHECK_FALSE(cell(p, 0, 3).filled);  // GΔ7 has F#
    CHECK_FALSE(cell(p, 6, 1).filled);  // F#mb5
    CHECK(cell(p, 0, 1).roman == "V");  // numerals are relative to the project key
    CHECK(cell(p, 6, 1).roman == "#iv°");
    CHECK(cell(p, 6, 1).symbol == "F#mb5");  // symbols are spelled in the browsed key
}

TEST_CASE("browsing G minor from a C minor project reproduces Klimper's numeral row") {
    const Palette p = buildPalette(key('G', 0, Mode::Aeolian), kCAeolian);
    std::vector<std::string> r;
    for (int d = 0; d < 7; ++d) {
        r.push_back(cell(p, d, 1).roman);
    }
    CHECK(r == std::vector<std::string>{"v", "vi°", "bVII", "i", "ii", "bIII", "IV"});
    CHECK_FALSE(cell(p, 1, 1).filled);  // A° has A natural
    CHECK(cell(p, 2, 1).filled);        // Bb
}

// --- Structure -----------------------------------------------------------------------------

TEST_CASE("cells are ordered by tier, then degree") {
    const Palette p = buildPalette(kCIonian, kCIonian);
    REQUIRE_FALSE(p.cells.empty());
    for (std::size_t i = 1; i < p.cells.size(); ++i) {
        const PaletteCell& prev = p.cells[i - 1];
        const PaletteCell& next = p.cells[i];
        CAPTURE(i);
        CHECK((next.tier > prev.tier || (next.tier == prev.tier && next.degree > prev.degree)));
    }
    CHECK(p.cells.front().tier == 0);
    CHECK(p.cells.front().degree == 0);
    CHECK(p.cells.back().tier == p.tiers - 1);
}

TEST_CASE("cell helper throws for an absent cell") {
    const Palette p = buildPalette(kCIonian, kCIonian);
    CHECK_THROWS_AS(cell(p, 6, 0), std::out_of_range);  // no B5
    CHECK_THROWS_AS(cell(p, 2, 3), std::out_of_range);  // gap at iii
}

// --- Properties ------------------------------------------------------------------------------

TEST_CASE("every tonic x mode produces a sane palette") {
    for (PitchClass pc = 0; pc < 12; ++pc) {
        for (int m = 0; m < kNumModes; ++m) {
            const Key k{canonicalTonic(pc, Mode(m)), Mode(m)};
            CAPTURE(pc);
            CAPTURE(m);
            const Palette p = buildPalette(k, k);
            CHECK(p.tiers >= 2);
            for (int d = 0; d < 7; ++d) {
                CAPTURE(d);
                const auto col = columnSymbols(p, d);
                CHECK(std::set<std::string>(col.begin(), col.end()).size() == col.size());  // unique per column
                CHECK(cell(p, d, 1).chord.type != nullptr);                                  // a triad always exists
                CHECK(pitchClasses(cell(p, d, 1).chord) == stackedThirds(k, d));
            }
            for (const PaletteCell& c : p.cells) {
                CAPTURE(c.symbol);
                CHECK(fullyInKey(c.chord, k));
                CHECK(c.filled);
                CHECK(c.chord.root >= 0);
                CHECK(c.chord.root < 12);
            }
        }
    }
}

TEST_CASE("every triad is one of maj, min, dim or aug") {
    for (PitchClass pc = 0; pc < 12; ++pc) {
        for (int m = 0; m < kNumModes; ++m) {
            const Key k{canonicalTonic(pc, Mode(m)), Mode(m)};
            const Palette p = buildPalette(k, k);
            for (int d = 0; d < 7; ++d) {
                CAPTURE(pc);
                CAPTURE(m);
                CAPTURE(d);
                const std::string_view id = cell(p, d, 1).chord.type->id;
                CHECK((id == "maj" || id == "min" || id == "dim" || id == "aug"));
            }
        }
    }
}

TEST_CASE("symbol and roman of every cell agree with the chord, browsed key and project key") {
    const Key browsed = key('G', 0, Mode::Aeolian);
    const Palette p = buildPalette(browsed, kCAeolian);
    const auto scale = scalePitchClasses(browsed.tonicPc(), browsed.mode);
    for (const PaletteCell& c : p.cells) {
        CAPTURE(c.symbol);
        CHECK(c.chord.root == scale[static_cast<std::size_t>(c.degree)]);
        CHECK(c.symbol == chordSymbol(c.chord, browsed));
        CHECK(c.roman == romanNumeral(c.chord, kCAeolian));
        CHECK(c.filled == fullyInKey(c.chord, kCAeolian));
        CHECK(fullyInKey(c.chord, browsed));
    }
}
