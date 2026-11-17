#include "doctest.h"

#include "chordlab/applied.h"
#include "chordlab/chord.h"
#include "chordlab/compiler.h"
#include "chordlab/document.h"
#include "chordlab/feel.h"
#include "chordlab/function.h"
#include "chordlab/harmony.h"
#include "chordlab/json_api.h"
#include "chordlab/limits.h"
#include "chordlab/link.h"
#include "chordlab/listener.h"
#include "chordlab/palette.h"
#include "chordlab/pitch.h"
#include "chordlab/presets.h"
#include "chordlab/relatedness.h"
#include "chordlab/scale.h"
#include "chordlab/suggest.h"
#include "chordlab/symbol.h"
#include "chordlab/tension.h"
#include "chordlab/transpose.h"
#include "chordlab/voicing.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
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

TEST_CASE("appendChord without a length appends one bar of the meter") {
    struct Case { int num; int den; double beats; };
    for (const Case& c : {Case{4, 4, 4.0}, Case{3, 4, 3.0}, Case{6, 8, 3.0}, Case{7, 8, 3.5}, Case{2, 4, 2.0},
                          Case{5, 4, 5.0}, Case{12, 8, 6.0}}) {
        CAPTURE(c.num);
        CAPTURE(c.den);
        DocumentStore store;
        applyOk(store, {{"cmd", "setMeter"}, {"num", c.num}, {"den", c.den}});
        applyOk(store, {{"cmd", "appendChord"}, {"root", 0}, {"type", "maj"}});
        const json snap = snapshot(store);
        REQUIRE(snap.at("events").size() == 1);
        CHECK(snap.at("events")[0].at("lengthBeats").get<double>() == c.beats);
    }
}

TEST_CASE("appendChord: an explicit length wins in any meter, and setMeter leaves lengths unchanged") {
    DocumentStore store;
    applyOk(store, {{"cmd", "appendChord"}, {"root", 0}, {"type", "maj"}});
    applyOk(store, {{"cmd", "appendChord"}, {"root", 7}, {"type", "maj"}, {"lengthBeats", 2.5}});
    applyOk(store, {{"cmd", "setMeter"}, {"num", 7}, {"den", 8}});
    json snap = snapshot(store);
    CHECK(snap.at("events")[0].at("lengthBeats").get<double>() == 4.0);
    CHECK(snap.at("events")[1].at("lengthBeats").get<double>() == 2.5);
    applyOk(store, {{"cmd", "appendChord"}, {"root", 2}, {"type", "min"}, {"lengthBeats", 1.0}});
    applyOk(store, {{"cmd", "appendChord"}, {"root", 4}, {"type", "min"}});
    snap = snapshot(store);
    CHECK(snap.at("events")[2].at("lengthBeats").get<double>() == 1.0);
    CHECK(snap.at("events")[3].at("lengthBeats").get<double>() == 3.5);
    applyOk(store, {{"cmd", "setMeter"}, {"num", 3}, {"den", 4}});
    snap = snapshot(store);
    CHECK(snap.at("events")[0].at("lengthBeats").get<double>() == 4.0);
    CHECK(snap.at("events")[1].at("lengthBeats").get<double>() == 2.5);
    CHECK(snap.at("events")[2].at("lengthBeats").get<double>() == 1.0);
    CHECK(snap.at("events")[3].at("lengthBeats").get<double>() == 3.5);
    CHECK(snap.at("endBeat").get<double>() == 11.0);
}

TEST_CASE("loadPreset: one-bar steps follow the meter, in append and replace, and leave meter and feel alone") {
    struct Case { int num; int den; double beats; };
    for (const Case& c : {Case{4, 4, 4.0}, Case{3, 4, 3.0}, Case{7, 8, 3.5}, Case{6, 8, 3.0}}) {
        CAPTURE(c.num);
        CAPTURE(c.den);
        for (const char* mode : {"append", "replace"}) {
            CAPTURE(mode);
            DocumentStore store;
            applyOk(store, {{"cmd", "setMeter"}, {"num", c.num}, {"den", c.den}});
            applyOk(store, {{"cmd", "setFeel"}, {"rhythm", "pulse"}, {"strum", "down"},
                            {"strumSpeed", "tight"}, {"humanise", "medium"}});
            applyOk(store, {{"cmd", "loadPreset"}, {"id", "axis"}, {"mode", mode}});
            const json snap = snapshot(store);
            REQUIRE(snap.at("events").size() == 4);
            for (const json& e : snap.at("events")) {
                CHECK(e.at("lengthBeats").get<double>() == c.beats);
            }
            CHECK(snap.at("endBeat").get<double>() == 4 * c.beats);
            CHECK(snap.at("meter") == json({{"num", c.num}, {"den", c.den}}));
            CHECK(snap.at("feel").at("rhythm") == "pulse");
        }
    }
}

