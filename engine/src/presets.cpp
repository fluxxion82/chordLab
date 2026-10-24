#include "chordlab/presets.h"

#include "chordlab/scale.h"
#include "chordlab/symbol.h"

#include <array>
#include <cassert>
#include <cstddef>

namespace chordlab {

namespace {

/// Wire names in enum order.
constexpr std::array<const char*, 3> kTonalityNames{{"Major", "Minor", "Any"}};

constexpr double kBar = 4.0;   // the default step length (P8)
constexpr double kHalf = 2.0;  // the records marked "2-beat"

/// Separator of `presetNumerals`: an en dash between spaces.
constexpr const char* kNumeralSeparator = " \xE2\x80\x93 ";

}  // namespace

const char* tonalityName(Tonality tonality) {
    const auto i = static_cast<std::size_t>(tonality);
    assert(i < kTonalityNames.size() && "Tonality outside the enum");
    return i < kTonalityNames.size() ? kTonalityNames[i] : "";
}

const std::vector<Preset>& allPresets() {
    // Function-local static: initialised once, lives for the program, so pointers into it are stable.
    // Each step is {degree 1..7, alter, type id, beats} against the parallel major (P1); the minor
    // presets' VI, III and VII are therefore bVI, bIII and bVII.
    static const std::vector<Preset> kPresets{
        // Pop
        {"axis", "Axis", "Pop", Tonality::Major,
         {{1, 0, "maj", kBar}, {5, 0, "maj", kBar}, {6, 0, "min", kBar}, {4, 0, "maj", kBar}}},
        {"sensitive", "Sensitive", "Pop", Tonality::Major,
         {{6, 0, "min", kBar}, {4, 0, "maj", kBar}, {1, 0, "maj", kBar}, {5, 0, "maj", kBar}}},
        {"doo-wop", "Doo-wop", "Pop", Tonality::Major,
         {{1, 0, "maj", kBar}, {6, 0, "min", kBar}, {4, 0, "maj", kBar}, {5, 0, "maj", kBar}}},
        {"three-chord", "Three-chord", "Pop", Tonality::Major,
         {{1, 0, "maj", kBar}, {4, 0, "maj", kBar}, {5, 0, "maj", kBar}, {4, 0, "maj", kBar}}},
        {"pachelbel", "Pachelbel", "Pop", Tonality::Major,
         {{1, 0, "maj", kBar}, {5, 0, "maj", kBar}, {6, 0, "min", kBar}, {3, 0, "min", kBar},
          {4, 0, "maj", kBar}, {1, 0, "maj", kBar}, {4, 0, "maj", kBar}, {5, 0, "maj", kBar}}},
        // Rock
        {"mixolydian-rock", "Mixolydian rock", "Rock", Tonality::Major,
         {{1, 0, "maj", kBar}, {7, -1, "maj", kBar}, {4, 0, "maj", kBar}, {4, 0, "maj", kBar}}},
        {"classic-rock", "Classic rock", "Rock", Tonality::Major,
         {{1, 0, "maj", kBar}, {4, 0, "maj", kBar}, {7, -1, "maj", kBar}, {4, 0, "maj", kBar}}},
        {"power-ballad", "Power ballad", "Rock", Tonality::Major,
         {{1, 0, "maj", kBar}, {5, 0, "maj", kBar}, {2, 0, "min", kBar}, {4, 0, "maj", kBar}}},
        // Blues
        {"blues-12", "12-bar blues", "Blues", Tonality::Any,
         {{1, 0, "7", kBar}, {1, 0, "7", kBar}, {1, 0, "7", kBar}, {1, 0, "7", kBar},
          {4, 0, "7", kBar}, {4, 0, "7", kBar}, {1, 0, "7", kBar}, {1, 0, "7", kBar},
          {5, 0, "7", kBar}, {4, 0, "7", kBar}, {1, 0, "7", kBar}, {5, 0, "7", kBar}}},
        {"blues-quick", "Quick-change blues", "Blues", Tonality::Any,
         {{1, 0, "7", kBar}, {4, 0, "7", kBar}, {1, 0, "7", kBar}, {1, 0, "7", kBar},
          {4, 0, "7", kBar}, {4, 0, "7", kBar}, {1, 0, "7", kBar}, {1, 0, "7", kBar},
          {5, 0, "7", kBar}, {4, 0, "7", kBar}, {1, 0, "7", kBar}, {5, 0, "7", kBar}}},
        // Jazz
        {"ii-v-i", "ii\xE2\x80\x93V\xE2\x80\x93I", "Jazz", Tonality::Major,
         {{2, 0, "m7", kBar}, {5, 0, "7", kBar}, {1, 0, "maj7", kBar}, {1, 0, "maj7", kBar}}},
        {"i-vi-ii-v", "I\xE2\x80\x93vi\xE2\x80\x93ii\xE2\x80\x93V turnaround", "Jazz", Tonality::Major,
         {{1, 0, "maj7", kHalf}, {6, 0, "m7", kHalf}, {2, 0, "m7", kHalf}, {5, 0, "7", kHalf}}},
        {"iii-vi-ii-v", "iii\xE2\x80\x93vi\xE2\x80\x93ii\xE2\x80\x93V turnaround", "Jazz", Tonality::Major,
         {{3, 0, "m7", kHalf}, {6, 0, "m7", kHalf}, {2, 0, "m7", kHalf}, {5, 0, "7", kHalf}}},
        {"minor-ii-v-i", "Minor ii\xE2\x80\x93V\xE2\x80\x93i", "Jazz", Tonality::Minor,
         {{2, 0, "m7b5", kBar}, {5, 0, "7", kBar}, {1, 0, "m7", kBar}, {1, 0, "m7", kBar}}},
        // Folk / Country
        {"folk-i-iv-i-v", "Folk", "Folk / Country", Tonality::Major,
         {{1, 0, "maj", kBar}, {4, 0, "maj", kBar}, {1, 0, "maj", kBar}, {5, 0, "maj", kBar}}},
        {"country", "Country", "Folk / Country", Tonality::Major,
         {{1, 0, "maj", kBar}, {1, 0, "maj", kBar}, {4, 0, "maj", kBar}, {5, 0, "maj", kBar}}},
        // EDM / Dance
        {"edm-minor", "Dance minor", "EDM / Dance", Tonality::Minor,
         {{1, 0, "min", kBar}, {6, -1, "maj", kBar}, {3, -1, "maj", kBar}, {7, -1, "maj", kBar}}},
        {"edm-major", "Dance major", "EDM / Dance", Tonality::Major,
         {{6, 0, "min", kHalf}, {4, 0, "maj", kHalf}, {1, 0, "maj", kHalf}, {5, 0, "maj", kHalf}}},
        {"edm-minor-2", "Dance minor II", "EDM / Dance", Tonality::Minor,
         {{1, 0, "min", kBar}, {7, -1, "maj", kBar}, {6, -1, "maj", kBar}, {7, -1, "maj", kBar}}},
        // R&B / Neo-soul
        {"neo-soul", "Neo-soul", "R&B / Neo-soul", Tonality::Major,
         {{1, 0, "maj7", kBar}, {3, 0, "m7", kBar}, {6, 0, "m7", kBar}, {4, 0, "maj7", kBar}}},
        {"rnb-ii-v", "R&B ii\xE2\x80\x93V", "R&B / Neo-soul", Tonality::Major,
         {{2, 0, "m9", kBar}, {5, 0, "7", kBar}, {1, 0, "maj9", kBar}, {6, 0, "m7", kBar}}},
        // Film / Epic
        {"epic-minor", "Epic minor", "Film / Epic", Tonality::Minor,
         {{1, 0, "min", kBar}, {6, -1, "maj", kBar}, {3, -1, "maj", kBar}, {7, -1, "maj", kBar}}},
        {"andalusian", "Andalusian", "Film / Epic", Tonality::Minor,
         {{1, 0, "min", kBar}, {7, -1, "maj", kBar}, {6, -1, "maj", kBar}, {5, 0, "7", kBar}}},
        {"film-minor-iv", "Minor iv", "Film / Epic", Tonality::Minor,
         {{1, 0, "min", kBar}, {4, 0, "min", kBar}, {7, -1, "maj", kBar}, {3, -1, "maj", kBar}}},
        // Gospel
        {"gospel-iii", "Gospel", "Gospel", Tonality::Major,
         {{1, 0, "maj", kBar}, {3, 0, "min", kBar}, {4, 0, "maj", kBar}, {5, 0, "maj", kBar}}},
        {"gospel-v7", "Gospel V7", "Gospel", Tonality::Major,
         {{1, 0, "maj", kBar}, {4, 0, "maj", kBar}, {1, 0, "maj", kBar}, {5, 0, "7", kBar}}},
        // Classical
        {"cadential", "Cadential", "Classical", Tonality::Major,
         {{1, 0, "maj", kBar}, {4, 0, "maj", kBar}, {5, 0, "maj", kBar}, {1, 0, "maj", kBar}}},
        {"circle", "Circle of fifths", "Classical", Tonality::Major,
         {{1, 0, "maj", kBar}, {4, 0, "maj", kBar}, {7, 0, "dim", kBar}, {3, 0, "min", kBar},
          {6, 0, "min", kBar}, {2, 0, "min", kBar}, {5, 0, "maj", kBar}, {1, 0, "maj", kBar}}},
        {"lament", "Lament", "Classical", Tonality::Minor,
         {{1, 0, "min", kBar}, {7, -1, "maj", kBar}, {6, -1, "maj", kBar}, {5, 0, "maj", kBar}}},
    };
    return kPresets;
}

const Preset* findPreset(std::string_view id) {
    for (const Preset& p : allPresets()) {
        if (id == p.id) {
            return &p;
        }
    }
    return nullptr;
}

bool presetFits(const Preset& preset, const Key& key) {
    assert(static_cast<int>(key.mode) >= 0 && static_cast<int>(key.mode) < kNumModes);
    if (preset.tonality == Tonality::Any) {
        return true;
    }
    const bool majorKey = key.mode == Mode::Ionian || key.mode == Mode::Lydian || key.mode == Mode::Mixolydian;
    return majorKey == (preset.tonality == Tonality::Major);
}

std::vector<std::pair<Chord, double>> resolvePreset(const Preset& preset, const Key& key) {
    const PitchClass tonic = key.tonicPc();
    const std::array<int, 7>& major = modeIntervals(Mode::Ionian);
    std::vector<std::pair<Chord, double>> out;
    out.reserve(preset.steps.size());
    for (const PresetStep& s : preset.steps) {
        assert(s.degree >= 1 && s.degree <= 7);
        assert(s.alter >= -1 && s.alter <= 1);
        const PitchClass root = pitchClassOf(tonic + major[static_cast<std::size_t>(s.degree - 1)] + s.alter);
        out.emplace_back(Chord{root, &chordType(s.typeId)}, s.beats);
    }
    return out;
}

std::string presetNumerals(const Preset& preset) {
    const Key reference = preset.tonality == Tonality::Minor ? Key{{'A', 0}, Mode::Aeolian}
                                                             : Key{{'C', 0}, Mode::Ionian};
    std::string out;
    for (const auto& [chord, beats] : resolvePreset(preset, reference)) {
        if (!out.empty()) {
            out += kNumeralSeparator;
        }
        out += romanNumeral(chord, reference);
    }
    return out;
}

}  // namespace chordlab
