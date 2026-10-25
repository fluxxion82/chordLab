#include "doctest.h"

#include "chordlab/applied.h"
#include "chordlab/chord.h"
#include "chordlab/harmony.h"
#include "chordlab/palette.h"
#include "chordlab/pitch.h"
#include "chordlab/relatedness.h"
#include "chordlab/scale.h"
#include "chordlab/suggest.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

using namespace chordlab;

namespace {

Key key(char letter, int alter, Mode mode) {
    return Key{Tonic{letter, alter}, mode};
}

Chord chord(PitchClass root, std::string_view typeId) {
    return Chord{root, &chordType(typeId)};
}

constexpr PitchClass C = 0, D = 2, E = 4, F = 5, G = 7, Ab = 8, A = 9, B = 11;

const Key kCIonian = key('C', 0, Mode::Ionian);
const Key kAAeolian = key('A', 0, Mode::Aeolian);

const Chord kC = chord(C, "maj");
const Chord kCmaj7 = chord(C, "maj7");
const Chord kDm = chord(D, "min");
const Chord kEm = chord(E, "min");
const Chord kF = chord(F, "maj");
const Chord kG = chord(G, "maj");
const Chord kG7 = chord(G, "7");
const Chord kAm = chord(A, "min");
const Chord kBdim = chord(B, "dim");

constexpr RelationMode kAllModes[] = {RelationMode::Off, RelationMode::CommonTones, RelationMode::VoiceLeading,
                                      RelationMode::Function, RelationMode::Blend};

/// The score of the single candidate `candidate` against `reference` in C major, under `mode`.
int scoreInC(const Chord& reference, const Chord& candidate, RelationMode mode) {
    const std::vector<Relatedness> r = relatedness(reference, {candidate}, kCIonian, kCIonian, mode);
    REQUIRE(r.size() == 1);
    return r[0].score;
}

/// The chords of the palette cells.
std::vector<Chord> chordsOf(const Palette& palette) {
    std::vector<Chord> out;
    for (const PaletteCell& c : palette.cells) {
        out.push_back(c.chord);
    }
    return out;
}

}  // namespace

// --- Names ---------------------------------------------------------------------------------------

TEST_CASE("relationModeName / parseRelationMode: the five wire names, round trip, exact match only") {
    CHECK(std::string(relationModeName(RelationMode::Off)) == "off");
    CHECK(std::string(relationModeName(RelationMode::CommonTones)) == "commonTones");
    CHECK(std::string(relationModeName(RelationMode::VoiceLeading)) == "voiceLeading");
    CHECK(std::string(relationModeName(RelationMode::Function)) == "function");
    CHECK(std::string(relationModeName(RelationMode::Blend)) == "blend");
    for (const RelationMode mode : kAllModes) {
        CHECK(parseRelationMode(relationModeName(mode)) == mode);
    }
    CHECK(static_cast<int>(std::size(kAllModes)) == kNumRelationModes);
    for (const char* bad : {"", "Blend", "BLEND", "common tones", "commontones", " blend", "blend ", "none"}) {
        CAPTURE(bad);
        CHECK_FALSE(parseRelationMode(bad).has_value());
    }
}

// --- Per-mode formulas (R4), C reference in C major -------------------------------------------------

TEST_CASE("relatedness: Off scores nothing") {
    CHECK(relatedness(kC, {kC, kF, kAm}, kCIonian, kCIonian, RelationMode::Off).empty());
    CHECK(relatedness(kC, {}, kCIonian, kCIonian, RelationMode::Blend).empty());
}

