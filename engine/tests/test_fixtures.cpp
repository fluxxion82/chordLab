// Golden JSON fixtures for the UI contract (json_api.h). The committed files in tests/fixtures/ are what the
// Swift side decodes; this test builds the same documents through the public API and compares the engine's
// output with them byte for byte, so a change to the wire format has to update the fixtures on purpose.
//
// To regenerate after a deliberate contract change:
//     CHORDLAB_UPDATE_FIXTURES=1 build/engine_tests -tc="golden fixtures*"
// (the test rewrites the files, still passes, and a second run without the variable proves equality).
//
// The four link-state-*.json files are `linkStateJson` of views of real registries and endpoints (link.h), built
// with fixed instance ids, fixed lineages, a counting uid generator and a registry clock the test drives, so they
// are the same on every run.
//
// The two *-legacy-phase1.json files are hand-written Phase 1 shapes, state-legacy-feel.json a pre-arpeggio feel,
// state-legacy-length.json a state from before the new-chord length and state-legacy-tracks.json a state from before
// tracks (a copy of state-progression.json as it was then); they are never rewritten.

#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/document.h"
#include "chordlab/json_api.h"
#include "chordlab/link.h"
#include "chordlab/palette.h"
#include "chordlab/pitch.h"
#include "chordlab/relatedness.h"
#include "chordlab/scale.h"
#include "chordlab/tracks.h"
#include "chordlab/triple_buffer.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
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

/// Every top-level member of a snapshot, as documented in json_api.h (37 of them). A contract change updates
/// this list and the fixtures together: the snapshot's key set must equal it, so an undocumented key and a
/// missing documented one both fail.
const std::set<std::string> kSnapshotKeys = {
    "schema",         "projectKey",     "browsedKey",   "projectKeyName", "browsedKeyName",
    "progressionLoop", "followHost",    "autoVoiceLead", "endBeat",       "selectedEventId",
    "modes",          "tonics",         "events",       "sections",       "suggestions",
    "palette",        "homeTriads",     "relationMode", "relationReference", "presets",
    "applied",        "transposeWithKey", "keyHintSeen",    "feel",           "meter",
    "feelLimited",    "midiThru",       "selectedEventIds", "newChordBeats", "ticksPerBeat",
    "revision",       "tracks",         "chordsMuted",  "chordsSolo",     "chordsAudible",
    "noteSelection",  "trackLimits"};

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
/// autoVoiceLead on; sections 1 "Verse" (at 0, colour 0) and 2 "Chorus" (at 2, colour 3); Am and F selected (ids 2, 3), F the anchor.
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
    store.setSelectedEvents({am, f}, f);  // Am and F selected, F the anchor
    if (browsed != nullptr) {
        store.setBrowsedKey(*browsed);
    }
    return store;
}

/// The snapshot as the shell builds it: the live chord selection plus the note selection and the revision.
std::string snapshotOf(const DocumentStore& store) {
    const Document& d = store.doc();
    return snapshotJson(d, buildPalette(d.browsedKey, d.projectKey), store.selectedEventId(), store.selectedEventIds(),
                        SnapshotExtras{store.noteSelection(), store.revision()});
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

/// The progression fixture with a non-default value in every feel field (arpeggio, pinky up-down, half rate, two
/// octaves, down-up strum, tight, light humanise) in 6/8 and a new-chord length of a dotted quarter.
DocumentStore feelStore() {
    DocumentStore store = progressionStore(nullptr);
    store.setFeel(Feel{Rhythm::Arpeggio, Strum::DownUp, StrumSpeed::Tight, Humanise::Light, ArpPattern::PinkyUpDown,
                       ArpRate::Half, ArpRange::Two});
    store.setMeter(Meter{6, 8});
    store.setNewChordLength(1.5);
    return store;
}

std::string stateFeel() {
    return stateJson(feelStore(), 900, 600);
}

std::string snapshotFeel() {
    return snapshotOf(feelStore());
}

/// The progression fixture with tracks (tracks design D1, D3): track 1 "Bass" (bass, muted) with notes 1-3 and
/// track 2 "Lead" (lead, soloed) with notes 4-5. Note 3 went in at offset 1440 of F, which was then shortened to one
/// beat, so it is inactive and G7 (with note 5 at offset 480) moved one beat earlier. Notes 4 and 5 are selected.
DocumentStore tracksStore() {
    DocumentStore store = progressionStore(nullptr);
    REQUIRE(store.addTrack(TrackRole::Bass) == 1);
    REQUIRE(store.addTrack(TrackRole::Lead) == 2);
    REQUIRE(store.insertNotes(1, {NoteSpec{36, 0, 960, 100}, NoteSpec{45, 3840, 1920, 90},
                                  NoteSpec{41, 7680 + 1440, 480, 80}}) == std::vector<int>{1, 2, 3});
    REQUIRE(store.insertNotes(2, {NoteSpec{72, 960, 480, 110}, NoteSpec{79, 9600 + 480, 720, 127}}) ==
            std::vector<int>{4, 5});
    std::string refusal;
    REQUIRE_MESSAGE(store.setLength(3, 1.0, &refusal), refusal);
    REQUIRE(store.setTrackMute(1, true));
    REQUIRE(store.setTrackSolo(2, true));
    store.setNoteSelection(2, {4, 5});
    return store;
}

std::string stateTracks() {
    return stateJson(tracksStore(), 900, 600);
}

std::string snapshotTracks() {
    return snapshotOf(tracksStore());
}

}  // namespace