TEST_CASE("snapshot: start beats accumulate and symbols follow the project key, not the browsed key") {
    DocumentStore store;
    applyOk(store, {{"cmd", "setKey"}, {"letter", "F"}, {"alter", 0}, {"mode", "Ionian"}});
    applyOk(store, {{"cmd", "appendChord"}, {"root", 0}, {"type", "maj"}});
    applyOk(store, {{"cmd", "appendChord"}, {"root", 3}, {"type", "maj"}, {"lengthBeats", 2.5}, {"velocity", 80}});
    applyOk(store, {{"cmd", "browseKey"}, {"steps", 4}});  // A major (from F, where setKey left the home browse): browsing must not respell the progression
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
    // A borrowed chord names its source after everything else; common tones are spelled as the new chord spells them.
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

/// The store's snapshot with its live selection (the anchor and every selected id), parsed back.
json selectedSnapshot(const DocumentStore& store) {
    const Document& d = store.doc();
    return json::parse(snapshotJson(d, buildPalette(d.browsedKey, d.projectKey), store.selectedEventId(),
                                    store.selectedEventIds()));
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

/// The text the snapshot shows for sized cell `i` of `expectedRelations`: a palette cell spells its common
/// tones in the browsed key, a home triad in the project key, as each cell's own symbol is spelled.
std::string expectedText(const json& snap, const Document& doc, const std::vector<Relatedness>& expected,
                         std::size_t i) {
    const bool palette = i < snap.at("palette").at("cells").size();
    return relationText(expected[i].relation, palette ? doc.browsedKey : doc.projectKey);
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
        CHECK(cells[i].at("relationText") == expectedText(snap, store.doc(), expected, i));
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
        CHECK(cells[i].at("relationText") == expectedText(snap, store.doc(), expected, i));
        lo = std::min(lo, cells[i].at("relation").get<int>());
        hi = std::max(hi, cells[i].at("relation").get<int>());
    }
    CHECK(lo == 0);
    CHECK(hi == 100);
    // One population: the home row is not normalised on its own (its worst is not forced to 0).
    const json& home = snap.at("homeTriads");
    CHECK(home[3].at("relation") == 100);  // F itself
    CHECK(home[4].at("relation") == 100);  // G: the best chord after F, D after PD, the top of the population
    // The relation text: common tones as the cell's chord spells them, the borrowing in the project key.
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
    CHECK(snapshot(store).at("browsedKeyName") == "F Ionian");  // browsing starts from Eb: setKey took the home browse along
    applyOk(store, {{"cmd", "browseKey"}, {"home", true}});
    CHECK(snapshot(store).at("browsedKeyName") == "Eb Ionian");
}

// --- setKey ----------------------------------------------------------------------------

TEST_CASE("setKey: sets the project key, a home browse follows, and is undoable") {
    DocumentStore store;
    applyOk(store, {{"cmd", "setKey"}, {"letter", "D"}, {"alter", 0}, {"mode", "Dorian"}});
    json snap = snapshot(store);
    CHECK(snap.at("projectKeyName") == "D Dorian");
    CHECK(snap.at("projectKey") == keyJson("D", 0, "Dorian"));
    CHECK(snap.at("browsedKeyName") == "D Dorian");  // the browse was home (C Ionian) and stays home

    applyOk(store, {{"cmd", "setKey"}, {"letter", "B"}, {"alter", -1}, {"mode", "MelodicMinor"}});
    CHECK(snapshot(store).at("projectKeyName") == "Bb MelodicMinor");

    REQUIRE(store.undo());
    CHECK(store.doc().projectKey == key('D', 0, Mode::Dorian));
    REQUIRE(store.undo());
    CHECK(store.doc().projectKey == key('C', 0, Mode::Ionian));
    CHECK_FALSE(store.undo());
}

TEST_CASE("undo / redo: a home browse follows the project key they restore, so the palette never stays on the old key") {
    DocumentStore store;
    applyOk(store, {{"cmd", "setKey"}, {"letter", "G"}, {"alter", 0}, {"mode", "Ionian"}});
    applyOk(store, {{"cmd", "browseKey"}, {"home", true}});
    CHECK(snapshot(store).at("browsedKeyName") == "G Ionian");

    applyOk(store, {{"cmd", "undo"}});
    json snap = snapshot(store);
    CHECK(snap.at("projectKeyName") == "C Ionian");
    CHECK(snap.at("browsedKeyName") == "C Ionian");
    applyOk(store, {{"cmd", "redo"}});
    snap = snapshot(store);
    CHECK(snap.at("projectKeyName") == "G Ionian");
    CHECK(snap.at("browsedKeyName") == "G Ionian");

    // Browsing another key is kept across the undo, as every live setting is.
    applyOk(store, {{"cmd", "browseKey"}, {"steps", 1}});
    CHECK(snapshot(store).at("browsedKeyName") == "D Ionian");
    applyOk(store, {{"cmd", "undo"}});
    snap = snapshot(store);
    CHECK(snap.at("projectKeyName") == "C Ionian");
    CHECK(snap.at("browsedKeyName") == "D Ionian");
}

// --- Transpose commands ------------------------------------------------------------------

TEST_CASE("setTransposeWithKey and setKeyHintSeen: set the live flags, no history, no-ops accepted") {
    DocumentStore store = threeChords();
    CHECK(snapshot(store).at("transposeWithKey") == false);
    CHECK(snapshot(store).at("keyHintSeen") == false);
    applyOk(store, {{"cmd", "setTransposeWithKey"}, {"on", true}});
    applyOk(store, {{"cmd", "setKeyHintSeen"}, {"on", true}});
    json snap = snapshot(store);
    CHECK(snap.at("transposeWithKey") == true);
    CHECK(snap.at("keyHintSeen") == true);
    applyOk(store, {{"cmd", "setTransposeWithKey"}, {"on", true}});  // the same value: a no-op
    applyOk(store, {{"cmd", "setKeyHintSeen"}, {"on", true}});
    applyOk(store, {{"cmd", "setTransposeWithKey"}, {"on", false}});
    CHECK(snapshot(store).at("transposeWithKey") == false);
    CHECK(snapshot(store).at("keyHintSeen") == true);
    REQUIRE(store.undo());  // the three appendChords are the only history
    REQUIRE(store.undo());
    REQUIRE(store.undo());
    CHECK_FALSE(store.undo());
    CHECK(store.doc().keyHintSeen == true);
}

TEST_CASE("transposeFromKey: moves every chord from the given tonic to the project tonic in one undo step") {
    DocumentStore store = threeChords();
    store.setProjectKey(key('D', 0, Mode::Ionian));  // switch off: chords stay
    applyOk(store, {{"cmd", "transposeFromKey"}, {"fromTonicPc", 0}});
    json snap = snapshot(store);
    CHECK(snap.at("events")[0].at("root") == 2);
    CHECK(snap.at("events")[1].at("root") == 7);
    CHECK(snap.at("events")[2].at("root") == 9);
    CHECK(snap.at("events")[0].at("symbol") == "D");
    CHECK(snap.at("events")[1].at("symbol") == "G");
    CHECK(snap.at("events")[2].at("symbol") == "A7");
    CHECK(snap.at("projectKeyName") == "D Ionian");
    REQUIRE(store.undo());
    CHECK(snapshot(store).at("events")[0].at("root") == 0);
    CHECK(snapshot(store).at("projectKeyName") == "D Ionian");

    applyOk(store, {{"cmd", "transposeFromKey"}, {"fromTonicPc", 2}});  // same tonic: a no-op, no history
    CHECK(snapshot(store).at("events")[0].at("root") == 0);
    CHECK(store.undo());  // undoes the setProjectKey, not a phantom step
    CHECK(store.doc().projectKey == key('C', 0, Mode::Ionian));
}

TEST_CASE("transposeProgression: moves every chord by the signed interval in one undo step") {
    DocumentStore store = threeChords();
    applyOk(store, {{"cmd", "transposeProgression"}, {"semitones", 1}});
    json snap = snapshot(store);
    CHECK(snap.at("events")[0].at("root") == 1);
    CHECK(snap.at("events")[1].at("root") == 6);
    CHECK(snap.at("events")[2].at("root") == 8);
    CHECK(snap.at("projectKeyName") == "C Ionian");
    applyOk(store, {{"cmd", "transposeProgression"}, {"semitones", -11}});
    CHECK(snapshot(store).at("events")[0].at("root") == 2);
    REQUIRE(store.undo());
    CHECK(snapshot(store).at("events")[0].at("root") == 1);
    REQUIRE(store.undo());
    CHECK(snapshot(store).at("events")[0].at("root") == 0);

    applyOk(store, {{"cmd", "transposeProgression"}, {"semitones", 0}});  // a no-op: no history
    applyOk(store, {{"cmd", "transposeProgression"}, {"semitones", 3}});
    REQUIRE(store.undo());
    CHECK(snapshot(store).at("events")[0].at("root") == 0);
    DocumentStore empty;
    applyOk(empty, {{"cmd", "transposeProgression"}, {"semitones", 5}});
    CHECK_FALSE(empty.undo());
}

TEST_CASE("setKey with transposeWithKey on: event roots and symbols move, in one undo step") {
    DocumentStore store = threeChords();  // C F G7 in C
    applyOk(store, {{"cmd", "setTransposeWithKey"}, {"on", true}});
    const json before = snapshot(store);
    applyOk(store, {{"cmd", "setKey"}, {"letter", "G"}, {"alter", 0}, {"mode", "Ionian"}});
    json snap = snapshot(store);
    CHECK(snap.at("projectKeyName") == "G Ionian");
    CHECK(snap.at("events")[0].at("root") == 7);   // C -> G (down a fourth)
    CHECK(snap.at("events")[1].at("root") == 0);   // F -> C
    CHECK(snap.at("events")[2].at("root") == 2);   // G7 -> D7
    CHECK(snap.at("events")[0].at("symbol") == "G");
    CHECK(snap.at("events")[1].at("symbol") == "C");
    CHECK(snap.at("events")[2].at("symbol") == "D7");
    CHECK(snap.at("transposeWithKey") == true);
    REQUIRE(store.undo());
    json undone = snapshot(store);
    CHECK(undone.at("projectKeyName") == "C Ionian");
    CHECK(undone.at("events") == before.at("events"));
    CHECK(undone.at("transposeWithKey") == true);  // live state survives
}

/// threeChords (C F G7, four beats each) with a Bass track (id 1) holding pitches 2 and 6 on C (ids 1 and 2), 40 on F
/// (id 3) and 125 on G7 (id 4).
DocumentStore threeChordsWithBassNotes() {
    DocumentStore store = threeChords();
    REQUIRE(store.addTrack(TrackRole::Bass) == 1);
    REQUIRE(store.insertNotes(1, {NoteSpec{2, 0, 480}, NoteSpec{6, 0, 480}, NoteSpec{40, 3840, 480},
                                  NoteSpec{125, 7680, 480}}) == std::vector<int>{1, 2, 3, 4});
    return store;
}

std::vector<std::pair<int, int>> bassIdAndPitch(const DocumentStore& store) {
    std::vector<std::pair<int, int>> out;
    for (const TrackNote& n : store.doc().tracks.at(0).notes) {
        out.emplace_back(n.id, n.pitch);
    }
    return out;
}

using IdPitches = std::vector<std::pair<int, int>>;

TEST_CASE("setKey with transposeWithKey on carries the track notes by the chords' interval, one undo step") {
    DocumentStore store = threeChordsWithBassNotes();
    applyOk(store, {{"cmd", "setTransposeWithKey"}, {"on", true}});
    const Document before = store.doc();
    const std::uint64_t revision = store.revision();

    applyOk(store, {{"cmd", "setKey"}, {"letter", "G"}, {"alter", 0}, {"mode", "Ionian"}});

    CHECK(snapshot(store).at("events")[0].at("root") == 7);  // C -> G, down a fourth (-5)
    CHECK(bassIdAndPitch(store) == IdPitches{{2, 1}, {1, 9}, {3, 35}, {4, 120}});  // -5, 2 and 6 folded up and re-sorted
    CHECK(store.revision() == revision + 1);
    const Document after = store.doc();

    REQUIRE(store.undo());
    CHECK(store.doc() == before);
    CHECK(bassIdAndPitch(store) == IdPitches{{1, 2}, {2, 6}, {3, 40}, {4, 125}});
    REQUIRE(store.redo());
    CHECK(store.doc() == after);
}

TEST_CASE("setKey with transposeWithKey off, and a mode-only setKey, leave the track notes alone") {
    DocumentStore off = threeChordsWithBassNotes();
    applyOk(off, {{"cmd", "setKey"}, {"letter", "G"}, {"alter", 0}, {"mode", "Ionian"}});
    CHECK(bassIdAndPitch(off) == IdPitches{{1, 2}, {2, 6}, {3, 40}, {4, 125}});

    DocumentStore on = threeChordsWithBassNotes();
    applyOk(on, {{"cmd", "setTransposeWithKey"}, {"on", true}});
    applyOk(on, {{"cmd", "setKey"}, {"letter", "C"}, {"alter", 0}, {"mode", "Aeolian"}});
    CHECK(bassIdAndPitch(on) == IdPitches{{1, 2}, {2, 6}, {3, 40}, {4, 125}});
    CHECK(on.doc().events == threeChords().doc().events);
}

TEST_CASE("transposeFromKey and transposeProgression carry the track notes, the explicit interval exactly") {
    DocumentStore store = threeChordsWithBassNotes();
    applyOk(store, {{"cmd", "setKey"}, {"letter", "G"}, {"alter", 0}, {"mode", "Ionian"}});  // switch off
    applyOk(store, {{"cmd", "transposeFromKey"}, {"fromTonicPc", 0}});
    CHECK(bassIdAndPitch(store) == IdPitches{{2, 1}, {1, 9}, {3, 35}, {4, 120}});  // the chords' -5
    REQUIRE(store.undo());
    CHECK(bassIdAndPitch(store) == IdPitches{{1, 2}, {2, 6}, {3, 40}, {4, 125}});

    DocumentStore explicitUp = threeChordsWithBassNotes();
    applyOk(explicitUp, {{"cmd", "transposeProgression"}, {"semitones", 7}});  // +7, not -5
    CHECK(bassIdAndPitch(explicitUp) == IdPitches{{1, 9}, {2, 13}, {3, 47}, {4, 120}});
    CHECK(snapshot(explicitUp).at("events")[0].at("root") == 7);

    DocumentStore explicitDown = threeChordsWithBassNotes();
    applyOk(explicitDown, {{"cmd", "transposeProgression"}, {"semitones", -7}});
    CHECK(bassIdAndPitch(explicitDown) == IdPitches{{1, 7}, {2, 11}, {3, 33}, {4, 118}});
}

/// C, F, G7 all at octave -1 (the chords' direction is then +7 for a fifth up, not the smaller -5) with a Bass track
/// (id 1): 40 on C (id 1), 52 on F (id 2), 100 on G7 (id 3).
DocumentStore lowChordsWithBassNotes() {
    DocumentStore store;
    REQUIRE(store.insertChords(0, {ChordSpec{kC, 4.0, 100, std::nullopt, -1}, ChordSpec{kF, 4.0, 100, std::nullopt, -1},
                                   ChordSpec{kG7, 4.0, 100, std::nullopt, -1}}) == std::vector<int>{1, 2, 3});
    REQUIRE(store.addTrack(TrackRole::Bass) == 1);
    REQUIRE(store.insertNotes(1, {NoteSpec{40, 0, 480}, NoteSpec{52, 3840, 480}, NoteSpec{100, 7680, 480}}) ==
            std::vector<int>{1, 2, 3});
    return store;
}

TEST_CASE("setKey and transposeFromKey carry the notes by the chords' larger move (+7), not the smaller one (-5)") {
    DocumentStore on = lowChordsWithBassNotes();
    REQUIRE(chooseKeyTranspose(on.doc().events, 7) == 7);
    applyOk(on, {{"cmd", "setTransposeWithKey"}, {"on", true}});
    applyOk(on, {{"cmd", "setKey"}, {"letter", "G"}, {"alter", 0}, {"mode", "Ionian"}});
    CHECK(snapshot(on).at("events")[0].at("root") == 7);
    CHECK(bassIdAndPitch(on) == IdPitches{{1, 47}, {2, 59}, {3, 107}});  // 40 -> 47, not 35

    DocumentStore off = lowChordsWithBassNotes();
    applyOk(off, {{"cmd", "setKey"}, {"letter", "G"}, {"alter", 0}, {"mode", "Ionian"}});  // switch off
    CHECK(bassIdAndPitch(off) == IdPitches{{1, 40}, {2, 52}, {3, 100}});
    applyOk(off, {{"cmd", "transposeFromKey"}, {"fromTonicPc", 0}});
    CHECK(snapshot(off).at("events")[0].at("root") == 7);
    CHECK(bassIdAndPitch(off) == IdPitches{{1, 47}, {2, 59}, {3, 107}});
}

TEST_CASE("rejection: transpose commands with missing, mistyped or out-of-range fields are refused whole") {
    DocumentStore store = threeChords();
    checkRejected(store, {{"cmd", "setTransposeWithKey"}});
    checkRejected(store, {{"cmd", "setTransposeWithKey"}, {"on", 1}});
    checkRejected(store, {{"cmd", "setTransposeWithKey"}, {"on", "true"}});
    checkRejected(store, {{"cmd", "setKeyHintSeen"}});
    checkRejected(store, {{"cmd", "setKeyHintSeen"}, {"on", 0}});
    checkRejected(store, {{"cmd", "setKeyHintSeen"}, {"on", nullptr}});
    checkRejected(store, {{"cmd", "transposeFromKey"}});
    checkRejected(store, {{"cmd", "transposeFromKey"}, {"fromTonicPc", "0"}});
    checkRejected(store, {{"cmd", "transposeFromKey"}, {"fromTonicPc", 1.5}});
    checkRejected(store, {{"cmd", "transposeFromKey"}, {"fromTonicPc", -1}});
    checkRejected(store, {{"cmd", "transposeFromKey"}, {"fromTonicPc", 12}});
    checkRejected(store, {{"cmd", "transposeProgression"}});
    checkRejected(store, {{"cmd", "transposeProgression"}, {"semitones", "1"}});
    checkRejected(store, {{"cmd", "transposeProgression"}, {"semitones", 0.5}});
    checkRejected(store, {{"cmd", "transposeProgression"}, {"semitones", 12}});
    checkRejected(store, {{"cmd", "transposeProgression"}, {"semitones", -12}});

    std::string error;
    CHECK_FALSE(apply(store, {{"cmd", "transposeFromKey"}, {"fromTonicPc", 12}}, error));
    CHECK(error.find("fromTonicPc") != std::string::npos);
    CHECK(error.find("in 0..11") != std::string::npos);
    CHECK_FALSE(apply(store, {{"cmd", "transposeProgression"}, {"semitones", 12}}, error));
    CHECK(error.find("semitones") != std::string::npos);
    CHECK(error.find("in -11..11") != std::string::npos);
}

TEST_CASE("snapshot: thirty-seven top-level keys, with the transpose flags, the feel, midiThru, the selection, the new-chord length and the tracks keys") {
    DocumentStore store = threeChords();
    CHECK(snapshot(store).size() == 37);
}

// --- Feel and meter --------------------------------------------------------------------------

namespace {

/// How many undo and how many redo steps `store` has, counted on a copy.
std::pair<int, int> historyDepth(const DocumentStore& store) {
    DocumentStore redoCopy = store;
    int redos = 0;
    while (redoCopy.redo()) {
        ++redos;
    }
    DocumentStore undoCopy = store;
    int undos = 0;
    while (undoCopy.undo()) {
        ++undos;
    }
    return {undos, redos};
}

/// Checks that `cmd` is rejected and that neither the document nor the undo/redo history changed.
void checkRejectedKeepsHistory(DocumentStore& store, const json& cmd) {
    CAPTURE(cmd.dump());
    const Document before = store.doc();
    const std::pair<int, int> depth = historyDepth(store);
    std::string error;
    CHECK_FALSE(apply(store, cmd, error));
    CHECK_FALSE(error.empty());
    CHECK(store.doc() == before);
    CHECK(historyDepth(store) == depth);
}

json feelCmd(json rhythm, json strum, json speed, json humanise) {
    return json{{"cmd", "setFeel"}, {"rhythm", rhythm}, {"strum", strum}, {"strumSpeed", speed}, {"humanise", humanise}};
}

const json kBlockFeelCmd = feelCmd("block", "off", "medium", "off");

/// `setFeel` with the arpeggio fields too.
json arpCmd(const char* pattern, const char* rate, const char* range, const char* rhythm = "arpeggio") {
    json c = feelCmd(rhythm, "off", "medium", "off");
    c["arpPattern"] = pattern;
    c["arpRate"] = rate;
    c["arpRange"] = range;
    return c;
}

}  // namespace

TEST_CASE("setFeel: every option of every field is accepted and lands in the document") {
    const std::pair<const char*, Rhythm> rhythms[] = {{"block", Rhythm::Block},
                                                      {"pulse", Rhythm::Pulse},
                                                      {"subdivided", Rhythm::Subdivided},
                                                      {"offbeats", Rhythm::Offbeats},
                                                      {"lowThenUpper", Rhythm::LowThenUpper},
                                                      {"arpeggio", Rhythm::Arpeggio}};
    const std::pair<const char*, Strum> strums[] = {
        {"off", Strum::Off}, {"down", Strum::Down}, {"up", Strum::Up}, {"downUp", Strum::DownUp}};
    const std::pair<const char*, StrumSpeed> speeds[] = {
        {"tight", StrumSpeed::Tight}, {"medium", StrumSpeed::Medium}, {"loose", StrumSpeed::Loose}};
    const std::pair<const char*, Humanise> humanises[] = {
        {"off", Humanise::Off}, {"light", Humanise::Light}, {"medium", Humanise::Medium}, {"heavy", Humanise::Heavy}};
    // Each field in turn at every option, the others at a non-default value so that a mix-up between fields shows.
    for (const auto& r : rhythms) {
        DocumentStore store = threeChords();
        applyOk(store, feelCmd(r.first, "up", "loose", "heavy"));
        CHECK(store.doc().feel == Feel{r.second, Strum::Up, StrumSpeed::Loose, Humanise::Heavy});
    }
    {
        DocumentStore store = threeChords();
        applyOk(store, feelCmd("arpUp", "up", "loose", "heavy"));
        CHECK(store.doc().feel == Feel{Rhythm::Arpeggio, Strum::Up, StrumSpeed::Loose, Humanise::Heavy, ArpPattern::Up});
        applyOk(store, feelCmd("arpUpDown", "up", "loose", "heavy"));
        CHECK(store.doc().feel == Feel{Rhythm::Arpeggio, Strum::Up, StrumSpeed::Loose, Humanise::Heavy, ArpPattern::UpDown});
    }
    for (const auto& st : strums) {
        DocumentStore store = threeChords();
        applyOk(store, feelCmd("arpUp", st.first, "loose", "heavy"));
        CHECK(store.doc().feel == Feel{Rhythm::Arpeggio, st.second, StrumSpeed::Loose, Humanise::Heavy});
    }
    for (const auto& sp : speeds) {
        DocumentStore store = threeChords();
        applyOk(store, feelCmd("arpUp", "up", sp.first, "heavy"));
        CHECK(store.doc().feel == Feel{Rhythm::Arpeggio, Strum::Up, sp.second, Humanise::Heavy});
    }
    for (const auto& h : humanises) {
        DocumentStore store = threeChords();
        applyOk(store, feelCmd("arpUp", "up", "loose", h.first));
        CHECK(store.doc().feel == Feel{Rhythm::Arpeggio, Strum::Up, StrumSpeed::Loose, h.second});
    }
}

TEST_CASE("setFeel: every arpeggio pattern, rate and range is accepted") {
    const std::pair<const char*, ArpPattern> patterns[] = {
        {"up", ArpPattern::Up}, {"down", ArpPattern::Down}, {"upDown", ArpPattern::UpDown}, {"downUp", ArpPattern::DownUp},
        {"upAndDown", ArpPattern::UpAndDown}, {"downAndUp", ArpPattern::DownAndUp}, {"converge", ArpPattern::Converge},
        {"diverge", ArpPattern::Diverge}, {"conAndDiverge", ArpPattern::ConAndDiverge}, {"pinkyUp", ArpPattern::PinkyUp},
        {"pinkyUpDown", ArpPattern::PinkyUpDown}, {"thumbUp", ArpPattern::ThumbUp}, {"thumbUpDown", ArpPattern::ThumbUpDown},
        {"alberti", ArpPattern::Alberti}, {"rootFirst", ArpPattern::RootFirst}, {"chord", ArpPattern::Chord},
        {"random", ArpPattern::Random}};
    static_assert(sizeof(patterns) / sizeof(patterns[0]) == kArpPatternCount, "every pattern has a wire name");
    const std::pair<const char*, ArpRate> rates[] = {
        {"pulse", ArpRate::Pulse}, {"subdivision", ArpRate::Subdivision}, {"half", ArpRate::Half}};
    const std::pair<const char*, ArpRange> ranges[] = {{"one", ArpRange::One}, {"two", ArpRange::Two}};
    for (const auto& p : patterns) {
        DocumentStore store = threeChords();
        applyOk(store, arpCmd(p.first, "half", "two"));
        Feel expected{Rhythm::Arpeggio};
        expected.pattern = p.second; expected.rate = ArpRate::Half; expected.range = ArpRange::Two;
        CHECK(store.doc().feel == expected);
    }
    for (const auto& r : rates) {
        DocumentStore store = threeChords();
        applyOk(store, arpCmd("alberti", r.first, "two"));
        CHECK(store.doc().feel.rate == r.second);
    }
    for (const auto& g : ranges) {
        DocumentStore store = threeChords();
        applyOk(store, arpCmd("alberti", "half", g.first));
        CHECK(store.doc().feel.range == g.second);
    }
}

TEST_CASE("setFeel: the arpeggio fields are optional and default; present means valid") {
    DocumentStore store = threeChords();
    applyOk(store, feelCmd("arpeggio", "off", "medium", "off"));
    CHECK(store.doc().feel == Feel{Rhythm::Arpeggio});  // up, subdivision, one
    // Kept under another rhythm too (they ride along, unheard).
    applyOk(store, arpCmd("random", "half", "two", "pulse"));
    Feel expected{Rhythm::Pulse};
    expected.pattern = ArpPattern::Random; expected.rate = ArpRate::Half; expected.range = ArpRange::Two;
    CHECK(store.doc().feel == expected);
    // One field at a time: the others keep their defaults.
    {
        DocumentStore s2 = threeChords();
        json c = feelCmd("arpeggio", "off", "medium", "off");
        c["arpRate"] = "half";
        applyOk(s2, c);
        Feel e2{Rhythm::Arpeggio};
        e2.rate = ArpRate::Half;
        CHECK(s2.doc().feel == e2);
    }
    // Unknown or mistyped values reject the whole message; nothing changes, no history.
    for (const char* field : {"arpPattern", "arpRate", "arpRange"}) {
        for (const json& wrong : {json("sideways"), json(nullptr), json(3), json(true), json::object(), json::array(), json("")}) {
            json c = arpCmd("up", "pulse", "one");
            c[field] = wrong;
            checkRejectedKeepsHistory(store, c);
        }
    }
    std::string error;
    CHECK_FALSE(apply(store, arpCmd("sideways", "pulse", "one"), error));
    CHECK(error.find("arpPattern") != std::string::npos);
}

TEST_CASE("setFeel: legacy rhythm names map to the arpeggio; a named pattern wins; partial fields combine") {
    DocumentStore store = threeChords();
    applyOk(store, feelCmd("arpUp", "down", "tight", "light"));
    CHECK(store.doc().feel == Feel{Rhythm::Arpeggio, Strum::Down, StrumSpeed::Tight, Humanise::Light, ArpPattern::Up});
    applyOk(store, feelCmd("arpUpDown", "down", "tight", "light"));
    CHECK(store.doc().feel == Feel{Rhythm::Arpeggio, Strum::Down, StrumSpeed::Tight, Humanise::Light, ArpPattern::UpDown});
    applyOk(store, arpCmd("alberti", "pulse", "two", "arpUpDown"));
    Feel expected{Rhythm::Arpeggio};
    expected.pattern = ArpPattern::Alberti; expected.rate = ArpRate::Pulse; expected.range = ArpRange::Two;
    CHECK(store.doc().feel == expected);
    // Legacy name plus only a rate: the legacy pattern and the given rate.
    json partial = feelCmd("arpUpDown", "off", "medium", "off");
    partial["arpRate"] = "half";
    applyOk(store, partial);
    Feel e2{Rhythm::Arpeggio};
    e2.pattern = ArpPattern::UpDown; e2.rate = ArpRate::Half;
    CHECK(store.doc().feel == e2);
    // The snapshot never writes the legacy names.
    CHECK(snapshot(store).at("feel").at("rhythm") == "arpeggio");
}

TEST_CASE("setFeel: reads back in the snapshot as seven strings") {
    DocumentStore store = threeChords();
    CHECK(snapshot(store).at("feel") == json({{"rhythm", "block"}, {"strum", "off"}, {"strumSpeed", "medium"},
                                              {"humanise", "off"}, {"arpPattern", "up"}, {"arpRate", "subdivision"},
                                              {"arpRange", "one"}}));
    applyOk(store, arpCmd("thumbUpDown", "half", "two"));
    CHECK(snapshot(store).at("feel") == json({{"rhythm", "arpeggio"}, {"strum", "off"}, {"strumSpeed", "medium"},
                                              {"humanise", "off"}, {"arpPattern", "thumbUpDown"}, {"arpRate", "half"},
                                              {"arpRange", "two"}}));
}

TEST_CASE("setFeel: rejected messages leave the document and the undo history untouched") {
    DocumentStore store = threeChords();
    store.setVelocity(1, 64);
    store.setVelocity(2, 65);
    store.undo();  // one redo step, and the undo steps of the three chords and the first velocity
    REQUIRE(historyDepth(store).second == 1);
    const json ok = feelCmd("pulse", "down", "tight", "light");

    std::vector<json> bad;
    bad.push_back(feelCmd("waltz", "off", "medium", "off"));      // unknown names
    bad.push_back(feelCmd("block", "sideways", "medium", "off"));
    bad.push_back(feelCmd("block", "off", "fast", "off"));
    bad.push_back(feelCmd("block", "off", "medium", "extreme"));
    bad.push_back(feelCmd("Block", "off", "medium", "off"));      // case matters
    bad.push_back(feelCmd("", "off", "medium", "off"));
    for (const char* field : {"rhythm", "strum", "strumSpeed", "humanise"}) {
        json missing = ok;
        missing.erase(field);
        bad.push_back(missing);
        for (const json& wrong : {json(nullptr), json(3), json(true), json::object(), json::array(),
                                  json({"pulse"}), json({{"v", "pulse"}}), json(1.5)}) {
            json c = ok;
            c[field] = wrong;
            bad.push_back(c);
        }
    }
    for (const json& c : bad) {
        checkRejectedKeepsHistory(store, c);
    }

    std::string error;
    CHECK_FALSE(apply(store, feelCmd("waltz", "off", "medium", "off"), error));
    CHECK(error.find("rhythm") != std::string::npos);
    CHECK_FALSE(apply(store, feelCmd("block", "off", "medium", nullptr), error));
    CHECK(error.find("humanise") != std::string::npos);
}

TEST_CASE("setFeel: undo and redo, and an equal feel pushes no history") {
    DocumentStore store = threeChords();
    const std::pair<int, int> start = historyDepth(store);
    applyOk(store, kBlockFeelCmd);  // already the feel
    CHECK(historyDepth(store) == start);

    applyOk(store, feelCmd("subdivided", "down", "loose", "heavy"));
    CHECK(store.doc().feel == Feel{Rhythm::Subdivided, Strum::Down, StrumSpeed::Loose, Humanise::Heavy});
    applyOk(store, feelCmd("pulse", "off", "medium", "off"));
    applyOk(store, {{"cmd", "undo"}});
    CHECK(store.doc().feel == Feel{Rhythm::Subdivided, Strum::Down, StrumSpeed::Loose, Humanise::Heavy});
    applyOk(store, {{"cmd", "undo"}});
    CHECK(store.doc().feel == Feel{});
    applyOk(store, {{"cmd", "redo"}});
    CHECK(store.doc().feel == Feel{Rhythm::Subdivided, Strum::Down, StrumSpeed::Loose, Humanise::Heavy});
    applyOk(store, {{"cmd", "redo"}});
    CHECK(store.doc().feel == Feel{Rhythm::Pulse, Strum::Off, StrumSpeed::Medium, Humanise::Off});
}

TEST_CASE("setMeter: valid meters are accepted, undone and redone, and shown in the snapshot") {
    DocumentStore store = threeChords();
    const int startDepth = historyDepth(store).first;
    CHECK(snapshot(store).at("meter") == json({{"num", 4}, {"den", 4}}));
    const std::pair<int, int> meters[] = {{3, 4}, {6, 8}, {7, 8}, {2, 4}, {16, 8}, {1, 2}, {12, 8}, {5, 4}};
    for (const auto& m : meters) {
        applyOk(store, {{"cmd", "setMeter"}, {"num", m.first}, {"den", m.second}});
        CHECK(store.doc().meter.num == m.first);
        CHECK(store.doc().meter.den == m.second);
        CHECK(snapshot(store).at("meter") == json({{"num", m.first}, {"den", m.second}}));
    }
    // Eight distinct changes from 4/4 gave eight undo steps.
    CHECK(historyDepth(store).first == startDepth + 8);
    applyOk(store, {{"cmd", "undo"}});
    CHECK(store.doc().meter.num == 12);
    CHECK(store.doc().meter.den == 8);
    applyOk(store, {{"cmd", "redo"}});
    CHECK(store.doc().meter.num == 5);
    CHECK(store.doc().meter.den == 4);

    const std::pair<int, int> depth = historyDepth(store);
    applyOk(store, {{"cmd", "setMeter"}, {"num", 5}, {"den", 4}});  // equal: no history
    CHECK(historyDepth(store) == depth);
}

TEST_CASE("setMeter: rejected messages leave the document and the undo history untouched") {
    DocumentStore store = threeChords();
    store.setVelocity(1, 64);
    applyOk(store, {{"cmd", "setMeter"}, {"num", 3}, {"den", 4}});
    store.undo();
    REQUIRE(historyDepth(store).second == 1);

    std::vector<json> bad = {
        {{"cmd", "setMeter"}, {"num", 0}, {"den", 4}},       // out of range
        {{"cmd", "setMeter"}, {"num", 17}, {"den", 4}},
        {{"cmd", "setMeter"}, {"num", -3}, {"den", 4}},
        {{"cmd", "setMeter"}, {"num", 4}, {"den", 3}},       // den not 2, 4 or 8
        {{"cmd", "setMeter"}, {"num", 4}, {"den", 0}},
        {{"cmd", "setMeter"}, {"num", 4}, {"den", 16}},
        {{"cmd", "setMeter"}, {"num", 4}, {"den", 1}},
        {{"cmd", "setMeter"}, {"num", 4}},                   // missing
        {{"cmd", "setMeter"}, {"den", 4}},
        {{"cmd", "setMeter"}},
        {{"cmd", "setMeter"}, {"num", 3.5}, {"den", 4}},     // not integers
        {{"cmd", "setMeter"}, {"num", 3.0}, {"den", 4}},
        {{"cmd", "setMeter"}, {"num", 3}, {"den", 4.0}},
        {{"cmd", "setMeter"}, {"num", 3}, {"den", 4.5}},
        {{"cmd", "setMeter"}, {"num", "3"}, {"den", 4}},     // strings, nulls, nested values
        {{"cmd", "setMeter"}, {"num", 3}, {"den", "4"}},
        {{"cmd", "setMeter"}, {"num", nullptr}, {"den", 4}},
        {{"cmd", "setMeter"}, {"num", 3}, {"den", nullptr}},
        {{"cmd", "setMeter"}, {"num", true}, {"den", 4}},
        {{"cmd", "setMeter"}, {"num", json::object()}, {"den", 4}},
        {{"cmd", "setMeter"}, {"num", json::array({3})}, {"den", 4}},
        {{"cmd", "setMeter"}, {"num", 3}, {"den", json({{"v", 4}})}},
        {{"cmd", "setMeter"}, {"num", 3}, {"den", json::array()}},
        {{"cmd", "setMeter"}, {"num", 4294967299ull}, {"den", 4}},  // 2^32 + 3 must not wrap to 3
        {{"cmd", "setMeter"}, {"num", 3}, {"den", 4294967300ull}},
    };
    for (const json& c : bad) {
        checkRejectedKeepsHistory(store, c);
    }
    std::string error;
    CHECK_FALSE(apply(store, {{"cmd", "setMeter"}, {"num", 4}, {"den", 3}}, error));
    CHECK(error.find("den") != std::string::npos);
    CHECK_FALSE(apply(store, {{"cmd", "setMeter"}, {"num", 0}, {"den", 4}}, error));
    CHECK(error.find("num") != std::string::npos);
}

TEST_CASE("snapshot: feelLimited is true only when the feel performs and is over budget") {
    // 64 one-bar ninths come to 5120 events under Subdivided: more than kMaxChordEvents (test_feel.cpp).
    DocumentStore store;
    for (int i = 0; i < 64; ++i) {
        applyOk(store, {{"cmd", "appendChord"}, {"root", 0}, {"type", "9"}, {"lengthBeats", 4.0}});
    }
    REQUIRE(store.doc().events.size() == 64);
    CHECK(snapshot(store).at("feelLimited") == false);  // block feel

    applyOk(store, feelCmd("subdivided", "off", "medium", "off"));
    const json limited = snapshot(store);
    CHECK(limited.at("feelLimited") == true);
    CHECK(compile(store.doc()).feelLimited);  // the same answer as the playback path

    applyOk(store, feelCmd("pulse", "off", "medium", "off"));  // 64 x 4 pulses x 5 notes x 2 = 2560: fits
    CHECK(snapshot(store).at("feelLimited") == false);

    applyOk(store, feelCmd("subdivided", "off", "medium", "off"));
    applyOk(store, {{"cmd", "undo"}});  // back to pulse
    CHECK(snapshot(store).at("feelLimited") == false);
    applyOk(store, {{"cmd", "redo"}});
    CHECK(snapshot(store).at("feelLimited") == true);

    DocumentStore small = threeChords();
    applyOk(small, feelCmd("arpUpDown", "downUp", "loose", "heavy"));
    CHECK(snapshot(small).at("feelLimited") == false);
}

TEST_CASE("snapshot: endBeat is the compiled playback period, event lengths stay authored") {
    // Three chords of 0.2506 beat: not tick-aligned. Chord boundaries are lround(0.2506 * 960) = 241,
    // lround(0.5012 * 960) = 481 and lround(0.7518 * 960) = 722 ticks, so a performing feel plays a period of
    // 722 / 960 beat, while the authored lengths sum to a little more than that.
    DocumentStore store;
    for (int i = 0; i < 3; ++i) {
        applyOk(store, {{"cmd", "appendChord"}, {"root", 0}, {"type", "maj"}, {"lengthBeats", 0.2506}});
    }
    const double authored = (0.2506 + 0.2506) + 0.2506;  // left to right, as Document::endBeat accumulates
    REQUIRE(store.doc().endBeat() == authored);
    REQUIRE(authored != 722.0 / 960.0);

    // Block chords: the old path, the authored sum.
    const json block = snapshot(store);
    CHECK(block.at("endBeat").get<double>() == authored);
    CHECK(block.at("endBeat").get<double>() == compile(store.doc()).endBeat);

    // A rhythm, and a strum on block chords: the performed period, 722 ticks.
    for (const json& cmd : {feelCmd("pulse", "off", "medium", "off"), feelCmd("block", "down", "medium", "off")}) {
        CAPTURE(cmd.dump());
        applyOk(store, cmd);
        const json performed = snapshot(store);
        CHECK(performed.at("feelLimited") == false);
        CHECK(performed.at("endBeat").get<double>() == 722.0 / 960.0);
        CHECK(performed.at("endBeat").get<double>() == 0.7520833333333333);
        CHECK(performed.at("endBeat").get<double>() == compile(store.doc()).endBeat);
        // The events keep their authored positions and lengths.
        const json& events = performed.at("events");
        REQUIRE(events.size() == 3);
        CHECK(events[0].at("startBeat").get<double>() == 0.0);
        CHECK(events[1].at("startBeat").get<double>() == 0.2506);
        CHECK(events[2].at("startBeat").get<double>() == 0.2506 + 0.2506);
        for (const json& e : events) {
            CHECK(e.at("lengthBeats").get<double>() == 0.2506);
        }
    }

    // Back to block chords: the authored sum again.
    applyOk(store, kBlockFeelCmd);
    CHECK(snapshot(store).at("endBeat").get<double>() == authored);
}

TEST_CASE("snapshot: an over-budget feel publishes the authored sum as endBeat") {
    // 64 one-bar ninths and one 4.0004-beat chord: 5200+ Subdivided events, over budget, so block chords play
    // and the period is the authored sum; the performed period would be 260 beats (249600 ticks).
    DocumentStore store;
    for (int i = 0; i < 64; ++i) {
        applyOk(store, {{"cmd", "appendChord"}, {"root", 0}, {"type", "9"}, {"lengthBeats", 4.0}});
    }
    applyOk(store, {{"cmd", "appendChord"}, {"root", 0}, {"type", "9"}, {"lengthBeats", 4.0004}});
    double authored = 0.0;
    for (const ChordEvent& e : store.doc().events) {
        authored += e.lengthBeats;
    }
    REQUIRE(store.doc().endBeat() == authored);
    REQUIRE(authored != 260.0);
    CHECK(snapshot(store).at("endBeat").get<double>() == authored);  // block feel

    applyOk(store, feelCmd("subdivided", "off", "medium", "off"));
    const json limited = snapshot(store);
    CHECK(limited.at("feelLimited") == true);
    CHECK(limited.at("endBeat").get<double>() == authored);
    CHECK(limited.at("endBeat").get<double>() == compile(store.doc()).endBeat);
    for (const json& e : limited.at("events")) {
        CHECK(e.at("lengthBeats").get<double>() >= 4.0);
    }
}

TEST_CASE("snapshot: a block document never reads as feelLimited, with the most chords of the shortest length") {
    // kMaxChords quarter-beat ninths (five notes each, ten events per chord): still within the snapshot budget, so
    // compile's verdict (false without a performing feel) agrees with performDocument's.
    DocumentStore store;
    for (int i = 0; i < kMaxChords; ++i) {
        applyOk(store, {{"cmd", "appendChord"}, {"root", 0}, {"type", "9"}, {"lengthBeats", kMinLengthBeats}});
    }
    REQUIRE(store.doc().events.size() == static_cast<std::size_t>(kMaxChords));
    CHECK_FALSE(performDocument(store.doc()).feelLimited);
    const json snap = snapshot(store);
    CHECK(snap.at("feelLimited") == false);
    CHECK(snap.at("endBeat").get<double>() == store.doc().endBeat());
}

TEST_CASE("state: feel and meter are saved and restored, non-default values included") {
    DocumentStore store = threeChords();
    applyOk(store, feelCmd("offbeats", "downUp", "tight", "medium"));
    applyOk(store, {{"cmd", "setMeter"}, {"num", 7}, {"den", 8}});

    const json saved = json::parse(stateJson(store));
    CHECK(saved.at("feel") ==
          json({{"rhythm", "offbeats"}, {"strum", "downUp"}, {"strumSpeed", "tight"}, {"humanise", "medium"},
                {"arpPattern", "up"}, {"arpRate", "subdivision"}, {"arpRange", "one"}}));
    CHECK(saved.at("meter") == json({{"num", 7}, {"den", 8}}));

    DocumentStore loaded;
    std::string error;
    REQUIRE_MESSAGE(loadStateJson(loaded, stateJson(store), error), error);
    CHECK(loaded.doc() == store.doc());
    CHECK(loaded.doc().feel == Feel{Rhythm::Offbeats, Strum::DownUp, StrumSpeed::Tight, Humanise::Medium});
    CHECK(loaded.doc().meter.num == 7);
    CHECK(loaded.doc().meter.den == 8);
    CHECK(stateJson(loaded) == stateJson(store));

    // The default state writes the default feel and meter.
    const json def = json::parse(stateJson(threeChords()));
    CHECK(def.at("feel") == json({{"rhythm", "block"}, {"strum", "off"}, {"strumSpeed", "medium"}, {"humanise", "off"},
                                  {"arpPattern", "up"}, {"arpRate", "subdivision"}, {"arpRange", "one"}}));
    CHECK(def.at("meter") == json({{"num", 4}, {"den", 4}}));
}

TEST_CASE("state: a state without feel and meter loads block chords in 4/4") {
    DocumentStore source = threeChords();
    json legacy = json::parse(stateJson(source));
    legacy.erase("feel");
    legacy.erase("meter");

    DocumentStore store = threeChords();
    applyOk(store, feelCmd("pulse", "down", "tight", "light"));
    applyOk(store, {{"cmd", "setMeter"}, {"num", 3}, {"den", 4}});
    std::string error;
    REQUIRE_MESSAGE(loadStateJson(store, legacy.dump(), error), error);
    CHECK(store.doc().feel == Feel{});
    CHECK(store.doc().meter.num == 4);
    CHECK(store.doc().meter.den == 4);

    // Either one alone may be absent.
    json onlyFeel = json::parse(stateJson(source));
    onlyFeel["feel"] = {{"rhythm", "arpUp"}, {"strum", "off"}, {"strumSpeed", "medium"}, {"humanise", "off"}};
    onlyFeel.erase("meter");
    REQUIRE_MESSAGE(loadStateJson(store, onlyFeel.dump(), error), error);
    CHECK(store.doc().feel.rhythm == Rhythm::Arpeggio);
    CHECK(store.doc().meter.num == 4);
    json onlyMeter = json::parse(stateJson(source));
    onlyMeter["meter"] = {{"num", 6}, {"den", 8}};
    onlyMeter.erase("feel");
    REQUIRE_MESSAGE(loadStateJson(store, onlyMeter.dump(), error), error);
    CHECK(store.doc().feel == Feel{});
    CHECK(store.doc().meter.num == 6);
    CHECK(store.doc().meter.den == 8);
}

TEST_CASE("state: a present but invalid feel or meter rejects the whole state, history intact") {
    DocumentStore store = threeChords();
    store.setVelocity(1, 70);
    store.setVelocity(2, 71);
    store.undo();
    const Document before = store.doc();
    const std::pair<int, int> depth = historyDepth(store);
    const json good = json::parse(stateJson(threeChords()));

    std::vector<json> badFeel = {
        nullptr, 3, "block", json::array(), json::object(),
        json({{"rhythm", "block"}, {"strum", "off"}, {"strumSpeed", "medium"}}),                         // missing humanise
        json({{"rhythm", "waltz"}, {"strum", "off"}, {"strumSpeed", "medium"}, {"humanise", "off"}}),    // unknown name
        json({{"rhythm", nullptr}, {"strum", "off"}, {"strumSpeed", "medium"}, {"humanise", "off"}}),
        json({{"rhythm", json::object()}, {"strum", "off"}, {"strumSpeed", "medium"}, {"humanise", "off"}}),
        json({{"rhythm", "block"}, {"strum", json::array({"off"})}, {"strumSpeed", "medium"}, {"humanise", "off"}}),
        json({{"rhythm", "block"}, {"strum", "off"}, {"strumSpeed", 1}, {"humanise", "off"}}),
    };
    for (const json& f : badFeel) {
        json state = good;
        state["feel"] = f;
        std::string error;
        CHECK_FALSE(loadStateJson(store, state.dump(), error));
        CHECK(error.find("feel") != std::string::npos);
        CHECK(store.doc() == before);
        CHECK(historyDepth(store) == depth);
    }
    std::vector<json> badMeter = {
        nullptr, 4, "4/4", json::array({4, 4}), json::object(),
        json({{"num", 4}}),
        json({{"den", 4}}),
        json({{"num", 0}, {"den", 4}}),
        json({{"num", 4}, {"den", 3}}),
        json({{"num", 17}, {"den", 4}}),
        json({{"num", 3.5}, {"den", 4}}),
        json({{"num", 3.0}, {"den", 4}}),
        json({{"num", "3"}, {"den", 4}}),
        json({{"num", nullptr}, {"den", 4}}),
        json({{"num", json::object()}, {"den", 4}}),
    };
    for (const json& m : badMeter) {
        json state = good;
        state["meter"] = m;
        std::string error;
        CHECK_FALSE(loadStateJson(store, state.dump(), error));
        CHECK(error.find("meter") != std::string::npos);
        CHECK(store.doc() == before);
        CHECK(historyDepth(store) == depth);
    }
}

TEST_CASE("state: malformed arpeggio fields inside a saved feel reject the whole state") {
    DocumentStore store = threeChords();
    store.setVelocity(1, 70);
    const Document before = store.doc();
    const std::pair<int, int> depth = historyDepth(store);
    const json good = json::parse(stateJson(threeChords()));
    for (const char* field : {"arpPattern", "arpRate", "arpRange"}) {
        for (const json& wrong : {json("sideways"), json(nullptr), json(3)}) {
            json state = good;
            state["feel"][field] = wrong;
            std::string error;
            CHECK_FALSE(loadStateJson(store, state.dump(), error));
            CHECK(error.find(field) != std::string::npos);
            CHECK(store.doc() == before);
            CHECK(historyDepth(store) == depth);
        }
    }
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

TEST_CASE("setMidiThru: the command sets the live flag, no history, undo and redo keep it") {
    DocumentStore store = threeChords();
    CHECK(snapshot(store).at("midiThru") == true);
    applyOk(store, {{"cmd", "setMidiThru"}, {"on", false}});
    CHECK(snapshot(store).at("midiThru") == false);
    CHECK(store.doc().midiThru == false);
    applyOk(store, {{"cmd", "setMidiThru"}, {"on", false}});  // the same value: a no-op
    REQUIRE(store.undo());  // the three appendChords are the only history
    CHECK(snapshot(store).at("midiThru") == false);
    REQUIRE(store.undo());
    REQUIRE(store.undo());
    CHECK_FALSE(store.undo());
    CHECK(store.doc().midiThru == false);
    REQUIRE(store.redo());
    CHECK(snapshot(store).at("midiThru") == false);
    applyOk(store, {{"cmd", "setMidiThru"}, {"on", true}});
    CHECK(snapshot(store).at("midiThru") == true);
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
    checkRejected(store, {{"cmd", "setMidiThru"}, {"on", 1}});
    checkRejected(store, {{"cmd", "setMidiThru"}, {"on", "yes"}});
    checkRejected(store, {{"cmd", "setMidiThru"}});

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
    CHECK(state.at("transposeWithKey") == false);
    CHECK(state.at("keyHintSeen") == false);
    CHECK(state.at("newChordBeats").is_null());

    const json defaults = json::parse(stateJson(store));
    CHECK(defaults.at("editor") == json({{"width", 900}, {"height", 620}}));

    const json empty = json::parse(stateJson(DocumentStore{}));
    CHECK(empty.at("autoVoiceLead") == false);
    CHECK(empty.at("sections") == json::array());
    CHECK(empty.at("nextSectionId") == 1);
    CHECK(empty.at("relationMode") == "blend");
    CHECK(empty.at("transposeWithKey") == false);
    CHECK(empty.at("keyHintSeen") == false);
    CHECK(empty.at("newChordBeats").is_null());
}

TEST_CASE("state: transposeWithKey and keyHintSeen round-trip and a legacy state loads them false") {
    DocumentStore original = richStore();
    original.setTransposeWithKey(true);
    original.setKeyHintSeen(true);
    json j = json::parse(stateJson(original));
    CHECK(j.at("transposeWithKey") == true);
    CHECK(j.at("keyHintSeen") == true);

    DocumentStore loaded;
    std::string error;
    REQUIRE_MESSAGE(loadStateJson(loaded, j.dump(), error), error);
    CHECK(loaded.doc() == original.doc());
    CHECK(loaded.doc().transposeWithKey == true);
    CHECK(loaded.doc().keyHintSeen == true);

    j.erase("transposeWithKey");
    j.erase("keyHintSeen");
    loaded.setTransposeWithKey(true);
    loaded.setKeyHintSeen(true);
    REQUIRE_MESSAGE(loadStateJson(loaded, j.dump(), error), error);
    CHECK(loaded.doc().transposeWithKey == false);
    CHECK(loaded.doc().keyHintSeen == false);
}

TEST_CASE("state: midiThru round-trips as false and a state without it loads true") {
    DocumentStore original = richStore();
    CHECK(json::parse(stateJson(original)).at("midiThru") == true);
    original.setMidiThru(false);
    json j = json::parse(stateJson(original));
    CHECK(j.at("midiThru") == false);

    DocumentStore loaded;
    std::string error;
    REQUIRE_MESSAGE(loadStateJson(loaded, j.dump(), error), error);
    CHECK(loaded.doc() == original.doc());
    CHECK(loaded.doc().midiThru == false);

    j.erase("midiThru");
    loaded.setMidiThru(false);
    REQUIRE_MESSAGE(loadStateJson(loaded, j.dump(), error), error);
    CHECK(loaded.doc().midiThru == true);
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
    rejects("transposeWithKey not a bool", [](json& s) { s["transposeWithKey"] = "yes"; });
    rejects("keyHintSeen not a bool", [](json& s) { s["keyHintSeen"] = 1; });
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
    rejects("midiThru not a bool", [](json& s) { s["midiThru"] = 1; });
    rejects("midiThru a string", [](json& s) { s["midiThru"] = "yes"; });
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
    CHECK(error == "\"appendChord\" refused: no event ids left");  // its own message, not the store's reason
    CHECK(store.doc() == before);
    CHECK(store.nextId() == INT_MAX);
    CHECK_FALSE(store.undo());
}

TEST_CASE("boundary: an insert the id counter cannot serve is reported with its own message, and changes nothing") {
    DocumentStore store(INT_MAX - 1);  // one id left
    const Document before = store.doc();
    std::string error;
    const json c = {{"root", 0}, {"type", "maj"}};
    CHECK_FALSE(apply(store, {{"cmd", "insertChords"}, {"index", 0}, {"chords", json::array({c, c})}}, error));
    CHECK(error == "\"insertChords\" refused: not enough event ids left");
    CHECK(store.doc() == before);
    CHECK(store.nextId() == INT_MAX - 1);
    CHECK_FALSE(store.undo());
    applyOk(store, {{"cmd", "insertChords"}, {"index", 0}, {"chords", json::array({c})}});  // one id: enough
    CHECK(store.nextId() == INT_MAX);
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
    CHECK(error == "\"appendChord\" refused: the document already holds " + std::to_string(kMaxChords) + " chords");
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

/// The largest state the engine can produce: kMaxChords chords and kMaxTracks full tracks, every value at its
/// widest printing form, built as a document and installed with `replace` (the store's own ids start at 1):
/// - ids at the top of their ranges (chords, sections, tracks and notes at INT_MAX - n .. INT_MAX - 1, every counter
///   INT_MAX), so each prints ten digits;
/// - chords: maj7#11 (the longest type id) on root 11, octave -1, velocity 127, a length in [kMinLengthBeats, 1)
///   that needs all 17 significant digits (`kWidestLengthBeats`, 19 characters; a length of 1 beat or more has a
///   digit fewer after the point), a section on every one;
/// - names (sections and tracks) of `kMaxSectionNameBytes` / `kMaxTrackNameBytes` bytes of '\x01': both readers
///   accept control characters in a name, and JSON writes each as six bytes (`\u0001`), the widest escape there is
///   (`"` and `\` take two, other bytes one);
/// - notes: `kMaxNotesPerTrack` on each track, all anchored to the first chord and inactive (past its end), which
///   lifts the polyphony and onset caps (they count active notes only) and so allows the widest offsets and lengths:
///   offsets 10000000 + j (eight digits; the note domain ends at `kMaxNoteTick`, 15728640, so none has nine) and
///   lengths 5000000 (seven digits: an eight-digit offset leaves under 10000000 ticks, and an eight-digit length
///   leaves an offset of seven), pitches and velocities 100..127 (three digits);
/// - the top-level fields at their longest: Bb HarmonicMinor keys, the longest feel names, a 16/4 meter, the live
///   flags that default to true set false, the relation mode with the longest name, the longest new-chord length.
constexpr double kWidestLengthBeats = 0.30000000000000004;

DocumentStore worstCaseStore() {
    REQUIRE(json(kWidestLengthBeats).dump().size() == 19);
    REQUIRE(kWidestLengthBeats >= kMinLengthBeats);
    REQUIRE(json(kMaxNoteTick).dump().size() == 8);  // no nine-digit offset or length
    Document doc;
    doc.relationMode = RelationMode::VoiceLeading;
    doc.newChordBeats = 63.75;  // the longest value, five characters
    doc.projectKey = key('B', -1, Mode::HarmonicMinor);
    doc.browsedKey = doc.projectKey;
    doc.feel = Feel{Rhythm::LowThenUpper, Strum::DownUp, StrumSpeed::Medium, Humanise::Medium, ArpPattern::ConAndDiverge,
                    ArpRate::Subdivision, ArpRange::One};
    doc.meter = Meter{16, 4};
    doc.progressionLoop = false;  // false prints a character longer than true
    doc.followHost = false;
    doc.midiThru = false;
    std::string name;
    for (int i = 0; i < kMaxSectionNameBytes; ++i) {
        name.push_back('\x01');
    }
    for (int i = 0; i < kMaxChords; ++i) {
        doc.events.push_back(ChordEvent{INT_MAX - kMaxChords + i, chord(11, "maj7#11"), kWidestLengthBeats, 127, 0, -1, false});
        doc.sections.push_back(Section{INT_MAX - kMaxChords + i, name, 0, i});
    }
    std::string trackName;
    for (int i = 0; i < kMaxTrackNameBytes; ++i) {
        trackName.push_back('\x01');
    }
    for (int t = 0; t < kMaxTracks; ++t) {
        doc.tracks.push_back(Track{INT_MAX - kMaxTracks + t, trackName, TrackRole::Other, false, false, {}});
    }
    const int firstChord = INT_MAX - kMaxChords;
    int noteId = INT_MAX - kMaxTracks * kMaxNotesPerTrack;
    for (int t = 0; t < kMaxTracks; ++t) {
        for (int j = 0; j < kMaxNotesPerTrack; ++j) {
            doc.tracks[static_cast<std::size_t>(t)].notes.push_back(
                TrackNote{noteId++, 100 + (j + t) % 28, firstChord, 10000000 + j, 5000000, 100 + j % 28});
        }
    }
    REQUIRE(noteId == INT_MAX);
    std::string error;
    REQUIRE_MESSAGE(validateTracks(doc, INT_MAX, INT_MAX, error), error);
    DocumentStore store;
    store.replace(std::move(doc), INT_MAX, INT_MAX, INT_MAX, INT_MAX);
    return store;
}

/// `stateJson` of `worstCaseStore()` at the default editor size, measured; pinned so that a change to the
/// state's shape shows up here against `kMaxJsonBytes` (1 MiB) before it shows up as a state that will
/// not load.
constexpr std::size_t kWorstCaseStateBytes = 563624;

}  // namespace

TEST_CASE("capacity: the largest possible state is under kMaxJsonBytes and round-trips") {
    const DocumentStore original = worstCaseStore();
    REQUIRE(original.doc().events.size() == static_cast<std::size_t>(kMaxChords));
    REQUIRE(original.doc().sections.size() == static_cast<std::size_t>(kMaxChords));
    REQUIRE(original.doc().tracks.size() == static_cast<std::size_t>(kMaxTracks));
    for (const Track& t : original.doc().tracks) {
        REQUIRE(t.notes.size() == static_cast<std::size_t>(kMaxNotesPerTrack));
    }
    CHECK(trackPolyphony(original.doc()) == 0);  // every note inactive, as laid out
    CHECK(densestOnsetWindow(original.doc()) == 0);
    const std::string text = stateJson(original);
    MESSAGE("worst-case state: " << text.size() << " bytes");
    CHECK(text.size() == kWorstCaseStateBytes);
    CHECK(text.size() < kMaxJsonBytes);

    DocumentStore loaded;
    std::string error;
    REQUIRE_MESSAGE(loadStateJson(loaded, text, error), error);
    CHECK(loaded.doc() == original.doc());
    CHECK(loaded.nextId() == INT_MAX);
    CHECK(loaded.nextSectionId() == INT_MAX);
    CHECK(loaded.nextTrackId() == INT_MAX);
    CHECK(loaded.nextNoteId() == INT_MAX);
    CHECK(loaded.doc().sections.back().name.size() == static_cast<std::size_t>(kMaxSectionNameBytes));
    CHECK(loaded.doc().tracks.back().name.size() == static_cast<std::size_t>(kMaxTrackNameBytes));
    CHECK(stateJson(loaded) == text);
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
    CHECK(store.selectedEventIds() == std::vector<int>{4, 5, 6, 7});  // the preset's chords, the last the anchor
    CHECK(store.selectedEventId() == 7);
    applyOk(store, {{"cmd", "undo"}});
    CHECK(store.doc() == before);
}

TEST_CASE("loadPreset replace: chords and sections replaced, keys and live state kept, the loaded chords selected") {
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
    CHECK(store.selectedEventId() == 7);
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
        CHECK(error ==
              "\"loadPreset\" refused: the document would hold more than " + std::to_string(kMaxChords) + " chords");
        CHECK(store.doc() == before);
        CHECK(store.nextId() == nextId);
        applyOk(store, {{"cmd", "loadPreset"}, {"id", "axis"}, {"mode", "replace"}});  // 4 alone fit
        CHECK(store.doc().events.size() == 4);
    }
    SUBCASE("event ids") {
        DocumentStore store(INT_MAX - 3);
        std::string error;
        CHECK_FALSE(apply(store, {{"cmd", "loadPreset"}, {"id", "axis"}, {"mode", "replace"}}, error));
        CHECK(error == "\"loadPreset\" refused: not enough event ids left");
        CHECK(store.doc().events.empty());
        CHECK_FALSE(store.undo());
    }
    SUBCASE("section ids") {
        DocumentStore store;
        store.replace(Document{}, 1, INT_MAX);
        std::string error;
        CHECK_FALSE(apply(store, {{"cmd", "loadPreset"}, {"id", "axis"}, {"mode", "append"}}, error));
        CHECK(error == "\"loadPreset\" refused: not enough section ids left");
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
const std::vector<std::string> kCellMembers{"degree",  "tier",     "root",    "type",         "symbol",
                                            "roman",   "family",   "function", "tension",     "filled",
                                            "voicing", "voicingNames", "outOfKey", "outOfKeyNames"};

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
        // The out-of-key names are spelled as the symbol is: C#dim7 in C major lists C# and Bb, not Db.
        json names = json::array();
        for (const int pc : e.at("outOfKey").get<std::vector<int>>()) {
            names.push_back(chordToneSpelling(a.chord, pc, a.spellingKey).text());
        }
        CHECK(e.at("outOfKeyNames") == names);
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
                                        {"voicingNames", {"A4", "C#5", "E5", "G5"}},
                                        {"outOfKey", {1}},
                                        {"outOfKeyNames", {"C#"}}}));
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
                    CHECK(cells[i].at("relationText") == expectedText(snap, doc, population, i));
                }

                // Applied chords: the extras of the same call, their texts spelled in their own keys.
                std::vector<Chord> sized;
                for (const json& c : cells) {
                    sized.push_back(chord(c.at("root").get<int>(), c.at("type").get<std::string>()));
                }
                const std::vector<AppliedChord> appliedList = appliedChords(doc.browsedKey);
                std::vector<Chord> extras;
                for (const AppliedChord& a : appliedList) {
                    extras.push_back(a.chord);
                }
                const std::vector<Relatedness> all =
                    relatedness(reference, sized, extras, doc.projectKey, doc.browsedKey, doc.relationMode);
                const json& applied = snap.at("applied");
                REQUIRE(applied.size() == extras.size());
                for (std::size_t i = 0; i < extras.size(); ++i) {
                    const json& e = applied[i];
                    CHECK(e.at("relation") == all[sized.size() + i].score);
                    CHECK(e.at("relationText") == relationText(all[sized.size() + i].relation, appliedList[i].spellingKey));
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

TEST_CASE("snapshot: voicingNames and outOfKeyNames spell notes as the chord does, so a tip's lines agree") {
    DocumentStore store;
    applyOk(store, {{"cmd", "appendChord"}, {"root", 4}, {"type", "7"}});    // E7: E G# B D
    applyOk(store, {{"cmd", "appendChord"}, {"root", 5}, {"type", "min"}});  // Fm: F Ab C
    json snap = snapshot(store);
    const json e7 = snap.at("events")[0];
    CHECK(e7.at("voicing") == json({64, 68, 71, 74}));
    CHECK(e7.at("voicingNames") == json({"E4", "G#4", "B4", "D5"}));
    CHECK(e7.at("outOfKey") == json({8}));
    CHECK(e7.at("outOfKeyNames") == json({"G#"}));  // the Live run's tip said "Out of key: G♯" and "Ab"
    const json fm = snap.at("events")[1];
    CHECK(fm.at("outOfKeyNames") == json({"Ab"}));  // the same pitch class, read as Fm's third
    CHECK(fm.at("voicingNames") == json({"F4", "Ab4", "C5"}));

    // Palette cells are spelled in the browsed key and read against the project key.
    applyOk(store, {{"cmd", "browseKey"}, {"steps", 1}});  // G major
    snap = snapshot(store);
    const json fSharpDim = cellAt(snap, 6, 1);
    CHECK(fSharpDim.at("symbol") == "F#mb5");
    CHECK(fSharpDim.at("outOfKeyNames") == json({"F#"}));
    CHECK(fSharpDim.at("voicingNames") == json({"F#4", "A4", "C5"}));
    for (const json& cell : snap.at("palette").at("cells")) {
        CAPTURE(cell.dump());
        CHECK(cell.at("voicingNames").size() == cell.at("voicing").size());
        CHECK(cell.at("outOfKeyNames").size() == cell.at("outOfKey").size());
    }
    // Home triads: the project key's own spelling.
    REQUIRE(snap.at("homeTriads").size() == 7);
    CHECK(snap.at("homeTriads")[6].at("voicingNames") == json({"B4", "D5", "F5"}));
    CHECK(snap.at("homeTriads")[6].at("outOfKeyNames") == json::array());
}

TEST_CASE("why.commonNames and every common-tone text spell shared tones as the chord does: E7's G#") {
    DocumentStore store;
    applyOk(store, {{"cmd", "appendChord"}, {"root", 4}, {"type", "7"}});
    applyOk(store, {{"cmd", "appendChord"}, {"root", 4}, {"type", "7"}});
    applyOk(store, {{"cmd", "setRelationMode"}, {"mode", "commonTones"}});
    const json snap = snapshotSelecting(store, 2);
    const json why = snap.at("events")[1].at("why");
    CHECK(why.at("commonPcs") == json({2, 4, 8, 11}));
    CHECK(why.at("commonNames") == json({"D", "E", "G#", "B"}));
    CHECK(why.at("text") == "E7 → E7: same root, 4 common tones (D, E, G#, B), smooth voice leading (cost 0)");
    // The applied V7/vi cell is E7 too: its relation to the selected E7 (the Live run's tip) says G#.
    bool found = false;
    for (const json& entry : snap.at("applied")) {
        if (entry.at("label") == "V7/vi") {
            found = true;
            CHECK(entry.at("symbol") == "E7");
            CHECK(entry.at("relationText") == "4 common tones (D, E, G#, B) · same root · cost 0");
            CHECK(entry.at("outOfKeyNames") == json({"G#"}));
        }
    }
    CHECK(found);
}

TEST_CASE("appendChord: the new chord is selected, so the snapshot's selection and reference follow it") {
    DocumentStore store = cG7Am();
    CHECK(store.selectedEventId() == 3);  // the last append
    applyOk(store, {{"cmd", "setRelationMode"}, {"mode", "blend"}});
    applyOk(store, {{"cmd", "selectEvent"}, {"id", 1}});
    applyOk(store, {{"cmd", "appendChord"}, {"root", 5}, {"type", "maj"}});  // F, id 4
    CHECK(store.selectedEventId() == 4);
    const json snap = snapshotSelecting(store, store.selectedEventId());
    CHECK(snap.at("selectedEventId") == 4);
    CHECK(snap.at("relationReference") == json({{"eventId", 4}, {"symbol", "F"}}));
    applyOk(store, {{"cmd", "undo"}});
    CHECK(store.selectedEventId() == kNoEventId);
}

TEST_CASE("loadPreset append: the leading default section is named by count after sections came and went") {
    DocumentStore store = cG7Am();
    applyOk(store, {{"cmd", "addSection"}, {"index", 0}});
    applyOk(store, {{"cmd", "addSection"}, {"index", 2}});
    applyOk(store, {{"cmd", "removeSection"}, {"id", 2}});
    applyOk(store, {{"cmd", "removeSection"}, {"id", 1}});
    REQUIRE(store.doc().sections.empty());
    applyOk(store, {{"cmd", "loadPreset"}, {"id", "axis"}, {"mode", "append"}});
    const json sections = snapshot(store).at("sections");
    REQUIRE(sections.size() == 2);
    CHECK(sections[0] == json({{"id", 3}, {"name", "Section 1"}, {"colourIndex", 2}, {"firstEventIndex", 0}}));
    CHECK(sections[1].at("name") == "Axis");
}

TEST_CASE("snapshot: a cell's relationText spells its common tones as the cell's own symbol is spelled") {
    // Project C major, Bb selected, browsing Db major: the Gb cell shares Bb with the reference. The project
    // key would write the chord F# and the tone A#; the cell reads Gb, so its text and names say Bb.
    DocumentStore store;
    store.appendChord(chord(10, "maj"));
    store.setBrowsedKey(key('D', -1, Mode::Ionian));
    applyOk(store, {{"cmd", "setRelationMode"}, {"mode", "commonTones"}});
    json snap = selectedSnapshot(store);
    const json gb = cellAt(snap, 3, 1);
    REQUIRE(gb.at("symbol") == "Gb");
    CHECK(gb.at("relationText") == "1 common tone (Bb) · down a third · cost 2 · borrowed from C Locrian");
    CHECK(gb.at("voicingNames").is_array());
    // The home row (project key) and the Bb reference's own cell are unaffected.
    REQUIRE(snap.at("homeTriads").size() == 7);
    CHECK(snap.at("homeTriads")[0].at("relationText") == "no common tones · up a step · cost 6");

    // An applied cell spells in its own key: browsing G major, vii°7/iii is A#dim7 (spelled in B major), and
    // its tone shared with Bb reads A#, where the project key would say Bb.
    store.setBrowsedKey(key('G', 0, Mode::Ionian));
    snap = selectedSnapshot(store);
    bool found = false;
    for (const json& entry : snap.at("applied")) {
        if (entry.at("label") == "vii°7/iii") {
            found = true;
            CHECK(entry.at("symbol") == "A#dim7");
            CHECK(entry.at("relationText") == "1 common tone (A#) · same root · cost 5");
        }
    }
    CHECK(found);
}

TEST_CASE("appendChord with an inversion: the new selected event carries it, manual, one bar long") {
    DocumentStore store;
    applyOk(store, {{"cmd", "setMeter"}, {"num", 7}, {"den", 8}});
    applyOk(store, {{"cmd", "appendChord"}, {"root", 9}, {"type", "m7"}, {"inversion", 2}});
    REQUIRE(store.doc().events.size() == 1);
    const ChordEvent& e = store.doc().events[0];
    CHECK(e.inversion == 2);
    CHECK(e.manualVoicing);
    CHECK(e.lengthBeats == 3.5);  // one bar of 7/8
    CHECK(store.selectedEventId() == e.id);
    const json ev = eventWithId(snapshot(store), e.id);
    CHECK(ev.at("inversion") == 2);
    CHECK(ev.at("manualVoicing") == true);
    CHECK(ev.at("voicing") == json(voicing(chord(9, "m7"), 2, 0)));

    DocumentStore plain;
    applyOk(plain, {{"cmd", "appendChord"}, {"root", 9}, {"type", "m7"}});
    CHECK(plain.doc().events[0].inversion == 0);
    CHECK_FALSE(plain.doc().events[0].manualVoicing);
    DocumentStore zero;
    applyOk(zero, {{"cmd", "appendChord"}, {"root", 9}, {"type", "m7"}, {"inversion", 0}});
    CHECK(zero.doc().events[0].inversion == 0);
    CHECK(zero.doc().events[0].manualVoicing);
}

TEST_CASE("appendChord with an inversion: it compiles as given under auto voice-leading") {
    DocumentStore store;
    store.setAutoVoiceLead(true);
    applyOk(store, {{"cmd", "appendChord"}, {"root", 0}, {"type", "maj"}});
    applyOk(store, {{"cmd", "appendChord"}, {"root", 9}, {"type", "m7"}, {"inversion", 1}});
    const std::vector<std::vector<int>> chosen = voicings(store.doc());
    REQUIRE(chosen.size() == 2);
    CHECK(chosen[1] == voicing(chord(9, "m7"), 1, 0));
    // The register tells manual from voice-led: the voice-led Am7 after C is [60,64,67,69], also a C on the
    // bottom, while the manual first inversion is [72,76,79,81].
    CHECK(chosen[1].front() == 72);
    CHECK(eventWithId(snapshot(store), store.doc().events[1].id).at("voicing") == json(chosen[1]));
}

TEST_CASE("appendChord with an inversion: out of range or not an integer is rejected naming the field") {
    DocumentStore store = cG7Am();
    for (const json& bad : {json(4), json(-1), json("1"), json(1.5), json(nullptr), json(true)}) {
        const Document before = store.doc();
        std::string error;
        CAPTURE(bad.dump());
        CHECK_FALSE(apply(store, {{"cmd", "appendChord"}, {"root", 9}, {"type", "m7"}, {"inversion", bad}}, error));
        CHECK(error.find("inversion") != std::string::npos);
        CHECK(store.doc() == before);
    }
    checkRejected(store, {{"cmd", "appendChord"}, {"root", 0}, {"type", "maj"}, {"inversion", 3}});  // a triad has 3
    applyOk(store, {{"cmd", "appendChord"}, {"root", 9}, {"type", "m7"}, {"inversion", 3}});  // the last of four
}

TEST_CASE("appendChord with an inversion: one undo step, redo and the state round trip keep it") {
    DocumentStore store = cG7Am();
    const std::size_t before = store.doc().events.size();
    applyOk(store, {{"cmd", "appendChord"}, {"root", 9}, {"type", "m7"}, {"inversion", 2}});
    REQUIRE(store.doc().events.size() == before + 1);
    const int id = store.doc().events.back().id;
    const Document appended = store.doc();

    DocumentStore loaded;
    std::string error;
    REQUIRE_MESSAGE(loadStateJson(loaded, stateJson(store), error), error);
    CHECK(loaded.doc() == store.doc());
    CHECK(loaded.doc().events.back().inversion == 2);
    CHECK(loaded.doc().events.back().manualVoicing);

    applyOk(store, {{"cmd", "undo"}});
    CHECK(store.doc().events.size() == before);
    CHECK(store.findIndex(id) < 0);
    applyOk(store, {{"cmd", "redo"}});
    CHECK(store.doc() == appended);
    CHECK(store.doc().events.back().inversion == 2);
    CHECK(store.doc().events.back().manualVoicing);
}

// --- heardJson -----------------------------------------------------------------------------

namespace {

// 120 bpm at 48 kHz: one beat is 24000 samples.
constexpr double kHeardBpm = 120.0;
constexpr double kHeardRate = 48000.0;

std::uint64_t heardAt(double beat) {
    return static_cast<std::uint64_t>(beat * 24000.0);
}

void heardOn(Listener& l, int pitch, double beat) {
    l.feed({HeardEvent::NoteOn, static_cast<std::uint8_t>(pitch), 100, heardAt(beat)}, kHeardBpm, kHeardRate);
}

void heardOff(Listener& l, int pitch, double beat) {
    l.feed({HeardEvent::NoteOff, static_cast<std::uint8_t>(pitch), 0, heardAt(beat)}, kHeardBpm, kHeardRate);
}

/// Holds `pitches` from beat 0.
Listener holding(const std::vector<int>& pitches) {
    Listener l;
    for (int p : pitches) {
        heardOn(l, p, 0);
    }
    return l;
}

/// The C major scale played note by note, each released as the next starts: the listener settles on C Ionian.
Listener heardCMajorScale() {
    Listener l;
    const std::vector<int> scale{60, 62, 64, 65, 67, 69, 71, 72};
    for (std::size_t i = 0; i < scale.size(); ++i) {
        const double beat = static_cast<double>(i);
        heardOn(l, scale[i], beat);
        heardOff(l, scale[i], beat + 1);
        l.advance(heardAt(beat + 1), kHeardBpm, kHeardRate);
    }
    return l;
}

std::vector<std::string> heldNames(const json& j) {
    return j.at("heldNames").get<std::vector<std::string>>();
}

}  // namespace

TEST_CASE("heardJson: a fresh listener is inactive with nothing heard") {
    const Listener l;
    const json j = json::parse(heardJson(l, key('C', 0, Mode::Ionian)));
    CHECK(j["schema"] == 1);
    CHECK(j["active"] == false);
    CHECK(j["held"] == json::array());
    CHECK(j["heldNames"] == json::array());
    CHECK(j["chord"].is_null());
    CHECK(j["chordHeld"] == false);
    CHECK(j["key"].is_null());
    CHECK(j["onsets"] == 0);
}

TEST_CASE("heardJson: the top-level members are exactly the wire's") {
    const Listener l = holding({57, 60, 64, 67});
    const json j = json::parse(heardJson(l, key('C', 0, Mode::Ionian)));
    std::vector<std::string> keys;
    for (auto it = j.begin(); it != j.end(); ++it) {
        keys.push_back(it.key());
    }
    std::sort(keys.begin(), keys.end());
    CHECK(keys == std::vector<std::string>{"active", "chord", "chordHeld", "held", "heldNames", "key", "onsets",
                                           "schema"});
    std::vector<std::string> chordKeys;
    for (auto it = j["chord"].begin(); it != j["chord"].end(); ++it) {
        chordKeys.push_back(it.key());
    }
    std::sort(chordKeys.begin(), chordKeys.end());
    CHECK(chordKeys == std::vector<std::string>{"bass", "inversion", "roman", "root", "symbol", "type"});
}

TEST_CASE("heardJson: Am7 held in C major") {
    const Listener l = holding({57, 60, 64, 67});
    const json j = json::parse(heardJson(l, key('C', 0, Mode::Ionian)));
    CHECK(j["active"] == true);
    CHECK(j["held"] == json::array({57, 60, 64, 67}));
    CHECK(heldNames(j) == std::vector<std::string>{"A", "C", "E", "G"});
    CHECK(j["chord"] == json{{"root", 9}, {"type", "m7"}, {"symbol", "Am7"}, {"roman", "vi7"}, {"inversion", 0},
                             {"bass", 57}});
    CHECK(j["chordHeld"] == true);
    CHECK(j["key"].is_null());
    CHECK(j["onsets"] == 4);
}

TEST_CASE("heardJson: E7 spells its third's neighbour G# in C major") {
    const Listener l = holding({64, 68, 71, 74});
    const json j = json::parse(heardJson(l, key('C', 0, Mode::Ionian)));
    CHECK(heldNames(j) == std::vector<std::string>{"E", "G#", "B", "D"});
    CHECK(j["chord"]["symbol"] == "E7");
    CHECK(j["chordHeld"] == true);
}

TEST_CASE("heardJson: a lone held note is spelled by the project key") {
    const Listener l = holding({68});
    const json j = json::parse(heardJson(l, key('C', 0, Mode::Ionian)));
    CHECK(j["active"] == true);
    CHECK(j["chord"].is_null());
    CHECK(heldNames(j) == std::vector<std::string>{"Ab"});
}

TEST_CASE("heardJson: the heard key names the tonic and mode; the project key only spells") {
    const Listener l = heardCMajorScale();
    for (const Key& project : {key('C', 0, Mode::Ionian), key('D', 0, Mode::Ionian)}) {
        const json j = json::parse(heardJson(l, project));
        REQUIRE(j["key"].is_object());
        CHECK(j["key"]["tonic"] == json{{"letter", "C"}, {"alter", 0}});
        CHECK(j["key"]["mode"] == "Ionian");
        CHECK(j["key"]["name"] == "C Ionian");
        CHECK(j["key"]["percent"].is_number_integer());
        // The model's r_s for the 8-note scale is 0.901352, so lround(100 * r) = 90.
        CHECK(j["key"]["percent"].get<int>() == 90);
        CHECK(j["key"]["percent"].get<int>() >= 0);
        CHECK(j["key"]["percent"].get<int>() <= 100);
        std::vector<std::string> keyMembers;
        for (auto it = j["key"].begin(); it != j["key"].end(); ++it) {
            keyMembers.push_back(it.key());
        }
        std::sort(keyMembers.begin(), keyMembers.end());
        CHECK(keyMembers == std::vector<std::string>{"mode", "name", "percent", "tonic"});
        std::vector<std::string> tonicMembers;
        for (auto it = j["key"]["tonic"].begin(); it != j["key"]["tonic"].end(); ++it) {
            tonicMembers.push_back(it.key());
        }
        std::sort(tonicMembers.begin(), tonicMembers.end());
        CHECK(tonicMembers == std::vector<std::string>{"alter", "letter"});
        CHECK(j["onsets"] == 8);
    }
}

TEST_CASE("heardJson: a shown key with a negative correlation reads 0 percent") {
    // C major acquired, then 40 beats later a lone F# for one beat: the window holds only F#, C Ionian's r is
    // defined and negative (-0.2297) and fails retain once, so it is still shown for this tick.
    Listener l = heardCMajorScale();
    heardOn(l, 66, 48);
    l.advance(heardAt(49), kHeardBpm, kHeardRate);
    REQUIRE(l.key().has_value());
    REQUIRE(l.key()->r < 0.0);
    CHECK(l.key()->r == doctest::Approx(-0.229749).epsilon(1e-5));
    const json j = json::parse(heardJson(l, key('C', 0, Mode::Ionian)));
    REQUIRE(j["key"].is_object());
    CHECK(j["key"]["name"] == "C Ionian");
    CHECK(j["key"]["percent"].get<int>() == 0);  // clamped: lround(100 * r) would be -23
}

TEST_CASE("heardJson: after the hands lift the chord stays, unheld, and loose notes use the key's spelling") {
    Listener l = holding({57, 60, 64, 67});
    heardOff(l, 57, 1);
    heardOff(l, 60, 1);
    heardOff(l, 64, 1);
    json j = json::parse(heardJson(l, key('C', 0, Mode::Ionian)));
    CHECK(j["chord"]["symbol"] == "Am7");
    CHECK(j["chordHeld"] == false);
    CHECK(j["held"] == json::array({67}));
    CHECK(heldNames(j) == std::vector<std::string>{"G"});

    // E7 with its root lifted is no longer held as a chord, so its G# reads by spell(): Ab in C major.
    Listener e = holding({64, 68, 71, 74});
    heardOff(e, 64, 1);
    j = json::parse(heardJson(e, key('C', 0, Mode::Ionian)));
    CHECK(j["chordHeld"] == false);
    CHECK(heldNames(j) == std::vector<std::string>{"Ab", "B", "D"});
}

TEST_CASE("heardJson: a held non-chord-tone reads by spell() while chord tones keep the chord's spelling") {
    // E7 plus C#/Db (61): the chord stays latched and held, 61 is not one of its tones.
    const Listener l = holding({64, 68, 71, 74, 61});
    const json c = json::parse(heardJson(l, key('C', 0, Mode::Ionian)));
    CHECK(c["chordHeld"] == true);
    CHECK(c["chord"]["symbol"] == "E7");
    CHECK(heldNames(c) == std::vector<std::string>{"Db", "E", "G#", "B", "D"});
    const json d = json::parse(heardJson(l, key('D', 0, Mode::Ionian)));
    CHECK(d["chordHeld"] == true);
    CHECK(d["chord"]["symbol"] == "E7");
    CHECK(heldNames(d) == std::vector<std::string>{"C#", "E", "G#", "B", "D"});
}

TEST_CASE("heardJson: the project key changes the spellings, never the heard key") {
    Listener l = heardCMajorScale();
    heardOn(l, 61, 9);
    heardOn(l, 66, 9);
    struct Row {
        Key project;
        std::vector<std::string> names;
        const char* symbol;
        const char* roman;
    };
    const std::vector<Row> rows{{key('C', 0, Mode::Ionian), {"C#", "F#"}, "F#5", "#IV5"},
                                {key('D', 0, Mode::Ionian), {"C#", "F#"}, "F#5", "III5"},
                                {key('F', 0, Mode::Ionian), {"Db", "Gb"}, "Gb5", "bII5"}};
    for (const Row& row : rows) {
        const json j = json::parse(heardJson(l, row.project));
        CHECK(heldNames(j) == row.names);
        CHECK(j["chord"]["symbol"] == row.symbol);
        CHECK(j["chord"]["roman"] == row.roman);
        CHECK(j["key"]["name"] == "C Ionian");
    }
}

TEST_CASE("heardJson: held is ascending whatever the input order") {
    const Listener l = holding({67, 57, 64, 60});
    const json j = json::parse(heardJson(l, key('C', 0, Mode::Ionian)));
    CHECK(j["held"] == json::array({57, 60, 64, 67}));
    CHECK(heldNames(j) == std::vector<std::string>{"A", "C", "E", "G"});
}

// --- Strip editing: the new-chord length in the state -------------------------------------------------------

TEST_CASE("state: newChordBeats round-trips as a number, null by default; a state without it loads null") {
    DocumentStore original = richStore();
    CHECK(json::parse(stateJson(original)).at("newChordBeats").is_null());
    original.setNewChordLength(1.5);
    json j = json::parse(stateJson(original));
    CHECK(j.at("newChordBeats") == 1.5);

    DocumentStore loaded;
    std::string error;
    REQUIRE_MESSAGE(loadStateJson(loaded, j.dump(), error), error);
    CHECK(loaded.doc() == original.doc());
    CHECK(loaded.doc().newChordBeats == std::optional<double>(1.5));

    j.erase("newChordBeats");
    REQUIRE_MESSAGE(loadStateJson(loaded, j.dump(), error), error);
    CHECK_FALSE(loaded.doc().newChordBeats.has_value());
    j["newChordBeats"] = nullptr;
    loaded.setNewChordLength(2.0);
    REQUIRE_MESSAGE(loadStateJson(loaded, j.dump(), error), error);
    CHECK_FALSE(loaded.doc().newChordBeats.has_value());
    j["newChordBeats"] = 1.3;  // snapped, as the setter stores it
    REQUIRE_MESSAGE(loadStateJson(loaded, j.dump(), error), error);
    CHECK(loaded.doc().newChordBeats == std::optional<double>(1.25));
    j["newChordBeats"] = 500;  // clamped
    REQUIRE_MESSAGE(loadStateJson(loaded, j.dump(), error), error);
    CHECK(loaded.doc().newChordBeats == std::optional<double>(64.0));
}

TEST_CASE("state: a mistyped newChordBeats rejects the whole state, naming the field") {
    DocumentStore store = threeChords();
    json j = json::parse(stateJson(store));
    for (const json& wrong : {json("1.5"), json(true), json::object(), json::array()}) {
        CAPTURE(wrong.dump());
        j["newChordBeats"] = wrong;
        checkStateRejected(store, j.dump());
        std::string error;
        CHECK_FALSE(loadStateJson(store, j.dump(), error));
        CHECK(error.find("newChordBeats") != std::string::npos);
    }
}

TEST_CASE("appendChord: without a length, the new-chord length once one is set") {
    DocumentStore store;
    store.setNewChordLength(0.75);
    applyOk(store, {{"cmd", "appendChord"}, {"root", 0}, {"type", "maj"}});
    applyOk(store, {{"cmd", "appendChord"}, {"root", 5}, {"type", "maj"}, {"lengthBeats", 2.0}});
    CHECK(store.doc().events[0].lengthBeats == 0.75);
    CHECK(store.doc().events[1].lengthBeats == 2.0);
}

// --- Strip editing: insertChords, removeChords, setLengths, selectEvents, setNewChordLength ----------------

namespace {

/// One `insertChords` entry: `root` and `type` plus the members of `extra`.
json entry(int root, const char* type, json extra = json::object()) {
    extra["root"] = root;
    extra["type"] = type;
    return extra;
}

json insertCmd(json index, json chords) {
    return json{{"cmd", "insertChords"}, {"index", std::move(index)}, {"chords", std::move(chords)}};
}

/// The length of every event of the snapshot, in order.
std::vector<double> snapshotLengths(const json& snap) {
    std::vector<double> lengths;
    for (const json& e : snap.at("events")) {
        lengths.push_back(e.at("lengthBeats").get<double>());
    }
    return lengths;
}

}  // namespace

TEST_CASE("insertChords: the entries land at the index in order, one undo step, selected with the last as anchor") {
    DocumentStore store = threeChords();  // C 1, F 2, G7 3
    const std::pair<int, int> depth = historyDepth(store);
    applyOk(store, insertCmd(1, json::array({entry(9, "min", {{"lengthBeats", 2.0}}), entry(2, "min", {{"velocity", 80}})})));
    const json snap = selectedSnapshot(store);
    CHECK(snapshotIds(snap) == std::vector<int>{1, 4, 5, 2, 3});
    CHECK(snapshotLengths(snap) == std::vector<double>{4.0, 2.0, 4.0, 4.0, 4.0});  // no length: one bar
    CHECK(snap.at("events").at(1).at("symbol") == "Am");
    CHECK(snap.at("events").at(2).at("velocity") == 80);
    CHECK(snap.at("selectedEventIds") == json::array({4, 5}));
    CHECK(snap.at("selectedEventId") == 5);
    CHECK(historyDepth(store) == std::make_pair(depth.first + 1, 0));
    applyOk(store, {{"cmd", "undo"}});
    CHECK(snapshotIds(snapshot(store)) == std::vector<int>{1, 2, 3});
    CHECK(store.selectedEventIds().empty());
}

TEST_CASE("insertChords: voicing fields; a given inversion or octave marks the chord manual unless told otherwise") {
    DocumentStore store;
    applyOk(store, insertCmd(0, json::array({entry(0, "maj", {{"inversion", 1}}),
                                             entry(0, "maj", {{"octave", -1}}),
                                             entry(0, "maj", {{"inversion", 2}, {"manualVoicing", false}}),
                                             entry(0, "maj", {{"manualVoicing", true}}),
                                             entry(0, "maj")})));
    const auto& e = store.doc().events;
    REQUIRE(e.size() == 5);
    CHECK(e[0] == ChordEvent{1, kC, 4.0, 100, 1, 0, true});
    CHECK(e[1] == ChordEvent{2, kC, 4.0, 100, 0, -1, true});
    CHECK(e[2] == ChordEvent{3, kC, 4.0, 100, 2, 0, false});
    CHECK(e[3] == ChordEvent{4, kC, 4.0, 100, 0, 0, true});
    CHECK(e[4] == ChordEvent{5, kC, 4.0, 100, 0, 0, false});
}

TEST_CASE("insertChords: one bad entry rejects the whole message, names chords[i].field, and keeps the history") {
    DocumentStore store = threeChords();
    const json good = entry(5, "maj");
    struct Bad {
        json third;
        const char* names;
    };
    const Bad cases[] = {
        {entry(-1, "maj"), "chords[2].root"},
        {entry(12, "maj"), "chords[2].root"},
        {json({{"root", "0"}, {"type", "maj"}}), "chords[2].root"},
        {json({{"root", 1.5}, {"type", "maj"}}), "chords[2].root"},
        {json({{"type", "maj"}}), "chords[2].root"},
        {entry(0, "nope"), "chords[2].type"},
        {json({{"root", 0}, {"type", 7}}), "chords[2].type"},
        {json({{"root", 0}}), "chords[2].type"},
        {entry(0, "maj", {{"lengthBeats", "4"}}), "chords[2].lengthBeats"},
        {entry(0, "maj", {{"lengthBeats", nullptr}}), "chords[2].lengthBeats"},
        {entry(0, "maj", {{"velocity", 1.5}}), "chords[2].velocity"},
        {entry(0, "maj", {{"velocity", "100"}}), "chords[2].velocity"},
        {entry(0, "maj", {{"inversion", 3}}), "chords[2].inversion"},  // a triad has inversions 0..2
        {entry(0, "maj", {{"inversion", -1}}), "chords[2].inversion"},
        {entry(0, "maj", {{"inversion", "1"}}), "chords[2].inversion"},
        {entry(0, "maj", {{"octave", 2}}), "chords[2].octave"},
        {entry(0, "maj", {{"octave", -2}}), "chords[2].octave"},
        {entry(0, "maj", {{"octave", 0.5}}), "chords[2].octave"},
        {entry(0, "maj", {{"manualVoicing", 1}}), "chords[2].manualVoicing"},
        {entry(0, "maj", {{"manualVoicing", "yes"}}), "chords[2].manualVoicing"},
        {json(5), "chords[2]"},
        {json::array(), "chords[2]"},
    };
    for (const Bad& bad : cases) {
        const json cmd = insertCmd(1, json::array({good, good, bad.third}));
        checkRejectedKeepsHistory(store, cmd);
        std::string error;
        CHECK_FALSE(apply(store, cmd, error));
        CAPTURE(error);
        CHECK(error.find(bad.names) != std::string::npos);
    }
    checkRejectedKeepsHistory(store, {{"cmd", "insertChords"}, {"chords", json::array({good})}});  // no index
    checkRejectedKeepsHistory(store, insertCmd("1", json::array({good})));
    checkRejectedKeepsHistory(store, insertCmd(1.5, json::array({good})));
    checkRejectedKeepsHistory(store, {{"cmd", "insertChords"}, {"index", 1}});  // no chords
    checkRejectedKeepsHistory(store, insertCmd(1, json::array()));
    checkRejectedKeepsHistory(store, insertCmd(1, good));  // an object, not an array
    checkRejectedKeepsHistory(store, insertCmd(1, json(std::vector<json>(kMaxChords + 1, good))));
    CHECK(store.nextId() == 4);  // no id consumed
}

TEST_CASE("insertChords: refused whole past kMaxChords, with the reason; exactly kMaxChords is allowed") {
    DocumentStore store;
    std::string error;
    REQUIRE_MESSAGE(loadStateJson(store, stateWithChords(kMaxChords - 2), error), error);
    const json three = json::array({entry(0, "maj"), entry(5, "maj"), entry(7, "7")});
    CHECK_FALSE(apply(store, insertCmd(0, three), error));
    CHECK(error == "\"insertChords\" refused: the document would hold more than " + std::to_string(kMaxChords) +
                       " chords");
    CHECK(store.doc().events.size() == static_cast<std::size_t>(kMaxChords) - 2);
    CHECK(store.nextId() == kMaxChords - 1);
    applyOk(store, insertCmd(0, json::array({entry(0, "maj"), entry(5, "maj")})));
    CHECK(store.doc().events.size() == static_cast<std::size_t>(kMaxChords));
    CHECK_FALSE(apply(store, insertCmd(0, json::array({entry(0, "maj")})), error));
}

TEST_CASE("removeChords: the listed chords that exist, as removeChord would one after another, in one undo step") {
    DocumentStore store = threeChords();
    applyOk(store, {{"cmd", "appendChord"}, {"root", 0}, {"type", "maj"}});  // id 4
    applyOk(store, {{"cmd", "addSection"}, {"index", 0}, {"name", "A"}});
    applyOk(store, {{"cmd", "addSection"}, {"index", 2}, {"name", "B"}});
    DocumentStore sequential = store;
    const std::pair<int, int> depth = historyDepth(store);
    applyOk(store, {{"cmd", "removeChords"}, {"ids", json::array({3, 99, 1, 3})}});
    sequential.removeChord(1);
    sequential.removeChord(3);
    CHECK(snapshotIds(snapshot(store)) == std::vector<int>{2, 4});
    CHECK(store.doc().sections == sequential.doc().sections);
    CHECK(historyDepth(store).first == depth.first + 1);
    applyOk(store, {{"cmd", "undo"}});
    CHECK(snapshotIds(snapshot(store)) == std::vector<int>{1, 2, 3, 4});
}

TEST_CASE("removeChords / setLengths: no id that exists, or nothing to change, is a valid no-op without history") {
    DocumentStore store = threeChords();
    const Document before = store.doc();
    const std::pair<int, int> depth = historyDepth(store);
    applyOk(store, {{"cmd", "removeChords"}, {"ids", json::array({98, 99})}});
    applyOk(store, {{"cmd", "setLengths"}, {"ids", json::array({98})}, {"lengthBeats", 1.0}});
    applyOk(store, {{"cmd", "setLengths"}, {"ids", json::array({1})}, {"lengthBeats", 4.0}});  // already
    CHECK(store.doc() == before);
    CHECK(historyDepth(store) == depth);
}

TEST_CASE("setLengths: every listed chord, clamped, one undo step each call") {
    DocumentStore store = threeChords();
    applyOk(store, {{"cmd", "setLengths"}, {"ids", json::array({1, 3, 3})}, {"lengthBeats", 0.1}});
    CHECK(snapshotLengths(snapshot(store)) == std::vector<double>{0.25, 4.0, 0.25});
    applyOk(store, {{"cmd", "setLengths"}, {"ids", json::array({2})}, {"lengthBeats", 1000}});
    CHECK(snapshotLengths(snapshot(store)) == std::vector<double>{0.25, 64.0, 0.25});
    applyOk(store, {{"cmd", "undo"}});
    applyOk(store, {{"cmd", "undo"}});
    CHECK(snapshotLengths(snapshot(store)) == std::vector<double>{4.0, 4.0, 4.0});
}

TEST_CASE("rejection: removeChords and setLengths need a non-empty array of integer ids, setLengths a finite length") {
    DocumentStore store = threeChords();
    for (const char* name : {"removeChords", "setLengths"}) {
        CAPTURE(name);
        const auto withIds = [&](json ids) {
            json c{{"cmd", name}, {"ids", std::move(ids)}};
            if (std::string(name) == "setLengths") {
                c["lengthBeats"] = 1.0;
            }
            return c;
        };
        json missing = withIds(json::array({1}));
        missing.erase("ids");
        checkRejectedKeepsHistory(store, missing);
        checkRejectedKeepsHistory(store, withIds(json::array()));
        checkRejectedKeepsHistory(store, withIds(1));
        checkRejectedKeepsHistory(store, withIds(nullptr));
        checkRejectedKeepsHistory(store, withIds(json::array({1, "2"})));
        checkRejectedKeepsHistory(store, withIds(json::array({1.5})));
        std::string error;
        CHECK_FALSE(apply(store, withIds(json::array({1, true})), error));
        CHECK(error.find("ids[1]") != std::string::npos);
    }
    checkRejectedKeepsHistory(store, {{"cmd", "setLengths"}, {"ids", json::array({1})}});
    checkRejectedKeepsHistory(store, {{"cmd", "setLengths"}, {"ids", json::array({1})}, {"lengthBeats", "1"}});
    checkRejectedKeepsHistory(store, {{"cmd", "setLengths"}, {"ids", json::array({1})}, {"lengthBeats", nullptr}});
}

TEST_CASE("selectEvents: unknown ids dropped, strip order, the anchor or the last; never history") {
    DocumentStore store = threeChords();
    const std::pair<int, int> depth = historyDepth(store);
    applyOk(store, {{"cmd", "selectEvents"}, {"ids", json::array({3, 1, 99, 1})}, {"anchor", 1}});
    CHECK(store.selectedEventIds() == std::vector<int>{1, 3});
    CHECK(store.selectedEventId() == 1);
    applyOk(store, {{"cmd", "selectEvents"}, {"ids", json::array({1, 2})}});
    CHECK(store.selectedEventId() == 2);
    applyOk(store, {{"cmd", "selectEvents"}, {"ids", json::array({1, 2})}, {"anchor", nullptr}});
    CHECK(store.selectedEventId() == 2);
    applyOk(store, {{"cmd", "selectEvents"}, {"ids", json::array({1, 2})}, {"anchor", 3}});
    CHECK(store.selectedEventId() == 2);
    applyOk(store, {{"cmd", "selectEvents"}, {"ids", json::array()}});
    CHECK(store.selectedEventIds().empty());
    CHECK(store.selectedEventId() == kNoEventId);
    CHECK(historyDepth(store) == depth);
}

TEST_CASE("rejection: selectEvents needs an array of integer ids and an integer or null anchor") {
    DocumentStore store = threeChords();
    store.setSelectedEvents({1, 2}, 1);
    checkRejectedKeepsHistory(store, {{"cmd", "selectEvents"}});
    checkRejectedKeepsHistory(store, {{"cmd", "selectEvents"}, {"ids", 1}});
    checkRejectedKeepsHistory(store, {{"cmd", "selectEvents"}, {"ids", json::array({true})}});
    checkRejectedKeepsHistory(store, {{"cmd", "selectEvents"}, {"ids", json::array({1})}, {"anchor", "1"}});
    checkRejectedKeepsHistory(store, {{"cmd", "selectEvents"}, {"ids", json::array({1})}, {"anchor", 1.5}});
    CHECK(store.selectedEventIds() == std::vector<int>{1, 2});  // a rejected message leaves the selection alone
    CHECK(store.selectedEventId() == 1);
}

TEST_CASE("snapshot: selectedEventIds is the selection in strip order; selectedEventId is its anchor") {
    DocumentStore store = threeChords();
    applyOk(store, {{"cmd", "selectEvents"}, {"ids", json::array({3, 1})}, {"anchor", 1}});
    json snap = selectedSnapshot(store);
    CHECK(snap.at("selectedEventIds") == json::array({1, 3}));
    CHECK(snap.at("selectedEventId") == 1);
    CHECK(snap.at("relationReference").at("eventId") == 1);  // the suggestions and relations follow the anchor
    applyOk(store, {{"cmd", "moveChord"}, {"id", 3}, {"index", 0}});
    CHECK(selectedSnapshot(store).at("selectedEventIds") == json::array({3, 1}));
    applyOk(store, {{"cmd", "selectEvent"}, {"id", nullptr}});
    snap = selectedSnapshot(store);
    CHECK(snap.at("selectedEventIds") == json::array());
    CHECK(snap.at("selectedEventId").is_null());
}

TEST_CASE("snapshot: the selection and its anchor are normalised together, so a selection always has its anchor") {
    DocumentStore store = threeChords();
    applyOk(store, {{"cmd", "moveChord"}, {"id", 3}, {"index", 0}});  // strip order 3, 1, 2
    const Document& d = store.doc();
    const Palette palette = buildPalette(d.browsedKey, d.projectKey);
    // The anchor alone (a caller that passes no ids): listed.
    json snap = json::parse(snapshotJson(d, palette, 2));
    CHECK(snap.at("selectedEventIds") == json::array({2}));
    CHECK(snap.at("selectedEventId") == 2);
    // A stale anchor with a surviving selection: unknown ids dropped, the last surviving block is the anchor,
    // and the suggestions and relations follow it.
    snap = json::parse(snapshotJson(d, palette, 99, {99, 2}));
    CHECK(snap.at("selectedEventIds") == json::array({2}));
    CHECK(snap.at("selectedEventId") == 2);
    CHECK(snap.at("relationReference").at("eventId") == 2);
    // No anchor at all: the last selected block in strip order.
    snap = json::parse(snapshotJson(d, palette, kNoEventId, {1, 3}));
    CHECK(snap.at("selectedEventIds") == json::array({3, 1}));
    CHECK(snap.at("selectedEventId") == 1);
    CHECK(snap.at("relationReference").at("eventId") == 1);
    // An anchor that names a block outside the selection: the selection wins, its last block is the anchor.
    snap = json::parse(snapshotJson(d, palette, 2, {3, 1}));
    CHECK(snap.at("selectedEventIds") == json::array({3, 1}));
    CHECK(snap.at("selectedEventId") == 1);
    // Nothing that names an event: no selection, no anchor.
    snap = json::parse(snapshotJson(d, palette, 99, {98}));
    CHECK(snap.at("selectedEventIds") == json::array());
    CHECK(snap.at("selectedEventId").is_null());
}

TEST_CASE("setNewChordLength: a number or null, snapped, live, used by appendChord and insertChords, in the snapshot") {
    DocumentStore store;
    CHECK(snapshot(store).at("newChordBeats").is_null());
    applyOk(store, {{"cmd", "setNewChordLength"}, {"lengthBeats", 1.3}});
    CHECK(snapshot(store).at("newChordBeats") == 1.25);
    applyOk(store, {{"cmd", "appendChord"}, {"root", 0}, {"type", "maj"}});
    applyOk(store, insertCmd(1, json::array({entry(5, "maj"), entry(7, "7", {{"lengthBeats", 2.0}})})));
    CHECK(snapshotLengths(snapshot(store)) == std::vector<double>{1.25, 1.25, 2.0});
    CHECK(historyDepth(store) == std::make_pair(2, 0));  // the two edits, not the setting
    applyOk(store, {{"cmd", "undo"}});
    CHECK(snapshot(store).at("newChordBeats") == 1.25);  // live state: kept
    applyOk(store, {{"cmd", "setNewChordLength"}, {"lengthBeats", nullptr}});
    CHECK(snapshot(store).at("newChordBeats").is_null());
    applyOk(store, {{"cmd", "appendChord"}, {"root", 9}, {"type", "min"}});
    CHECK(snapshotLengths(snapshot(store)).back() == 4.0);
    for (const json& wrong : {json("1"), json(true), json::object(), json::array()}) {
        checkRejectedKeepsHistory(store, {{"cmd", "setNewChordLength"}, {"lengthBeats", wrong}});
    }
    checkRejectedKeepsHistory(store, {{"cmd", "setNewChordLength"}});
}

// --- Document changed: the host's modified flag -----------------------------------------------------------

namespace {

/// Sends `cmd` (schema added), requires it to be accepted and returns its `documentChanged` flag.
bool changes(DocumentStore& store, json cmd) {
    cmd["schema"] = kJsonSchema;
    std::string error;
    bool changed = true;  // the call must set it either way
    const bool ok = applyCommandJson(store, cmd.dump(), error, &changed);
    REQUIRE_MESSAGE(ok, error);
    return changed;
}

}  // namespace

TEST_CASE("applyCommandJson: documentChanged says whether the document differs from before the command") {
    DocumentStore store = threeChords();

    SUBCASE("an edit that changes the document") {
        CHECK(changes(store, {{"cmd", "setLength"}, {"id", 1}, {"lengthBeats", 2.0}}));
        CHECK(changes(store, {{"cmd", "appendChord"}, {"root", 9}, {"type", "min"}}));
        CHECK(changes(store, {{"cmd", "removeChord"}, {"id", 2}}));
        CHECK(changes(store, {{"cmd", "setKey"}, {"letter", "G"}, {"alter", 0}, {"mode", "Ionian"}}));
        CHECK(changes(store, {{"cmd", "setInversion"}, {"id", 1}, {"inversion", 1}}));
    }
    SUBCASE("an accepted no-op: the value already held, an id no event has, a move to the current place") {
        CHECK_FALSE(changes(store, {{"cmd", "setLength"}, {"id", 1}, {"lengthBeats", 4.0}}));
        CHECK_FALSE(changes(store, {{"cmd", "setLength"}, {"id", 99}, {"lengthBeats", 1.0}}));
        CHECK_FALSE(changes(store, {{"cmd", "moveChord"}, {"id", 1}, {"index", 0}}));
    }
    SUBCASE("the selection is not part of the document") {
        CHECK_FALSE(changes(store, {{"cmd", "selectEvent"}, {"id", 2}}));
        CHECK(store.selectedEventId() == 2);
        CHECK_FALSE(changes(store, {{"cmd", "selectEvents"}, {"ids", {1, 3}}, {"anchor", 3}}));
        CHECK(store.selectedEventIds() == std::vector<int>{1, 3});
    }
    SUBCASE("live state the saved state carries counts, when it moves") {
        CHECK(changes(store, {{"cmd", "setNewChordLength"}, {"lengthBeats", 1.5}}));
        CHECK_FALSE(changes(store, {{"cmd", "setNewChordLength"}, {"lengthBeats", 1.5}}));
        CHECK(changes(store, {{"cmd", "browseKey"}, {"steps", 1}}));
        CHECK(changes(store, {{"cmd", "browseKey"}, {"home", true}}));
        CHECK_FALSE(changes(store, {{"cmd", "browseKey"}, {"home", true}}));
        CHECK_FALSE(changes(store, {{"cmd", "setFollowHost"}, {"on", true}}));  // on by default
        CHECK(changes(store, {{"cmd", "setFollowHost"}, {"on", false}}));
    }
    SUBCASE("undo and redo: a change when there is something to undo or redo, none otherwise") {
        CHECK(changes(store, {{"cmd", "undo"}}));  // the last append
        CHECK(store.doc().events.size() == 2);
        CHECK(changes(store, {{"cmd", "redo"}}));
        CHECK_FALSE(changes(store, {{"cmd", "redo"}}));  // nothing left to redo
        DocumentStore fresh;
        CHECK_FALSE(changes(fresh, {{"cmd", "undo"}}));
    }
    SUBCASE("a rejected command reports no change") {
        std::string error;
        bool changed = true;
        CHECK_FALSE(applyCommandJson(store, R"({"schema":1,"cmd":"nonsense"})", error, &changed));
        CHECK_FALSE(changed);
        changed = true;
        CHECK_FALSE(applyCommandJson(store, "not json", error, &changed));
        CHECK_FALSE(changed);
        changed = true;
        CHECK_FALSE(applyCommandJson(store, R"({"schema":1,"cmd":"setLength","id":1,"lengthBeats":"nan"})", error, &changed));
        CHECK_FALSE(changed);
    }
    SUBCASE("the flag is optional") {
        std::string error;
        CHECK(applyCommandJson(store, R"({"schema":1,"cmd":"setLength","id":1,"lengthBeats":2})", error));
        CHECK(store.doc().events[0].lengthBeats == 2.0);
    }
}

// --- Chord timing commands carry notes and can be refused by the track limits (tracks Task 2a) -----------

namespace {

constexpr std::int32_t kBarTicks = 4 * kFeelTicksPerBeat;

const std::string kPolyphonyLimit =
    "That change would leave more than " + std::to_string(kMaxTrackPolyphony) + " track notes sounding at once";

const std::string kOnsetLimit =
    "That change would leave more than " + std::to_string(kMaxTrackOnsetsPerBeat) + " track notes starting within one beat";

const std::string kDomainLimit = "That change would move a track note past the end of what ChordLab can hold";

/// `threeChords` with a Bass track (id 1): kMaxTrackPolyphony notes held from tick 0 into the second chord (to
/// kBar + 3000) and one note at the start of the third chord, after they end. Moving the third chord earlier, or
/// shortening or removing the second, would put that note under the held ones.
DocumentStore heldNotes() {
    DocumentStore store = threeChords();
    REQUIRE(store.addTrack(TrackRole::Bass) == 1);
    std::vector<NoteSpec> held;
    for (int i = 0; i < kMaxTrackPolyphony; ++i) {
        held.push_back(NoteSpec{40 + i, 0, kBarTicks + 3000});
    }
    REQUIRE(store.insertNotes(1, held).size() == static_cast<std::size_t>(kMaxTrackPolyphony));
    REQUIRE(store.insertNotes(1, {NoteSpec{70, 2 * kBarTicks, 480}}).size() == 1);
    return store;
}

/// Sends `cmd` and checks the store's refusal: false, the error `"<wire>" refused: <reason>` (the polyphony message
/// unless another is given), `documentChanged` false, and the store as it was (document, revision, history,
/// counters, selection and anchor).
void checkTrackRefusal(DocumentStore& store, json cmd, const std::string& wire,
                       const std::string& reason = kPolyphonyLimit) {
    CAPTURE(cmd.dump());
    cmd["schema"] = kJsonSchema;
    const DocumentStore reference = store;
    std::string error;
    bool changed = true;
    CHECK_FALSE(applyCommandJson(store, cmd.dump(), error, &changed));
    CHECK_FALSE(changed);
    CHECK(error == "\"" + wire + "\" refused: " + reason);
    CHECK(store.doc() == reference.doc());
    CHECK(store.revision() == reference.revision());
    CHECK(store.nextId() == reference.nextId());
    CHECK(store.nextSectionId() == reference.nextSectionId());
    CHECK(store.nextTrackId() == reference.nextTrackId());
    CHECK(store.nextNoteId() == reference.nextNoteId());
    CHECK(store.selectedEventIds() == reference.selectedEventIds());
    CHECK(store.selectedEventId() == reference.selectedEventId());
    CHECK(store.noteSelection() == reference.noteSelection());
    DocumentStore a = store;
    DocumentStore b = reference;
    CHECK(a.undo() == b.undo());
    CHECK(a.doc() == b.doc());  // no history pushed
}

}  // namespace

TEST_CASE("chord timing commands: a refusal by the track limits names the command and the limit and changes nothing") {
    DocumentStore store = heldNotes();
    checkTrackRefusal(store, {{"cmd", "moveChord"}, {"id", 3}, {"index", 1}}, "moveChord");
    checkTrackRefusal(store, {{"cmd", "setLength"}, {"id", 2}, {"lengthBeats", 1.0}}, "setLength");
    checkTrackRefusal(store, {{"cmd", "setLengths"}, {"ids", {2}}, {"lengthBeats", 1.0}}, "setLengths");
    checkTrackRefusal(store, {{"cmd", "removeChord"}, {"id", 2}}, "removeChord");
    checkTrackRefusal(store, {{"cmd", "removeChords"}, {"ids", {2, 99}}}, "removeChords");
    // The same commands that fit go through, and no-ops stay successes.
    CHECK(changes(store, {{"cmd", "setLength"}, {"id", 2}, {"lengthBeats", 3.5}}));  // chord 3 at 7200, after 6840
    CHECK_FALSE(changes(store, {{"cmd", "setLength"}, {"id", 2}, {"lengthBeats", 3.5}}));
    CHECK_FALSE(changes(store, {{"cmd", "removeChord"}, {"id", 99}}));
    CHECK_FALSE(changes(store, {{"cmd", "moveChord"}, {"id", 1}, {"index", 0}}));
}

TEST_CASE("insertChords: a refusal by the track limits names the command and the limit and changes nothing") {
    // X, A, B of 0.2506 beats and C of four; note 1 on B's last tick goes inactive when X is removed, and C's notes
    // then start where it lies: inserting a 0.2506-beat chord at 0 again would widen B by a tick and reactivate it.
    DocumentStore store;
    const int x = store.appendChord(kC, 0.2506);
    store.appendChord(kF, 0.2506);
    store.appendChord(kG7, 0.2506);
    store.appendChord(kC, 4.0);
    REQUIRE(store.addTrack(TrackRole::Bass) == 1);
    REQUIRE(store.insertNotes(1, {NoteSpec{36, 721, 480}}) == std::vector<int>{1});
    REQUIRE(store.removeChord(x));
    std::vector<NoteSpec> onC;
    for (int i = 0; i < kMaxTrackPolyphony; ++i) {
        onC.push_back(NoteSpec{60 + i, 481, 480});
    }
    REQUIRE(store.insertNotes(1, onC).size() == static_cast<std::size_t>(kMaxTrackPolyphony));
    checkTrackRefusal(store,
                      {{"cmd", "insertChords"}, {"index", 0}, {"chords", {{{"root", 0}, {"type", "maj"}, {"lengthBeats", 0.2506}}}}},
                      "insertChords");
    CHECK(changes(store, {{"cmd", "insertChords"}, {"index", 0}, {"chords", {{{"root", 0}, {"type", "maj"}, {"lengthBeats", 0.25}}}}}));
}

TEST_CASE("chord timing commands: a refusal by the onset cap names the command and the cap and changes nothing") {
    // Half the cap (rounded down) of short notes ending where the first chord ends, and the rest of the cap plus one
    // at the start of the third: removing the second chord, or moving the third into its place, packs them all into
    // one beat.
    constexpr int cap = kMaxTrackOnsetsPerBeat;
    constexpr int tail = cap / 2;
    constexpr int head = cap - tail + 1;
    constexpr std::int32_t step = kMinTrackNoteTicks;  // back to back: polyphony 1
    static_assert((cap + 1) * step < kFeelTicksPerBeat, "the packed run must fit in one beat");
    DocumentStore store = threeChords();
    REQUIRE(store.addTrack(TrackRole::Bass) == 1);
    std::vector<NoteSpec> notes;
    for (int j = 0; j < tail; ++j) {
        notes.push_back(NoteSpec{60, kBarTicks - (tail - j) * step, step});
    }
    for (int j = 0; j < head; ++j) {
        notes.push_back(NoteSpec{62, 2 * kBarTicks + j * step, step});
    }
    REQUIRE(store.insertNotes(1, notes).size() == notes.size());
    checkTrackRefusal(store, {{"cmd", "removeChord"}, {"id", 2}}, "removeChord", kOnsetLimit);
    checkTrackRefusal(store, {{"cmd", "moveChord"}, {"id", 3}, {"index", 1}}, "moveChord", kOnsetLimit);
}

TEST_CASE("insertChords: refused when it pushes a note past the end of the note range; the store's reason") {
    // kMaxChords - 1 chords of kMaxLengthBeats and a note in the last running to kMaxNoteTick: a one-beat chord
    // inserted at 0 would start it a beat later, its length past the range.
    DocumentStore store;
    for (int i = 0; i < kMaxChords - 1; ++i) {
        REQUIRE(store.appendChord(kC, kMaxLengthBeats) == i + 1);
    }
    REQUIRE(store.addTrack(TrackRole::Lead) == 1);
    const auto lastStart = static_cast<std::int64_t>((kMaxChords - 2) * kMaxLengthBeats * kFeelTicksPerBeat);
    REQUIRE(store.insertNotes(1, {NoteSpec{60, lastStart, kMaxNoteTick - lastStart}}) == std::vector<int>{1});
    const json oneBeat = json::array({{{"root", 0}, {"type", "maj"}, {"lengthBeats", 1.0}}});
    checkTrackRefusal(store, {{"cmd", "insertChords"}, {"index", 0}, {"chords", oneBeat}}, "insertChords", kDomainLimit);
    applyOk(store, {{"cmd", "insertChords"}, {"index", kMaxChords - 1}, {"chords", oneBeat}});  // at the end: nothing moves
}

TEST_CASE("chord timing commands: a refusal by the note range reads in chord terms (D17a)") {
    // A first chord one beat short of kMaxLengthBeats, the rest full, and a note in the last running to kMaxNoteTick:
    // lengthening the first by that beat starts the note a beat later, its length past the range.
    DocumentStore store;
    REQUIRE(store.appendChord(kC, kMaxLengthBeats - 1.0) == 1);
    for (int i = 1; i < kMaxChords; ++i) {
        REQUIRE(store.appendChord(kC, kMaxLengthBeats) == i + 1);
    }
    REQUIRE(store.addTrack(TrackRole::Lead) == 1);
    const auto lastStart = static_cast<std::int64_t>(kMaxNoteTick) - kFeelTicksPerBeat * static_cast<std::int64_t>(kMaxLengthBeats);
    REQUIRE(store.insertNotes(1, {NoteSpec{60, lastStart - kFeelTicksPerBeat, kMaxNoteTick - lastStart + kFeelTicksPerBeat}}) ==
            std::vector<int>{1});
    checkTrackRefusal(store, {{"cmd", "setLength"}, {"id", 1}, {"lengthBeats", kMaxLengthBeats}}, "setLength", kDomainLimit);
    checkTrackRefusal(store, {{"cmd", "setLengths"}, {"ids", {1}}, {"lengthBeats", kMaxLengthBeats}}, "setLengths",
                      kDomainLimit);
}

TEST_CASE("chord timing commands carry the notes: later chords' notes move, removed chords' notes go") {
    DocumentStore store = threeChords();
    REQUIRE(store.addTrack(TrackRole::Bass) == 1);
    REQUIRE(store.insertNotes(1, {NoteSpec{36, 100, 480}, NoteSpec{38, kBarTicks + 200, 480},
                                  NoteSpec{40, 2 * kBarTicks + 300, 480}}) == std::vector<int>{1, 2, 3});
    const std::vector<TrackNote> notes = store.doc().tracks.at(0).notes;
    CHECK(changes(store, {{"cmd", "setLength"}, {"id", 1}, {"lengthBeats", 2.0}}));
    CHECK(store.doc().tracks.at(0).notes == notes);
    CHECK(noteStart(store.doc(), store.doc().tracks.at(0).notes.at(2)) == 1920 + kBarTicks + 300);
    CHECK(changes(store, {{"cmd", "removeChord"}, {"id", 2}}));
    CHECK(store.doc().tracks.at(0).notes == std::vector<TrackNote>{notes.at(0), notes.at(2)});
    CHECK(noteStart(store.doc(), store.doc().tracks.at(0).notes.at(1)) == 1920 + 300);
}

TEST_CASE("loadPreset: append keeps every note; replace removes every note and keeps the tracks") {
    DocumentStore store = threeChords();
    REQUIRE(store.addTrack(TrackRole::Bass) == 1);
    REQUIRE(store.addTrack(TrackRole::Lead) == 2);
    REQUIRE(store.insertNotes(1, {NoteSpec{36, 100, 480}, NoteSpec{38, kBarTicks + 200, 480}}).size() == 2);
    REQUIRE(store.setTrackMute(2, true));
    const std::vector<Track> tracks = store.doc().tracks;
    CHECK(changes(store, {{"cmd", "loadPreset"}, {"id", "axis"}, {"mode", "append"}}));
    CHECK(store.doc().events.size() == 7);
    CHECK(store.doc().tracks == tracks);
    CHECK(changes(store, {{"cmd", "loadPreset"}, {"id", "axis"}, {"mode", "replace"}}));
    CHECK(store.doc().events.size() == 4);
    REQUIRE(store.doc().tracks.size() == 2);
    CHECK(store.doc().tracks.at(0).notes.empty());
    CHECK(store.doc().tracks.at(1).notes.empty());
    CHECK(store.doc().tracks.at(0).name == "Bass");
    CHECK(store.doc().tracks.at(1).mute);
    REQUIRE(store.undo());
    CHECK(store.doc().tracks == tracks);
}

// --- Track and note commands (tracks Task 3) -------------------------------------------------------------

namespace {

/// `threeChords` with an empty Bass track (id 1) named "Bass".
DocumentStore chordsAndBass() {
    DocumentStore store = threeChords();
    REQUIRE(store.addTrack(TrackRole::Bass) == 1);
    return store;
}

/// `chordsAndBass` with Bass notes 1..3 at 100, kBarTicks + 200 and 2 * kBarTicks + 300, in the three chords.
DocumentStore chordsAndNotes() {
    DocumentStore store = chordsAndBass();
    REQUIRE(store.insertNotes(1, {NoteSpec{36, 100, 480}, NoteSpec{38, kBarTicks + 200, 480},
                                  NoteSpec{40, 2 * kBarTicks + 300, 480}}) == std::vector<int>{1, 2, 3});
    return store;
}

/// Sends `text` (raw) and checks it was refused with exactly `expected` and that nothing at all changed: the
/// document, revision, history, counters and selections, and `documentChanged` stayed false.
void checkErrorRaw(DocumentStore& store, const std::string& text, const std::string& expected) {
    CAPTURE(text.substr(0, 300));
    const DocumentStore reference = store;
    std::string error;
    bool changed = true;
    CHECK_FALSE(applyCommandJson(store, text, error, &changed));
    CHECK_FALSE(changed);
    CHECK(error == expected);
    CHECK(store.doc() == reference.doc());
    CHECK(store.revision() == reference.revision());
    CHECK(store.nextId() == reference.nextId());
    CHECK(store.nextTrackId() == reference.nextTrackId());
    CHECK(store.nextNoteId() == reference.nextNoteId());
    CHECK(store.selectedEventIds() == reference.selectedEventIds());
    CHECK(store.noteSelection() == reference.noteSelection());
    DocumentStore a = store;
    DocumentStore b = reference;
    CHECK(a.undo() == b.undo());
    CHECK(a.doc() == b.doc());  // no history pushed
}

void checkError(DocumentStore& store, json cmd, const std::string& expected) {
    cmd["schema"] = kJsonSchema;
    checkErrorRaw(store, cmd.dump(), expected);
}

/// `checkError` for a store's refusal: `"<wire>" refused: <reason>`.
void checkRefusedWith(DocumentStore& store, json cmd, const std::string& wire, const std::string& reason) {
    const std::string expected = "\"" + wire + "\" refused: " + reason;
    checkError(store, std::move(cmd), expected);
}

const std::string kInt64Range = "in -9223372036854775808..9223372036854775807";
const std::string kDragMessage = "The notes changed while you were dragging";
const std::string kRoleMessage = "must be \"bass\", \"lead\" or \"other\"";

const std::vector<TrackNote>& notesOfTrack(const DocumentStore& store, std::size_t trackIndex = 0) {
    return store.doc().tracks.at(trackIndex).notes;
}

/// The note ids of track `trackIndex`, in its note order.
std::vector<int> noteIds(const DocumentStore& store, std::size_t trackIndex = 0) {
    std::vector<int> ids;
    for (const TrackNote& n : notesOfTrack(store, trackIndex)) {
        ids.push_back(n.id);
    }
    return ids;
}

std::vector<int> trackIds(const DocumentStore& store) {
    std::vector<int> ids;
    for (const Track& t : store.doc().tracks) {
        ids.push_back(t.id);
    }
    return ids;
}

/// The derived start of the note with `id`, whichever track holds it.
std::int64_t noteStartOf(const DocumentStore& store, int id) {
    for (const Track& t : store.doc().tracks) {
        for (const TrackNote& n : t.notes) {
            if (n.id == id) {
                return noteStart(store.doc(), n);
            }
        }
    }
    FAIL("no note with id " << id);
    return -1;
}

json noteJson(int pitch, std::int64_t start, std::int64_t length) {
    return {{"pitch", pitch}, {"start", start}, {"length", length}};
}

}  // namespace

TEST_CASE("addTrack: adds a track of the role with its default or given name, one undo step") {
    DocumentStore store = threeChords();
    CHECK(changes(store, {{"cmd", "addTrack"}, {"role", "lead"}}));
    CHECK(changes(store, {{"cmd", "addTrack"}, {"role", "bass"}, {"name", "Walk"}}));
    CHECK(changes(store, {{"cmd", "addTrack"}, {"role", "other"}}));
    CHECK(changes(store, {{"cmd", "addTrack"}, {"role", "other"}, {"name", ""}}));  // empty: the default, as the store's
    REQUIRE(store.doc().tracks.size() == 4);
    CHECK(store.doc().tracks.at(0).name == "Lead");
    CHECK(store.doc().tracks.at(0).role == TrackRole::Lead);
    CHECK(store.doc().tracks.at(1).name == "Walk");
    CHECK(store.doc().tracks.at(1).role == TrackRole::Bass);
    CHECK(store.doc().tracks.at(2).name == "Track");
    CHECK(store.doc().tracks.at(2).role == TrackRole::Other);
    CHECK(store.doc().tracks.at(3).name == "Track 2");
    CHECK(trackIds(store) == std::vector<int>{1, 2, 3, 4});
    CHECK(historyDepth(store).first == 3 + 4);  // three chords and four tracks
    applyOk(store, {{"cmd", "undo"}});
    CHECK(store.doc().tracks.size() == 3);
    // At the limit: refused with the store's reason; the 64-byte name is the longest taken.
    DocumentStore full = threeChords();
    for (int i = 0; i < kMaxTracks; ++i) {
        applyOk(full, {{"cmd", "addTrack"}, {"role", "other"}, {"name", std::string(kMaxTrackNameBytes, 'n')}});
    }
    CHECK(full.doc().tracks.at(0).name.size() == static_cast<std::size_t>(kMaxTrackNameBytes));
    checkRefusedWith(full, {{"cmd", "addTrack"}, {"role", "bass"}}, "addTrack", "At most 4 tracks");
}

TEST_CASE("addTrack: a bad role or name is rejected naming the field; nothing changes") {
    DocumentStore store = chordsAndNotes();
    checkError(store, {{"cmd", "addTrack"}}, "missing \"role\"");
    for (const json& bad : {json("Bass"), json("BASS"), json("drums"), json(""), json(1), json(nullptr), json(true), json::array()}) {
        checkError(store, {{"cmd", "addTrack"}, {"role", bad}}, "\"role\" " + kRoleMessage);
    }
    for (const json& bad : {json(5), json(nullptr), json(true), json::array(), json::object()}) {
        checkError(store, {{"cmd", "addTrack"}, {"role", "lead"}, {"name", bad}}, "\"name\" must be a string");
    }
    // As a section name: longer than the bound is rejected, not cut.
    checkError(store, {{"cmd", "addTrack"}, {"role", "lead"}, {"name", std::string(kMaxTrackNameBytes + 1, 'x')}},
               "\"name\" must be at most 64 bytes");
}

TEST_CASE("removeTrack, patchTrack, moveTrack: the store calls; an unknown track is refused, no-ops are accepted") {
    DocumentStore store = chordsAndNotes();
    applyOk(store, {{"cmd", "addTrack"}, {"role", "lead"}});
    applyOk(store, {{"cmd", "addTrack"}, {"role", "other"}});
    REQUIRE(trackIds(store) == std::vector<int>{1, 2, 3});
    SUBCASE("patchTrack") {
        CHECK(changes(store, {{"cmd", "patchTrack"}, {"id", 2}, {"name", "Melody"}}));
        CHECK(store.doc().tracks.at(1).name == "Melody");
        CHECK(store.doc().tracks.at(1).role == TrackRole::Lead);
        CHECK(changes(store, {{"cmd", "patchTrack"}, {"id", 2}, {"role", "bass"}}));
        CHECK(store.doc().tracks.at(1).role == TrackRole::Bass);
        CHECK(changes(store, {{"cmd", "patchTrack"}, {"id", 2}, {"name", "Hook"}, {"role", "other"}}));
        CHECK(store.doc().tracks.at(1).name == "Hook");
        CHECK(store.doc().tracks.at(1).role == TrackRole::Other);
        CHECK(changes(store, {{"cmd", "patchTrack"}, {"id", 2}, {"name", ""}}));  // empty stays empty, as renameSection
        CHECK(store.doc().tracks.at(1).name.empty());
        CHECK_FALSE(changes(store, {{"cmd", "patchTrack"}, {"id", 2}, {"name", ""}, {"role", "other"}}));  // no-op
        const std::pair<int, int> depth = historyDepth(store);
        CHECK_FALSE(changes(store, {{"cmd", "patchTrack"}, {"id", 1}, {"name", "Bass"}}));
        CHECK(historyDepth(store) == depth);
        checkRefusedWith(store, {{"cmd", "patchTrack"}, {"id", 1}}, "patchTrack", "Give a name or a role");
        checkRefusedWith(store, {{"cmd", "patchTrack"}, {"id", 9}, {"name", "x"}}, "patchTrack", "No track with id 9");
        checkError(store, {{"cmd", "patchTrack"}, {"id", 1}, {"role", "Lead"}}, "\"role\" " + kRoleMessage);
        checkError(store, {{"cmd", "patchTrack"}, {"id", 1}, {"name", 4}}, "\"name\" must be a string");
        checkError(store, {{"cmd", "patchTrack"}, {"id", 1}, {"name", std::string(65, 'x')}}, "\"name\" must be at most 64 bytes");
        checkError(store, {{"cmd", "patchTrack"}, {"name", "x"}}, "missing \"id\"");
    }
    SUBCASE("moveTrack") {
        CHECK(changes(store, {{"cmd", "moveTrack"}, {"id", 3}, {"index", 0}}));
        CHECK(trackIds(store) == std::vector<int>{3, 1, 2});
        CHECK(changes(store, {{"cmd", "moveTrack"}, {"id", 3}, {"index", 99}}));  // clamped to the end
        CHECK(trackIds(store) == std::vector<int>{1, 2, 3});
        CHECK(changes(store, {{"cmd", "moveTrack"}, {"id", 1}, {"index", -5}}) == false);  // already first: clamped to 0
        CHECK(trackIds(store) == std::vector<int>{1, 2, 3});
        checkRefusedWith(store, {{"cmd", "moveTrack"}, {"id", 9}, {"index", 0}}, "moveTrack", "No track with id 9");
        checkError(store, {{"cmd", "moveTrack"}, {"id", 1}}, "missing \"index\"");
        checkError(store, {{"cmd", "moveTrack"}, {"id", 1}, {"index", 1.5}}, "\"index\" must be an integer");
        checkError(store, {{"cmd", "moveTrack"}, {"id", 1}, {"index", 2147483648ll}}, "\"index\" must be an integer");
    }
    SUBCASE("removeTrack") {
        CHECK(changes(store, {{"cmd", "removeTrack"}, {"id", 1}}));  // with its notes
        CHECK(trackIds(store) == std::vector<int>{2, 3});
        checkRefusedWith(store, {{"cmd", "removeTrack"}, {"id", 1}}, "removeTrack", "No track with id 1");
        checkError(store, {{"cmd", "removeTrack"}, {"id", "1"}}, "\"id\" must be an integer");
        applyOk(store, {{"cmd", "undo"}});
        CHECK(trackIds(store) == std::vector<int>{1, 2, 3});
        CHECK(noteIds(store) == std::vector<int>{1, 2, 3});
    }
}

TEST_CASE("setTrackMute, setTrackSolo, setChordsMute, setChordsSolo: live flags, no history, the host is dirtied") {
    DocumentStore store = chordsAndNotes();
    const std::uint64_t revision = store.revision();
    const std::pair<int, int> depth = historyDepth(store);
    CHECK(changes(store, {{"cmd", "setTrackMute"}, {"id", 1}, {"on", true}}));
    CHECK(store.doc().tracks.at(0).mute);
    CHECK(changes(store, {{"cmd", "setTrackSolo"}, {"id", 1}, {"on", true}}));
    CHECK(store.doc().tracks.at(0).solo);
    CHECK(changes(store, {{"cmd", "setChordsMute"}, {"on", true}}));
    CHECK(store.doc().chordsMuted);
    CHECK(changes(store, {{"cmd", "setChordsSolo"}, {"on", true}}));
    CHECK(store.doc().chordsSolo);
    CHECK(store.revision() == revision);  // live state moves no revision
    CHECK(historyDepth(store) == depth);  // and pushes no history
    // The value already held is a valid no-op, reported as no change.
    CHECK_FALSE(changes(store, {{"cmd", "setTrackMute"}, {"id", 1}, {"on", true}}));
    CHECK_FALSE(changes(store, {{"cmd", "setChordsSolo"}, {"on", true}}));
    CHECK(changes(store, {{"cmd", "setTrackMute"}, {"id", 1}, {"on", false}}));
    CHECK(changes(store, {{"cmd", "setChordsMute"}, {"on", false}}));
    CHECK_FALSE(store.doc().tracks.at(0).mute);
    CHECK_FALSE(store.doc().chordsMuted);
    // An unknown track is an error (the store cannot tell it from a no-op, the command can); the chords have no id.
    checkRefusedWith(store, {{"cmd", "setTrackMute"}, {"id", 9}, {"on", true}}, "setTrackMute", "No track with id 9");
    checkRefusedWith(store, {{"cmd", "setTrackSolo"}, {"id", 0}, {"on", true}}, "setTrackSolo", "No track with id 0");
    checkError(store, {{"cmd", "setTrackMute"}, {"id", 1}}, "missing \"on\"");
    checkError(store, {{"cmd", "setTrackSolo"}, {"id", 1}, {"on", 1}}, "\"on\" must be true or false");
    checkError(store, {{"cmd", "setChordsMute"}}, "missing \"on\"");
    checkError(store, {{"cmd", "setChordsSolo"}, {"on", "true"}}, "\"on\" must be true or false");
    checkError(store, {{"cmd", "setTrackMute"}, {"on", true}}, "missing \"id\"");
}

TEST_CASE("insertNotes: inserts the notes with velocity 100 by default, selects them, one undo step, a revision") {
    DocumentStore store = chordsAndBass();
    const std::uint64_t revision = store.revision();
    json notes = json::array({noteJson(60, kBarTicks + 100, 480), noteJson(62, 0, 960)});
    notes[1]["velocity"] = 64;
    CHECK(changes(store, {{"cmd", "insertNotes"}, {"trackId", 1}, {"notes", notes}}));
    REQUIRE(notesOfTrack(store).size() == 2);
    CHECK(store.revision() == revision + 1);
    // In note order (start, pitch, id): the second spec first.
    CHECK(notesOfTrack(store).at(0) == TrackNote{2, 62, 1, 0, 960, 64});
    CHECK(notesOfTrack(store).at(1) == TrackNote{1, 60, 2, 100, 480, 100});
    CHECK(store.noteSelection() == NoteSelection{1, {2, 1}});
    CHECK(historyDepth(store).first == 3 + 1 + 1);  // chords, the track, the notes
    applyOk(store, {{"cmd", "undo"}});
    CHECK(notesOfTrack(store).empty());
    CHECK(store.noteSelection() == NoteSelection{});
    // Pitch and velocity are clamped by the store, start and length by their domain.
    notes = json::array({noteJson(200, -40, 1)});
    notes[0]["velocity"] = 0;
    applyOk(store, {{"cmd", "insertNotes"}, {"trackId", 1}, {"notes", notes}});
    CHECK(notesOfTrack(store).at(0).pitch == 127);
    CHECK(notesOfTrack(store).at(0).velocity == 1);
    CHECK(notesOfTrack(store).at(0).offsetTick == 0);
    CHECK(notesOfTrack(store).at(0).lengthTicks == kMinTrackNoteTicks);
    // The extremes of 64 bits clamp too; a start past the end has no chord and is refused by the store.
    notes = json::array({noteJson(60, std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max())});
    applyOk(store, {{"cmd", "insertNotes"}, {"trackId", 1}, {"notes", notes}});
    CHECK(noteStartOf(store, store.nextNoteId() - 1) == 0);  // the clamped start; ids are never reused after the undo
    notes = json::array({noteJson(60, std::numeric_limits<std::int64_t>::max(), 480)});
    checkRefusedWith(store, {{"cmd", "insertNotes"}, {"trackId", 1}, {"notes", notes}}, "insertNotes",
                     "A note must start before the end of the progression");
}

TEST_CASE("insertNotes: refusals by the store name the command and its reason") {
    DocumentStore store = chordsAndBass();
    const json one = json::array({noteJson(60, 0, 480)});
    checkRefusedWith(store, {{"cmd", "insertNotes"}, {"trackId", 9}, {"notes", one}}, "insertNotes", "No track with id 9");
    checkRefusedWith(store, {{"cmd", "insertNotes"}, {"trackId", 1}, {"notes", json::array()}}, "insertNotes",
                     "No notes to insert");
    DocumentStore empty;
    REQUIRE(empty.addTrack(TrackRole::Lead) == 1);
    checkRefusedWith(empty, {{"cmd", "insertNotes"}, {"trackId", 1}, {"notes", one}}, "insertNotes", "Add chords first");
    json stack = json::array();
    for (int i = 0; i <= kMaxTrackPolyphony; ++i) {
        stack.push_back(noteJson(40 + i, 0, 480));
    }
    checkRefusedWith(store, {{"cmd", "insertNotes"}, {"trackId", 1}, {"notes", stack}}, "insertNotes",
                     "At most " + std::to_string(kMaxTrackPolyphony) + " track notes can sound at once");
}

TEST_CASE("insertNotes, updateNotes: every field's type and range error names the field with its index") {
    DocumentStore store = chordsAndNotes();
    const std::string at2 = "notes[2].";
    const auto three = [](json third) {
        return json::array({noteJson(60, 0, 480), noteJson(62, 480, 480), std::move(third)});
    };
    SUBCASE("insertNotes") {
        const auto bad = [&](const std::string& field, const json& value) {
            json third = noteJson(64, 960, 480);
            third[field] = value;
            return json{{"cmd", "insertNotes"}, {"trackId", 1}, {"notes", three(third)}};
        };
        for (const json& v : {json("1"), json(1.5), json(nullptr), json(true), json::array(), json(2147483648ll), json(-2147483649ll)}) {
            checkError(store, bad("pitch", v), at2 + "pitch must be an integer");
            checkError(store, bad("velocity", v), at2 + "velocity must be an integer");
        }
        for (const json& v : {json("1"), json(1.5), json(nullptr), json(true), json::array(), json(1e300)}) {
            checkError(store, bad("start", v), at2 + "start must be an integer " + kInt64Range);
            checkError(store, bad("length", v), at2 + "length must be an integer " + kInt64Range);
        }
        for (const char* field : {"pitch", "start", "length"}) {
            json third = noteJson(64, 960, 480);
            third.erase(field);
            checkError(store, {{"cmd", "insertNotes"}, {"trackId", 1}, {"notes", three(third)}},
                       std::string("missing ") + at2 + field);
        }
        checkError(store, {{"cmd", "insertNotes"}, {"trackId", 1}, {"notes", three(json(5))}}, "notes[2] must be an object");
        checkError(store, {{"cmd", "insertNotes"}, {"trackId", 1}, {"notes", three(json::array())}}, "notes[2] must be an object");
        checkError(store, {{"cmd", "insertNotes"}, {"trackId", 1}}, "missing \"notes\"");
        checkError(store, {{"cmd", "insertNotes"}, {"trackId", 1}, {"notes", "x"}}, "\"notes\" must be an array");
        checkError(store, {{"cmd", "insertNotes"}, {"notes", json::array()}}, "missing \"trackId\"");
        for (const json& v : {json("1"), json(1.5), json(nullptr), json(2147483648ll), json(-2147483649ll)}) {
            checkError(store, {{"cmd", "insertNotes"}, {"trackId", v}, {"notes", json::array()}}, "\"trackId\" must be an integer");
        }
    }
    SUBCASE("integers beyond 64 bits, as raw text") {
        const std::string head = R"({"schema":1,"cmd":"insertNotes","trackId":1,"notes":[{"pitch":60,"length":480,"start":)";
        for (const char* beyond : {"9223372036854775808", "-9223372036854775809", "18446744073709551616", "1e19"}) {
            checkErrorRaw(store, head + beyond + "}]}", "notes[0].start must be an integer " + kInt64Range);
        }
        const std::string lengthHead = R"({"schema":1,"cmd":"insertNotes","trackId":1,"notes":[{"pitch":60,"start":0,"length":)";
        for (const char* beyond : {"9223372036854775808", "-9223372036854775809"}) {
            checkErrorRaw(store, lengthHead + beyond + "}]}", "notes[0].length must be an integer " + kInt64Range);
        }
        // The edges of int64 are accepted (and clamped) and the extremes that fit are not errors.
        std::string error;
        CHECK(applyCommandJson(store, head + "9223372036854775807}]}", error) == false);  // no chord owns it: refused, not an error of range
        CHECK(error == "\"insertNotes\" refused: A note must start before the end of the progression");
        CHECK(applyCommandJson(store, head + "-9223372036854775808}]}", error));
    }
    SUBCASE("updateNotes") {
        const auto bad = [&](const std::string& field, const json& value) {
            json entry = {{"id", 1}};
            entry[field] = value;
            return json{{"cmd", "updateNotes"}, {"trackId", 1}, {"notes", json::array({json{{"id", 2}}, json{{"id", 3}}, entry})}};
        };
        for (const json& v : {json("1"), json(1.5), json(nullptr), json(true), json(2147483648ll)}) {
            checkError(store, bad("pitch", v), at2 + "pitch must be an integer");
            checkError(store, bad("velocity", v), at2 + "velocity must be an integer");
            checkError(store, bad("id", v), at2 + "id must be an integer");
        }
        for (const json& v : {json("1"), json(1.5), json(nullptr), json(true)}) {
            checkError(store, bad("start", v), at2 + "start must be an integer " + kInt64Range);
            checkError(store, bad("length", v), at2 + "length must be an integer " + kInt64Range);
        }
        checkError(store, {{"cmd", "updateNotes"}, {"trackId", 1}, {"notes", json::array({json::object()})}}, "missing notes[0].id");
        checkError(store, {{"cmd", "updateNotes"}, {"trackId", 1}, {"notes", json::array({7})}}, "notes[0] must be an object");
        checkError(store, {{"cmd", "updateNotes"}, {"trackId", 1}}, "missing \"notes\"");
        checkError(store, {{"cmd", "updateNotes"}, {"notes", json::array()}}, "missing \"trackId\"");
    }
}

TEST_CASE("insertNotes, removeNotes, updateNotes, selectNotes: array sizes are bounded before the store is touched") {
    DocumentStore store = chordsAndNotes();
    json many = json::array();
    for (int i = 0; i <= kMaxNotesPerTrack; ++i) {
        many.push_back(noteJson(60, 0, 480));
    }
    checkError(store, {{"cmd", "insertNotes"}, {"trackId", 1}, {"notes", many}},
               "\"notes\" must hold at most " + std::to_string(kMaxNotesPerTrack) + " notes");
    json edits = json::array();
    json ids = json::array();
    for (int i = 0; i <= kMaxNotesPerTrack; ++i) {
        edits.push_back({{"id", 1}});
        ids.push_back(1);
    }
    checkError(store, {{"cmd", "updateNotes"}, {"trackId", 1}, {"notes", edits}},
               "\"notes\" must hold at most " + std::to_string(kMaxNotesPerTrack) + " notes");
    checkError(store, {{"cmd", "removeNotes"}, {"trackId", 1}, {"ids", ids}},
               "\"ids\" must hold at most " + std::to_string(kMaxNotesPerTrack) + " ids");
    checkError(store, {{"cmd", "selectNotes"}, {"trackId", 1}, {"ids", ids}},
               "\"ids\" must hold at most " + std::to_string(kMaxNotesPerTrack) + " ids");
}

TEST_CASE("insertNotes: a 1024-note message fits kMaxJsonBytes and is accepted") {
    // Starts spread so that the onset cap holds exactly (kMaxTrackOnsetsPerBeat per 960-tick window) and no two
    // notes overlap (polyphony 1, under kMaxTrackPolyphony).
    constexpr std::int64_t step = kFeelTicksPerBeat / kMaxTrackOnsetsPerBeat;
    static_assert(step >= kMinTrackNoteTicks, "back-to-back notes of the minimum length must fit one step");
    constexpr std::int64_t totalTicks = kMaxNotesPerTrack * step;
    constexpr int bars = static_cast<int>((totalTicks + kBarTicks - 1) / kBarTicks);
    DocumentStore store;
    for (int i = 0; i < bars; ++i) {
        REQUIRE(store.appendChord(kC, 4.0, 100) == i + 1);
    }
    REQUIRE(store.addTrack(TrackRole::Lead) == 1);
    json notes = json::array();
    for (int i = 0; i < kMaxNotesPerTrack; ++i) {
        json note = noteJson(40 + i % 40, i * step, kMinTrackNoteTicks);
        note["velocity"] = 1 + i % 127;
        notes.push_back(note);
    }
    const std::string text = json{{"schema", kJsonSchema}, {"cmd", "insertNotes"}, {"trackId", 1}, {"notes", notes}}.dump();
    CHECK(text.size() < kMaxJsonBytes);
    std::string error;
    bool changed = false;
    REQUIRE_MESSAGE(applyCommandJson(store, text, error, &changed), error);
    CHECK(changed);
    CHECK(notesOfTrack(store).size() == static_cast<std::size_t>(kMaxNotesPerTrack));
    CHECK(store.noteSelection().ids.size() == static_cast<std::size_t>(kMaxNotesPerTrack));
    // One more is over the track's limit: the store's refusal, not a parse error.
    checkRefusedWith(store, {{"cmd", "insertNotes"}, {"trackId", 1}, {"notes", json::array({noteJson(60, 0, 480)})}}, "insertNotes",
                     "At most " + std::to_string(kMaxNotesPerTrack) + " notes per track");
}

TEST_CASE("baseRevision: the three note commands apply on a match and refuse on any other value, changing nothing") {
    DocumentStore store = chordsAndNotes();
    const std::uint64_t revision = store.revision();
    const json one = json::array({noteJson(60, 100, 480)});
    const json edit = json::array({json{{"id", 1}, {"pitch", 50}}});
    const json removal = json::array({3});
    for (const std::uint64_t wrong : {revision + 1, revision - 1, std::uint64_t{0}, std::numeric_limits<std::uint64_t>::max()}) {
        checkRefusedWith(store, {{"cmd", "insertNotes"}, {"trackId", 1}, {"notes", one}, {"baseRevision", wrong}}, "insertNotes", kDragMessage);
        checkRefusedWith(store, {{"cmd", "updateNotes"}, {"trackId", 1}, {"notes", edit}, {"baseRevision", wrong}}, "updateNotes", kDragMessage);
        checkRefusedWith(store, {{"cmd", "removeNotes"}, {"trackId", 1}, {"ids", removal}, {"baseRevision", wrong}}, "removeNotes", kDragMessage);
    }
    CHECK(store.revision() == revision);
    // Live state and selection moved nothing, so a drag started before them still lands.
    applyOk(store, {{"cmd", "setTrackMute"}, {"id", 1}, {"on", true}});
    applyOk(store, {{"cmd", "selectNotes"}, {"trackId", 1}, {"ids", {1}}});
    CHECK(store.revision() == revision);
    CHECK(changes(store, {{"cmd", "updateNotes"}, {"trackId", 1}, {"notes", edit}, {"baseRevision", revision}}));
    CHECK(notesOfTrack(store).at(0).pitch == 50);
    CHECK(store.revision() == revision + 1);
    // The previous base is stale now, for each command.
    checkRefusedWith(store, {{"cmd", "removeNotes"}, {"trackId", 1}, {"ids", removal}, {"baseRevision", revision}}, "removeNotes", kDragMessage);
    CHECK(changes(store, {{"cmd", "removeNotes"}, {"trackId", 1}, {"ids", removal}, {"baseRevision", revision + 1}}));
    CHECK(changes(store, {{"cmd", "insertNotes"}, {"trackId", 1}, {"notes", one}, {"baseRevision", revision + 2}}));
    // An undo moves it as well; a chord edit too.
    checkRefusedWith(store, {{"cmd", "insertNotes"}, {"trackId", 1}, {"notes", one}, {"baseRevision", revision + 2}}, "insertNotes", kDragMessage);
    // Without baseRevision, no check at all.
    CHECK(changes(store, {{"cmd", "insertNotes"}, {"trackId", 1}, {"notes", one}}));
    // The check comes before the store's own: a stale base on an unknown track is the drag message.
    checkRefusedWith(store, {{"cmd", "insertNotes"}, {"trackId", 9}, {"notes", one}, {"baseRevision", 1}}, "insertNotes", kDragMessage);
}

TEST_CASE("baseRevision: a non-negative integer of 64 bits; anything else is an error naming the field") {
    DocumentStore store = chordsAndNotes();
    const json removal = json::array({3});
    for (const json& bad : {json(-1), json(1.5), json(1.0), json("1"), json(nullptr), json(true), json::array(), json(-9223372036854775807ll - 1)}) {
        checkError(store, {{"cmd", "removeNotes"}, {"trackId", 1}, {"ids", removal}, {"baseRevision", bad}},
                   "\"baseRevision\" must be a non-negative integer");
        checkError(store, {{"cmd", "insertNotes"}, {"trackId", 1}, {"notes", json::array()}, {"baseRevision", bad}},
                   "\"baseRevision\" must be a non-negative integer");
    }
    checkErrorRaw(store, R"({"schema":1,"cmd":"removeNotes","trackId":1,"ids":[3],"baseRevision":18446744073709551616})",
                  "\"baseRevision\" must be a non-negative integer");
    checkErrorRaw(store, R"({"schema":1,"cmd":"removeNotes","trackId":1,"ids":[3],"baseRevision":-0.0})",
                  "\"baseRevision\" must be a non-negative integer");
}

TEST_CASE("removeNotes: removes the listed notes of the track, unknown ids skipped, an empty list a no-op") {
    DocumentStore store = chordsAndNotes();
    CHECK(changes(store, {{"cmd", "removeNotes"}, {"trackId", 1}, {"ids", {3, 99, 3}}}));
    CHECK(noteIds(store) == std::vector<int>{1, 2});
    const std::uint64_t revision = store.revision();
    CHECK_FALSE(changes(store, {{"cmd", "removeNotes"}, {"trackId", 1}, {"ids", {99}}}));
    CHECK_FALSE(changes(store, {{"cmd", "removeNotes"}, {"trackId", 1}, {"ids", json::array()}}));
    CHECK(store.revision() == revision);
    checkRefusedWith(store, {{"cmd", "removeNotes"}, {"trackId", 7}, {"ids", {1}}}, "removeNotes", "No track with id 7");
    checkError(store, {{"cmd", "removeNotes"}, {"trackId", 1}, {"ids", {1, "2"}}}, "ids[1] must be an integer");
    checkError(store, {{"cmd", "removeNotes"}, {"trackId", 1}, {"ids", {1, 2147483648ll}}}, "ids[1] must be an integer");
    checkError(store, {{"cmd", "removeNotes"}, {"trackId", 1}}, "missing \"ids\"");
    checkError(store, {{"cmd", "removeNotes"}, {"ids", {1}}}, "missing \"trackId\"");
    checkError(store, {{"cmd", "removeNotes"}, {"trackId", 1}, {"ids", 1}}, "\"ids\" must be an array");
    applyOk(store, {{"cmd", "undo"}});
    CHECK(noteIds(store) == std::vector<int>{1, 2, 3});
}

TEST_CASE("generateBass: replaces the scope's notes in one undo step, every chord without a scope, nothing selected") {
    DocumentStore store = chordsAndNotes();
    store.setNoteSelection(1, {1, 2});
    const std::uint64_t revision = store.revision();
    const std::pair<int, int> depth = historyDepth(store);
    CHECK(changes(store, {{"cmd", "generateBass"}, {"trackId", 1}, {"pattern", "roots"}, {"scope", {3, 1}}}));
    CHECK(notesOfTrack(store) == std::vector<TrackNote>{TrackNote{4, 36, 1, 0, kBarTicks, 100},
                                                        TrackNote{2, 38, 2, 200, 480, 100},
                                                        TrackNote{5, 31, 3, 0, kBarTicks, 100}});
    CHECK(store.revision() == revision + 1);
    CHECK(historyDepth(store).first == depth.first + 1);
    CHECK(store.noteSelection() == NoteSelection{1, {2}});  // the generated notes are not selected
    // No scope, and an empty one: every chord.
    CHECK(changes(store, {{"cmd", "generateBass"}, {"trackId", 1}, {"pattern", "rootPulse"}}));
    CHECK(notesOfTrack(store).size() == 12);
    CHECK(changes(store, {{"cmd", "generateBass"}, {"trackId", 1}, {"pattern", "octaves"}, {"scope", json::array()}}));
    CHECK(notesOfTrack(store).size() == 24);
    // Every pattern name is accepted; a baseRevision is not read (this is not a drag), stale or not.
    for (const char* pattern : {"roots", "rootPulse", "rootFifth", "octaves", "followFeel", "walking"}) {
        CAPTURE(pattern);
        CHECK(changes(store, {{"cmd", "generateBass"}, {"trackId", 1}, {"pattern", pattern}, {"scope", {2}},
                              {"baseRevision", 1}}));
    }
    applyOk(store, {{"cmd", "undo"}});
    applyOk(store, {{"cmd", "undo"}});
    CHECK(changes(store, {{"cmd", "generateBass"}, {"trackId", 1}, {"pattern", "walking"}, {"scope", {1}}}));
    CHECK(notesOfTrack(store).front().pitch == 36);
}

TEST_CASE("generateBass: nothing owned and nothing generated is an accepted no-op") {
    DocumentStore store;
    REQUIRE(store.appendChord(kC, 0.5) == 1);
    REQUIRE(store.addTrack(TrackRole::Bass) == 1);
    applyOk(store, {{"cmd", "setFeel"}, {"rhythm", "arpeggio"}, {"arpPattern", "down"}, {"arpRate", "half"},
                    {"strum", "off"}, {"strumSpeed", "medium"}, {"humanise", "off"}});
    const std::uint64_t revision = store.revision();
    CHECK_FALSE(changes(store, {{"cmd", "generateBass"}, {"trackId", 1}, {"pattern", "followFeel"}}));
    CHECK(store.revision() == revision);
}

TEST_CASE("generateBass: refusals by the store name the command and its reason") {
    DocumentStore store = chordsAndNotes();
    checkRefusedWith(store, {{"cmd", "generateBass"}, {"trackId", 9}, {"pattern", "roots"}}, "generateBass",
                     "No track with id 9");
    checkRefusedWith(store, {{"cmd", "generateBass"}, {"trackId", 1}, {"pattern", "roots"}, {"scope", {1, 42}}},
                     "generateBass", "No chord with id 42");
    DocumentStore empty;
    REQUIRE(empty.addTrack(TrackRole::Lead) == 1);
    checkRefusedWith(empty, {{"cmd", "generateBass"}, {"trackId", 1}, {"pattern", "walking"}}, "generateBass",
                     "Add chords first");
    // Eight notes sounding over the second half of G7 on another track, after the bass's own short note there:
    // Roots over G7 replaces that note with one lasting the chord, a ninth sounding note.
    REQUIRE(store.addTrack(TrackRole::Lead) == 2);
    std::vector<NoteSpec> pad;
    for (int i = 0; i < kMaxTrackPolyphony; ++i) {
        pad.push_back(NoteSpec{60 + i, 2 * kBarTicks + 2 * kFeelTicksPerBeat, 2 * kFeelTicksPerBeat});
    }
    REQUIRE(store.insertNotes(2, pad).size() == static_cast<std::size_t>(kMaxTrackPolyphony));
    checkRefusedWith(store, {{"cmd", "generateBass"}, {"trackId", 1}, {"pattern", "roots"}, {"scope", {3}}},
                     "generateBass", "At most " + std::to_string(kMaxTrackPolyphony) + " track notes can sound at once");
}

TEST_CASE("generateBass: every field's type and range error names the field, scope entries with their index") {
    DocumentStore store = chordsAndNotes();
    const std::string patterns =
        "must be \"roots\", \"rootPulse\", \"rootFifth\", \"octaves\", \"followFeel\" or \"walking\"";
    checkError(store, {{"cmd", "generateBass"}, {"pattern", "roots"}}, "missing \"trackId\"");
    for (const json& v : {json("1"), json(1.5), json(nullptr), json(true), json(2147483648ll), json(-2147483649ll)}) {
        checkError(store, {{"cmd", "generateBass"}, {"trackId", v}, {"pattern", "roots"}}, "\"trackId\" must be an integer");
    }
    checkError(store, {{"cmd", "generateBass"}, {"trackId", 1}}, "missing \"pattern\"");
    for (const json& v : {json("Roots"), json("root"), json(""), json(0), json(nullptr), json::array({"roots"})}) {
        checkError(store, {{"cmd", "generateBass"}, {"trackId", 1}, {"pattern", v}}, "\"pattern\" " + patterns);
    }
    for (const json& v : {json("1"), json(1), json(nullptr), json::object()}) {
        checkError(store, {{"cmd", "generateBass"}, {"trackId", 1}, {"pattern", "roots"}, {"scope", v}},
                   "\"scope\" must be an array");
    }
    for (const json& v : {json("2"), json(1.5), json(nullptr), json(true), json(2147483648ll), json(-2147483649ll)}) {
        checkError(store, {{"cmd", "generateBass"}, {"trackId", 1}, {"pattern", "roots"}, {"scope", {1, 2, v}}},
                   "scope[2] must be an integer");
    }
    json many = json::array();
    for (int i = 0; i <= kMaxChords; ++i) {
        many.push_back(1);
    }
    checkError(store, {{"cmd", "generateBass"}, {"trackId", 1}, {"pattern", "roots"}, {"scope", many}},
               "\"scope\" must hold at most " + std::to_string(kMaxChords) + " ids");
    many.erase(many.size() - 1);  // exactly kMaxChords: accepted (repeats count once)
    CHECK(changes(store, {{"cmd", "generateBass"}, {"trackId", 1}, {"pattern", "roots"}, {"scope", many}}));
    // Beyond 64 bits as raw text.
    const std::string head = R"({"schema":1,"cmd":"generateBass","trackId":1,"pattern":"roots","scope":[)";
    for (const char* beyond : {"9223372036854775808", "-9223372036854775809", "18446744073709551616", "1e19"}) {
        checkErrorRaw(store, head + beyond + "]}", "scope[0] must be an integer");
    }
}

TEST_CASE("generateLead: replaces the scope's notes in one undo step, every chord without a scope, nothing selected") {
    DocumentStore store = chordsAndNotes();
    store.setNoteSelection(1, {1, 2});
    const std::uint64_t revision = store.revision();
    const std::pair<int, int> depth = historyDepth(store);
    // Held tone over C and G7: C5 and B4 (F, outside the scope, still moves the line, to C5).
    CHECK(changes(store, {{"cmd", "generateLead"}, {"trackId", 1}, {"pattern", "heldTone"}, {"scope", {3, 1}}}));
    CHECK(notesOfTrack(store) == std::vector<TrackNote>{TrackNote{4, 72, 1, 0, kBarTicks, 100},
                                                        TrackNote{2, 38, 2, 200, 480, 100},
                                                        TrackNote{5, 71, 3, 0, kBarTicks, 100}});
    CHECK(store.revision() == revision + 1);
    CHECK(historyDepth(store).first == depth.first + 1);
    CHECK(store.noteSelection() == NoteSelection{1, {2}});  // the generated notes are not selected
    // No scope, and an empty one: every chord. Motif without a variation is variation 0: four notes a bar.
    CHECK(changes(store, {{"cmd", "generateLead"}, {"trackId", 1}, {"pattern", "motif"}}));
    CHECK(notesOfTrack(store).size() == 12);
    CHECK(changes(store, {{"cmd", "generateLead"}, {"trackId", 1}, {"pattern", "arpeggio"}, {"scope", json::array()}}));
    CHECK(notesOfTrack(store).size() == 24);
    // Every pattern name is accepted, with variation 0 spelled out or not; a baseRevision is not read.
    for (const char* pattern : {"heldTone", "guideTones", "arpeggio", "stepwise", "motif", "followFeel"}) {
        CAPTURE(pattern);
        CHECK(changes(store, {{"cmd", "generateLead"}, {"trackId", 1}, {"pattern", pattern}, {"scope", {2}},
                              {"baseRevision", 1}}));
        CHECK(changes(store, {{"cmd", "generateLead"}, {"trackId", 1}, {"pattern", pattern}, {"scope", {2}},
                              {"variation", 0}}));
    }
}

TEST_CASE("generateLead: Motif's variation, reduced modulo 64") {
    DocumentStore store = chordsAndNotes();
    const auto pitchesAndOffsets = [](const DocumentStore& s) {
        std::vector<std::pair<int, int>> out;
        for (const TrackNote& n : notesOfTrack(s)) {
            out.emplace_back(n.offsetTick, n.pitch);
        }
        return out;
    };
    CHECK(changes(store, {{"cmd", "generateLead"}, {"trackId", 1}, {"pattern", "motif"}, {"variation", 9}}));
    const auto nine = pitchesAndOffsets(store);
    std::vector<std::pair<int, int>> expected;
    for (const GeneratedNote& g : generateLead(store.doc(), LeadPattern::Motif, {}, 9)) {
        expected.emplace_back(g.offsetTick, g.pitch);
    }
    CHECK(nine == expected);
    CHECK(changes(store, {{"cmd", "generateLead"}, {"trackId", 1}, {"pattern", "motif"}, {"variation", 73}}));
    CHECK(pitchesAndOffsets(store) == nine);
    CHECK(changes(store, {{"cmd", "generateLead"}, {"trackId", 1}, {"pattern", "motif"}, {"variation", 2147483647}}));
    CHECK(changes(store, {{"cmd", "generateLead"}, {"trackId", 1}, {"pattern", "motif"}, {"variation", 0}}));
    CHECK_FALSE(pitchesAndOffsets(store) == nine);
    // Non-zero for another pattern: the store's refusal.
    for (const char* pattern : {"heldTone", "guideTones", "arpeggio", "stepwise", "followFeel"}) {
        CAPTURE(pattern);
        for (const json& v : {json(1), json(64), json(2147483647)}) {
            checkRefusedWith(store, {{"cmd", "generateLead"}, {"trackId", 1}, {"pattern", pattern}, {"variation", v}},
                             "generateLead", "variation is only supported for motif");
        }
    }
}

TEST_CASE("generateLead: \"variation\": -0 is the JSON integer 0, accepted for every pattern") {
    DocumentStore store = chordsAndNotes();
    for (const char* pattern : {"heldTone", "motif", "followFeel"}) {
        CAPTURE(pattern);
        const std::string text = std::string(R"({"schema":1,"cmd":"generateLead","trackId":1,"pattern":")") + pattern +
                                 R"(","variation":-0})";
        std::string error;
        bool changed = false;
        CHECK(applyCommandJson(store, text, error, &changed));
        CHECK(error.empty());
        CHECK(changed);
    }
    // The Motif notes -0 writes are variation 0's.
    std::vector<int> pitches;
    for (const TrackNote& n : notesOfTrack(store)) {
        pitches.push_back(n.pitch);
    }
    CHECK(changes(store, {{"cmd", "generateLead"}, {"trackId", 1}, {"pattern", "followFeel"}, {"variation", 0}}));
    CHECK(changes(store, {{"cmd", "generateLead"}, {"trackId", 1}, {"pattern", "motif"}}));
    std::vector<int> zero;
    for (const TrackNote& n : notesOfTrack(store)) {
        zero.push_back(n.pitch);
    }
    std::string error;
    REQUIRE(applyCommandJson(store, R"({"schema":1,"cmd":"generateLead","trackId":1,"pattern":"motif","variation":-0})",
                             error));
    std::vector<int> minusZero;
    for (const TrackNote& n : notesOfTrack(store)) {
        minusZero.push_back(n.pitch);
    }
    CHECK(minusZero == zero);
}

