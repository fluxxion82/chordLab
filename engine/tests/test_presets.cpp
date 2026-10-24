#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/document.h"
#include "chordlab/pitch.h"
#include "chordlab/presets.h"
#include "chordlab/scale.h"
#include "chordlab/symbol.h"

#include <climits>
#include <cstddef>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace chordlab;

namespace {

Key key(char letter, int alter, Mode mode) {
    return Key{Tonic{letter, alter}, mode};
}

Chord chord(PitchClass root, std::string_view typeId) {
    return Chord{root, &chordType(typeId)};
}

const Key kCIonian = key('C', 0, Mode::Ionian);
const Key kAAeolian = key('A', 0, Mode::Aeolian);

const Preset& preset(std::string_view id) {
    const Preset* p = findPreset(id);
    REQUIRE_MESSAGE(p != nullptr, "no preset " << std::string(id));
    return *p;
}

}  // namespace

// ---- The table ----

TEST_CASE("presets: the 29 records of the plan, in table order, with unique ids") {
    const std::vector<std::string> expected{
        "axis",          "sensitive",     "doo-wop",     "three-chord", "pachelbel",  "mixolydian-rock",
        "classic-rock",  "power-ballad",  "blues-12",    "blues-quick", "ii-v-i",     "i-vi-ii-v",
        "iii-vi-ii-v",   "minor-ii-v-i",  "folk-i-iv-i-v", "country",   "edm-minor",  "edm-major",
        "edm-minor-2",   "neo-soul",      "rnb-ii-v",    "epic-minor",  "andalusian", "film-minor-iv",
        "gospel-iii",    "gospel-v7",     "cadential",   "circle",      "lament"};
    const std::vector<Preset>& all = allPresets();
    REQUIRE(all.size() == expected.size());
    std::set<std::string> ids;
    for (std::size_t i = 0; i < all.size(); ++i) {
        CHECK(std::string(all[i].id) == expected[i]);
        CHECK(ids.insert(all[i].id).second);
    }
    CHECK(std::string(all.front().name) == "Axis");
    CHECK(std::string(all.front().genre) == "Pop");
    CHECK(all.front().tonality == Tonality::Major);
    CHECK(std::string(all.back().name) == "Lament");
    CHECK(std::string(all.back().genre) == "Classical");
    CHECK(all.back().tonality == Tonality::Minor);
    CHECK(preset("blues-12").tonality == Tonality::Any);
    CHECK(preset("blues-12").steps.size() == 12);
    CHECK(preset("pachelbel").steps.size() == 8);
}

TEST_CASE("presets: every step is well formed for the store") {
    for (const Preset& p : allPresets()) {
        CAPTURE(std::string(p.id));
        CHECK_FALSE(p.steps.empty());
        CHECK(p.steps.size() <= static_cast<std::size_t>(kMaxChords));
        CHECK_FALSE(std::string(p.name).empty());
        CHECK(std::string(p.name).size() <= static_cast<std::size_t>(kMaxSectionNameBytes));  // names a section
        CHECK_FALSE(std::string(p.genre).empty());
        for (const PresetStep& s : p.steps) {
            CHECK(s.degree >= 1);
            CHECK(s.degree <= 7);
            CHECK(s.alter >= -1);
            CHECK(s.alter <= 1);
            CHECK_NOTHROW(chordType(s.typeId));
            CHECK(s.beats >= kMinLengthBeats);
            CHECK(s.beats <= kMaxLengthBeats);
        }
    }
}

TEST_CASE("presets: lengths are four beats except the two-beat records") {
    const std::set<std::string> twoBeat{"i-vi-ii-v", "iii-vi-ii-v", "edm-major"};
    for (const Preset& p : allPresets()) {
        CAPTURE(std::string(p.id));
        for (const PresetStep& s : p.steps) {
            CHECK(s.beats == (twoBeat.count(p.id) == 1 ? 2.0 : 4.0));
        }
    }
}

TEST_CASE("findPreset: known ids, unknown ids") {
    CHECK(findPreset("axis") == &allPresets().front());
    CHECK(findPreset("lament") == &allPresets().back());
    CHECK(findPreset("") == nullptr);
    CHECK(findPreset("Axis") == nullptr);
    CHECK(findPreset("axis ") == nullptr);
    CHECK(findPreset("nope") == nullptr);
}