TEST_CASE("golden fixtures: snapshots and state match the committed files byte for byte") {
    const std::string empty = homeEmpty();
    const std::string home = homeProgression();
    const std::string browsing = browsingProgression();
    const std::string state = stateProgression();
    const std::string stateWithFeel = stateFeel();
    const std::string snapshotWithFeel = snapshotFeel();
    const std::string stateWithTracks = stateTracks();
    const std::string snapshotWithTracks = snapshotTracks();

    checkGolden("snapshot-home-empty.json", empty);
    checkGolden("snapshot-home-progression.json", home);
    checkGolden("snapshot-browsing-progression.json", browsing);
    checkGolden("state-progression.json", state);
    checkGolden("state-feel.json", stateWithFeel);
    checkGolden("snapshot-feel.json", snapshotWithFeel);
    checkGolden("state-tracks.json", stateWithTracks);
    checkGolden("snapshot-tracks.json", snapshotWithTracks);
}

TEST_CASE("golden fixtures: generated snapshots carry the documented top-level keys") {
    for (const std::string& text : {homeEmpty(), homeProgression(), browsingProgression(), snapshotFeel(), snapshotTracks()}) {
        checkSnapshotShape(text);
    }
    const json empty = json::parse(homeEmpty());
    CHECK(empty.at("events").empty());
    CHECK(empty.at("sections").empty());
    CHECK(empty.at("selectedEventId").is_null());
    CHECK(empty.at("selectedEventIds") == json::array());
    CHECK(empty.at("newChordBeats").is_null());
    CHECK(empty.at("homeTriads").empty());
    CHECK(empty.at("projectKeyName") == "C Ionian");
    CHECK(empty.at("relationMode") == "blend");
    CHECK(empty.at("relationReference").is_null());
    for (const json& cell : empty.at("palette").at("cells")) {
        CHECK_FALSE(cell.contains("relation"));
        CHECK_FALSE(cell.contains("relationText"));
    }

    const json home = json::parse(homeProgression());
    CHECK(home.at("events").size() == 4);
    CHECK(home.at("sections").size() == 2);
    CHECK(home.at("selectedEventId") == 3);
    CHECK(home.at("selectedEventIds") == json::array({2, 3}));
    CHECK(home.at("autoVoiceLead") == true);
    CHECK(home.at("homeTriads").empty());
    CHECK(home.at("events").at(1).at("manualVoicing") == true);
    CHECK(home.at("relationReference") == json({{"eventId", 3}, {"symbol", "F"}}));  // the selection
    for (const json& cell : home.at("palette").at("cells")) {
        CHECK(cell.at("relation").is_number_integer());
        CHECK(cell.at("relationText").is_string());
    }

    const json browsing = json::parse(browsingProgression());
    CHECK(browsing.at("browsedKeyName") == "A Aeolian");
    CHECK(browsing.at("homeTriads").size() == 7);
    CHECK(browsing.at("relationReference") == json({{"eventId", 3}, {"symbol", "F"}}));
    for (const json& triad : browsing.at("homeTriads")) {
        CHECK(triad.at("relation").is_number_integer());
        CHECK(triad.at("relationText").is_string());
    }
}