TEST_CASE("relatedness: commonTones is the shared share of the larger chord, rounded") {
    CHECK(scoreInC(kC, kAm, RelationMode::CommonTones) == 67);  // 2 of 3
    CHECK(scoreInC(kC, kCmaj7, RelationMode::CommonTones) == 75);  // 3 of 4
    CHECK(scoreInC(kC, kF, RelationMode::CommonTones) == 33);
    CHECK(scoreInC(kC, kG, RelationMode::CommonTones) == 33);
    CHECK(scoreInC(kC, kDm, RelationMode::CommonTones) == 0);
    CHECK(scoreInC(kC, kC, RelationMode::CommonTones) == 100);
    CHECK(scoreInC(kC, kEm, RelationMode::CommonTones) == 67);
    CHECK(scoreInC(kC, chord(C, "5"), RelationMode::CommonTones) == 67);  // C G against C E G
    CHECK(scoreInC(kG7, chord(D, "m7"), RelationMode::CommonTones) == 50);  // D F of four each
}

TEST_CASE("relatedness: voiceLeading loses ten per unit of pitch-class cost, floored at 0") {
    CHECK(scoreInC(kC, kAm, RelationMode::VoiceLeading) == 80);  // G -> A: cost 2
    CHECK(scoreInC(kC, kEm, RelationMode::VoiceLeading) == 90);  // C -> B: cost 1
    CHECK(scoreInC(kC, kF, RelationMode::VoiceLeading) == 70);
    CHECK(scoreInC(kC, kG, RelationMode::VoiceLeading) == 70);
    CHECK(scoreInC(kC, kDm, RelationMode::VoiceLeading) == 50);
    CHECK(scoreInC(kC, kC, RelationMode::VoiceLeading) == 100);
    // The formula over every type on C against every type on F# (far apart: the costs reach 10 and beyond,
    // where the score bottoms out at 0).
    bool floored = false;
    for (const ChordType& a : allChordTypes()) {
        for (const ChordType& b : allChordTypes()) {
            const Chord x{C, &a};
            const Chord y{6, &b};
            const int cost = pcVoiceLeadingCost(pitchClasses(x), pitchClasses(y));
            CAPTURE(a.id);
            CAPTURE(b.id);
            CHECK(scoreInC(x, y, RelationMode::VoiceLeading) == std::max(0, 100 - 10 * cost));
            floored = floored || cost >= 10;
        }
    }
    CHECK(floored);
}

TEST_CASE("relatedness: function is the Pop transition table as a percentage") {
    CHECK(scoreInC(kC, kG, RelationMode::Function) == 60);   // T -> D
    CHECK(scoreInC(kC, kF, RelationMode::Function) == 80);   // T -> PD
    CHECK(scoreInC(kG7, kC, RelationMode::Function) == 100); // D -> T
    CHECK(scoreInC(kDm, kG7, RelationMode::Function) == 90); // PD -> D
    CHECK(scoreInC(kC, kAm, RelationMode::Function) == 30);  // T -> T
    CHECK(scoreInC(kC, kC, RelationMode::Function) == 30);   // the one mode where the same chord is not 100
    // Other involved: AΔ7 is Other in C major, either side.
    const Chord amaj7 = chord(A, "maj7");
    CHECK(scoreInC(amaj7, kC, RelationMode::Function) == 20);
    CHECK(scoreInC(kG7, amaj7, RelationMode::Function) == 20);
    CHECK(scoreInC(amaj7, amaj7, RelationMode::Function) == 20);
}

TEST_CASE("relatedness: the text is relationText of relate, whatever the mode") {
    const std::vector<Chord> candidates{kAm, kF, chord(Ab, "maj"), kC};
    for (const RelationMode mode : kAllModes) {
        if (mode == RelationMode::Off) {
            continue;
        }
        CAPTURE(relationModeName(mode));
        const std::vector<Relatedness> r = relatedness(kC, candidates, kCIonian, kCIonian, mode);
        REQUIRE(r.size() == candidates.size());
        for (std::size_t i = 0; i < r.size(); ++i) {
            CHECK(r[i].text == relationText(relate(kC, candidates[i], kCIonian), kCIonian));
            CHECK(r[i].score >= 0);
            CHECK(r[i].score <= 100);
        }
        CHECK(r[0].text == "2 common tones (C, E) · down a third · cost 2");
        CHECK(r[2].text == "1 common tone (C) · down a third · cost 2 · borrowed from C Aeolian");
        CHECK(r[3].text == "3 common tones (C, E, G) · same root · cost 0");
    }
}