TEST_CASE("tonalityName") {
    CHECK(std::string(tonalityName(Tonality::Major)) == "Major");
    CHECK(std::string(tonalityName(Tonality::Minor)) == "Minor");
    CHECK(std::string(tonalityName(Tonality::Any)) == "Any");
}

// ---- Resolution ----

TEST_CASE("resolvePreset: every preset resolves in all twelve tonics of Ionian and Aeolian") {
    for (const Mode mode : {Mode::Ionian, Mode::Aeolian}) {
        for (PitchClass pc = 0; pc < 12; ++pc) {
            const Key k{canonicalTonic(pc, mode), mode};
            for (const Preset& p : allPresets()) {
                CAPTURE(std::string(p.id));
                CAPTURE(pc);
                std::vector<std::pair<Chord, double>> resolved;
                REQUIRE_NOTHROW(resolved = resolvePreset(p, k));
                REQUIRE(resolved.size() == p.steps.size());
                for (std::size_t i = 0; i < resolved.size(); ++i) {
                    const PresetStep& s = p.steps[i];
                    const Chord& c = resolved[i].first;
                    CHECK(c.type == &chordType(s.typeId));
                    CHECK(c.root == pitchClassOf(pc + modeIntervals(Mode::Ionian)[static_cast<std::size_t>(s.degree - 1)] +
                                                 s.alter));
                    CHECK(resolved[i].second == s.beats);
                }
            }
        }
    }
}

TEST_CASE("resolvePreset: bVII is Bb in C Ionian and G in A Aeolian (read against the parallel major)") {
    const Preset& rock = preset("mixolydian-rock");
    REQUIRE(rock.steps[1].degree == 7);
    REQUIRE(rock.steps[1].alter == -1);
    CHECK(resolvePreset(rock, kCIonian)[1].first == chord(10, "maj"));
    CHECK(resolvePreset(rock, kAAeolian)[1].first == chord(7, "maj"));
    // The mode never matters, only the tonic.
    CHECK(resolvePreset(rock, key('C', 0, Mode::Aeolian))[1].first == chord(10, "maj"));
    CHECK(resolvePreset(preset("edm-minor"), kAAeolian) ==
          std::vector<std::pair<Chord, double>>{
              {chord(9, "min"), 4.0}, {chord(5, "maj"), 4.0}, {chord(0, "maj"), 4.0}, {chord(7, "maj"), 4.0}});
}

TEST_CASE("resolvePreset: ii[m9] in C is D-F-A-C-E") {
    const std::vector<std::pair<Chord, double>> r = resolvePreset(preset("rnb-ii-v"), kCIonian);
    REQUIRE(r.size() == 4);
    CHECK(r[0].first == chord(2, "m9"));
    CHECK(pitchClasses(r[0].first) == std::vector<PitchClass>{2, 5, 9, 0, 4});
    CHECK(r[2].first == chord(0, "maj9"));
}

TEST_CASE("resolvePreset: the root is reduced modulo 12 (vii in B wraps past the octave)") {
    // B Ionian: vii is A#, past the octave (11 + 11 = 22 -> 10).
    const std::vector<std::pair<Chord, double>> r = resolvePreset(preset("circle"), key('B', 0, Mode::Ionian));
    CHECK(r[2].first == chord(10, "dim"));
    // C Ionian: the circle of fifths.
    std::vector<PitchClass> roots;
    for (const auto& [c, beats] : resolvePreset(preset("circle"), kCIonian)) {
        roots.push_back(c.root);
    }
    CHECK(roots == std::vector<PitchClass>{0, 5, 11, 4, 9, 2, 7, 0});
}

// ---- Fit ----

