#include "doctest.h"

#include "chordlab/applied.h"
#include "chordlab/chord.h"
#include "chordlab/compiler.h"
#include "chordlab/document.h"
#include "chordlab/function.h"
#include "chordlab/harmony.h"
#include "chordlab/json_api.h"
#include "chordlab/limits.h"
#include "chordlab/palette.h"
#include "chordlab/pitch.h"
#include "chordlab/presets.h"
#include "chordlab/relatedness.h"
#include "chordlab/scale.h"
#include "chordlab/suggest.h"
#include "chordlab/symbol.h"
#include "chordlab/tension.h"
#include "chordlab/voicing.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <climits>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace chordlab;
using nlohmann::json;

namespace {

Key key(char letter, int alter, Mode mode) {
    return Key{Tonic{letter, alter}, mode};
}

Chord chord(PitchClass root, std::string_view typeId) {
    return Chord{root, &chordType(typeId)};
}

const Chord kC = chord(0, "maj");
const Chord kF = chord(5, "maj");
const Chord kG7 = chord(7, "7");

/// The wire form of a key.
json keyJson(const char* letter, int alter, const char* mode) {
    return json{{"tonic", {{"letter", letter}, {"alter", alter}}}, {"mode", mode}};
}

/// Sends `cmd` to the store, adding `"schema": 1` unless the command already carries a schema field.
bool apply(DocumentStore& store, json cmd, std::string& error) {
    if (!cmd.contains("schema")) {
        cmd["schema"] = kJsonSchema;
    }
    return applyCommandJson(store, cmd.dump(), error);
}

/// Sends `cmd` and requires it to be accepted.
void applyOk(DocumentStore& store, json cmd) {
    std::string error;
    const bool ok = apply(store, std::move(cmd), error);
    REQUIRE_MESSAGE(ok, error);
}

/// Checks that `cmd` is rejected: false, an error message, and the document exactly as it was.
void checkRejected(DocumentStore& store, const json& cmd) {
    CAPTURE(cmd.dump());
    const Document before = store.doc();
    std::string error;
    CHECK_FALSE(apply(store, cmd, error));
    CHECK_FALSE(error.empty());
    CHECK(store.doc() == before);
}

/// Checks that `text` (raw, not necessarily JSON) is rejected without touching the document.
void checkRejectedRaw(DocumentStore& store, std::string_view text) {
    CAPTURE(text);
    const Document before = store.doc();
    std::string error;
    CHECK_FALSE(applyCommandJson(store, text, error));
    CHECK_FALSE(error.empty());
    CHECK(store.doc() == before);
}

/// The store's snapshot with the palette of its browsed key, parsed back.
json snapshot(const DocumentStore& store) {
    const Document& d = store.doc();
    return json::parse(snapshotJson(d, buildPalette(d.browsedKey, d.projectKey)));
}

/// Symbols of every palette cell on `tier` in the snapshot, in degree order.
std::vector<std::string> tierSymbols(const json& snap, int tier) {
    std::vector<std::string> out;
    for (const json& c : snap.at("palette").at("cells")) {
        if (c.at("tier") == tier) {
            out.push_back(c.at("symbol").get<std::string>());
        }
    }
    return out;
}

/// The snapshot's palette cell at (`degree`, `tier`); null if there is none.
json cellAt(const json& snap, int degree, int tier) {
    for (const json& c : snap.at("palette").at("cells")) {
        if (c.at("degree") == degree && c.at("tier") == tier) {
            return c;
        }
    }
    return json();
}

/// Ids of the snapshot's events in playback order.
std::vector<int> snapshotIds(const json& snap) {
    std::vector<int> ids;
    for (const json& e : snap.at("events")) {
        ids.push_back(e.at("id").get<int>());
    }
    return ids;
}

/// A store with C (id 1), F (id 2), G7 (id 3), four beats each at velocity 100.
DocumentStore threeChords() {
    DocumentStore store;
    store.appendChord(kC, 4.0, 100);
    store.appendChord(kF, 4.0, 100);
    store.appendChord(kG7, 4.0, 100);
    return store;
}

/// A document exercising every field: non-default keys, toggles and relation mode, a gap in the ids, mixed lengths, a
/// manual voicing, auto voice-leading on, and two sections. Ids are 1 and 3 (2 was removed), so the next
/// id is 4; the sections are 1 ("A", at 0) and 2 ("B", colour 3, at 1), so the next section id is 3.
DocumentStore richStore() {
    DocumentStore store;
    store.setProjectKey(key('D', 0, Mode::Dorian));
    store.setBrowsedKey(key('F', 0, Mode::Lydian));
    store.setProgressionLoop(false);
    store.setFollowHost(false);
    store.appendChord(kC, 4.0, 100);
    store.appendChord(chord(5, "maj7"), 2.5, 80);
    store.appendChord(kG7, 1.0, 127);
    store.removeChord(2);
    store.setInversion(3, 2);
    store.setOctave(3, -1);
    store.setAutoVoiceLead(true);
    store.addSection(0, "A");
    store.addSection(1, "B");
    store.setSectionColour(2, 3);
    store.setRelationMode(RelationMode::VoiceLeading);
    return store;
}

}  // namespace

// --- Snapshot --------------------------------------------------------------------------

TEST_CASE("snapshot: appendChord yields one event spelled and numbered in the project key") {
    DocumentStore store;
    applyOk(store, {{"cmd", "appendChord"}, {"root", 0}, {"type", "maj"}});

    const json snap = snapshot(store);
    CHECK(snap.at("schema") == kJsonSchema);
    CHECK(snap.at("projectKey") == keyJson("C", 0, "Ionian"));
    CHECK(snap.at("browsedKey") == keyJson("C", 0, "Ionian"));
    CHECK(snap.at("projectKeyName") == "C Ionian");
    CHECK(snap.at("browsedKeyName") == "C Ionian");
    CHECK(snap.at("progressionLoop") == true);
    CHECK(snap.at("followHost") == true);
    CHECK(snap.at("endBeat").get<double>() == 4.0);

    REQUIRE(snap.at("events").size() == 1);
    const json& e = snap.at("events")[0];
    CHECK(e.at("id") == 1);
    CHECK(e.at("root") == 0);
    CHECK(e.at("type") == "maj");
    CHECK(e.at("symbol") == "C");
    CHECK(e.at("roman") == "I");
    CHECK(e.at("family") == "Major");
    CHECK(e.at("startBeat").get<double>() == 0.0);
    CHECK(e.at("lengthBeats").get<double>() == 4.0);
    CHECK(e.at("velocity") == 100);
    CHECK(e.at("voicing") == json({60, 64, 67}));
}

TEST_CASE("snapshot: start beats accumulate and symbols follow the project key, not the browsed key") {
    DocumentStore store;
    applyOk(store, {{"cmd", "setKey"}, {"letter", "F"}, {"alter", 0}, {"mode", "Ionian"}});
    applyOk(store, {{"cmd", "appendChord"}, {"root", 0}, {"type", "maj"}});
    applyOk(store, {{"cmd", "appendChord"}, {"root", 3}, {"type", "maj"}, {"lengthBeats", 2.5}, {"velocity", 80}});
    applyOk(store, {{"cmd", "browseKey"}, {"steps", 3}});  // A major: browsing must not respell the progression
    REQUIRE(store.doc().browsedKey == key('A', 0, Mode::Ionian));

    const json snap = snapshot(store);
    REQUIRE(snap.at("events").size() == 2);
    CHECK(snap.at("events")[0].at("symbol") == "C");
    CHECK(snap.at("events")[0].at("roman") == "V");
    const json& e = snap.at("events")[1];
    CHECK(e.at("id") == 2);
    CHECK(e.at("symbol") == "Eb");  // chromatic in F major: bVII by the parallel-major degree table; D# in the browsed key
    CHECK(e.at("roman") == "bVII");
    CHECK(e.at("family") == "Major");
    CHECK(e.at("startBeat").get<double>() == 4.0);
    CHECK(e.at("lengthBeats").get<double>() == 2.5);
    CHECK(e.at("velocity") == 80);
    CHECK(snap.at("endBeat").get<double>() == 6.5);
}

TEST_CASE("snapshot: empty document") {
    const DocumentStore store;
    const json snap = snapshot(store);
    CHECK(snap.at("events").empty());
    CHECK(snap.at("endBeat").get<double>() == 0.0);
}

TEST_CASE("snapshot: palette carries the C major grid with Klimper symbols, numerals, families and voicings") {
    const DocumentStore store;
    const json snap = snapshot(store);
    CHECK(snap.at("palette").at("tiers") == 5);
    CHECK(tierSymbols(snap, 1) == std::vector<std::string>{"C", "Dm", "Em", "F", "G", "Am", "Bmb5"});

    const json c = cellAt(snap, 0, 1);
    REQUIRE_FALSE(c.is_null());
    CHECK(c.at("root") == 0);
    CHECK(c.at("type") == "maj");
    CHECK(c.at("roman") == "I");
    CHECK(c.at("family") == "Major");
    CHECK(c.at("filled") == true);
    CHECK(c.at("voicing") == json({60, 64, 67}));

    CHECK(cellAt(snap, 1, 1).at("family") == "Minor");
    CHECK(cellAt(snap, 6, 1).at("family") == "Other");
    CHECK(cellAt(snap, 6, 1).at("roman") == "vii°");
    CHECK(cellAt(snap, 0, 0).at("type") == "5");
    CHECK(cellAt(snap, 6, 0).is_null());  // no B5 in C major
    for (const json& cell : snap.at("palette").at("cells")) {
        CAPTURE(cell.dump());
        CHECK(cell.at("filled") == true);
    }
}

// --- Snapshot: key tables and out-of-key notes -----------------------------------------

namespace {

/// The `Mode` enumerator names in enum order: what the snapshot's `modes` must list.
const std::vector<std::string> kModeNames{"Ionian",  "Dorian",  "Phrygian",      "Lydian",      "Mixolydian",
                                          "Aeolian", "Locrian", "HarmonicMinor", "MelodicMinor"};

}  // namespace

TEST_CASE("snapshot: modes lists the nine mode identifiers in enum order, under the unchanged schema") {
    const DocumentStore store;
    const json snap = snapshot(store);
    CHECK(snap.at("schema") == 1);  // tonics, modes and outOfKey are additive: no schema bump
    REQUIRE(snap.at("modes").size() == kNumModes);
    CHECK(snap.at("modes").get<std::vector<std::string>>() == kModeNames);
}

TEST_CASE("snapshot: tonics carries the canonical tonic of every pitch class in every mode") {
    const DocumentStore store;
    const json snap = snapshot(store);
    const json& tonics = snap.at("tonics");
    CHECK(tonics.size() == kNumModes);

    CHECK(tonics.at("Ionian")[6] == json({{"letter", "F"}, {"alter", 1}, {"text", "F#"}}));
    CHECK(tonics.at("Ionian")[3] == json({{"letter", "E"}, {"alter", -1}, {"text", "Eb"}}));
    CHECK(tonics.at("Locrian")[3].at("text") == "D#");  // sig +4, not Eb Locrian's -8
    CHECK(tonics.at("Locrian")[5].at("text") == "F");   // F (-6) over E# (+6): the natural wins the tie

    // Every mode has one entry per pitch class, and each is the engine's own canonicalTonic.
    for (int m = 0; m < kNumModes; ++m) {
        const std::string& name = kModeNames[static_cast<std::size_t>(m)];
        CAPTURE(name);
        const json& row = tonics.at(name);
        REQUIRE(row.size() == 12);
        for (PitchClass pc = 0; pc < 12; ++pc) {
            const Tonic t = canonicalTonic(pc, static_cast<Mode>(m));
            const json& entry = row.at(static_cast<std::size_t>(pc));
            CHECK(entry.at("letter") == std::string(1, t.letter));
            CHECK(entry.at("alter") == t.alter);
            CHECK(entry.at("text") == Spelling{t.letter, t.alter}.text());
        }
    }
}

TEST_CASE("snapshot: outOfKey lists each chord's pitch classes outside the project key, sorted") {
    DocumentStore store;
    applyOk(store, {{"cmd", "browseKey"}, {"steps", 1}});                       // G major browsed from C major
    applyOk(store, {{"cmd", "appendChord"}, {"root", 8}, {"type", "maj"}});   // Ab: Ab C Eb
    applyOk(store, {{"cmd", "appendChord"}, {"root", 5}, {"type", "min"}});   // Fm: F Ab C
    applyOk(store, {{"cmd", "appendChord"}, {"root", 7}, {"type", "maj"}});   // G: diatonic

    json snap = snapshot(store);
    const json fSharpDim = cellAt(snap, 6, 1);
    REQUIRE_FALSE(fSharpDim.is_null());
    CHECK(fSharpDim.at("symbol") == "F#mb5");
    CHECK(fSharpDim.at("outOfKey") == json::array({6}));
    CHECK(fSharpDim.at("filled") == false);
    const json g = cellAt(snap, 0, 1);
    CHECK(g.at("filled") == true);
    CHECK(g.at("outOfKey") == json::array());

    // `filled` and `outOfKey` are two views of one fact, and the list is the sorted set of chromatic notes.
    for (const json& cell : snap.at("palette").at("cells")) {
        CAPTURE(cell.dump());
        const auto out = cell.at("outOfKey").get<std::vector<int>>();
        CHECK(cell.at("filled") == out.empty());
        CHECK(std::is_sorted(out.begin(), out.end()));
        CHECK(std::adjacent_find(out.begin(), out.end()) == out.end());
        const Chord ch = chord(cell.at("root").get<int>(), cell.at("type").get<std::string>());
        for (const PitchClass pc : pitchClasses(ch)) {
            const bool listed = std::find(out.begin(), out.end(), pc) != out.end();
            CHECK(listed == !inScale(pc, 0, Mode::Ionian));
        }
    }

    REQUIRE(snap.at("events").size() == 3);
    CHECK(snap.at("events")[0].at("outOfKey") == json::array({3, 8}));  // sorted: the chord spells them 8, 0, 3
    CHECK(snap.at("events")[1].at("outOfKey") == json::array({8}));
    CHECK(snap.at("events")[2].at("outOfKey") == json::array());

    // Relative to the project key, not the browsed key: moving the project key to Ab major flips them.
    applyOk(store, {{"cmd", "setKey"}, {"letter", "A"}, {"alter", -1}, {"mode", "Ionian"}});
    snap = snapshot(store);
    CHECK(snap.at("events")[0].at("outOfKey") == json::array());
    CHECK(snap.at("events")[1].at("outOfKey") == json::array());
    CHECK(snap.at("events")[2].at("outOfKey") == json::array({2, 11}));  // G B D in Ab major: B and D
    CHECK(cellAt(snap, 0, 1).at("outOfKey") == json::array({2, 11}));    // the browsed G cell, now borrowed
    CHECK(cellAt(snap, 0, 1).at("filled") == false);
}

// --- Snapshot: function, tension, voicing, why, sections, suggestions (Phase 2) ---------

namespace {

/// A store with C (id 1), G7 (id 2), Am (id 3) in C major, auto voice-leading on.
DocumentStore cG7Am() {
    DocumentStore store;
    store.appendChord(kC);
    store.appendChord(kG7);
    store.appendChord(chord(9, "min"));
    store.setAutoVoiceLead(true);
    return store;
}

/// The store's snapshot with `selectedEventId` passed through.
json snapshotSelecting(const DocumentStore& store, int selectedEventId) {
    const Document& d = store.doc();
    return json::parse(snapshotJson(d, buildPalette(d.browsedKey, d.projectKey), selectedEventId));
}

/// The `symbol` of every suggestion in the snapshot, in order.
std::vector<std::string> suggestionSymbols(const json& snap) {
    std::vector<std::string> out;
    for (const json& s : snap.at("suggestions")) {
        out.push_back(s.at("symbol").get<std::string>());
    }
    return out;
}

/// Checks that the snapshot's suggestions are exactly `suggestNext(previous, project, browsed)`, field by field.
void checkSuggestionsAre(const json& snap, const Chord* previous, const Document& doc) {
    const std::vector<Suggestion> expected = suggestNext(previous, doc.projectKey, doc.browsedKey);
    const json& actual = snap.at("suggestions");
    REQUIRE(actual.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CAPTURE(i);
        const json& s = actual[i];
        const Suggestion& e = expected[i];
        CHECK(s.at("root") == e.chord.root);
        CHECK(s.at("type") == e.chord.type->id);
        CHECK(s.at("symbol") == e.label);
        CHECK(s.at("roman") == e.roman);
        CHECK(s.at("function") == functionShortName(functionOf(e.chord, doc.projectKey)));
        CHECK(s.at("score").get<double>() == e.score);
        std::vector<std::string> names;
        for (const Reason r : e.reasons) {
            names.emplace_back(reasonName(r));
        }
        CHECK(s.at("reasons").get<std::vector<std::string>>() == names);
        CHECK(s.at("reasonTexts").get<std::vector<std::string>>() == e.reasonTexts);
    }
}

}  // namespace