TEST_CASE("relatedness: a minor key reads functions and borrowings against it") {
    const Chord am = kAm;
    const Chord e = chord(E, "maj");    // harmonic minor's V
    const Chord dm = chord(D, "min");   // iv
    const Chord c = chord(C, "maj");    // III
    const std::vector<Chord> candidates{e, dm, c};
    const auto fn = relatedness(am, candidates, kAAeolian, kAAeolian, RelationMode::Function);
    REQUIRE(fn.size() == 3);
    CHECK(fn[0].score == 60);  // T -> D
    CHECK(fn[1].score == 80);  // T -> PD
    CHECK(fn[2].score == 30);  // T -> T
    const auto common = relatedness(am, candidates, kAAeolian, kAAeolian, RelationMode::CommonTones);
    CHECK(common[0].score == 33);
    CHECK(common[1].score == 33);
    CHECK(common[2].score == 67);
    CHECK(fn[0].text == "1 common tone (E) · up a fifth · cost 2 · borrowed from A harmonic minor");
    CHECK(fn[2].text == "2 common tones (C, E) · up a third · cost 2");
    // Blend after Am in A minor: the iv and the V outrank the III's tonic-to-tonic step... as suggestNext says.
    const auto blend = relatedness(am, candidates, kAAeolian, kAAeolian, RelationMode::Blend);
    const std::vector<int> h = suggestionHundredths(&am, candidates, kAAeolian, kAAeolian);
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        for (std::size_t j = 0; j < candidates.size(); ++j) {
            if (h[i] > h[j]) {
                CHECK(blend[i].score >= blend[j].score);
            }
        }
    }
}

// --- Blend ---------------------------------------------------------------------------------------

TEST_CASE("relatedness: blend after C in C major puts F, G and Am first among the triads") {
    const Palette palette = buildPalette(kCIonian, kCIonian);
    const std::vector<Chord> chords = chordsOf(palette);
    const auto r = relatedness(kC, chords, kCIonian, kCIonian, RelationMode::Blend);
    REQUIRE(r.size() == chords.size());
    struct Triad {
        Chord chord;
        int score;
    };
    std::vector<Triad> triads;
    for (std::size_t i = 0; i < chords.size(); ++i) {
        if (palette.cells[i].tier == kPaletteTriadTier) {
            if (chords[i] == kC) {
                CHECK(r[i].score == 100);  // the reference itself
            } else {
                triads.push_back({chords[i], r[i].score});
            }
        }
    }
    REQUIRE(triads.size() == 6);
    std::stable_sort(triads.begin(), triads.end(), [](const Triad& a, const Triad& b) { return a.score > b.score; });
    std::vector<Chord> top3{triads[0].chord, triads[1].chord, triads[2].chord};
    CHECK(std::count(top3.begin(), top3.end(), kF) == 1);
    CHECK(std::count(top3.begin(), top3.end(), kG) == 1);
    CHECK(std::count(top3.begin(), top3.end(), kAm) == 1);
    CHECK(triads[2].score > triads[3].score);  // and strictly ahead of Dm, Em and B°
    // F is the best non-reference chord of the whole palette (2.10 after C), so it is 100.
    const auto f = std::find(chords.begin(), chords.end(), kF) - chords.begin();
    CHECK(r[static_cast<std::size_t>(f)].score == 100);
}