TEST_CASE("generateLead: nothing owned and nothing generated is an accepted no-op") {
    DocumentStore store;
    REQUIRE(store.appendChord(kC, 0.5) == 1);
    REQUIRE(store.addTrack(TrackRole::Lead) == 1);
    applyOk(store, {{"cmd", "setFeel"}, {"rhythm", "arpeggio"}, {"arpPattern", "up"}, {"arpRate", "half"},
                    {"strum", "off"}, {"strumSpeed", "medium"}, {"humanise", "off"}});
    const std::uint64_t revision = store.revision();
    CHECK_FALSE(changes(store, {{"cmd", "generateLead"}, {"trackId", 1}, {"pattern", "followFeel"}}));
    CHECK(store.revision() == revision);
}

TEST_CASE("generateLead: refusals by the store name the command and its reason") {
    DocumentStore store = chordsAndNotes();
    checkRefusedWith(store, {{"cmd", "generateLead"}, {"trackId", 9}, {"pattern", "motif"}}, "generateLead",
                     "No track with id 9");
    checkRefusedWith(store, {{"cmd", "generateLead"}, {"trackId", 1}, {"pattern", "motif"}, {"scope", {1, 42}}},
                     "generateLead", "No chord with id 42");
    DocumentStore empty;
    REQUIRE(empty.addTrack(TrackRole::Lead) == 1);
    checkRefusedWith(empty, {{"cmd", "generateLead"}, {"trackId", 1}, {"pattern", "stepwise"}}, "generateLead",
                     "Add chords first");
    // Eight notes sounding over the second half of G7 on another track: Held tone over G7 would be a ninth.
    REQUIRE(store.addTrack(TrackRole::Other) == 2);
    std::vector<NoteSpec> pad;
    for (int i = 0; i < kMaxTrackPolyphony; ++i) {
        pad.push_back(NoteSpec{60 + i, 2 * kBarTicks + 2 * kFeelTicksPerBeat, 2 * kFeelTicksPerBeat});
    }
    REQUIRE(store.insertNotes(2, pad).size() == static_cast<std::size_t>(kMaxTrackPolyphony));
    checkRefusedWith(store, {{"cmd", "generateLead"}, {"trackId", 1}, {"pattern", "heldTone"}, {"scope", {3}}},
                     "generateLead", "At most " + std::to_string(kMaxTrackPolyphony) + " track notes can sound at once");
}

