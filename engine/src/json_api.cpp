#include "chordlab/json_api.h"

#include "chordlab/applied.h"
#include "chordlab/chord.h"
#include "chordlab/compiler.h"
#include "chordlab/document.h"
#include "chordlab/feel.h"
#include "chordlab/function.h"
#include "chordlab/generate.h"
#include "chordlab/guide.h"
#include "chordlab/harmony.h"
#include "chordlab/identify.h"
#include "chordlab/listener.h"
#include "chordlab/meter.h"
#include "chordlab/palette.h"
#include "chordlab/pitch.h"
#include "chordlab/presets.h"
#include "chordlab/relatedness.h"
#include "chordlab/relations.h"
#include "chordlab/scale.h"
#include "chordlab/suggest.h"
#include "chordlab/symbol.h"
#include "chordlab/tension.h"
#include "chordlab/tracks.h"
#include "chordlab/voicing.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
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
std::vector<PitchClass> outOfKeyPcs(const Chord& chord, const Key& key) {
    std::vector<PitchClass> out;
    for (const PitchClass pc : pitchClasses(chord)) {
        if (!inScale(pc, key.tonicPc(), key.mode)) {
            out.push_back(pc);
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

json outOfKeyJson(const Chord& chord, const Key& key) {
    return json(outOfKeyPcs(chord, key));
}

/// `outOfKeyPcs` spelled as tones of `chord` (`chordToneSpelling`, the root as `spellingKey` spells it):
/// the names the UI shows beside the symbol, "G#" for E7's third where the key would say Ab.
json outOfKeyNamesJson(const Chord& chord, const Key& key, const Key& spellingKey) {
    json out = json::array();
    for (const PitchClass pc : outOfKeyPcs(chord, key)) {
        out.push_back(chordToneSpelling(chord, pc, spellingKey).text());
    }
    return out;
}

/// `voicing`'s MIDI notes as names with octaves, each spelled as a tone of `chord`: "G#3" for note 56 in
/// E7. Middle C (60) is C4, and the octave is the letter's (B#3 and Cb4 are notes 60 and 59), as in
/// scientific pitch notation.
json voicingNamesJson(const Chord& chord, const std::vector<int>& voicing, const Key& spellingKey) {
    json out = json::array();
    for (const int note : voicing) {
        const Spelling spelling = chordToneSpelling(chord, pitchClassOf(note), spellingKey);
        const int natural = note - spelling.alter;  // the letter's own note
        const int octave = (natural >= 0 ? natural : natural - 11) / 12 - 1;
        out.push_back(spelling.text() + std::to_string(octave));
    }
    return out;
}

/// An event's `guide`: `harmonicGuide(chord, projectKey)` (guide.h) as four parallel views indexed by pitch
/// class, `{"roles":[12 guideRoleName], "scale":scaleMask, "degrees":[12], "names":[12]}`.
json guideJson(const Chord& chord, const Key& projectKey) {
    const Guide guide = harmonicGuide(chord, projectKey);
    json roles = json::array();
    json degrees = json::array();
    json names = json::array();
    for (std::size_t pc = 0; pc < 12; ++pc) {
        roles.push_back(guideRoleName(guide.role[pc]));
        degrees.push_back(guide.degree[pc]);
        names.push_back(guide.name[pc]);
    }
    return json{{"roles", std::move(roles)}, {"scale", guide.scaleMask}, {"degrees", std::move(degrees)},
                {"names", std::move(names)}};
}

/// One palette cell as the snapshot writes it (`palette.cells` and `homeTriads` alike): the cell's own
/// position, chord, symbol, numeral and `filled`, plus what is read against `projectKey`, the names of
/// its voicing and out-of-key tones spelled as the symbol is (`spellingKey`: the browsed key for a
/// palette cell, the project key for a home triad, its own for an applied chord), plus `relation` and
/// `relationText` (its common tones spelled in `spellingKey` too) from `relation` when it is non-null
/// (absent otherwise: no reference, or mode off).
json cellJson(const PaletteCell& c, const Key& projectKey, const Key& spellingKey, const Relatedness* relation) {
    const std::vector<int> voicing = closeVoicing(c.chord);
    json cell = {{"degree", c.degree},
            {"tier", c.tier},
            {"root", c.chord.root},
            {"type", c.chord.type->id},
            {"symbol", c.symbol},
            {"roman", c.roman},
            {"family", familyName(c.chord.type->family)},
            {"function", functionShortName(functionOf(c.chord, projectKey))},
            {"tension", tension(c.chord, projectKey)},
            {"filled", c.filled},
            {"voicing", voicing},
            {"voicingNames", voicingNamesJson(c.chord, voicing, spellingKey)},
            {"outOfKey", outOfKeyJson(c.chord, projectKey)},
            {"outOfKeyNames", outOfKeyNamesJson(c.chord, projectKey, spellingKey)}};
    if (relation != nullptr) {
        cell["relation"] = relation->score;
        cell["relationText"] = relationText(relation->relation, spellingKey);
    }
    return cell;
}

/// The cells of the snapshot's home row: nothing while the project key is the one browsed (the palette
/// already shows it); otherwise the project key's seven diatonic triads in degree order, each the cell the
/// palette's triad row would hold for it when browsing the project key.
std::vector<PaletteCell> homeTriadCells(const Document& doc) {
    std::vector<PaletteCell> out;
    if (doc.browsedKey == doc.projectKey) {
        return out;
    }
    for (int degree = 0; degree < 7; ++degree) {
        const Chord triad = diatonicTriad(doc.projectKey, degree);
        out.push_back(PaletteCell{degree, kPaletteTriadTier, triad, fullyInKey(triad, doc.projectKey),
                                  chordSymbol(triad, doc.projectKey), romanNumeral(triad, doc.projectKey)});
    }
    return out;
}

/// The snapshot's `homeTriads`: `homeTriadCells` as `cellJson` writes them, each with an `id`
/// ("home-<degree>") that no palette cell has. `relations` is null, or holds one entry per home cell.
json homeTriadsJson(const std::vector<PaletteCell>& home, const Key& projectKey, const Relatedness* relations) {
    json out = json::array();
    for (std::size_t i = 0; i < home.size(); ++i) {
        json entry = cellJson(home[i], projectKey, projectKey, relations != nullptr ? &relations[i] : nullptr);
        entry["id"] = "home-" + std::to_string(home[i].degree);
        out.push_back(std::move(entry));
    }
    return out;
}

/// Rows of the snapshot's applied-chord grid: the secondary dominant above the leading-tone chord.
constexpr int kAppliedDominantTier = 1;
constexpr int kAppliedLeadingToneTier = 0;

/// The cells of the snapshot's `applied`, parallel to `applied` (`appliedChords(doc.browsedKey)`): each in
/// the column of the degree it tonicises, the dominant seventh on the upper row, spelled in its own
/// `spellingKey` and read against the project key like every cell.
std::vector<PaletteCell> appliedCells(const Document& doc, const std::vector<AppliedChord>& applied) {
    std::vector<PaletteCell> out;
    out.reserve(applied.size());
    for (const AppliedChord& a : applied) {
        const bool dominant = std::string_view(a.chord.type->id) == "7";
        out.push_back(PaletteCell{a.targetDegree, dominant ? kAppliedDominantTier : kAppliedLeadingToneTier, a.chord,
                                  fullyInKey(a.chord, doc.projectKey), chordSymbol(a.chord, a.spellingKey),
                                  romanNumeral(a.chord, doc.projectKey)});
    }
    return out;
}

/// The snapshot's `applied`: `appliedCells` as `cellJson` writes them, each with an `id`
/// ("applied-<degree>-<tier>"), the `label` ("V7/ii", relative to the browsed key) and the `target`, the
/// tonicised triad's symbol in the browsed key. `relations` is null, or holds one entry per cell.
json appliedJson(const Document& doc, const std::vector<AppliedChord>& applied, const std::vector<PaletteCell>& cells,
                 const Relatedness* relations) {
    json out = json::array();
    for (std::size_t i = 0; i < cells.size(); ++i) {
        json entry = cellJson(cells[i], doc.projectKey, applied[i].spellingKey, relations != nullptr ? &relations[i] : nullptr);
        entry["id"] = "applied-" + std::to_string(cells[i].degree) + "-" + std::to_string(cells[i].tier);
        entry["label"] = applied[i].label;
        entry["target"] = chordSymbol(diatonicTriad(doc.browsedKey, applied[i].targetDegree), doc.browsedKey);
        out.push_back(std::move(entry));
    }
    return out;
}

/// The `why` object of event `to` against the event `from` before it: `relate` as data plus the sentence
/// whose rules json_api.h spells out, from the same pieces (harmony.h) as the suggestions' reasons and the
/// cells' `relationText`.
json whyJson(const Chord& from, const Chord& to, const Key& projectKey) {
    const Relation relation = relate(from, to, projectKey);

    json commonNames = json::array();
    for (const Spelling& spelling : commonToneSpellings(relation, projectKey)) {
        commonNames.push_back(spelling.text());
    }

    std::string text = chordSymbol(from, projectKey) + " → " + chordSymbol(to, projectKey) + ": " +
                       rootMotionName(relation.motion);
    if (functionOf(from, projectKey) == Function::Dominant && functionOf(to, projectKey) == Function::Tonic) {
        text += ", resolves (D→T)";
    }
    if (relation.commonTones >= 1) {
        text += ", " + commonTonesText(relation, projectKey);
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

/// A track's notes as the snapshot publishes them: one array per field, all of the track's note count, in its note
/// order (tracks design D6). Built column by column in one pass over the notes, with no object per note.
json snapshotNoteColumnsJson(const ChordTimeline& timeline, const Track& track) {
    const std::size_t count = track.notes.size();
    json::array_t id, pitch, start, length, velocity, anchor, offset, active;
    for (json::array_t* column : {&id, &pitch, &start, &length, &velocity, &anchor, &offset, &active}) {
        column->reserve(count);
    }
    for (const TrackNote& n : track.notes) {
        id.emplace_back(n.id);
        pitch.emplace_back(n.pitch);
        start.emplace_back(noteStart(timeline, n));
        length.emplace_back(n.lengthTicks);
        velocity.emplace_back(n.velocity);
        anchor.emplace_back(n.anchorEventId);
        offset.emplace_back(n.offsetTick);
        active.emplace_back(noteActive(timeline, n));
    }
    json columns = json::object();
    columns["id"] = json(std::move(id));
    columns["pitch"] = json(std::move(pitch));
    columns["start"] = json(std::move(start));
    columns["length"] = json(std::move(length));
    columns["velocity"] = json(std::move(velocity));
    columns["anchor"] = json(std::move(anchor));
    columns["offset"] = json(std::move(offset));
    columns["active"] = json(std::move(active));
    return columns;
}

/// The snapshot's `tracks` (tracks design D6): each track with its audibility and its notes column-wise
/// (`snapshotNoteColumnsJson`: derived start, anchor, offset, active). The chord timeline is built once for all notes.
json snapshotTracksJson(const Document& doc) {
    const ChordTimeline timeline = chordTimeline(doc);
    json tracks = json::array();
    for (std::size_t i = 0; i < doc.tracks.size(); ++i) {
        const Track& t = doc.tracks[i];
        tracks.push_back({{"id", t.id},
                          {"name", t.name},
                          {"role", trackRoleName(t.role)},
                          {"mute", t.mute},
                          {"solo", t.solo},
                          {"audible", trackAudible(doc, i)},
                          {"notes", snapshotNoteColumnsJson(timeline, t)}});
    }
    return tracks;
}

/// The snapshot's `noteSelection`: `selection` normalised against `doc` (the ids its track holds, in the track's note
/// order, once each), or null when that leaves nothing or the track is not in `doc`.
json noteSelectionJson(const Document& doc, const NoteSelection& selection) {
    const auto track = std::find_if(doc.tracks.begin(), doc.tracks.end(),
                                    [&](const Track& t) { return t.id == selection.trackId; });
    if (selection.trackId == kNoTrackId || track == doc.tracks.end()) {
        return json();
    }
    const std::set<int> wanted(selection.ids.begin(), selection.ids.end());
    json ids = json::array();
    for (const TrackNote& n : track->notes) {
        if (wanted.count(n.id) != 0) {
            ids.push_back(n.id);
        }
    }
    if (ids.empty()) {
        return json();
    }
    return {{"trackId", selection.trackId}, {"ids", std::move(ids)}};
}

/// The snapshot's `trackLimits`: the track caps by name (tracks.h).
json trackLimitsJson() {
    return {{"maxTracks", kMaxTracks},
            {"maxNotesPerTrack", kMaxNotesPerTrack},
            {"minNoteTicks", kMinTrackNoteTicks},
            {"maxPolyphony", kMaxTrackPolyphony},
            {"maxOnsetsPerBeat", kMaxTrackOnsetsPerBeat}};
}

/// The snapshot's `presets`: every record of `allPresets()` in table order, with `fits` read against
/// `projectKey`.
json presetsJson(const Key& projectKey) {
    json presets = json::array();
    for (const Preset& p : allPresets()) {
        presets.push_back({{"id", p.id},
                           {"name", p.name},
                           {"genre", p.genre},
                           {"tonality", tonalityName(p.tonality)},
                           {"numerals", presetNumerals(p)},
                           {"fits", presetFits(p, projectKey)}});
    }
    return presets;
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

    /// An integer in `lo`..`hi` inclusive, read at 64 bits: the JSON number must be an integer (no fraction or
    /// exponent) that fits an `int64_t`, so nothing is narrowed or wrapped before the range check (2^63 and
    /// -2^63 - 1 are errors, not wrapped values).
    bool getInt64In(const char* name, std::int64_t lo, std::int64_t hi, std::int64_t& out) {
        const json* v = require(name);
        if (v == nullptr) {
            return false;
        }
        if (v->is_number_unsigned()) {
            const auto u = v->get<std::uint64_t>();
            if (hi >= 0 && u <= static_cast<std::uint64_t>(hi) && static_cast<std::int64_t>(u) >= lo) {
                out = static_cast<std::int64_t>(u);
                return true;
            }
        } else if (v->is_number_integer()) {
            const auto i = v->get<std::int64_t>();
            if (i >= lo && i <= hi) {
                out = i;
                return true;
            }
        }
        return fail(name, "must be an integer in " + std::to_string(lo) + ".." + std::to_string(hi));
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

    /// The field, or nullptr after recording "missing <field>".
    const json* require(const char* name) {
        const json* v = find(name);
        if (v == nullptr) {
            error_ = "missing " + label(name);
        }
        return v;
    }

private:
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

/// Reads the relation mode at `fields[name]`: one of the five `relationModeName` strings. Missing,
/// mistyped and unknown values alike get a message that lists the five.
bool parseRelationModeField(Fields& fields, const char* name, RelationMode& out) {
    static const std::string kExpected = [] {
        std::string list = "must be one of ";
        for (int i = 0; i < kNumRelationModes; ++i) {
            list += std::string(i == 0 ? "" : ", ") + "\"" + relationModeName(static_cast<RelationMode>(i)) + "\"";
        }
        return list;
    }();
    const json* v = fields.find(name);
    if (v == nullptr) {
        return fields.fail(name, "is missing; it " + kExpected);
    }
    if (!v->is_string()) {
        return fields.fail(name, kExpected);
    }
    const std::string text = v->get<std::string>();
    const std::optional<RelationMode> mode = parseRelationMode(text);
    if (!mode) {
        return fields.fail(name, kExpected + " (got \"" + text + "\")");
    }
    out = *mode;
    return true;
}

/// Wire names of `ProgressionLoad`, in enum order: what `loadPreset`'s `mode` accepts.
constexpr const char* kProgressionLoadNames[] = {"append", "replace"};

/// Reads `loadPreset`'s `mode`: "append" or "replace". Missing, mistyped and unknown values alike get a
/// message that lists both.
bool parseProgressionLoadField(Fields& fields, ProgressionLoad& out) {
    const std::string expected = std::string("must be one of \"") + kProgressionLoadNames[0] + "\", \"" +
                                 kProgressionLoadNames[1] + "\"";
    const json* v = fields.find("mode");
    if (v == nullptr) {
        return fields.fail("mode", "is missing; it " + expected);
    }
    if (!v->is_string()) {
        return fields.fail("mode", expected);
    }
    const std::string text = v->get<std::string>();
    if (text == kProgressionLoadNames[0]) {
        out = ProgressionLoad::Append;
    } else if (text == kProgressionLoadNames[1]) {
        out = ProgressionLoad::Replace;
    } else {
        return fields.fail("mode", expected + " (got \"" + text + "\")");
    }
    return true;
}

// --- Feel and meter wire names ---------------------------------------------------------

/// Wire names of the feel enums, each in enum order (`feel.h`).
constexpr const char* kRhythmNames[] = {"block", "pulse", "subdivided", "offbeats", "lowThenUpper", "arpeggio"};
constexpr const char* kArpPatternNames[] = {"up",       "down",        "upDown",        "downUp",  "upAndDown",   "downAndUp",
                                            "converge", "diverge",     "conAndDiverge", "pinkyUp", "pinkyUpDown",
                                            "thumbUp",  "thumbUpDown", "alberti",       "rootFirst", "chord",     "random"};
static_assert(sizeof(kArpPatternNames) / sizeof(kArpPatternNames[0]) == kArpPatternCount, "one name per pattern");
constexpr const char* kArpRateNames[] = {"pulse", "subdivision", "half"};
constexpr const char* kArpRangeNames[] = {"one", "two"};
constexpr const char* kStrumNames[] = {"off", "down", "up", "downUp"};
constexpr const char* kStrumSpeedNames[] = {"tight", "medium", "loose"};
constexpr const char* kHumaniseNames[] = {"off", "light", "medium", "heavy"};

template <std::size_t N>
std::string optionList(const char* const (&names)[N]) {
    std::string list = "must be one of ";
    for (std::size_t i = 0; i < N; ++i) {
        list += std::string(i == 0 ? "" : ", ") + "\"" + names[i] + "\"";
    }
    return list;
}

/// Reads `fields[name]` as one of `names` and writes its index (the enum value) into `out`. Missing, mistyped and
/// unknown values alike get a message that lists the options.
template <std::size_t N, typename Enum>
bool parseNameField(Fields& fields, const char* name, const char* const (&names)[N], Enum& out) {
    const std::string expected = optionList(names);
    const json* v = fields.find(name);
    if (v == nullptr) {
        return fields.fail(name, "is missing; it " + expected);
    }
    if (!v->is_string()) {
        return fields.fail(name, expected);
    }
    const std::string text = v->get<std::string>();
    for (std::size_t i = 0; i < N; ++i) {
        if (text == names[i]) {
            out = static_cast<Enum>(i);
            return true;
        }
    }
    return fields.fail(name, expected + " (got \"" + text + "\")");
}

/// A feel in `fields` (a `setFeel` message or a state's `feel` object): `rhythm`, `strum`, `strumSpeed` and
/// `humanise` required; `arpPattern`, `arpRate` and `arpRange` optional (the defaults up, subdivision, one when
/// missing; present means valid, or the whole message / state is rejected). `rhythm` also accepts `arpUp` and
/// `arpUpDown`, the names of builds before the patterns: the Arpeggio rhythm with pattern up / upDown unless
/// `arpPattern` is given. Parsed into a local first, so a failure leaves `out` as it was.
bool parseFeelFields(Fields& fields, Feel& out) {
    Feel feel;
    const json* rhythm = fields.find("rhythm");
    const bool legacyUp = rhythm != nullptr && rhythm->is_string() && *rhythm == "arpUp";
    const bool legacyUpDown = rhythm != nullptr && rhythm->is_string() && *rhythm == "arpUpDown";
    if (legacyUp || legacyUpDown) {
        feel.rhythm = Rhythm::Arpeggio;
        feel.pattern = legacyUpDown ? ArpPattern::UpDown : ArpPattern::Up;
    } else if (!parseNameField(fields, "rhythm", kRhythmNames, feel.rhythm)) {
        return false;
    }
    if (!parseNameField(fields, "strum", kStrumNames, feel.strum) ||
        !parseNameField(fields, "strumSpeed", kStrumSpeedNames, feel.speed) ||
        !parseNameField(fields, "humanise", kHumaniseNames, feel.humanise)) {
        return false;
    }
    if (fields.has("arpPattern") && !parseNameField(fields, "arpPattern", kArpPatternNames, feel.pattern)) {
        return false;
    }
    if (fields.has("arpRate") && !parseNameField(fields, "arpRate", kArpRateNames, feel.rate)) {
        return false;
    }
    if (fields.has("arpRange") && !parseNameField(fields, "arpRange", kArpRangeNames, feel.range)) {
        return false;
    }
    out = feel;
    return true;
}

/// `num` and `den` of a meter, in `fields` (a `setMeter` message or a state's `meter` object): JSON integers (like every
/// integer field: 3.0 and "3" are rejected) forming a `validMeter`.
bool parseMeterFields(Fields& fields, Meter& out) {
    Meter meter;
    if (!fields.getIntIn("num", 1, 16, meter.num) || !fields.getInt("den", meter.den)) {
        return false;
    }
    if (!validMeter(meter)) {
        return fields.fail("den", "must be 2, 4 or 8");
    }
    out = meter;
    return true;
}

json feelToJson(const Feel& feel) {
    return {{"rhythm", kRhythmNames[static_cast<int>(feel.rhythm)]},
            {"strum", kStrumNames[static_cast<int>(feel.strum)]},
            {"strumSpeed", kStrumSpeedNames[static_cast<int>(feel.speed)]},
            {"humanise", kHumaniseNames[static_cast<int>(feel.humanise)]},
            {"arpPattern", kArpPatternNames[static_cast<int>(feel.pattern)]},
            {"arpRate", kArpRateNames[static_cast<int>(feel.rate)]},
            {"arpRange", kArpRangeNames[static_cast<int>(feel.range)]}};
}

json meterToJson(const Meter& meter) {
    return {{"num", meter.num}, {"den", meter.den}};
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
        SetTransposeWithKey,
        SetKeyHintSeen,
        SetMidiThru,
        SetFeel,
        SetMeter,
        TransposeFromKey,
        TransposeProgression,
        AddSection,
        RenameSection,
        SetSectionColour,
        RemoveSection,
        MoveSectionStart,
        SelectEvent,
        SelectEvents,
        InsertChords,
        RemoveChords,
        SetLengths,
        SetNewChordLength,
        SetRelationMode,
        LoadPreset,
        AddTrack,
        RemoveTrack,
        PatchTrack,
        MoveTrack,
        SetTrackMute,
        SetTrackSolo,
        SetChordsMute,
        SetChordsSolo,
        InsertNotes,
        RemoveNotes,
        UpdateNotes,
        GenerateBass,
        GenerateLead,
        SelectNotes,
        Undo,
        Redo
    };
    Kind kind = Kind::Undo;
    Key key{};                 // SetKey
    int steps = 0;             // BrowseSteps
    Mode mode = Mode::Ionian;  // BrowseMode
    Chord chord{0, nullptr};   // AppendChord
    double lengthBeats = 4.0;  // AppendChord (optional), SetLength, SetLengths, SetNewChordLength (when `hasLength`)
    // AppendChord: `lengthBeats` was given, otherwise the new-chord length; SetNewChordLength: false is null
    bool hasLength = false;
    int velocity = 100;        // AppendChord (optional, store default), SetVelocity
    // MoveChord, SetLength, SetVelocity, RemoveChord, the voicing, section and selection commands; SelectEvents' anchor
    // (kNoEventId when absent or null); the track of the track and note commands (wire `id`, or `trackId` for the note
    // commands; SelectNotes: kNoTrackId for a null `trackId`)
    int id = 0;
    int index = 0;             // MoveChord, AddSection, MoveSectionStart, InsertChords, MoveTrack
    // SetProgressionLoop, SetFollowHost, SetAutoVoiceLead, SetTransposeWithKey, SetKeyHintSeen, SetMidiThru,
    // SetTrackMute, SetTrackSolo, SetChordsMute, SetChordsSolo
    bool on = false;
    Feel feel{};               // SetFeel
    Meter meter{};             // SetMeter
    int semitones = 0;         // TransposeFromKey (the pitch class moved from, 0..11), TransposeProgression (-11..11)
    int inversion = 0;         // SetInversion, AppendChord (when `hasInversion`)
    bool hasInversion = false; // AppendChord: `inversion` was given; the chord is then manual
    int octave = 0;            // SetOctave
    int colourIndex = 0;       // SetSectionColour
    RelationMode relationMode = RelationMode::Blend;  // SetRelationMode
    const Preset* preset = nullptr;                   // LoadPreset: a record of allPresets(), never null once parsed
    ProgressionLoad load = ProgressionLoad::Append;   // LoadPreset
    TrackRole role = TrackRole::Other;                // AddTrack, PatchTrack (when `hasRole`)
    bool hasRole = false;                             // PatchTrack: `role` was given
    bool hasName = false;                             // AddTrack, PatchTrack: a `name` was given (it travels in `name`)
    bool hasBaseRevision = false;                     // InsertNotes, RemoveNotes, UpdateNotes: `baseRevision` was given
    std::uint64_t baseRevision = 0;                   // ... and this is it
    BassPattern bassPattern = BassPattern::Roots;     // GenerateBass (its scope travels in `CommandLists::ids`)
    LeadPattern leadPattern = LeadPattern::Motif;     // GenerateLead (its scope travels in `CommandLists::ids`)
    int variation = 0;                                // GenerateLead: 0..INT_MAX, 0 when absent
};
static_assert(std::is_trivially_destructible_v<Command>, "Command must be plain data: destroying one must not allocate");

/// What a command carries besides `Command`'s plain fields, owned by the reader's frame like the section name:
/// the chords of `insertChords` (with their notes), the ids of `removeChords`, `setLengths`, `selectEvents`,
/// `removeNotes` and `selectNotes` and the scope of `generateBass` and `generateLead`, and the notes of `insertNotes` and
/// `updateNotes`. Its destructor frees, never allocates.
struct CommandLists {
    std::vector<ChordSpec> chords;
    std::vector<int> ids;
    std::vector<NoteSpec> notes;
    std::vector<NoteEdit> edits;
};

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
    if (fields.has("lengthBeats")) {
        if (!fields.getNumber("lengthBeats", out.lengthBeats)) {
            return false;
        }
        out.hasLength = true;
    }
    if (fields.has("velocity") && !fields.getInt("velocity", out.velocity)) {
        return false;
    }
    out.chord = Chord{root, type};
    if (fields.has("inversion")) {
        if (!fields.getIntIn("inversion", 0, inversionCount(out.chord) - 1, out.inversion)) {
            return false;
        }
        out.hasInversion = true;
    }
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

/// `setProgressionLoop` / `setFollowHost` / `setAutoVoiceLead` / `setTransposeWithKey` / `setKeyHintSeen` /
/// `setMidiThru`.
bool parseToggle(Fields& fields, Command::Kind kind, Command& out) {
    out.kind = kind;
    return fields.getBool("on", out.on);
}

/// `setFeel {rhythm, strum, strumSpeed, humanise[, arpPattern, arpRate, arpRange]}`.
bool parseSetFeel(Fields& fields, Command& out) {
    out.kind = Command::Kind::SetFeel;
    return parseFeelFields(fields, out.feel);
}

/// `setMeter {num, den}`.
bool parseSetMeter(Fields& fields, Command& out) {
    out.kind = Command::Kind::SetMeter;
    return parseMeterFields(fields, out.meter);
}

/// `transposeFromKey {fromTonicPc}`.
bool parseTransposeFromKey(Fields& fields, Command& out) {
    out.kind = Command::Kind::TransposeFromKey;
    return fields.getIntIn("fromTonicPc", 0, 11, out.semitones);
}

/// `transposeProgression {semitones}`.
bool parseTransposeProgression(Fields& fields, Command& out) {
    out.kind = Command::Kind::TransposeProgression;
    return fields.getIntIn("semitones", -11, 11, out.semitones);
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

/// Reads a `name` field: a string of at most `maxBytes` UTF-8 bytes.
bool getBoundedName(Fields& fields, std::string& name, int maxBytes) {
    if (!fields.getString("name", name)) {
        return false;
    }
    if (name.size() > static_cast<std::size_t>(maxBytes)) {
        return fields.fail("name", "must be at most " + std::to_string(maxBytes) + " bytes");
    }
    return true;
}

/// Reads a section `name` field: a string of at most `kMaxSectionNameBytes` UTF-8 bytes.
bool getSectionName(Fields& fields, std::string& name) {
    return getBoundedName(fields, name, kMaxSectionNameBytes);
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

/// Reads the role at `fields[name]`: "bass", "lead" or "other" (lowercase, as the snapshot publishes them).
/// Missing is "missing <field>"; mistyped and unknown values alike are one message listing the three.
bool parseRoleField(Fields& fields, const char* name, TrackRole& out) {
    const json* v = fields.require(name);
    if (v == nullptr) {
        return false;
    }
    if (v->is_string()) {
        if (const std::optional<TrackRole> role = trackRoleFromName(v->get_ref<const std::string&>())) {
            out = *role;
            return true;
        }
    }
    return fields.fail(name, "must be \"bass\", \"lead\" or \"other\"");
}

/// An optional integer: absent leaves `out` empty, present must be an integer (null is not one).
bool getOptionalInt(Fields& fields, const char* name, std::optional<int>& out) {
    if (!fields.has(name)) {
        return true;
    }
    int value = 0;
    if (!fields.getInt(name, value)) {
        return false;
    }
    out = value;
    return true;
}

/// An optional 64-bit tick count (`getInt64In` over the whole of int64; the store clamps it into the domain).
bool getOptionalTicks(Fields& fields, const char* name, std::optional<std::int64_t>& out) {
    if (!fields.has(name)) {
        return true;
    }
    std::int64_t value = 0;
    if (!fields.getInt64In(name, std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max(), value)) {
        return false;
    }
    out = value;
    return true;
}

/// A required 64-bit tick count.
bool getTicks(Fields& fields, const char* name, std::int64_t& out) {
    return fields.getInt64In(name, std::numeric_limits<std::int64_t>::min(), std::numeric_limits<std::int64_t>::max(), out);
}

/// The most chord notes one `insertChords` batch may carry in all: every track's worth.
constexpr std::size_t kMaxChordNotes = static_cast<std::size_t>(kMaxTracks) * static_cast<std::size_t>(kMaxNotesPerTrack);

/// The `notes` of one `insertChords` entry, `chordFields` being `chords[i]`: an array of objects `{lane:{trackId?,
/// name, role}, offset, length, pitch, velocity?}` (`velocity` 100 when absent). `noteTotal` counts the notes of the
/// whole batch so far and may not pass `kMaxChordNotes`, checked before any element is read. Ticks are int64
/// (the store clamps), pitch, velocity and the lane's `trackId` ints. Errors carry the whole path
/// ("chords[1].notes[0].lane.role must be ...").
bool parseChordNotes(Fields& chordFields, std::vector<ChordNoteSpec>& out, std::size_t& noteTotal, std::string& error) {
    const json* list = chordFields.getArray("notes");
    if (list == nullptr) {
        return false;
    }
    if (list->size() > kMaxChordNotes - noteTotal) {
        return chordFields.fail("notes", "must not bring the batch to more than " + std::to_string(kMaxChordNotes) + " notes");
    }
    noteTotal += list->size();
    out.clear();
    out.reserve(list->size());
    for (std::size_t i = 0; i < list->size(); ++i) {
        const std::string where = chordFields.label("notes") + "[" + std::to_string(i) + "]";
        const json& item = (*list)[i];
        if (!item.is_object()) {
            error = where + " must be an object";
            return false;
        }
        Fields noteFields(item, where, error);
        ChordNoteSpec note;
        const json* laneObject = noteFields.getObject("lane");
        if (laneObject == nullptr) {
            return false;
        }
        Fields laneFields(*laneObject, noteFields.label("lane"), error);
        if (!getBoundedName(laneFields, note.lane.name, kMaxTrackNameBytes) ||
            !parseRoleField(laneFields, "role", note.lane.role)) {
            return false;
        }
        if (laneFields.has("trackId") && !laneFields.find("trackId")->is_null()) {
            int trackId = 0;
            if (!laneFields.getInt("trackId", trackId)) {
                return false;
            }
            note.lane.trackId = trackId;
        }
        if (!getTicks(noteFields, "offset", note.offsetTick) || !getTicks(noteFields, "length", note.lengthTicks) ||
            !noteFields.getInt("pitch", note.pitch)) {
            return false;
        }
        if (noteFields.has("velocity") && !noteFields.getInt("velocity", note.velocity)) {
            return false;
        }
        out.push_back(std::move(note));
    }
    return true;
}

/// One `insertChords` entry, `fields` being `chords[i]`: `root` (0..11) and `type` (a dictionary id) required;
/// `lengthBeats` (a finite number), `velocity` (an integer), `inversion` (0..tones - 1 of the chord), `octave`
/// (kMinVoicingOctave..kMaxVoicingOctave) and `manualVoicing` (a boolean) optional. Lengths and velocities
/// are clamped by the store; the voicing fields are range-checked here, as `appendChord`'s inversion is. Every
/// failure names the field with the entry's index ("chords[2].root must be an integer in 0..11").
bool parseChordSpec(Fields& fields, ChordSpec& out, std::size_t& noteTotal, std::string& error) {
    int root = 0;
    std::string typeId;
    if (!fields.getIntIn("root", 0, 11, root) || !fields.getString("type", typeId)) {
        return false;
    }
    const ChordType* type = findChordType(typeId);
    if (type == nullptr) {
        return fields.fail("type", "\"" + typeId + "\" is not a chord type");
    }
    ChordSpec spec{Chord{root, type}};
    if (fields.has("lengthBeats")) {
        double beats = 0.0;
        if (!fields.getNumber("lengthBeats", beats)) {
            return false;
        }
        spec.lengthBeats = beats;
    }
    if (fields.has("velocity") && !fields.getInt("velocity", spec.velocity)) {
        return false;
    }
    if (fields.has("inversion")) {
        int inversion = 0;
        if (!fields.getIntIn("inversion", 0, inversionCount(spec.chord) - 1, inversion)) {
            return false;
        }
        spec.inversion = inversion;
    }
    if (fields.has("octave")) {
        int octave = 0;
        if (!fields.getIntIn("octave", kMinVoicingOctave, kMaxVoicingOctave, octave)) {
            return false;
        }
        spec.octave = octave;
    }
    if (fields.has("manualVoicing")) {
        bool manual = false;
        if (!fields.getBool("manualVoicing", manual)) {
            return false;
        }
        spec.manualVoicing = manual;
    }
    if (fields.has("notes") && !parseChordNotes(fields, spec.notes, noteTotal, error)) {
        return false;
    }
    out = std::move(spec);
    return true;
}

/// `insertChords {index, chords}`: `index` an integer (clamped by the store); `chords` a non-empty array of at
/// most `kMaxChords` objects, each read by `parseChordSpec`. All of it is read before the store is touched, so
/// one bad entry rejects the whole message.
bool parseInsertChords(Fields& fields, Command& out, std::vector<ChordSpec>& chords, std::string& error) {
    out.kind = Command::Kind::InsertChords;
    if (!fields.getInt("index", out.index)) {
        return false;
    }
    const json* list = fields.getArray("chords");
    if (list == nullptr) {
        return false;
    }
    if (list->empty()) {
        return fields.fail("chords", "must hold at least one chord");
    }
    if (list->size() > static_cast<std::size_t>(kMaxChords)) {
        return fields.fail("chords", "must hold at most " + std::to_string(kMaxChords) + " chords");
    }
    chords.clear();
    chords.reserve(list->size());
    std::size_t noteTotal = 0;
    for (std::size_t i = 0; i < list->size(); ++i) {
        const std::string where = "chords[" + std::to_string(i) + "]";
        const json& item = (*list)[i];
        if (!item.is_object()) {
            error = where + " must be an object";
            return false;
        }
        Fields itemFields(item, where, error);
        ChordSpec spec{Chord{0, nullptr}};
        if (!parseChordSpec(itemFields, spec, noteTotal, error)) {
            return false;
        }
        chords.push_back(std::move(spec));
    }
    return true;
}

/// `ids` (or the array field `name`): an array of integers, non-empty unless `allowEmpty` (`selectEvents`, where an
/// empty list clears), of at most `maxCount`. Repeats and ids no event has are kept here: the store call decides what
/// they mean (most skip an unknown id, `generateBass` refuses one in its `scope`). Each
/// element is read through `toInt` (a JSON integer that fits an `int`, checked at 64 bits, never wrapped) and a bad
/// one is named by its index ("ids[1] must be an integer", "scope[2] must be an integer").
bool parseIds(Fields& fields, bool allowEmpty, std::vector<int>& ids, std::string& error,
              std::size_t maxCount = std::numeric_limits<std::size_t>::max(), const char* name = "ids") {
    const json* list = fields.getArray(name);
    if (list == nullptr) {
        return false;
    }
    if (list->empty() && !allowEmpty) {
        return fields.fail(name, "must hold at least one id");
    }
    if (list->size() > maxCount) {
        return fields.fail(name, "must hold at most " + std::to_string(maxCount) + " ids");
    }
    ids.clear();
    ids.reserve(list->size());
    for (std::size_t i = 0; i < list->size(); ++i) {
        int id = 0;
        if (!toInt((*list)[i], id)) {
            error = std::string(name) + "[" + std::to_string(i) + "] must be an integer";
            return false;
        }
        ids.push_back(id);
    }
    return true;
}

/// `selectEvents {ids, anchor?}`: `ids` an array of integers (empty clears the selection); `anchor` an
/// integer, or null or absent for the last selected block.
bool parseSelectEvents(Fields& fields, Command& out, std::vector<int>& ids, std::string& error) {
    out.kind = Command::Kind::SelectEvents;
    if (!parseIds(fields, true, ids, error)) {
        return false;
    }
    out.id = kNoEventId;
    const json* anchor = fields.find("anchor");
    if (anchor == nullptr || anchor->is_null()) {
        return true;
    }
    return toInt(*anchor, out.id) || fields.fail("anchor", "must be an integer or null");
}

/// The optional `baseRevision` of the note commands (tracks D4): absent, or a non-negative JSON integer (read as
/// `uint64_t`, which nlohmann holds exactly); a negative, fractional or larger value is an error, not a wrapped one.
bool parseBaseRevision(Fields& fields, Command& out) {
    const json* v = fields.find("baseRevision");
    if (v == nullptr) {
        return true;
    }
    if (v->is_number_unsigned()) {
        out.baseRevision = v->get<std::uint64_t>();
    } else if (v->is_number_integer() && v->get<std::int64_t>() >= 0) {
        out.baseRevision = static_cast<std::uint64_t>(v->get<std::int64_t>());
    } else {
        return fields.fail("baseRevision", "must be a non-negative integer");
    }
    out.hasBaseRevision = true;
    return true;
}

/// `addTrack {role, name?}`: `role` "bass", "lead" or "other"; `name` a string of at most `kMaxTrackNameBytes` bytes
/// (longer is rejected, not cut, as a section name), travelling in `name`; empty or absent takes the store's default.
bool parseAddTrack(Fields& fields, Command& out, std::string& name) {
    out.kind = Command::Kind::AddTrack;
    if (!parseRoleField(fields, "role", out.role)) {
        return false;
    }
    out.hasName = fields.has("name");
    return !out.hasName || getBoundedName(fields, name, kMaxTrackNameBytes);
}

/// `patchTrack {id, name?, role?}`: either field may be absent; with neither the store refuses.
bool parsePatchTrack(Fields& fields, Command& out, std::string& name) {
    out.kind = Command::Kind::PatchTrack;
    if (!fields.getInt("id", out.id)) {
        return false;
    }
    out.hasName = fields.has("name");
    if (out.hasName && !getBoundedName(fields, name, kMaxTrackNameBytes)) {
        return false;
    }
    out.hasRole = fields.has("role");
    return !out.hasRole || parseRoleField(fields, "role", out.role);
}

/// `setTrackMute {id, on}` / `setTrackSolo {id, on}`.
bool parseTrackToggle(Fields& fields, Command::Kind kind, Command& out) {
    out.kind = kind;
    return fields.getInt("id", out.id) && fields.getBool("on", out.on);
}

/// `insertNotes {trackId, notes:[{pitch, start, length, velocity?}], baseRevision?}`: `notes` an array of at most
/// `kMaxNotesPerTrack` objects (an empty one is the store's refusal); `pitch` and `velocity` ints (100 when
/// absent), `start` and `length` 64-bit integers, all clamped by the store. Errors name the entry
/// ("notes[2].start must be an integer in ...").
bool parseInsertNotes(Fields& fields, Command& out, std::vector<NoteSpec>& notes, std::string& error) {
    out.kind = Command::Kind::InsertNotes;
    if (!fields.getInt("trackId", out.id)) {
        return false;
    }
    const json* list = fields.getArray("notes");
    if (list == nullptr) {
        return false;
    }
    if (list->size() > static_cast<std::size_t>(kMaxNotesPerTrack)) {
        return fields.fail("notes", "must hold at most " + std::to_string(kMaxNotesPerTrack) + " notes");
    }
    notes.clear();
    notes.reserve(list->size());
    for (std::size_t i = 0; i < list->size(); ++i) {
        const std::string where = "notes[" + std::to_string(i) + "]";
        const json& item = (*list)[i];
        if (!item.is_object()) {
            error = where + " must be an object";
            return false;
        }
        Fields noteFields(item, where, error);
        NoteSpec spec{};
        spec.velocity = 100;
        if (!noteFields.getInt("pitch", spec.pitch) || !getTicks(noteFields, "start", spec.startTick) ||
            !getTicks(noteFields, "length", spec.lengthTicks)) {
            return false;
        }
        if (noteFields.has("velocity") && !noteFields.getInt("velocity", spec.velocity)) {
            return false;
        }
        notes.push_back(spec);
    }
    return parseBaseRevision(fields, out);
}

/// `updateNotes {trackId, notes:[{id, pitch?, start?, length?, velocity?}], baseRevision?}`: as `insertNotes`, with
/// `id` required and every other field optional (absent keeps the note's value; null is an error).
bool parseUpdateNotes(Fields& fields, Command& out, std::vector<NoteEdit>& edits, std::string& error) {
    out.kind = Command::Kind::UpdateNotes;
    if (!fields.getInt("trackId", out.id)) {
        return false;
    }
    const json* list = fields.getArray("notes");
    if (list == nullptr) {
        return false;
    }
    if (list->size() > static_cast<std::size_t>(kMaxNotesPerTrack)) {
        return fields.fail("notes", "must hold at most " + std::to_string(kMaxNotesPerTrack) + " notes");
    }
    edits.clear();
    edits.reserve(list->size());
    for (std::size_t i = 0; i < list->size(); ++i) {
        const std::string where = "notes[" + std::to_string(i) + "]";
        const json& item = (*list)[i];
        if (!item.is_object()) {
            error = where + " must be an object";
            return false;
        }
        Fields noteFields(item, where, error);
        NoteEdit edit{};
        if (!noteFields.getInt("id", edit.id) || !getOptionalInt(noteFields, "pitch", edit.pitch) ||
            !getOptionalTicks(noteFields, "start", edit.startTick) ||
            !getOptionalTicks(noteFields, "length", edit.lengthTicks) ||
            !getOptionalInt(noteFields, "velocity", edit.velocity)) {
            return false;
        }
        edits.push_back(edit);
    }
    return parseBaseRevision(fields, out);
}

/// `removeNotes {trackId, ids, baseRevision?}`: `ids` an array of at most `kMaxNotesPerTrack` integers; empty is a
/// valid no-op, as the store's.
bool parseRemoveNotes(Fields& fields, Command& out, std::vector<int>& ids, std::string& error) {
    out.kind = Command::Kind::RemoveNotes;
    return fields.getInt("trackId", out.id) && parseIds(fields, true, ids, error, static_cast<std::size_t>(kMaxNotesPerTrack)) &&
           parseBaseRevision(fields, out);
}

/// `generateBass {trackId, pattern, scope?}`: `pattern` one of the `bassPatternName` names (missing is "missing
/// \"pattern\"", anything else one message listing the six); `scope` absent, or an array of at most `kMaxChords`
/// integers read as `ids` are (absent or empty: every chord), into `ids`. No `baseRevision`: not a drag.
bool parseGenerateBass(Fields& fields, Command& out, std::vector<int>& ids, std::string& error) {
    out.kind = Command::Kind::GenerateBass;
    if (!fields.getInt("trackId", out.id)) {
        return false;
    }
    const json* pattern = fields.require("pattern");
    if (pattern == nullptr) {
        return false;
    }
    const std::optional<BassPattern> named =
        pattern->is_string() ? bassPatternFromName(pattern->get_ref<const std::string&>()) : std::nullopt;
    if (!named) {
        std::string expected = "must be ";
        for (int i = 0; i < kBassPatternCount; ++i) {
            expected += std::string(i == 0 ? "" : (i + 1 == kBassPatternCount ? " or " : ", ")) + "\"" +
                        bassPatternName(static_cast<BassPattern>(i)) + "\"";
        }
        return fields.fail("pattern", expected);
    }
    out.bassPattern = *named;
    ids.clear();
    return !fields.has("scope") ||
           parseIds(fields, true, ids, error, static_cast<std::size_t>(kMaxChords), "scope");
}

/// `generateLead {trackId, pattern, scope?, variation?}`: `pattern` one of the `leadPatternName` names (missing is
/// "missing \"pattern\"", anything else one message listing the six); `scope` as `generateBass`'s; `variation` absent
/// (0) or a JSON integer in 0..INT_MAX, read at 64 bits before narrowing (a negative, fractional, boolean, null,
/// string or larger value is an error naming the field). Whether a non-zero variation suits the pattern is the store's
/// call (`DocumentStore::generateLead` refuses it for every pattern but Motif). No `baseRevision`: not a drag.
bool parseGenerateLead(Fields& fields, Command& out, std::vector<int>& ids, std::string& error) {
    out.kind = Command::Kind::GenerateLead;
    if (!fields.getInt("trackId", out.id)) {
        return false;
    }
    const json* pattern = fields.require("pattern");
    if (pattern == nullptr) {
        return false;
    }
    const std::optional<LeadPattern> named =
        pattern->is_string() ? leadPatternFromName(pattern->get_ref<const std::string&>()) : std::nullopt;
    if (!named) {
        std::string expected = "must be ";
        for (int i = 0; i < kLeadPatternCount; ++i) {
            expected += std::string(i == 0 ? "" : (i + 1 == kLeadPatternCount ? " or " : ", ")) + "\"" +
                        leadPatternName(static_cast<LeadPattern>(i)) + "\"";
        }
        return fields.fail("pattern", expected);
    }
    out.leadPattern = *named;
    out.variation = 0;
    if (fields.has("variation")) {
        std::int64_t variation = 0;
        if (!fields.getInt64In("variation", 0, INT_MAX, variation)) {
            return false;
        }
        out.variation = static_cast<int>(variation);  // in range: checked at 64 bits above
    }
    ids.clear();
    return !fields.has("scope") ||
           parseIds(fields, true, ids, error, static_cast<std::size_t>(kMaxChords), "scope");
}

/// `selectNotes {trackId, ids}`: `trackId` an integer, or null for no track (null, like `kNoTrackId` = -1, clears the
/// selection, as `selectEvent`); `ids` an array of at most `kMaxNotesPerTrack` integers, empty clears.
bool parseSelectNotes(Fields& fields, Command& out, std::vector<int>& ids, std::string& error) {
    out.kind = Command::Kind::SelectNotes;
    const json* track = fields.require("trackId");
    if (track == nullptr) {
        return false;
    }
    if (track->is_null()) {
        out.id = kNoTrackId;
    } else if (!toInt(*track, out.id)) {
        return fields.fail("trackId", "must be an integer or null");
    }
    return parseIds(fields, true, ids, error, static_cast<std::size_t>(kMaxNotesPerTrack));
}

/// `setNewChordLength {lengthBeats}`: a finite number, or null for one bar of the meter; the field is required.
bool parseSetNewChordLength(Fields& fields, Command& out) {
    out.kind = Command::Kind::SetNewChordLength;
    const json* length = fields.find("lengthBeats");
    if (length == nullptr) {
        return fields.fail("lengthBeats", "is missing; it must be a finite number or null");
    }
    if (length->is_null()) {
        out.hasLength = false;
        return true;
    }
    out.hasLength = true;
    return fields.getNumber("lengthBeats", out.lengthBeats);
}

/// `loadPreset {id, mode}`: `id` names a record of `allPresets()`, `mode` is "append" or "replace".
bool parseLoadPreset(Fields& fields, Command& out) {
    out.kind = Command::Kind::LoadPreset;
    std::string id;
    if (!fields.getString("id", id)) {
        return false;
    }
    out.preset = findPreset(id);
    if (out.preset == nullptr) {
        return fields.fail("id", "\"" + id + "\" is not a preset");
    }
    return parseProgressionLoadField(fields, out.load);
}

// --- Readers ---------------------------------------------------------------------------
//
// Each reader runs in two phases, and the JSON tree must be gone before the store is touched: nlohmann's
// destructor is noexcept but allocates (json_value::destroy flattens the children onto a heap-allocated
// std::vector), so a json object still alive after a mutation could turn an allocation failure during
// its cleanup into std::terminate with the mutation already applied. Phase A parses and validates into
// plain data and returns with the tree destroyed; Phase B takes nothing but the store and that data.

/// Phase A of `applyCommandJson`: parses and validates `text` into `out`, a section name into `name` and the
/// lists of the plural commands into `lists`.
bool parseCommand(std::string_view text, Command& out, std::string& name, CommandLists& lists, std::string& error) {
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
    if (cmd == "setMidiThru") {
        return parseToggle(fields, Command::Kind::SetMidiThru, out);
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
    if (cmd == "setTransposeWithKey") {
        return parseToggle(fields, Command::Kind::SetTransposeWithKey, out);
    }
    if (cmd == "setKeyHintSeen") {
        return parseToggle(fields, Command::Kind::SetKeyHintSeen, out);
    }
    if (cmd == "setFeel") {
        return parseSetFeel(fields, out);
    }
    if (cmd == "setMeter") {
        return parseSetMeter(fields, out);
    }
    if (cmd == "transposeFromKey") {
        return parseTransposeFromKey(fields, out);
    }
    if (cmd == "transposeProgression") {
        return parseTransposeProgression(fields, out);
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
    if (cmd == "selectEvents") {
        return parseSelectEvents(fields, out, lists.ids, error);
    }
    if (cmd == "insertChords") {
        return parseInsertChords(fields, out, lists.chords, error);
    }
    if (cmd == "removeChords") {
        out.kind = Command::Kind::RemoveChords;
        return parseIds(fields, false, lists.ids, error);
    }
    if (cmd == "setLengths") {
        out.kind = Command::Kind::SetLengths;
        return parseIds(fields, false, lists.ids, error) && fields.getNumber("lengthBeats", out.lengthBeats);
    }
    if (cmd == "setNewChordLength") {
        return parseSetNewChordLength(fields, out);
    }
    if (cmd == "setRelationMode") {
        out.kind = Command::Kind::SetRelationMode;
        return parseRelationModeField(fields, "mode", out.relationMode);
    }
    if (cmd == "loadPreset") {
        return parseLoadPreset(fields, out);
    }
    if (cmd == "addTrack") {
        return parseAddTrack(fields, out, name);
    }
    if (cmd == "removeTrack") {
        return parseIdOnly(fields, Command::Kind::RemoveTrack, out);
    }
    if (cmd == "patchTrack") {
        return parsePatchTrack(fields, out, name);
    }
    if (cmd == "moveTrack") {
        out.kind = Command::Kind::MoveTrack;
        return fields.getInt("id", out.id) && fields.getInt("index", out.index);
    }
    if (cmd == "setTrackMute") {
        return parseTrackToggle(fields, Command::Kind::SetTrackMute, out);
    }
    if (cmd == "setTrackSolo") {
        return parseTrackToggle(fields, Command::Kind::SetTrackSolo, out);
    }
    if (cmd == "setChordsMute") {
        return parseToggle(fields, Command::Kind::SetChordsMute, out);
    }
    if (cmd == "setChordsSolo") {
        return parseToggle(fields, Command::Kind::SetChordsSolo, out);
    }
    if (cmd == "insertNotes") {
        return parseInsertNotes(fields, out, lists.notes, error);
    }
    if (cmd == "removeNotes") {
        return parseRemoveNotes(fields, out, lists.ids, error);
    }
    if (cmd == "updateNotes") {
        return parseUpdateNotes(fields, out, lists.edits, error);
    }
    if (cmd == "generateBass") {
        return parseGenerateBass(fields, out, lists.ids, error);
    }
    if (cmd == "generateLead") {
        return parseGenerateLead(fields, out, lists.ids, error);
    }
    if (cmd == "selectNotes") {
        return parseSelectNotes(fields, out, lists.ids, error);
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
enum class Refusal {
    None,
    DocumentFull,
    IdsExhausted,
    InsertDocumentFull,
    InsertIdsExhausted,
    SectionIdsExhausted,
    PresetDocumentFull,
    PresetIdsExhausted,
    PresetSectionIdsExhausted,
    PresetInvalid,
    Store  ///< the store (or the command, for a stale `baseRevision` or an unknown track of a live flag) refused and gave its reason
};

/// The wire name of a command that `Refusal::Store` can refuse, for its error message.
const char* commandWireName(Command::Kind kind) {
    switch (kind) {
        case Command::Kind::AppendChord:
            return "appendChord";
        case Command::Kind::InsertChords:
            return "insertChords";
        case Command::Kind::MoveChord:
            return "moveChord";
        case Command::Kind::SetLength:
            return "setLength";
        case Command::Kind::SetLengths:
            return "setLengths";
        case Command::Kind::RemoveChord:
            return "removeChord";
        case Command::Kind::RemoveChords:
            return "removeChords";
        case Command::Kind::LoadPreset:
            return "loadPreset";
        case Command::Kind::AddTrack:
            return "addTrack";
        case Command::Kind::RemoveTrack:
            return "removeTrack";
        case Command::Kind::PatchTrack:
            return "patchTrack";
        case Command::Kind::MoveTrack:
            return "moveTrack";
        case Command::Kind::SetTrackMute:
            return "setTrackMute";
        case Command::Kind::SetTrackSolo:
            return "setTrackSolo";
        case Command::Kind::InsertNotes:
            return "insertNotes";
        case Command::Kind::RemoveNotes:
            return "removeNotes";
        case Command::Kind::UpdateNotes:
            return "updateNotes";
        case Command::Kind::GenerateBass:
            return "generateBass";
        case Command::Kind::GenerateLead:
            return "generateLead";
        default:
            break;
    }
    return "command";  // no other command reaches Refusal::Store
}

/// Classifies a store call that reports through the `std::string* refusal` convention (document.h): `ok` its
/// result, `reason` the string it filled. `Refusal::Store` when it failed with a reason (refused), else `None`
/// (changed, or failed without one: a valid no-op).
Refusal storeRefusal(bool ok, const std::string& reason) {
    return !ok && !reason.empty() ? Refusal::Store : Refusal::None;
}

/// True, with the drag message in `reason`, when `command` carries a `baseRevision` that is not the store's
/// revision: the gesture that produced the command was computed from an older document (tracks D4), so nothing is
/// applied. Checked before the store sees the command, so a stale command is this refusal whatever else is wrong with it.
bool staleBase(const DocumentStore& store, const Command& command, std::string& reason) {
    if (!command.hasBaseRevision || command.baseRevision == store.revision()) {
        return false;
    }
    reason = "The notes changed while you were dragging";
    return true;
}

/// `loadPreset`: resolves the preset in the project key and hands it to the store, which builds the
/// whole next document and commits it once; a refusal is the store's own reason
/// (`progressionRefusal`), asked before the call so that nothing has changed, or else the track invariant's
/// (`Refusal::Store`, the store's message in `reason`).
Refusal applyLoadPreset(DocumentStore& store, const Preset& preset, ProgressionLoad load, std::string& reason) {
    const std::vector<std::pair<Chord, double>> steps = resolvePreset(preset, store.doc().projectKey, store.doc().meter);
    switch (store.progressionRefusal(steps, load)) {
        case ProgressionRefusal::None:
            break;
        case ProgressionRefusal::DocumentFull:
            return Refusal::PresetDocumentFull;
        case ProgressionRefusal::EventIdsExhausted:
            return Refusal::PresetIdsExhausted;
        case ProgressionRefusal::SectionIdsExhausted:
            return Refusal::PresetSectionIdsExhausted;
        case ProgressionRefusal::Empty:
        case ProgressionRefusal::NonFiniteLength:
            return Refusal::PresetInvalid;  // not in the table (test_presets.cpp), kept total
    }
    const bool loaded = load == ProgressionLoad::Append ? store.appendProgression(steps, preset.name, &reason)
                                                        : store.replaceProgression(steps, preset.name, &reason);
    if (loaded) {
        return Refusal::None;
    }
    // The pre-checks passed, so only the track invariant can have refused (the notes of an append stay put and a
    // replace removes them all, so it never does); kept total.
    return reason.empty() ? Refusal::PresetInvalid : Refusal::Store;
}

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
/// strong guarantee. Takes nothing but the store, plain data, the section name and the lists, so no JSON
/// object can be alive here. Refuses an append or an insert, when the result would pass `kMaxChords`
/// chords or the store's id counter is exhausted, and a section add when the section id counter is (each
/// classified by a pre-check that mirrors the store's); a preset load for the store's `progressionRefusal`; and
/// any chord timing edit (append, insert, preset load, move, resize, removal) that the track invariant refuses
/// (`Refusal::Store`, with the store's message in `reason`). Either way nothing changes.
Refusal applyCommand(DocumentStore& store, const Command& command, std::string& name, const CommandLists& lists,
                     std::string& reason) {
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
            if (store.nextId() >= INT_MAX) {
                return Refusal::IdsExhausted;  // the store's own id check, asked first
            }
            // No length: the document's new-chord length (D3; one bar of the meter until one is chosen). The length
            // is finite (validated), so only the track invariant can still refuse.
            return store.appendChord(command.chord,
                                     command.hasLength ? std::optional<double>(command.lengthBeats) : std::nullopt,
                                     command.velocity,
                                     command.hasInversion ? std::optional<int>(command.inversion) : std::nullopt,
                                     &reason) != kNoEventId
                       ? Refusal::None
                       : Refusal::Store;
        case Command::Kind::MoveChord:
            return storeRefusal(store.moveChord(command.id, command.index, &reason), reason);
        case Command::Kind::SetLength:
            return storeRefusal(store.setLength(command.id, command.lengthBeats, &reason), reason);
        case Command::Kind::SetVelocity:
            store.setVelocity(command.id, command.velocity);
            return Refusal::None;
        case Command::Kind::RemoveChord:
            // An unknown id is a valid no-op.
            return storeRefusal(store.removeChord(command.id, &reason), reason);
        case Command::Kind::SetProgressionLoop:
            store.setProgressionLoop(command.on);
            return Refusal::None;
        case Command::Kind::SetFollowHost:
            store.setFollowHost(command.on);
            return Refusal::None;
        case Command::Kind::SetMidiThru:
            store.setMidiThru(command.on);
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
        case Command::Kind::SetTransposeWithKey:
            store.setTransposeWithKey(command.on);
            return Refusal::None;
        case Command::Kind::SetKeyHintSeen:
            store.setKeyHintSeen(command.on);
            return Refusal::None;
        case Command::Kind::SetFeel:
            store.setFeel(command.feel);
            return Refusal::None;
        case Command::Kind::SetMeter:
            store.setMeter(command.meter);
            return Refusal::None;
        case Command::Kind::TransposeFromKey:
            store.transposeFromKey(static_cast<PitchClass>(command.semitones));
            return Refusal::None;  // a move of nothing is a valid no-op
        case Command::Kind::TransposeProgression:
            store.transposeProgression(command.semitones);
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
        case Command::Kind::SelectEvents:
            store.setSelectedEvents(lists.ids, command.id);
            return Refusal::None;
        case Command::Kind::InsertChords:
            if (lists.chords.size() > static_cast<std::size_t>(kMaxChords) - doc.events.size()) {
                return Refusal::InsertDocumentFull;
            }
            if (store.nextId() > INT_MAX - static_cast<int>(lists.chords.size())) {  // <= kMaxChords: no overflow
                return Refusal::InsertIdsExhausted;  // the store's own id check, asked first
            }
            // Lengths are finite (validated), the batch fits and the ids suffice. The store can still refuse: a lane
            // that matches more than one track, a new track past `kMaxTracks`, track or note ids exhausted, and the
            // track invariant (a note pushed along leaves the note range (D1), or a pushed fractional chord gains a
            // rounded tick and reactivates a note over a cap).
            return store.insertChords(command.index, lists.chords, &reason).empty() ? Refusal::Store : Refusal::None;
        case Command::Kind::RemoveChords:
            // No id that exists is a valid no-op.
            return storeRefusal(store.removeChords(lists.ids, &reason), reason);
        case Command::Kind::SetLengths:
            return storeRefusal(store.setLengths(lists.ids, command.lengthBeats, &reason), reason);
        case Command::Kind::SetNewChordLength:
            store.setNewChordLength(command.hasLength ? std::optional<double>(command.lengthBeats) : std::nullopt);
            return Refusal::None;
        case Command::Kind::SetRelationMode:
            store.setRelationMode(command.relationMode);
            return Refusal::None;
        case Command::Kind::LoadPreset:
            return applyLoadPreset(store, *command.preset, command.load, reason);
        case Command::Kind::AddTrack:
            return store.addTrack(command.role, command.hasName ? std::optional<std::string>(std::move(name)) : std::nullopt,
                                  &reason) != kNoTrackId
                       ? Refusal::None
                       : Refusal::Store;
        case Command::Kind::RemoveTrack:
            return storeRefusal(store.removeTrack(command.id, &reason), reason);
        case Command::Kind::PatchTrack:
            return storeRefusal(store.patchTrack(command.id,
                                                 command.hasName ? std::optional<std::string>(std::move(name)) : std::nullopt,
                                                 command.hasRole ? std::optional<TrackRole>(command.role) : std::nullopt,
                                                 &reason),
                                reason);
        case Command::Kind::MoveTrack:
            return storeRefusal(store.moveTrack(command.id, command.index, &reason), reason);
        case Command::Kind::SetTrackMute:
        case Command::Kind::SetTrackSolo:
            // The store cannot tell an unknown id from a value already held (both false), so the command asks first:
            // an unknown track is an error, the value already held a valid no-op.
            if (store.findTrackIndex(command.id) < 0) {
                reason = "No track with id " + std::to_string(command.id);
                return Refusal::Store;
            }
            if (command.kind == Command::Kind::SetTrackMute) {
                store.setTrackMute(command.id, command.on);
            } else {
                store.setTrackSolo(command.id, command.on);
            }
            return Refusal::None;
        case Command::Kind::SetChordsMute:
            store.setChordsMute(command.on);
            return Refusal::None;
        case Command::Kind::SetChordsSolo:
            store.setChordsSolo(command.on);
            return Refusal::None;
        case Command::Kind::InsertNotes:
            if (staleBase(store, command, reason)) {
                return Refusal::Store;
            }
            return store.insertNotes(command.id, lists.notes, &reason).empty() ? Refusal::Store : Refusal::None;
        case Command::Kind::RemoveNotes:
            if (staleBase(store, command, reason)) {
                return Refusal::Store;
            }
            return storeRefusal(store.removeNotes(command.id, lists.ids, &reason), reason);
        case Command::Kind::UpdateNotes:
            if (staleBase(store, command, reason)) {
                return Refusal::Store;
            }
            return storeRefusal(store.updateNotes(command.id, lists.edits, &reason), reason);
        case Command::Kind::GenerateBass:
            return storeRefusal(store.generateBass(command.id, command.bassPattern, lists.ids, &reason), reason);
        case Command::Kind::GenerateLead:
            return storeRefusal(
                store.generateLead(command.id, command.leadPattern, lists.ids, command.variation, &reason), reason);
        case Command::Kind::SelectNotes:
            store.setNoteSelection(command.id, lists.ids);  // live state; an unknown track is a no-op, as selectEvent
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

/// A state note's tick field (`offset`, `length`): read at 64 bits over the whole of `int32_t` (`getInt64In`, so 2^63
/// or a fraction is an error, never a wrapped value) and only then narrowed. The domain is `validateTracks`' to check.
bool getStateTicks(Fields& fields, const char* name, std::int32_t& out) {
    std::int64_t value = 0;
    if (!fields.getInt64In(name, std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max(),
                           value)) {
        return false;
    }
    out = static_cast<std::int32_t>(value);
    return true;
}

/// Reads the optional `tracks` array of a state into `doc.tracks` (tracks design D7): each entry needs `id` (1..INT_MAX-1,
/// so a derived counter cannot overflow), `name`, `role`, `mute`, `solo` and `notes`, each note `id` (1..INT_MAX-1),
/// `pitch`, `anchor`, `offset`, `length` and `velocity`. Types only: nothing is clamped, and the invariant (anchors,
/// domain, ids, order, names, counts, playback limits) is `validateTracks`', run by `parseState` on the whole.
/// `maxTrackId` / `maxNoteId` are the largest ids read (0 without any).
bool parseTracks(Fields& fields, Document& doc, int& maxTrackId, int& maxNoteId, std::string& error) {
    maxTrackId = 0;
    maxNoteId = 0;
    if (!fields.has("tracks")) {
        return true;
    }
    const json* tracks = fields.getArray("tracks");
    if (tracks == nullptr) {
        return false;
    }
    for (std::size_t i = 0; i < tracks->size(); ++i) {
        const std::string where = "tracks[" + std::to_string(i) + "]";
        const json& trackObject = (*tracks)[i];
        if (!trackObject.is_object()) {
            error = where + " must be an object";
            return false;
        }
        Fields trackFields(trackObject, where, error);
        Track track{0, std::string(), TrackRole::Other, false, false, {}};
        if (!trackFields.getIntIn("id", 1, INT_MAX - 1, track.id) || !trackFields.getString("name", track.name) ||
            !parseRoleField(trackFields, "role", track.role) || !trackFields.getBool("mute", track.mute) ||
            !trackFields.getBool("solo", track.solo)) {
            return false;
        }
        const json* notes = trackFields.getArray("notes");
        if (notes == nullptr) {
            return false;
        }
        track.notes.reserve(notes->size());
        for (std::size_t j = 0; j < notes->size(); ++j) {
            const std::string noteWhere = trackFields.label("notes") + "[" + std::to_string(j) + "]";
            const json& noteObject = (*notes)[j];
            if (!noteObject.is_object()) {
                error = noteWhere + " must be an object";
                return false;
            }
            Fields noteFields(noteObject, noteWhere, error);
            TrackNote note{0, 0, 0, 0, 0, 0};
            if (!noteFields.getIntIn("id", 1, INT_MAX - 1, note.id) || !noteFields.getInt("pitch", note.pitch) ||
                !noteFields.getInt("anchor", note.anchorEventId) || !getStateTicks(noteFields, "offset", note.offsetTick) ||
                !getStateTicks(noteFields, "length", note.lengthTicks) || !noteFields.getInt("velocity", note.velocity)) {
                return false;
            }
            maxNoteId = std::max(maxNoteId, note.id);
            track.notes.push_back(note);
        }
        maxTrackId = std::max(maxTrackId, track.id);
        doc.tracks.push_back(std::move(track));
    }
    return true;
}

/// Reads the optional id counter `name` of a state: absent leaves `out` 0 (`replace` derives one past the largest
/// id); present it must be an integer in 1..INT_MAX greater than `maxId`, as `nextSectionId` (`what` names the ids in
/// the message: "track", "note").
bool parseCounter(Fields& fields, const char* name, int maxId, const char* what, int& out) {
    out = 0;
    if (!fields.has(name)) {
        return true;
    }
    if (!fields.getIntIn(name, 1, INT_MAX, out)) {
        return false;
    }
    if (out <= maxId) {
        return fields.fail(name, std::string("must exceed every ") + what + " id (" + std::to_string(maxId) + ")");
    }
    return true;
}

/// Phase A of `loadStateJson`: parses and validates `text` into a document, the four id counters and an
/// editor size. `Document` holds vectors and strings, whose destruction frees but never allocates.
/// `nextSectionId`, `nextTrackId` and `nextNoteId` are 0 when the state carries none: `replace` then derives them.
bool parseState(std::string_view text, Document& doc, int& nextId, int& nextSectionId, int& nextTrackId,
                int& nextNoteId, int& width, int& height, std::string& error) {
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
    // Optional (states saved before the key transpose switch have neither): false.
    if (fields.has("transposeWithKey") && !fields.getBool("transposeWithKey", doc.transposeWithKey)) {
        return false;
    }
    if (fields.has("keyHintSeen") && !fields.getBool("keyHintSeen", doc.keyHintSeen)) {
        return false;
    }
    // Optional (states saved before MIDI Thru have none): true.
    if (fields.has("midiThru") && !fields.getBool("midiThru", doc.midiThru)) {
        return false;
    }
    // Optional (states saved before the new-chord length have none): null, one bar of the meter. A number is
    // snapped and clamped as `setNewChordLength` stores it; anything else rejects the state.
    if (fields.has("newChordBeats") && !fields.find("newChordBeats")->is_null()) {
        double beats = 0.0;
        if (!fields.getNumber("newChordBeats", beats)) {
            return false;
        }
        doc.newChordBeats = snapNewChordBeats(beats);
    }
    // Optional (states saved before the feel have neither): block chords in 4/4. Present means valid, or the
    // whole state is rejected.
    if (fields.has("feel")) {
        const json* feelObject = fields.getObject("feel");
        if (feelObject == nullptr) {
            return false;
        }
        Fields feelFields(*feelObject, fields.label("feel"), error);
        if (!parseFeelFields(feelFields, doc.feel)) {
            return false;
        }
    }
    if (fields.has("meter")) {
        const json* meterObject = fields.getObject("meter");
        if (meterObject == nullptr) {
            return false;
        }
        Fields meterFields(*meterObject, fields.label("meter"), error);
        if (!parseMeterFields(meterFields, doc.meter)) {
            return false;
        }
    }
    // Optional (states saved before the relation modes have none): the default, blend.
    if (fields.has("relationMode") && !parseRelationModeField(fields, "relationMode", doc.relationMode)) {
        return false;
    }
    // Optional (states saved before tracks have neither): the chords lane neither muted nor soloed.
    if ((fields.has("chordsMuted") && !fields.getBool("chordsMuted", doc.chordsMuted)) ||
        (fields.has("chordsSolo") && !fields.getBool("chordsSolo", doc.chordsSolo))) {
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

    // Tracks (optional: states saved before tracks have none) after the chords, whose ids the anchors name and whose
    // clamped lengths place the notes. Read for types only; the invariant runs on the whole candidate below.
    int maxTrackId = 0;
    int maxNoteId = 0;
    if (!parseTracks(fields, doc, maxTrackId, maxNoteId, error) ||
        !parseCounter(fields, "nextTrackId", maxTrackId, "track", nextTrackId) ||
        !parseCounter(fields, "nextNoteId", maxNoteId, "note", nextNoteId)) {
        return false;
    }
    // The track invariant (D7) with the counters `replace` will publish: given, or one past the largest id (the ids
    // are below INT_MAX, so neither overflows). A failure rejects the whole state; nothing of a track is clamped.
    std::string reason;
    if (!validateTracks(doc, nextTrackId != 0 ? nextTrackId : maxTrackId + 1, nextNoteId != 0 ? nextNoteId : maxNoteId + 1,
                        reason)) {
        error = "\"tracks\" rejected: " + reason;
        return false;
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

std::string heardJson(const Listener& listener, const Key& projectKey) {
    const std::vector<int> held = listener.held();
    const std::optional<HeardChord> heard = listener.chord();
    const bool chordHeld = listener.chordHeld();

    json names = json::array();
    json pitches = json::array();
    for (const int pitch : held) {
        const PitchClass pc = static_cast<PitchClass>(pitch % 12);
        bool chordTone = false;
        if (heard && chordHeld) {
            for (const PitchClass tone : pitchClasses(heard->chord)) {
                chordTone = chordTone || tone == pc;
            }
        }
        names.push_back((chordTone ? chordToneSpelling(heard->chord, pc, projectKey) : spell(pc, projectKey)).text());
        pitches.push_back(pitch);
    }

    json chord;  // null
    if (heard) {
        chord = {{"root", heard->chord.root},
                 {"type", heard->chord.type->id},
                 {"symbol", chordSymbol(heard->chord, projectKey)},
                 {"roman", romanNumeral(heard->chord, projectKey)},
                 {"inversion", heard->inversion},
                 {"bass", heard->bass}};
    }

    json key;  // null
    if (const std::optional<HeardKey> heardKey = listener.key()) {
        key = keyToJson(heardKey->key);  // {"tonic":{"letter","alter"},"mode"}
        key["name"] = keyName(heardKey->key);
        key["percent"] = std::clamp(static_cast<int>(std::lround(100.0 * heardKey->r)), 0, 100);
    }

    return json{{"schema", kJsonSchema},
                {"active", listener.active()},
                {"held", std::move(pitches)},
                {"heldNames", std::move(names)},
                {"chord", std::move(chord)},
                {"chordHeld", chordHeld},
                {"key", std::move(key)},
                {"onsets", listener.onsetsInWindow()}}
        .dump();
}

static_assert(kNoEventId == -1, "snapshotJson's default selectedEventId must be kNoEventId");

std::string snapshotJson(const Document& doc, const Palette& palette, int selectedEventId,
                         const std::vector<int>& selectedEventIds, const SnapshotExtras& extras) {
    // The selection and its anchor, normalised together (strip editing design D2): the given ids that name an
    // event, in strip order; when none does, the anchor alone if it names one. A non-empty selection whose
    // anchor is absent, unknown or not one of its blocks takes its last block as the anchor. The suggestions and
    // the relations follow that anchor, so they never point outside the selection.
    std::vector<int> selection;
    {
        const std::set<int> wanted(selectedEventIds.begin(), selectedEventIds.end());
        for (const ChordEvent& e : doc.events) {
            if (wanted.count(e.id) != 0) {
                selection.push_back(e.id);
            }
        }
    }
    const bool anchorNamesEvent = std::any_of(doc.events.begin(), doc.events.end(),
                                              [&](const ChordEvent& e) { return e.id == selectedEventId; });
    if (selection.empty() && anchorNamesEvent) {
        selection.push_back(selectedEventId);
    }
    const int anchor = std::find(selection.begin(), selection.end(), selectedEventId) != selection.end()
                           ? selectedEventId
                           : (selection.empty() ? kNoEventId : selection.back());
    int selectedIndex = -1;
    for (std::size_t i = 0; i < doc.events.size(); ++i) {
        if (doc.events[i].id == anchor) {
            selectedIndex = static_cast<int>(i);
            break;
        }
    }

    const std::vector<std::vector<int>> compiledVoicings = voicings(doc);
    // The playback period and the budget verdict come from the one compile playback itself runs: with a
    // performing feel the period is the last chord's end tick / 960 (tick-rounded), otherwise, and in the
    // over-budget fallback, the authored sum. Event positions below stay authored.
    const Compiled compiled = compile(doc);
    // Each chord's playback span, from the same compile's verdict (tracks design D8).
    const std::vector<PlaySpan> spans = chordPlaySpans(doc, compiled);
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
                      {"voicingNames", voicingNamesJson(e.chord, compiledVoicings[i], doc.projectKey)},
                      {"outOfKey", outOfKeyJson(e.chord, doc.projectKey)},
                      {"outOfKeyNames", outOfKeyNamesJson(e.chord, doc.projectKey, doc.projectKey)},
                      {"why", i == 0 ? json() : whyJson(doc.events[i - 1].chord, e.chord, doc.projectKey)},
                      {"playStart", spans[i].start},
                      {"playEnd", spans[i].end},
                      {"guide", guideJson(e.chord, doc.projectKey)}};
        events.push_back(std::move(event));
        beat += e.lengthBeats;
    }

    // Suggestions and relations follow the selected chord, else the last one; with neither, suggestions
    // open the progression and there is no relation reference.
    const int referenceIndex = selectedIndex >= 0 ? selectedIndex : static_cast<int>(doc.events.size()) - 1;
    const ChordEvent* reference = referenceIndex >= 0 ? &doc.events[static_cast<std::size_t>(referenceIndex)] : nullptr;
    const Chord* previous = reference != nullptr ? &reference->chord : nullptr;

    // Relatedness of every sized cell, palette then home row, in one call so that blend normalises over both;
    // the applied chords are sized on that scale as extras, without entering the range.
    const std::vector<PaletteCell> home = homeTriadCells(doc);
    const std::vector<AppliedChord> applied = appliedChords(doc.browsedKey);
    const std::vector<PaletteCell> appliedCellList = appliedCells(doc, applied);
    std::vector<Relatedness> relations;
    json relationReference;  // null
    if (reference != nullptr && doc.relationMode != RelationMode::Off) {
        std::vector<Chord> sized;
        sized.reserve(palette.cells.size() + home.size());
        for (const PaletteCell& c : palette.cells) {
            sized.push_back(c.chord);
        }
        for (const PaletteCell& c : home) {
            sized.push_back(c.chord);
        }
        std::vector<Chord> extras;
        extras.reserve(applied.size());
        for (const AppliedChord& a : applied) {
            extras.push_back(a.chord);
        }
        relations = relatedness(reference->chord, sized, extras, doc.projectKey, doc.browsedKey, doc.relationMode);
        relationReference = {{"eventId", reference->id}, {"symbol", chordSymbol(reference->chord, doc.projectKey)}};
    }
    const Relatedness* cellRelations = relations.empty() ? nullptr : relations.data();
    const Relatedness* homeRelations = relations.empty() ? nullptr : relations.data() + palette.cells.size();
    const Relatedness* appliedRelations =
        relations.empty() ? nullptr : relations.data() + palette.cells.size() + home.size();

    json cells = json::array();
    for (std::size_t i = 0; i < palette.cells.size(); ++i) {
        cells.push_back(cellJson(palette.cells[i], doc.projectKey, doc.browsedKey,
                                 cellRelations != nullptr ? &cellRelations[i] : nullptr));
    }

    const json snapshot = {{"schema", kJsonSchema},
                           {"feel", feelToJson(doc.feel)},
                           {"meter", meterToJson(doc.meter)},
                           {"feelLimited", compiled.feelLimited},
                           {"projectKey", keyToJson(doc.projectKey)},
                           {"browsedKey", keyToJson(doc.browsedKey)},
                           {"projectKeyName", keyName(doc.projectKey)},
                           {"browsedKeyName", keyName(doc.browsedKey)},
                           {"progressionLoop", doc.progressionLoop},
                           {"followHost", doc.followHost},
                           {"autoVoiceLead", doc.autoVoiceLead},
                           {"transposeWithKey", doc.transposeWithKey},
                           {"keyHintSeen", doc.keyHintSeen},
                           {"midiThru", doc.midiThru},
                           {"endBeat", compiled.endBeat},
                           {"selectedEventId", selectedIndex >= 0 ? json(anchor) : json()},
                           {"selectedEventIds", selection},
                           {"newChordBeats", doc.newChordBeats ? json(*doc.newChordBeats) : json()},
                           {"modes", modesJson()},
                           {"tonics", tonicsJson()},
                           {"events", std::move(events)},
                           {"sections", sectionsJson(doc)},
                           {"suggestions", suggestionsJson(previous, doc)},
                           {"homeTriads", homeTriadsJson(home, doc.projectKey, homeRelations)},
                           {"relationMode", relationModeName(doc.relationMode)},
                           {"relationReference", std::move(relationReference)},
                           {"presets", presetsJson(doc.projectKey)},
                           {"applied", appliedJson(doc, applied, appliedCellList, appliedRelations)},
                           {"palette", {{"tiers", palette.tiers}, {"cells", std::move(cells)}}},
                           {"ticksPerBeat", kFeelTicksPerBeat},
                           {"revision", extras.revision},
                           {"tracks", snapshotTracksJson(doc)},
                           {"chordsMuted", doc.chordsMuted},
                           {"chordsSolo", doc.chordsSolo},
                           {"chordsAudible", chordsAudible(doc)},
                           {"noteSelection", noteSelectionJson(doc, extras.noteSelection)},
                           {"trackLimits", trackLimitsJson()}};
    return snapshot.dump();
}

namespace {

/// The state's `tracks` (tracks design D7): every track with its live flags, each note as stored (anchor and offset,
/// no derived start), in the track's note order.
json stateTracksJson(const Document& doc) {
    json tracks = json::array();
    for (const Track& t : doc.tracks) {
        json notes = json::array();
        for (const TrackNote& n : t.notes) {
            notes.push_back({{"id", n.id},
                             {"pitch", n.pitch},
                             {"anchor", n.anchorEventId},
                             {"offset", n.offsetTick},
                             {"length", n.lengthTicks},
                             {"velocity", n.velocity}});
        }
        tracks.push_back({{"id", t.id},
                          {"name", t.name},
                          {"role", trackRoleName(t.role)},
                          {"mute", t.mute},
                          {"solo", t.solo},
                          {"notes", std::move(notes)}});
    }
    return tracks;
}

}  // namespace

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
                        {"transposeWithKey", doc.transposeWithKey},
                        {"keyHintSeen", doc.keyHintSeen},
                        {"midiThru", doc.midiThru},
                        {"newChordBeats", doc.newChordBeats ? json(*doc.newChordBeats) : json()},
                        {"feel", feelToJson(doc.feel)},
                        {"meter", meterToJson(doc.meter)},
                        {"relationMode", relationModeName(doc.relationMode)},
                        {"events", std::move(events)},
                        {"sections", sectionsJson(doc)},
                        {"tracks", stateTracksJson(doc)},
                        {"chordsMuted", doc.chordsMuted},
                        {"chordsSolo", doc.chordsSolo},
                        {"nextId", store.nextId()},
                        {"nextSectionId", store.nextSectionId()},
                        {"nextTrackId", store.nextTrackId()},
                        {"nextNoteId", store.nextNoteId()},
                        {"editor", {{"width", editorWidth}, {"height", editorHeight}}}};
    return state.dump();
}

bool applyCommandJson(DocumentStore& store, std::string_view text, std::string& error,
                      bool* documentChanged) noexcept {
    if (documentChanged != nullptr) {
        *documentChanged = false;
    }
    try {
        error.clear();
        Command command;
        std::string name;  // a section name; its destructor frees, never allocates
        CommandLists lists;  // the plural commands' chords and ids; freed, never allocated, on return
        if (!parseCommand(text, command, name, lists, error)) {  // Phase A: the JSON tree is destroyed on return
            return false;
        }
        // The document as it was, for `documentChanged`: a copy taken before the apply (so a failed
        // allocation here changes nothing) and compared after it with `operator==`, which covers every
        // field. A refused command leaves the document as it was, so the flag stays false for one.
        const std::optional<Document> before =
            documentChanged != nullptr ? std::optional<Document>(store.doc()) : std::nullopt;
        // Phase B: only the store, plain data, the name and the store's refusal reason in scope.
        std::string reason;
        const Refusal refusal = applyCommand(store, command, name, lists, reason);
        if (before.has_value() && refusal == Refusal::None) {
            *documentChanged = store.doc() != *before;
        }
        switch (refusal) {
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
            case Refusal::InsertDocumentFull:
                error = "\"insertChords\" refused: the document would hold more than " + std::to_string(kMaxChords) +
                        " chords";
                return false;
            case Refusal::InsertIdsExhausted:
                error = "\"insertChords\" refused: not enough event ids left";
                return false;
            case Refusal::PresetDocumentFull:
                error = "\"loadPreset\" refused: the document would hold more than " + std::to_string(kMaxChords) +
                        " chords";
                return false;
            case Refusal::PresetIdsExhausted:
                error = "\"loadPreset\" refused: not enough event ids left";
                return false;
            case Refusal::PresetSectionIdsExhausted:
                error = "\"loadPreset\" refused: not enough section ids left";
                return false;
            case Refusal::PresetInvalid:
                error = "\"loadPreset\" refused: the preset has no loadable steps";
                return false;
            case Refusal::Store:
                error = std::string("\"") + commandWireName(command.kind) + "\" refused: " + reason;
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
        int nextTrackId = 0;
        int nextNoteId = 0;
        int width = kDefaultEditorWidth;
        int height = kDefaultEditorHeight;
        // Phase A: the tree is destroyed on return; the tracks have passed validateTracks with the counters below.
        if (!parseState(text, doc, nextId, nextSectionId, nextTrackId, nextNoteId, width, height, error)) {
            return false;
        }
        // Phase B: cannot throw, nor can the writes below
        store.replace(std::move(doc), nextId, nextSectionId, nextTrackId, nextNoteId);
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