TEST_CASE("relatedness: blend spans 0..100 over the candidates other than the reference") {
    const std::vector<Chord> chords = chordsOf(buildPalette(kCIonian, kCIonian));
    const auto r = relatedness(kG7, chords, kCIonian, kCIonian, RelationMode::Blend);
    int lo = 100;
    int hi = 0;
    for (std::size_t i = 0; i < chords.size(); ++i) {
        if (chords[i] != kG7) {
            lo = std::min(lo, r[i].score);
            hi = std::max(hi, r[i].score);
        }
    }
    CHECK(lo == 0);
    CHECK(hi == 100);
    // One candidate, or candidates that tie, are all 100; the reference alone is 100.
    CHECK(relatedness(kC, {kBdim}, kCIonian, kCIonian, RelationMode::Blend)[0].score == 100);
    const auto tie = relatedness(kC, {kF, kF}, kCIonian, kCIonian, RelationMode::Blend);
    CHECK(tie[0].score == 100);
    CHECK(tie[1].score == 100);
    CHECK(relatedness(kC, {kC}, kCIonian, kCIonian, RelationMode::Blend)[0].score == 100);
    // The reference does not take part in the normalisation: adding it changes no other score.
    const auto without = relatedness(kC, {kF, kBdim, kAm}, kCIonian, kCIonian, RelationMode::Blend);
    const auto with = relatedness(kC, {kF, kC, kBdim, kAm}, kCIonian, kCIonian, RelationMode::Blend);
    CHECK(with[0].score == without[0].score);
    CHECK(with[1].score == 100);
    CHECK(with[2].score == without[1].score);
    CHECK(with[3].score == without[2].score);
    CHECK(without[1].score == 0);
    CHECK(without[0].score == 100);
}

namespace {

/// For the browsed palette, the blend scores never contradict `suggestNext`'s ranking of the same chords:
/// walking suggestNext's full list restricted to palette chords, the blend score never rises, and a cell
/// with a strictly higher suggestion score never gets a lower blend score.
void checkBlendFollowsSuggestNext(const Chord& reference, const Key& project, const Key& browsed) {
    const std::vector<Chord> cells = chordsOf(buildPalette(browsed, project));
    const auto blend = relatedness(reference, cells, project, browsed, RelationMode::Blend);
    REQUIRE(blend.size() == cells.size());
    const auto ranked = suggestNext(&reference, project, browsed, Style::Pop, 100000);
    std::vector<int> order;  // blend scores in suggestNext's order
    for (const Suggestion& s : ranked) {
        const auto it = std::find(cells.begin(), cells.end(), s.chord);
        if (it != cells.end()) {
            order.push_back(blend[static_cast<std::size_t>(it - cells.begin())].score);
        }
    }
    const std::size_t nonReference =
        static_cast<std::size_t>(std::count_if(cells.begin(), cells.end(), [&](const Chord& c) { return c != reference; }));
    CHECK(order.size() == nonReference);  // suggestNext ranks every palette cell but the reference
    CHECK(std::is_sorted(order.rbegin(), order.rend()));
    if (!order.empty()) {
        CHECK(order.front() == 100);  // the best cell
    }
}

}  // namespace

TEST_CASE("relatedness: blend orders the browsed palette exactly as suggestNext ranks it") {
    const Key gIonian = key('G', 0, Mode::Ionian);
    const Key cAeolian = key('C', 0, Mode::Aeolian);
    for (const Chord& reference : {kC, kDm, kEm, kF, kG, kG7, kAm, kBdim, chord(Ab, "maj")}) {
        CAPTURE(reference.root);
        CAPTURE(reference.type->id);
        checkBlendFollowsSuggestNext(reference, kCIonian, kCIonian);
        checkBlendFollowsSuggestNext(reference, kCIonian, gIonian);
        checkBlendFollowsSuggestNext(reference, kCIonian, cAeolian);
    }
    for (int m = 0; m < kNumModes; ++m) {
        const Key k{canonicalTonic(D, Mode(m)), Mode(m)};
        for (int d = 0; d < 7; ++d) {
            CAPTURE(m);
            CAPTURE(d);
            checkBlendFollowsSuggestNext(diatonicTriad(k, d), k, k);
        }
    }
}

// --- Extra candidates ------------------------------------------------------------------------------