TEST_CASE("generateLead: every field's type and range error names the field") {
    DocumentStore store = chordsAndNotes();
    const std::string patterns =
        "must be \"heldTone\", \"guideTones\", \"arpeggio\", \"stepwise\", \"motif\" or \"followFeel\"";
    const std::string variationRange = "\"variation\" must be an integer in 0..2147483647";
    checkError(store, {{"cmd", "generateLead"}, {"pattern", "motif"}}, "missing \"trackId\"");
    for (const json& v : {json("1"), json(1.5), json(nullptr), json(true), json(2147483648ll), json(-2147483649ll)}) {
        checkError(store, {{"cmd", "generateLead"}, {"trackId", v}, {"pattern", "motif"}}, "\"trackId\" must be an integer");
    }
    checkError(store, {{"cmd", "generateLead"}, {"trackId", 1}}, "missing \"pattern\"");
    for (const json& v : {json("Motif"), json("roots"), json("walking"), json(""), json(4), json(nullptr),
                          json::array({"motif"})}) {
        checkError(store, {{"cmd", "generateLead"}, {"trackId", 1}, {"pattern", v}}, "\"pattern\" " + patterns);
    }
    for (const json& v : {json("1"), json(1), json(nullptr), json::object()}) {
        checkError(store, {{"cmd", "generateLead"}, {"trackId", 1}, {"pattern", "motif"}, {"scope", v}},
                   "\"scope\" must be an array");
    }
    for (const json& v : {json("2"), json(1.5), json(nullptr), json(true), json(2147483648ll)}) {
        checkError(store, {{"cmd", "generateLead"}, {"trackId", 1}, {"pattern", "motif"}, {"scope", {1, 2, v}}},
                   "scope[2] must be an integer");
    }
    json many = json::array();
    for (int i = 0; i <= kMaxChords; ++i) {
        many.push_back(1);
    }
    checkError(store, {{"cmd", "generateLead"}, {"trackId", 1}, {"pattern", "motif"}, {"scope", many}},
               "\"scope\" must hold at most " + std::to_string(kMaxChords) + " ids");
    // variation: a JSON integer in 0..2147483647, checked at 64 bits before narrowing, for every pattern.
    for (const char* pattern : {"motif", "heldTone"}) {
        for (const json& v : {json(-1), json(-2147483648ll), json(2147483648ll), json(4294967296ll),
                              json(9223372036854775807ll), json(-9223372036854775807ll - 1), json(true), json(false),
                              json(nullptr), json("1"), json("0"), json(1.0), json(0.0), json(1.5), json::array(),
                              json::object()}) {
            CAPTURE(v.dump());
            checkError(store, {{"cmd", "generateLead"}, {"trackId", 1}, {"pattern", pattern}, {"variation", v}},
                       variationRange);
        }
    }
    const std::string head = R"({"schema":1,"cmd":"generateLead","trackId":1,"pattern":"motif","variation":)";
    for (const char* raw : {"9223372036854775808", "-9223372036854775809", "18446744073709551615", "18446744073709551616",
                            "1e19", "1e2", "1.0", "-0.0", "2147483648"}) {
        CAPTURE(raw);
        checkErrorRaw(store, head + raw + "}", variationRange);
    }
    // A field error comes before the store's checks: a bad variation on an unknown track is the field's error.
    checkError(store, {{"cmd", "generateLead"}, {"trackId", 9}, {"pattern", "heldTone"}, {"variation", -1}},
               variationRange);
}