TEST_CASE("snapshot: cells and events carry the function label and the tension in the project key") {
    const DocumentStore store = cG7Am();
    const json snap = snapshot(store);
    CHECK(snap.at("schema") == 1);  // every Phase 2 field is additive

    CHECK(cellAt(snap, 0, 1).at("function") == "T");
    CHECK(cellAt(snap, 1, 1).at("function") == "PD");
    CHECK(cellAt(snap, 2, 1).at("function") == "T");
    CHECK(cellAt(snap, 3, 1).at("function") == "PD");
    CHECK(cellAt(snap, 4, 1).at("function") == "D");
    CHECK(cellAt(snap, 5, 1).at("function") == "T");
    CHECK(cellAt(snap, 6, 1).at("function") == "D");
    CHECK(cellAt(snap, 0, 1).at("tension") == 3);
    CHECK(cellAt(snap, 4, 3).at("type") == "7");
    CHECK(cellAt(snap, 4, 3).at("tension") == 27);
    for (const json& cell : snap.at("palette").at("cells")) {
        CAPTURE(cell.dump());
        const Chord ch = chord(cell.at("root").get<int>(), cell.at("type").get<std::string>());
        CHECK(cell.at("function") == functionShortName(functionOf(ch, store.doc().projectKey)));
        CHECK(cell.at("tension") == tension(ch, store.doc().projectKey));
    }

    REQUIRE(snap.at("events").size() == 3);
    CHECK(snap.at("events")[0].at("function") == "T");
    CHECK(snap.at("events")[1].at("function") == "D");
    CHECK(snap.at("events")[2].at("function") == "T");
    CHECK(snap.at("events")[0].at("tension") == 3);
    CHECK(snap.at("events")[1].at("tension") == 27);
    CHECK(snap.at("events")[2].at("tension") == 3);

    // Against the project key, not the browsed one: Ab in C major is a predominant with two foreign notes.
    DocumentStore borrowed;
    borrowed.appendChord(chord(8, "maj"));
    const json b = snapshot(borrowed);
    CHECK(b.at("events")[0].at("function") == "PD");
    CHECK(b.at("events")[0].at("tension") == 37);
}

TEST_CASE("snapshot: events carry inversion, octave, manualVoicing and the voicing compile gives them") {
    DocumentStore store = cG7Am();
    json snap = snapshot(store);
    CHECK(snap.at("autoVoiceLead") == true);
    for (const json& e : snap.at("events")) {
        CAPTURE(e.dump());
        CHECK(e.at("inversion") == 0);
        CHECK(e.at("octave") == 0);
        CHECK(e.at("manualVoicing") == false);
    }
    // Auto voice-leading: the first chord as stored, G7 in second inversion an octave down under it, Am likewise.
    CHECK(snap.at("events")[0].at("voicing") == json({60, 64, 67}));
    CHECK(snap.at("events")[1].at("voicing") == json({62, 65, 67, 71}));
    CHECK(snap.at("events")[2].at("voicing") == json({60, 64, 69}));
    const std::vector<std::vector<int>> chosen = voicings(store.doc());
    for (std::size_t i = 0; i < chosen.size(); ++i) {
        CHECK(snap.at("events")[i].at("voicing") == json(chosen[i]));
    }
    // Palette cells keep the plain close voicing: they are not in the progression.
    CHECK(cellAt(snap, 4, 3).at("type") == "7");
    CHECK(cellAt(snap, 4, 3).at("voicing") == json(closeVoicing(kG7)));

    // A manual voicing is reported as stored and sounds as stored.
    store.setInversion(2, 1);
    store.setOctave(2, 1);
    snap = snapshot(store);
    const json& g7 = snap.at("events")[1];
    CHECK(g7.at("inversion") == 1);
    CHECK(g7.at("octave") == 1);
    CHECK(g7.at("manualVoicing") == true);
    CHECK(g7.at("voicing") == json(voicing(kG7, 1, 1)));
    CHECK(snap.at("events")[0].at("manualVoicing") == false);

    // With auto voice-leading off every chord is voiced as stored: the defaults are the close voicing.
    store.setAutoVoiceLead(false);
    snap = snapshot(store);
    CHECK(snap.at("autoVoiceLead") == false);
    CHECK(snap.at("events")[1].at("voicing") == json(voicing(kG7, 1, 1)));
    CHECK(snap.at("events")[2].at("voicing") == json(closeVoicing(chord(9, "min"))));
}

TEST_CASE("snapshot: why is null on the first event and relates every later one to the chord before it") {
    const DocumentStore store = cG7Am();
    const json snap = snapshot(store);
    REQUIRE(snap.at("events").size() == 3);
    CHECK(snap.at("events")[0].at("why").is_null());

    // C -> G7: one common tone, up a fifth, pitch-class cost 4 over four voices (smooth), not borrowed.
    const json& g7 = snap.at("events")[1].at("why");
    REQUIRE(g7.is_object());
    CHECK(g7.at("commonTones") == 1);
    CHECK(g7.at("commonPcs") == json({7}));
    CHECK(g7.at("commonNames") == json({"G"}));
    CHECK(g7.at("rootMotion") == "up a fifth");
    CHECK(g7.at("voiceLeadingCost") == 4);
    CHECK(g7.at("borrowedFrom").is_null());
    CHECK(g7.at("borrowedFromName").is_null());
    CHECK(g7.at("text") == "C → G7: up a fifth, 1 common tone (G), smooth voice leading (cost 4)");

    // G7 -> Am: nothing in common, up a step, cost 7 (not smooth), the dominant resolving to a tonic chord.
    const json& am = snap.at("events")[2].at("why");
    CHECK(am.at("commonTones") == 0);
    CHECK(am.at("commonPcs") == json::array());
    CHECK(am.at("commonNames") == json::array());
    CHECK(am.at("rootMotion") == "up a step");
    CHECK(am.at("voiceLeadingCost") == 7);
    CHECK(am.at("text") == "G7 → Am: up a step, resolves (D→T)");

    // The data agrees with `relate` field by field.
    const Relation r = relate(kG7, chord(9, "min"), store.doc().projectKey);
    CHECK(am.at("commonTones") == r.commonTones);
    CHECK(am.at("rootMotion") == rootMotionName(r.motion));
    CHECK(am.at("voiceLeadingCost") == r.voiceLeadingCost);
}

TEST_CASE("snapshot: the why sentence follows the documented rules clause by clause") {
    // Each pair is its own two-chord document; the sentence read is the second event's.
    const auto textOf = [](const Chord& from, const Chord& to, Key projectKey = key('C', 0, Mode::Ionian)) {
        DocumentStore store;
        store.setProjectKey(projectKey);
        store.appendChord(from);
        store.appendChord(to);
        return snapshot(store).at("events")[1].at("why").at("text").get<std::string>();
    };

    // The plan's example: a cadence with a common tone and a smooth move.
    CHECK(textOf(kG7, kC) == "G7 → C: down a fifth, resolves (D→T), 1 common tone (G), smooth voice leading (cost 4)");
    // Plural common tones.
    CHECK(textOf(kC, chord(9, "min")) == "C → Am: down a third, 2 common tones (C, E), smooth voice leading (cost 2)");
    // A repeated chord keeps all its tones and costs nothing.
    CHECK(textOf(kC, kC) == "C → C: same root, 3 common tones (C, E, G), smooth voice leading (cost 0)");
    // A borrowed chord names its source after everything else; common tones are spelled in the project key.
    CHECK(textOf(kC, chord(8, "maj")) ==
          "C → Ab: down a third, 1 common tone (C), smooth voice leading (cost 2), borrowed from C Aeolian");
    // Nothing in common, nothing smooth (cost 5 over three voices), in no parallel mode: the root motion
    // stands alone.
    CHECK(textOf(kC, chord(6, "min")) == "C → F#m: tritone");
    // D major in C major is Lydian's: the borrowed clause can be the only one.
    CHECK(textOf(kC, chord(2, "maj")) == "C → D: up a step, borrowed from C Lydian");
    // A move of three voices by a semitone each is smooth (cost 3 over three voices).
    CHECK(textOf(kC, chord(1, "maj")) == "C → Db: up a semitone, smooth voice leading (cost 3), borrowed from C Phrygian");
    // The altered minor scales are written in prose, as the suggestions' reason texts write them.
    CHECK(textOf(chord(0, "min"), kG7, key('C', 0, Mode::Aeolian)) ==
          "Cm → G7: up a fifth, 1 common tone (G), borrowed from C harmonic minor");
    // Four voices moving a total of four semitones is smooth; five is not.
    CHECK(textOf(kG7, chord(9, "min")) == "G7 → Am: up a step, resolves (D→T)");
    // Symbols are spelled in the project key, like the events' own `symbol`.
    CHECK(textOf(chord(10, "maj"), chord(3, "maj"), key('E', -1, Mode::Ionian)) ==
          "Bb → Eb: down a fifth, resolves (D→T), 1 common tone (Bb), smooth voice leading (cost 3)");
}

TEST_CASE("snapshot: why carries the borrowed-from key as data and as a name") {
    DocumentStore store;
    store.appendChord(kC);
    store.appendChord(chord(8, "maj"));
    const json why = snapshot(store).at("events")[1].at("why");
    CHECK(why.at("borrowedFrom") == keyJson("C", 0, "Aeolian"));
    CHECK(why.at("borrowedFromName") == "C Aeolian");

    store.setProjectKey(key('C', 0, Mode::Aeolian));
    store.appendChord(kG7);
    const json g7 = snapshot(store).at("events")[2].at("why");
    CHECK(g7.at("borrowedFrom") == keyJson("C", 0, "HarmonicMinor"));
    CHECK(g7.at("borrowedFromName") == "C HarmonicMinor");  // as `keyName` and `projectKeyName` write it
}

TEST_CASE("snapshot: sections are listed in order with their ids, names, colours and start indices") {
    DocumentStore store = cG7Am();
    CHECK(snapshot(store).at("sections") == json::array());

    REQUIRE(store.addSection(0, "Verse") == 1);
    REQUIRE(store.addSection(2) == 2);
    store.setSectionColour(2, 5);
    const json sections = snapshot(store).at("sections");
    REQUIRE(sections.size() == 2);
    CHECK(sections[0] == json({{"id", 1}, {"name", "Verse"}, {"colourIndex", 0}, {"firstEventIndex", 0}}));
    CHECK(sections[1] == json({{"id", 2}, {"name", "Section 2"}, {"colourIndex", 5}, {"firstEventIndex", 2}}));
}

TEST_CASE("snapshot: selectedEventId is echoed when it names an event, else null") {
    const DocumentStore store = cG7Am();
    CHECK(snapshot(store).at("selectedEventId").is_null());
    CHECK(snapshotSelecting(store, 2).at("selectedEventId") == 2);
    CHECK(snapshotSelecting(store, 99).at("selectedEventId").is_null());
    CHECK(snapshotSelecting(store, kNoEventId).at("selectedEventId").is_null());
}

TEST_CASE("snapshot: suggestions follow the selected event, else the last event, else open the progression") {
    const DocumentStore store = cG7Am();
    const Chord am = chord(9, "min");

    // No selection: after the last chord (Am), a predominant leads.
    json snap = snapshot(store);
    REQUIRE(snap.at("suggestions").size() <= 8);
    CHECK(snap.at("suggestions").size() == 8);
    checkSuggestionsAre(snap, &am, store.doc());
    CHECK(suggestionSymbols(snap)[0] == "F");

    // The selected event's chord: after G7 (id 2) the tonic leads, with the cadence among its reasons.
    snap = snapshotSelecting(store, 2);
    checkSuggestionsAre(snap, &kG7, store.doc());
    const json& top = snap.at("suggestions")[0];
    CHECK(top.at("symbol") == "C");
    CHECK(top.at("roman") == "I");
    CHECK(top.at("function") == "T");
    CHECK(top.at("root") == 0);
    CHECK(top.at("type") == "maj");
    const auto reasons = top.at("reasons").get<std::vector<std::string>>();
    CHECK(std::find(reasons.begin(), reasons.end(), "cadence") != reasons.end());
    CHECK(top.at("reasons").size() == top.at("reasonTexts").size());
    CHECK(top.at("score").get<double>() > snap.at("suggestions")[1].at("score").get<double>());

    // An id no event has is no selection: back to the last chord.
    snap = snapshotSelecting(store, 42);
    checkSuggestionsAre(snap, &am, store.doc());

    // An empty document opens with the tonic triad.
    const DocumentStore empty;
    snap = snapshot(empty);
    checkSuggestionsAre(snap, nullptr, empty.doc());
    CHECK(suggestionSymbols(snap)[0] == "C");
    CHECK(snap.at("suggestions")[0].at("reasons").get<std::vector<std::string>>() ==
          std::vector<std::string>{"diatonic", "tonic"});

    // The browsed key supplies the candidates, the project key the numerals and functions.
    DocumentStore browsing = cG7Am();
    browsing.setBrowsedKey(key('G', 0, Mode::Ionian));
    snap = snapshot(browsing);
    checkSuggestionsAre(snap, &am, browsing.doc());
}

// --- Snapshot: the home-key compare row -------------------------------------------------

namespace {

/// One member of every `homeTriads` entry, in order.
template <typename T>
std::vector<T> homeField(const json& snap, const char* member) {
    std::vector<T> out;
    for (const json& t : snap.at("homeTriads")) {
        out.push_back(t.at(member).get<T>());
    }
    return out;
}

const std::vector<std::string> kCMajorTriads{"C", "Dm", "Em", "F", "G", "Am", "Bmb5"};
const std::vector<std::string> kMajorRomans{"I", "ii", "iii", "IV", "V", "vi", "vii°"};

}  // namespace

TEST_CASE("snapshot: homeTriads is empty while the browsed key is the project key") {
    DocumentStore store;
    json snap = snapshot(store);
    REQUIRE(snap.at("homeTriads").is_array());
    CHECK(snap.at("homeTriads").empty());
    CHECK(snap.at("schema") == 1);  // additive

    // Away and home again, and a project key other than the default.
    store.setBrowsedKey(key('G', 0, Mode::Ionian));
    CHECK(snapshot(store).at("homeTriads").size() == 7);
    applyOk(store, {{"cmd", "browseKey"}, {"home", true}});
    CHECK(snapshot(store).at("homeTriads").empty());
    store.setProjectKey(key('A', 0, Mode::Aeolian));
    store.setBrowsedKey(key('A', 0, Mode::Aeolian));
    CHECK(snapshot(store).at("homeTriads").empty());
}

TEST_CASE("snapshot: homeTriads lists the project key's seven triads in degree order, whatever is browsed") {
    for (const Key& browsed :
         {key('A', 0, Mode::Aeolian), key('D', 0, Mode::Dorian), key('F', 1, Mode::Ionian), key('C', 0, Mode::Aeolian)}) {
        CAPTURE(keyName(browsed));
        DocumentStore store;
        store.setBrowsedKey(browsed);
        const json snap = snapshot(store);
        REQUIRE(snap.at("homeTriads").size() == 7);
        CHECK(homeField<std::string>(snap, "symbol") == kCMajorTriads);
        CHECK(homeField<std::string>(snap, "roman") == kMajorRomans);
        CHECK(homeField<int>(snap, "degree") == std::vector<int>{0, 1, 2, 3, 4, 5, 6});
        CHECK(homeField<int>(snap, "root") == std::vector<int>{0, 2, 4, 5, 7, 9, 11});
        CHECK(homeField<std::string>(snap, "type") ==
              std::vector<std::string>{"maj", "min", "min", "maj", "maj", "min", "dim"});
        CHECK(homeField<std::string>(snap, "function") == std::vector<std::string>{"T", "PD", "T", "PD", "D", "T", "D"});
        CHECK(homeField<std::string>(snap, "id") ==
              std::vector<std::string>{"home-0", "home-1", "home-2", "home-3", "home-4", "home-5", "home-6"});
        for (const json& t : snap.at("homeTriads")) {
            CAPTURE(t.dump());
            CHECK(t.at("tier") == 1);  // the palette's triad row
            CHECK(t.at("filled") == true);
            CHECK(t.at("outOfKey").empty());
        }
        CHECK(snap.at("homeTriads")[0].at("voicing") == json({60, 64, 67}));
        CHECK(snap.at("homeTriads")[0].at("family") == "Major");
        CHECK(snap.at("homeTriads")[6].at("family") == "Other");
    }
}

