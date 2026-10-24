#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/document.h"
#include "chordlab/pitch.h"
#include "chordlab/presets.h"
#include "chordlab/scale.h"
#include "chordlab/symbol.h"

#include <climits>
#include <cmath>
#include <cstddef>
#include <limits>
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

// ---- DocumentStore::appendProgression / replaceProgression ----

namespace {

using Steps = std::vector<std::pair<Chord, double>>;

const Steps kFourSteps{{chord(0, "maj"), 4.0}, {chord(7, "maj"), 4.0}, {chord(9, "min"), 4.0}, {chord(5, "maj"), 4.0}};

/// Checks that `after` is exactly `before`: document, both counters, the selection, and the whole undo
/// and redo history (each walked on copies).
void checkSameStore(const DocumentStore& before, const DocumentStore& after) {
    CHECK(after.doc() == before.doc());
    CHECK(after.nextId() == before.nextId());
    CHECK(after.nextSectionId() == before.nextSectionId());
    CHECK(after.selectedEventId() == before.selectedEventId());
    for (const bool undoing : {true, false}) {
        DocumentStore a = before;
        DocumentStore b = after;
        for (;;) {
            const bool stepA = undoing ? a.undo() : a.redo();
            const bool stepB = undoing ? b.undo() : b.redo();
            REQUIRE(stepA == stepB);
            if (!stepA) {
                break;
            }
            CHECK(a.doc() == b.doc());
        }
    }
}

/// A store with C, Am (ids 1, 2), no sections, Am selected.
DocumentStore twoChordStore() {
    DocumentStore store;
    REQUIRE(store.appendChord(chord(0, "maj")) == 1);
    REQUIRE(store.appendChord(chord(9, "min"), 2.0, 90) == 2);
    store.setSelectedEvent(2);
    return store;
}

}  // namespace

TEST_CASE("appendProgression: onto an empty document, one section at 0 and one undo step") {
    DocumentStore store;
    store.setProjectKey(key('D', 0, Mode::Dorian));
    store.setBrowsedKey(key('G', 0, Mode::Ionian));
    store.setAutoVoiceLead(true);
    const Document before = store.doc();
    CHECK(store.progressionRefusal(kFourSteps, ProgressionLoad::Append) == ProgressionRefusal::None);
    REQUIRE(store.appendProgression(kFourSteps, "Axis"));
    const Document& d = store.doc();
    REQUIRE(d.events.size() == 4);
    for (std::size_t i = 0; i < 4; ++i) {
        CAPTURE(i);
        CHECK(d.events[i] == ChordEvent{static_cast<int>(i) + 1, kFourSteps[i].first, 4.0, 100, 0, 0, false});
    }
    REQUIRE(d.sections.size() == 1);
    CHECK(d.sections[0] == Section{1, "Axis", 0, 0});
    CHECK(sectionsWellFormed(d));
    CHECK(store.nextId() == 5);
    CHECK(store.nextSectionId() == 2);
    CHECK(d.endBeat() == 16.0);
    // Keys, the voicing flag and live state are untouched.
    CHECK(d.projectKey == before.projectKey);
    CHECK(d.browsedKey == before.browsedKey);
    CHECK(d.autoVoiceLead);

    // One step: undo restores the exact document; the counters keep their advance (ids are never reused).
    REQUIRE(store.undo());
    CHECK(store.doc() == before);
    CHECK(store.nextId() == 5);
    CHECK(store.nextSectionId() == 2);
    REQUIRE(store.redo());
    CHECK(store.doc().events.size() == 4);
    CHECK(store.doc().sections.size() == 1);
    REQUIRE(store.undo());
    REQUIRE(store.undo());  // the autoVoiceLead toggle: the load was exactly one step
    CHECK_FALSE(store.doc().autoVoiceLead);
}

