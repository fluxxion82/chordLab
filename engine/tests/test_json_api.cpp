#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/document.h"
#include "chordlab/json_api.h"
#include "chordlab/palette.h"
#include "chordlab/pitch.h"
#include "chordlab/scale.h"

#include <nlohmann/json.hpp>

#include <algorithm>
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

/// A document exercising every field: non-default keys and toggles, a gap in the ids, mixed lengths.
/// Ids are 1 and 3 (2 was removed), so the next id is 4.
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
    CHECK(e.at("symbol") == "Eb");  // chromatic in F major, spelled on the flat side; D# in the browsed key
    CHECK(e.at("roman") == "bVII");
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

    REQUIRE(state.at("events").size() == 2);
    CHECK(state.at("events")[0] == json({{"id", 1}, {"root", 0}, {"type", "maj"}, {"lengthBeats", 4.0}, {"velocity", 100}}));
    CHECK(state.at("events")[1] == json({{"id", 3}, {"root", 7}, {"type", "7"}, {"lengthBeats", 1.0}, {"velocity", 127}}));

    const json defaults = json::parse(stateJson(store));
    CHECK(defaults.at("editor") == json({{"width", 900}, {"height", 620}}));
}

TEST_CASE("state: loadStateJson(stateJson(doc)) round-trips the document, the ids, the id counter and the editor size") {
    const DocumentStore original = richStore();
    const std::string text = stateJson(original, 1024, 768);

    DocumentStore loaded;
    loaded.appendChord(kC);  // pre-existing content and history must be discarded
    std::string error;
    int width = 0;
    int height = 0;
    REQUIRE_MESSAGE(loadStateJson(loaded, text, error, &width, &height), error);
    CHECK(error.empty());
    CHECK(loaded.doc() == original.doc());
    CHECK(width == 1024);
    CHECK(height == 768);
    CHECK_FALSE(loaded.undo());  // history cleared
    CHECK_FALSE(loaded.redo());
    CHECK(loaded.appendChord(kF) == 4);  // the counter continues where the saved document stopped

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
