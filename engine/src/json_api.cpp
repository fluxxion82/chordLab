#include "chordlab/json_api.h"

#include "chordlab/chord.h"
#include "chordlab/compiler.h"
#include "chordlab/document.h"
#include "chordlab/function.h"
#include "chordlab/harmony.h"
#include "chordlab/palette.h"
#include "chordlab/pitch.h"
#include "chordlab/relations.h"
#include "chordlab/scale.h"
#include "chordlab/suggest.h"
#include "chordlab/symbol.h"
#include "chordlab/tension.h"
#include "chordlab/voicing.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace chordlab {

namespace {

using nlohmann::json;

// --- Names -----------------------------------------------------------------------------

/// `Mode` enumerator names, in enum order: the wire form of a mode.
constexpr const char* kModeNames[kNumModes] = {"Ionian",  "Dorian",  "Phrygian",      "Lydian",      "Mixolydian",
                                               "Aeolian", "Locrian", "HarmonicMinor", "MelodicMinor"};

const char* modeName(Mode mode) {
    return kModeNames[static_cast<int>(mode)];
}

/// Sets `out` to the mode named `name`; false if no mode has that name.
bool parseMode(std::string_view name, Mode& out) {
    for (int i = 0; i < kNumModes; ++i) {
        if (name == kModeNames[i]) {
            out = static_cast<Mode>(i);
            return true;
        }
    }
    return false;
}

const char* familyName(Family family) {
    switch (family) {
        case Family::Major:
            return "Major";
        case Family::Minor:
            return "Minor";
        case Family::Other:
            return "Other";
    }
    return "Other";
}

/// The dictionary entry with `id`, or nullptr. Unlike `chordType`, does not throw: unknown ids are
/// expected input here.
const ChordType* findChordType(std::string_view id) {
    for (const ChordType& t : allChordTypes()) {
        if (id == t.id) {
            return &t;
        }
    }
    return nullptr;
}

/// ASCII spelling of a tonic: "C", "Db", "F#".
std::string tonicText(const Tonic& tonic) {
    return Spelling{tonic.letter, tonic.alter}.text();
}

// --- Writing ---------------------------------------------------------------------------

json keyToJson(const Key& key) {
    return json{{"tonic", {{"letter", std::string(1, key.tonic.letter)}, {"alter", key.tonic.alter}}},
                {"mode", modeName(key.mode)}};
}

/// The `modes` list of the snapshot: every mode name in enum order, so the UI never hardcodes it.
json modesJson() {
    json modes = json::array();
    for (const char* name : kModeNames) {
        modes.push_back(name);
    }
    return modes;
}

/// The `tonics` table of the snapshot: per mode (keyed by name), the canonical tonic of each pitch
/// class 0..11 as `{"letter","alter","text"}`, `text` being its ASCII spelling.
json tonicsJson() {
    json tonics = json::object();
    for (int m = 0; m < kNumModes; ++m) {
        const Mode mode = static_cast<Mode>(m);
        json row = json::array();
        for (PitchClass pc = 0; pc < 12; ++pc) {
            const Tonic tonic = canonicalTonic(pc, mode);
            row.push_back(
                {{"letter", std::string(1, tonic.letter)}, {"alter", tonic.alter}, {"text", tonicText(tonic)}});
        }
        tonics[kModeNames[m]] = std::move(row);
    }
    return tonics;
}

/// The pitch classes of `chord` outside the scale of `key`, ascending and without repeats: empty exactly
/// when `fullyInKey(chord, key)`.
json outOfKeyJson(const Chord& chord, const Key& key) {
    std::vector<PitchClass> out;
    for (const PitchClass pc : pitchClasses(chord)) {
        if (!inScale(pc, key.tonicPc(), key.mode)) {
            out.push_back(pc);
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return json(out);
}

/// One palette cell as the snapshot writes it (`palette.cells` and `homeTriads` alike): the cell's own
/// position, chord, symbol, numeral and `filled`, plus what is read against `projectKey`.
json cellJson(const PaletteCell& c, const Key& projectKey) {
    return {{"degree", c.degree},
            {"tier", c.tier},
            {"root", c.chord.root},
            {"type", c.chord.type->id},
            {"symbol", c.symbol},
            {"roman", c.roman},
            {"family", familyName(c.chord.type->family)},
            {"function", functionShortName(functionOf(c.chord, projectKey))},
            {"tension", tension(c.chord, projectKey)},
            {"filled", c.filled},
            {"voicing", closeVoicing(c.chord)},
            {"outOfKey", outOfKeyJson(c.chord, projectKey)}};
}

/// The snapshot's `homeTriads`: nothing while the project key is the one browsed (the palette already
/// shows it); otherwise the project key's seven diatonic triads in degree order, each the cell the
/// palette's triad row would hold for it when browsing the project key, plus an `id` ("home-<degree>")
/// that no palette cell has.
json homeTriadsJson(const Document& doc) {
    json out = json::array();
    if (doc.browsedKey == doc.projectKey) {
        return out;
    }
    for (int degree = 0; degree < 7; ++degree) {
        const Chord triad = diatonicTriad(doc.projectKey, degree);
        const PaletteCell cell{degree,
                               kPaletteTriadTier,
                               triad,
                               fullyInKey(triad, doc.projectKey),
                               chordSymbol(triad, doc.projectKey),
                               romanNumeral(triad, doc.projectKey)};
        json entry = cellJson(cell, doc.projectKey);
        entry["id"] = "home-" + std::to_string(degree);
        out.push_back(std::move(entry));
    }
    return out;
}

/// Mode names as prose, for the sentences the UI shows: the `Mode` spelling except for the two altered
/// minor scales. The same table as the suggestions' reason texts (suggest.cpp), so that "borrowed from"
/// reads identically in the why popover and the suggestion popover.
constexpr const char* kModeProse[kNumModes] = {"Ionian",  "Dorian",  "Phrygian",       "Lydian",       "Mixolydian",
                                               "Aeolian", "Locrian", "harmonic minor", "melodic minor"};

std::string keyProse(const Key& key) {
    return tonicText(key.tonic) + " " + kModeProse[static_cast<int>(key.mode)];
}

/// The `why` object of event `to` against the event `from` before it: `relate` as data plus the sentence
/// whose rules json_api.h spells out.
json whyJson(const Chord& from, const Chord& to, const Key& projectKey) {
    const Relation relation = relate(from, to, projectKey);

    json commonNames = json::array();
    std::string names;
    for (const PitchClass pc : relation.commonPcs) {
        const std::string name = spell(pc, projectKey).text();
        commonNames.push_back(name);
        names += (names.empty() ? "" : ", ") + name;
    }

    std::string text = chordSymbol(from, projectKey) + " → " + chordSymbol(to, projectKey) + ": " +
                       rootMotionName(relation.motion);
    if (functionOf(from, projectKey) == Function::Dominant && functionOf(to, projectKey) == Function::Tonic) {
        text += ", resolves (D→T)";
    }
    if (relation.commonTones >= 1) {
        text += ", " + std::to_string(relation.commonTones) + (relation.commonTones == 1 ? " common tone (" : " common tones (") +
                names + ")";
    }
    // The cost sums over one voice per pitch class of the larger chord (the smaller is padded up to it).
    const int voices = static_cast<int>(std::max(pitchClasses(from).size(), pitchClasses(to).size()));
    if (relation.voiceLeadingCost <= voices) {
        text += ", smooth voice leading (cost " + std::to_string(relation.voiceLeadingCost) + ")";
    }
    if (relation.borrowedFrom) {
        text += ", borrowed from " + keyProse(*relation.borrowedFrom);
    }

    return json{{"commonTones", relation.commonTones},
                {"commonPcs", relation.commonPcs},
                {"commonNames", std::move(commonNames)},
                {"rootMotion", rootMotionName(relation.motion)},
                {"voiceLeadingCost", relation.voiceLeadingCost},
                {"borrowedFrom", relation.borrowedFrom ? keyToJson(*relation.borrowedFrom) : json()},
                {"borrowedFromName", relation.borrowedFrom ? json(keyName(*relation.borrowedFrom)) : json()},
                {"text", std::move(text)}};
}

json sectionsJson(const Document& doc) {
    json sections = json::array();
    for (const Section& s : doc.sections) {
        sections.push_back(
            {{"id", s.id}, {"name", s.name}, {"colourIndex", s.colourIndex}, {"firstEventIndex", s.firstEventIndex}});
    }
    return sections;
}

/// `suggestNext` after `previous` (null to open), as the snapshot lists it.
json suggestionsJson(const Chord* previous, const Document& doc) {
    json suggestions = json::array();
    for (const Suggestion& s : suggestNext(previous, doc.projectKey, doc.browsedKey)) {
        json reasons = json::array();
        for (const Reason r : s.reasons) {
            reasons.push_back(reasonName(r));
        }
        suggestions.push_back({{"root", s.chord.root},
                               {"type", s.chord.type->id},
                               {"symbol", s.label},
                               {"roman", s.roman},
                               {"function", functionShortName(functionOf(s.chord, doc.projectKey))},
                               {"score", s.score},
                               {"reasons", std::move(reasons)},
                               {"reasonTexts", s.reasonTexts}});
    }
    return suggestions;
}

// --- Reading ---------------------------------------------------------------------------

/// Sets `out` to the value of `v` when it is a JSON integer that fits an int.
bool toInt(const json& v, int& out) {
    if (v.is_number_unsigned()) {
        const auto u = v.get<std::uint64_t>();
        if (u > static_cast<std::uint64_t>(INT_MAX)) {
            return false;
        }
        out = static_cast<int>(u);
        return true;
    }
    if (v.is_number_integer()) {
        const auto i = v.get<std::int64_t>();
        if (i < INT_MIN || i > INT_MAX) {
            return false;
        }
        out = static_cast<int>(i);
        return true;
    }
    return false;
}

/// Typed access to the fields of one JSON object. Every getter returns false after writing a message
/// naming the field into `error` ("missing \"root\"", "events[2].root must be an integer in 0..11").
class Fields {
public:
    /// `where` names the object in messages ("events[2]", "editor"); empty for the top-level message.
    Fields(const json& object, std::string where, std::string& error)
        : object_(object), where_(std::move(where)), error_(error) {}

    const json* find(const char* name) const {
        const auto it = object_.find(name);
        return it == object_.end() ? nullptr : &*it;
    }

    bool has(const char* name) const {
        return find(name) != nullptr;
    }

    bool getInt(const char* name, int& out) {
        const json* v = require(name);
        if (v == nullptr) {
            return false;
        }
        return toInt(*v, out) || fail(name, "must be an integer");
    }

    /// An integer in `lo`..`hi` inclusive.
    bool getIntIn(const char* name, int lo, int hi, int& out) {
        const json* v = require(name);
        if (v == nullptr) {
            return false;
        }
        int value = 0;
        if (toInt(*v, value) && value >= lo && value <= hi) {
            out = value;
            return true;
        }
        const std::string range = hi == INT_MAX ? ">= " + std::to_string(lo)
                                                : "in " + std::to_string(lo) + ".." + std::to_string(hi);
        return fail(name, "must be an integer " + range);
    }

    /// A finite number (integer or float).
    bool getNumber(const char* name, double& out) {
        const json* v = require(name);
        if (v == nullptr) {
            return false;
        }
        if (v->is_number()) {
            const double value = v->get<double>();
            if (std::isfinite(value)) {
                out = value;
                return true;
            }
        }
        return fail(name, "must be a finite number");
    }

    bool getBool(const char* name, bool& out) {
        const json* v = require(name);
        if (v == nullptr) {
            return false;
        }
        if (!v->is_boolean()) {
            return fail(name, "must be true or false");
        }
        out = v->get<bool>();
        return true;
    }

    bool getString(const char* name, std::string& out) {
        const json* v = require(name);
        if (v == nullptr) {
            return false;
        }
        if (!v->is_string()) {
            return fail(name, "must be a string");
        }
        out = v->get<std::string>();
        return true;
    }

    /// The field as an array, or nullptr with an error.
    const json* getArray(const char* name) {
        const json* v = require(name);
        if (v == nullptr) {
            return nullptr;
        }
        if (!v->is_array()) {
            fail(name, "must be an array");
            return nullptr;
        }
        return v;
    }

    /// The field as an object, or nullptr with an error.
    const json* getObject(const char* name) {
        const json* v = require(name);
        if (v == nullptr) {
            return nullptr;
        }
        if (!v->is_object()) {
            fail(name, "must be an object");
            return nullptr;
        }
        return v;
    }

    /// The name of `name` inside this object, for messages and for nesting.
    std::string label(const char* name) const {
        return where_.empty() ? "\"" + std::string(name) + "\"" : where_ + "." + name;
    }

    /// Always false, after recording `what` about the field.
    bool fail(const char* name, const std::string& what) {
        error_ = label(name) + " " + what;
        return false;
    }

private:
    const json* require(const char* name) {
        const json* v = find(name);
        if (v == nullptr) {
            error_ = "missing " + label(name);
        }
        return v;
    }

    const json& object_;
    std::string where_;
    std::string& error_;
};

/// Guards that run on the raw text before the parser sees it:
/// - longer than `kMaxJsonBytes`: refused outright;
/// - a raw NUL byte anywhere: nlohmann's lexer reads it as end of input, so "valid command" + NUL +
///   anything would apply the command and silently drop the rest;
/// - nested deeper than `kMaxJsonDepth`, counting `{` and `[` outside string literals (a backslash
///   escapes the next character, so an escaped quote does not end a literal).
/// Unbalanced brackets and unterminated strings are left to the parser, which rejects them.
bool prescan(std::string_view text, std::string& error) {
    if (text.size() > kMaxJsonBytes) {
        error = "message longer than " + std::to_string(kMaxJsonBytes) + " bytes";
        return false;
    }
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (const char c : text) {
        if (c == '\0') {
            error = "message contains a NUL byte";
            return false;
        }
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                inString = false;
            }
        } else if (c == '"') {
            inString = true;
        } else if (c == '{' || c == '[') {
            if (++depth > kMaxJsonDepth) {
                error = "message nested deeper than " + std::to_string(kMaxJsonDepth) + " levels";
                return false;
            }
        } else if ((c == '}' || c == ']') && depth > 0) {
            --depth;
        }
    }
    return true;
}

/// Parses `text` into `out` and checks that it is an object carrying the supported schema.
bool parseMessage(std::string_view text, json& out, std::string& error) {
    if (!prescan(text, error)) {
        return false;
    }
    out = json::parse(text, nullptr, /*allow_exceptions=*/false);
    if (out.is_discarded()) {
        error = "malformed JSON";
        return false;
    }
    if (!out.is_object()) {
        error = "message must be a JSON object";
        return false;
    }
    Fields fields(out, "", error);
    int schema = 0;
    if (!fields.getInt("schema", schema)) {
        return false;
    }
    if (schema != kJsonSchema) {
        error = "unsupported schema " + std::to_string(schema) + " (expected " + std::to_string(kJsonSchema) + ")";
        return false;
    }
    return true;
}

/// Reads the flat tonic fields `letter` (a one-character string 'A'..'G') and `alter` (-1..1).
bool parseTonic(Fields& fields, Tonic& out) {
    std::string letter;
    int alter = 0;
    if (!fields.getString("letter", letter)) {
        return false;
    }
    if (letter.size() != 1 || letter[0] < 'A' || letter[0] > 'G') {
        return fields.fail("letter", "must be one of \"A\"..\"G\"");
    }
    if (!fields.getIntIn("alter", -1, 1, alter)) {
        return false;
    }
    out = Tonic{letter[0], alter};
    return true;
}

/// Reads the `mode` field, a `Mode` enumerator name.
bool parseModeField(Fields& fields, Mode& out) {
    std::string name;
    if (!fields.getString("mode", name)) {
        return false;
    }
    if (!parseMode(name, out)) {
        return fields.fail("mode", "\"" + name + "\" is not a mode");
    }
    return true;
}

/// Reads the key object at `fields[name]`: `{"tonic":{"letter","alter"},"mode"}`.
bool parseKeyField(Fields& fields, const char* name, Key& out, std::string& error) {
    const json* keyObject = fields.getObject(name);
    if (keyObject == nullptr) {
        return false;
    }
    Fields keyFields(*keyObject, fields.label(name), error);
    const json* tonicObject = keyFields.getObject("tonic");
    if (tonicObject == nullptr) {
        return false;
    }
    Fields tonicFields(*tonicObject, keyFields.label("tonic"), error);
    return parseTonic(tonicFields, out.tonic) && parseModeField(keyFields, out.mode);
}

// --- Commands --------------------------------------------------------------------------

/// A validated UI command: plain data, trivially destructible, so that nothing can throw while one goes
/// out of scope (see the Readers section for why that matters). Only the fields its `kind` uses are
/// meaningful; the chord type is resolved to its dictionary entry during validation. The one string a
/// command can carry, a section name, travels beside it in a `std::string` owned by the reader's frame
/// (destroying a string frees but never allocates, like the `Document` of `loadStateJson`).
struct Command {
    enum class Kind {
        SetKey,
        BrowseSteps,
        BrowseMode,
        BrowseHome,
        AppendChord,
        MoveChord,
        SetLength,
        SetVelocity,
        RemoveChord,
        SetProgressionLoop,
        SetFollowHost,
        SetInversion,
        SetOctave,
        ResetVoicing,
        SetAutoVoiceLead,
        AddSection,
        RenameSection,
        SetSectionColour,
        RemoveSection,
        MoveSectionStart,
        SelectEvent,
        Undo,
        Redo
    };
    Kind kind = Kind::Undo;
    Key key{};                 // SetKey
    int steps = 0;             // BrowseSteps
    Mode mode = Mode::Ionian;  // BrowseMode
    Chord chord{0, nullptr};   // AppendChord
    double lengthBeats = 4.0;  // AppendChord (optional, store default), SetLength
    int velocity = 100;        // AppendChord (optional, store default), SetVelocity
    int id = 0;                // MoveChord, SetLength, SetVelocity, RemoveChord, the voicing, section and selection commands
    int index = 0;             // MoveChord, AddSection, MoveSectionStart
    bool on = false;           // SetProgressionLoop, SetFollowHost, SetAutoVoiceLead
    int inversion = 0;         // SetInversion
    int octave = 0;            // SetOctave
    int colourIndex = 0;       // SetSectionColour
};
static_assert(std::is_trivially_destructible_v<Command>, "Command must be plain data: destroying one must not allocate");

// Phase A parsers: each reads the fields of one command kind into `out`, reporting through `Fields`.

bool parseSetKey(Fields& fields, Command& out) {
    out.kind = Command::Kind::SetKey;
    return parseTonic(fields, out.key.tonic) && parseModeField(fields, out.key.mode);
}

bool parseBrowseKey(Fields& fields, Command& out) {
    const int selectors = (fields.has("steps") ? 1 : 0) + (fields.has("mode") ? 1 : 0) + (fields.has("home") ? 1 : 0);
    if (selectors != 1) {
        return fields.fail("browseKey", "needs exactly one of \"steps\", \"mode\" or \"home\"");
    }
    if (fields.has("steps")) {
        out.kind = Command::Kind::BrowseSteps;
        return fields.getInt("steps", out.steps);
    }
    if (fields.has("mode")) {
        out.kind = Command::Kind::BrowseMode;
        return parseModeField(fields, out.mode);
    }
    bool home = false;
    if (!fields.getBool("home", home)) {
        return false;
    }
    if (!home) {
        return fields.fail("home", "must be true");
    }
    out.kind = Command::Kind::BrowseHome;
    return true;
}

bool parseAppendChord(Fields& fields, Command& out) {
    out.kind = Command::Kind::AppendChord;
    int root = 0;
    std::string typeId;
    if (!fields.getIntIn("root", 0, 11, root) || !fields.getString("type", typeId)) {
        return false;
    }
    const ChordType* type = findChordType(typeId);
    if (type == nullptr) {
        return fields.fail("type", "\"" + typeId + "\" is not a chord type");
    }
    if (fields.has("lengthBeats") && !fields.getNumber("lengthBeats", out.lengthBeats)) {
        return false;
    }
    if (fields.has("velocity") && !fields.getInt("velocity", out.velocity)) {
        return false;
    }
    out.chord = Chord{root, type};
    return true;
}

bool parseMoveChord(Fields& fields, Command& out) {
    out.kind = Command::Kind::MoveChord;
    return fields.getInt("id", out.id) && fields.getInt("index", out.index);
}

bool parseSetLength(Fields& fields, Command& out) {
    out.kind = Command::Kind::SetLength;
    return fields.getInt("id", out.id) && fields.getNumber("lengthBeats", out.lengthBeats);
}

bool parseSetVelocity(Fields& fields, Command& out) {
    out.kind = Command::Kind::SetVelocity;
    return fields.getInt("id", out.id) && fields.getInt("velocity", out.velocity);
}

bool parseRemoveChord(Fields& fields, Command& out) {
    out.kind = Command::Kind::RemoveChord;
    return fields.getInt("id", out.id);
}

/// `setProgressionLoop` / `setFollowHost` / `setAutoVoiceLead`.
bool parseToggle(Fields& fields, Command::Kind kind, Command& out) {
    out.kind = kind;
    return fields.getBool("on", out.on);
}

/// `resetVoicing` / `removeSection`: an id and nothing else.
bool parseIdOnly(Fields& fields, Command::Kind kind, Command& out) {
    out.kind = kind;
    return fields.getInt("id", out.id);
}

bool parseSetInversion(Fields& fields, Command& out) {
    out.kind = Command::Kind::SetInversion;
    return fields.getInt("id", out.id) && fields.getInt("inversion", out.inversion);
}

bool parseSetOctave(Fields& fields, Command& out) {
    out.kind = Command::Kind::SetOctave;
    return fields.getInt("id", out.id) && fields.getInt("octave", out.octave);
}

/// Reads a section `name` field: a string of at most `kMaxSectionNameBytes` UTF-8 bytes.
bool getSectionName(Fields& fields, std::string& name) {
    if (!fields.getString("name", name)) {
        return false;
    }
    if (name.size() > static_cast<std::size_t>(kMaxSectionNameBytes)) {
        return fields.fail("name", "must be at most " + std::to_string(kMaxSectionNameBytes) + " bytes");
    }
    return true;
}

/// `addSection {index, name?}`: the name, when given, goes to `name`; absent, it stays empty (the store's
/// default name).
bool parseAddSection(Fields& fields, Command& out, std::string& name) {
    out.kind = Command::Kind::AddSection;
    if (!fields.getInt("index", out.index)) {
        return false;
    }
    return !fields.has("name") || getSectionName(fields, name);
}

bool parseRenameSection(Fields& fields, Command& out, std::string& name) {
    out.kind = Command::Kind::RenameSection;
    return fields.getInt("id", out.id) && getSectionName(fields, name);
}

bool parseSetSectionColour(Fields& fields, Command& out) {
    out.kind = Command::Kind::SetSectionColour;
    return fields.getInt("id", out.id) && fields.getInt("colourIndex", out.colourIndex);
}

bool parseMoveSectionStart(Fields& fields, Command& out) {
    out.kind = Command::Kind::MoveSectionStart;
    return fields.getInt("id", out.id) && fields.getInt("index", out.index);
}

/// `selectEvent {id}`: an integer, or null for no selection.
bool parseSelectEvent(Fields& fields, Command& out) {
    out.kind = Command::Kind::SelectEvent;
    const json* id = fields.find("id");
    if (id == nullptr) {
        return fields.fail("id", "is missing");
    }
    if (id->is_null()) {
        out.id = kNoEventId;
        return true;
    }
    return toInt(*id, out.id) || fields.fail("id", "must be an integer or null");
}

// --- Readers ---------------------------------------------------------------------------
//
// Each reader runs in two phases, and the JSON tree must be gone before the store is touched: nlohmann's
// destructor is noexcept but allocates (json_value::destroy flattens the children onto a heap-allocated
// std::vector), so a json object still alive after a mutation could turn an allocation failure during
// its cleanup into std::terminate with the mutation already applied. Phase A parses and validates into
// plain data and returns with the tree destroyed; Phase B takes nothing but the store and that data.

/// Phase A of `applyCommandJson`: parses and validates `text` into `out`, and a section name into `name`.
bool parseCommand(std::string_view text, Command& out, std::string& name, std::string& error) {
    json message;
    if (!parseMessage(text, message, error)) {
        return false;
    }
    Fields fields(message, "", error);
    std::string cmd;
    if (!fields.getString("cmd", cmd)) {
        return false;
    }
    if (cmd == "setKey") {
        return parseSetKey(fields, out);
    }
    if (cmd == "browseKey") {
        return parseBrowseKey(fields, out);
    }
    if (cmd == "appendChord") {
        return parseAppendChord(fields, out);
    }
    if (cmd == "moveChord") {
        return parseMoveChord(fields, out);
    }
    if (cmd == "setLength") {
        return parseSetLength(fields, out);
    }
    if (cmd == "setVelocity") {
        return parseSetVelocity(fields, out);
    }
    if (cmd == "removeChord") {
        return parseRemoveChord(fields, out);
    }
    if (cmd == "setProgressionLoop") {
        return parseToggle(fields, Command::Kind::SetProgressionLoop, out);
    }
    if (cmd == "setFollowHost") {
        return parseToggle(fields, Command::Kind::SetFollowHost, out);
    }
    if (cmd == "setInversion") {
        return parseSetInversion(fields, out);
    }
    if (cmd == "setOctave") {
        return parseSetOctave(fields, out);
    }
    if (cmd == "resetVoicing") {
        return parseIdOnly(fields, Command::Kind::ResetVoicing, out);
    }
    if (cmd == "setAutoVoiceLead") {
        return parseToggle(fields, Command::Kind::SetAutoVoiceLead, out);
    }
    if (cmd == "addSection") {
        return parseAddSection(fields, out, name);
    }
    if (cmd == "renameSection") {
        return parseRenameSection(fields, out, name);
    }
    if (cmd == "setSectionColour") {
        return parseSetSectionColour(fields, out);
    }
    if (cmd == "removeSection") {
        return parseIdOnly(fields, Command::Kind::RemoveSection, out);
    }
    if (cmd == "moveSectionStart") {
        return parseMoveSectionStart(fields, out);
    }
    if (cmd == "selectEvent") {
        return parseSelectEvent(fields, out);
    }
    if (cmd == "undo") {
        out.kind = Command::Kind::Undo;
        return true;
    }
    if (cmd == "redo") {
        out.kind = Command::Kind::Redo;
        return true;
    }
    error = "unknown cmd \"" + cmd + "\"";
    return false;
}

/// Why Phase B refused a validated command; `None` is success.
enum class Refusal { None, DocumentFull, IdsExhausted, SectionIdsExhausted };

/// True when `index` is a chord position of `doc` at which no section starts: the one case in which the
/// store's `addSection` can still refuse, for want of ids.
bool canStartSection(const Document& doc, int index) {
    if (index < 0 || index >= static_cast<int>(doc.events.size())) {
        return false;
    }
    for (const Section& s : doc.sections) {
        if (s.firstEventIndex == index) {
            return false;
        }
    }
    return true;
}

/// Phase B of `applyCommandJson`: applies a validated command through one store call, which gives the
/// strong guarantee. Takes nothing but the store, plain data and the section name, so no JSON object can
/// be alive here. Refuses only an append, when the document already holds `kMaxChords` chords or the
/// store's id counter is exhausted, and a section add when the section id counter is; either way nothing
/// changes.
Refusal applyCommand(DocumentStore& store, const Command& command, std::string& name) {
    const Document& doc = store.doc();
    switch (command.kind) {
        case Command::Kind::SetKey:
            store.setProjectKey(command.key);
            return Refusal::None;
        case Command::Kind::BrowseSteps:
            store.setBrowsedKey(fifthsNeighbour(doc.browsedKey, command.steps));
            return Refusal::None;
        case Command::Kind::BrowseMode:
            store.setBrowsedKey(Key{canonicalTonic(doc.browsedKey.tonicPc(), command.mode), command.mode});
            return Refusal::None;
        case Command::Kind::BrowseHome:
            store.setBrowsedKey(doc.projectKey);
            return Refusal::None;
        case Command::Kind::AppendChord:
            if (doc.events.size() >= static_cast<std::size_t>(kMaxChords)) {
                return Refusal::DocumentFull;
            }
            return store.appendChord(command.chord, command.lengthBeats, command.velocity) != kNoEventId
                       ? Refusal::None
                       : Refusal::IdsExhausted;
        case Command::Kind::MoveChord:
            store.moveChord(command.id, command.index);
            return Refusal::None;
        case Command::Kind::SetLength:
            store.setLength(command.id, command.lengthBeats);
            return Refusal::None;
        case Command::Kind::SetVelocity:
            store.setVelocity(command.id, command.velocity);
            return Refusal::None;
        case Command::Kind::RemoveChord:
            store.removeChord(command.id);
            return Refusal::None;
        case Command::Kind::SetProgressionLoop:
            store.setProgressionLoop(command.on);
            return Refusal::None;
        case Command::Kind::SetFollowHost:
            store.setFollowHost(command.on);
            return Refusal::None;
        case Command::Kind::SetInversion:
            store.setInversion(command.id, command.inversion);
            return Refusal::None;
        case Command::Kind::SetOctave:
            store.setOctave(command.id, command.octave);
            return Refusal::None;
        case Command::Kind::ResetVoicing:
            store.resetVoicing(command.id);
            return Refusal::None;
        case Command::Kind::SetAutoVoiceLead:
            store.setAutoVoiceLead(command.on);
            return Refusal::None;
        case Command::Kind::AddSection:
            if (!canStartSection(doc, command.index)) {
                return Refusal::None;  // a valid no-op, like an unknown id
            }
            return store.addSection(command.index, std::move(name)) != kNoSectionId ? Refusal::None
                                                                                     : Refusal::SectionIdsExhausted;
        case Command::Kind::RenameSection:
            store.renameSection(command.id, std::move(name));
            return Refusal::None;
        case Command::Kind::SetSectionColour:
            store.setSectionColour(command.id, command.colourIndex);
            return Refusal::None;
        case Command::Kind::RemoveSection:
            store.removeSection(command.id);
            return Refusal::None;
        case Command::Kind::MoveSectionStart:
            store.moveSectionStart(command.id, command.index);
            return Refusal::None;
        case Command::Kind::SelectEvent:
            store.setSelectedEvent(command.id);
            return Refusal::None;
        case Command::Kind::Undo:
            store.undo();  // nothing to undo is a no-op, not an error
            return Refusal::None;
        case Command::Kind::Redo:
            store.redo();
            return Refusal::None;
    }
    return Refusal::None;
}

/// Reads the optional `sections` array of a state into `doc.sections` and checks the section invariants
/// (`sectionsWellFormed`) on the whole: each entry needs `id`, `name`, `colourIndex` (clamped) and
/// `firstEventIndex`. `maxSectionId` is the largest id read (0 without sections). `doc.events` must be
/// complete, since the invariants bound the indices by it.
bool parseSections(Fields& fields, Document& doc, int& maxSectionId, std::string& error) {
    maxSectionId = 0;
    if (!fields.has("sections")) {
        return true;
    }
    const json* sections = fields.getArray("sections");
    if (sections == nullptr) {
        return false;
    }
    for (std::size_t i = 0; i < sections->size(); ++i) {
        const std::string where = "sections[" + std::to_string(i) + "]";
        const json& sectionObject = (*sections)[i];
        if (!sectionObject.is_object()) {
            error = where + " must be an object";
            return false;
        }
        Fields sectionFields(sectionObject, where, error);
        Section section{};
        if (!sectionFields.getIntIn("id", 1, INT_MAX - 1, section.id) || !getSectionName(sectionFields, section.name) ||
            !sectionFields.getInt("colourIndex", section.colourIndex) ||
            !sectionFields.getInt("firstEventIndex", section.firstEventIndex)) {
            return false;
        }
        section.colourIndex = std::clamp(section.colourIndex, 0, kNumSectionColours - 1);
        maxSectionId = std::max(maxSectionId, section.id);
        doc.sections.push_back(std::move(section));
    }
    if (!sectionsWellFormed(doc)) {
        return fields.fail("sections", "must start at 0, be sorted by firstEventIndex with distinct starts inside "
                                       "the progression, and have distinct ids");
    }
    return true;
}

/// Phase A of `loadStateJson`: parses and validates `text` into a document, the two id counters and an
/// editor size. `Document` holds vectors and strings, whose destruction frees but never allocates.
/// `nextSectionId` is 0 when the state carries none: `replace` then derives it.
bool parseState(std::string_view text, Document& doc, int& nextId, int& nextSectionId, int& width, int& height,
                std::string& error) {
    json message;
    if (!parseMessage(text, message, error)) {
        return false;
    }
    Fields fields(message, "", error);

    if (!parseKeyField(fields, "projectKey", doc.projectKey, error) ||
        !parseKeyField(fields, "browsedKey", doc.browsedKey, error) ||
        !fields.getBool("progressionLoop", doc.progressionLoop) || !fields.getBool("followHost", doc.followHost)) {
        return false;
    }
    if (fields.has("autoVoiceLead") && !fields.getBool("autoVoiceLead", doc.autoVoiceLead)) {
        return false;
    }

    const json* events = fields.getArray("events");
    if (events == nullptr) {
        return false;
    }
    if (events->size() > static_cast<std::size_t>(kMaxChords)) {
        return fields.fail("events", "must hold at most " + std::to_string(kMaxChords) + " chords");
    }
    std::set<int> ids;
    int maxId = 0;
    for (std::size_t i = 0; i < events->size(); ++i) {
        const std::string where = "events[" + std::to_string(i) + "]";
        const json& eventObject = (*events)[i];
        if (!eventObject.is_object()) {
            error = where + " must be an object";
            return false;
        }
        Fields eventFields(eventObject, where, error);
        ChordEvent event{};
        int root = 0;
        std::string typeId;
        double lengthBeats = 0.0;
        if (!eventFields.getIntIn("id", 1, INT_MAX, event.id) || !eventFields.getIntIn("root", 0, 11, root) ||
            !eventFields.getString("type", typeId) || !eventFields.getNumber("lengthBeats", lengthBeats) ||
            !eventFields.getInt("velocity", event.velocity)) {
            return false;
        }
        if (!ids.insert(event.id).second) {
            return eventFields.fail("id", std::to_string(event.id) + " is used twice");
        }
        const ChordType* type = findChordType(typeId);
        if (type == nullptr) {
            return eventFields.fail("type", "\"" + typeId + "\" is not a chord type");
        }
        event.chord = Chord{root, type};
        event.lengthBeats = std::clamp(lengthBeats, kMinLengthBeats, kMaxLengthBeats);
        event.velocity = std::clamp(event.velocity, kMinVelocity, kMaxVelocity);
        // The voicing fields are optional (states saved before Phase 2 have none) and clamped like the store's setters.
        if ((eventFields.has("inversion") && !eventFields.getInt("inversion", event.inversion)) ||
            (eventFields.has("octave") && !eventFields.getInt("octave", event.octave)) ||
            (eventFields.has("manualVoicing") && !eventFields.getBool("manualVoicing", event.manualVoicing))) {
            return false;
        }
        event.inversion = std::clamp(event.inversion, 0, inversionCount(event.chord) - 1);
        event.octave = std::clamp(event.octave, kMinVoicingOctave, kMaxVoicingOctave);
        maxId = std::max(maxId, event.id);
        doc.events.push_back(event);
    }

    if (!fields.getIntIn("nextId", 1, INT_MAX, nextId)) {
        return false;
    }
    if (nextId <= maxId) {
        return fields.fail("nextId", "must exceed every event id (" + std::to_string(maxId) + ")");
    }

    int maxSectionId = 0;
    if (!parseSections(fields, doc, maxSectionId, error)) {
        return false;
    }
    nextSectionId = 0;
    if (fields.has("nextSectionId")) {
        if (!fields.getIntIn("nextSectionId", 1, INT_MAX, nextSectionId)) {
            return false;
        }
        if (nextSectionId <= maxSectionId) {
            return fields.fail("nextSectionId", "must exceed every section id (" + std::to_string(maxSectionId) + ")");
        }
    }

    width = kDefaultEditorWidth;
    height = kDefaultEditorHeight;
    if (fields.has("editor")) {
        const json* editorObject = fields.getObject("editor");
        if (editorObject == nullptr) {
            return false;
        }
        Fields editorFields(*editorObject, "editor", error);
        if (!editorFields.getIntIn("width", 1, INT_MAX, width) || !editorFields.getIntIn("height", 1, INT_MAX, height)) {
            return false;
        }
    }

    return true;
}

/// Records the failure of a reader that threw: an allocation failure, or a nlohmann exception that the
/// type checks did not anticipate. Runs inside the catch handler of a noexcept function, so it must not
/// throw itself: a failure to assign just leaves `error` as it was.
void setInternalError(std::string& error) noexcept {
    try {
        error = "internal error";
    } catch (...) {  // NOLINT(bugprone-empty-catch)
    }
}

}  // namespace