TEST_CASE("appendProgression: onto an unsectioned progression, a leading default section and the preset's") {
    DocumentStore store = twoChordStore();
    const Document before = store.doc();
    REQUIRE(store.appendProgression(kFourSteps, "Dance minor"));
    const Document& d = store.doc();
    REQUIRE(d.events.size() == 6);
    CHECK(d.events[0] == before.events[0]);
    CHECK(d.events[1] == before.events[1]);
    CHECK(d.events[2].id == 3);
    CHECK(d.events[5].id == 6);
    REQUIRE(d.sections.size() == 2);
    CHECK(d.sections[0] == Section{1, "Section 1", 0, 0});
    CHECK(d.sections[1] == Section{2, "Dance minor", 1, 2});
    CHECK(sectionsWellFormed(d));
    CHECK(store.nextSectionId() == 3);
    CHECK(store.selectedEventId() == 2);  // the selected chord is still there
    REQUIRE(store.undo());
    CHECK(store.doc() == before);
    CHECK(store.selectedEventId() == 2);
    REQUIRE(store.undo());  // the load was one step: the next undo takes back the second append
    CHECK(store.doc().events.size() == 1);
}

TEST_CASE("appendProgression: onto a sectioned progression, one new section at the join") {
    DocumentStore store = twoChordStore();
    REQUIRE(store.addSection(0, "Verse") == 1);
    REQUIRE(store.addSection(1, "Chorus") == 2);
    const Document before = store.doc();
    REQUIRE(store.appendProgression(kFourSteps, "Axis"));
    const Document& d = store.doc();
    REQUIRE(d.sections.size() == 3);
    CHECK(d.sections[0] == before.sections[0]);
    CHECK(d.sections[1] == before.sections[1]);
    CHECK(d.sections[2] == Section{3, "Axis", 2, 2});
    CHECK(sectionsWellFormed(d));
    CHECK(store.nextSectionId() == 4);
    REQUIRE(store.undo());
    CHECK(store.doc() == before);
}

TEST_CASE("replaceProgression: one section at 0, the old chords and sections gone, the selection cleared") {
    DocumentStore store = twoChordStore();
    REQUIRE(store.addSection(1, "Chorus") == 2);  // after the leading "Section 1"
    store.setProjectKey(key('A', 0, Mode::Aeolian));
    store.setProgressionLoop(false);
    store.setRelationMode(RelationMode::Function);
    const Document before = store.doc();
    REQUIRE(store.selectedEventId() == 2);
    CHECK(store.progressionRefusal(kFourSteps, ProgressionLoad::Replace) == ProgressionRefusal::None);
    REQUIRE(store.replaceProgression(kFourSteps, "Epic minor"));
    const Document& d = store.doc();
    REQUIRE(d.events.size() == 4);
    CHECK(d.events[0].id == 3);
    CHECK(d.events[3].id == 6);
    CHECK(d.events[3] == ChordEvent{6, kFourSteps[3].first, 4.0, 100, 0, 0, false});
    REQUIRE(d.sections.size() == 1);
    CHECK(d.sections[0] == Section{3, "Epic minor", 2, 0});
    CHECK(sectionsWellFormed(d));
    CHECK(store.selectedEventId() == kNoEventId);
    CHECK(d.projectKey == before.projectKey);
    CHECK(d.progressionLoop == false);
    CHECK(d.relationMode == RelationMode::Function);
    CHECK(store.nextId() == 7);
    CHECK(store.nextSectionId() == 4);
    REQUIRE(store.undo());
    CHECK(store.doc() == before);
    CHECK(store.selectedEventId() == kNoEventId);  // the selection is never restored by undo
}

TEST_CASE("replaceProgression: onto an empty document") {
    DocumentStore store;
    REQUIRE(store.replaceProgression(kFourSteps, "Axis"));
    CHECK(store.doc().events.size() == 4);
    REQUIRE(store.doc().sections.size() == 1);
    CHECK(store.doc().sections[0] == Section{1, "Axis", 0, 0});
    REQUIRE(store.undo());
    CHECK(store.doc() == Document{});
}