TEST_CASE("snapshot: homeTriads of a minor project key are spelled and numbered in that key") {
    DocumentStore store;
    store.setProjectKey(key('A', 0, Mode::Aeolian));
    store.setBrowsedKey(key('C', 0, Mode::Ionian));  // the same notes, another key: still a compare row
    json snap = snapshot(store);
    CHECK(homeField<std::string>(snap, "symbol") == std::vector<std::string>{"Am", "Bmb5", "C", "Dm", "Em", "F", "G"});
    CHECK(homeField<std::string>(snap, "roman") ==
          std::vector<std::string>{"i", "ii°", "bIII", "iv", "v", "bVI", "bVII"});

    // Flats follow the project key, not the browsed one.
    store.setProjectKey(key('C', 0, Mode::Aeolian));
    store.setBrowsedKey(key('E', 0, Mode::Ionian));
    snap = snapshot(store);
    CHECK(homeField<std::string>(snap, "symbol") ==
          std::vector<std::string>{"Cm", "Dmb5", "Eb", "Fm", "Gm", "Ab", "Bb"});

    // Harmonic minor: its own triads (the augmented III, the major V, the diminished vii).
    store.setProjectKey(key('A', 0, Mode::HarmonicMinor));
    snap = snapshot(store);
    CHECK(homeField<std::string>(snap, "type") ==
          std::vector<std::string>{"min", "dim", "aug", "min", "maj", "maj", "dim"});
}

TEST_CASE("snapshot: a home triad is the palette's triad cell of the project key, plus its id") {
    for (const Key& project : {key('C', 0, Mode::Ionian), key('A', 0, Mode::Aeolian), key('E', -1, Mode::Lydian),
                               key('D', 0, Mode::Dorian), key('F', 1, Mode::HarmonicMinor)}) {
        CAPTURE(keyName(project));
        DocumentStore store;
        store.setProjectKey(project);
        store.setBrowsedKey(project);
        const json home = snapshot(store);  // browsing the project key: the palette's tier 1 is the reference
        store.setBrowsedKey(key('B', 0, Mode::Phrygian));
        const json away = snapshot(store);
        REQUIRE(away.at("homeTriads").size() == 7);
        for (int degree = 0; degree < 7; ++degree) {
            CAPTURE(degree);
            json triad = away.at("homeTriads")[static_cast<std::size_t>(degree)];
            CHECK(triad.at("id") == "home-" + std::to_string(degree));
            triad.erase("id");
            CHECK(triad == cellAt(home, degree, 1));  // symbol, roman, function, tension, voicing, all of it
        }
        // No palette cell carries an id, so a home id cannot collide with one.
        for (const json& cell : away.at("palette").at("cells")) {
            CHECK_FALSE(cell.contains("id"));
        }
    }
}

// --- Relations ---------------------------------------------------------------------------

namespace {

/// The store's snapshot with its live selection, parsed back.
json selectedSnapshot(const DocumentStore& store) {
    const Document& d = store.doc();
    return json::parse(snapshotJson(d, buildPalette(d.browsedKey, d.projectKey), store.selectedEventId()));
}

/// Every cell the UI sizes: the palette's, then the home row's.
std::vector<json> sizedCells(const json& snap) {
    std::vector<json> out(snap.at("palette").at("cells").begin(), snap.at("palette").at("cells").end());
    out.insert(out.end(), snap.at("homeTriads").begin(), snap.at("homeTriads").end());
    return out;
}

/// True when no sized cell carries either relation member.
bool noCellRelations(const json& snap) {
    for (const json& c : sizedCells(snap)) {
        if (c.contains("relation") || c.contains("relationText")) {
            return false;
        }
    }
    return true;
}

/// The engine's own relatedness of every sized cell to `reference`, in `sizedCells` order.
std::vector<Relatedness> expectedRelations(const json& snap, const Document& doc, const Chord& reference) {
    std::vector<Chord> chords;
    for (const json& c : sizedCells(snap)) {
        chords.push_back(chord(c.at("root").get<int>(), c.at("type").get<std::string>()));
    }
    return relatedness(reference, chords, doc.projectKey, doc.browsedKey, doc.relationMode);
}

}  // namespace

TEST_CASE("snapshot: relationMode is always there; no chords means no reference and no cell relations") {
    DocumentStore store;
    json snap = selectedSnapshot(store);
    CHECK(snap.at("relationMode") == "blend");
    REQUIRE(snap.contains("relationReference"));
    CHECK(snap.at("relationReference").is_null());
    CHECK(noCellRelations(snap));

    store.setBrowsedKey(key('A', 0, Mode::Aeolian));  // home row present, still nothing to relate to
    snap = selectedSnapshot(store);
    CHECK(snap.at("homeTriads").size() == 7);
    CHECK(snap.at("relationReference").is_null());
    CHECK(noCellRelations(snap));

    for (const char* mode : {"off", "commonTones", "voiceLeading", "function", "blend"}) {
        applyOk(store, {{"cmd", "setRelationMode"}, {"mode", mode}});
        CHECK(selectedSnapshot(store).at("relationMode") == mode);
    }
}

TEST_CASE("snapshot: the reference is the selected event, else the last; every sized cell carries its relation") {
    DocumentStore store = threeChords();  // C (1), F (2), G7 (3)
    json snap = selectedSnapshot(store);
    CHECK(snap.at("relationReference") == json({{"eventId", 3}, {"symbol", "G7"}}));
    for (const json& c : sizedCells(snap)) {
        CHECK(c.contains("relation"));
        CHECK(c.at("relation").is_number_integer());
        CHECK(c.at("relationText").is_string());
    }
    std::vector<Relatedness> expected = expectedRelations(snap, store.doc(), kG7);
    std::vector<json> cells = sizedCells(snap);
    REQUIRE(cells.size() == expected.size());
    for (std::size_t i = 0; i < cells.size(); ++i) {
        CHECK(cells[i].at("relation") == expected[i].score);
        CHECK(cells[i].at("relationText") == expected[i].text);
    }
    // After G7, C is the best cell (D -> T with a cadence): 100.
    CHECK(cellAt(snap, 0, 1).at("relation") == 100);
    REQUIRE(cellAt(snap, 4, 3).at("symbol") == "G7");
    CHECK(cellAt(snap, 4, 3).at("relation") == 100);  // the reference itself

    store.setSelectedEvent(1);  // C
    applyOk(store, {{"cmd", "setRelationMode"}, {"mode", "commonTones"}});
    snap = selectedSnapshot(store);
    CHECK(snap.at("relationReference") == json({{"eventId", 1}, {"symbol", "C"}}));
    CHECK(cellAt(snap, 5, 1).at("relation") == 67);  // Am
    CHECK(cellAt(snap, 5, 1).at("relationText") == "2 common tones (C, E) · down a third · cost 2");
    CHECK(cellAt(snap, 0, 3).at("relation") == 75);  // CΔ7
    CHECK(cellAt(snap, 1, 1).at("relation") == 0);   // Dm
    CHECK(cellAt(snap, 0, 1).at("relation") == 100);

    applyOk(store, {{"cmd", "setRelationMode"}, {"mode", "function"}});
    snap = selectedSnapshot(store);
    CHECK(cellAt(snap, 4, 1).at("relation") == 60);  // C -> G
    CHECK(cellAt(snap, 3, 1).at("relation") == 80);  // C -> F
    CHECK(cellAt(snap, 0, 1).at("relation") == 30);  // C -> C: T -> T

    // A stale selection reads as none: back to the last chord.
    snap = json::parse(snapshotJson(store.doc(), buildPalette(store.doc().browsedKey, store.doc().projectKey), 99));
    CHECK(snap.at("relationReference") == json({{"eventId", 3}, {"symbol", "G7"}}));
}

TEST_CASE("snapshot: mode off drops the reference and every cell relation") {
    DocumentStore store = threeChords();
    store.setBrowsedKey(key('E', 0, Mode::Ionian));
    applyOk(store, {{"cmd", "setRelationMode"}, {"mode", "off"}});
    const json snap = selectedSnapshot(store);
    CHECK(snap.at("relationMode") == "off");
    CHECK(snap.at("relationReference").is_null());
    CHECK(snap.at("homeTriads").size() == 7);
    CHECK(noCellRelations(snap));
}

TEST_CASE("snapshot: blend is normalised over the palette and the home row together") {
    DocumentStore store = threeChords();
    store.setSelectedEvent(2);  // F
    store.setBrowsedKey(key('E', 0, Mode::Ionian));
    const json snap = selectedSnapshot(store);
    CHECK(snap.at("relationReference") == json({{"eventId", 2}, {"symbol", "F"}}));
    const std::vector<json> cells = sizedCells(snap);
    REQUIRE(snap.at("homeTriads").size() == 7);
    const std::vector<Relatedness> expected = expectedRelations(snap, store.doc(), kF);
    REQUIRE(cells.size() == expected.size());
    int lo = 100;
    int hi = 0;
    for (std::size_t i = 0; i < cells.size(); ++i) {
        CHECK(cells[i].at("relation") == expected[i].score);
        CHECK(cells[i].at("relationText") == expected[i].text);
        lo = std::min(lo, cells[i].at("relation").get<int>());
        hi = std::max(hi, cells[i].at("relation").get<int>());
    }
    CHECK(lo == 0);
    CHECK(hi == 100);
    // One population: the home row is not normalised on its own (its worst is not forced to 0).
    const json& home = snap.at("homeTriads");
    CHECK(home[3].at("relation") == 100);  // F itself
    CHECK(home[4].at("relation") == 100);  // G: the best chord after F, D after PD, the top of the population
    // The relation text is spelled in the project key, borrowings and all.
    CHECK(home[0].at("relationText") == "1 common tone (C) · up a fifth · cost 3");
}

TEST_CASE("snapshot: the reference symbol is the event's own symbol, spelled in the project key") {
    DocumentStore store;
    store.setProjectKey(key('E', -1, Mode::Ionian));
    store.appendChord(chord(3, "maj"));   // Eb
    store.appendChord(chord(10, "7"));    // Bb7
    store.appendChord(chord(1, "maj"));   // Db: borrowed bVII
    json snap = selectedSnapshot(store);
    CHECK(snap.at("relationReference") == json({{"eventId", 3}, {"symbol", "Db"}}));
    CHECK(snap.at("relationReference").at("symbol") == snap.at("events")[2].at("symbol"));
    store.setSelectedEvent(2);
    snap = selectedSnapshot(store);
    CHECK(snap.at("relationReference") == json({{"eventId", 2}, {"symbol", snap.at("events")[1].at("symbol")}}));
    CHECK(snap.at("relationReference").at("symbol") == "Bb7");
}

// --- browseKey -------------------------------------------------------------------------

TEST_CASE("browseKey steps: moves the browsed key along the fifths and hollows the borrowed cells") {
    DocumentStore store;
    applyOk(store, {{"cmd", "browseKey"}, {"steps", 1}});

    json snap = snapshot(store);
    CHECK(snap.at("browsedKeyName") == "G Ionian");
    CHECK(snap.at("browsedKey") == keyJson("G", 0, "Ionian"));
    CHECK(snap.at("projectKeyName") == "C Ionian");
    CHECK(tierSymbols(snap, 1) == std::vector<std::string>{"G", "Am", "Bm", "C", "D", "Em", "F#mb5"});

    int hollow = 0;
    for (const json& cell : snap.at("palette").at("cells")) {
        CAPTURE(cell.dump());
        const Chord ch = chord(cell.at("root").get<int>(), cell.at("type").get<std::string>());
        const auto pcs = pitchClasses(ch);
        const bool hasFSharp = std::find(pcs.begin(), pcs.end(), 6) != pcs.end();
        CHECK(cell.at("filled") == !hasFSharp);
        hollow += hasFSharp ? 1 : 0;
    }
    CHECK(hollow > 0);
    CHECK(cellAt(snap, 6, 1).at("roman") == "#iv°");  // numerals stay relative to the project key

    applyOk(store, {{"cmd", "browseKey"}, {"steps", -2}});
    CHECK(snapshot(store).at("browsedKeyName") == "F Ionian");

    applyOk(store, {{"cmd", "browseKey"}, {"home", true}});
    snap = snapshot(store);
    CHECK(snap.at("browsedKeyName") == "C Ionian");
    CHECK(snap.at("browsedKey") == keyJson("C", 0, "Ionian"));

    CHECK_FALSE(store.undo());  // browsing is live state: no history
}

TEST_CASE("browseKey mode: keeps the tonic pitch class with its canonical spelling") {
    DocumentStore store;
    applyOk(store, {{"cmd", "browseKey"}, {"mode", "Aeolian"}});
    json snap = snapshot(store);
    CHECK(snap.at("browsedKeyName") == "C Aeolian");
    CHECK(snap.at("browsedKey") == keyJson("C", 0, "Aeolian"));
    CHECK(tierSymbols(snap, 1) == std::vector<std::string>{"Cm", "Dmb5", "Eb", "Fm", "Gm", "Ab", "Bb"});
    CHECK(cellAt(snap, 2, 1).at("roman") == "bIII");

    applyOk(store, {{"cmd", "browseKey"}, {"steps", 6}});
    CHECK(snapshot(store).at("browsedKeyName") == "F# Aeolian");
    applyOk(store, {{"cmd", "browseKey"}, {"mode", "Lydian"}});
    CHECK(snapshot(store).at("browsedKeyName") == "Gb Lydian");  // 5 flats beat F# Lydian's 7 sharps
    applyOk(store, {{"cmd", "browseKey"}, {"mode", "HarmonicMinor"}});
    CHECK(snapshot(store).at("browsedKeyName") == "F# HarmonicMinor");
}

TEST_CASE("browseKey home: returns to the project key after setKey") {
    DocumentStore store;
    applyOk(store, {{"cmd", "setKey"}, {"letter", "E"}, {"alter", -1}, {"mode", "Ionian"}});
    applyOk(store, {{"cmd", "browseKey"}, {"steps", 2}});
    CHECK(snapshot(store).at("browsedKeyName") == "D Ionian");  // browsing still starts from C: setKey left it
    applyOk(store, {{"cmd", "browseKey"}, {"home", true}});
    CHECK(snapshot(store).at("browsedKeyName") == "Eb Ionian");
}

// --- setKey ----------------------------------------------------------------------------

TEST_CASE("setKey: sets the project key only and is undoable") {
    DocumentStore store;
    applyOk(store, {{"cmd", "setKey"}, {"letter", "D"}, {"alter", 0}, {"mode", "Dorian"}});
    json snap = snapshot(store);
    CHECK(snap.at("projectKeyName") == "D Dorian");
    CHECK(snap.at("projectKey") == keyJson("D", 0, "Dorian"));
    CHECK(snap.at("browsedKeyName") == "C Ionian");

    applyOk(store, {{"cmd", "setKey"}, {"letter", "B"}, {"alter", -1}, {"mode", "MelodicMinor"}});
    CHECK(snapshot(store).at("projectKeyName") == "Bb MelodicMinor");

    REQUIRE(store.undo());
    CHECK(store.doc().projectKey == key('D', 0, Mode::Dorian));
    REQUIRE(store.undo());
    CHECK(store.doc().projectKey == key('C', 0, Mode::Ionian));
    CHECK_FALSE(store.undo());
}

// --- Editing ---------------------------------------------------------------------------

TEST_CASE("edit commands: move, length, velocity, remove") {
    DocumentStore store = threeChords();

    applyOk(store, {{"cmd", "moveChord"}, {"id", 3}, {"index", 0}});
    CHECK(snapshotIds(snapshot(store)) == std::vector<int>{3, 1, 2});

    applyOk(store, {{"cmd", "setLength"}, {"id", 1}, {"lengthBeats", 2.0}});
    applyOk(store, {{"cmd", "setVelocity"}, {"id", 2}, {"velocity", 64}});
    json snap = snapshot(store);
    CHECK(snap.at("events")[1].at("lengthBeats").get<double>() == 2.0);
    CHECK(snap.at("events")[2].at("startBeat").get<double>() == 6.0);
    CHECK(snap.at("events")[2].at("velocity") == 64);
    CHECK(snap.at("endBeat").get<double>() == 10.0);

    applyOk(store, {{"cmd", "removeChord"}, {"id", 1}});
    snap = snapshot(store);
    CHECK(snapshotIds(snap) == std::vector<int>{3, 2});
    CHECK(snap.at("events")[1].at("startBeat").get<double>() == 4.0);

    // Out-of-range values are clamped by the store, not rejected.
    applyOk(store, {{"cmd", "setLength"}, {"id", 2}, {"lengthBeats", 1000}});
    applyOk(store, {{"cmd", "setVelocity"}, {"id", 2}, {"velocity", 200}});
    applyOk(store, {{"cmd", "setLength"}, {"id", 3}, {"lengthBeats", 0.01}});
    snap = snapshot(store);
    CHECK(snap.at("events")[1].at("lengthBeats").get<double>() == kMaxLengthBeats);
    CHECK(snap.at("events")[1].at("velocity") == kMaxVelocity);
    CHECK(snap.at("events")[0].at("lengthBeats").get<double>() == kMinLengthBeats);
}