TEST_CASE("updateNotes: absolute values, a moved start re-anchors, an absent field keeps the note's, no-ops accepted") {
    DocumentStore store = chordsAndNotes();
    const std::vector<TrackNote> before = notesOfTrack(store);
    // Pitch only, velocity only, length only.
    CHECK(changes(store, {{"cmd", "updateNotes"}, {"trackId", 1}, {"notes", {{{"id", 1}, {"pitch", 41}}}}}));
    CHECK(changes(store, {{"cmd", "updateNotes"}, {"trackId", 1}, {"notes", {{{"id", 1}, {"velocity", 20}}}}}));
    CHECK(changes(store, {{"cmd", "updateNotes"}, {"trackId", 1}, {"notes", {{{"id", 1}, {"length", 960}}}}}));
    CHECK(notesOfTrack(store).at(0) == TrackNote{1, 41, 1, 100, 960, 20});
    // A start across a boundary: the note changes its chord.
    CHECK(changes(store, {{"cmd", "updateNotes"}, {"trackId", 1}, {"notes", {{{"id", 1}, {"start", kBarTicks + 50}}}}}));
    CHECK(noteIds(store) == std::vector<int>{1, 2, 3});
    CHECK(notesOfTrack(store).at(0).anchorEventId == 2);
    CHECK(notesOfTrack(store).at(0).offsetTick == 50);
    // The snapshot's own start is no move; the same values again are no change at all.
    const std::uint64_t revision = store.revision();
    CHECK_FALSE(changes(store, {{"cmd", "updateNotes"}, {"trackId", 1}, {"notes", {{{"id", 1}, {"start", kBarTicks + 50}, {"pitch", 41}}}}}));
    CHECK_FALSE(changes(store, {{"cmd", "updateNotes"}, {"trackId", 1}, {"notes", json::array()}}));
    CHECK(store.revision() == revision);
    // Several notes in one step.
    const std::pair<int, int> depth = historyDepth(store);
    CHECK(changes(store, {{"cmd", "updateNotes"}, {"trackId", 1}, {"notes", {{{"id", 2}, {"pitch", 1}}, {{"id", 3}, {"pitch", 2}}}}}));
    CHECK(historyDepth(store).first == depth.first + 1);
    // The store's refusals.
    checkRefusedWith(store, {{"cmd", "updateNotes"}, {"trackId", 1}, {"notes", {{{"id", 99}, {"pitch", 1}}}}}, "updateNotes",
                     "No note with id 99 in track 1");
    checkRefusedWith(store, {{"cmd", "updateNotes"}, {"trackId", 1}, {"notes", {{{"id", 2}, {"pitch", 1}}, {{"id", 2}, {"pitch", 3}}}}},
                     "updateNotes", "Note 2 is listed twice");
    checkRefusedWith(store, {{"cmd", "updateNotes"}, {"trackId", 8}, {"notes", json::array()}}, "updateNotes", "No track with id 8");
    checkRefusedWith(store, {{"cmd", "updateNotes"}, {"trackId", 1}, {"notes", {{{"id", 2}, {"start", 3 * kBarTicks}}}}}, "updateNotes",
                     "A note must start before the end of the progression");
}

