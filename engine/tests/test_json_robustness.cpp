// Deterministic mutation fuzz of the two JSON readers (applyCommandJson, loadStateJson; json_api.h).
//
// A fixed-seed xorshift64 mutates a corpus of valid messages (one per command, plus the state fixtures)
// and feeds every mutant to both readers. After every call the store must still satisfy the document
// invariants, a snapshot of it must still serialise and parse, an accepted call must have cleared `error`
// and a rejected one must have set it and left the store as it was. Nothing here uses <random> or any
// clock, so a failure reproduces from the iteration number printed with it.

#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/compiler.h"
#include "chordlab/document.h"
#include "chordlab/feel.h"
#include "chordlab/json_api.h"
#include "chordlab/meter.h"
#include "chordlab/palette.h"
#include "chordlab/pitch.h"
#include "chordlab/scale.h"
#include "chordlab/tracks.h"
#include "chordlab/voicing.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <optional>
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
                                       "64.00001", "1e308", "NaN",  "Infinity", "0x10",
                                       "9223372036854775807", "-9223372036854775808", "9223372036854775808",
                                       "-9223372036854775809", "1024", "4096", "960"};
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
    const std::vector<int>& selection = store.selectedEventIds();
    int previousIndex = -1;
    for (int id : selection) {
        const int index = store.findIndex(id);
        if (index < 0) {
            return "selected event id " + std::to_string(id) + " names no event";
        }
        if (index <= previousIndex) {
            return "selection not in strip order, or repeated";
        }
        previousIndex = index;
    }
    if (selection.empty() != (sel == kNoEventId) ||
        (sel != kNoEventId && std::find(selection.begin(), selection.end(), sel) == selection.end())) {
        return "the anchor is not one of the selected ids";
    }
    if (store.nextId() < 1 || store.nextSectionId() < 1) {
        return "nextId or nextSectionId below 1";
    }
    if (!validMeter(d.meter)) {
        return "invalid meter " + std::to_string(d.meter.num) + "/" + std::to_string(d.meter.den);
    }
    if (d.newChordBeats.has_value() &&
        (!std::isfinite(*d.newChordBeats) || *d.newChordBeats < kMinLengthBeats || *d.newChordBeats > kMaxLengthBeats ||
         std::fmod(*d.newChordBeats * 4.0, 1.0) != 0.0)) {
        return "newChordBeats off the quarter-beat grid or out of bounds";
    }
    if (static_cast<int>(d.feel.rhythm) < 0 || d.feel.rhythm > Rhythm::Arpeggio || static_cast<int>(d.feel.strum) < 0 ||
        d.feel.strum > Strum::DownUp || static_cast<int>(d.feel.speed) < 0 || d.feel.speed > StrumSpeed::Loose ||
        static_cast<int>(d.feel.humanise) < 0 || d.feel.humanise > Humanise::Heavy ||
        static_cast<int>(d.feel.pattern) < 0 || d.feel.pattern > ArpPattern::Random ||
        static_cast<int>(d.feel.rate) < 0 || d.feel.rate > ArpRate::Half || static_cast<int>(d.feel.range) < 0 ||
        d.feel.range > ArpRange::Two) {
        return "feel enum out of range";
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
    // The tracks: the one invariant of tracks.h with the counters the store holds, and a note selection that is a
    // duplicate-free subsequence of its track's notes, empty exactly when it names no track.
    std::string trackError;
    if (!validateTracks(d, store.nextTrackId(), store.nextNoteId(), trackError)) {
        return "track invariant broken: " + trackError;
    }
    const NoteSelection& noteSelection = store.noteSelection();
    if (noteSelection.ids.empty() != (noteSelection.trackId == kNoTrackId)) {
        return "the note selection is empty exactly when it names no track";
    }
    if (noteSelection.trackId != kNoTrackId) {
        const int trackIndex = store.findTrackIndex(noteSelection.trackId);
        if (trackIndex < 0) {
            return "the note selection names a track that does not exist";
        }
        const std::vector<TrackNote>& notes = d.tracks[static_cast<std::size_t>(trackIndex)].notes;
        std::size_t at = 0;
        for (const int id : noteSelection.ids) {
            while (at < notes.size() && notes[at].id != id) {
                ++at;
            }
            if (at == notes.size()) {
                return "the note selection is not a subsequence of the track's notes";
            }
            ++at;
        }
    }
    if (!snapshot) {
        return {};
    }
    try {
        const std::string snap =
            snapshotJson(d, buildPalette(d.browsedKey, d.projectKey), store.selectedEventId(), store.selectedEventIds());
        const json j = json::parse(snap);
        if (!j.is_object() || j.value("schema", 0) != kJsonSchema) {
            return "snapshot is not a schema-1 object";
        }
        if (j.at("meter") != json({{"num", d.meter.num}, {"den", d.meter.den}})) {
            return "snapshot meter disagrees with the document";
        }
        if (!j.at("feel").is_object() || j.at("feel").size() != 7 || !j.at("feelLimited").is_boolean() ||
            j.at("feelLimited").get<bool>() != compile(d).feelLimited) {
            return "snapshot feel or feelLimited disagrees with the document";
        }
        const json sId = j.at("selectedEventId");
        if (sel == kNoEventId ? !sId.is_null() : sId != sel) {
            return "snapshot selectedEventId disagrees with the store";
        }
        if (j.at("selectedEventIds") != json(selection)) {
            return "snapshot selectedEventIds disagrees with the store";
        }
        if (d.newChordBeats.has_value() ? j.at("newChordBeats") != *d.newChordBeats : !j.at("newChordBeats").is_null()) {
            return "snapshot newChordBeats disagrees with the document";
        }
        // The applied chords are sized exactly when there is a reference, on the palette's 0..100 scale.
        const bool sized = !j.at("relationReference").is_null();
        for (const json& cell : j.at("applied")) {
            if (cell.contains("relation") != sized) {
                return "applied cell relation disagrees with relationReference";
            }
            if (sized && (cell.at("relation").get<int>() < 0 || cell.at("relation").get<int>() > 100)) {
                return "applied cell relation outside 0..100";
            }
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
    std::vector<int> selection;
    int nextTrackId;
    int nextNoteId;
    std::uint64_t revision;
    NoteSelection noteSelection;
    explicit Observable(const DocumentStore& s)
        : doc(s.doc()), nextId(s.nextId()), nextSectionId(s.nextSectionId()), selected(s.selectedEventId()),
          selection(s.selectedEventIds()), nextTrackId(s.nextTrackId()), nextNoteId(s.nextNoteId()),
          revision(s.revision()), noteSelection(s.noteSelection()) {}
    bool operator==(const Observable& o) const {
        return doc == o.doc && nextId == o.nextId && nextSectionId == o.nextSectionId && selected == o.selected &&
               selection == o.selection && nextTrackId == o.nextTrackId && nextNoteId == o.nextNoteId &&
               revision == o.revision && noteSelection == o.noteSelection;
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

/// One valid message for every command name of json_api.h (three for browseKey, two each for selectEvent,
/// selectEvents, setNewChordLength, loadPreset, setFeel and setMeter), against ids 1..4 and section ids 1..2 of
/// `prefilledStore`.
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
        {{"cmd", "setTransposeWithKey"}, {"on", true}},
        {{"cmd", "setKeyHintSeen"}, {"on", true}},
        {{"cmd", "setMidiThru"}, {"on", false}},
        {{"cmd", "transposeFromKey"}, {"fromTonicPc", 7}},
        {{"cmd", "transposeProgression"}, {"semitones", -3}},
        {{"cmd", "addSection"}, {"index", 3}, {"name", "Bridge"}},
        {{"cmd", "renameSection"}, {"id", 2}, {"name", "Chorus \xCE\x94"}},
        {{"cmd", "setSectionColour"}, {"id", 1}, {"colourIndex", 5}},
        {{"cmd", "removeSection"}, {"id", 2}},
        {{"cmd", "moveSectionStart"}, {"id", 2}, {"index", 3}},
        {{"cmd", "selectEvent"}, {"id", 3}},
        {{"cmd", "selectEvent"}, {"id", nullptr}},
        {{"cmd", "setRelationMode"}, {"mode", "voiceLeading"}},
        {{"cmd", "loadPreset"}, {"id", "andalusian"}, {"mode", "append"}},
        {{"cmd", "loadPreset"}, {"id", "blues-12"}, {"mode", "replace"}},
        {{"cmd", "setFeel"}, {"rhythm", "lowThenUpper"}, {"strum", "downUp"}, {"strumSpeed", "loose"}, {"humanise", "heavy"}},
        {{"cmd", "setFeel"}, {"rhythm", "block"}, {"strum", "off"}, {"strumSpeed", "medium"}, {"humanise", "off"}},
        {{"cmd", "setMeter"}, {"num", 6}, {"den", 8}},
        {{"cmd", "setMeter"}, {"num", 4}, {"den", 4}},
        {{"cmd", "insertChords"}, {"index", 2},
         {"chords", json::array({json({{"root", 2}, {"type", "m7"}, {"lengthBeats", 0.75}, {"velocity", 90}, {"inversion", 1},
                                       {"octave", -1}, {"manualVoicing", true}}),
                                 json({{"root", 7}, {"type", "7"}})})}},
        {{"cmd", "removeChords"}, {"ids", json::array({4, 2, 99})}},
        {{"cmd", "setLengths"}, {"ids", json::array({1, 3})}, {"lengthBeats", 1.5}},
        {{"cmd", "selectEvents"}, {"ids", json::array({1, 2, 4})}, {"anchor", 2}},
        {{"cmd", "selectEvents"}, {"ids", json::array()}},
        {{"cmd", "setNewChordLength"}, {"lengthBeats", 0.5}},
        {{"cmd", "setNewChordLength"}, {"lengthBeats", nullptr}},
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

/// `prefilledStore` plus tracks: Bass (id 1) with notes 1..3 in the first three chords, Lead (id 2, named "Melody")
/// with note 4 in the second, an Other track (id 3) without notes, note 2 selected; `withRedo` leaves one undone step.
/// The notes sit far inside the polyphony and onset caps.
DocumentStore prefilledTrackStore(bool withRedo = false) {
    DocumentStore store = prefilledStore(false);
    store.addTrack(TrackRole::Bass);
    store.addTrack(TrackRole::Lead, std::string("Melody"));
    store.addTrack(TrackRole::Other);
    store.insertNotes(1, {NoteSpec{36, 100, 480}, NoteSpec{38, 3840 + 200, 960, 80}, NoteSpec{40, 7680 + 300, 480}});
    store.insertNotes(2, {NoteSpec{72, 3840, 960}});
    if (withRedo) {
        store.setVelocity(1, 77);
        store.undo();
    }
    store.setSelectedEvent(2);
    store.setNoteSelection(1, {2});
    return store;
}

/// One valid message per track and note command (and `insertChords` with notes), against `prefilledTrackStore(false)`:
/// `revision` is that store's revision, for the `baseRevision` entries (stale on a store with a redo step, which is
/// the refusal path).
std::vector<std::string> trackCommandCorpus(std::uint64_t revision) {
    const json bassLane = {{"trackId", 1}, {"name", "Bass"}, {"role", "bass"}};
    const std::vector<json> bodies = {
        {{"cmd", "addTrack"}, {"role", "lead"}},
        {{"cmd", "addTrack"}, {"role", "other"}, {"name", "Pad \xCE\x94"}},
        {{"cmd", "removeTrack"}, {"id", 2}},
        {{"cmd", "patchTrack"}, {"id", 3}, {"name", "Keys"}, {"role", "lead"}},
        {{"cmd", "patchTrack"}, {"id", 1}, {"role", "other"}},
        {{"cmd", "moveTrack"}, {"id", 3}, {"index", 0}},
        {{"cmd", "setTrackMute"}, {"id", 1}, {"on", true}},
        {{"cmd", "setTrackSolo"}, {"id", 2}, {"on", true}},
        {{"cmd", "setChordsMute"}, {"on", true}},
        {{"cmd", "setChordsSolo"}, {"on", true}},
        {{"cmd", "insertNotes"}, {"trackId", 3},
         {"notes", json::array({json({{"pitch", 60}, {"start", 0}, {"length", 480}}),
                                json({{"pitch", 64}, {"start", 4000}, {"length", 960}, {"velocity", 90}})})},
         {"baseRevision", revision}},
        {{"cmd", "insertNotes"}, {"trackId", 1},
         {"notes", json::array({json({{"pitch", 130}, {"start", 12000}, {"length", 99999999999}, {"velocity", 500}})})}},
        {{"cmd", "removeNotes"}, {"trackId", 1}, {"ids", json::array({3, 99, 3})}, {"baseRevision", revision}},
        {{"cmd", "removeNotes"}, {"trackId", 1}, {"ids", json::array()}},
        {{"cmd", "updateNotes"}, {"trackId", 1},
         {"notes", json::array({json({{"id", 1}, {"pitch", 50}, {"velocity", 30}}),
                                json({{"id", 2}, {"start", 7700}, {"length", 480}})})},
         {"baseRevision", revision}},
        {{"cmd", "selectNotes"}, {"trackId", 1}, {"ids", json::array({3, 1, 77})}},
        {{"cmd", "selectNotes"}, {"trackId", nullptr}, {"ids", json::array()}},
        {{"cmd", "insertChords"}, {"index", 1},
         {"chords", json::array({json({{"root", 0}, {"type", "maj"}, {"lengthBeats", 2.0},
                                       {"notes", json::array({json({{"lane", bassLane}, {"offset", 480}, {"length", 480}, {"pitch", 33}, {"velocity", 70}}),
                                                              json({{"lane", {{"name", "Melody"}, {"role", "lead"}}}, {"offset", 5000}, {"length", 480}, {"pitch", 76}}),
                                                              json({{"lane", {{"name", "Drone"}, {"role", "other"}}}, {"offset", 0}, {"length", 960}, {"pitch", 36}, {"velocity", 60}})})}}),
                                 json({{"root", 7}, {"type", "7"},
                                       {"notes", json::array({json({{"lane", {{"name", "Drone"}, {"role", "other"}}}, {"offset", 0}, {"length", 960}, {"pitch", 43}})})}})})}},
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
        bool changed = true;  // applyCommandJson sets it on every call
        const Document docBefore = cmdStore.doc();
        try {
            ok = applyCommandJson(cmdStore, text, error, &changed);
        } catch (...) {
            return "applyCommandJson let an exception escape";
        }
        if (!ok && changed) {
            return "applyCommandJson rejected but reported documentChanged";
        }
        if (ok && changed != (cmdStore.doc() != docBefore)) {
            return std::string("applyCommandJson reported documentChanged ") + (changed ? "true" : "false") +
                   " but the document " + (changed ? "is the same" : "differs");
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
    corpus.push_back(readFixture("state-feel.json"));
    corpus.push_back(readFixture("state-legacy-feel.json"));
    corpus.push_back(readFixture("state-legacy-length.json"));
    corpus.push_back(readFixture("state-tracks.json"));
    corpus.push_back(readFixture("state-legacy-tracks.json"));
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

// ---- feel and meter payloads ----

TEST_CASE("json robustness: mutated setFeel and setMeter payloads apply or reject atomically") {
    const std::vector<std::string> corpus = {
        json({{"schema", kJsonSchema}, {"cmd", "setFeel"}, {"rhythm", "arpUpDown"}, {"strum", "downUp"},
              {"strumSpeed", "loose"}, {"humanise", "heavy"}}).dump(),
        json({{"schema", kJsonSchema}, {"cmd", "setFeel"}, {"rhythm", "arpeggio"}, {"strum", "off"},
              {"strumSpeed", "medium"}, {"humanise", "off"}, {"arpPattern", "pinkyUpDown"}, {"arpRate", "half"},
              {"arpRange", "two"}}).dump(),
        json({{"schema", kJsonSchema}, {"cmd", "setFeel"}, {"rhythm", "block"}, {"strum", "off"},
              {"strumSpeed", "medium"}, {"humanise", "off"}}).dump(),
        json({{"schema", kJsonSchema}, {"cmd", "setMeter"}, {"num", 7}, {"den", 8}}).dump(),
        json({{"schema", kJsonSchema}, {"cmd", "setMeter"}, {"num", 4}, {"den", 4}}).dump(),
    };
    Rng rng{0xD1B54A32D192ED03ull};
    DocumentStore cmdStore = prefilledStore(true);
    DocumentStore stateStore = prefilledStore(true);
    int accepted = 0;
    constexpr int kFeelIterations = 1000;
    for (int i = 0; i < kFeelIterations; ++i) {
        if (i % 25 == 0) {
            cmdStore = prefilledStore((i / 25) % 2 == 0);
            stateStore = prefilledStore((i / 25) % 2 == 0);
        }
        std::string m = corpus[rng.below(corpus.size())];
        const std::size_t mutations = rng.below(2) == 0 ? 1 : 1 + rng.below(3);
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
    CHECK(accepted > 10);
    CHECK(accepted < kFeelIterations);
}

// ---- strip-editing payloads ----

TEST_CASE("json robustness: mutated strip-editing payloads apply or reject atomically") {
    const std::vector<std::string> corpus = {
        json({{"schema", kJsonSchema}, {"cmd", "insertChords"}, {"index", 2},
              {"chords", json::array({json({{"root", 2}, {"type", "m7"}, {"lengthBeats", 0.75}, {"velocity", 90},
                                            {"inversion", 1}, {"octave", -1}, {"manualVoicing", true}}),
                                      json({{"root", 7}, {"type", "7"}})})}}).dump(),
        json({{"schema", kJsonSchema}, {"cmd", "removeChords"}, {"ids", json::array({4, 2, 99})}}).dump(),
        json({{"schema", kJsonSchema}, {"cmd", "setLengths"}, {"ids", json::array({1, 3})}, {"lengthBeats", 1.5}}).dump(),
        json({{"schema", kJsonSchema}, {"cmd", "selectEvents"}, {"ids", json::array({1, 2, 4})}, {"anchor", 2}}).dump(),
        json({{"schema", kJsonSchema}, {"cmd", "setNewChordLength"}, {"lengthBeats", 0.5}}).dump(),
        json({{"schema", kJsonSchema}, {"cmd", "setNewChordLength"}, {"lengthBeats", nullptr}}).dump(),
    };
    Rng rng{0xA0761D6478BD642Full};
    DocumentStore cmdStore = prefilledStore(true);
    DocumentStore stateStore = prefilledStore(true);
    int accepted = 0;
    constexpr int kStripIterations = 1000;
    for (int i = 0; i < kStripIterations; ++i) {
        if (i % 25 == 0) {
            cmdStore = prefilledStore((i / 25) % 2 == 0);
            stateStore = prefilledStore((i / 25) % 2 == 0);
        }
        std::string m = corpus[rng.below(corpus.size())];
        const std::size_t mutations = rng.below(2) == 0 ? 1 : 1 + rng.below(3);
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
    CHECK(accepted > 10);
    CHECK(accepted < kStripIterations);
}

// ---- track and note payloads ----

TEST_CASE("json robustness: every track and note command is accepted on a prepared store, and its mutants apply or reject atomically") {
    const std::vector<std::string> corpus = trackCommandCorpus(prefilledTrackStore(false).revision());
    // The corpus itself: each entry accepted on a fresh prepared store, then all of them in a row on a shared one.
    for (std::size_t i = 0; i < corpus.size(); ++i) {
        DocumentStore store = prefilledTrackStore(false);
        std::string error;
        CHECK_MESSAGE(applyCommandJson(store, corpus[i], error), "track corpus entry " << i << " rejected: " << error
                                                                                        << " input=" << escaped(corpus[i]));
    }
    {
        DocumentStore cmdStore = prefilledTrackStore(false);
        DocumentStore stateStore = prefilledStore();
        for (const std::string& m : corpus) {
            const std::string problem = feed(cmdStore, stateStore, m);
            CHECK_MESSAGE(problem.empty(), problem << " input=" << escaped(m));
        }
    }
    Rng rng{0xE7037ED1A0B428DBull};
    DocumentStore cmdStore = prefilledTrackStore(true);
    DocumentStore stateStore = prefilledStore(true);
    int accepted = 0;
    constexpr int kTrackIterations = 1500;
    for (int i = 0; i < kTrackIterations; ++i) {
        if (i % 25 == 0) {  // a fresh store now and then (alternately with a redo step, which makes baseRevision stale)
            cmdStore = prefilledTrackStore((i / 25) % 2 == 0);
            stateStore = prefilledStore((i / 25) % 2 == 0);
        }
        std::string m = corpus[rng.below(corpus.size())];
        const std::size_t mutations = rng.below(2) == 0 ? 1 : 1 + rng.below(3);
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
    MESSAGE("json robustness track fuzz: " << kTrackIterations << " mutants, " << accepted << " accepted as commands");
    CHECK(accepted > 20);
    CHECK(accepted < kTrackIterations);
}

TEST_CASE("json robustness: integers at the edges of 64 and 32 bits are refused or clamped in every track field, never wrapped") {
    const char* const edges[] = {"9223372036854775807", "-9223372036854775808", "9223372036854775808", "-9223372036854775809",
                                 "18446744073709551615", "18446744073709551616", "2147483647", "2147483648", "-2147483648",
                                 "-2147483649", "4294967296", "1e19", "-1e19"};
    DocumentStore cmdStore = prefilledTrackStore();
    DocumentStore stateStore = prefilledStore();
    const auto sweep = [&](const std::string& before, const std::string& after) {
        for (const char* edge : edges) {
            const std::string text = before + edge + after;
            const std::string problem = feed(cmdStore, stateStore, text);
            REQUIRE_MESSAGE(problem.empty(), problem << " input=" << text);
        }
    };
    sweep(R"({"schema":1,"cmd":"insertNotes","trackId":1,"notes":[{"pitch":60,"length":480,"start":)", "}]}");
    sweep(R"({"schema":1,"cmd":"insertNotes","trackId":1,"notes":[{"pitch":60,"start":0,"length":)", "}]}");
    sweep(R"({"schema":1,"cmd":"insertNotes","trackId":1,"notes":[{"start":0,"length":480,"pitch":)", "}]}");
    sweep(R"({"schema":1,"cmd":"insertNotes","trackId":1,"notes":[{"pitch":60,"start":0,"length":480,"velocity":)", "}]}");
    sweep(R"({"schema":1,"cmd":"insertNotes","trackId":)", R"(,"notes":[{"pitch":60,"start":0,"length":480}]})");
    sweep(R"({"schema":1,"cmd":"updateNotes","trackId":1,"notes":[{"id":1,"start":)", "}]}");
    sweep(R"({"schema":1,"cmd":"updateNotes","trackId":1,"notes":[{"id":1,"length":)", "}]}");
    sweep(R"({"schema":1,"cmd":"updateNotes","trackId":1,"notes":[{"id":)", R"(,"pitch":1}]})");
    sweep(R"({"schema":1,"cmd":"removeNotes","trackId":1,"ids":[1],"baseRevision":)", "}");
    sweep(R"({"schema":1,"cmd":"removeNotes","trackId":1,"ids":[)", "]}");
    sweep(R"({"schema":1,"cmd":"selectNotes","trackId":)", R"(,"ids":[1]})");
    sweep(R"({"schema":1,"cmd":"moveTrack","id":1,"index":)", "}");
    sweep(R"({"schema":1,"cmd":"setTrackMute","on":true,"id":)", "}");
    sweep(R"({"schema":1,"cmd":"removeTrack","id":)", "}");
    sweep(R"({"schema":1,"cmd":"patchTrack","name":"x","id":)", "}");
    sweep(R"({"schema":1,"cmd":"setTrackSolo","on":true,"id":)", "}");
    sweep(R"({"schema":1,"cmd":"insertChords","index":)",
          R"(,"chords":[{"root":0,"type":"maj","notes":[{"lane":{"name":"B","role":"bass"},"length":480,"pitch":40,"offset":0}]}]})");
    sweep(R"({"schema":1,"cmd":"insertChords","index":0,"chords":[{"root":0,"type":"maj","notes":[{"lane":{"name":"B","role":"bass"},"length":480,"pitch":40,"offset":0,"velocity":)",
          "}]}]}");
    sweep(R"({"schema":1,"cmd":"insertChords","index":0,"chords":[{"root":0,"type":"maj","notes":[{"lane":{"name":"B","role":"bass"},"length":480,"pitch":40,"offset":)",
          "}]}]}");
    sweep(R"({"schema":1,"cmd":"insertChords","index":0,"chords":[{"root":0,"type":"maj","notes":[{"lane":{"name":"B","role":"bass","trackId":)",
          R"(},"length":480,"pitch":40,"offset":0}]}]})");
}

TEST_CASE("json robustness: every Length menu value as the new-chord length and on four blocks survives a save") {
    // The menu's note values, the bars of 4/4, 3/4, 7/8 and 12/8, and the ends of the range.
    const double values[] = {0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0, 3.5, 6.0, 7.0, 8.0, 14.0, 16.0, 24.0, 63.75, 64.0};
    DocumentStore cmdStore = prefilledStore();
    DocumentStore stateStore = prefilledStore();
    for (const double beats : values) {
        CAPTURE(beats);
        const std::string newLength =
            json({{"schema", kJsonSchema}, {"cmd", "setNewChordLength"}, {"lengthBeats", beats}}).dump();
        const std::string lengths =
            json({{"schema", kJsonSchema}, {"cmd", "setLengths"}, {"ids", json::array({1, 2, 3, 4})}, {"lengthBeats", beats}})
                .dump();
        for (const std::string& m : {newLength, lengths}) {
            bool accepted = false;
            const std::string problem = feed(cmdStore, stateStore, m, &accepted);
            REQUIRE_MESSAGE(problem.empty(), problem << " input=" << m);
            CHECK(accepted);
        }
        CHECK(cmdStore.doc().newChordBeats == std::optional<double>(beats));
        DocumentStore again;
        std::string error;
        REQUIRE_MESSAGE(loadStateJson(again, stateJson(cmdStore), error), error);
        CHECK(again.doc() == cmdStore.doc());
    }
}

TEST_CASE("json robustness: every valid feel and meter keeps the invariants and survives a save") {
    const char* const rhythms[] = {"block", "pulse", "subdivided", "offbeats", "lowThenUpper", "arpeggio"};
    const char* const strums[] = {"off", "down", "up", "downUp"};
    const char* const speeds[] = {"tight", "medium", "loose"};
    const char* const humanises[] = {"off", "light", "medium", "heavy"};
    DocumentStore cmdStore = prefilledStore();
    DocumentStore stateStore = prefilledStore();
    int n = 0;
    for (const char* r : rhythms) {
        for (const char* st : strums) {
            for (const char* sp : speeds) {
                for (const char* h : humanises) {
                    // One meter per feel, cycled, so that every meter meets several feels.
                    static const int kMeters[][2] = {{4, 4}, {3, 4}, {6, 8}, {7, 8}, {2, 4}, {16, 8}, {1, 2}, {5, 4}};
                    const int* meter = kMeters[n++ % 8];
                    const std::string feelMsg =
                        json({{"schema", kJsonSchema}, {"cmd", "setFeel"}, {"rhythm", r}, {"strum", st},
                              {"strumSpeed", sp}, {"humanise", h}}).dump();
                    const std::string meterMsg =
                        json({{"schema", kJsonSchema}, {"cmd", "setMeter"}, {"num", meter[0]}, {"den", meter[1]}}).dump();
                    for (const std::string& m : {feelMsg, meterMsg}) {
                        bool accepted = false;
                        const std::string problem = feed(cmdStore, stateStore, m, &accepted);
                        REQUIRE_MESSAGE(problem.empty(), problem << " input=" << m);
                        CHECK(accepted);
                    }
                    // The save carries both and loads back to the same document.
                    DocumentStore again;
                    std::string error;
                    REQUIRE_MESSAGE(loadStateJson(again, stateJson(cmdStore), error), error);
                    CHECK(again.doc() == cmdStore.doc());
                }
            }
        }
    }
    CHECK(n == 6 * 4 * 3 * 4);
}

TEST_CASE("json robustness: every arpeggio pattern, rate and range keeps the invariants and survives a save") {
    const char* const patterns[] = {"up",        "down",          "upDown",      "downUp",     "upAndDown",  "downAndUp",
                                    "converge",  "diverge",       "conAndDiverge", "pinkyUp",  "pinkyUpDown", "thumbUp",
                                    "thumbUpDown", "alberti",     "rootFirst",   "chord",      "random"};
    const char* const rates[] = {"pulse", "subdivision", "half"};
    const char* const ranges[] = {"one", "two"};
    DocumentStore cmdStore = prefilledStore();
    DocumentStore stateStore = prefilledStore();
    int m = 0;
    for (const char* p : patterns) {
        for (const char* r : rates) {
            for (const char* g : ranges) {
                ++m;
                const std::string feelMsg =
                    json({{"schema", kJsonSchema}, {"cmd", "setFeel"}, {"rhythm", "arpeggio"}, {"strum", "off"},
                          {"strumSpeed", "medium"}, {"humanise", "off"}, {"arpPattern", p}, {"arpRate", r},
                          {"arpRange", g}}).dump();
                bool accepted = false;
                const std::string problem = feed(cmdStore, stateStore, feelMsg, &accepted);
                REQUIRE_MESSAGE(problem.empty(), problem << " input=" << feelMsg);
                CHECK(accepted);
                DocumentStore again;
                std::string error;
                REQUIRE_MESSAGE(loadStateJson(again, stateJson(cmdStore), error), error);
                CHECK(again.doc() == cmdStore.doc());
            }
        }
    }
    CHECK(m == 17 * 3 * 2);
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

TEST_CASE("json robustness: a mistyped midiThru is rejected by both readers") {
    for (const char* name : {"state-progression.json", "state-legacy-phase1.json", "state-feel.json", "state-legacy-feel.json",
                             "state-tracks.json", "state-legacy-tracks.json"}) {
        const std::string fixture = readFixture(name);
        REQUIRE_FALSE(fixture.empty());
        for (const json& bad : {json(1), json("yes"), json(nullptr), json::array()}) {
            json state = json::parse(fixture);
            state["midiThru"] = bad;
            DocumentStore store = prefilledStore();
            const Observable before(store);
            std::string error;
            CHECK_FALSE(loadStateJson(store, state.dump(), error));
            CHECK_FALSE(error.empty());
            CHECK(error.find("midiThru") != std::string::npos);
            CHECK(Observable(store) == before);
        }
    }
    for (const json& bad : {json(1), json("yes"), json(nullptr)}) {
        DocumentStore store = prefilledStore();
        const Observable before(store);
        std::string error;
        CHECK_FALSE(applyCommandJson(store, json({{"schema", 1}, {"cmd", "setMidiThru"}, {"on", bad}}).dump(), error));
        CHECK(error.find("on") != std::string::npos);
        CHECK(Observable(store) == before);
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