TEST_CASE("edit commands: unknown ids are accepted as no-ops and record no history") {
    DocumentStore store;
    applyOk(store, {{"cmd", "appendChord"}, {"root", 0}, {"type", "maj"}});
    const Document before = store.doc();

    std::string error;
    CHECK(apply(store, {{"cmd", "moveChord"}, {"id", 99}, {"index", 0}}, error));
    CHECK(apply(store, {{"cmd", "setLength"}, {"id", 99}, {"lengthBeats", 1.0}}, error));
    CHECK(apply(store, {{"cmd", "setVelocity"}, {"id", 99}, {"velocity", 1}}, error));
    CHECK(apply(store, {{"cmd", "removeChord"}, {"id", 99}}, error));
    CHECK(error.empty());
    CHECK(store.doc() == before);

    REQUIRE(store.undo());  // the append
    CHECK(store.doc().events.empty());
    CHECK_FALSE(store.undo());
}

TEST_CASE("toggles: setProgressionLoop and setFollowHost are live state") {
    DocumentStore store;
    applyOk(store, {{"cmd", "setProgressionLoop"}, {"on", false}});
    applyOk(store, {{"cmd", "setFollowHost"}, {"on", false}});
    json snap = snapshot(store);
    CHECK(snap.at("progressionLoop") == false);
    CHECK(snap.at("followHost") == false);
    CHECK_FALSE(store.undo());

    applyOk(store, {{"cmd", "setFollowHost"}, {"on", true}});
    CHECK(snapshot(store).at("followHost") == true);
}

TEST_CASE("setRelationMode: each of the five modes is live state, untouched by undo and redo") {
    DocumentStore store;
    const std::vector<std::pair<const char*, RelationMode>> modes{{"off", RelationMode::Off},
                                                                  {"commonTones", RelationMode::CommonTones},
                                                                  {"voiceLeading", RelationMode::VoiceLeading},
                                                                  {"function", RelationMode::Function},
                                                                  {"blend", RelationMode::Blend}};
    for (const auto& [name, mode] : modes) {
        applyOk(store, {{"cmd", "setRelationMode"}, {"mode", name}});
        CHECK(store.doc().relationMode == mode);
    }
    CHECK_FALSE(store.undo());  // never history

    applyOk(store, {{"cmd", "appendChord"}, {"root", 0}, {"type", "maj"}});
    applyOk(store, {{"cmd", "setRelationMode"}, {"mode", "function"}});
    applyOk(store, {{"cmd", "undo"}});
    CHECK(store.doc().events.empty());
    CHECK(store.doc().relationMode == RelationMode::Function);
    applyOk(store, {{"cmd", "setRelationMode"}, {"mode", "off"}});
    applyOk(store, {{"cmd", "redo"}});  // the redo stack survived the mode change
    CHECK(store.doc().events.size() == 1);
    CHECK(store.doc().relationMode == RelationMode::Off);
}

TEST_CASE("setRelationMode: a missing, mistyped or unknown mode is rejected with the five names") {
    DocumentStore store;
    store.setRelationMode(RelationMode::CommonTones);
    for (const json& cmd : {json{{"cmd", "setRelationMode"}},
                            json{{"cmd", "setRelationMode"}, {"mode", "Blend"}},
                            json{{"cmd", "setRelationMode"}, {"mode", "none"}},
                            json{{"cmd", "setRelationMode"}, {"mode", ""}},
                            json{{"cmd", "setRelationMode"}, {"mode", 4}},
                            json{{"cmd", "setRelationMode"}, {"mode", nullptr}},
                            json{{"cmd", "setRelationMode"}, {"on", true}}}) {
        CAPTURE(cmd.dump());
        checkRejected(store, cmd);
        std::string error;
        CHECK_FALSE(apply(store, cmd, error));
        for (const char* name : {"\"off\"", "\"commonTones\"", "\"voiceLeading\"", "\"function\"", "\"blend\""}) {
            CHECK_MESSAGE(error.find(name) != std::string::npos, error);
        }
        CHECK(error.find("\"mode\"") != std::string::npos);
    }
    CHECK(store.doc().relationMode == RelationMode::CommonTones);
}

TEST_CASE("undo / redo commands") {
    DocumentStore store;
    std::string error;
    CHECK(apply(store, {{"cmd", "undo"}}, error));  // nothing to undo: a no-op, not an error
    CHECK(apply(store, {{"cmd", "redo"}}, error));
    CHECK(error.empty());

    applyOk(store, {{"cmd", "appendChord"}, {"root", 0}, {"type", "maj"}});
    applyOk(store, {{"cmd", "appendChord"}, {"root", 5}, {"type", "maj"}});
    applyOk(store, {{"cmd", "undo"}});
    CHECK(snapshotIds(snapshot(store)) == std::vector<int>{1});
    applyOk(store, {{"cmd", "undo"}});
    CHECK(snapshot(store).at("events").empty());
    applyOk(store, {{"cmd", "redo"}});
    applyOk(store, {{"cmd", "redo"}});
    CHECK(snapshotIds(snapshot(store)) == std::vector<int>{1, 2});
    CHECK(apply(store, {{"cmd", "redo"}}, error));
    CHECK(snapshotIds(snapshot(store)) == std::vector<int>{1, 2});
}

// --- Voicing, sections and selection commands (Phase 2) --------------------------------

namespace {

/// The snapshot's event with `id`; null if there is none.
json eventWithId(const json& snap, int id) {
    for (const json& e : snap.at("events")) {
        if (e.at("id") == id) {
            return e;
        }
    }
    return json();
}

}  // namespace

TEST_CASE("voicing commands: setInversion, setOctave and resetVoicing edit one event's voicing, clamped, undoably") {
    DocumentStore store = cG7Am();

    applyOk(store, {{"cmd", "setInversion"}, {"id", 2}, {"inversion", 2}});
    json g7 = eventWithId(snapshot(store), 2);
    CHECK(g7.at("inversion") == 2);
    CHECK(g7.at("octave") == 0);
    CHECK(g7.at("manualVoicing") == true);
    CHECK(g7.at("voicing") == json(voicing(kG7, 2, 0)));  // manual: honoured under auto voice-leading

    applyOk(store, {{"cmd", "setOctave"}, {"id", 2}, {"octave", -1}});
    g7 = eventWithId(snapshot(store), 2);
    CHECK(g7.at("octave") == -1);
    CHECK(g7.at("voicing") == json(voicing(kG7, 2, -1)));

    // Out-of-range values are clamped by the store, not rejected.
    applyOk(store, {{"cmd", "setInversion"}, {"id", 2}, {"inversion", 9}});
    applyOk(store, {{"cmd", "setOctave"}, {"id", 2}, {"octave", 5}});
    g7 = eventWithId(snapshot(store), 2);
    CHECK(g7.at("inversion") == inversionCount(kG7) - 1);
    CHECK(g7.at("octave") == kMaxVoicingOctave);
    applyOk(store, {{"cmd", "setInversion"}, {"id", 2}, {"inversion", -3}});
    applyOk(store, {{"cmd", "setOctave"}, {"id", 2}, {"octave", -7}});
    g7 = eventWithId(snapshot(store), 2);
    CHECK(g7.at("inversion") == 0);
    CHECK(g7.at("octave") == kMinVoicingOctave);

    applyOk(store, {{"cmd", "resetVoicing"}, {"id", 2}});
    g7 = eventWithId(snapshot(store), 2);
    CHECK(g7.at("inversion") == 0);
    CHECK(g7.at("octave") == 0);
    CHECK(g7.at("manualVoicing") == false);
    CHECK(g7.at("voicing") == json({62, 65, 67, 71}));  // auto voice-leading decides again

    // Undoable, one step each.
    applyOk(store, {{"cmd", "undo"}});
    g7 = eventWithId(snapshot(store), 2);
    CHECK(g7.at("manualVoicing") == true);
    CHECK(g7.at("octave") == kMinVoicingOctave);
    applyOk(store, {{"cmd", "undo"}});
    CHECK(eventWithId(snapshot(store), 2).at("octave") == kMaxVoicingOctave);

    // The other events are untouched throughout.
    CHECK(eventWithId(snapshot(store), 1).at("manualVoicing") == false);
    CHECK(eventWithId(snapshot(store), 3).at("manualVoicing") == false);
}

TEST_CASE("voicing commands: unknown ids are accepted as no-ops and record no history") {
    DocumentStore store = cG7Am();
    const Document before = store.doc();
    std::string error;
    CHECK(apply(store, {{"cmd", "setInversion"}, {"id", 99}, {"inversion", 1}}, error));
    CHECK(apply(store, {{"cmd", "setOctave"}, {"id", 99}, {"octave", 1}}, error));
    CHECK(apply(store, {{"cmd", "resetVoicing"}, {"id", 99}}, error));
    CHECK(error.empty());
    CHECK(store.doc() == before);
    REQUIRE(store.undo());  // the setAutoVoiceLead of cG7Am, not a phantom entry
    CHECK(store.doc().autoVoiceLead == false);
}

TEST_CASE("setAutoVoiceLead: undoable, and the compiled voicings follow it") {
    DocumentStore store = cG7Am();
    applyOk(store, {{"cmd", "setAutoVoiceLead"}, {"on", false}});
    json snap = snapshot(store);
    CHECK(snap.at("autoVoiceLead") == false);
    CHECK(snap.at("events")[1].at("voicing") == json(closeVoicing(kG7)));

    applyOk(store, {{"cmd", "setAutoVoiceLead"}, {"on", true}});
    snap = snapshot(store);
    CHECK(snap.at("autoVoiceLead") == true);
    CHECK(snap.at("events")[1].at("voicing") == json({62, 65, 67, 71}));

    applyOk(store, {{"cmd", "undo"}});
    CHECK(snapshot(store).at("autoVoiceLead") == false);
    applyOk(store, {{"cmd", "redo"}});
    CHECK(snapshot(store).at("autoVoiceLead") == true);

    std::string error;
    CHECK(apply(store, {{"cmd", "setAutoVoiceLead"}, {"on", true}}, error));  // the same value: a no-op
    applyOk(store, {{"cmd", "undo"}});
    CHECK(snapshot(store).at("autoVoiceLead") == false);
}

TEST_CASE("section commands: add, rename, colour, move and remove, in the snapshot's sections list") {
    DocumentStore store = cG7Am();
    applyOk(store, {{"cmd", "addSection"}, {"index", 0}, {"name", "Verse"}});
    json sections = snapshot(store).at("sections");
    REQUIRE(sections.size() == 1);
    CHECK(sections[0] == json({{"id", 1}, {"name", "Verse"}, {"colourIndex", 0}, {"firstEventIndex", 0}}));

    applyOk(store, {{"cmd", "addSection"}, {"index", 2}});  // name optional: the store's default
    sections = snapshot(store).at("sections");
    REQUIRE(sections.size() == 2);
    CHECK(sections[1] == json({{"id", 2}, {"name", "Section 2"}, {"colourIndex", 1}, {"firstEventIndex", 2}}));

    applyOk(store, {{"cmd", "renameSection"}, {"id", 2}, {"name", "Chorus"}});
    applyOk(store, {{"cmd", "setSectionColour"}, {"id", 2}, {"colourIndex", 99}});  // clamped by the store
    applyOk(store, {{"cmd", "moveSectionStart"}, {"id", 2}, {"index", 1}});
    sections = snapshot(store).at("sections");
    CHECK(sections[1] == json({{"id", 2}, {"name", "Chorus"}, {"colourIndex", kNumSectionColours - 1}, {"firstEventIndex", 1}}));
    applyOk(store, {{"cmd", "setSectionColour"}, {"id", 2}, {"colourIndex", -4}});
    CHECK(snapshot(store).at("sections")[1].at("colourIndex") == 0);

    applyOk(store, {{"cmd", "removeSection"}, {"id", 1}});  // the first: the next takes index 0
    sections = snapshot(store).at("sections");
    REQUIRE(sections.size() == 1);
    CHECK(sections[0].at("id") == 2);
    CHECK(sections[0].at("firstEventIndex") == 0);

    // Each one undoable.
    applyOk(store, {{"cmd", "undo"}});
    CHECK(snapshot(store).at("sections").size() == 2);
    applyOk(store, {{"cmd", "undo"}});  // the colour -4
    applyOk(store, {{"cmd", "undo"}});  // the move
    CHECK(snapshot(store).at("sections")[1].at("firstEventIndex") == 2);
}

TEST_CASE("addSection: the first section at an inner chord gets a leading section, as the store does") {
    DocumentStore store = cG7Am();
    applyOk(store, {{"cmd", "addSection"}, {"index", 1}, {"name", "B"}});
    const json sections = snapshot(store).at("sections");
    REQUIRE(sections.size() == 2);
    CHECK(sections[0] == json({{"id", 1}, {"name", "Section 1"}, {"colourIndex", 0}, {"firstEventIndex", 0}}));
    CHECK(sections[1] == json({{"id", 2}, {"name", "B"}, {"colourIndex", 1}, {"firstEventIndex", 1}}));
}

TEST_CASE("section commands: an index that is no chord position or already starts a section, and unknown ids, are no-ops") {
    DocumentStore store = cG7Am();
    applyOk(store, {{"cmd", "addSection"}, {"index", 0}});
    applyOk(store, {{"cmd", "addSection"}, {"index", 2}});
    const Document before = store.doc();
    const int nextSectionId = store.nextSectionId();

    std::string error;
    CHECK(apply(store, {{"cmd", "addSection"}, {"index", 3}}, error));
    CHECK(apply(store, {{"cmd", "addSection"}, {"index", -1}}, error));
    CHECK(apply(store, {{"cmd", "addSection"}, {"index", 2}}, error));  // already starts one
    CHECK(apply(store, {{"cmd", "renameSection"}, {"id", 99}, {"name", "x"}}, error));
    CHECK(apply(store, {{"cmd", "setSectionColour"}, {"id", 99}, {"colourIndex", 1}}, error));
    CHECK(apply(store, {{"cmd", "removeSection"}, {"id", 99}}, error));
    CHECK(apply(store, {{"cmd", "moveSectionStart"}, {"id", 99}, {"index", 1}}, error));
    CHECK(apply(store, {{"cmd", "moveSectionStart"}, {"id", 1}, {"index", 1}}, error));   // the first is pinned
    CHECK(apply(store, {{"cmd", "moveSectionStart"}, {"id", 2}, {"index", 7}}, error));   // out of range
    CHECK(apply(store, {{"cmd", "moveSectionStart"}, {"id", 2}, {"index", 0}}, error));   // a collision
    CHECK(error.empty());
    CHECK(store.doc() == before);
    CHECK(store.nextSectionId() == nextSectionId);  // no id consumed by the refused adds

    DocumentStore empty;
    CHECK(apply(empty, {{"cmd", "addSection"}, {"index", 0}}, error));  // no chord to start on
    CHECK(empty.doc() == Document{});

    REQUIRE(store.undo());  // the second add, not a phantom entry
    CHECK(store.doc().sections.size() == 1);
}

TEST_CASE("addSection / renameSection: a name longer than kMaxSectionNameBytes is rejected whole; the bound itself passes") {
    DocumentStore store = cG7Am();
    const std::string atBound(kMaxSectionNameBytes, 'n');
    const std::string over = atBound + "n";
    applyOk(store, {{"cmd", "addSection"}, {"index", 0}, {"name", atBound}});
    CHECK(store.doc().sections[0].name == atBound);

    checkRejected(store, {{"cmd", "addSection"}, {"index", 1}, {"name", over}});
    checkRejected(store, {{"cmd", "addSection"}, {"index", 1}, {"name", std::string(63, 'a') + "\xC3\xA9"}});  // 65 bytes
    checkRejected(store, {{"cmd", "renameSection"}, {"id", 1}, {"name", over}});
    checkRejected(store, {{"cmd", "renameSection"}, {"id", 1}, {"name", std::string(1000, 'z')}});
    CHECK(store.nextSectionId() == 2);

    applyOk(store, {{"cmd", "renameSection"}, {"id", 1}, {"name", std::string(62, 'a') + "\xC3\xA9"}});  // 64 bytes
    CHECK(store.doc().sections[0].name.size() == static_cast<std::size_t>(kMaxSectionNameBytes));
}

TEST_CASE("addSection: refused with a clear error when the section ids are exhausted, changing nothing") {
    DocumentStore store;
    Document doc = cG7Am().doc();
    store.replace(doc, 4, INT_MAX);
    const Document before = store.doc();
    std::string error;
    CHECK_FALSE(apply(store, {{"cmd", "addSection"}, {"index", 0}}, error));
    CAPTURE(error);
    CHECK(error.find("addSection") != std::string::npos);
    CHECK(store.doc() == before);
    CHECK_FALSE(store.undo());
}

