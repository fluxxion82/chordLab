// Deterministic mutation fuzz of the two JSON readers (applyCommandJson, loadStateJson; json_api.h).
//
// A fixed-seed xorshift64 mutates a corpus of valid messages (one per command, plus the state fixtures)
// and feeds every mutant to both readers. After every call the store must still satisfy the document
// invariants, a snapshot of it must still serialise and parse, an accepted call must have cleared `error`
// and a rejected one must have set it and left the store as it was. Nothing here uses <random> or any
// clock, so a failure reproduces from the iteration number printed with it.

#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/document.h"
#include "chordlab/json_api.h"
#include "chordlab/palette.h"
#include "chordlab/pitch.h"
#include "chordlab/scale.h"
#include "chordlab/voicing.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace chordlab;
using nlohmann::json;

namespace {

/// Iterations of the main loop: 3000 mutants through both readers. The expensive part is the snapshot
/// (about 8 ms at -O0), taken after every call that changed the store, which is why the whole case stays
/// near a second in Debug.
constexpr int kIterations = 3000;

struct Rng {
    std::uint64_t state;
    std::uint64_t next() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    }
    std::size_t below(std::size_t n) { return static_cast<std::size_t>(next() % n); }
};

const char* const kNumberLiterals[] = {"1e400",  "-1e400",   "18446744073709551615", "-99999999999", "99999999999",
                                       "2147483648", "-2147483649", "0",     "-1",     "1",
                                       "2",     "3",      "11",   "12",     "255",
                                       "0.0",   "-0.0",   "4.5",  "1e-400", "63.99999",
                                       "64.00001", "1e308", "NaN",  "Infinity", "0x10"};
constexpr std::size_t kNumberLiteralCount = sizeof(kNumberLiterals) / sizeof(kNumberLiterals[0]);

std::string insertToken(Rng& rng) {
    static const std::string kTokens[] = {"{", "}", "[", "]", "\"", ",", ":", std::string(1, '\0'), "-"};
    return kTokens[rng.below(sizeof(kTokens) / sizeof(kTokens[0]))];
}

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

/// Applies one random mutation to `s`.
void mutateOnce(std::string& s, Rng& rng) {
    if (s.empty()) {
        s = insertToken(rng);
        return;
    }
    switch (rng.below(9)) {
        case 0: {  // flip a bit of a byte
            const std::size_t at = rng.below(s.size());
            const unsigned bit = static_cast<unsigned>(rng.below(8));
            s[at] ^= static_cast<char>(1u << bit);
            break;
        }
        case 1: {  // delete a range
            const std::size_t at = rng.below(s.size());
            const std::size_t len = 1 + rng.below(8);
            s.erase(at, len);
            break;
        }
        case 2: {  // duplicate a range
            const std::size_t at = rng.below(s.size());
            const std::size_t len = 1 + rng.below(32);
            const std::string piece = s.substr(at, len);
            s.insert(at, piece);
            break;
        }
        case 3: {  // insert a structural token
            // One RNG draw per statement: argument evaluation order is unspecified, and the seed must give the
            // same mutants on every compiler.
            const std::size_t at = rng.below(s.size() + 1);
            const std::string token = insertToken(rng);
            s.insert(at, token);
            break;
        }
        case 4: {  // insert a number literal
            const std::size_t at = rng.below(s.size() + 1);
            const char* literal = kNumberLiterals[rng.below(kNumberLiteralCount)];
            s.insert(at, literal);
            break;
        }
        case 5:
        case 7:
        case 8: {  // replace a digit run (three of nine picks: most mutants should still be well-formed JSON) with another number
            std::vector<std::size_t> starts;
            for (std::size_t i = 0; i < s.size(); ++i) {
                if (isDigit(s[i]) && (i == 0 || !isDigit(s[i - 1]))) {
                    starts.push_back(i);
                }
            }
            if (starts.empty()) {
                s.push_back(' ');
                break;
            }
            const std::size_t at = starts[rng.below(starts.size())];
            std::size_t end = at;
            while (end < s.size() && (isDigit(s[end]) || s[end] == '.')) {
                ++end;
            }
            std::string replacement = kNumberLiterals[rng.below(kNumberLiteralCount)];
            if (rng.below(2) == 0) {
                const int small = static_cast<int>(rng.below(300)) - 20;
                replacement = std::to_string(small);
            }
            s.replace(at, end - at, replacement);
            break;
        }
        default: {  // truncate
            s.resize(rng.below(s.size()));
            break;
        }
    }
}