// --- Public API ------------------------------------------------------------------------

std::string keyName(const Key& key) {
    return tonicText(key.tonic) + " " + modeName(key.mode);
}

static_assert(kNoEventId == -1, "snapshotJson's default selectedEventId must be kNoEventId");

std::string snapshotJson(const Document& doc, const Palette& palette, int selectedEventId) {
    // The selection counts only when an event has that id; the UI may hold a stale one for a moment.
    int selectedIndex = -1;
    for (std::size_t i = 0; i < doc.events.size(); ++i) {
        if (doc.events[i].id == selectedEventId) {
            selectedIndex = static_cast<int>(i);
            break;
        }
    }

    const std::vector<std::vector<int>> compiledVoicings = voicings(doc);
    json events = json::array();
    double beat = 0.0;  // accumulated left to right, exactly as Document::startBeatOf does
    for (std::size_t i = 0; i < doc.events.size(); ++i) {
        const ChordEvent& e = doc.events[i];
        json event = {{"id", e.id},
                      {"root", e.chord.root},
                      {"type", e.chord.type->id},
                      {"symbol", chordSymbol(e.chord, doc.projectKey)},
                      {"roman", romanNumeral(e.chord, doc.projectKey)},
                      {"family", familyName(e.chord.type->family)},
                      {"function", functionShortName(functionOf(e.chord, doc.projectKey))},
                      {"tension", tension(e.chord, doc.projectKey)},
                      {"startBeat", beat},
                      {"lengthBeats", e.lengthBeats},
                      {"velocity", e.velocity},
                      {"inversion", e.inversion},
                      {"octave", e.octave},
                      {"manualVoicing", e.manualVoicing},
                      {"voicing", compiledVoicings[i]},
                      {"outOfKey", outOfKeyJson(e.chord, doc.projectKey)},
                      {"why", i == 0 ? json() : whyJson(doc.events[i - 1].chord, e.chord, doc.projectKey)}};
        events.push_back(std::move(event));
        beat += e.lengthBeats;
    }

    json cells = json::array();
    for (const PaletteCell& c : palette.cells) {
        cells.push_back(cellJson(c, doc.projectKey));
    }

    // Suggestions follow the selected chord, else the last one, else open the progression.
    const Chord* previous = nullptr;
    if (selectedIndex >= 0) {
        previous = &doc.events[static_cast<std::size_t>(selectedIndex)].chord;
    } else if (!doc.events.empty()) {
        previous = &doc.events.back().chord;
    }

    const json snapshot = {{"schema", kJsonSchema},
                           {"projectKey", keyToJson(doc.projectKey)},
                           {"browsedKey", keyToJson(doc.browsedKey)},
                           {"projectKeyName", keyName(doc.projectKey)},
                           {"browsedKeyName", keyName(doc.browsedKey)},
                           {"progressionLoop", doc.progressionLoop},
                           {"followHost", doc.followHost},
                           {"autoVoiceLead", doc.autoVoiceLead},
                           {"endBeat", beat},
                           {"selectedEventId", selectedIndex >= 0 ? json(selectedEventId) : json()},
                           {"modes", modesJson()},
                           {"tonics", tonicsJson()},
                           {"events", std::move(events)},
                           {"sections", sectionsJson(doc)},
                           {"suggestions", suggestionsJson(previous, doc)},
                           {"homeTriads", homeTriadsJson(doc)},
                           {"palette", {{"tiers", palette.tiers}, {"cells", std::move(cells)}}}};
    return snapshot.dump();
}