TEST_CASE("selectEvent: live selection in the store; null clears, unknown ids are no-ops, a removed event is deselected") {
    DocumentStore store = cG7Am();
    applyOk(store, {{"cmd", "selectEvent"}, {"id", 2}});
    CHECK(store.selectedEventId() == 2);
    json snap = snapshotSelecting(store, store.selectedEventId());
    CHECK(snap.at("selectedEventId") == 2);
    CHECK(suggestionSymbols(snap)[0] == "C");  // after G7

    std::string error;
    CHECK(apply(store, {{"cmd", "selectEvent"}, {"id", 99}}, error));
    CHECK(store.selectedEventId() == 2);

    applyOk(store, {{"cmd", "selectEvent"}, {"id", nullptr}});
    CHECK(store.selectedEventId() == kNoEventId);
    CHECK(snapshotSelecting(store, store.selectedEventId()).at("selectedEventId").is_null());

    applyOk(store, {{"cmd", "selectEvent"}, {"id", 3}});
    applyOk(store, {{"cmd", "removeChord"}, {"id", 3}});
    CHECK(store.selectedEventId() == kNoEventId);
    applyOk(store, {{"cmd", "undo"}});
    CHECK(store.selectedEventId() == kNoEventId);  // back, but not reselected

    applyOk(store, {{"cmd", "selectEvent"}, {"id", 1}});
    applyOk(store, {{"cmd", "undo"}});  // the setAutoVoiceLead: the selection is live state and survives
    CHECK(store.selectedEventId() == 1);
    applyOk(store, {{"cmd", "redo"}});
    CHECK(store.selectedEventId() == 1);
}

TEST_CASE("selectEvent: never records history") {
    DocumentStore store;
    applyOk(store, {{"cmd", "appendChord"}, {"root", 0}, {"type", "maj"}});
    applyOk(store, {{"cmd", "selectEvent"}, {"id", 1}});
    applyOk(store, {{"cmd", "selectEvent"}, {"id", nullptr}});
    REQUIRE(store.undo());  // the append
    CHECK_FALSE(store.undo());
}

TEST_CASE("rejection: Phase 2 commands with missing or mistyped fields are refused whole") {
    DocumentStore store = cG7Am();
    applyOk(store, {{"cmd", "addSection"}, {"index", 0}, {"name", "Verse"}});
    applyOk(store, {{"cmd", "selectEvent"}, {"id", 2}});

    checkRejected(store, {{"cmd", "setInversion"}, {"id", 1}});
    checkRejected(store, {{"cmd", "setInversion"}, {"inversion", 1}});
    checkRejected(store, {{"cmd", "setInversion"}, {"id", 1}, {"inversion", "1"}});
    checkRejected(store, {{"cmd", "setInversion"}, {"id", 1}, {"inversion", 1.5}});
    checkRejected(store, {{"cmd", "setInversion"}, {"id", "1"}, {"inversion", 1}});
    checkRejected(store, {{"cmd", "setOctave"}, {"id", 1}});
    checkRejected(store, {{"cmd", "setOctave"}, {"id", 1}, {"octave", "up"}});
    checkRejected(store, {{"cmd", "setOctave"}, {"id", 1}, {"octave", 0.5}});
    checkRejected(store, {{"cmd", "resetVoicing"}});
    checkRejected(store, {{"cmd", "resetVoicing"}, {"id", "1"}});
    checkRejected(store, {{"cmd", "setAutoVoiceLead"}});
    checkRejected(store, {{"cmd", "setAutoVoiceLead"}, {"on", 1}});
    checkRejected(store, {{"cmd", "setAutoVoiceLead"}, {"on", "true"}});
    checkRejected(store, {{"cmd", "addSection"}});
    checkRejected(store, {{"cmd", "addSection"}, {"index", "0"}});
    checkRejected(store, {{"cmd", "addSection"}, {"index", 0.5}});
    checkRejected(store, {{"cmd", "addSection"}, {"index", 1}, {"name", 5}});
    checkRejected(store, {{"cmd", "addSection"}, {"index", 1}, {"name", nullptr}});
    checkRejected(store, {{"cmd", "renameSection"}, {"id", 1}});
    checkRejected(store, {{"cmd", "renameSection"}, {"name", "x"}});
    checkRejected(store, {{"cmd", "renameSection"}, {"id", 1}, {"name", 3}});
    checkRejected(store, {{"cmd", "setSectionColour"}, {"id", 1}});
    checkRejected(store, {{"cmd", "setSectionColour"}, {"id", 1}, {"colourIndex", "red"}});
    checkRejected(store, {{"cmd", "setSectionColour"}, {"id", 1}, {"colourIndex", 1.5}});
    checkRejected(store, {{"cmd", "removeSection"}});
    checkRejected(store, {{"cmd", "removeSection"}, {"id", "1"}});
    checkRejected(store, {{"cmd", "moveSectionStart"}, {"id", 1}});
    checkRejected(store, {{"cmd", "moveSectionStart"}, {"index", 1}});
    checkRejected(store, {{"cmd", "moveSectionStart"}, {"id", 1}, {"index", 1.5}});
    checkRejected(store, {{"cmd", "selectEvent"}});
    checkRejected(store, {{"cmd", "selectEvent"}, {"id", "2"}});
    checkRejected(store, {{"cmd", "selectEvent"}, {"id", true}});
    checkRejected(store, {{"cmd", "selectEvent"}, {"id", 2.5}});
    checkRejected(store, {{"cmd", "selectEvent"}, {"id", json::array()}});

    CHECK(store.selectedEventId() == 2);  // a rejected selectEvent leaves the selection alone
    REQUIRE(store.undo());  // the addSection: no phantom entries
    CHECK(store.doc().sections.empty());
}

// --- Rejection -------------------------------------------------------------------------

TEST_CASE("rejection: a fresh store is untouched and gains no history") {
    DocumentStore store;

    checkRejectedRaw(store, "");
    checkRejectedRaw(store, "{not json");
    checkRejectedRaw(store, "[]");
    checkRejectedRaw(store, "42");
    checkRejectedRaw(store, "null");
    checkRejectedRaw(store, R"({"cmd":"undo"})");                     // missing schema
    checkRejectedRaw(store, R"({"schema":"1","cmd":"undo"})");        // schema must be a number
    checkRejectedRaw(store, R"({"schema":2,"cmd":"undo"})");
    checkRejectedRaw(store, R"({"schema":1})");                       // missing cmd
    checkRejectedRaw(store, R"({"schema":1,"cmd":5})");
    checkRejectedRaw(store, R"({"schema":1,"cmd":"explode"})");
    checkRejectedRaw(store, R"({"schema":1,"cmd":"appendChord","root":0,"type":"maj","lengthBeats":1e999})");

    // appendChord
    checkRejected(store, {{"cmd", "appendChord"}, {"root", 0}, {"type", "nope"}});
    checkRejected(store, {{"cmd", "appendChord"}, {"root", 12}, {"type", "maj"}});
    checkRejected(store, {{"cmd", "appendChord"}, {"root", -1}, {"type", "maj"}});
    checkRejected(store, {{"cmd", "appendChord"}, {"root", "0"}, {"type", "maj"}});
    checkRejected(store, {{"cmd", "appendChord"}, {"root", 0.5}, {"type", "maj"}});
    checkRejected(store, {{"cmd", "appendChord"}, {"type", "maj"}});
    checkRejected(store, {{"cmd", "appendChord"}, {"root", 0}});
    checkRejected(store, {{"cmd", "appendChord"}, {"root", 0}, {"type", 7}});
    checkRejected(store, {{"cmd", "appendChord"}, {"root", 0}, {"type", "maj"}, {"lengthBeats", "nan"}});
    checkRejected(store, {{"cmd", "appendChord"}, {"root", 0}, {"type", "maj"}, {"velocity", 1.5}});
    checkRejected(store, {{"cmd", "appendChord"}, {"root", 0}, {"type", "maj"}, {"velocity", "loud"}});

    // setKey
    checkRejected(store, {{"cmd", "setKey"}, {"letter", "H"}, {"alter", 0}, {"mode", "Ionian"}});
    checkRejected(store, {{"cmd", "setKey"}, {"letter", "c"}, {"alter", 0}, {"mode", "Ionian"}});
    checkRejected(store, {{"cmd", "setKey"}, {"letter", "CC"}, {"alter", 0}, {"mode", "Ionian"}});
    checkRejected(store, {{"cmd", "setKey"}, {"letter", ""}, {"alter", 0}, {"mode", "Ionian"}});
    checkRejected(store, {{"cmd", "setKey"}, {"letter", 0}, {"alter", 0}, {"mode", "Ionian"}});
    checkRejected(store, {{"cmd", "setKey"}, {"letter", "C"}, {"alter", 2}, {"mode", "Ionian"}});
    checkRejected(store, {{"cmd", "setKey"}, {"letter", "C"}, {"alter", "0"}, {"mode", "Ionian"}});
    checkRejected(store, {{"cmd", "setKey"}, {"letter", "C"}, {"alter", 0}, {"mode", "Major"}});
    checkRejected(store, {{"cmd", "setKey"}, {"letter", "C"}, {"alter", 0}, {"mode", 0}});
    checkRejected(store, {{"cmd", "setKey"}, {"letter", "C"}, {"alter", 0}});

    // browseKey: exactly one selector, each well typed
    checkRejected(store, {{"cmd", "browseKey"}});
    checkRejected(store, {{"cmd", "browseKey"}, {"steps", 1}, {"home", true}});
    checkRejected(store, {{"cmd", "browseKey"}, {"steps", 1}, {"mode", "Dorian"}});
    checkRejected(store, {{"cmd", "browseKey"}, {"home", false}});
    checkRejected(store, {{"cmd", "browseKey"}, {"home", 1}});
    checkRejected(store, {{"cmd", "browseKey"}, {"steps", 1.5}});
    checkRejected(store, {{"cmd", "browseKey"}, {"steps", "1"}});
    checkRejected(store, {{"cmd", "browseKey"}, {"mode", 3}});
    checkRejected(store, {{"cmd", "browseKey"}, {"mode", "Minor"}});

    // Editing and toggles
    checkRejected(store, {{"cmd", "moveChord"}, {"id", 1}});
    checkRejected(store, {{"cmd", "moveChord"}, {"id", "1"}, {"index", 0}});
    checkRejected(store, {{"cmd", "moveChord"}, {"id", 1}, {"index", 0.5}});
    checkRejected(store, {{"cmd", "setLength"}, {"id", 1}, {"lengthBeats", "nan"}});
    checkRejected(store, {{"cmd", "setLength"}, {"id", 1}});
    checkRejected(store, {{"cmd", "setVelocity"}, {"id", 1}, {"velocity", "100"}});
    checkRejected(store, {{"cmd", "removeChord"}});
    checkRejected(store, {{"cmd", "setProgressionLoop"}, {"on", "yes"}});
    checkRejected(store, {{"cmd", "setProgressionLoop"}});
    checkRejected(store, {{"cmd", "setFollowHost"}, {"on", 1}});

    CHECK(store.doc() == Document{});
    CHECK_FALSE(store.undo());
    CHECK_FALSE(store.redo());
}

TEST_CASE("rejection: after a valid edit, a rejected command adds no history entry and keeps the redo stack") {
    DocumentStore store;
    applyOk(store, {{"cmd", "appendChord"}, {"root", 0}, {"type", "maj"}});
    applyOk(store, {{"cmd", "appendChord"}, {"root", 5}, {"type", "maj"}});
    applyOk(store, {{"cmd", "undo"}});
    const Document before = store.doc();

    checkRejected(store, {{"cmd", "appendChord"}, {"root", 0}, {"type", "nope"}});
    checkRejected(store, {{"cmd", "setLength"}, {"id", 1}, {"lengthBeats", "nan"}});
    checkRejected(store, {{"cmd", "setKey"}, {"letter", "C"}, {"alter", 0}, {"mode", "Major"}});

    REQUIRE(store.redo());  // redo stack intact
    CHECK(store.doc().events.size() == 2);
    REQUIRE(store.undo());
    CHECK(store.doc() == before);
    REQUIRE(store.undo());  // exactly the one real edit remains
    CHECK(store.doc().events.empty());
    CHECK_FALSE(store.undo());
}

// --- State -----------------------------------------------------------------------------

TEST_CASE("state: shape") {
    const DocumentStore store = richStore();
    const json state = json::parse(stateJson(store, 1024, 768));
    CHECK(state.at("schema") == kJsonSchema);
    CHECK(state.at("projectKey") == keyJson("D", 0, "Dorian"));
    CHECK(state.at("browsedKey") == keyJson("F", 0, "Lydian"));
    CHECK(state.at("progressionLoop") == false);
    CHECK(state.at("followHost") == false);
    CHECK(state.at("nextId") == 4);
    CHECK(state.at("editor") == json({{"width", 1024}, {"height", 768}}));
    CHECK_FALSE(state.contains("palette"));
    CHECK_FALSE(state.contains("tonics"));  // the key tables are for display, not persistence
    CHECK_FALSE(state.contains("modes"));
    CHECK_FALSE(state.contains("suggestions"));
    CHECK_FALSE(state.contains("selectedEventId"));  // the selection is live state

    REQUIRE(state.at("events").size() == 2);
    CHECK(state.at("events")[0] == json({{"id", 1},
                                         {"root", 0},
                                         {"type", "maj"},
                                         {"lengthBeats", 4.0},
                                         {"velocity", 100},
                                         {"inversion", 0},
                                         {"octave", 0},
                                         {"manualVoicing", false}}));
    CHECK(state.at("events")[1] == json({{"id", 3},
                                         {"root", 7},
                                         {"type", "7"},
                                         {"lengthBeats", 1.0},
                                         {"velocity", 127},
                                         {"inversion", 2},
                                         {"octave", -1},
                                         {"manualVoicing", true}}));
    CHECK(state.at("autoVoiceLead") == true);
    REQUIRE(state.at("sections").size() == 2);
    CHECK(state.at("sections")[0] == json({{"id", 1}, {"name", "A"}, {"colourIndex", 0}, {"firstEventIndex", 0}}));
    CHECK(state.at("sections")[1] == json({{"id", 2}, {"name", "B"}, {"colourIndex", 3}, {"firstEventIndex", 1}}));
    CHECK(state.at("nextSectionId") == 3);
    CHECK(state.at("relationMode") == "voiceLeading");

    const json defaults = json::parse(stateJson(store));
    CHECK(defaults.at("editor") == json({{"width", 900}, {"height", 620}}));

    const json empty = json::parse(stateJson(DocumentStore{}));
    CHECK(empty.at("autoVoiceLead") == false);
    CHECK(empty.at("sections") == json::array());
    CHECK(empty.at("nextSectionId") == 1);
    CHECK(empty.at("relationMode") == "blend");
}

TEST_CASE("state: loadStateJson(stateJson(doc)) round-trips the document, the ids, the id counter and the editor size") {
    const DocumentStore original = richStore();
    const std::string text = stateJson(original, 1024, 768);

    DocumentStore loaded;
    loaded.appendChord(kC);  // pre-existing content, history and selection must be discarded
    loaded.setSelectedEvent(1);
    std::string error;
    int width = 0;
    int height = 0;
    REQUIRE_MESSAGE(loadStateJson(loaded, text, error, &width, &height), error);
    CHECK(error.empty());
    CHECK(loaded.doc() == original.doc());  // voicing fields, autoVoiceLead and sections included
    CHECK(loaded.doc().events[1].inversion == 2);
    CHECK(loaded.doc().events[1].octave == -1);
    CHECK(loaded.doc().events[1].manualVoicing == true);
    CHECK(loaded.doc().autoVoiceLead == true);
    CHECK(loaded.doc().sections == original.doc().sections);
    CHECK(loaded.doc().relationMode == RelationMode::VoiceLeading);
    CHECK(width == 1024);
    CHECK(height == 768);
    CHECK_FALSE(loaded.undo());  // history cleared
    CHECK_FALSE(loaded.redo());
    CHECK(loaded.selectedEventId() == kNoEventId);
    CHECK(loaded.appendChord(kF) == 4);  // the counters continue where the saved document stopped
    CHECK(loaded.nextSectionId() == 3);
    CHECK(loaded.addSection(2) == 3);

    // The out-params are optional, and `editor` is optional in the text (defaults to 900 x 620).
    DocumentStore again;
    REQUIRE(loadStateJson(again, text, error));
    CHECK(again.doc() == original.doc());

    json noEditor = json::parse(text);
    noEditor.erase("editor");
    width = height = 0;
    REQUIRE(loadStateJson(again, noEditor.dump(), error, &width, &height));
    CHECK(width == 900);
    CHECK(height == 620);
}

TEST_CASE("state: an empty document round-trips") {
    const DocumentStore empty;
    DocumentStore loaded = threeChords();
    std::string error;
    REQUIRE(loadStateJson(loaded, stateJson(empty), error));
    CHECK(loaded.doc() == Document{});
    CHECK(loaded.appendChord(kC) == 1);
}