/// `s` with control characters, quotes and non-ASCII bytes escaped, for a failure message.
std::string escaped(const std::string& s) {
    std::ostringstream out;
    for (unsigned char c : s) {
        if (c == '\\' || c == '"') {
            out << '\\' << static_cast<char>(c);
        } else if (c < 0x20 || c >= 0x7F) {
            static const char* hex = "0123456789abcdef";
            out << "\\x" << hex[c >> 4] << hex[c & 15];
        } else {
            out << static_cast<char>(c);
        }
    }
    return out.str();
}

Chord chordOf(PitchClass root, const char* typeId) {
    return Chord{root, &chordType(typeId)};
}

/// The store every mutant lands on: four chords (ids 1..4), two sections (ids 1, 2), Am voiced by hand,
/// event 2 selected; `withRedo` leaves one undone step on the redo stack.
DocumentStore prefilledStore(bool withRedo = false) {
    DocumentStore store;
    store.appendChord(chordOf(0, "maj"), 4.0, 100);
    store.appendChord(chordOf(9, "min"), 4.0, 90);
    store.appendChord(chordOf(5, "maj"), 2.0, 80);
    store.appendChord(chordOf(7, "7"), 6.0, 110);
    store.setInversion(2, 1);
    store.addSection(0, "Verse");
    store.addSection(2, "Chorus");
    if (withRedo) {
        // An undone edit leaves the document as it was and one step on the redo stack, so the redo path of
        // the "rejected means unchanged" check has something to walk.
        store.setVelocity(1, 77);
        store.undo();
    }
    store.setSelectedEvent(2);
    return store;
}

/// Empty when `store` satisfies every invariant the documentation promises, else the first one broken.
/// The snapshot (7-8 ms per call at -O0) is a pure function of the document and the selection, so it is
/// only taken when `snapshot` is set: by `feed`, for every call that changed what the snapshot is a function of.
std::string invariantProblem(const DocumentStore& store, bool snapshot = true) {
    const Document& d = store.doc();
    if (!sectionsWellFormed(d)) {
        return "sections not well formed";
    }
    if (d.events.size() > static_cast<std::size_t>(kMaxChords)) {
        return "more than kMaxChords events";
    }
    const int sel = store.selectedEventId();
    if (sel != kNoEventId && store.findIndex(sel) < 0) {
        return "selected event id " + std::to_string(sel) + " names no event";
    }
    if (store.nextId() < 1 || store.nextSectionId() < 1) {
        return "nextId or nextSectionId below 1";
    }
    std::set<int> ids;
    for (const ChordEvent& e : d.events) {
        if (!ids.insert(e.id).second) {
            return "duplicate event id " + std::to_string(e.id);
        }
        if (e.id < 1 || e.id >= store.nextId()) {
            return "event id " + std::to_string(e.id) + " outside 1..nextId-1";
        }
        if (e.chord.type == nullptr || e.chord.root < 0 || e.chord.root > 11) {
            return "bad chord on event " + std::to_string(e.id);
        }
        if (!std::isfinite(e.lengthBeats) || e.lengthBeats < kMinLengthBeats || e.lengthBeats > kMaxLengthBeats) {
            return "length out of bounds on event " + std::to_string(e.id);
        }
        if (e.velocity < kMinVelocity || e.velocity > kMaxVelocity) {
            return "velocity out of bounds on event " + std::to_string(e.id);
        }
        if (e.inversion < 0 || e.inversion >= inversionCount(e.chord)) {
            return "inversion out of bounds on event " + std::to_string(e.id);
        }
        if (e.octave < kMinVoicingOctave || e.octave > kMaxVoicingOctave) {
            return "octave out of bounds on event " + std::to_string(e.id);
        }
    }
    for (const Section& s : d.sections) {
        if (s.id >= store.nextSectionId()) {
            return "section id " + std::to_string(s.id) + " not below nextSectionId";
        }
    }
    if (!snapshot) {
        return {};
    }
    try {
        const std::string snap = snapshotJson(d, buildPalette(d.browsedKey, d.projectKey), store.selectedEventId());
        const json j = json::parse(snap);
        if (!j.is_object() || j.value("schema", 0) != kJsonSchema) {
            return "snapshot is not a schema-1 object";
        }
        const json sId = j.at("selectedEventId");
        if (sel == kNoEventId ? !sId.is_null() : sId != sel) {
            return "snapshot selectedEventId disagrees with the store";
        }
    } catch (const std::exception& ex) {
        return std::string("snapshot threw: ") + ex.what();
    }
    return {};
}