TEST_CASE("selectNotes: normalises to the track's note order, drops unknown ids, never moves the revision") {
    DocumentStore store = chordsAndNotes();
    const std::uint64_t revision = store.revision();
    const std::pair<int, int> depth = historyDepth(store);
    CHECK_FALSE(changes(store, {{"cmd", "selectNotes"}, {"trackId", 1}, {"ids", {3, 1, 99, 3}}}));
    CHECK(store.noteSelection() == NoteSelection{1, {1, 3}});  // note order, repeats collapsed, 99 dropped
    CHECK_FALSE(changes(store, {{"cmd", "selectNotes"}, {"trackId", 1}, {"ids", {2}}}));
    CHECK(store.noteSelection() == NoteSelection{1, {2}});
    CHECK(store.revision() == revision);
    CHECK(historyDepth(store) == depth);
    // Only unknown ids: nothing is selected.
    CHECK_FALSE(changes(store, {{"cmd", "selectNotes"}, {"trackId", 1}, {"ids", {98, 99}}}));
    CHECK(store.noteSelection() == NoteSelection{});
    // Empty ids clear; so does a null track, and kNoTrackId (-1) reaches the store as the same value.
    applyOk(store, {{"cmd", "selectNotes"}, {"trackId", 1}, {"ids", {1}}});
    CHECK_FALSE(changes(store, {{"cmd", "selectNotes"}, {"trackId", 1}, {"ids", json::array()}}));
    CHECK(store.noteSelection() == NoteSelection{});
    applyOk(store, {{"cmd", "selectNotes"}, {"trackId", 1}, {"ids", {1}}});
    CHECK_FALSE(changes(store, {{"cmd", "selectNotes"}, {"trackId", nullptr}, {"ids", {1}}}));
    CHECK(store.noteSelection() == NoteSelection{});
    applyOk(store, {{"cmd", "selectNotes"}, {"trackId", 1}, {"ids", {1}}});
    CHECK_FALSE(changes(store, {{"cmd", "selectNotes"}, {"trackId", -1}, {"ids", json::array()}}));
    CHECK(store.noteSelection() == NoteSelection{});
    // A track no track has is a no-op, as selectEvent with an unknown id: the selection stays.
    applyOk(store, {{"cmd", "selectNotes"}, {"trackId", 1}, {"ids", {2}}});
    CHECK_FALSE(changes(store, {{"cmd", "selectNotes"}, {"trackId", 9}, {"ids", {1}}}));
    CHECK(store.noteSelection() == NoteSelection{1, {2}});
    // Errors.
    checkError(store, {{"cmd", "selectNotes"}, {"ids", {1}}}, "missing \"trackId\"");
    checkError(store, {{"cmd", "selectNotes"}, {"trackId", 1}}, "missing \"ids\"");
    for (const json& bad : {json("1"), json(1.5), json(true), json::array(), json(2147483648ll)}) {
        checkError(store, {{"cmd", "selectNotes"}, {"trackId", bad}, {"ids", {1}}}, "\"trackId\" must be an integer or null");
    }
    checkError(store, {{"cmd", "selectNotes"}, {"trackId", 1}, {"ids", {1, 1.5}}}, "ids[1] must be an integer");
    checkError(store, {{"cmd", "selectNotes"}, {"trackId", 1}, {"ids", "all"}}, "\"ids\" must be an array");
    // Edits keep the selection, dropping what is gone (the store's rule).
    applyOk(store, {{"cmd", "selectNotes"}, {"trackId", 1}, {"ids", {1, 2, 3}}});
    applyOk(store, {{"cmd", "removeNotes"}, {"trackId", 1}, {"ids", {2}}});
    CHECK(store.noteSelection() == NoteSelection{1, {1, 3}});
}

TEST_CASE("documentChanged: true for note edits, track edits and the persisted live flags; false for selection, refusals, no-ops") {
    DocumentStore store = chordsAndNotes();
    const json one = json::array({noteJson(60, 100, 480)});
    SUBCASE("true") {
        CHECK(changes(store, {{"cmd", "insertNotes"}, {"trackId", 1}, {"notes", one}}));
        CHECK(changes(store, {{"cmd", "updateNotes"}, {"trackId", 1}, {"notes", {{{"id", 1}, {"pitch", 99}}}}}));
        CHECK(changes(store, {{"cmd", "removeNotes"}, {"trackId", 1}, {"ids", {2}}}));
        CHECK(changes(store, {{"cmd", "addTrack"}, {"role", "lead"}}));
        CHECK(changes(store, {{"cmd", "patchTrack"}, {"id", 2}, {"name", "x"}}));
        CHECK(changes(store, {{"cmd", "moveTrack"}, {"id", 2}, {"index", 0}}));
        CHECK(changes(store, {{"cmd", "removeTrack"}, {"id", 2}}));
        CHECK(changes(store, {{"cmd", "setTrackMute"}, {"id", 1}, {"on", true}}));
        CHECK(changes(store, {{"cmd", "setTrackSolo"}, {"id", 1}, {"on", true}}));
        CHECK(changes(store, {{"cmd", "setChordsMute"}, {"on", true}}));
        CHECK(changes(store, {{"cmd", "setChordsSolo"}, {"on", true}}));
        CHECK(changes(store, {{"cmd", "insertChords"}, {"index", 0},
                              {"chords", {{{"root", 0}, {"type", "maj"},
                                           {"notes", {{{"lane", {{"name", "Bass"}, {"role", "bass"}}}, {"offset", 0}, {"length", 480}, {"pitch", 40}}}}}}}}));
    }
    SUBCASE("false") {
        CHECK_FALSE(changes(store, {{"cmd", "selectNotes"}, {"trackId", 1}, {"ids", {1, 2}}}));
        CHECK_FALSE(changes(store, {{"cmd", "selectNotes"}, {"trackId", nullptr}, {"ids", json::array()}}));
        // Accepted no-ops.
        CHECK_FALSE(changes(store, {{"cmd", "removeNotes"}, {"trackId", 1}, {"ids", {99}}}));
        CHECK_FALSE(changes(store, {{"cmd", "updateNotes"}, {"trackId", 1}, {"notes", {{{"id", 1}, {"pitch", 36}}}}}));
        CHECK_FALSE(changes(store, {{"cmd", "patchTrack"}, {"id", 1}, {"name", "Bass"}}));
        CHECK_FALSE(changes(store, {{"cmd", "moveTrack"}, {"id", 1}, {"index", 0}}));
        CHECK_FALSE(changes(store, {{"cmd", "setTrackMute"}, {"id", 1}, {"on", false}}));
        CHECK_FALSE(changes(store, {{"cmd", "setChordsSolo"}, {"on", false}}));
        // Refusals, errors and a stale base.
        for (const json& cmd : {json{{"cmd", "removeTrack"}, {"id", 9}}, json{{"cmd", "setTrackMute"}, {"id", 9}, {"on", true}},
                                json{{"cmd", "insertNotes"}, {"trackId", 1}, {"notes", one}, {"baseRevision", 999}},
                                json{{"cmd", "addTrack"}, {"role", "tuba"}}}) {
            json c = cmd;
            c["schema"] = kJsonSchema;
            std::string error;
            bool changed = true;
            CHECK_FALSE(applyCommandJson(store, c.dump(), error, &changed));
            CHECK_FALSE(changed);
        }
    }
}

TEST_CASE("track and note commands are undoable steps with a revision each; the live flags and the selection are not") {
    DocumentStore store = chordsAndNotes();
    const std::uint64_t r0 = store.revision();
    applyOk(store, {{"cmd", "addTrack"}, {"role", "other"}});
    applyOk(store, {{"cmd", "insertNotes"}, {"trackId", 2}, {"notes", {noteJson(60, 0, 480)}}});
    applyOk(store, {{"cmd", "updateNotes"}, {"trackId", 2}, {"notes", {{{"id", 4}, {"pitch", 61}}}}});
    applyOk(store, {{"cmd", "removeNotes"}, {"trackId", 2}, {"ids", {4}}});
    applyOk(store, {{"cmd", "patchTrack"}, {"id", 2}, {"role", "lead"}});
    applyOk(store, {{"cmd", "moveTrack"}, {"id", 2}, {"index", 0}});
    CHECK(store.revision() == r0 + 6);
    applyOk(store, {{"cmd", "setTrackSolo"}, {"id", 2}, {"on", true}});
    applyOk(store, {{"cmd", "selectNotes"}, {"trackId", 1}, {"ids", {1}}});
    CHECK(store.revision() == r0 + 6);
    for (int i = 0; i < 6; ++i) {
        applyOk(store, {{"cmd", "undo"}});
    }
    CHECK(store.revision() == r0 + 12);  // an undo is a content change too
    CHECK(trackIds(store) == std::vector<int>{1});
}

// --- insertChords carries notes (D21) ---

namespace {

json laneJson(const std::string& name, const char* role, std::optional<int> trackId = std::nullopt) {
    json lane = {{"name", name}, {"role", role}};
    if (trackId.has_value()) {
        lane["trackId"] = *trackId;
    }
    return lane;
}

json chordNoteJson(json lane, std::int64_t offset, std::int64_t length, int pitch, std::optional<int> velocity = std::nullopt) {
    json note = {{"lane", std::move(lane)}, {"offset", offset}, {"length", length}, {"pitch", pitch}};
    if (velocity.has_value()) {
        note["velocity"] = *velocity;
    }
    return note;
}

json insertWith(int index, json notes, double lengthBeats = 4.0) {
    return {{"cmd", "insertChords"},
            {"index", index},
            {"chords", json::array({json{{"root", 0}, {"type", "maj"}, {"lengthBeats", lengthBeats}, {"notes", std::move(notes)}}})}};
}

}  // namespace

TEST_CASE("insertChords: entries with notes resolve each lane by track id, else name and role, else a new track") {
    DocumentStore store = chordsAndNotes();
    applyOk(store, {{"cmd", "addTrack"}, {"role", "lead"}});
    REQUIRE(trackIds(store) == std::vector<int>{1, 2});
    const std::uint64_t revision = store.revision();
    // One entry: a note by id into the Lead track (whatever its name and role say), one by name and role into Bass,
    // two into one new "Pad".
    const json notes = json::array({chordNoteJson(laneJson("whatever", "bass", 2), 480, 960, 72, 90),
                                    chordNoteJson(laneJson("Bass", "bass"), 0, 480, 33),
                                    chordNoteJson(laneJson("Pad", "other"), 0, 480, 60),
                                    chordNoteJson(laneJson("Pad", "other"), 960, 480, 64)});
    CHECK(changes(store, insertWith(1, notes)));  // a new chord (id 4) before the old second one
    CHECK(store.revision() == revision + 1);
    REQUIRE(trackIds(store) == std::vector<int>{1, 2, 3});
    CHECK(store.doc().tracks.at(2).name == "Pad");
    CHECK(store.doc().tracks.at(2).role == TrackRole::Other);
    CHECK(noteIds(store, 0) == std::vector<int>{1, 5, 2, 3});  // note 5 (pitch 33) at the new chord, before the moved ones
    CHECK(notesOfTrack(store, 0).at(1) == TrackNote{5, 33, 4, 0, 480, 100});  // velocity defaults to 100
    CHECK(notesOfTrack(store, 1) == std::vector<TrackNote>{TrackNote{4, 72, 4, 480, 960, 90}});
    CHECK(notesOfTrack(store, 2) == std::vector<TrackNote>{TrackNote{6, 60, 4, 0, 480, 100}, TrackNote{7, 64, 4, 960, 480, 100}});
    CHECK(store.nextTrackId() == 4);
    CHECK(store.nextNoteId() == 8);
    CHECK(noteStartOf(store, 4) == kBarTicks + 480);
    CHECK(historyDepth(store).first == 3 + 1 + 1 + 1 + 1);  // chords, Bass, its notes, Lead, this one step
    applyOk(store, {{"cmd", "undo"}});  // one step takes the chord, the notes and the new track
    CHECK(trackIds(store) == std::vector<int>{1, 2});
    CHECK(store.doc().events.size() == 3);
    CHECK(noteIds(store, 0) == std::vector<int>{1, 2, 3});
}

TEST_CASE("insertChords: notes carried inactive; anchors on the new chords; the existing notes keep theirs") {
    DocumentStore store = chordsAndNotes();
    // A one-beat chord at the start with a note at offset 2000: past its end, inactive, kept.
    applyOk(store, insertWith(0, {chordNoteJson(laneJson("Bass", "bass", 1), 2000, 480, 50)}, 1.0));
    const TrackNote& carried = notesOfTrack(store).at(1);  // starts at tick 2000, after the one at 100 + 960
    REQUIRE(carried.id == 4);
    CHECK(carried.anchorEventId == 4);
    CHECK(carried.offsetTick == 2000);
    CHECK_FALSE(noteActive(store.doc(), carried));
    for (const TrackNote& n : notesOfTrack(store)) {
        if (n.id <= 3) {
            CHECK(n.anchorEventId == n.id);  // C, F, G7 were ids 1..3 and keep their notes
        }
    }
}

TEST_CASE("insertChords: an ambiguous lane and a lane with no room are refused whole") {
    DocumentStore store = chordsAndBass();
    applyOk(store, {{"cmd", "addTrack"}, {"role", "bass"}, {"name", "Bass"}});  // a second "Bass" Bass track
    REQUIRE(trackIds(store) == std::vector<int>{1, 2});
    checkRefusedWith(store, insertWith(0, {chordNoteJson(laneJson("Bass", "bass"), 0, 480, 40)}), "insertChords",
                     "Lane \"Bass\" (bass) matches more than one track");
    CHECK(changes(store, insertWith(0, {chordNoteJson(laneJson("Bass", "bass", 2), 0, 480, 40)})));  // an id settles it
    applyOk(store, {{"cmd", "addTrack"}, {"role", "lead"}});
    applyOk(store, {{"cmd", "addTrack"}, {"role", "other"}});
    REQUIRE(store.doc().tracks.size() == static_cast<std::size_t>(kMaxTracks));
    checkRefusedWith(store, insertWith(0, {chordNoteJson(laneJson("Pad", "other"), 0, 480, 40)}), "insertChords", "At most 4 tracks");
}

TEST_CASE("insertChords: a refusal by the track limits keeps the new track and ids out; counters and history unchanged") {
    DocumentStore store = chordsAndBass();
    json stack = json::array();
    for (int i = 0; i <= kMaxTrackPolyphony; ++i) {
        stack.push_back(chordNoteJson(laneJson("Stack", "lead"), 0, 480, 40 + i));
    }
    checkRefusedWith(store, insertWith(0, stack), "insertChords",
                     kPolyphonyLimit);
    CHECK(trackIds(store) == std::vector<int>{1});
}

TEST_CASE("insertChords: note entries' errors name the chord, the note and the field") {
    DocumentStore store = chordsAndBass();
    const json good = chordNoteJson(laneJson("Bass", "bass"), 0, 480, 40);
    const auto withNotes = [](json notes) {
        return json{{"cmd", "insertChords"},
                    {"index", 0},
                    {"chords", json::array({json{{"root", 0}, {"type", "maj"}},
                                            json{{"root", 5}, {"type", "maj"}, {"notes", std::move(notes)}}})}};
    };
    const auto note = [&](const std::function<void(json&)>& spoil) {
        json n = good;
        spoil(n);
        return withNotes(json::array({good, n}));
    };
    const std::string at = "chords[1].notes[1].";
    checkError(store, note([](json& n) { n["lane"]["role"] = "Bass"; }), "chords[1].notes[1].lane.role " + kRoleMessage);
    checkError(store, note([](json& n) { n["lane"]["role"] = 3; }), "chords[1].notes[1].lane.role " + kRoleMessage);
    checkError(store, note([](json& n) { n["lane"].erase("role"); }), "missing chords[1].notes[1].lane.role");
    checkError(store, note([](json& n) { n["lane"].erase("name"); }), "missing chords[1].notes[1].lane.name");
    checkError(store, note([](json& n) { n["lane"]["name"] = 5; }), "chords[1].notes[1].lane.name must be a string");
    checkError(store, note([](json& n) { n["lane"]["name"] = std::string(65, 'x'); }),
               "chords[1].notes[1].lane.name must be at most 64 bytes");
    checkError(store, note([](json& n) { n["lane"]["trackId"] = "1"; }), "chords[1].notes[1].lane.trackId must be an integer");
    checkError(store, note([](json& n) { n["lane"]["trackId"] = 2147483648ll; }), "chords[1].notes[1].lane.trackId must be an integer");
    checkError(store, note([](json& n) { n.erase("lane"); }), "missing chords[1].notes[1].lane");
    checkError(store, note([](json& n) { n["lane"] = "Bass"; }), "chords[1].notes[1].lane must be an object");
    checkError(store, note([](json& n) { n["offset"] = 1.5; }), at + "offset must be an integer " + kInt64Range);
    checkError(store, note([](json& n) { n["offset"] = "0"; }), at + "offset must be an integer " + kInt64Range);
    checkError(store, note([](json& n) { n.erase("offset"); }), "missing " + at + "offset");
    checkError(store, note([](json& n) { n["length"] = nullptr; }), at + "length must be an integer " + kInt64Range);
    checkError(store, note([](json& n) { n.erase("length"); }), "missing " + at + "length");
    checkError(store, note([](json& n) { n["pitch"] = 1.5; }), at + "pitch must be an integer");
    checkError(store, note([](json& n) { n["pitch"] = 2147483648ll; }), at + "pitch must be an integer");
    checkError(store, note([](json& n) { n.erase("pitch"); }), "missing " + at + "pitch");
    checkError(store, note([](json& n) { n["velocity"] = "loud"; }), at + "velocity must be an integer");
    checkError(store, withNotes(json::array({good, 7})), "chords[1].notes[1] must be an object");
    checkError(store, withNotes(json("notes")), "chords[1].notes must be an array");
    checkErrorRaw(store,
                  R"({"schema":1,"cmd":"insertChords","index":0,"chords":[{"root":0,"type":"maj","notes":[{"lane":{"name":"B","role":"bass"},"offset":9223372036854775808,"length":1,"pitch":1}]}]})",
                  "chords[0].notes[0].offset must be an integer " + kInt64Range);
    // The batch is bounded as a whole.
    json many = json::array();
    for (int i = 0; i <= kMaxTracks * kMaxNotesPerTrack; ++i) {
        many.push_back(good);
    }
    checkError(store, withNotes(many), "chords[1].notes must not bring the batch to more than " +
                                           std::to_string(kMaxTracks * kMaxNotesPerTrack) + " notes");
    // A chord entry without notes (or with an empty list) is as before.
    CHECK(changes(store, withNotes(json::array())));
}

TEST_CASE("insertChords: the note cap counts across entries, so the error names the entry that passes it") {
    DocumentStore store = chordsAndBass();
    // Short notes, minimal whitespace: 4097 notes stay far under kMaxJsonBytes.
    const std::string note = R"({"lane":{"name":"B","role":"bass"},"offset":0,"length":30,"pitch":40})";
    const auto notes = [&](int count) {
        std::string out = "[";
        for (int i = 0; i < count; ++i) {
            out += (i > 0 ? "," : "") + note;
        }
        return out + "]";
    };
    const std::string text = R"({"schema":1,"cmd":"insertChords","index":0,"chords":[{"root":0,"type":"maj","notes":)" +
                             notes(4000) + R"(},{"root":5,"type":"maj","notes":)" + notes(97) + "}]}";
    REQUIRE(text.size() < kMaxJsonBytes);
    checkErrorRaw(store, text,
                  "chords[1].notes must not bring the batch to more than " + std::to_string(kMaxTracks * kMaxNotesPerTrack) +
                      " notes");
}

TEST_CASE("insertChords: the largest clipboard message the bounds allow is under kMaxJsonBytes and accepted") {
    // 256 chords of four beats, 16 notes each (4096 in all, four tracks' worth), every note with a lane of a 64-byte
    // name of slashes and a trackId, compact JSON with the slashes escaped as Swift's JSONEncoder writes them.
    constexpr int chordCount = kMaxChords;
    constexpr int notesPerChord = kMaxTracks * kMaxNotesPerTrack / chordCount;
    static_assert(chordCount * notesPerChord == kMaxTracks * kMaxNotesPerTrack, "the batch is exactly the note cap");
    const auto laneName = [](int track) { return std::string(kMaxTrackNameBytes - 1, '/') + static_cast<char>('a' + track); };
    const auto escaped = [](const std::string& name) {
        std::string out;
        for (const char c : name) {
            out += c == '/' ? "\\/" : std::string(1, c);
        }
        return out;
    };
    DocumentStore store;
    for (int t = 0; t < kMaxTracks; ++t) {
        REQUIRE(store.addTrack(TrackRole::Other, laneName(t)) == t + 1);
    }
    std::string text = R"({"schema":1,"cmd":"insertChords","index":0,"chords":[)";
    for (int c = 0; c < chordCount; ++c) {
        text += (c > 0 ? "," : "") + std::string(R"({"root":0,"type":"maj","lengthBeats":4,"notes":[)");
        for (int n = 0; n < notesPerChord; ++n) {
            const int track = n % kMaxTracks;
            const int slot = n / kMaxTracks;  // 0..3: one note per beat per track, four tracks sounding together
            text += (n > 0 ? "," : "") + std::string(R"({"lane":{"trackId":)") + std::to_string(track + 1) + R"(,"name":")" +
                    escaped(laneName(track)) + R"(","role":"other"},"offset":)" + std::to_string(slot * 960) +
                    R"(,"length":480,"pitch":)" + std::to_string(36 + track * 12 + slot) + R"(,"velocity":100})";
        }
        text += "]}";
    }
    text += "]}";
    MESSAGE("worst-case clipboard insertChords message: " << text.size() << " bytes of " << kMaxJsonBytes);
    REQUIRE(text.size() < kMaxJsonBytes);
    std::string error;
    bool changed = false;
    REQUIRE_MESSAGE(applyCommandJson(store, text, error, &changed), error);
    CHECK(changed);
    CHECK(store.doc().events.size() == static_cast<std::size_t>(chordCount));
    std::size_t total = 0;
    for (const Track& track : store.doc().tracks) {
        CHECK(track.notes.size() == static_cast<std::size_t>(kMaxNotesPerTrack));
        total += track.notes.size();
    }
    CHECK(total == static_cast<std::size_t>(kMaxTracks * kMaxNotesPerTrack));
}

// --- Tracks in the snapshot and the state (tracks design D6, D7) ---