TEST_CASE("state: a legacy state without the Phase 2 fields loads with the defaults") {
    json legacy = json::parse(stateJson(richStore()));
    for (json& e : legacy["events"]) {
        e.erase("inversion");
        e.erase("octave");
        e.erase("manualVoicing");
    }
    legacy.erase("autoVoiceLead");
    legacy.erase("sections");
    legacy.erase("nextSectionId");
    legacy.erase("relationMode");

    DocumentStore loaded;
    loaded.setRelationMode(RelationMode::Off);  // replaced by the default, not kept
    std::string error;
    REQUIRE_MESSAGE(loadStateJson(loaded, legacy.dump(), error), error);
    CHECK(loaded.doc().relationMode == RelationMode::Blend);
    REQUIRE(loaded.doc().events.size() == 2);
    for (const ChordEvent& e : loaded.doc().events) {
        CHECK(e.inversion == 0);
        CHECK(e.octave == 0);
        CHECK(e.manualVoicing == false);
    }
    CHECK(loaded.doc().autoVoiceLead == false);
    CHECK(loaded.doc().sections.empty());
    CHECK(loaded.nextSectionId() == 1);
    CHECK(loaded.addSection(0) == 1);

    // Each field is optional on its own: a partial event, and sections without a counter (derived).
    json partial = json::parse(stateJson(richStore()));
    partial["events"][1].erase("octave");
    partial.erase("nextSectionId");
    REQUIRE_MESSAGE(loadStateJson(loaded, partial.dump(), error), error);
    CHECK(loaded.doc().events[1].inversion == 2);
    CHECK(loaded.doc().events[1].octave == 0);
    CHECK(loaded.doc().events[1].manualVoicing == true);
    CHECK(loaded.nextSectionId() == 3);  // one past the largest section id

    // A counter above the derived one is kept, like nextId.
    json ahead = json::parse(stateJson(richStore()));
    ahead["nextSectionId"] = 10;
    REQUIRE_MESSAGE(loadStateJson(loaded, ahead.dump(), error), error);
    CHECK(loaded.nextSectionId() == 10);
    CHECK(loaded.doc().sections.size() == 2);
}

TEST_CASE("state: inversions, octaves and section colours are clamped on load, like the store does") {
    json state = json::parse(stateJson(richStore()));
    state["events"][0]["inversion"] = 9;
    state["events"][0]["octave"] = 5;
    state["events"][1]["inversion"] = -1;
    state["events"][1]["octave"] = -9;
    state["sections"][0]["colourIndex"] = 99;
    state["sections"][1]["colourIndex"] = -1;

    DocumentStore loaded;
    std::string error;
    REQUIRE_MESSAGE(loadStateJson(loaded, state.dump(), error), error);
    CHECK(loaded.doc().events[0].inversion == inversionCount(kC) - 1);
    CHECK(loaded.doc().events[0].octave == kMaxVoicingOctave);
    CHECK(loaded.doc().events[1].inversion == 0);
    CHECK(loaded.doc().events[1].octave == kMinVoicingOctave);
    CHECK(loaded.doc().sections[0].colourIndex == kNumSectionColours - 1);
    CHECK(loaded.doc().sections[1].colourIndex == 0);
}

TEST_CASE("state: bad Phase 2 fields reject the whole state, sections checked before anything is replaced") {
    DocumentStore store;
    store.appendChord(kC);
    store.setSelectedEvent(1);
    const Document before = store.doc();
    const json good = json::parse(stateJson(richStore()));

    const auto rejects = [&](const char* what, const std::function<void(json&)>& patch) {
        CAPTURE(what);
        json bad = good;
        patch(bad);
        std::string error;
        CHECK_FALSE(loadStateJson(store, bad.dump(), error));
        CHECK_FALSE(error.empty());
        CHECK(store.doc() == before);
        CHECK(store.nextSectionId() == 1);
        CHECK(store.selectedEventId() == 1);
    };

    rejects("inversion not an integer", [](json& s) { s["events"][1]["inversion"] = "2"; });
    rejects("inversion fractional", [](json& s) { s["events"][1]["inversion"] = 1.5; });
    rejects("octave not an integer", [](json& s) { s["events"][1]["octave"] = "down"; });
    rejects("manualVoicing not a bool", [](json& s) { s["events"][1]["manualVoicing"] = 1; });
    rejects("autoVoiceLead not a bool", [](json& s) { s["autoVoiceLead"] = "yes"; });
    rejects("sections not an array", [](json& s) { s["sections"] = 1; });
    rejects("section not an object", [](json& s) { s["sections"][0] = 1; });
    rejects("section id missing", [](json& s) { s["sections"][0].erase("id"); });
    rejects("section name missing", [](json& s) { s["sections"][0].erase("name"); });
    rejects("section colour missing", [](json& s) { s["sections"][0].erase("colourIndex"); });
    rejects("section index missing", [](json& s) { s["sections"][0].erase("firstEventIndex"); });
    rejects("section id zero", [](json& s) { s["sections"][0]["id"] = 0; });
    rejects("section id INT_MAX", [](json& s) { s["sections"][1]["id"] = INT_MAX; s["nextSectionId"] = INT_MAX; });
    rejects("section name not a string", [](json& s) { s["sections"][0]["name"] = 5; });
    rejects("section name over the bound", [](json& s) { s["sections"][0]["name"] = std::string(kMaxSectionNameBytes + 1, 'n'); });
    rejects("section name over the bound by a multi-byte character",
            [](json& s) { s["sections"][1]["name"] = std::string(kMaxSectionNameBytes - 1, 'a') + "\xC3\xA9"; });
    rejects("section colour not an integer", [](json& s) { s["sections"][0]["colourIndex"] = "red"; });
    rejects("section index not an integer", [](json& s) { s["sections"][0]["firstEventIndex"] = "0"; });
    rejects("first section not at 0", [](json& s) { s["sections"].erase(0); });
    rejects("section index past the progression", [](json& s) { s["sections"][1]["firstEventIndex"] = 2; });
    rejects("section index negative", [](json& s) { s["sections"][0]["firstEventIndex"] = -1; });
    rejects("two sections at one index", [](json& s) { s["sections"][1]["firstEventIndex"] = 0; });
    rejects("sections unsorted", [](json& s) { std::swap(s["sections"][0], s["sections"][1]); });
    rejects("duplicate section ids", [](json& s) { s["sections"][1]["id"] = 1; });
    rejects("sections on an empty progression", [](json& s) {
        s["events"] = json::array();
        s["sections"] = json::array({{{"id", 1}, {"name", "A"}, {"colourIndex", 0}, {"firstEventIndex", 0}}});
    });
    rejects("nextSectionId equal to a section id", [](json& s) { s["nextSectionId"] = 2; });
    rejects("nextSectionId zero", [](json& s) { s["nextSectionId"] = 0; });
    rejects("nextSectionId not an integer", [](json& s) { s["nextSectionId"] = "3"; });

    REQUIRE(store.undo());  // history intact
    CHECK(store.doc().events.empty());
}

TEST_CASE("state: lengths and velocities are clamped on load, like the store does") {
    json state = json::parse(stateJson(richStore()));
    state["events"][0]["lengthBeats"] = 1000;
    state["events"][0]["velocity"] = 0;
    state["events"][1]["lengthBeats"] = 0.01;
    state["events"][1]["velocity"] = 300;

    DocumentStore loaded;
    std::string error;
    REQUIRE_MESSAGE(loadStateJson(loaded, state.dump(), error), error);
    CHECK(loaded.doc().events[0].lengthBeats == kMaxLengthBeats);
    CHECK(loaded.doc().events[0].velocity == kMinVelocity);
    CHECK(loaded.doc().events[1].lengthBeats == kMinLengthBeats);
    CHECK(loaded.doc().events[1].velocity == kMaxVelocity);
}

TEST_CASE("state: a bad state is rejected whole and the store keeps its document and history") {
    DocumentStore store;
    store.appendChord(kC);
    const Document before = store.doc();
    const json good = json::parse(stateJson(richStore()));

    // Each mutation produces one invalid state; `patch` edits a copy of the good state.
    const auto rejects = [&](const char* what, const std::function<void(json&)>& patch) {
        CAPTURE(what);
        json bad = good;
        patch(bad);
        std::string error;
        CHECK_FALSE(loadStateJson(store, bad.dump(), error));
        CHECK_FALSE(error.empty());
        CHECK(store.doc() == before);
    };

    rejects("unknown type", [](json& s) { s["events"][1]["type"] = "nope"; });
    rejects("nextId equal to an id", [](json& s) { s["nextId"] = 3; });
    rejects("nextId below the ids", [](json& s) { s["nextId"] = 1; });
    rejects("nextId missing", [](json& s) { s.erase("nextId"); });
    rejects("nextId zero on an empty progression", [](json& s) { s["events"] = json::array(); s["nextId"] = 0; });
    rejects("nextId not an integer", [](json& s) { s["nextId"] = 4.5; });
    rejects("duplicate ids", [](json& s) { s["events"][1]["id"] = 1; s["nextId"] = 2; });
    rejects("id zero", [](json& s) { s["events"][0]["id"] = 0; });
    rejects("root out of range", [](json& s) { s["events"][0]["root"] = 12; });
    rejects("root not an integer", [](json& s) { s["events"][0]["root"] = "0"; });
    rejects("length not a number", [](json& s) { s["events"][0]["lengthBeats"] = "4"; });
    rejects("velocity not an integer", [](json& s) { s["events"][0]["velocity"] = 100.5; });
    rejects("event missing a field", [](json& s) { s["events"][0].erase("velocity"); });
    rejects("event not an object", [](json& s) { s["events"][0] = 1; });
    rejects("events not an array", [](json& s) { s["events"] = 1; });
    rejects("events missing", [](json& s) { s.erase("events"); });
    rejects("schema 2", [](json& s) { s["schema"] = 2; });
    rejects("schema missing", [](json& s) { s.erase("schema"); });
    rejects("project key mode unknown", [](json& s) { s["projectKey"]["mode"] = "Major"; });
    rejects("project key letter bad", [](json& s) { s["projectKey"]["tonic"]["letter"] = "H"; });
    rejects("browsed key alter bad", [](json& s) { s["browsedKey"]["tonic"]["alter"] = -2; });
    rejects("browsed key missing", [](json& s) { s.erase("browsedKey"); });
    rejects("key not an object", [](json& s) { s["projectKey"] = "C"; });
    rejects("toggle not a bool", [](json& s) { s["followHost"] = 1; });
    rejects("toggle missing", [](json& s) { s.erase("progressionLoop"); });
    rejects("relation mode unknown", [](json& s) { s["relationMode"] = "Blend"; });
    rejects("relation mode not a string", [](json& s) { s["relationMode"] = 4; });
    rejects("relation mode null", [](json& s) { s["relationMode"] = nullptr; });
    rejects("editor not an object", [](json& s) { s["editor"] = 900; });
    rejects("editor width not an integer", [](json& s) { s["editor"]["width"] = "wide"; });
    rejects("editor height missing", [](json& s) { s["editor"].erase("height"); });
    rejects("editor width not positive", [](json& s) { s["editor"]["width"] = 0; });

    std::string error;
    CHECK_FALSE(loadStateJson(store, "{not json", error));
    CHECK_FALSE(loadStateJson(store, "[]", error));
    CHECK_FALSE(loadStateJson(store, "", error));
    CHECK_FALSE(error.empty());
    CHECK(store.doc() == before);

    // A rejected load leaves the editor size alone.
    int width = -1;
    int height = -1;
    CHECK_FALSE(loadStateJson(store, "[]", error, &width, &height));
    CHECK(width == -1);
    CHECK(height == -1);

    REQUIRE(store.undo());  // history intact
    CHECK(store.doc().events.empty());
}

// --- Boundary --------------------------------------------------------------------------

namespace {

const std::string kAppendC = R"({"schema":1,"cmd":"appendChord","root":0,"type":"maj"})";

/// `text` followed by a NUL byte and garbage: what a C-string reader would silently truncate.
std::string withNulTail(std::string text) {
    text.push_back('\0');
    text += R"(,"garbage")";
    return text;
}

/// A valid append command padded with an ignored string field to exactly `total` bytes.
std::string paddedAppend(std::size_t total) {
    const std::string head = R"({"schema":1,"cmd":"appendChord","root":0,"type":"maj","pad":")";
    const std::string tail = R"("})";
    REQUIRE(total > head.size() + tail.size());
    return head + std::string(total - head.size() - tail.size(), 'x') + tail;
}

/// A valid state padded with an ignored string field to exactly `total` bytes.
std::string paddedState(std::size_t total) {
    std::string text = stateJson(richStore());
    text.pop_back();  // the closing brace
    const std::string open = R"(,"pad":")";
    const std::string close = R"("})";
    REQUIRE(total > text.size() + open.size() + close.size());
    return text + open + std::string(total - text.size() - open.size() - close.size(), 'x') + close;
}

/// An undo command whose ignored field `x` nests `arrays` arrays: `arrays + 1` levels in all.
std::string nestedUndo(int arrays) {
    return R"({"schema":1,"cmd":"undo","x":)" + std::string(static_cast<std::size_t>(arrays), '[') +
           std::string(static_cast<std::size_t>(arrays), ']') + "}";
}

/// Checks that the state text is rejected and that the store, its history and its counter are untouched.
void checkStateRejected(DocumentStore& store, const std::string& text) {
    const Document before = store.doc();
    const int nextId = store.nextId();
    std::string error;
    CHECK_FALSE(loadStateJson(store, text, error));
    CHECK_FALSE(error.empty());
    CHECK(store.doc() == before);
    CHECK(store.nextId() == nextId);
}

}  // namespace

TEST_CASE("boundary: a raw NUL byte anywhere rejects the message (the lexer would stop there)") {
    DocumentStore store;
    checkRejectedRaw(store, withNulTail(kAppendC));
    checkRejectedRaw(store, std::string(1, '\0') + kAppendC);
    std::string inside = kAppendC;
    inside.insert(1, 1, '\0');
    checkRejectedRaw(store, inside);
    CHECK(store.doc().events.empty());
    CHECK(store.nextId() == 1);
    CHECK_FALSE(store.undo());

    checkStateRejected(store, withNulTail(stateJson(richStore())));
    CHECK_FALSE(store.undo());
}

TEST_CASE("boundary: an escaped \\u0000 is an ordinary character: it never matches a name and is ignored elsewhere") {
    DocumentStore store;
    checkRejectedRaw(store, R"({"schema":1,"cmd":"undo\u0000"})");
    checkRejectedRaw(store, R"({"schema":1,"cmd":"appendChord","root":0,"type":"maj\u0000"})");
    checkRejectedRaw(store, R"({"schema":1,"cmd":"setKey","letter":"C\u0000","alter":0,"mode":"Ionian"})");
    checkRejectedRaw(store, R"({"schema":1,"cmd":"browseKey","mode":"\u0000Dorian"})");
    std::string error;
    CHECK(applyCommandJson(store, R"({"schema":1,"cmd":"undo","note":"\u0000"})", error));
    CHECK(store.doc() == Document{});
}

TEST_CASE("boundary: a member given twice takes its last value") {
    DocumentStore store;
    std::string error;
    REQUIRE(applyCommandJson(store, R"({"schema":1,"cmd":"appendChord","root":0,"type":"maj","root":7})", error));
    CHECK(snapshot(store).at("events")[0].at("symbol") == "G");
    checkRejectedRaw(store, R"({"schema":1,"schema":2,"cmd":"undo"})");
    CHECK(store.doc().events.size() == 1);
    CHECK(applyCommandJson(store, R"({"schema":2,"schema":1,"cmd":"undo"})", error));
    CHECK(store.doc().events.empty());
}

TEST_CASE("boundary: messages longer than kMaxJsonBytes are rejected, one of exactly kMaxJsonBytes accepted") {
    DocumentStore store;
    checkRejectedRaw(store, paddedAppend(kMaxJsonBytes + 1));
    checkRejectedRaw(store, paddedAppend(kMaxJsonBytes * 4));
    CHECK(store.nextId() == 1);
    CHECK_FALSE(store.undo());

    std::string error;
    REQUIRE_MESSAGE(applyCommandJson(store, paddedAppend(kMaxJsonBytes), error), error);
    CHECK(store.doc().events.size() == 1);
    CHECK(store.nextId() == 2);

    checkStateRejected(store, paddedState(kMaxJsonBytes + 1));
    REQUIRE_MESSAGE(loadStateJson(store, paddedState(kMaxJsonBytes), error), error);
    CHECK(store.doc() == richStore().doc());
}

TEST_CASE("boundary: nesting deeper than kMaxJsonDepth is rejected before parsing") {
    DocumentStore store;
    std::string error;
    CHECK(applyCommandJson(store, nestedUndo(kMaxJsonDepth - 1), error));  // the top-level object is level 1
    checkRejectedRaw(store, nestedUndo(kMaxJsonDepth));
    checkRejectedRaw(store, nestedUndo(100000));

    // Objects count too: each "[{"a":" adds two levels.
    const auto mixed = [](int pairs) {
        std::string text = R"({"schema":1,"cmd":"undo","x":)";
        for (int i = 0; i < pairs; ++i) {
            text += R"([{"a":)";
        }
        text += "1";
        for (int i = 0; i < pairs; ++i) {
            text += "}]";
        }
        return text + "}";
    };
    CHECK(applyCommandJson(store, mixed((kMaxJsonDepth - 1) / 2), error));  // 63 levels
    checkRejectedRaw(store, mixed(kMaxJsonDepth / 2));                      // 65 levels

    // Brackets inside string literals are text, escaped quotes included.
    CHECK(applyCommandJson(store, R"({"schema":1,"cmd":"undo","x":"\"[[{{)" + std::string(500, '[') + R"(\\"})", error));
    CHECK(applyCommandJson(store, R"({"schema":1,"cmd":"undo","x":"\\","y":")" + std::string(500, '{') + R"("})", error));

    // Unbalanced brackets outside strings are the parser's business and still fail.
    checkRejectedRaw(store, R"({"schema":1,"cmd":"undo"}]]]])");
    checkRejectedRaw(store, R"([[[{"schema":1,"cmd":"undo"})");
    CHECK(store.doc() == Document{});

    std::string state = stateJson(richStore());
    state.pop_back();
    checkStateRejected(store, state + R"(,"pad":)" + std::string(100, '[') + std::string(100, ']') + "}");
}