/// What a reader may not change when it rejects a message.
struct Observable {
    Document doc;
    int nextId;
    int nextSectionId;
    int selected;
    explicit Observable(const DocumentStore& s)
        : doc(s.doc()), nextId(s.nextId()), nextSectionId(s.nextSectionId()), selected(s.selectedEventId()) {}
    bool operator==(const Observable& o) const {
        return doc == o.doc && nextId == o.nextId && nextSectionId == o.nextSectionId && selected == o.selected;
    }
};

/// Walks the whole undo history of `a` and `b` (on copies) and then redoes back, requiring both to agree at
/// every step: the same return value and the same observable state. Empty when they do. This is what makes
/// "a rejected call changed nothing" cover the history, not only the current document.
std::string historyDifference(const DocumentStore& a, const DocumentStore& b) {
    DocumentStore x = a;
    DocumentStore y = b;
    const std::size_t limit = DocumentStore::kMaxUndoDepth + 2;
    std::size_t undone = 0;
    for (; undone < limit; ++undone) {
        const bool rx = x.undo();
        const bool ry = y.undo();
        if (rx != ry) {
            return "undo history depth differs at step " + std::to_string(undone);
        }
        if (!(Observable(x) == Observable(y))) {
            return "state differs after undo step " + std::to_string(undone);
        }
        if (!rx) {
            break;
        }
    }
    for (std::size_t redone = 0; redone < limit + undone; ++redone) {
        const bool rx = x.redo();
        const bool ry = y.redo();
        if (rx != ry) {
            return "redo history depth differs at step " + std::to_string(redone);
        }
        if (!(Observable(x) == Observable(y))) {
            return "state differs after redo step " + std::to_string(redone);
        }
        if (!rx) {
            break;
        }
    }
    return {};
}