namespace {

/// The snapshot as the shell builds it (`ChordLab.mm`, `PushSnapshot`): the live chord selection and the
/// `SnapshotExtras` of the store's note selection and revision, parsed back.
json shellSnapshot(const DocumentStore& store) {
    const Document& d = store.doc();
    return json::parse(snapshotJson(d, buildPalette(d.browsedKey, d.projectKey), store.selectedEventId(),
                                    store.selectedEventIds(), SnapshotExtras{store.noteSelection(), store.revision()}));
}

/// Every track field non-default: C, F, G7 (threeChords, ids 1..3); track 1 "Bass" (bass, muted) and track 2 "Melody"
/// (lead, soloed); the chords lane muted and soloed; F shortened to two beats after its bass note at offset 2880 went
/// in, so that note (id 3) is inactive; a removed third track and a removed note (id 6), so both counters run ahead of
/// the largest ids (nextTrackId 4, nextNoteId 7); the lead notes selected; the chord F selected.
DocumentStore trackedStore() {
    DocumentStore store = threeChords();
    REQUIRE(store.addTrack(TrackRole::Bass) == 1);
    REQUIRE(store.addTrack(TrackRole::Lead, std::string("Melody")) == 2);
    REQUIRE(store.addTrack(TrackRole::Other) == 3);
    REQUIRE(store.insertNotes(1, {NoteSpec{36, 0, 960, 100}, NoteSpec{43, 1920, 960, 90},
                                  NoteSpec{41, kBarTicks + 2880, 480, 80}}) == std::vector<int>{1, 2, 3});
    REQUIRE(store.insertNotes(2, {NoteSpec{72, 960, 480, 110}, NoteSpec{76, 2 * kBarTicks + 500, 600, 127},
                                  NoteSpec{79, 2 * kBarTicks + 500, 600, 1}}) == std::vector<int>{4, 5, 6});
    REQUIRE(store.removeNotes(2, {6}));
    REQUIRE(store.removeTrack(3));
    std::string refusal;
    REQUIRE_MESSAGE(store.setLength(2, 2.0, &refusal), refusal);
    REQUIRE(store.setTrackMute(1, true));
    REQUIRE(store.setTrackSolo(2, true));
    REQUIRE(store.setChordsMute(true));
    REQUIRE(store.setChordsSolo(true));
    store.setNoteSelection(2, {5, 4});
    store.setSelectedEvent(2);
    REQUIRE(store.nextTrackId() == 4);
    REQUIRE(store.nextNoteId() == 7);
    return store;
}

/// `trackedStore`'s tracks as `stateJson` writes them.
const json kTrackedStateTracks = json::parse(R"([
  {"id":1,"name":"Bass","role":"bass","mute":true,"solo":false,"notes":[
    {"id":1,"pitch":36,"anchor":1,"offset":0,"length":960,"velocity":100},
    {"id":2,"pitch":43,"anchor":1,"offset":1920,"length":960,"velocity":90},
    {"id":3,"pitch":41,"anchor":2,"offset":2880,"length":480,"velocity":80}]},
  {"id":2,"name":"Melody","role":"lead","mute":false,"solo":true,"notes":[
    {"id":4,"pitch":72,"anchor":1,"offset":960,"length":480,"velocity":110},
    {"id":5,"pitch":76,"anchor":3,"offset":500,"length":600,"velocity":127}]}])");

/// `trackedStore`'s tracks as the snapshot publishes them (notes column-wise, one array per field): F is two beats, so G7 starts at 5760 and note 3 (offset
/// 2880 in a 1920-tick chord) is inactive; only the soloed lead is audible.
const json kTrackedSnapshotTracks = json::parse(R"([
  {"id":1,"name":"Bass","role":"bass","mute":true,"solo":false,"audible":false,"notes":{
    "id":[1,2,3],"pitch":[36,43,41],"start":[0,1920,6720],"length":[960,960,480],"velocity":[100,90,80],
    "anchor":[1,1,2],"offset":[0,1920,2880],"active":[true,true,false]}},
  {"id":2,"name":"Melody","role":"lead","mute":false,"solo":true,"audible":true,"notes":{
    "id":[4,5],"pitch":[72,76],"start":[960,6260],"length":[480,600],"velocity":[110,127],
    "anchor":[1,3],"offset":[960,500],"active":[true,true]}}])");

/// A track's `notes` in the snapshot when it holds none: every column present and empty.
json emptyNoteColumns() {
    json columns = json::object();
    for (const char* key : {"id", "pitch", "start", "length", "velocity", "anchor", "offset", "active"}) {
        columns[key] = json::array();
    }
    return columns;
}

const std::string kStateTracksRejected = "\"tracks\" rejected: ";

std::string polyphonyMessage() {
    return "At most " + std::to_string(kMaxTrackPolyphony) + " track notes can sound at once";
}

std::string onsetMessage() {
    return "At most " + std::to_string(kMaxTrackOnsetsPerBeat) + " notes can start within one beat across all tracks";
}

/// Checks that `text` is rejected whole: false with an error (equal to `expected` when given), and the document, the
/// history (the same undo), every id counter, the revision and both selections exactly as they were.
void checkTrackStateRejected(DocumentStore& store, const std::string& text, const std::string& expected = "") {
    CAPTURE(text.substr(0, 300));
    const DocumentStore before = store;
    std::string error;
    CHECK_FALSE(loadStateJson(store, text, error));
    CHECK_FALSE(error.empty());
    if (!expected.empty()) {
        CHECK(error == expected);
    }
    CHECK(store.doc() == before.doc());
    CHECK(store.nextId() == before.nextId());
    CHECK(store.nextSectionId() == before.nextSectionId());
    CHECK(store.nextTrackId() == before.nextTrackId());
    CHECK(store.nextNoteId() == before.nextNoteId());
    CHECK(store.revision() == before.revision());
    CHECK(store.selectedEventId() == before.selectedEventId());
    CHECK(store.selectedEventIds() == before.selectedEventIds());
    CHECK(store.noteSelection() == before.noteSelection());
    DocumentStore undone = store;
    DocumentStore reference = before;
    CHECK(undone.undo());
    REQUIRE(reference.undo());
    CHECK(undone.doc() == reference.doc());
}

/// `trackedStore`'s state with `edit` applied.
std::string editedTrackedState(const std::function<void(json&)>& edit) {
    json state = json::parse(stateJson(trackedStore()));
    edit(state);
    return state.dump();
}

/// A state of C, F, G7 (threeChords) with one MUTED bass track holding `notes`, each {pitch, anchor, offset, length}
/// at velocity 100, ids 1..n in the given order, the counters one past.
std::string stateWithMutedNotes(const std::vector<std::vector<int>>& notes) {
    json state = json::parse(stateJson(threeChords()));
    json list = json::array();
    int id = 0;
    for (const std::vector<int>& n : notes) {
        list.push_back({{"id", ++id}, {"pitch", n.at(0)}, {"anchor", n.at(1)}, {"offset", n.at(2)}, {"length", n.at(3)},
                        {"velocity", 100}});
    }
    state["tracks"] = json::array({json{{"id", 1}, {"name", "Bass"}, {"role", "bass"}, {"mute", true}, {"solo", false},
                                        {"notes", std::move(list)}}});
    state["nextTrackId"] = 2;
    state["nextNoteId"] = id + 1;
    return state.dump();
}

}  // namespace

TEST_CASE("snapshot: tracks, the chords lane, the note selection, the revision, ticksPerBeat and trackLimits") {
    const DocumentStore store = trackedStore();
    const json snap = shellSnapshot(store);
    CHECK(snap.at("ticksPerBeat") == 960);
    CHECK(snap.at("ticksPerBeat") == kFeelTicksPerBeat);
    CHECK(snap.at("revision") == store.revision());
    CHECK(snap.at("tracks") == kTrackedSnapshotTracks);
    CHECK(snap.at("chordsMuted") == true);
    CHECK(snap.at("chordsSolo") == true);
    CHECK(snap.at("chordsAudible") == true);  // soloed, so muting it does not count
    CHECK(snap.at("noteSelection") == json({{"trackId", 2}, {"ids", {4, 5}}}));
    CHECK(snap.at("trackLimits") == json({{"maxTracks", kMaxTracks},
                                          {"maxNotesPerTrack", kMaxNotesPerTrack},
                                          {"minNoteTicks", kMinTrackNoteTicks},
                                          {"maxPolyphony", kMaxTrackPolyphony},
                                          {"maxOnsetsPerBeat", kMaxTrackOnsetsPerBeat}}));
    CHECK(snap.at("selectedEventId") == 2);

    // Without the extras (every caller but the shell): revision 0 and no note selection; the rest is the same.
    const json plain = snapshot(store);
    CHECK(plain.at("revision") == 0);
    CHECK(plain.at("noteSelection").is_null());
    CHECK(plain.at("tracks") == kTrackedSnapshotTracks);

    // An empty document: no tracks, the lane flags false and audible.
    const json empty = shellSnapshot(DocumentStore{});
    CHECK(empty.at("tracks") == json::array());
    CHECK(empty.at("chordsMuted") == false);
    CHECK(empty.at("chordsSolo") == false);
    CHECK(empty.at("chordsAudible") == true);
    CHECK(empty.at("noteSelection").is_null());
    CHECK(empty.at("revision") == 1);
}

TEST_CASE("snapshot: noteSelection names only notes the track holds, in its note order; null otherwise") {
    const DocumentStore store = trackedStore();
    const Document& d = store.doc();
    const Palette palette = buildPalette(d.browsedKey, d.projectKey);
    const auto selectionOf = [&](const NoteSelection& selection) {
        return json::parse(snapshotJson(d, palette, kNoEventId, {}, SnapshotExtras{selection, 9})).at("noteSelection");
    };
    CHECK(selectionOf(NoteSelection{2, {5, 99, 4}}) == json({{"trackId", 2}, {"ids", {4, 5}}}));
    CHECK(selectionOf(NoteSelection{2, {99}}).is_null());
    CHECK(selectionOf(NoteSelection{7, {1}}).is_null());
    CHECK(selectionOf(NoteSelection{}).is_null());
}

TEST_CASE("snapshot: tracks[].audible and chordsAudible follow mute and solo") {
    DocumentStore store = threeChords();
    REQUIRE(store.addTrack(TrackRole::Bass) == 1);
    REQUIRE(store.addTrack(TrackRole::Lead) == 2);
    const auto audible = [&]() {
        const json snap = shellSnapshot(store);
        return std::vector<bool>{snap.at("chordsAudible").get<bool>(), snap.at("tracks").at(0).at("audible").get<bool>(),
                                 snap.at("tracks").at(1).at("audible").get<bool>()};
    };
    CHECK(audible() == std::vector<bool>{true, true, true});
    REQUIRE(store.setTrackMute(1, true));
    CHECK(audible() == std::vector<bool>{true, false, true});
    REQUIRE(store.setTrackSolo(2, true));
    CHECK(audible() == std::vector<bool>{false, false, true});
    REQUIRE(store.setChordsSolo(true));
    CHECK(audible() == std::vector<bool>{true, false, true});
    REQUIRE(store.setTrackSolo(1, true));  // a soloed lane sounds even when muted
    CHECK(audible() == std::vector<bool>{true, true, true});
    REQUIRE(store.setChordsMute(true));
    CHECK(audible() == std::vector<bool>{true, true, true});
    REQUIRE(store.setChordsSolo(false));
    CHECK(audible() == std::vector<bool>{false, true, true});
}

TEST_CASE("snapshot: each event's playStart and playEnd are chordPlaySpans on block, performing and over-budget documents") {
    const auto checkSpans = [](const DocumentStore& store) {
        const json events = shellSnapshot(store).at("events");
        const std::vector<PlaySpan> spans = chordPlaySpans(store.doc());
        REQUIRE(events.size() == spans.size());
        for (std::size_t i = 0; i < spans.size(); ++i) {
            CAPTURE(i);
            CHECK(events[i].at("playStart").get<double>() == spans[i].start);
            CHECK(events[i].at("playEnd").get<double>() == spans[i].end);
        }
        return events;
    };

    DocumentStore block;
    for (int i = 0; i < 3; ++i) block.appendChord(kC, 0.2506, 100);
    const json blockEvents = checkSpans(block);
    CHECK(blockEvents[1].at("playStart") == 0.2506);
    CHECK(blockEvents[2].at("playEnd") == 0.2506 + 0.2506 + 0.2506);

    DocumentStore performing = block;
    performing.setFeel(Feel{Rhythm::Pulse});
    const json performingEvents = checkSpans(performing);
    CHECK(performingEvents[1].at("playStart") == 241 / 960.0);
    CHECK(performingEvents[1].at("playEnd") == 481 / 960.0);
    CHECK(performingEvents[2].at("playEnd") == 722 / 960.0);
    CHECK(performingEvents[1].at("startBeat") == 0.2506);  // the strip keeps the authored positions
    CHECK(performingEvents[1].at("lengthBeats") == 0.2506);

    DocumentStore limited;
    for (int i = 0; i < 64; ++i) limited.appendChord(chord(0, "9"), 4.0, 100);
    limited.appendChord(kC, 0.2506, 100);
    limited.setFeel(Feel{Rhythm::Subdivided, Strum::Down, StrumSpeed::Medium, Humanise::Light});
    REQUIRE(shellSnapshot(limited).at("feelLimited") == true);
    const json limitedEvents = checkSpans(limited);
    CHECK(limitedEvents[64].at("playStart") == 256.0);
    CHECK(limitedEvents[64].at("playEnd") == 256.0 + 0.2506);
}

TEST_CASE("snapshot: the revision through the shell's snapshotJson across edits, undo, redo, a load and selection echoes") {
    DocumentStore store = threeChords();
    applyOk(store, {{"cmd", "addTrack"}, {"role", "bass"}});
    const std::uint64_t r0 = store.revision();
    json snap = shellSnapshot(store);
    CHECK(snap.at("revision") == r0);
    CHECK(snap.at("noteSelection").is_null());

    applyOk(store, {{"cmd", "insertNotes"}, {"trackId", 1}, {"notes", {noteJson(36, 0, 960), noteJson(38, 960, 960)}}});
    snap = shellSnapshot(store);
    CHECK(snap.at("revision") == r0 + 1);  // an edit bumps
    CHECK(snap.at("noteSelection") == json({{"trackId", 1}, {"ids", {1, 2}}}));

    applyOk(store, {{"cmd", "selectNotes"}, {"trackId", 1}, {"ids", {2}}});
    snap = shellSnapshot(store);
    CHECK(snap.at("revision") == r0 + 1);  // a selection echo: same revision, the new selection
    CHECK(snap.at("noteSelection") == json({{"trackId", 1}, {"ids", {2}}}));
    applyOk(store, {{"cmd", "selectNotes"}, {"trackId", nullptr}, {"ids", json::array()}});
    snap = shellSnapshot(store);
    CHECK(snap.at("revision") == r0 + 1);
    CHECK(snap.at("noteSelection").is_null());

    std::string error;
    CHECK_FALSE(apply(store, {{"cmd", "updateNotes"}, {"trackId", 1}, {"baseRevision", r0}, {"notes", {{{"id", 1}, {"pitch", 40}}}}},
                      error));
    CHECK(shellSnapshot(store).at("revision") == r0 + 1);  // a stale guarded edit is refused: unchanged

    applyOk(store, {{"cmd", "undo"}});
    snap = shellSnapshot(store);
    CHECK(snap.at("revision") == r0 + 2);
    CHECK(snap.at("tracks").at(0).at("notes") == emptyNoteColumns());
    applyOk(store, {{"cmd", "redo"}});
    snap = shellSnapshot(store);
    CHECK(snap.at("revision") == r0 + 3);
    CHECK(snap.at("tracks").at(0).at("notes").at("id").size() == 2);

    applyOk(store, {{"cmd", "selectNotes"}, {"trackId", 1}, {"ids", {1}}});
    REQUIRE_MESSAGE(loadStateJson(store, stateJson(store), error), error);
    snap = shellSnapshot(store);
    CHECK(snap.at("revision") == r0 + 4);  // a state load bumps and clears the selection
    CHECK(snap.at("noteSelection").is_null());

    applyOk(store, {{"cmd", "undo"}});  // nothing to undo after a load
    applyOk(store, {{"cmd", "redo"}});
    CHECK(shellSnapshot(store).at("revision") == r0 + 4);
}

TEST_CASE("state: tracks, the chords lane flags and the track and note counters round-trip; ids are never reused") {
    const DocumentStore original = trackedStore();
    const std::string text = stateJson(original);
    const json state = json::parse(text);
    CHECK(state.at("tracks") == kTrackedStateTracks);
    CHECK(state.at("chordsMuted") == true);
    CHECK(state.at("chordsSolo") == true);
    CHECK(state.at("nextTrackId") == 4);
    CHECK(state.at("nextNoteId") == 7);
    CHECK_FALSE(state.contains("noteSelection"));  // live state, never saved
    CHECK_FALSE(state.contains("revision"));

    DocumentStore loaded;
    std::string error;
    REQUIRE_MESSAGE(loadStateJson(loaded, text, error), error);
    CHECK(loaded.doc() == original.doc());
    CHECK(loaded.nextTrackId() == 4);
    CHECK(loaded.nextNoteId() == 7);
    CHECK(loaded.noteSelection() == NoteSelection{});
    CHECK(stateJson(loaded) == text);
    const json snap = shellSnapshot(loaded);
    CHECK(snap.at("tracks") == kTrackedSnapshotTracks);  // published with the derived start, anchor, offset, active
    CHECK(snap.at("noteSelection").is_null());

    // The counters were restored, not derived: the removed track 3 and note 6 are never handed out again.
    CHECK(loaded.addTrack(TrackRole::Other) == 4);
    CHECK(loaded.insertNotes(4, {NoteSpec{60, 0, 480}}) == std::vector<int>{7});
}

TEST_CASE("state: without the track keys a state loads no tracks, unmuted unsoloed chords and derived counters") {
    json bare = json::parse(stateJson(threeChords()));
    for (const char* name : {"tracks", "chordsMuted", "chordsSolo", "nextTrackId", "nextNoteId"}) {
        REQUIRE(bare.contains(name));
        bare.erase(name);
    }
    DocumentStore store = trackedStore();
    std::string error;
    REQUIRE_MESSAGE(loadStateJson(store, bare.dump(), error), error);
    CHECK(store.doc().tracks.empty());
    CHECK_FALSE(store.doc().chordsMuted);
    CHECK_FALSE(store.doc().chordsSolo);
    CHECK(store.nextTrackId() == 1);
    CHECK(store.nextNoteId() == 1);
    CHECK(store.doc().events == threeChords().doc().events);

    // Tracks without counters: one past the largest ids (track 2, note 5), not the saved 4 and 7.
    json noCounters = json::parse(stateJson(trackedStore()));
    noCounters.erase("nextTrackId");
    noCounters.erase("nextNoteId");
    noCounters.erase("chordsMuted");
    noCounters.erase("chordsSolo");
    REQUIRE_MESSAGE(loadStateJson(store, noCounters.dump(), error), error);
    CHECK(store.doc().tracks == trackedStore().doc().tracks);
    CHECK(store.nextTrackId() == 3);
    CHECK(store.nextNoteId() == 6);
    CHECK_FALSE(store.doc().chordsMuted);
    CHECK_FALSE(store.doc().chordsSolo);
}

TEST_CASE("state: a note anchored to a missing chord rejects the whole state; everything is kept") {
    DocumentStore store = trackedStore();
    checkTrackStateRejected(store, editedTrackedState([](json& s) { s["tracks"][0]["notes"][0]["anchor"] = 99; }),
                            kStateTracksRejected + "Note 1 belongs to no chord");
}

TEST_CASE("state: a malformed track rejects the whole state naming the field; everything is kept") {
    DocumentStore store = trackedStore();
    const std::string int32Range = "must be an integer in -2147483648..2147483647";
    const std::vector<std::pair<std::function<void(json&)>, std::string>> cases = {
        {[](json& s) { s["tracks"] = json::object(); }, "\"tracks\" must be an array"},
        {[](json& s) { s["tracks"][0] = 5; }, "tracks[0] must be an object"},
        {[](json& s) { s["tracks"][0]["id"] = "1"; }, "tracks[0].id must be an integer in 1..2147483646"},
        {[](json& s) { s["tracks"][0]["id"] = INT_MAX; }, "tracks[0].id must be an integer in 1..2147483646"},
        {[](json& s) { s["tracks"][0]["name"] = 3; }, "tracks[0].name must be a string"},
        {[](json& s) { s["tracks"][1]["role"] = "Lead"; }, "tracks[1].role " + kRoleMessage},
        {[](json& s) { s["tracks"][1]["role"] = "drums"; }, "tracks[1].role " + kRoleMessage},
        {[](json& s) { s["tracks"][1]["role"] = 1; }, "tracks[1].role " + kRoleMessage},
        {[](json& s) { s["tracks"][0]["mute"] = "yes"; }, "tracks[0].mute must be true or false"},
        {[](json& s) { s["tracks"][0]["solo"] = nullptr; }, "tracks[0].solo must be true or false"},
        {[](json& s) { s["tracks"][0].erase("notes"); }, "missing tracks[0].notes"},
        {[](json& s) { s["tracks"][0]["notes"] = nullptr; }, "tracks[0].notes must be an array"},
        {[](json& s) { s["tracks"][0]["notes"][1] = "note"; }, "tracks[0].notes[1] must be an object"},
        {[](json& s) { s["tracks"][0]["notes"][0]["pitch"] = 60.5; }, "tracks[0].notes[0].pitch must be an integer"},
        {[](json& s) { s["tracks"][0]["notes"][0]["pitch"] = 2147483648LL; }, "tracks[0].notes[0].pitch must be an integer"},
        {[](json& s) { s["tracks"][0]["notes"][0]["velocity"] = true; }, "tracks[0].notes[0].velocity must be an integer"},
        {[](json& s) { s["tracks"][0]["notes"][0]["anchor"] = "1"; }, "tracks[0].notes[0].anchor must be an integer"},
        {[](json& s) { s["tracks"][0]["notes"][0]["id"] = 0; }, "tracks[0].notes[0].id must be an integer in 1..2147483646"},
        {[](json& s) { s["tracks"][0]["notes"][0].erase("length"); }, "missing tracks[0].notes[0].length"},
        {[](json& s) { s["tracks"][0]["notes"][0]["offset"] = 9223372036854775808ULL; },
         "tracks[0].notes[0].offset " + int32Range},
        {[](json& s) { s["tracks"][0]["notes"][0]["offset"] = 2147483648LL; }, "tracks[0].notes[0].offset " + int32Range},
        {[](json& s) { s["tracks"][0]["notes"][0]["length"] = std::numeric_limits<std::int64_t>::min(); },
         "tracks[0].notes[0].length " + int32Range},
        {[](json& s) { s["tracks"][0]["notes"][0]["length"] = 1e3; }, "tracks[0].notes[0].length " + int32Range},
        {[](json& s) { s["chordsMuted"] = "yes"; }, "\"chordsMuted\" must be true or false"},
        {[](json& s) { s["chordsSolo"] = 1; }, "\"chordsSolo\" must be true or false"},
        {[](json& s) { s["nextTrackId"] = 2; }, "\"nextTrackId\" must exceed every track id (2)"},
        {[](json& s) { s["nextTrackId"] = 0; }, "\"nextTrackId\" must be an integer >= 1"},
        {[](json& s) { s["nextTrackId"] = "4"; }, "\"nextTrackId\" must be an integer >= 1"},
        {[](json& s) { s["nextNoteId"] = 5; }, "\"nextNoteId\" must exceed every note id (5)"},
        {[](json& s) { s["nextNoteId"] = 2147483648LL; }, "\"nextNoteId\" must be an integer >= 1"},
    };
    for (const auto& [edit, expected] : cases) {
        checkTrackStateRejected(store, editedTrackedState(edit), expected);
    }
    // A value past what an int64 holds is not an integer at all; the 64-bit reader refuses it before any narrowing.
    std::string text = stateJson(trackedStore());
    const std::string offset = "\"offset\":1920";
    REQUIRE(text.find(offset) != std::string::npos);
    std::string huge = text;
    huge.replace(huge.find(offset), offset.size(), "\"offset\":-9223372036854775809");
    checkTrackStateRejected(store, huge, "tracks[0].notes[1].offset " + int32Range);
    huge = text;
    huge.replace(huge.find(offset), offset.size(), "\"offset\":18446744073709551616");
    checkTrackStateRejected(store, huge, "tracks[0].notes[1].offset " + int32Range);
}

TEST_CASE("state: notes outside the domain, repeated ids, a long name and a fifth track are rejected by validateTracks") {
    DocumentStore store = trackedStore();
    const std::vector<std::function<void(json&)>> cases = {
        [](json& s) { s["tracks"][0]["notes"][0]["pitch"] = 128; },
        [](json& s) { s["tracks"][0]["notes"][0]["pitch"] = -1; },
        [](json& s) { s["tracks"][0]["notes"][0]["velocity"] = 0; },
        [](json& s) { s["tracks"][0]["notes"][0]["velocity"] = 128; },
        [](json& s) { s["tracks"][0]["notes"][0]["offset"] = -1; },
        [](json& s) { s["tracks"][0]["notes"][0]["length"] = kMinTrackNoteTicks - 1; },
        [](json& s) { s["tracks"][0]["notes"][0]["length"] = kMaxNoteTick + 1; },
        [](json& s) { s["tracks"][1]["notes"][0]["id"] = 1; },              // a note id used twice across tracks
        [](json& s) { s["tracks"][1]["id"] = 1; },                          // a track id used twice
        [](json& s) { std::swap(s["tracks"][0]["notes"][0], s["tracks"][0]["notes"][1]); },  // not in note order
        [](json& s) { s["tracks"][0]["name"] = std::string(static_cast<std::size_t>(kMaxTrackNameBytes) + 1, 'x'); },
        [](json& s) {
            for (int id = 3; id <= kMaxTracks + 1; ++id) {
                s["tracks"].push_back({{"id", id}, {"name", "T"}, {"role", "other"}, {"mute", false}, {"solo", false},
                                       {"notes", json::array()}});
            }
            s["nextTrackId"] = kMaxTracks + 2;
        },
    };
    for (const auto& edit : cases) {
        const std::string text = editedTrackedState(edit);
        checkTrackStateRejected(store, text);
        std::string error;
        DocumentStore probe;
        CHECK_FALSE(loadStateJson(probe, text, error));
        CHECK(error.rfind(kStateTracksRejected, 0) == 0);
    }
    // At the bounds the same edits load.
    std::string error;
    DocumentStore probe;
    CHECK_MESSAGE(loadStateJson(probe, editedTrackedState([](json& s) {
                      s["tracks"][0]["notes"][0]["pitch"] = 127;
                      s["tracks"][0]["notes"][0]["velocity"] = 1;
                      s["tracks"][0]["notes"][0]["length"] = kMinTrackNoteTicks;
                      s["tracks"][0]["name"] = std::string(static_cast<std::size_t>(kMaxTrackNameBytes), 'x');
                  }),
                                error),
                  error);
}

TEST_CASE("state: over the polyphony cap or the onset cap on a MUTED track is rejected whole; at the caps it loads") {
    DocumentStore store = trackedStore();
    std::vector<std::vector<int>> chord;  // pitches 40.. all at tick 0 for a beat
    for (int i = 0; i <= kMaxTrackPolyphony; ++i) {
        chord.push_back({40 + i, 1, 0, 960});
    }
    checkTrackStateRejected(store, stateWithMutedNotes(chord), kStateTracksRejected + polyphonyMessage());
    chord.pop_back();
    std::string error;
    DocumentStore probe;
    REQUIRE_MESSAGE(loadStateJson(probe, stateWithMutedNotes(chord), error), error);
    CHECK(probe.doc().tracks.at(0).mute);
    CHECK(trackPolyphony(probe.doc()) == kMaxTrackPolyphony);

    std::vector<std::vector<int>> run;  // touching 30-tick notes, so polyphony stays 1
    for (int i = 0; i <= kMaxTrackOnsetsPerBeat; ++i) {
        run.push_back({60, 1, i * kMinTrackNoteTicks, kMinTrackNoteTicks});
    }
    REQUIRE((kMaxTrackOnsetsPerBeat + 1) * kMinTrackNoteTicks <= 960);
    checkTrackStateRejected(store, stateWithMutedNotes(run), kStateTracksRejected + onsetMessage());
    run.pop_back();
    REQUIRE_MESSAGE(loadStateJson(probe, stateWithMutedNotes(run), error), error);
    CHECK(densestOnsetWindow(probe.doc()) == kMaxTrackOnsetsPerBeat);
}

TEST_CASE("state: tracks are validated against the clamped chord lengths, not the saved ones") {
    // Chord 1 saved 100 beats long loads 64 (61440 ticks): a note at offset 61440 or later is past its end, inactive,
    // and so out of the polyphony and onset counts; under the saved 96000 ticks every one below would sound.
    REQUIRE(kMaxLengthBeats * kFeelTicksPerBeat == 61440);
    std::vector<std::vector<int>> notes;  // kMaxTrackPolyphony + 1 at once, kMaxTrackOnsetsPerBeat + 1 in one beat
    for (int i = 0; i <= std::max(kMaxTrackPolyphony, kMaxTrackOnsetsPerBeat); ++i) {
        notes.push_back({40 + i, 1, 61440 + i, 960});
    }
    json state = json::parse(stateWithMutedNotes(notes));
    state["events"][0]["lengthBeats"] = 100;

    std::string error;
    DocumentStore probe;
    REQUIRE_MESSAGE(loadStateJson(probe, state.dump(), error), error);
    CHECK(probe.doc().events.at(0).lengthBeats == kMaxLengthBeats);
    CHECK(trackPolyphony(probe.doc()) == 0);
    CHECK(densestOnsetWindow(probe.doc()) == 0);
    const json snapNotes = shellSnapshot(probe).at("tracks").at(0).at("notes");
    REQUIRE(snapNotes.at("id").size() == notes.size());
    REQUIRE(snapNotes.at("active").size() == notes.size());
    for (const json& active : snapNotes.at("active")) {
        CHECK(active == false);
    }

    // The same notes under a chord saved at 64 beats: identical document, so identical verdict.
    state["events"][0]["lengthBeats"] = 64;
    DocumentStore same;
    REQUIRE_MESSAGE(loadStateJson(same, state.dump(), error), error);
    CHECK(same.doc() == probe.doc());

    // And the arrangement is a real one: with the notes inside the chord (offsets 0..), the caps reject it.
    for (std::vector<int>& n : notes) {
        n.at(2) -= 61440;
    }
    json inside = json::parse(stateWithMutedNotes(notes));
    inside["events"][0]["lengthBeats"] = 100;
    CHECK_FALSE(loadStateJson(same, inside.dump(), error));
    CHECK(error.rfind(kStateTracksRejected, 0) == 0);
}

TEST_CASE("insertChords: a chord carrying a note into a full track is refused with the validator's text") {
    // The per-track note limit is a structure fault, not a chord-term one (D17a): the text passes through whole.
    // kMaxTrackOnsetsPerBeat onsets a beat, none overlapping: only the count is at its cap. They span as many
    // chords of kMaxLengthBeats as they need; the new chord goes after the last.
    constexpr int spacing = kFeelTicksPerBeat / kMaxTrackOnsetsPerBeat;
    constexpr int chordTicks = static_cast<int>(kMaxLengthBeats) * kFeelTicksPerBeat;
    constexpr int chords = (kMaxNotesPerTrack * spacing + chordTicks - 1) / chordTicks;
    static_assert(spacing >= kMinTrackNoteTicks, "the notes must not overlap");
    DocumentStore store;
    for (int i = 0; i < chords; ++i) REQUIRE(store.appendChord(kC, kMaxLengthBeats) == i + 1);
    REQUIRE(store.addTrack(TrackRole::Bass) == 1);
    std::vector<NoteSpec> notes;
    for (int i = 0; i < kMaxNotesPerTrack; ++i) {
        notes.push_back(NoteSpec{40, i * spacing, 30});
    }
    REQUIRE(store.insertNotes(1, notes).size() == static_cast<std::size_t>(kMaxNotesPerTrack));
    const json one = json::array({chordNoteJson(laneJson("Bass", "bass", 1), 0, 480, 40)});
    const json cmd = insertWith(chords, one);
    REQUIRE(cmd.dump().size() < kMaxJsonBytes);
    checkTrackRefusal(store, cmd, "insertChords", "At most " + std::to_string(kMaxNotesPerTrack) + " notes per track");
}

// --- The instance block of the state and the link state (per-track instruments plan I11, I12) -------------------

namespace {

/// A store with chords, sections and tracks: `trackedStore` with two sections.
DocumentStore linkedDocumentStore() {
    DocumentStore store = trackedStore();
    REQUIRE(store.addSection(0, "Verse") == 1);
    REQUIRE(store.addSection(2, "Bridge") == 2);
    return store;
}

/// A block with every field set: two lanes and a follow that carries all three hints.
InstanceState fullInstance() {
    InstanceState s;
    s.id = "instance-1";
    s.documentId = "doc-1";
    s.lanes = {{1, "lane-uid-1"}, {2, "lane-uid-2"}};
    s.follow = LinkFollow{"leader-1", "leader-doc", "leader-lane", "Melody", "lead", "ChordLab B"};
    return s;
}

/// `fullInstance()` on `trackedStore()`, saved at 1000 x 700, with `edit` applied to the parsed state.
std::string editedInstanceState(const std::function<void(json&)>& edit) {
    const InstanceState instance = fullInstance();
    json state = json::parse(stateJson(trackedStore(), 1000, 700, &instance));
    edit(state);
    return state.dump();
}

/// Checks that `text` is rejected whole with exactly `expected` as the error, on `trackedStore()`: the document, the
/// history, every id counter, the revision, both selections and all three out-params as they were. A second call
/// without out-params gives the same refusal: the block is validated whether or not the caller asks for it.
void checkInstanceRejected(const std::string& text, const std::string& expected) {
    CAPTURE(expected);
    DocumentStore store = trackedStore();
    const DocumentStore before = store;
    const std::optional<InstanceState> sentinel = InstanceState{"sentinel", "sentinel-doc", {{7, "sentinel-lane"}}, {}};
    std::optional<InstanceState> instance = sentinel;
    int width = -7;
    int height = -7;
    std::string error;
    CHECK_FALSE(loadStateJson(store, text, error, &width, &height, &instance));
    CHECK(error == expected);
    CHECK(width == -7);
    CHECK(height == -7);
    CHECK(instance == sentinel);
    CHECK(store.doc() == before.doc());
    CHECK(store.nextId() == before.nextId());
    CHECK(store.nextSectionId() == before.nextSectionId());
    CHECK(store.nextTrackId() == before.nextTrackId());
    CHECK(store.nextNoteId() == before.nextNoteId());
    CHECK(store.revision() == before.revision());
    CHECK(store.selectedEventIds() == before.selectedEventIds());
    CHECK(store.noteSelection() == before.noteSelection());
    DocumentStore undone = store;
    DocumentStore reference = before;
    CHECK(undone.undo());
    REQUIRE(reference.undo());
    CHECK(undone.doc() == reference.doc());

    std::string again;
    CHECK_FALSE(loadStateJson(store, text, again));
    CHECK(again == expected);
    CHECK(store.doc() == before.doc());
}

/// Loads `text` on a fresh store, requiring success, and returns the block it read.
std::optional<InstanceState> loadedInstance(const std::string& text) {
    DocumentStore store;
    std::optional<InstanceState> instance = InstanceState{"stale", "stale", {}, {}};
    std::string error;
    REQUIRE_MESSAGE(loadStateJson(store, text, error, nullptr, nullptr, &instance), error);
    CHECK(error.empty());
    return instance;
}

const std::string kLinkIdRule = "must be a string of 1..64 printable ASCII characters (0x21..0x7E)";
const std::string kLaneKeyRule =
    "instance.lanes keys must be track numbers in 1..2147483646, decimal without sign or leading zeros";

}  // namespace