TEST_CASE("appendProgression / replaceProgression: lengths clamped, an empty name takes the default") {
    DocumentStore store;
    const Steps steps{{chord(0, "maj"), 100.0}, {chord(2, "min"), 0.1}};
    REQUIRE(store.appendProgression(steps, ""));
    CHECK(store.doc().events[0].lengthBeats == kMaxLengthBeats);
    CHECK(store.doc().events[1].lengthBeats == kMinLengthBeats);
    CHECK(store.doc().sections[0].name == "Section 1");
    REQUIRE(store.replaceProgression(steps, std::string(80, 'x')));
    CHECK(store.doc().sections[0].name == std::string(kMaxSectionNameBytes, 'x'));
}

TEST_CASE("appendProgression / replaceProgression: refusals change nothing") {
    SUBCASE("empty steps") {
        DocumentStore store = twoChordStore();
        const DocumentStore before = store;
        CHECK(store.progressionRefusal({}, ProgressionLoad::Append) == ProgressionRefusal::Empty);
        CHECK(store.progressionRefusal({}, ProgressionLoad::Replace) == ProgressionRefusal::Empty);
        CHECK_FALSE(store.appendProgression({}, "X"));
        CHECK_FALSE(store.replaceProgression({}, "X"));
        checkSameStore(before, store);
    }
    SUBCASE("a non-finite length") {
        DocumentStore store = twoChordStore();
        const DocumentStore before = store;
        const Steps inf{{chord(0, "maj"), 4.0}, {chord(5, "maj"), std::numeric_limits<double>::infinity()}};
        const Steps nan{{chord(0, "maj"), std::nan("")}};
        CHECK(store.progressionRefusal(inf, ProgressionLoad::Append) == ProgressionRefusal::NonFiniteLength);
        CHECK(store.progressionRefusal(nan, ProgressionLoad::Replace) == ProgressionRefusal::NonFiniteLength);
        CHECK_FALSE(store.appendProgression(inf, "X"));
        CHECK_FALSE(store.replaceProgression(nan, "X"));
        checkSameStore(before, store);
    }
    SUBCASE("capacity: an append past kMaxChords, a replace with more than kMaxChords") {
        DocumentStore store;
        for (int i = 0; i < kMaxChords - 2; ++i) {
            REQUIRE(store.appendChord(chord(i % 12, "maj")) != kNoEventId);
        }
        REQUIRE(store.undo());  // 253 chords, and one entry on the redo stack
        store.setSelectedEvent(5);
        const DocumentStore before = store;
        CHECK(store.progressionRefusal(kFourSteps, ProgressionLoad::Append) == ProgressionRefusal::DocumentFull);
        CHECK_FALSE(store.appendProgression(kFourSteps, "Axis"));
        checkSameStore(before, store);
        const Steps full(static_cast<std::size_t>(kMaxChords) + 1, {chord(0, "maj"), 1.0});
        CHECK(store.progressionRefusal(full, ProgressionLoad::Replace) == ProgressionRefusal::DocumentFull);
        CHECK_FALSE(store.replaceProgression(full, "X"));
        checkSameStore(before, store);
        // Exactly full is fine, either way.
        const Steps three(3, {chord(0, "maj"), 1.0});
        CHECK(store.appendProgression(three, "Fill"));
        CHECK(store.doc().events.size() == static_cast<std::size_t>(kMaxChords));
        CHECK(store.replaceProgression(Steps(static_cast<std::size_t>(kMaxChords), {chord(0, "maj"), 1.0}), "All"));
        CHECK(store.doc().events.size() == static_cast<std::size_t>(kMaxChords));
    }
    SUBCASE("event ids: the counter must hand out the whole batch") {
        DocumentStore store(INT_MAX - 3);  // three ids left: INT_MAX-3 .. INT_MAX-1
        const DocumentStore before = store;
        CHECK(store.progressionRefusal(kFourSteps, ProgressionLoad::Append) == ProgressionRefusal::EventIdsExhausted);
        CHECK(store.progressionRefusal(kFourSteps, ProgressionLoad::Replace) == ProgressionRefusal::EventIdsExhausted);
        CHECK_FALSE(store.appendProgression(kFourSteps, "Axis"));
        CHECK_FALSE(store.replaceProgression(kFourSteps, "Axis"));
        checkSameStore(before, store);
        const Steps three(kFourSteps.begin(), kFourSteps.begin() + 3);
        REQUIRE(store.appendProgression(three, "Three"));
        CHECK(store.doc().events.back().id == INT_MAX - 1);
        CHECK(store.nextId() == INT_MAX);
        CHECK(store.appendChord(chord(0, "maj")) == kNoEventId);  // the counter stops, as before
    }
    SUBCASE("section ids: two for an append onto an unsectioned progression, one otherwise") {
        Document doc;
        doc.events.push_back(ChordEvent{1, chord(0, "maj"), 4.0, 100});
        doc.events.push_back(ChordEvent{2, chord(7, "maj"), 4.0, 100});
        DocumentStore store;
        store.replace(doc, 3, INT_MAX - 1);  // one section id left
        store.setSelectedEvent(1);
        const DocumentStore before = store;
        CHECK(store.progressionRefusal(kFourSteps, ProgressionLoad::Append) ==
              ProgressionRefusal::SectionIdsExhausted);
        CHECK_FALSE(store.appendProgression(kFourSteps, "Axis"));
        checkSameStore(before, store);
        CHECK(store.progressionRefusal(kFourSteps, ProgressionLoad::Replace) == ProgressionRefusal::None);
        REQUIRE(store.replaceProgression(kFourSteps, "Axis"));
        CHECK(store.doc().sections.front().id == INT_MAX - 1);
        CHECK(store.nextSectionId() == INT_MAX);
        // None left: both refused, even onto the now-sectioned progression.
        const DocumentStore exhausted = store;
        CHECK(store.progressionRefusal(kFourSteps, ProgressionLoad::Append) ==
              ProgressionRefusal::SectionIdsExhausted);
        CHECK(store.progressionRefusal(kFourSteps, ProgressionLoad::Replace) ==
              ProgressionRefusal::SectionIdsExhausted);
        CHECK_FALSE(store.appendProgression(kFourSteps, "Axis"));
        CHECK_FALSE(store.replaceProgression(kFourSteps, "Axis"));
        checkSameStore(exhausted, store);
    }
    SUBCASE("onto an empty document an append needs one section id") {
        DocumentStore store;
        store.replace(Document{}, 1, INT_MAX - 1);
        REQUIRE(store.appendProgression(kFourSteps, "Axis"));
        CHECK(store.doc().sections.size() == 1);
    }
}

TEST_CASE("loading a resolved preset: every preset appends and replaces as one step") {
    for (const Preset& p : allPresets()) {
        CAPTURE(std::string(p.id));
        DocumentStore store = twoChordStore();
        const Document before = store.doc();
        const Steps steps = resolvePreset(p, key('E', -1, Mode::Ionian));
        REQUIRE(store.appendProgression(steps, p.name));
        CHECK(store.doc().events.size() == 2 + steps.size());
        CHECK(store.doc().sections.back().name == p.name);
        CHECK(store.doc().endBeat() == doctest::Approx(before.endBeat() + [&] {
                  double sum = 0.0;
                  for (const PresetStep& s : p.steps) {
                      sum += s.beats;
                  }
                  return sum;
              }()));
        REQUIRE(store.replaceProgression(steps, p.name));
        CHECK(store.doc().events.size() == steps.size());
        REQUIRE(store.undo());
        REQUIRE(store.undo());
        CHECK(store.doc() == before);
    }
}