TEST_CASE("presetFits: Major fits Ionian, Lydian, Mixolydian; Minor the other six; Any all nine") {
    const Preset& major = preset("axis");
    const Preset& minor = preset("edm-minor");
    const Preset& any = preset("blues-12");
    const std::set<Mode> majorModes{Mode::Ionian, Mode::Lydian, Mode::Mixolydian};
    for (int m = 0; m < kNumModes; ++m) {
        const Mode mode = static_cast<Mode>(m);
        CAPTURE(m);
        const Key k{canonicalTonic(4, mode), mode};
        CHECK(presetFits(major, k) == (majorModes.count(mode) == 1));
        CHECK(presetFits(minor, k) == (majorModes.count(mode) == 0));
        CHECK(presetFits(any, k));
    }
}

// ---- Numerals ----

TEST_CASE("presetNumerals: the engine's spelling, C major for Major and Any, A minor for Minor") {
    CHECK(presetNumerals(preset("edm-minor")) == "i \xE2\x80\x93 bVI \xE2\x80\x93 bIII \xE2\x80\x93 bVII");
    CHECK(presetNumerals(preset("axis")) == "I \xE2\x80\x93 V \xE2\x80\x93 vi \xE2\x80\x93 IV");
    // Consecutive identical steps are not collapsed.
    const std::string blues = presetNumerals(preset("blues-12"));
    CHECK(blues == "I7 – I7 – I7 – I7 – IV7 – IV7 – I7 – I7 – V7 – IV7 – I7 – V7");
    // Every preset, pinned: the text the UI shows and the docs quote.
    const std::vector<std::pair<std::string, std::string>> pinned{
        {"axis", "I – V – vi – IV"},
        {"sensitive", "vi – IV – I – V"},
        {"doo-wop", "I – vi – IV – V"},
        {"three-chord", "I – IV – V – IV"},
        {"pachelbel", "I – V – vi – iii – IV – I – IV – V"},
        {"mixolydian-rock", "I – bVII – IV – IV"},
        {"classic-rock", "I – IV – bVII – IV"},
        {"power-ballad", "I – V – ii – IV"},
        {"blues-12", "I7 – I7 – I7 – I7 – IV7 – IV7 – I7 – I7 – V7 – IV7 – I7 – V7"},
        {"blues-quick", "I7 – IV7 – I7 – I7 – IV7 – IV7 – I7 – I7 – V7 – IV7 – I7 – V7"},
        {"ii-v-i", "ii7 – V7 – IΔ7 – IΔ7"},
        {"i-vi-ii-v", "IΔ7 – vi7 – ii7 – V7"},
        {"iii-vi-ii-v", "iii7 – vi7 – ii7 – V7"},
        {"minor-ii-v-i", "iiø7 – V7 – i7 – i7"},
        {"folk-i-iv-i-v", "I – IV – I – V"},
        {"country", "I – I – IV – V"},
        {"edm-minor", "i – bVI – bIII – bVII"},
        {"edm-major", "vi – IV – I – V"},
        {"edm-minor-2", "i – bVII – bVI – bVII"},
        {"neo-soul", "IΔ7 – iii7 – vi7 – IVΔ7"},
        {"rnb-ii-v", "ii9 – V7 – IΔ9 – vi7"},
        {"epic-minor", "i – bVI – bIII – bVII"},
        {"andalusian", "i – bVII – bVI – V7"},
        {"film-minor-iv", "i – iv – bVII – bIII"},
        {"gospel-iii", "I – iii – IV – V"},
        {"gospel-v7", "I – IV – I – V7"},
        {"cadential", "I – IV – V – I"},
        {"circle", "I – IV – vii° – iii – vi – ii – V – I"},
        {"lament", "i – bVII – bVI – V"},
    };
    REQUIRE(pinned.size() == allPresets().size());
    for (const auto& entry : pinned) {
        CAPTURE(entry.first);
        CHECK(presetNumerals(preset(entry.first)) == entry.second);
    }
    // And each is romanNumeral of the step resolved in the reference key.
    for (const Preset& p : allPresets()) {
        CAPTURE(std::string(p.id));
        const Key reference = p.tonality == Tonality::Minor ? kAAeolian : kCIonian;
        std::string expected;
        for (const auto& [c, beats] : resolvePreset(p, reference)) {
            expected += (expected.empty() ? "" : " \xE2\x80\x93 ") + romanNumeral(c, reference);
        }
        CHECK(presetNumerals(p) == expected);
    }
}