TEST_CASE("instance block: with and without a follow, with zero and several lanes, it goes out and comes back equal") {
    const DocumentStore original = linkedDocumentStore();
    InstanceState noFollow = fullInstance();
    noFollow.follow.reset();
    InstanceState noLanes = fullInstance();
    noLanes.lanes.clear();
    InstanceState bare;  // no lanes, no follow
    bare.id = "i";
    bare.documentId = "d";
    InstanceState chordsLane = fullInstance();  // the chords lane followed, its hints empty as the registry gives them
    chordsLane.follow = LinkFollow{"leader-2", "leader-doc-2", "chords", "Chords", "", "ChordLab C"};
    InstanceState many = fullInstance();
    many.lanes = {{1, "u-1"}, {2, "u-2"}, {9, "u-9"}, {10, "u-10"}, {2147483646, "u-max"}};

    for (const InstanceState& instance : {fullInstance(), noFollow, noLanes, bare, chordsLane, many}) {
        CAPTURE(instance.id);
        CAPTURE(instance.lanes.size());
        const std::string text = stateJson(original, 1000, 700, &instance);
        DocumentStore loaded;
        loaded.appendChord(kC);  // replaced
        std::optional<InstanceState> read;
        int width = 0;
        int height = 0;
        std::string error;
        REQUIRE_MESSAGE(loadStateJson(loaded, text, error, &width, &height, &read), error);
        REQUIRE(read.has_value());
        CHECK(*read == instance);
        CHECK(loaded.doc() == original.doc());
        CHECK(loaded.nextId() == original.nextId());
        CHECK(loaded.nextSectionId() == original.nextSectionId());
        CHECK(loaded.nextTrackId() == original.nextTrackId());
        CHECK(loaded.nextNoteId() == original.nextNoteId());
        CHECK(width == 1000);
        CHECK(height == 700);
        CHECK(stateJson(loaded, 1000, 700, &*read) == text);  // saved again, byte for byte
    }
}

TEST_CASE("instance block: the document part is the state without it, byte for byte; the block comes last") {
    const DocumentStore store = linkedDocumentStore();
    REQUIRE_FALSE(store.doc().events.empty());
    REQUIRE_FALSE(store.doc().sections.empty());
    REQUIRE_FALSE(store.doc().tracks.empty());
    InstanceState noFollow = fullInstance();
    noFollow.follow.reset();
    InstanceState noLanes = fullInstance();
    noLanes.lanes.clear();
    const std::string plain = stateJson(store, 1000, 700);
    CHECK(stateJson(store, 1000, 700, nullptr) == plain);
    for (const InstanceState& instance : {fullInstance(), noFollow, noLanes}) {
        const std::string text = stateJson(store, 1000, 700, &instance);
        // Textually: the plain text up to its closing brace, then the one member, then the brace.
        REQUIRE(text.size() > plain.size());
        CHECK(text.compare(0, plain.size() - 1, plain, 0, plain.size() - 1) == 0);
        const std::string member = text.substr(plain.size() - 1);
        CHECK(member.rfind(",\"instance\":{", 0) == 0);
        CHECK(member.back() == '}');
        // As JSON: the state with the member erased is the plain state.
        json parsed = json::parse(text);
        REQUIRE(parsed.contains("instance"));
        parsed.erase("instance");
        CHECK(parsed.dump() == plain);
    }
    // The block's own shape: every field written, `follow` null without one, hints written as they are.
    const InstanceState instance = fullInstance();
    const json block = json::parse(stateJson(store, 1000, 700, &instance)).at("instance");
    CHECK(block == json::parse(R"({"id":"instance-1","doc":"doc-1","lanes":{"1":"lane-uid-1","2":"lane-uid-2"},
        "follow":{"leader":"leader-1","doc":"leader-doc","lane":"leader-lane","laneName":"Melody","role":"lead",
                  "leaderLabel":"ChordLab B"}})"));
    const json nullFollow = json::parse(stateJson(store, 1000, 700, &noFollow)).at("instance");
    CHECK(nullFollow.at("follow").is_null());
    CHECK(json::parse(stateJson(store, 1000, 700, &noLanes)).at("instance").at("lanes") == json::object());
}

TEST_CASE("instance block: a state without it loads exactly as before and empties the out-parameter") {
    const DocumentStore original = linkedDocumentStore();
    const std::string text = stateJson(original, 1000, 700);
    DocumentStore plain;
    std::string error;
    REQUIRE_MESSAGE(loadStateJson(plain, text, error), error);

    DocumentStore loaded;
    std::optional<InstanceState> instance = fullInstance();  // a stale value the load must clear
    int width = 0;
    int height = 0;
    REQUIRE_MESSAGE(loadStateJson(loaded, text, error, &width, &height, &instance), error);
    CHECK_FALSE(instance.has_value());
    CHECK(loaded.doc() == plain.doc());
    CHECK(loaded.doc() == original.doc());
    CHECK(loaded.nextId() == plain.nextId());
    CHECK(loaded.nextSectionId() == plain.nextSectionId());
    CHECK(loaded.nextTrackId() == plain.nextTrackId());
    CHECK(loaded.nextNoteId() == plain.nextNoteId());
    CHECK(width == 1000);
    CHECK(height == 700);

    // A state with a valid block loads the same document; the block does not touch it.
    const InstanceState block = fullInstance();
    DocumentStore withBlock;
    REQUIRE_MESSAGE(loadStateJson(withBlock, stateJson(original, 1000, 700, &block), error), error);
    CHECK(withBlock.doc() == plain.doc());
    CHECK(withBlock.nextNoteId() == plain.nextNoteId());
}

TEST_CASE("instance block: a non-object block, or a bad id or doc, rejects the whole state naming the field") {
    using Edit = std::function<void(json&)>;
    const std::vector<std::pair<Edit, std::string>> cases = {
        {[](json& s) { s["instance"] = nullptr; }, "\"instance\" must be an object"},
        {[](json& s) { s["instance"] = json::array(); }, "\"instance\" must be an object"},
        {[](json& s) { s["instance"] = "instance-1"; }, "\"instance\" must be an object"},
        {[](json& s) { s["instance"] = 1; }, "\"instance\" must be an object"},
        {[](json& s) { s["instance"].erase("id"); }, "missing instance.id"},
        {[](json& s) { s["instance"]["id"] = ""; }, "instance.id " + kLinkIdRule},
        {[](json& s) { s["instance"]["id"] = std::string(65, 'a'); }, "instance.id " + kLinkIdRule},
        {[](json& s) { s["instance"]["id"] = 5; }, "instance.id " + kLinkIdRule},
        {[](json& s) { s["instance"]["id"] = nullptr; }, "instance.id " + kLinkIdRule},
        {[](json& s) { s["instance"]["id"] = "has space"; }, "instance.id " + kLinkIdRule},
        {[](json& s) { s["instance"]["id"] = "tab\there"; }, "instance.id " + kLinkIdRule},
        {[](json& s) { s["instance"]["id"] = std::string("nul\0x", 5); }, "instance.id " + kLinkIdRule},
        {[](json& s) { s["instance"]["id"] = "del\x7f"; }, "instance.id " + kLinkIdRule},
        {[](json& s) { s["instance"]["id"] = "caf\xC3\xA9"; }, "instance.id " + kLinkIdRule},
        {[](json& s) { s["instance"].erase("doc"); }, "missing instance.doc"},
        {[](json& s) { s["instance"]["doc"] = ""; }, "instance.doc " + kLinkIdRule},
        {[](json& s) { s["instance"]["doc"] = std::string(65, 'd'); }, "instance.doc " + kLinkIdRule},
        {[](json& s) { s["instance"]["doc"] = json::array({"doc-1"}); }, "instance.doc " + kLinkIdRule},
        {[](json& s) { s["instance"]["doc"] = "doc 1"; }, "instance.doc " + kLinkIdRule},
        {[](json& s) { s["instance"]["doc"] = "doc\n1"; }, "instance.doc " + kLinkIdRule},
    };
    for (const auto& [edit, expected] : cases) {
        checkInstanceRejected(editedInstanceState(edit), expected);
    }
}

TEST_CASE("instance block: bad lanes reject the whole state naming the field") {
    using Edit = std::function<void(json&)>;
    std::vector<std::pair<Edit, std::string>> cases = {
        {[](json& s) { s["instance"].erase("lanes"); }, "missing instance.lanes"},
        {[](json& s) { s["instance"]["lanes"] = json::array(); }, "instance.lanes must be an object"},
        {[](json& s) { s["instance"]["lanes"] = nullptr; }, "instance.lanes must be an object"},
        {[](json& s) { s["instance"]["lanes"]["1"] = 1; }, "instance.lanes.1 " + kLinkIdRule},
        {[](json& s) { s["instance"]["lanes"]["1"] = ""; }, "instance.lanes.1 " + kLinkIdRule},
        {[](json& s) { s["instance"]["lanes"]["1"] = std::string(65, 'u'); }, "instance.lanes.1 " + kLinkIdRule},
        {[](json& s) { s["instance"]["lanes"]["2"] = "lane uid"; }, "instance.lanes.2 " + kLinkIdRule},
        {[](json& s) { s["instance"]["lanes"]["2"] = "lane\x01uid"; }, "instance.lanes.2 " + kLinkIdRule},
        {[](json& s) { s["instance"]["lanes"]["2"] = "lane-\xC3\xA9"; }, "instance.lanes.2 " + kLinkIdRule},
        {[](json& s) { s["instance"]["lanes"]["2"] = "lane-uid-1"; }, "instance.lanes.2 repeats the uid of instance.lanes.1"},
        {[](json& s) { s["instance"]["lanes"]["2"] = "chords"; }, "instance.lanes.2 must not be \"chords\""},
    };
    for (const char* key : {"01", "-1", "+1", "1.0", "1e3", " 1", "1 ", "x", "", "0", "00", "2147483647", "2147483648",
                            "99999999999", "18446744073709551617"}) {
        cases.emplace_back([key](json& s) { s["instance"]["lanes"] = json{{key, "lane-uid-x"}}; }, kLaneKeyRule);
    }
    // A key with an escaped NUL after the digit ("1\u0000" in the text), a full-width digit one, a key of 100000 digits.
    for (const std::string& key : {std::string("1\0", 2), std::string("\xEF\xBC\x91"), std::string(100000, '1')}) {
        cases.emplace_back([key](json& s) { s["instance"]["lanes"] = json{{key, "lane-uid-x"}}; }, kLaneKeyRule);
    }
    cases.emplace_back(
        [](json& s) {
            json lanes = json::object();
            for (int i = 1; i <= 20000; ++i) {
                lanes[std::to_string(i)] = "u" + std::to_string(i);
            }
            s["instance"]["lanes"] = lanes;
        },
        "instance.lanes must hold at most 68 entries");
    cases.emplace_back(
        [](json& s) {
            json lanes = json::object();
            for (std::size_t i = 1; i <= kMaxTracks + kMaxVanishedLaneUids + 1; ++i) {
                lanes[std::to_string(i)] = "u" + std::to_string(i);
            }
            s["instance"]["lanes"] = lanes;
        },
        "instance.lanes must hold at most 68 entries");
    for (const auto& [edit, expected] : cases) {
        checkInstanceRejected(editedInstanceState(edit), expected);
    }
    static_assert(kMaxTracks + kMaxVanishedLaneUids == 68, "the message above names the cap");
}

TEST_CASE("instance block: a bad follow rejects the whole state naming the field") {
    using Edit = std::function<void(json&)>;
    std::vector<std::pair<Edit, std::string>> cases = {
        {[](json& s) { s["instance"]["follow"] = 5; }, "instance.follow must be null or an object"},
        {[](json& s) { s["instance"]["follow"] = "leader-1"; }, "instance.follow must be null or an object"},
        {[](json& s) { s["instance"]["follow"] = json::array(); }, "instance.follow must be null or an object"},
        {[](json& s) { s["instance"]["follow"] = false; }, "instance.follow must be null or an object"},
        {[](json& s) { s["instance"]["follow"]["laneName"] = std::string(65, 'n'); },
         "instance.follow.laneName must be a string of at most 64 bytes"},
        {[](json& s) { s["instance"]["follow"]["laneName"] = 5; },
         "instance.follow.laneName must be a string of at most 64 bytes"},
        {[](json& s) { s["instance"]["follow"]["role"] = 5; }, "instance.follow.role must be a string of at most 16 bytes"},
        {[](json& s) { s["instance"]["follow"]["role"] = nullptr; },
         "instance.follow.role must be a string of at most 16 bytes"},
        {[](json& s) { s["instance"]["follow"]["role"] = std::string(17, 'r'); },
         "instance.follow.role must be a string of at most 16 bytes"},
        {[](json& s) { s["instance"]["follow"]["leaderLabel"] = std::string(97, 'l'); },
         "instance.follow.leaderLabel must be a string of at most 96 bytes"},
        {[](json& s) { s["instance"]["follow"]["leaderLabel"] = json::object(); },
         "instance.follow.leaderLabel must be a string of at most 96 bytes"},
    };
    for (const char* name : {"leader", "doc", "lane"}) {
        const std::string field = std::string("instance.follow.") + name;
        cases.emplace_back([name](json& s) { s["instance"]["follow"].erase(name); }, "missing " + field);
        cases.emplace_back([name](json& s) { s["instance"]["follow"][name] = ""; }, field + " " + kLinkIdRule);
        cases.emplace_back([name](json& s) { s["instance"]["follow"][name] = 7; }, field + " " + kLinkIdRule);
        cases.emplace_back([name](json& s) { s["instance"]["follow"][name] = std::string(65, 'z'); },
                           field + " " + kLinkIdRule);
        cases.emplace_back([name](json& s) { s["instance"]["follow"][name] = "two words"; }, field + " " + kLinkIdRule);
    }
    for (const auto& [edit, expected] : cases) {
        checkInstanceRejected(editedInstanceState(edit), expected);
    }
}

TEST_CASE("instance block: every bound is inclusive: the longest ids, the edge characters, the largest key, the most lanes") {
    // 64 bytes from both ends of the printable range, quote and backslash included (they escape in JSON).
    std::string edgeId;
    for (int i = 0; i < 64; ++i) {
        edgeId.push_back("!~\"\\"[i % 4]);
    }
    REQUIRE(edgeId.size() == kMaxLinkIdBytes);
    InstanceState edges = fullInstance();
    edges.id = edgeId;
    edges.documentId = std::string(64, '~');
    edges.lanes = {{1, std::string(64, '!')}, {2147483646, edgeId}};
    edges.follow = LinkFollow{edgeId, std::string(64, 'D'), "chords", "", "", ""};
    std::string twoByteName;  // 64 bytes of UTF-8: the bound is bytes, not characters
    for (int i = 0; i < 32; ++i) {
        twoByteName += "\xC3\xA9";
    }
    InstanceState longHints = fullInstance();
    longHints.follow->laneName = twoByteName;
    longHints.follow->laneRole = std::string(16, 'r');
    longHints.follow->leaderLabel = std::string(96, 'l');
    InstanceState mostLanes = fullInstance();
    mostLanes.lanes.clear();
    for (int i = 0; i < static_cast<int>(kMaxTracks + kMaxVanishedLaneUids); ++i) {
        mostLanes.lanes[INT_MAX - 1 - i] = "lane-" + std::to_string(i);
    }
    REQUIRE(mostLanes.lanes.size() == 68);
    const DocumentStore store = trackedStore();
    for (const InstanceState& instance : {edges, longHints, mostLanes}) {
        const std::optional<InstanceState> read = loadedInstance(stateJson(store, 900, 620, &instance));
        REQUIRE(read.has_value());
        CHECK(*read == instance);
    }
    // The smallest: one-byte ids, the key "1".
    const std::optional<InstanceState> small = loadedInstance(editedInstanceState([](json& s) {
        s["instance"]["id"] = "a";
        s["instance"]["doc"] = "b";
        s["instance"]["lanes"] = json{{"1", "c"}};
        s["instance"]["follow"] = json{{"leader", "d"}, {"doc", "e"}, {"lane", "f"}};
    }));
    REQUIRE(small.has_value());
    CHECK(*small == InstanceState{"a", "b", {{1, "c"}}, LinkFollow{"d", "e", "f", "", "", ""}});
}

TEST_CASE("instance block: absent hints load empty, an absent follow is null, unknown members are ignored, a repeated member takes its last value") {
    const std::optional<InstanceState> noHints = loadedInstance(editedInstanceState([](json& s) {
        s["instance"]["follow"].erase("laneName");
        s["instance"]["follow"].erase("role");
        s["instance"]["follow"].erase("leaderLabel");
    }));
    REQUIRE(noHints.has_value());
    REQUIRE(noHints->follow.has_value());
    CHECK(*noHints->follow == LinkFollow{"leader-1", "leader-doc", "leader-lane", "", "", ""});

    const std::optional<InstanceState> extra = loadedInstance(editedInstanceState([](json& s) {
        s["instance"]["future"] = json::array({1, "x", nullptr});
        s["instance"]["follow"]["future"] = json::object();
    }));
    CHECK(extra == fullInstance());

    const std::optional<InstanceState> nullFollow =
        loadedInstance(editedInstanceState([](json& s) { s["instance"]["follow"] = nullptr; }));
    REQUIRE(nullFollow.has_value());
    CHECK_FALSE(nullFollow->follow.has_value());
    // An absent follow is a null one: not following (a writer that leaves a null follow out stays readable).
    const std::optional<InstanceState> noFollow =
        loadedInstance(editedInstanceState([](json& s) { s["instance"].erase("follow"); }));
    REQUIRE(noFollow.has_value());
    CHECK_FALSE(noFollow->follow.has_value());
    CHECK(noFollow->lanes == fullInstance().lanes);

    // A repeated member, as nlohmann reads it (json_api.h): two "instance" members are the last one, and a track
    // number written twice is its last uid (the first, a uid of another entry, would otherwise be a repeat).
    const InstanceState first = fullInstance();
    const std::string text = stateJson(trackedStore(), 900, 620, &first);
    std::string twice = text.substr(0, text.size() - 1) +
                        R"(,"instance":{"id":"second","doc":"d2","lanes":{"1":"lane-uid-2","1":"u-last"},"follow":null}})";
    const std::optional<InstanceState> last = loadedInstance(twice);
    CHECK(last == InstanceState{"second", "d2", {{1, "u-last"}}, {}});
    // And member order means nothing: the block read from the front of the state is the same block.
    std::string front = text;
    const std::size_t at = front.rfind(",\"instance\":");
    REQUIRE(at != std::string::npos);
    const std::string member = front.substr(at + 1, front.size() - 2 - at);
    front.erase(at, front.size() - 1 - at);
    front.insert(1, member + ",");
    CHECK(front.rfind("{\"instance\":", 0) == 0);
    CHECK(loadedInstance(front) == first);
}

namespace {

/// One block per kind of rule violation, each `fullInstance()` broken in exactly one way, with the field the reason
/// names.
std::vector<std::pair<InstanceState, std::string>> brokenInstances() {
    std::vector<std::pair<InstanceState, std::string>> out;
    auto broken = [&](const std::string& field, const std::function<void(InstanceState&)>& edit) {
        InstanceState s = fullInstance();
        edit(s);
        out.emplace_back(std::move(s), field);
    };
    broken("lanes", [](InstanceState& s) { s.lanes[0] = "lane-uid-0"; });
    broken("lanes", [](InstanceState& s) { s.lanes[-3] = "lane-uid-m3"; });
    broken("lanes", [](InstanceState& s) { s.lanes[INT_MAX] = "lane-uid-max"; });
    broken("lanes.2", [](InstanceState& s) { s.lanes[2] = "lane-uid-1"; });
    broken("lanes.2", [](InstanceState& s) { s.lanes[2] = "chords"; });
    broken("lanes.2", [](InstanceState& s) { s.lanes[2] = "lane uid"; });
    broken("lanes", [](InstanceState& s) {
        for (int i = 1; i <= 69; ++i) s.lanes[i] = "u" + std::to_string(i);
    });
    broken("id", [](InstanceState& s) { s.id = "has space"; });
    broken("id", [](InstanceState& s) { s.id.clear(); });
    broken("id", [](InstanceState& s) { s.id = std::string(65, 'i'); });
    broken("doc", [](InstanceState& s) { s.documentId = "caf\xC3\xA9"; });
    broken("follow.lane", [](InstanceState& s) { s.follow->laneUid.clear(); });
    broken("follow.leader", [](InstanceState& s) { s.follow->leaderId = "a\tb"; });
    broken("follow.doc", [](InstanceState& s) { s.follow->documentId = std::string(65, 'd'); });
    return out;
}

}  // namespace

TEST_CASE("validInstanceState: the block's rules on plain data, a reason naming the field for each kind of violation") {
    std::string why = "untouched";
    CHECK(validInstanceState(fullInstance(), &why));
    CHECK(why == "untouched");
    CHECK(validInstanceState(fullInstance()));
    InstanceState bare;
    bare.id = "i";
    bare.documentId = "d";
    CHECK(validInstanceState(bare));
    for (const auto& entry : brokenInstances()) {
        const InstanceState& instance = entry.first;
        const std::string& field = entry.second;
        CAPTURE(field);
        std::string reason;
        CHECK_FALSE(validInstanceState(instance, &reason));
        CHECK(reason.rfind(field, 0) == 0);
        CHECK_FALSE(validInstanceState(instance));
        // The reader agrees: written as given, the block is refused.
        json state = json::parse(stateJson(trackedStore()));
        json lanes = json::object();
        for (const auto& [track, uid] : instance.lanes) lanes[std::to_string(track)] = uid;
        const LinkFollow& f = *instance.follow;
        state["instance"] = {{"id", instance.id}, {"doc", instance.documentId}, {"lanes", lanes},
                             {"follow", {{"leader", f.leaderId}, {"doc", f.documentId}, {"lane", f.laneUid}}}};
        DocumentStore store;
        std::string error;
        CHECK_FALSE(loadStateJson(store, state.dump(), error));
        CHECK(error.find("instance") != std::string::npos);
    }
    // The hints: over their bound or not valid UTF-8.
    for (const auto& edit : std::vector<std::function<void(LinkFollow&)>>{
             [](LinkFollow& f) { f.laneName = std::string(65, 'n'); },
             [](LinkFollow& f) { f.laneName = "cut \xC3"; },
             [](LinkFollow& f) { f.laneRole = std::string(17, 'r'); },
             [](LinkFollow& f) { f.laneRole = "\xED\xA0\x80"; },  // a UTF-16 surrogate, encoded: not UTF-8
             [](LinkFollow& f) { f.leaderLabel = std::string(97, 'l'); },
             [](LinkFollow& f) { f.leaderLabel = "\xC0\x80"; }}) {  // an overlong NUL
        InstanceState s = fullInstance();
        edit(*s.follow);
        std::string reason;
        CHECK_FALSE(validInstanceState(s, &reason));
        CHECK(reason.rfind("follow.", 0) == 0);
    }
    InstanceState fourByte = fullInstance();
    fourByte.follow->laneName = "\xF0\x9F\x8E\xB9 keys";  // valid four-byte UTF-8
    CHECK(validInstanceState(fourByte));
}

TEST_CASE("writableInstanceState: a block that breaks a rule is left out, and the state written without it loads") {
    const DocumentStore store = linkedDocumentStore();
    for (const auto& entry : brokenInstances()) {
        const InstanceState& instance = entry.first;
        CAPTURE(entry.second);
        std::optional<InstanceState> written = fullInstance();
        CHECK_NOTHROW(written = writableInstanceState(instance));
        CHECK_FALSE(written.has_value());
        // What stateJson then writes: the state without the block, which loads with the document and no block.
        std::string text;
        CHECK_NOTHROW(text = stateJson(store, 1000, 700, written ? &*written : nullptr));
        DocumentStore loaded;
        std::optional<InstanceState> read = fullInstance();
        std::string error;
        REQUIRE_MESSAGE(loadStateJson(loaded, text, error, nullptr, nullptr, &read), error);
        CHECK_FALSE(read.has_value());
        CHECK(loaded.doc() == store.doc());
    }
    CHECK(writableInstanceState(fullInstance()) == fullInstance());  // a valid block unchanged
}

TEST_CASE("stateJson: a hint that is too long or not valid UTF-8 is written empty; the rest of the block survives") {
    const DocumentStore store = linkedDocumentStore();
    const std::vector<std::function<void(LinkFollow&)>> edits = {
        [](LinkFollow& f) { f.laneName = std::string(65, 'n'); },
        [](LinkFollow& f) { f.laneName = "cut \xC3"; },
        [](LinkFollow& f) { f.laneRole = std::string(17, 'r'); },
        [](LinkFollow& f) { f.laneRole = "\xFF"; },
        [](LinkFollow& f) { f.leaderLabel = std::string(97, 'l'); },
        [](LinkFollow& f) { f.leaderLabel = "bad \xE2\x82"; },
        [](LinkFollow& f) {
            f.laneName = "\x80";
            f.laneRole = std::string(1000, 'r');
            f.leaderLabel = "\xC3";
        },
    };
    for (std::size_t i = 0; i < edits.size(); ++i) {
        CAPTURE(i);
        InstanceState given = fullInstance();
        edits[i](*given.follow);
        // Expected: every hint that broke its rule emptied, everything else as given.
        InstanceState expected = given;
        LinkFollow& f = *expected.follow;
        const InstanceState full = fullInstance();
        const LinkFollow& original = *full.follow;
        if (f.laneName != original.laneName) f.laneName.clear();
        if (f.laneRole != original.laneRole) f.laneRole.clear();
        if (f.leaderLabel != original.leaderLabel) f.leaderLabel.clear();
        CHECK(writableInstanceState(given) == expected);

        std::string text;
        CHECK_NOTHROW(text = stateJson(store, 1000, 700, &given));
        DocumentStore loaded;
        std::optional<InstanceState> read;
        std::string error;
        REQUIRE_MESSAGE(loadStateJson(loaded, text, error, nullptr, nullptr, &read), error);
        CHECK(read == expected);
        CHECK(loaded.doc() == store.doc());
    }
}

namespace {

/// 64 bytes of quotes and backslashes (each escapes to two bytes), the bits of `n` choosing which: distinct for
/// distinct `n`, and as wide as a valid id prints.
std::string widestLinkId(std::uint64_t n) {
    std::string id;
    for (std::size_t bit = 0; bit < kMaxLinkIdBytes; ++bit) {
        id.push_back(((n >> bit) & 1u) != 0 ? '\\' : '"');
    }
    return id;
}

/// The widest valid block: ids and uids of `widestLinkId`, `kMaxTracks + kMaxVanishedLaneUids` lanes keyed by
/// ten-digit track numbers, and every hint at its byte bound of '\x01' (six bytes each as `\u0001`).
InstanceState worstCaseInstance() {
    InstanceState s;
    s.id = widestLinkId(1);
    s.documentId = widestLinkId(2);
    for (std::uint64_t i = 0; i < kMaxTracks + kMaxVanishedLaneUids; ++i) {
        s.lanes[INT_MAX - 1 - static_cast<int>(i)] = widestLinkId(100 + i);
    }
    s.follow = LinkFollow{widestLinkId(3), widestLinkId(4), widestLinkId(5),
                          std::string(static_cast<std::size_t>(kMaxTrackNameBytes), '\x01'), std::string(16, '\x01'),
                          std::string(kMaxLinkLabelBytes, '\x01')};
    return s;
}

/// `stateJson` of `worstCaseStore()` with `worstCaseInstance()` at the default editor size, measured; pinned beside
/// `kWorstCaseStateBytes` so that a change to the block's shape shows up here against `kMaxJsonBytes`.
constexpr std::size_t kWorstCaseStateWithInstanceBytes = 575235;

}  // namespace

TEST_CASE("capacity: the largest possible state with the largest instance block is under kMaxJsonBytes and round-trips") {
    const DocumentStore original = worstCaseStore();
    const InstanceState instance = worstCaseInstance();
    REQUIRE(instance.lanes.size() == kMaxTracks + kMaxVanishedLaneUids);
    const std::string text = stateJson(original, kDefaultEditorWidth, kDefaultEditorHeight, &instance);
    MESSAGE("worst-case state with the instance block: " << text.size() << " bytes");
    CHECK(text.size() == kWorstCaseStateWithInstanceBytes);
    CHECK(text.size() < kMaxJsonBytes);
    CHECK(stateJson(original).size() == kWorstCaseStateBytes);  // the block adds to the old pin, nothing else

    DocumentStore loaded;
    std::optional<InstanceState> read;
    std::string error;
    REQUIRE_MESSAGE(loadStateJson(loaded, text, error, nullptr, nullptr, &read), error);
    CHECK(loaded.doc() == original.doc());
    REQUIRE(read.has_value());
    CHECK(*read == instance);
    CHECK(stateJson(loaded, kDefaultEditorWidth, kDefaultEditorHeight, &*read) == text);
}

// --- linkStateJson --------------------------------------------------------------------------------------------

namespace {

/// A leader's view: itself, one other leader with the chords lane and a track lane, and two followers routed.
LinkView leaderView() {
    LinkView v;
    v.id = "self-id";
    v.label = "ChordLab A";
    LinkLeaderView other;
    other.id = "other-id";
    other.label = "ChordLab B";
    other.documentId = "other-doc";
    other.lineage = "lineage-7";
    other.lanes.push_back(LinkLaneView{"chords", "Chords", TrackRole::Bass, true, true, 0});
    other.lanes.push_back(LinkLaneView{"uid-bass", "Bass", TrackRole::Bass, false, false, 2});
    v.leaders.push_back(other);
    v.routed.push_back(LinkLaneView{"uid-lead", "Lead", TrackRole::Lead, false, true, 2});
    return v;
}

/// A follower's view with `status`, following "uid-bass" of "other-id".
LinkView followerView(LinkStatus status) {
    LinkView v = leaderView();
    v.routed.clear();
    v.status = status;
    v.follow = LinkFollow{"other-id", "other-doc", "uid-bass", "Bass", "bass", "ChordLab B"};
    return v;
}

}  // namespace

TEST_CASE("linkStateJson: a leader's view, every member as documented") {
    const json j = json::parse(linkStateJson(leaderView()));
    CHECK(j == json::parse(R"({"schema":1,"self":{"id":"self-id","label":"ChordLab A"},"following":null,
        "leaders":[{"id":"other-id","label":"ChordLab B","lineage":"lineage-7","lanes":[
            {"lane":"chords","name":"Chords","role":"","chords":true,"audible":true,"followers":0},
            {"lane":"uid-bass","name":"Bass","role":"bass","chords":false,"audible":false,"followers":2}]}],
        "routed":[{"lane":"uid-lead","followers":2}]})"));
    LinkView empty;
    CHECK(json::parse(linkStateJson(empty)) ==
          json::parse(R"({"schema":1,"self":{"id":"","label":""},"following":null,"leaders":[],"routed":[]})"));
}

TEST_CASE("linkStateJson: each status, and following null exactly when there is no follow or the status says so") {
    const std::vector<std::pair<LinkStatus, const char*>> statuses = {{LinkStatus::Playing, "playing"},
                                                                      {LinkStatus::Muted, "muted"},
                                                                      {LinkStatus::Waiting, "waiting"},
                                                                      {LinkStatus::LaneMissing, "laneMissing"},
                                                                      {LinkStatus::ProjectChanged, "projectChanged"}};
    for (const auto& entry : statuses) {
        const LinkStatus status = entry.first;
        const char* const name = entry.second;
        CAPTURE(name);
        const json j = json::parse(linkStateJson(followerView(status)));
        CHECK(j.at("following") == json({{"leader", "other-id"}, {"leaderLabel", "ChordLab B"}, {"lane", "uid-bass"},
                                         {"laneName", "Bass"}, {"role", "bass"}, {"status", name},
                                         {"switchTo", nullptr}}));
        CHECK(j.at("routed") == json::array());
    }
    CHECK(json::parse(linkStateJson(followerView(LinkStatus::NotFollowing))).at("following").is_null());
    LinkView noFollow = followerView(LinkStatus::Playing);
    noFollow.follow.reset();
    CHECK(json::parse(linkStateJson(noFollow)).at("following").is_null());
}

TEST_CASE("linkStateJson: switchTo is null or names the copy to switch to") {
    LinkView v = followerView(LinkStatus::Playing);
    CHECK(json::parse(linkStateJson(v)).at("following").at("switchTo").is_null());
    v.switchTo = LinkInstanceRef{"copy-id", "ChordLab D"};
    CHECK(json::parse(linkStateJson(v)).at("following").at("switchTo") ==
          json({{"leader", "copy-id"}, {"label", "ChordLab D"}}));
}

TEST_CASE("linkStateJson: invalid UTF-8 in names and labels is replaced, never thrown on") {
    LinkView v = followerView(LinkStatus::Muted);
    v.label = "bad \xFF label";
    v.follow->laneName = "cut \xC3";
    v.leaders[0].lanes[1].name = "\xE2\x82";
    v.switchTo = LinkInstanceRef{"copy-id", "\x80"};
    std::string text;
    CHECK_NOTHROW(text = linkStateJson(v));
    json j;
    CHECK_NOTHROW(j = json::parse(text));
    CHECK(j.at("self").at("label") == "bad \xEF\xBF\xBD label");
    CHECK(j.at("following").at("laneName").get<std::string>().find("\xEF\xBF\xBD") != std::string::npos);
    CHECK(j.at("leaders").at(0).at("lanes").at(1).at("name").get<std::string>().find("\xEF\xBF\xBD") !=
          std::string::npos);
    CHECK(j.at("following").at("switchTo").at("label") == "\xEF\xBF\xBD");
}