TEST_CASE("golden fixtures: the feel and meter fixtures carry non-default values") {
    const json snap = json::parse(snapshotFeel());
    CHECK(snap.at("feel") ==
          json({{"rhythm", "arpeggio"}, {"strum", "downUp"}, {"strumSpeed", "tight"}, {"humanise", "light"},
                {"arpPattern", "pinkyUpDown"}, {"arpRate", "half"}, {"arpRange", "two"}}));
    CHECK(snap.at("meter") == json({{"num", 6}, {"den", 8}}));
    CHECK(snap.at("feelLimited") == false);
    CHECK(snap.at("newChordBeats") == 1.5);
    const json state = json::parse(stateFeel());
    CHECK(state.at("feel") == snap.at("feel"));
    CHECK(state.at("meter") == snap.at("meter"));
    CHECK(state.at("newChordBeats") == 1.5);
    CHECK(json::parse(stateProgression()).at("newChordBeats").is_null());

    const json home = json::parse(homeProgression());
    CHECK(home.at("feel") ==
          json({{"rhythm", "block"}, {"strum", "off"}, {"strumSpeed", "medium"}, {"humanise", "off"},
                {"arpPattern", "up"}, {"arpRate", "subdivision"}, {"arpRange", "one"}}));
    CHECK(home.at("meter") == json({{"num", 4}, {"den", 4}}));
    CHECK(home.at("feelLimited") == false);

    DocumentStore loaded;
    std::string error;
    REQUIRE_MESSAGE(loadStateJson(loaded, stateFeel(), error), error);
    CHECK(loaded.doc() == feelStore().doc());
    CHECK(stateJson(loaded, 900, 600) == stateFeel());
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

TEST_CASE("golden fixtures: the tracks fixtures carry mixed mute and solo, an inactive note and a note selection") {
    const json snap = json::parse(snapshotTracks());
    const DocumentStore store = tracksStore();
    CHECK(snap.at("revision") == store.revision());
    CHECK(snap.at("ticksPerBeat") == 960);
    CHECK(snap.at("noteSelection") == json({{"trackId", 2}, {"ids", {4, 5}}}));
    const json& tracks = snap.at("tracks");
    REQUIRE(tracks.size() == 2);
    CHECK(tracks[0].at("mute") == true);
    CHECK(tracks[0].at("audible") == false);
    CHECK(tracks[1].at("solo") == true);
    CHECK(tracks[1].at("audible") == true);
    CHECK(snap.at("chordsAudible") == false);  // a track is soloed and the chords are not
    CHECK(tracks[0].at("notes").at("id").at(2) == 3);  // notes are columns: one array per field, in note order
    CHECK(tracks[0].at("notes").at("pitch").at(2) == 41);
    CHECK(tracks[0].at("notes").at("start").at(2) == 7680 + 1440);
    CHECK(tracks[0].at("notes").at("length").at(2) == 480);
    CHECK(tracks[0].at("notes").at("velocity").at(2) == 80);
    CHECK(tracks[0].at("notes").at("anchor").at(2) == 3);
    CHECK(tracks[0].at("notes").at("offset").at(2) == 1440);
    CHECK(tracks[0].at("notes").at("active").at(2) == false);
    CHECK(tracks[1].at("notes").at("start").at(1) == 8640 + 480);  // G7 moved a beat earlier with its note
    CHECK(snap.at("events").at(3).at("playStart") == 9.0);
    for (const json& e : snap.at("events")) {  // every event carries its harmonic guide, twelve entries per view
        const json& guide = e.at("guide");
        CHECK(guide.at("roles").size() == 12);
        CHECK(guide.at("degrees").size() == 12);
        CHECK(guide.at("names").size() == 12);
        CHECK(guide.at("roles").at(e.at("root").get<std::size_t>()) == "root");
    }

    DocumentStore loaded;
    std::string error;
    REQUIRE_MESSAGE(loadStateJson(loaded, stateTracks(), error), error);
    CHECK(loaded.doc() == store.doc());
    CHECK(loaded.nextTrackId() == store.nextTrackId());
    CHECK(loaded.nextNoteId() == store.nextNoteId());
    CHECK(stateJson(loaded, 900, 600) == stateTracks());
}

TEST_CASE("legacy fixture: state-legacy-tracks.json is a state saved before tracks") {
    // Hand-kept, never regenerated: state-progression.json as it was before the tracks fields.
    std::string text;
    REQUIRE_MESSAGE(readFile("state-legacy-tracks.json", text), "state-legacy-tracks.json missing");
    const json raw = json::parse(text);
    for (const char* absent : {"tracks", "chordsMuted", "chordsSolo", "nextTrackId", "nextNoteId"}) {
        CHECK_FALSE(raw.contains(absent));
    }
    DocumentStore store;
    std::string error;
    REQUIRE_MESSAGE(loadStateJson(store, text, error), error);
    CHECK(store.doc().tracks.empty());
    CHECK_FALSE(store.doc().chordsMuted);
    CHECK_FALSE(store.doc().chordsSolo);
    CHECK(store.nextTrackId() == 1);
    CHECK(store.nextNoteId() == 1);
    CHECK(store.doc().events == progressionStore(nullptr).doc().events);
    CHECK(store.doc().sections == progressionStore(nullptr).doc().sections);
    // Saved again, it carries the tracks keys at their defaults and loads back equal.
    const json saved = json::parse(stateJson(store, 900, 600));
    CHECK(saved.at("tracks") == json::array());
    CHECK(saved.at("nextTrackId") == 1);
    DocumentStore again;
    REQUIRE_MESSAGE(loadStateJson(again, saved.dump(), error), error);
    CHECK(again.doc() == store.doc());
}

TEST_CASE("legacy fixture: state-legacy-feel.json is a state saved before the arpeggio fields") {
    // Hand-kept, never regenerated: its feel is {"rhythm":"arpUpDown", ...} with no arpPattern/arpRate/arpRange.
    std::string text;
    REQUIRE_MESSAGE(readFile("state-legacy-feel.json", text), "state-legacy-feel.json missing");
    const json j = json::parse(text);
    CHECK(j.at("feel").at("rhythm") == "arpUpDown");
    CHECK_FALSE(j.at("feel").contains("arpPattern"));
    DocumentStore store;
    std::string error;
    REQUIRE_MESSAGE(loadStateJson(store, text, error), error);
    CHECK(store.doc().feel == Feel{Rhythm::Arpeggio, Strum::DownUp, StrumSpeed::Tight, Humanise::Light, ArpPattern::UpDown});
    // Saved again, it carries the new names and the defaults for the fields it never had.
    const json saved = json::parse(stateJson(store));
    CHECK(saved.at("feel").at("rhythm") == "arpeggio");
    CHECK(saved.at("feel").at("arpPattern") == "upDown");
    CHECK(saved.at("feel").at("arpRate") == "subdivision");
    CHECK(saved.at("feel").at("arpRange") == "one");
    // And loads back equal.
    DocumentStore again;
    REQUIRE_MESSAGE(loadStateJson(again, saved.dump(), error), error);
    CHECK(again.doc() == store.doc());
}

TEST_CASE("legacy fixture: state-legacy-length.json is a state saved before the new-chord length") {
    // Hand-kept, never regenerated: state-progression.json as it was before `newChordBeats`.
    std::string text;
    REQUIRE_MESSAGE(readFile("state-legacy-length.json", text), "state-legacy-length.json missing");
    const json j = json::parse(text);
    CHECK_FALSE(j.contains("newChordBeats"));
    DocumentStore store;
    std::string error;
    REQUIRE_MESSAGE(loadStateJson(store, text, error), error);
    CHECK_FALSE(store.doc().newChordBeats.has_value());
    CHECK(store.doc().events == progressionStore(nullptr).doc().events);
    // Saved again, it carries the key (null) and loads back equal.
    const json saved = json::parse(stateJson(store, 900, 600));
    CHECK(saved.at("newChordBeats").is_null());
    DocumentStore again;
    REQUIRE_MESSAGE(loadStateJson(again, saved.dump(), error), error);
    CHECK(again.doc() == store.doc());
}

// ---- Phase 1 legacy documents (hand-written, never regenerated) ----

TEST_CASE("golden fixtures: the legacy Phase 1 state loads with Phase 2 defaults") {
    std::string text;
    REQUIRE_MESSAGE(readFile("state-legacy-phase1.json", text), "state-legacy-phase1.json missing");
    const json raw = json::parse(text);
    for (const char* absent : {"autoVoiceLead", "sections", "nextSectionId", "relationMode", "midiThru"}) {
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
    CHECK(d.midiThru == true);  // a state saved before the flag existed loads with Thru on
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

// ---- The instance block (per-track instruments plan I11): no fixture carries one ----

TEST_CASE("golden fixtures: every state fixture loads as before without an instance block and empties the out-parameter") {
    // Read only: none of these files is rewritten here, whatever CHORDLAB_UPDATE_FIXTURES says.
    for (const char* name : {"state-progression.json", "state-feel.json", "state-tracks.json", "state-legacy-phase1.json",
                             "state-legacy-feel.json", "state-legacy-length.json", "state-legacy-tracks.json"}) {
        CAPTURE(name);
        std::string text;
        REQUIRE_MESSAGE(readFile(name, text), name << " missing");
        CHECK_FALSE(json::parse(text).contains("instance"));

        DocumentStore plain;
        std::string error;
        int plainWidth = 0;
        int plainHeight = 0;
        REQUIRE_MESSAGE(loadStateJson(plain, text, error, &plainWidth, &plainHeight), error);

        DocumentStore withOut;
        std::optional<InstanceState> instance = InstanceState{"stale-id", "stale-doc", {{1, "stale-lane"}}, {}};
        int width = 0;
        int height = 0;
        REQUIRE_MESSAGE(loadStateJson(withOut, text, error, &width, &height, &instance), error);
        CHECK(error.empty());
        CHECK_FALSE(instance.has_value());
        CHECK(withOut.doc() == plain.doc());
        CHECK(withOut.nextId() == plain.nextId());
        CHECK(withOut.nextSectionId() == plain.nextSectionId());
        CHECK(withOut.nextTrackId() == plain.nextTrackId());
        CHECK(withOut.nextNoteId() == plain.nextNoteId());
        CHECK(width == plainWidth);
        CHECK(height == plainHeight);
    }
    // The generated ones are still the engine's output with no instance given, the default spelled out.
    CHECK(stateJson(progressionStore(nullptr), 900, 600, nullptr) == stateProgression());
    CHECK(stateJson(feelStore(), 900, 600, nullptr) == stateFeel());
    CHECK(stateJson(tracksStore(), 900, 600, nullptr) == stateTracks());
    std::string onDisk;
    REQUIRE(readFile("state-tracks.json", onDisk));
    CHECK(stateJson(tracksStore(), 900, 600, nullptr) == onDisk);
}

// ---- The link state (per-track instruments plan I12): what the editor's Follow controls read ----

namespace {

/// The uids of one fixture world, shared by its instances: "uid-1", "uid-2", ... in the order they are asked for
/// (document ids, lane uids, a copy's fresh id).
LinkEndpoint::UidGenerator fixtureUids() {
    auto n = std::make_shared<int>(0);
    return [n] { return "uid-" + std::to_string(++*n); };
}

/// A registry clock the fixture drives: `*now` milliseconds (the copy rules, `LinkRegistry` rule 8, compare the
/// times at which loads made copies; a fixture never reads the real clock).
LinkRegistry::Clock fixtureClock(const std::shared_ptr<std::int64_t>& now) {
    return [now] { return *now; };
}

/// One instance as the shell drives its link, reduced to that: a store, its own buffer and publication, a lineage
/// (a fixed string per load), and its endpoint, whose hook republishes. Each operation runs the hooks it returned
/// and then publishes for itself, as the shell does. Held by unique_ptr: the hook keeps `this`.
struct LinkInstance {
    DocumentStore store;
    std::unique_ptr<TripleBuffer<SnapshotSlot>> own = std::make_unique<TripleBuffer<SnapshotSlot>>();
    Publication publication;
    std::string lineage;
    std::unique_ptr<LinkEndpoint> endpoint;  // last: destroyed first, while the rest still lives

    LinkInstance(LinkRegistry& registry, const std::string& id, LinkEndpoint::UidGenerator uids)
        : lineage("lineage-" + id) {
        endpoint = std::make_unique<LinkEndpoint>(registry, id, std::move(uids), [this] { publish(); });
        publish();  // the shell's constructor publishes its empty document
    }
    LinkInstance(const LinkInstance&) = delete;
    LinkInstance& operator=(const LinkInstance&) = delete;

    void publish() { endpoint->publishOwn(store, lineage, *own, publication); }

    static void run(const LinkRegistry::Hooks& hooks) {
        for (const auto& hook : hooks) {
            REQUIRE(hook != nullptr);
            (*hook)();
        }
    }

    /// A command: `fn` on the store, then a publish.
    void edit(const std::function<void(DocumentStore&)>& fn) {
        fn(store);
        publish();
    }

    /// A document of its own (no state load): replaced, then a publish.
    void open(const DocumentStore& doc) {
        store.replace(doc.doc(), doc.nextId(), doc.nextSectionId(), doc.nextTrackId(), doc.nextNoteId());
        publish();
    }

    /// A state load with the instance block: the document replaced, a new lineage, then `adopt`.
    void load(const DocumentStore& doc, const InstanceState& link, std::string newLineage) {
        store.replace(doc.doc(), doc.nextId(), doc.nextSectionId(), doc.nextTrackId(), doc.nextNoteId());
        lineage = std::move(newLineage);
        LinkRegistry::Hooks hooks;
        endpoint->adopt(link, hooks);
        run(hooks);
        publish();
    }

    /// A follow by command, with the leader's lineage as this instance's view shows it.
    void follow(const std::string& leaderId, const std::string& laneUid) {
        std::string leaderLineage;
        for (const LinkLeaderView& leader : endpoint->view().leaders) {
            if (leader.id == leaderId) leaderLineage = leader.lineage;
        }
        REQUIRE_FALSE(leaderLineage.empty());
        LinkRegistry::Hooks hooks;
        REQUIRE(endpoint->follow(leaderId, laneUid, leaderLineage, hooks) == LinkFollowResult::Applied);
        run(hooks);
        publish();
    }

    /// The lane uid this instance gave track `trackId`.
    std::string laneUid(int trackId) const {
        const std::string uid = endpoint->state().lanes.at(trackId);
        REQUIRE_FALSE(uid.empty());
        return uid;
    }
};

/// The progression fixture with track 1 "Bass" (bass) and track 2 "Lead" (lead), a few notes each.
DocumentStore linkSongStore() {
    DocumentStore store = progressionStore(nullptr);
    REQUIRE(store.addTrack(TrackRole::Bass, std::string("Bass")) == 1);
    REQUIRE(store.addTrack(TrackRole::Lead, std::string("Lead")) == 2);
    REQUIRE(store.insertNotes(1, {NoteSpec{36, 0, 960, 100}, NoteSpec{45, 3840, 960, 100}}).size() == 2);
    REQUIRE(store.insertNotes(2, {NoteSpec{72, 960, 480, 100}}).size() == 1);
    return store;
}

/// Another project: one chord and track 1 "Keys" (other).
DocumentStore linkOtherStore() {
    DocumentStore store;
    store.appendChord(chordOf(2, "m7"), 8.0, 100);
    REQUIRE(store.addTrack(TrackRole::Other, std::string("Keys")) == 1);
    return store;
}

/// link-state-leader.json: ChordLab B, a duplicate of ChordLab A (it loaded A's state while A lived, so it is a
/// copy under a fresh id), sees A (chords and two tracks; A muted its Lead after the copy was made, and C follows
/// A's Bass) and leads two followers of its own: D plays its chords, E its Lead (track 2). It has followers, so it
/// is a leader in its own right and `copyOf` is null (rule 11).
std::string linkStateLeader() {
    const auto now = std::make_shared<std::int64_t>(0);
    LinkRegistry registry{fixtureClock(now)};
    const LinkEndpoint::UidGenerator uids = fixtureUids();
    auto a = std::make_unique<LinkInstance>(registry, "id-original", uids);
    a->open(linkSongStore());
    auto b = std::make_unique<LinkInstance>(registry, "id-ctor-b", uids);
    b->load(a->store, a->endpoint->state(), "lineage-copy-1");  // Live's duplicate of A's chain
    *now += 60 * 1000;                                           // a person's pace from here on
    a->edit([](DocumentStore& d) { REQUIRE(d.setTrackMute(2, true)); });
    auto c = std::make_unique<LinkInstance>(registry, "id-follower-c", uids);
    auto d = std::make_unique<LinkInstance>(registry, "id-follower-d", uids);
    auto e = std::make_unique<LinkInstance>(registry, "id-follower-e", uids);
    const std::string copyId = b->endpoint->view().id;
    c->follow("id-original", a->laneUid(1));
    d->follow(copyId, std::string(kChordsLaneUid));
    e->follow(copyId, b->laneUid(2));
    return linkStateJson(b->endpoint->view());
}

/// link-state-follower.json: a duplicated group. ChordLab B follows the Bass of ChordLab A; then both were
/// duplicated in one go (C is A's copy, D is B's copy and so follows A's Bass too). D's view: playing, offered C
/// instead.
std::string linkStateFollower() {
    const auto now = std::make_shared<std::int64_t>(0);
    LinkRegistry registry{fixtureClock(now)};
    const LinkEndpoint::UidGenerator uids = fixtureUids();
    auto a = std::make_unique<LinkInstance>(registry, "id-leader", uids);
    a->open(linkSongStore());
    auto b = std::make_unique<LinkInstance>(registry, "id-follower", uids);
    b->follow("id-leader", a->laneUid(1));
    // The host duplicates the group's two chains 3 ms apart, well inside kPasteWindowMs, a minute after B's
    // follow: this is what makes D's switchTo appear (rule 8). Two duplications at a person's pace would not.
    *now += 60 * 1000;
    auto c = std::make_unique<LinkInstance>(registry, "id-ctor-c", uids);
    c->load(a->store, a->endpoint->state(), "lineage-leader-copy-1");
    *now += 3;
    auto d = std::make_unique<LinkInstance>(registry, "id-ctor-d", uids);
    d->load(b->store, b->endpoint->state(), "lineage-follower-copy-1");
    return linkStateJson(d->endpoint->view());
}

/// link-state-waiting.json: ChordLab B followed the Bass of ChordLab A and was saved; B and A are gone, and
/// ChordLab D loads B's saved state (B's id, its follow with the hints saved then) while A is missing. D sees
/// ChordLab C, another leader, and waits.
std::string linkStateWaiting() {
    const auto now = std::make_shared<std::int64_t>(0);
    LinkRegistry registry{fixtureClock(now)};
    const LinkEndpoint::UidGenerator uids = fixtureUids();
    auto a = std::make_unique<LinkInstance>(registry, "id-leader", uids);
    a->open(linkSongStore());
    auto b = std::make_unique<LinkInstance>(registry, "id-follower", uids);
    b->follow("id-leader", a->laneUid(1));
    auto c = std::make_unique<LinkInstance>(registry, "id-other", uids);
    c->open(linkOtherStore());
    const DocumentStore savedDoc = b->store;
    const InstanceState savedLink = b->endpoint->state();
    b.reset();
    a.reset();
    auto d = std::make_unique<LinkInstance>(registry, "id-ctor-d", uids);
    d->load(savedDoc, savedLink, "lineage-follower-2");
    return linkStateJson(d->endpoint->view());
}

/// link-state-copy.json: ChordLab B, a fresh duplicate of ChordLab A that nobody follows, so it is offered its
/// original (`copyOf`); A has chords and two tracks, and C has followed A's Bass since before (a minute later, by
/// command: no paste). B's `routed` is empty.
std::string linkStateCopy() {
    const auto now = std::make_shared<std::int64_t>(0);
    LinkRegistry registry{fixtureClock(now)};
    const LinkEndpoint::UidGenerator uids = fixtureUids();
    auto a = std::make_unique<LinkInstance>(registry, "id-original", uids);
    a->open(linkSongStore());
    auto b = std::make_unique<LinkInstance>(registry, "id-ctor-b", uids);
    b->load(a->store, a->endpoint->state(), "lineage-copy-1");  // Live's duplicate of A's chain
    *now += 60 * 1000;
    auto c = std::make_unique<LinkInstance>(registry, "id-follower-c", uids);
    c->follow("id-original", a->laneUid(1));
    return linkStateJson(b->endpoint->view());
}

/// Every member of `object` by name.
std::set<std::string> keysOf(const json& object) {
    std::set<std::string> keys;
    for (auto it = object.begin(); it != object.end(); ++it) keys.insert(it.key());
    return keys;
}

using Keys = std::set<std::string>;

/// The members linkStateJson documents (json_api.h), at every level of `text`.
void checkLinkStateShape(const std::string& text) {
    const json j = json::parse(text);
    CHECK(keysOf(j) == Keys{"schema", "self", "following", "leaders", "routed"});
    CHECK(j.at("schema") == kJsonSchema);
    CHECK(keysOf(j.at("self")) == Keys{"id", "label", "copyOf"});
    if (!j.at("self").at("copyOf").is_null()) CHECK(keysOf(j.at("self").at("copyOf")) == Keys{"leader", "label"});
    if (!j.at("following").is_null()) {
        CHECK(keysOf(j.at("following")) ==
              Keys{"leader", "leaderLabel", "lane", "laneName", "role", "status", "switchTo"});
        if (!j.at("following").at("switchTo").is_null()) {
            CHECK(keysOf(j.at("following").at("switchTo")) == Keys{"leader", "label"});
        }
    }
    for (const json& leader : j.at("leaders")) {
        CHECK(keysOf(leader) == Keys{"id", "label", "lineage", "lanes"});
        for (const json& lane : leader.at("lanes")) {
            CHECK(keysOf(lane) == Keys{"lane", "name", "role", "chords", "audible", "followers"});
        }
    }
    for (const json& lane : j.at("routed")) CHECK(keysOf(lane) == Keys{"lane", "followers", "trackId"});
}

}  // namespace

TEST_CASE("golden fixtures: the link states match the committed files byte for byte, and are the same every run") {
    const std::string leader = linkStateLeader();
    const std::string follower = linkStateFollower();
    const std::string waiting = linkStateWaiting();
    const std::string copy = linkStateCopy();
    CHECK(linkStateLeader() == leader);
    CHECK(linkStateFollower() == follower);
    CHECK(linkStateWaiting() == waiting);
    CHECK(linkStateCopy() == copy);
    checkGolden("link-state-leader.json", leader);
    checkGolden("link-state-follower.json", follower);
    checkGolden("link-state-waiting.json", waiting);
    checkGolden("link-state-copy.json", copy);
}

TEST_CASE("golden fixtures: the link states carry the documented members and what each one is for") {
    for (const std::string& text : {linkStateLeader(), linkStateFollower(), linkStateWaiting(), linkStateCopy()}) {
        checkLinkStateShape(text);
    }

    const json leader = json::parse(linkStateLeader());
    CHECK(leader.at("self").at("label") == "ChordLab B");
    CHECK(leader.at("self").at("id") != "id-original");
    CHECK(leader.at("self").at("copyOf").is_null());  // a copy with followers of its own (rule 11)
    CHECK(leader.at("following").is_null());
    REQUIRE(leader.at("leaders").size() == 1);  // followers are never listed
    const json& original = leader.at("leaders").at(0);
    CHECK(original.at("id") == "id-original");
    CHECK(original.at("lineage") == "lineage-id-original");
    REQUIRE(original.at("lanes").size() == 3);
    CHECK(original.at("lanes").at(0).at("lane") == kChordsLaneUid);
    CHECK(original.at("lanes").at(1).at("name") == "Bass");
    CHECK(original.at("lanes").at(1).at("followers") == 1);
    CHECK(original.at("lanes").at(2).at("name") == "Lead");
    CHECK(original.at("lanes").at(2).at("audible") == false);
    REQUIRE(leader.at("routed").size() == 2);
    CHECK(leader.at("routed").at(0) == json({{"lane", kChordsLaneUid}, {"followers", 1}, {"trackId", 0}}));
    CHECK(leader.at("routed").at(1).at("trackId") == 2);
    CHECK(leader.at("routed").at(1).at("followers") == 1);

    const json follower = json::parse(linkStateFollower());
    CHECK(follower.at("self").at("copyOf").is_null());  // a follower that is a copy has switchTo instead
    const json& following = follower.at("following");
    CHECK(following.at("leader") == "id-leader");
    CHECK(following.at("leaderLabel") == "ChordLab A");
    CHECK(following.at("laneName") == "Bass");
    CHECK(following.at("role") == "bass");
    CHECK(following.at("status") == "playing");
    CHECK(following.at("switchTo").at("label") == "ChordLab C");
    REQUIRE(follower.at("leaders").size() == 2);
    CHECK(follower.at("leaders").at(0).at("lanes").at(1).at("followers") == 2);  // B and D play A's Bass
    CHECK(following.at("switchTo").at("leader") == follower.at("leaders").at(1).at("id"));
    CHECK(follower.at("routed") == json::array());

    const json waiting = json::parse(linkStateWaiting());
    CHECK(waiting.at("self") == json({{"id", "id-follower"}, {"label", "ChordLab D"}, {"copyOf", nullptr}}));
    CHECK(waiting.at("following").at("status") == "waiting");
    CHECK(waiting.at("following").at("leader") == "id-leader");
    CHECK(waiting.at("following").at("leaderLabel") == "ChordLab A");  // the hints saved with the follow
    CHECK(waiting.at("following").at("laneName") == "Bass");
    CHECK(waiting.at("following").at("role") == "bass");
    CHECK(waiting.at("following").at("switchTo").is_null());
    REQUIRE(waiting.at("leaders").size() == 1);
    CHECK(waiting.at("leaders").at(0).at("label") == "ChordLab C");
    CHECK(waiting.at("routed") == json::array());

    const json copy = json::parse(linkStateCopy());
    CHECK(copy.at("self").at("label") == "ChordLab B");
    CHECK(copy.at("self").at("id") != "id-original");
    CHECK(copy.at("self").at("copyOf") == json({{"leader", "id-original"}, {"label", "ChordLab A"}}));
    CHECK(copy.at("following").is_null());
    CHECK(copy.at("routed") == json::array());
    REQUIRE(copy.at("leaders").size() == 1);  // C follows: never listed
    const json& itsOriginal = copy.at("leaders").at(0);
    CHECK(itsOriginal.at("id") == "id-original");
    REQUIRE(itsOriginal.at("lanes").size() == 3);
    CHECK(itsOriginal.at("lanes").at(0).at("lane") == kChordsLaneUid);
    CHECK(itsOriginal.at("lanes").at(1).at("name") == "Bass");
    CHECK(itsOriginal.at("lanes").at(1).at("followers") == 1);
    CHECK(itsOriginal.at("lanes").at(2).at("name") == "Lead");
    CHECK(itsOriginal.at("lanes").at(2).at("followers") == 0);
}