std::string stateJson(const DocumentStore& store, int editorWidth, int editorHeight) {
    const Document& doc = store.doc();
    json events = json::array();
    for (const ChordEvent& e : doc.events) {
        json event = {{"id", e.id},
                      {"root", e.chord.root},
                      {"type", e.chord.type->id},
                      {"lengthBeats", e.lengthBeats},
                      {"velocity", e.velocity},
                      {"inversion", e.inversion},
                      {"octave", e.octave},
                      {"manualVoicing", e.manualVoicing}};
        events.push_back(std::move(event));
    }
    const json state = {{"schema", kJsonSchema},
                        {"projectKey", keyToJson(doc.projectKey)},
                        {"browsedKey", keyToJson(doc.browsedKey)},
                        {"progressionLoop", doc.progressionLoop},
                        {"followHost", doc.followHost},
                        {"autoVoiceLead", doc.autoVoiceLead},
                        {"events", std::move(events)},
                        {"sections", sectionsJson(doc)},
                        {"nextId", store.nextId()},
                        {"nextSectionId", store.nextSectionId()},
                        {"editor", {{"width", editorWidth}, {"height", editorHeight}}}};
    return state.dump();
}

bool applyCommandJson(DocumentStore& store, std::string_view text, std::string& error) noexcept {
    try {
        error.clear();
        Command command;
        std::string name;  // a section name; its destructor frees, never allocates
        if (!parseCommand(text, command, name, error)) {  // Phase A: the JSON tree is destroyed on return
            return false;
        }
        switch (applyCommand(store, command, name)) {  // Phase B: only the store, plain data and the name in scope
            case Refusal::None:
                return true;
            case Refusal::DocumentFull:
                error = "\"appendChord\" refused: the document already holds " + std::to_string(kMaxChords) +
                        " chords";
                return false;
            case Refusal::IdsExhausted:
                error = "\"appendChord\" refused: no event ids left";
                return false;
            case Refusal::SectionIdsExhausted:
                error = "\"addSection\" refused: no section ids left";
                return false;
        }
        return true;
    } catch (...) {
        setInternalError(error);
        return false;
    }
}

bool loadStateJson(DocumentStore& store, std::string_view text, std::string& error, int* editorWidth,
                   int* editorHeight) noexcept {
    try {
        error.clear();
        Document doc;
        int nextId = 0;
        int nextSectionId = 0;
        int width = kDefaultEditorWidth;
        int height = kDefaultEditorHeight;
        if (!parseState(text, doc, nextId, nextSectionId, width, height, error)) {  // Phase A: the tree is destroyed on return
            return false;
        }
        store.replace(std::move(doc), nextId, nextSectionId);  // Phase B: cannot throw, nor can the writes below
        if (editorWidth != nullptr) {
            *editorWidth = width;
        }
        if (editorHeight != nullptr) {
            *editorHeight = height;
        }
        return true;
    } catch (...) {
        setInternalError(error);
        return false;
    }
}

}  // namespace chordlab