std::string readFixture(const char* name) {
    std::ifstream in(std::string(CHORDLAB_FIXTURE_DIR) + "/" + name, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

/// One valid message for every command name of json_api.h (three for browseKey), against ids 1..4 and
/// section ids 1..2 of `prefilledStore`.
std::vector<std::string> commandCorpus() {
    const std::vector<json> bodies = {
        {{"cmd", "setKey"}, {"letter", "D"}, {"alter", -1}, {"mode", "Dorian"}},
        {{"cmd", "browseKey"}, {"steps", 1}},
        {{"cmd", "browseKey"}, {"mode", "Aeolian"}},
        {{"cmd", "browseKey"}, {"home", true}},
        {{"cmd", "appendChord"}, {"root", 7}, {"type", "7"}, {"lengthBeats", 2.5}, {"velocity", 90}},
        {{"cmd", "moveChord"}, {"id", 3}, {"index", 0}},
        {{"cmd", "setLength"}, {"id", 2}, {"lengthBeats", 8.0}},
        {{"cmd", "setVelocity"}, {"id", 1}, {"velocity", 64}},
        {{"cmd", "removeChord"}, {"id", 4}},
        {{"cmd", "setProgressionLoop"}, {"on", false}},
        {{"cmd", "setFollowHost"}, {"on", false}},
        {{"cmd", "setInversion"}, {"id", 1}, {"inversion", 2}},
        {{"cmd", "setOctave"}, {"id", 3}, {"octave", -1}},
        {{"cmd", "resetVoicing"}, {"id", 2}},
        {{"cmd", "setAutoVoiceLead"}, {"on", true}},
        {{"cmd", "addSection"}, {"index", 3}, {"name", "Bridge"}},
        {{"cmd", "renameSection"}, {"id", 2}, {"name", "Chorus \xCE\x94"}},
        {{"cmd", "setSectionColour"}, {"id", 1}, {"colourIndex", 5}},
        {{"cmd", "removeSection"}, {"id", 2}},
        {{"cmd", "moveSectionStart"}, {"id", 2}, {"index", 3}},
        {{"cmd", "selectEvent"}, {"id", 3}},
        {{"cmd", "selectEvent"}, {"id", nullptr}},
        {{"cmd", "setRelationMode"}, {"mode", "voiceLeading"}},
        {{"cmd", "undo"}},
        {{"cmd", "redo"}},
    };
    std::vector<std::string> out;
    for (json body : bodies) {
        body["schema"] = kJsonSchema;
        out.push_back(body.dump());
    }
    return out;
}

/// Calls both readers with `text` on their stores and checks every promise; returns the first broken one.
/// `commandAccepted` is set when applyCommandJson took the message.
std::string feed(DocumentStore& cmdStore, DocumentStore& stateStore, const std::string& text,
                 bool* commandAccepted = nullptr) {
    {
        const Observable before(cmdStore);
        const DocumentStore storeBefore = cmdStore;
        std::string error = "stale";
        bool ok = false;
        try {
            ok = applyCommandJson(cmdStore, text, error);
        } catch (...) {
            return "applyCommandJson let an exception escape";
        }
        if (ok && !error.empty()) {
            return "applyCommandJson accepted but left error \"" + error + "\"";
        }
        if (!ok && (error.empty() || error == "stale")) {
            return "applyCommandJson rejected without setting error";
        }
        if (!ok && !(Observable(cmdStore) == before)) {
            return "applyCommandJson rejected but changed the store";
        }
        if (!ok) {
            const std::string h = historyDifference(storeBefore, cmdStore);
            if (!h.empty()) {
                return "applyCommandJson rejected but changed the undo/redo history: " + h;
            }
        }
        if (commandAccepted != nullptr) {
            *commandAccepted = ok;
        }
        const std::string problem = invariantProblem(cmdStore, !(Observable(cmdStore) == before));
        if (!problem.empty()) {
            return "after applyCommandJson: " + problem;
        }
    }
    {
        const Observable before(stateStore);
        const DocumentStore storeBefore = stateStore;
        std::string error = "stale";
        int w = -7;
        int h = -7;
        bool ok = false;
        try {
            ok = loadStateJson(stateStore, text, error, &w, &h);
        } catch (...) {
            return "loadStateJson let an exception escape";
        }
        if (ok && !error.empty()) {
            return "loadStateJson accepted but left error \"" + error + "\"";
        }
        if (ok && (w < 1 || h < 1)) {
            return "loadStateJson accepted a non-positive editor size";
        }
        if (!ok && (error.empty() || error == "stale")) {
            return "loadStateJson rejected without setting error";
        }
        if (!ok && (w != -7 || h != -7 || !(Observable(stateStore) == before))) {
            return "loadStateJson rejected but touched the store or the out-params";
        }
        if (!ok) {
            const std::string h = historyDifference(storeBefore, stateStore);
            if (!h.empty()) {
                return "loadStateJson rejected but changed the undo/redo history: " + h;
            }
        }
        const std::string problem = invariantProblem(stateStore, !(Observable(stateStore) == before));
        if (!problem.empty()) {
            return "after loadStateJson: " + problem;
        }
        if (ok) {
            // What it accepted it can save, and the save loads again.
            DocumentStore again;
            std::string e2;
            if (!loadStateJson(again, stateJson(stateStore, w, h), e2) || !(again.doc() == stateStore.doc())) {
                return "an accepted state does not round-trip: " + e2;
            }
        }
    }
    return {};
}

}  // namespace

TEST_CASE("json robustness: seeded mutation fuzz of both readers keeps every invariant") {
    std::vector<std::string> corpus = commandCorpus();
    const std::size_t commandCount = corpus.size();
    corpus.push_back(readFixture("state-progression.json"));
    corpus.push_back(readFixture("state-legacy-phase1.json"));
    for (std::size_t i = commandCount; i < corpus.size(); ++i) {
        REQUIRE_MESSAGE(!corpus[i].empty(), "state fixture missing");
    }

    // The corpus itself first. Every entry must be accepted by the reader it was written for, on a fresh
    // store (so a silently broken corpus command cannot hide behind the mutants), and then goes through
    // both readers on a shared store like any mutant.
    for (std::size_t i = 0; i < corpus.size(); ++i) {
        DocumentStore store = prefilledStore(i % 2 == 1);
        std::string error;
        const bool ok = i < commandCount ? applyCommandJson(store, corpus[i], error)
                                         : loadStateJson(store, corpus[i], error);
        CHECK_MESSAGE(ok, "corpus entry " << i << " rejected: " << error << " input=" << escaped(corpus[i]));
    }
    {
        DocumentStore cmdStore = prefilledStore();
        DocumentStore stateStore = prefilledStore();
        for (const std::string& m : corpus) {
            const std::string problem = feed(cmdStore, stateStore, m);
            CHECK_MESSAGE(problem.empty(), problem << " input=" << escaped(m));
        }
    }

    const auto t0 = std::chrono::steady_clock::now();
    Rng rng{0x9E3779B97F4A7C15ull};
    DocumentStore cmdStore = prefilledStore(true);
    DocumentStore stateStore = prefilledStore(true);
    int accepted = 0;
    for (int i = 0; i < kIterations; ++i) {
        if (i % 25 == 0) {  // a fresh, known store now and then; in between, accepted mutants accumulate
            const bool withRedo = (i / 25) % 2 == 0;  // alternate: with and without a redo step
            cmdStore = prefilledStore(withRedo);
            stateStore = prefilledStore(withRedo);
        }
        std::string m = corpus[rng.below(corpus.size())];
        std::size_t mutations = 1;
        if (rng.below(2) != 0) {
            mutations = 1 + rng.below(3);
        }
        for (std::size_t k = 0; k < mutations; ++k) {
            mutateOnce(m, rng);
        }
        bool commandAccepted = false;
        const std::string problem = feed(cmdStore, stateStore, m, &commandAccepted);
        if (!problem.empty()) {
            FAIL_CHECK(problem << " [iteration " << i << ", input=" << escaped(m) << "]");
            break;
        }
        accepted += commandAccepted ? 1 : 0;
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    MESSAGE("json robustness fuzz: " << kIterations << " mutants, " << accepted << " accepted as commands, " << ms
                                     << " ms");
    // The corpus is mostly valid, so a fuzz that rejects everything (or accepts everything) is not exercising
    // both sides.
    CHECK(accepted > 50);
    CHECK(accepted < kIterations);
}

// ---- explicit worst cases ----

namespace {

/// Both readers must reject `text` cleanly and leave their stores alone.
void checkRejectedByBoth(const std::string& text, const char* what) {
    CAPTURE(what);
    DocumentStore cmdStore = prefilledStore();
    DocumentStore stateStore = prefilledStore();
    const Observable cmdBefore(cmdStore);
    const Observable stateBefore(stateStore);

    std::string error = "stale";
    bool ok = true;
    CHECK_NOTHROW(ok = applyCommandJson(cmdStore, text, error));
    CHECK_FALSE(ok);
    CHECK_FALSE(error.empty());
    CHECK(error != "stale");
    CHECK(Observable(cmdStore) == cmdBefore);
    CHECK(invariantProblem(cmdStore, false).empty());

    error = "stale";
    ok = true;
    int w = -7;
    int h = -7;
    CHECK_NOTHROW(ok = loadStateJson(stateStore, text, error, &w, &h));
    CHECK_FALSE(ok);
    CHECK_FALSE(error.empty());
    CHECK(error != "stale");
    CHECK(w == -7);
    CHECK(h == -7);
    CHECK(Observable(stateStore) == stateBefore);
    CHECK(invariantProblem(stateStore, false).empty());
}

std::string nestedObjects(int depth) {
    std::string s;
    for (int i = 0; i < depth; ++i) {
        s += "{\"a\":";
    }
    s += "1";
    s.append(static_cast<std::size_t>(depth), '}');
    return s;
}

}  // namespace

TEST_CASE("json robustness: worst-case inputs are rejected cleanly") {
    SUBCASE("1 MiB of open brackets") {
        checkRejectedByBoth(std::string(kMaxJsonBytes, '['), "1 MiB of [");
    }
    SUBCASE("one byte over the size limit") {
        checkRejectedByBoth(std::string(kMaxJsonBytes + 1, ' '), "kMaxJsonBytes + 1 spaces");
    }
    SUBCASE("a valid-looking command padded past the size limit") {
        std::string text = "{\"schema\":1,\"cmd\":\"undo\",\"pad\":\"";
        text.append(kMaxJsonBytes, 'x');
        text += "\"}";
        checkRejectedByBoth(text, "padded command");
    }
    SUBCASE("objects nested past the depth limit") {
        checkRejectedByBoth(nestedObjects(kMaxJsonDepth + 1), "depth limit + 1");
        checkRejectedByBoth(nestedObjects(1000), "1000 deep");
        checkRejectedByBoth(nestedObjects(100000), "100000 deep");
    }
    SUBCASE("arrays nested past the depth limit inside an otherwise valid message") {
        std::string text = "{\"schema\":1,\"cmd\":\"undo\",\"x\":";
        text.append(static_cast<std::size_t>(kMaxJsonDepth), '[');
        text.append(static_cast<std::size_t>(kMaxJsonDepth), ']');
        text += "}";
        checkRejectedByBoth(text, "message with a 65-level array");
    }
    SUBCASE("a NUL in the middle of a valid message") {
        std::string cmd = "{\"schema\":1,\"cmd\":\"undo\"}";
        cmd.insert(cmd.size() / 2, 1, '\0');
        checkRejectedByBoth(cmd, "NUL inside a command");
        std::string state = readFixture("state-progression.json");
        REQUIRE_FALSE(state.empty());
        state.insert(state.size() / 2, 1, '\0');
        checkRejectedByBoth(state, "NUL inside a state");
        checkRejectedByBoth(std::string(1, '\0'), "a lone NUL");
        checkRejectedByBoth(std::string("{\"schema\":1,\"cmd\":\"undo\"}") + std::string(1, '\0') + "garbage",
                            "NUL after a complete message");
    }
    SUBCASE("empty and whitespace-only input") {
        checkRejectedByBoth("", "empty string");
        checkRejectedByBoth(" \n\t ", "whitespace");
    }
    SUBCASE("a non-object top level") {
        for (const char* text : {"[]", "[1,2,3]", "1", "-1", "1e400", "\"undo\"", "null", "true", "false", "{", "}",
                                 "[{\"schema\":1,\"cmd\":\"undo\"}]", "\"{\\\"schema\\\":1}\""}) {
            checkRejectedByBoth(text, text);
        }
    }
    SUBCASE("trailing garbage and truncation") {
        checkRejectedByBoth("{\"schema\":1,\"cmd\":\"undo\"} x", "trailing garbage");
        checkRejectedByBoth("{\"schema\":1,\"cmd\":\"undo\"", "truncated");
    }
}

TEST_CASE("json robustness: the depth limit is exactly kMaxJsonDepth levels") {
    // `undo` ignores unknown fields, so an extra member is a harmless way to add nesting: the top-level
    // object is level 1 and the arrays inside it levels 2.., so kMaxJsonDepth - 1 arrays is the deepest
    // accepted and one more is refused.
    auto withArrays = [](int arrays) {
        std::string text = "{\"schema\":1,\"cmd\":\"undo\",\"x\":";
        text.append(static_cast<std::size_t>(arrays), '[');
        text.append(static_cast<std::size_t>(arrays), ']');
        return text + "}";
    };
    DocumentStore store = prefilledStore();
    std::string error;
    CHECK_MESSAGE(applyCommandJson(store, withArrays(kMaxJsonDepth - 1), error), error);
    CHECK_FALSE(applyCommandJson(store, withArrays(kMaxJsonDepth), error));
    CHECK_FALSE(error.empty());
}