TEST_CASE("boundary: a rejected append leaves the id counter alone") {
    DocumentStore store;
    checkRejected(store, {{"cmd", "appendChord"}, {"root", 0}, {"type", "maj"}, {"lengthBeats", "nan"}});
    checkRejected(store, {{"cmd", "appendChord"}, {"root", 0}, {"type", "nope"}});
    CHECK(store.nextId() == 1);
    applyOk(store, {{"cmd", "appendChord"}, {"root", 0}, {"type", "maj"}});
    CHECK(store.nextId() == 2);
}

TEST_CASE("boundary: an append refused by an exhausted id counter is reported, and changes nothing") {
    DocumentStore store(INT_MAX);
    const Document before = store.doc();
    std::string error;
    CHECK_FALSE(applyCommandJson(store, kAppendC, error));
    CHECK_FALSE(error.empty());
    CHECK(store.doc() == before);
    CHECK(store.nextId() == INT_MAX);
    CHECK_FALSE(store.undo());
}

// --- Capacity --------------------------------------------------------------------------

namespace {

/// A store holding kMaxChords C chords, ids 1..kMaxChords.
DocumentStore fullStore() {
    DocumentStore store;
    for (int i = 0; i < kMaxChords; ++i) {
        REQUIRE(store.appendChord(kC) == i + 1);
    }
    return store;
}

/// A state holding `count` C chords, ids 1..count, with a matching nextId.
std::string stateWithChords(int count) {
    json state = json::parse(stateJson(DocumentStore{}));
    json events = json::array();
    for (int i = 1; i <= count; ++i) {
        events.push_back({{"id", i}, {"root", 0}, {"type", "maj"}, {"lengthBeats", 4.0}, {"velocity", 100}});
    }
    state["events"] = std::move(events);
    state["nextId"] = count + 1;
    return state.dump();
}

}  // namespace

TEST_CASE("capacity: appendChord on a full document is refused with a clear error and no history entry") {
    DocumentStore store = fullStore();
    const Document before = store.doc();

    std::string error;
    CHECK_FALSE(apply(store, {{"cmd", "appendChord"}, {"root", 5}, {"type", "maj"}}, error));
    CAPTURE(error);
    CHECK(error.find(std::to_string(kMaxChords)) != std::string::npos);
    CHECK(error.find("appendChord") != std::string::npos);
    CHECK(store.doc() == before);
    CHECK(store.nextId() == kMaxChords + 1);

    REQUIRE(store.undo());  // the most recent real append, not a phantom entry
    CHECK(store.doc().events.size() == static_cast<std::size_t>(kMaxChords) - 1);
    applyOk(store, {{"cmd", "appendChord"}, {"root", 5}, {"type", "maj"}});  // room again
    CHECK(store.doc().events.size() == static_cast<std::size_t>(kMaxChords));
}

TEST_CASE("capacity: a state with more than kMaxChords chords is rejected whole; exactly kMaxChords loads") {
    DocumentStore store = threeChords();
    checkStateRejected(store, stateWithChords(kMaxChords + 1));
    checkStateRejected(store, stateWithChords(kMaxChords + 100));
    REQUIRE(store.undo());  // history intact
    CHECK(store.doc().events.size() == 2);

    std::string error;
    REQUIRE_MESSAGE(loadStateJson(store, stateWithChords(kMaxChords), error), error);
    CHECK(store.doc().events.size() == static_cast<std::size_t>(kMaxChords));
    CHECK(store.nextId() == kMaxChords + 1);
    CHECK_FALSE(apply(store, {{"cmd", "appendChord"}, {"root", 0}, {"type", "maj"}}, error));
    CHECK_FALSE(error.empty());
}

namespace {

/// The largest state the engine can produce: kMaxChords chords, a section on every one of them, each
/// name at the byte bound and made of the characters JSON escapes to two bytes, and the relation mode with
/// the longest name.
DocumentStore worstCaseStore() {
    DocumentStore store = fullStore();
    store.setRelationMode(RelationMode::VoiceLeading);
    std::string name;
    for (int i = 0; i < kMaxSectionNameBytes; ++i) {
        name.push_back(i % 2 == 0 ? '"' : '\\');
    }
    for (int i = 0; i < kMaxChords; ++i) {
        REQUIRE(store.addSection(i, name) == i + 1);
    }
    return store;
}

/// `stateJson` of `worstCaseStore()` at the default editor size, measured; pinned so that a change to the
/// state's shape shows up here against `kMaxJsonBytes` (1 MiB) before it shows up as a state that will
/// not load.
constexpr std::size_t kWorstCaseStateBytes = 76800;

}  // namespace

TEST_CASE("capacity: the largest possible state is well under kMaxJsonBytes and round-trips") {
    const DocumentStore original = worstCaseStore();
    REQUIRE(original.doc().events.size() == static_cast<std::size_t>(kMaxChords));
    REQUIRE(original.doc().sections.size() == static_cast<std::size_t>(kMaxChords));
    const std::string text = stateJson(original);
    MESSAGE("worst-case state: " << text.size() << " bytes");
    CHECK(text.size() == kWorstCaseStateBytes);
    CHECK(text.size() <= kMaxJsonBytes / 8);

    DocumentStore loaded;
    std::string error;
    REQUIRE_MESSAGE(loadStateJson(loaded, text, error), error);
    CHECK(loaded.doc() == original.doc());
    CHECK(loaded.nextId() == kMaxChords + 1);
    CHECK(loaded.nextSectionId() == kMaxChords + 1);
    CHECK(loaded.doc().sections.back().name.size() == static_cast<std::size_t>(kMaxSectionNameBytes));
}

TEST_CASE("keyName: the spelled tonic and the mode name, as the snapshot's key names") {
    CHECK(keyName(key('C', 0, Mode::Ionian)) == "C Ionian");
    CHECK(keyName(key('B', -1, Mode::HarmonicMinor)) == "Bb HarmonicMinor");
    CHECK(keyName(key('F', 1, Mode::Dorian)) == "F# Dorian");
}

// --- Presets ---------------------------------------------------------------------------

TEST_CASE("loadPreset append: the preset resolved in the project key, after the last chord, one undo step") {
    DocumentStore store = threeChords();
    store.setProjectKey(key('G', 0, Mode::Ionian));
    store.setSelectedEvent(2);
    const Document before = store.doc();
    applyOk(store, {{"cmd", "loadPreset"}, {"id", "axis"}, {"mode", "append"}});
    const json snap = snapshot(store);
    CHECK(snapshotIds(snap) == std::vector<int>{1, 2, 3, 4, 5, 6, 7});
    std::vector<std::string> symbols;
    for (const json& e : snap.at("events")) {
        symbols.push_back(e.at("symbol").get<std::string>());
    }
    CHECK(symbols == std::vector<std::string>{"C", "F", "G7", "G", "D", "Em", "C"});
    CHECK(snap.at("events").at(3).at("roman") == "I");
    CHECK(snap.at("events").at(5).at("velocity") == 100);
    CHECK(snap.at("endBeat") == 28.0);
    REQUIRE(snap.at("sections").size() == 2);
    CHECK(snap.at("sections").at(0) ==
          json({{"id", 1}, {"name", "Section 1"}, {"colourIndex", 0}, {"firstEventIndex", 0}}));
    CHECK(snap.at("sections").at(1) == json({{"id", 2}, {"name", "Axis"}, {"colourIndex", 1}, {"firstEventIndex", 3}}));
    CHECK(store.selectedEventId() == 2);  // its chord is still there
    applyOk(store, {{"cmd", "undo"}});
    CHECK(store.doc() == before);
}

TEST_CASE("loadPreset replace: chords and sections replaced, keys and live state kept, selection cleared") {
    DocumentStore store = richStore();  // D Dorian, sections 1 and 2
    store.setSelectedEvent(3);
    const Document before = store.doc();
    applyOk(store, {{"cmd", "loadPreset"}, {"id", "andalusian"}, {"mode", "replace"}});
    const Document& d = store.doc();
    REQUIRE(d.events.size() == 4);
    CHECK(d.events[0].chord == chord(2, "min"));   // i in D
    CHECK(d.events[1].chord == chord(0, "maj"));   // bVII
    CHECK(d.events[2].chord == chord(10, "maj"));  // bVI
    CHECK(d.events[3].chord == chord(9, "7"));     // V7
    CHECK(d.events[0].id == 4);
    REQUIRE(d.sections.size() == 1);
    CHECK(d.sections[0] == Section{3, "Andalusian", 2, 0});
    CHECK(store.selectedEventId() == kNoEventId);
    CHECK(d.projectKey == before.projectKey);
    CHECK(d.browsedKey == before.browsedKey);
    CHECK(d.relationMode == before.relationMode);
    CHECK(d.autoVoiceLead == before.autoVoiceLead);
    applyOk(store, {{"cmd", "undo"}});
    CHECK(store.doc() == before);
}

TEST_CASE("loadPreset: an unknown, missing or mistyped id is rejected, naming it") {
    DocumentStore store = threeChords();
    for (const json& cmd : {json{{"cmd", "loadPreset"}, {"id", "nope"}, {"mode", "append"}},
                            json{{"cmd", "loadPreset"}, {"id", "Axis"}, {"mode", "append"}},
                            json{{"cmd", "loadPreset"}, {"id", ""}, {"mode", "replace"}}}) {
        checkRejected(store, cmd);
        std::string error;
        CHECK_FALSE(apply(store, cmd, error));
        CHECK_MESSAGE(error.find("\"" + cmd.at("id").get<std::string>() + "\" is not a preset") != std::string::npos,
                      error);
        CHECK(error.find("\"id\"") != std::string::npos);
    }
    checkRejected(store, {{"cmd", "loadPreset"}, {"mode", "append"}});
    checkRejected(store, {{"cmd", "loadPreset"}, {"id", 3}, {"mode", "append"}});
    checkRejected(store, {{"cmd", "loadPreset"}, {"id", nullptr}, {"mode", "append"}});
    CHECK(store.doc().events.size() == 3);
    CHECK(store.undo());  // the appends' history is all there is
}

TEST_CASE("loadPreset: a missing, mistyped or unknown mode is rejected, listing append and replace") {
    DocumentStore store = threeChords();
    for (const json& cmd : {json{{"cmd", "loadPreset"}, {"id", "axis"}},
                            json{{"cmd", "loadPreset"}, {"id", "axis"}, {"mode", "Append"}},
                            json{{"cmd", "loadPreset"}, {"id", "axis"}, {"mode", "insert"}},
                            json{{"cmd", "loadPreset"}, {"id", "axis"}, {"mode", 1}},
                            json{{"cmd", "loadPreset"}, {"id", "nope"}, {"mode", "insert"}}}) {
        CAPTURE(cmd.dump());
        checkRejected(store, cmd);
        std::string error;
        CHECK_FALSE(apply(store, cmd, error));
        if (cmd.at("id") == "axis") {
            CHECK_MESSAGE(error.find("\"append\"") != std::string::npos, error);
            CHECK_MESSAGE(error.find("\"replace\"") != std::string::npos, error);
            CHECK(error.find("\"mode\"") != std::string::npos);
        } else {
            // The id is validated before the mode, so an unknown id with a bad mode names the id.
            CHECK_MESSAGE(error.find("\"nope\"") != std::string::npos, error);
            CHECK_MESSAGE(error.find("\"append\"") == std::string::npos, error);
        }
    }
}

TEST_CASE("loadPreset: refusals by the store are reported with its reason and change nothing") {
    SUBCASE("capacity") {
        DocumentStore store;
        for (int i = 0; i < kMaxChords - 3; ++i) {
            REQUIRE(store.appendChord(kC) != kNoEventId);
        }
        const Document before = store.doc();
        const int nextId = store.nextId();
        std::string error;
        CHECK_FALSE(apply(store, {{"cmd", "loadPreset"}, {"id", "axis"}, {"mode", "append"}}, error));
        CHECK_MESSAGE(error.find("\"loadPreset\" refused") != std::string::npos, error);
        CHECK_MESSAGE(error.find(std::to_string(kMaxChords)) != std::string::npos, error);
        CHECK(store.doc() == before);
        CHECK(store.nextId() == nextId);
        applyOk(store, {{"cmd", "loadPreset"}, {"id", "axis"}, {"mode", "replace"}});  // 4 alone fit
        CHECK(store.doc().events.size() == 4);
    }
    SUBCASE("event ids") {
        DocumentStore store(INT_MAX - 3);
        std::string error;
        CHECK_FALSE(apply(store, {{"cmd", "loadPreset"}, {"id", "axis"}, {"mode", "replace"}}, error));
        CHECK_MESSAGE(error.find("\"loadPreset\" refused") != std::string::npos, error);
        CHECK_MESSAGE(error.find("event ids") != std::string::npos, error);
        CHECK(store.doc().events.empty());
        CHECK_FALSE(store.undo());
    }
    SUBCASE("section ids") {
        DocumentStore store;
        store.replace(Document{}, 1, INT_MAX);
        std::string error;
        CHECK_FALSE(apply(store, {{"cmd", "loadPreset"}, {"id", "axis"}, {"mode", "append"}}, error));
        CHECK_MESSAGE(error.find("\"loadPreset\" refused") != std::string::npos, error);
        CHECK_MESSAGE(error.find("section ids") != std::string::npos, error);
        CHECK(store.doc().events.empty());
        CHECK(store.nextId() == 1);
        CHECK_FALSE(store.undo());
    }
}

TEST_CASE("snapshot presets: every record in table order, constant but for fits") {
    DocumentStore store;
    const json snap = snapshot(store);
    const json& presets = snap.at("presets");
    REQUIRE(presets.is_array());
    REQUIRE(presets.size() == allPresets().size());
    CHECK(presets.at(0) == json({{"id", "axis"},
                                 {"name", "Axis"},
                                 {"genre", "Pop"},
                                 {"tonality", "Major"},
                                 {"numerals", "I \xE2\x80\x93 V \xE2\x80\x93 vi \xE2\x80\x93 IV"},
                                 {"fits", true}}));
    for (std::size_t i = 0; i < presets.size(); ++i) {
        const Preset& p = allPresets()[i];
        const json& entry = presets.at(i);
        CAPTURE(std::string(p.id));
        CHECK(entry.size() == 6);
        CHECK(entry.at("id") == p.id);
        CHECK(entry.at("name") == p.name);
        CHECK(entry.at("genre") == p.genre);
        CHECK(entry.at("tonality") == tonalityName(p.tonality));
        CHECK(entry.at("numerals") == presetNumerals(p));
        CHECK(entry.at("fits") == presetFits(p, store.doc().projectKey));
    }
}

TEST_CASE("snapshot presets: fits follows the project key, not the browsed key") {
    DocumentStore store;
    const auto fits = [&store](const char* id) {
        const json snap = snapshot(store);  // kept alive: the loop reads into it
        for (const json& entry : snap.at("presets")) {
            if (entry.at("id").get<std::string>() == id) {
                return entry.at("fits").get<bool>();
            }
        }
        FAIL("no preset " << id);
        return false;
    };
    CHECK(fits("axis"));
    CHECK_FALSE(fits("edm-minor"));
    CHECK(fits("blues-12"));
    applyOk(store, {{"cmd", "browseKey"}, {"mode", "Aeolian"}});  // browsing changes nothing
    CHECK(fits("axis"));
    applyOk(store, {{"cmd", "setKey"}, {"letter", "A"}, {"alter", 0}, {"mode", "Aeolian"}});
    CHECK_FALSE(fits("axis"));
    CHECK(fits("edm-minor"));
    CHECK(fits("blues-12"));
    applyOk(store, {{"cmd", "setKey"}, {"letter", "G"}, {"alter", 0}, {"mode", "Mixolydian"}});
    CHECK(fits("axis"));
    CHECK_FALSE(fits("lament"));
    applyOk(store, {{"cmd", "setKey"}, {"letter", "E"}, {"alter", 0}, {"mode", "HarmonicMinor"}});
    CHECK(fits("lament"));
}

// --- Applied chords ------------------------------------------------------------------------