namespace {

/// The palette of `browsed` followed by the project key's seven triads: the population the snapshot sizes
/// while another key is browsed.
std::vector<Chord> populationOf(const Key& project, const Key& browsed) {
    std::vector<Chord> out = chordsOf(buildPalette(browsed, project));
    for (int d = 0; d < 7; ++d) {
        out.push_back(diatonicTriad(project, d));
    }
    return out;
}

/// The applied chords of `key`, as chords.
std::vector<Chord> appliedOf(const Key& key) {
    std::vector<Chord> out;
    for (const AppliedChord& a : appliedChords(key)) {
        out.push_back(a.chord);
    }
    return out;
}

/// The blend score the extra-candidate rule gives a raw score against the population range lo..hi.
int expectedExtraBlend(int raw, int lo, int hi, bool emptyRange) {
    if (emptyRange || raw >= hi) {
        return 100;
    }
    if (raw <= lo) {
        return 0;
    }
    return (2 * 100 * (raw - lo) + (hi - lo)) / (2 * (hi - lo));
}

}  // namespace

TEST_CASE("relatedness with extras: the population scores exactly as it does without them, in every mode") {
    const Key gIonian = key('G', 0, Mode::Ionian);
    const Key eIonian = key('E', 0, Mode::Ionian);
    struct Case {
        Key project;
        Key browsed;
    };
    for (const Case& c : {Case{kCIonian, kCIonian}, Case{kCIonian, gIonian}, Case{kCIonian, eIonian},
                          Case{kAAeolian, kAAeolian}, Case{kAAeolian, kCIonian}}) {
        const std::vector<Chord> population = populationOf(c.project, c.browsed);
        const std::vector<Chord> extras = appliedOf(c.browsed);
        for (const Chord& reference : {kC, kF, kG7, kAm, kBdim, chord(A, "7"), chord(Ab, "maj")}) {
            for (const RelationMode mode : kAllModes) {
                CAPTURE(static_cast<int>(mode));
                CAPTURE(reference.root);
                CAPTURE(reference.type->id);
                const auto alone = relatedness(reference, population, c.project, c.browsed, mode);
                const auto with = relatedness(reference, population, extras, c.project, c.browsed, mode);
                if (mode == RelationMode::Off) {
                    CHECK(with.empty());
                    continue;
                }
                REQUIRE(alone.size() == population.size());
                REQUIRE(with.size() == population.size() + extras.size());
                for (std::size_t i = 0; i < population.size(); ++i) {
                    CHECK(with[i].score == alone[i].score);
                    CHECK(with[i].text == alone[i].text);
                }
                // An empty list of extras is the call without them.
                const auto none = relatedness(reference, population, {}, c.project, c.browsed, mode);
                REQUIRE(none.size() == alone.size());
                for (std::size_t i = 0; i < alone.size(); ++i) {
                    CHECK(none[i].score == alone[i].score);
                    CHECK(none[i].text == alone[i].text);
                }
            }
        }
    }
}

TEST_CASE("relatedness with extras: the per-candidate modes score an extra as if it stood alone") {
    const Key gIonian = key('G', 0, Mode::Ionian);
    const std::vector<Chord> population = populationOf(kCIonian, gIonian);
    const std::vector<Chord> extras = appliedOf(gIonian);
    for (const RelationMode mode : {RelationMode::CommonTones, RelationMode::VoiceLeading, RelationMode::Function}) {
        for (const Chord& reference : {kC, kG7, kAm}) {
            CAPTURE(static_cast<int>(mode));
            const auto with = relatedness(reference, population, extras, kCIonian, gIonian, mode);
            REQUIRE(with.size() == population.size() + extras.size());
            for (std::size_t i = 0; i < extras.size(); ++i) {
                const auto alone = relatedness(reference, {extras[i]}, kCIonian, gIonian, mode);
                CHECK(with[population.size() + i].score == alone[0].score);
                CHECK(with[population.size() + i].text == alone[0].text);
            }
        }
    }
}

