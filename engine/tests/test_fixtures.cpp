// Golden JSON fixtures for the UI contract (json_api.h). The committed files in tests/fixtures/ are what the
// Swift side decodes; this test builds the same documents through the public API and compares the engine's
// output with them byte for byte, so a change to the wire format has to update the fixtures on purpose.
//
// To regenerate after a deliberate contract change:
//     CHORDLAB_UPDATE_FIXTURES=1 build/engine_tests -tc="golden fixtures*"
// (the test rewrites the files, still passes, and a second run without the variable proves equality).
//
// The two *-legacy-phase1.json files are hand-written Phase 1 shapes; they are never rewritten.

#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/document.h"
#include "chordlab/json_api.h"
#include "chordlab/palette.h"
#include "chordlab/pitch.h"
#include "chordlab/relatedness.h"
#include "chordlab/scale.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace chordlab;
using nlohmann::json;

namespace {

const std::string kFixtureDir = CHORDLAB_FIXTURE_DIR;

bool updateMode() {
    const char* v = std::getenv("CHORDLAB_UPDATE_FIXTURES");
    return v != nullptr && std::string(v) == "1";
}

bool readFile(const std::string& name, std::string& out) {
    std::ifstream in(kFixtureDir + "/" + name, std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

/// Compares `actual` with the committed file `name` byte for byte, or rewrites the file in update mode.
void checkGolden(const std::string& name, const std::string& actual) {
    CAPTURE(name);
    if (updateMode()) {
        std::ofstream out(kFixtureDir + "/" + name, std::ios::binary | std::ios::trunc);
        REQUIRE_MESSAGE(out.good(), "cannot write fixture " << name);
        out.write(actual.data(), static_cast<std::streamsize>(actual.size()));
        out.close();
        REQUIRE_MESSAGE(!out.fail(), "writing fixture " << name << " failed; the file may be truncated");
        return;
    }
    std::string expected;
    REQUIRE_MESSAGE(readFile(name, expected), "fixture missing: " << kFixtureDir << "/" << name
                                                  << " (run with CHORDLAB_UPDATE_FIXTURES=1 to create it)");
    CHECK_MESSAGE(expected == actual, "fixture " << name << " differs from the engine's output; if the change is "
                                                 << "deliberate, regenerate with CHORDLAB_UPDATE_FIXTURES=1");
}

Chord chordOf(PitchClass root, const char* typeId) {
    return Chord{root, &chordType(typeId)};
}

/// Every top-level member of a snapshot, as documented in json_api.h (17 of them). A contract change updates
/// this list and the fixtures together: the snapshot's key set must equal it, so an undocumented key and a
/// missing documented one both fail.
const std::set<std::string> kSnapshotKeys = {
    "schema",         "projectKey",     "browsedKey",   "projectKeyName", "browsedKeyName",
    "progressionLoop", "followHost",    "autoVoiceLead", "endBeat",       "selectedEventId",
    "modes",          "tonics",         "events",       "sections",       "suggestions",
    "palette",        "homeTriads"};

void checkSnapshotShape(const std::string& text) {
    const json j = json::parse(text);
    REQUIRE(j.is_object());
    std::set<std::string> actual;
    for (auto it = j.begin(); it != j.end(); ++it) {
        actual.insert(it.key());
    }
    for (const std::string& key : kSnapshotKeys) {
        CHECK_MESSAGE(actual.count(key) == 1, "snapshot lacks documented key " << key);
    }
    for (const std::string& key : actual) {
        CHECK_MESSAGE(kSnapshotKeys.count(key) == 1, "snapshot has undocumented key " << key);
    }
    CHECK(actual == kSnapshotKeys);
    CHECK(j.at("schema") == kJsonSchema);
}

/// The progression fixture: ids 1..4 = C (4 beats), Am (4), F (2), G7 (6); Am manual (inversion 1);
/// autoVoiceLead on; sections 1 "Verse" (at 0, colour 0) and 2 "Chorus" (at 2, colour 3); F selected (id 3).
/// `browsed` is optional: the browsed key, when it is to differ from the project key.
DocumentStore progressionStore(const Key* browsed) {
    DocumentStore store;
    const int c = store.appendChord(chordOf(0, "maj"), 4.0, 100);
    const int am = store.appendChord(chordOf(9, "min"), 4.0, 100);
    const int f = store.appendChord(chordOf(5, "maj"), 2.0, 100);
    const int g7 = store.appendChord(chordOf(7, "7"), 6.0, 100);
    REQUIRE(c == 1);
    REQUIRE(am == 2);
    REQUIRE(f == 3);
    REQUIRE(g7 == 4);
    store.setInversion(am, 1);
    store.setAutoVoiceLead(true);
    const int verse = store.addSection(0, "Verse");
    const int chorus = store.addSection(2, "Chorus");
    REQUIRE(verse == 1);
    REQUIRE(chorus == 2);
    store.setSectionColour(chorus, 3);
    store.setSelectedEvent(f);
    if (browsed != nullptr) {
        store.setBrowsedKey(*browsed);
    }
    return store;
}

std::string snapshotOf(const DocumentStore& store) {
    const Document& d = store.doc();
    return snapshotJson(d, buildPalette(d.browsedKey, d.projectKey), store.selectedEventId());
}

std::string homeEmpty() {
    DocumentStore store;
    return snapshotOf(store);
}

std::string homeProgression() {
    return snapshotOf(progressionStore(nullptr));
}

std::string browsingProgression() {
    const Key aAeolian{Tonic{'A', 0}, Mode::Aeolian};
    return snapshotOf(progressionStore(&aAeolian));
}

std::string stateProgression() {
    return stateJson(progressionStore(nullptr), 900, 600);
}

}  // namespace

TEST_CASE("golden fixtures: snapshots and state match the committed files byte for byte") {
    const std::string empty = homeEmpty();
    const std::string home = homeProgression();
    const std::string browsing = browsingProgression();
    const std::string state = stateProgression();

    checkGolden("snapshot-home-empty.json", empty);
    checkGolden("snapshot-home-progression.json", home);
    checkGolden("snapshot-browsing-progression.json", browsing);
    checkGolden("state-progression.json", state);
}

TEST_CASE("golden fixtures: generated snapshots carry the documented top-level keys") {
    for (const std::string& text : {homeEmpty(), homeProgression(), browsingProgression()}) {
        checkSnapshotShape(text);
    }
    const json empty = json::parse(homeEmpty());
    CHECK(empty.at("events").empty());
    CHECK(empty.at("sections").empty());
    CHECK(empty.at("selectedEventId").is_null());
    CHECK(empty.at("homeTriads").empty());
    CHECK(empty.at("projectKeyName") == "C Ionian");

    const json home = json::parse(homeProgression());
    CHECK(home.at("events").size() == 4);
    CHECK(home.at("sections").size() == 2);
    CHECK(home.at("selectedEventId") == 3);
    CHECK(home.at("autoVoiceLead") == true);
    CHECK(home.at("homeTriads").empty());
    CHECK(home.at("events").at(1).at("manualVoicing") == true);

    const json browsing = json::parse(browsingProgression());
    CHECK(browsing.at("browsedKeyName") == "A Aeolian");
    CHECK(browsing.at("homeTriads").size() == 7);
}

TEST_CASE("golden fixtures: the state fixture round-trips") {
    DocumentStore loaded;
    std::string error;
    int w = 0;
    int h = 0;
    REQUIRE_MESSAGE(loadStateJson(loaded, stateProgression(), error, &w, &h), error);
    CHECK(w == 900);
    CHECK(h == 600);
    const DocumentStore original = progressionStore(nullptr);
    CHECK(loaded.doc() == original.doc());
    CHECK(loaded.nextId() == original.nextId());
    CHECK(loaded.nextSectionId() == original.nextSectionId());
    CHECK(stateJson(loaded, 900, 600) == stateProgression());
}

// ---- Phase 1 legacy documents (hand-written, never regenerated) ----

TEST_CASE("golden fixtures: the legacy Phase 1 state loads with Phase 2 defaults") {
    std::string text;
    REQUIRE_MESSAGE(readFile("state-legacy-phase1.json", text), "state-legacy-phase1.json missing");
    const json raw = json::parse(text);
    for (const char* absent : {"autoVoiceLead", "sections", "nextSectionId", "relationMode"}) {
        CHECK_FALSE(raw.contains(absent));
    }
    for (const json& e : raw.at("events")) {
        for (const char* absent : {"inversion", "octave", "manualVoicing"}) {
            CHECK_FALSE(e.contains(absent));
        }
    }

    DocumentStore store;
    std::string error;
    int w = 0;
    int h = 0;
    REQUIRE_MESSAGE(loadStateJson(store, text, error, &w, &h), error);
    CHECK(error.empty());
    CHECK(w == 900);
    CHECK(h == 620);
    const Document& d = store.doc();
    REQUIRE(d.events.size() == raw.at("events").size());
    CHECK(d.events[0].id == 1);
    CHECK(d.events[1].id == 2);
    CHECK(store.nextId() == raw.at("nextId").get<int>());
    CHECK_FALSE(d.autoVoiceLead);
    CHECK(d.sections.empty());
    CHECK(d.relationMode == RelationMode::Blend);
    CHECK(store.nextSectionId() == 1);
    for (const ChordEvent& e : d.events) {
        CHECK(e.inversion == 0);
        CHECK(e.octave == 0);
        CHECK_FALSE(e.manualVoicing);
    }
    CHECK(sectionsWellFormed(d));

    // The loaded document snapshots and re-saves in the current shape.
    checkSnapshotShape(snapshotOf(store));
    DocumentStore again;
    REQUIRE_MESSAGE(loadStateJson(again, stateJson(store), error), error);
    CHECK(again.doc() == d);
}

namespace {

/// Every member name of `legacy` is a member of `current`.
void checkKeysSubset(const json& legacy, const json& current, const std::string& where) {
    for (auto it = legacy.begin(); it != legacy.end(); ++it) {
        CHECK_MESSAGE(current.contains(it.key()), where << " has key " << it.key() << " that the engine no longer writes");
    }
}

}  // namespace

TEST_CASE("golden fixtures: the legacy Phase 1 snapshot is well formed and a subset of a real one") {
    std::string text;
    REQUIRE_MESSAGE(readFile("snapshot-legacy-phase1.json", text), "snapshot-legacy-phase1.json missing");
    REQUIRE(json::accept(text));
    const json legacy = json::parse(text);
    const json current = json::parse(homeProgression());
    REQUIRE(legacy.is_object());
    CHECK(legacy.at("schema") == kJsonSchema);

    // None of the Phase 2 members.
    for (const char* absent : {"function", "tension", "why", "suggestions", "sections", "homeTriads", "autoVoiceLead",
                               "inversion", "octave", "manualVoicing", "selectedEventId"}) {
        CAPTURE(absent);
        CHECK_FALSE(text.find(std::string("\"") + absent + "\"") != std::string::npos);
    }

    checkKeysSubset(legacy, current, "snapshot");
    REQUIRE(legacy.at("events").is_array());
    REQUIRE(legacy.at("events").size() >= 1);
    for (const json& e : legacy.at("events")) {
        checkKeysSubset(e, current.at("events").at(0), "event");
    }
    checkKeysSubset(legacy.at("palette"), current.at("palette"), "palette");
    REQUIRE(legacy.at("palette").at("cells").is_array());
    REQUIRE(legacy.at("palette").at("cells").size() >= 1);
    for (const json& cell : legacy.at("palette").at("cells")) {
        checkKeysSubset(cell, current.at("palette").at("cells").at(0), "cell");
    }

    // The constant tables are exactly the engine's.
    CHECK(legacy.at("modes") == current.at("modes"));
    CHECK(legacy.at("tonics") == current.at("tonics"));
}