namespace {

/// One member of every `applied` entry, in order.
template <typename T>
std::vector<T> appliedField(const json& snap, const char* member) {
    std::vector<T> out;
    for (const json& entry : snap.at("applied")) {
        out.push_back(entry.at(member).get<T>());
    }
    return out;
}

/// The members every palette cell carries without relations.
const std::vector<std::string> kCellMembers{"degree", "tier",   "root",    "type",    "symbol",  "roman",
                                            "family", "function", "tension", "filled", "voicing", "outOfKey"};

/// Checks what every `applied` entry carries beyond the expectations of each test: the members of a
/// palette cell plus `id`, `label`, `target`; each one the cell `cellJson` writes for the chord against the
/// project key; and the label, column and spelling of the matching `appliedChords(browsedKey)` entry.
void checkAppliedEntries(const json& snap, const Document& doc) {
    const std::vector<AppliedChord> applied = appliedChords(doc.browsedKey);
    const json& entries = snap.at("applied");
    REQUIRE(entries.is_array());
    REQUIRE(entries.size() == applied.size());
    for (std::size_t i = 0; i < applied.size(); ++i) {
        const AppliedChord& a = applied[i];
        const json& e = entries[i];
        CAPTURE(e.dump());
        for (const std::string& member : kCellMembers) {
            CHECK(e.contains(member));
        }
        const bool dominant = std::string_view(a.chord.type->id) == "7";
        CHECK(e.size() == kCellMembers.size() + 3 + (e.contains("relation") ? 2 : 0));
        CHECK(e.at("degree") == a.targetDegree);
        CHECK(e.at("tier") == (dominant ? 1 : 0));
        CHECK(e.at("id") == "applied-" + std::to_string(a.targetDegree) + "-" + (dominant ? "1" : "0"));
        CHECK(e.at("label") == a.label);
        const Chord target = diatonicTriad(doc.browsedKey, a.targetDegree);
        CHECK(e.at("target") == chordSymbol(target, doc.browsedKey));
        CHECK(e.at("root") == a.chord.root);
        CHECK(e.at("type") == a.chord.type->id);
        CHECK(e.at("symbol") == chordSymbol(a.chord, a.spellingKey));
        CHECK(e.at("roman") == romanNumeral(a.chord, doc.projectKey));
        CHECK(e.at("function") == functionShortName(functionOf(a.chord, doc.projectKey)));
        CHECK(e.at("tension") == tension(a.chord, doc.projectKey));
        CHECK(e.at("filled") == fullyInKey(a.chord, doc.projectKey));
        CHECK(e.at("voicing") == json(closeVoicing(a.chord)));
        CHECK(e.at("outOfKey").empty() == fullyInKey(a.chord, doc.projectKey));
        CHECK(e.at("family") == (dominant ? "Major" : "Other"));
    }
}

}  // namespace

TEST_CASE("snapshot applied: C Ionian has V7 and vii°7 of ii, iii, IV, V and vi, by column and row") {
    DocumentStore store;
    const json snap = snapshot(store);
    REQUIRE(snap.contains("applied"));
    checkAppliedEntries(snap, store.doc());
    CHECK(appliedField<std::string>(snap, "symbol") ==
          std::vector<std::string>{"A7", "C#dim7", "B7", "D#dim7", "C7", "Edim7", "D7", "F#dim7", "E7", "G#dim7"});
    CHECK(appliedField<std::string>(snap, "label") ==
          std::vector<std::string>{"V7/ii", "vii°7/ii", "V7/iii", "vii°7/iii", "V7/IV", "vii°7/IV", "V7/V",
                                   "vii°7/V", "V7/vi", "vii°7/vi"});
    CHECK(appliedField<std::string>(snap, "target") ==
          std::vector<std::string>{"Dm", "Dm", "Em", "Em", "F", "F", "G", "G", "Am", "Am"});
    CHECK(appliedField<int>(snap, "degree") == std::vector<int>{1, 1, 2, 2, 3, 3, 4, 4, 5, 5});
    CHECK(appliedField<int>(snap, "tier") == std::vector<int>{1, 0, 1, 0, 1, 0, 1, 0, 1, 0});
    CHECK(appliedField<std::string>(snap, "id") ==
          std::vector<std::string>{"applied-1-1", "applied-1-0", "applied-2-1", "applied-2-0", "applied-3-1",
                                   "applied-3-0", "applied-4-1", "applied-4-0", "applied-5-1", "applied-5-0"});
    CHECK(appliedField<bool>(snap, "filled") == std::vector<bool>(10, false));
    CHECK(appliedField<std::string>(snap, "function") == std::vector<std::string>(10, "D"));
    // The full first entry, so the wire shape is pinned once.
    CHECK(snap.at("applied")[0] == json({{"id", "applied-1-1"},
                                        {"label", "V7/ii"},
                                        {"target", "Dm"},
                                        {"degree", 1},
                                        {"tier", 1},
                                        {"root", 9},
                                        {"type", "7"},
                                        {"symbol", "A7"},
                                        {"roman", romanNumeral(chord(9, "7"), store.doc().projectKey)},
                                        {"family", "Major"},
                                        {"function", "D"},
                                        {"tension", tension(chord(9, "7"), store.doc().projectKey)},
                                        {"filled", false},
                                        {"voicing", closeVoicing(chord(9, "7"))},
                                        {"outOfKey", {1}}}));
    CHECK(snap.at("schema") == 1);  // additive
}

TEST_CASE("snapshot applied: A Aeolian tonicises bIII, iv, v, bVI and bVII; G7 is in the key") {
    DocumentStore store;
    store.setProjectKey(key('A', 0, Mode::Aeolian));
    store.setBrowsedKey(key('A', 0, Mode::Aeolian));
    const json snap = snapshot(store);
    checkAppliedEntries(snap, store.doc());
    CHECK(appliedField<std::string>(snap, "symbol") ==
          std::vector<std::string>{"G7", "Bdim7", "A7", "C#dim7", "B7", "D#dim7", "C7", "Edim7", "D7", "F#dim7"});
    CHECK(appliedField<std::string>(snap, "label") ==
          std::vector<std::string>{"V7/bIII", "vii°7/bIII", "V7/iv", "vii°7/iv", "V7/v", "vii°7/v", "V7/bVI",
                                   "vii°7/bVI", "V7/bVII", "vii°7/bVII"});
    CHECK(appliedField<std::string>(snap, "target") ==
          std::vector<std::string>{"C", "C", "Dm", "Dm", "Em", "Em", "F", "F", "G", "G"});
    CHECK(appliedField<int>(snap, "degree") == std::vector<int>{2, 2, 3, 3, 4, 4, 5, 5, 6, 6});
    std::vector<bool> filled(10, false);
    filled[0] = true;  // G7 = G B D F, the diatonic VII7
    CHECK(appliedField<bool>(snap, "filled") == filled);
}

TEST_CASE("snapshot applied: harmonic and melodic minor tonicise no augmented triad") {
    DocumentStore store;
    store.setProjectKey(key('A', 0, Mode::HarmonicMinor));
    store.setBrowsedKey(key('A', 0, Mode::HarmonicMinor));
    json snap = snapshot(store);
    checkAppliedEntries(snap, store.doc());
    CHECK(appliedField<std::string>(snap, "label") ==
          std::vector<std::string>{"V7/iv", "vii°7/iv", "V7/V", "vii°7/V", "V7/bVI", "vii°7/bVI"});
    CHECK(appliedField<std::string>(snap, "symbol") ==
          std::vector<std::string>{"A7", "C#dim7", "B7", "D#dim7", "C7", "Edim7"});
    CHECK(appliedField<std::string>(snap, "target") == std::vector<std::string>{"Dm", "Dm", "E", "E", "F", "F"});
    CHECK(appliedField<int>(snap, "degree") == std::vector<int>{3, 3, 4, 4, 5, 5});

    store.setProjectKey(key('A', 0, Mode::MelodicMinor));
    store.setBrowsedKey(key('A', 0, Mode::MelodicMinor));
    snap = snapshot(store);
    checkAppliedEntries(snap, store.doc());
    CHECK(appliedField<std::string>(snap, "label") ==
          std::vector<std::string>{"V7/ii", "vii°7/ii", "V7/IV", "vii°7/IV", "V7/V", "vii°7/V"});
    CHECK(appliedField<std::string>(snap, "symbol") ==
          std::vector<std::string>{"F#7", "A#dim7", "A7", "C#dim7", "B7", "D#dim7"});
    CHECK(appliedField<std::string>(snap, "target") == std::vector<std::string>{"Bm", "Bm", "D", "D", "E", "E"});
    CHECK(appliedField<int>(snap, "degree") == std::vector<int>{1, 1, 3, 3, 4, 4});
    for (const std::string& label : appliedField<std::string>(snap, "label")) {
        CHECK(label.find('+') == std::string::npos);
    }
}

TEST_CASE("snapshot applied: Locrian places each chord by its target's degree, not by its label's numeral") {
    DocumentStore store;
    store.setProjectKey(key('C', 0, Mode::Locrian));
    store.setBrowsedKey(key('C', 0, Mode::Locrian));
    const json snap = snapshot(store);
    checkAppliedEntries(snap, store.doc());
    CHECK(appliedField<int>(snap, "degree") == std::vector<int>{1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6});
    // Gb, the fifth degree, reads as #IV against the parallel major; the cell still sits in column 4.
    const json& v7OfV = snap.at("applied")[6];
    CHECK(v7OfV.at("label") == "V7/#IV");
    CHECK(v7OfV.at("degree") == 4);
    CHECK(v7OfV.at("tier") == 1);
    CHECK(v7OfV.at("target") == "Gb");
    CHECK(v7OfV.at("symbol") == "Db7");
    // Every column but the tonic's is filled, two rows each.
    for (int degree = 1; degree < 7; ++degree) {
        int count = 0;
        for (const json& e : snap.at("applied")) {
            count += e.at("degree") == degree ? 1 : 0;
        }
        CHECK(count == 2);
    }
}

TEST_CASE("snapshot applied: follows the browsed key; numerals, filled and outOfKey stay the project key's") {
    DocumentStore store;
    store.setBrowsedKey(key('G', 0, Mode::Ionian));
    json snap = snapshot(store);
    checkAppliedEntries(snap, store.doc());
    CHECK(appliedField<std::string>(snap, "symbol") ==
          std::vector<std::string>{"E7", "G#dim7", "F#7", "A#dim7", "G7", "Bdim7", "A7", "C#dim7", "B7", "D#dim7"});
    CHECK(appliedField<std::string>(snap, "label") ==
          std::vector<std::string>{"V7/ii", "vii°7/ii", "V7/iii", "vii°7/iii", "V7/IV", "vii°7/IV", "V7/V",
                                   "vii°7/V", "V7/vi", "vii°7/vi"});
    CHECK(appliedField<std::string>(snap, "target") ==
          std::vector<std::string>{"Am", "Am", "Bm", "Bm", "C", "C", "D", "D", "Em", "Em"});
    // G7, V7/IV of G, is the project key's own V7: filled, numbered V7, nothing out of key.
    const json& g7 = snap.at("applied")[4];
    CHECK(g7.at("symbol") == "G7");
    CHECK(g7.at("filled") == true);
    CHECK(g7.at("roman") == "V7");
    CHECK(g7.at("outOfKey").empty());

    // Browsing home again gives C's list back.
    applyOk(store, {{"cmd", "browseKey"}, {"home", true}});
    snap = snapshot(store);
    CHECK(appliedField<std::string>(snap, "symbol").front() == "A7");

    // Browsing a key with flats spells its applied chords there.
    store.setBrowsedKey(key('F', 0, Mode::Ionian));
    snap = snapshot(store);
    checkAppliedEntries(snap, store.doc());
    CHECK(appliedField<std::string>(snap, "symbol") ==
          std::vector<std::string>{"D7", "F#dim7", "E7", "G#dim7", "F7", "Adim7", "G7", "Bdim7", "A7", "C#dim7"});
    CHECK(appliedField<std::string>(snap, "target") ==
          std::vector<std::string>{"Gm", "Gm", "Am", "Am", "Bb", "Bb", "C", "C", "Dm", "Dm"});
}

TEST_CASE("snapshot applied: relation fields exactly when there is a reference and the mode is not off") {
    DocumentStore store;
    const auto noAppliedRelations = [](const json& snap) {
        for (const json& e : snap.at("applied")) {
            if (e.contains("relation") || e.contains("relationText")) {
                return false;
            }
        }
        return true;
    };
    // No chords: no reference.
    CHECK(noAppliedRelations(selectedSnapshot(store)));
    store = threeChords();
    store.setBrowsedKey(key('G', 0, Mode::Ionian));
    for (const char* mode : {"commonTones", "voiceLeading", "function", "blend"}) {
        CAPTURE(mode);
        applyOk(store, {{"cmd", "setRelationMode"}, {"mode", mode}});
        const json snap = selectedSnapshot(store);
        checkAppliedEntries(snap, store.doc());
        for (const json& e : snap.at("applied")) {
            CHECK(e.at("relation").is_number_integer());
            CHECK(e.at("relationText").is_string());
        }
    }
    applyOk(store, {{"cmd", "setRelationMode"}, {"mode", "off"}});
    const json off = selectedSnapshot(store);
    CHECK(off.at("applied").size() == 10);
    CHECK(noAppliedRelations(off));
}

TEST_CASE("snapshot applied: sized on the palette and home row's scale, which the applied chords leave unchanged") {
    for (const char* mode : {"commonTones", "voiceLeading", "function", "blend"}) {
        for (const Key& browsed : {key('C', 0, Mode::Ionian), key('E', 0, Mode::Ionian), key('D', 0, Mode::Dorian)}) {
            for (int selected : {1, 2, 3}) {
                CAPTURE(mode);
                CAPTURE(keyName(browsed));
                CAPTURE(selected);
                DocumentStore store = threeChords();  // C (1), F (2), G7 (3)
                store.setBrowsedKey(browsed);
                store.setSelectedEvent(selected);
                applyOk(store, {{"cmd", "setRelationMode"}, {"mode", mode}});
                const json snap = selectedSnapshot(store);
                const Document& doc = store.doc();
                const Chord reference = doc.events[static_cast<std::size_t>(selected - 1)].chord;

                // Palette and home row: exactly the relatedness of their own population, as before.
                const std::vector<json> cells = sizedCells(snap);
                const std::vector<Relatedness> population = expectedRelations(snap, doc, reference);
                REQUIRE(cells.size() == population.size());
                for (std::size_t i = 0; i < cells.size(); ++i) {
                    CHECK(cells[i].at("relation") == population[i].score);
                    CHECK(cells[i].at("relationText") == population[i].text);
                }

                // Applied chords: the extras of the same call.
                std::vector<Chord> sized;
                for (const json& c : cells) {
                    sized.push_back(chord(c.at("root").get<int>(), c.at("type").get<std::string>()));
                }
                std::vector<Chord> extras;
                for (const AppliedChord& a : appliedChords(doc.browsedKey)) {
                    extras.push_back(a.chord);
                }
                const std::vector<Relatedness> all =
                    relatedness(reference, sized, extras, doc.projectKey, doc.browsedKey, doc.relationMode);
                const json& applied = snap.at("applied");
                REQUIRE(applied.size() == extras.size());
                for (std::size_t i = 0; i < extras.size(); ++i) {
                    const json& e = applied[i];
                    CHECK(e.at("relation") == all[sized.size() + i].score);
                    CHECK(e.at("relationText") == all[sized.size() + i].text);
                    CHECK(e.at("relation").get<int>() >= 0);
                    CHECK(e.at("relation").get<int>() <= 100);
                }
            }
        }
    }
}

TEST_CASE("snapshot applied: blend scores are clamped to 0..100 against the population's range") {
    // After G7 in C, the applied chords score below the palette's best (C, the cadence) and some below its
    // worst; whatever happens, none leaves 0..100, and the palette's own range is still exactly 0..100.
    for (const Key& browsed : {key('C', 0, Mode::Ionian), key('G', 0, Mode::Ionian), key('A', 0, Mode::Aeolian),
                               key('E', -1, Mode::Ionian), key('F', 1, Mode::HarmonicMinor)}) {
        for (int selected : {1, 2, 3}) {
            CAPTURE(keyName(browsed));
            CAPTURE(selected);
            DocumentStore store = threeChords();
            store.setBrowsedKey(browsed);
            store.setSelectedEvent(selected);
            const json snap = selectedSnapshot(store);
            REQUIRE(snap.at("relationMode") == "blend");
            int lo = 100;
            int hi = 0;
            for (const json& c : sizedCells(snap)) {
                lo = std::min(lo, c.at("relation").get<int>());
                hi = std::max(hi, c.at("relation").get<int>());
            }
            CHECK(lo == 0);
            CHECK(hi == 100);
            for (const json& e : snap.at("applied")) {
                CAPTURE(e.at("symbol").get<std::string>());
                CHECK(e.at("relation").get<int>() >= 0);
                CHECK(e.at("relation").get<int>() <= 100);
            }
        }
    }
}