TEST_CASE("relatedness with extras: blend normalises extras with the population's range, clamped to 0..100") {
    const Key gIonian = key('G', 0, Mode::Ionian);
    struct Case {
        Key project;
        Key browsed;
    };
    for (const Case& c : {Case{kCIonian, kCIonian}, Case{kCIonian, gIonian}, Case{kAAeolian, kAAeolian}}) {
        const std::vector<Chord> population = populationOf(c.project, c.browsed);
        const std::vector<Chord> extras = appliedOf(c.browsed);
        for (const Chord& reference : {kC, kF, kG7, kAm, chord(A, "7")}) {
            CAPTURE(reference.root);
            CAPTURE(reference.type->id);
            const auto with = relatedness(reference, population, extras, c.project, c.browsed, RelationMode::Blend);
            REQUIRE(with.size() == population.size() + extras.size());
            const std::vector<int> rawPopulation =
                suggestionHundredths(&reference, population, c.project, c.browsed);
            const std::vector<int> rawExtras = suggestionHundredths(&reference, extras, c.project, c.browsed);
            int lo = 1 << 30;
            int hi = -(1 << 30);
            for (std::size_t i = 0; i < population.size(); ++i) {
                if (population[i] != reference) {
                    lo = std::min(lo, rawPopulation[i]);
                    hi = std::max(hi, rawPopulation[i]);
                }
            }
            for (std::size_t i = 0; i < extras.size(); ++i) {
                CAPTURE(i);
                const Relatedness& r = with[population.size() + i];
                CHECK(r.score >= 0);
                CHECK(r.score <= 100);
                const int expected =
                    extras[i] == reference ? 100 : expectedExtraBlend(rawExtras[i], lo, hi, hi <= lo);
                CHECK(r.score == expected);
                CHECK(r.text == relationText(relate(reference, extras[i], c.project), c.project));
            }
        }
    }
}

TEST_CASE("relatedness with extras: an extra outside the population's range is clamped, the reference is 100") {
    // Dm and Em are a narrow population after C: F and G score above both, Bdim below both.
    const std::vector<Chord> population{kDm, kEm};
    const std::vector<Chord> extras{kF, kG, kBdim, kC};
    const std::vector<int> rawPopulation = suggestionHundredths(&kC, population, kCIonian, kCIonian);
    const std::vector<int> rawExtras = suggestionHundredths(&kC, extras, kCIonian, kCIonian);
    const int hi = std::max(rawPopulation[0], rawPopulation[1]);
    const int lo = std::min(rawPopulation[0], rawPopulation[1]);
    REQUIRE(lo < hi);
    REQUIRE(rawExtras[0] > hi);
    REQUIRE(rawExtras[1] > hi);
    REQUIRE(rawExtras[2] < lo);
    const auto r = relatedness(kC, population, extras, kCIonian, kCIonian, RelationMode::Blend);
    REQUIRE(r.size() == 6);
    CHECK(r[0].score + r[1].score == 100);  // the population alone: 0 and 100
    CHECK(r[2].score == 100);  // F
    CHECK(r[3].score == 100);  // G
    CHECK(r[4].score == 0);    // Bdim
    CHECK(r[5].score == 100);  // the reference
    // A population with no range (a tie, a single chord, only the reference, nothing at all): an extra
    // at or above its score is 100, one below 0, and every extra 100 when there is no range at all.
    const auto single = relatedness(kC, {kDm}, {kF, kBdim, kC}, kCIonian, kCIonian, RelationMode::Blend);
    REQUIRE(single.size() == 4);
    CHECK(single[0].score == 100);
    CHECK(single[1].score == 100);
    CHECK(single[2].score == 0);
    CHECK(single[3].score == 100);
    const auto tie = relatedness(kC, {kDm, kDm}, {kDm}, kCIonian, kCIonian, RelationMode::Blend);
    REQUIRE(tie.size() == 3);
    CHECK(tie[2].score == 100);
    for (const std::vector<Chord>& empty : {std::vector<Chord>{}, std::vector<Chord>{kC}}) {
        const auto r2 = relatedness(kC, empty, {kF, kBdim}, kCIonian, kCIonian, RelationMode::Blend);
        REQUIRE(r2.size() == empty.size() + 2);
        CHECK(r2[empty.size()].score == 100);
        CHECK(r2[empty.size() + 1].score == 100);
    }
}
