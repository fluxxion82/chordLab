#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace chordlab {

// Only references cross this interface, so the definitions (document.h, palette.h) are not needed here.
struct Document;
class DocumentStore;
struct Key;
struct Palette;

/// Version of the wire format. Every message (snapshot, command, state) carries `"schema": kJsonSchema`;
/// a message with a different or missing schema is rejected.
/// Adding a field is not a schema change: the readers here ignore members they do not know, and the UI
/// reads only the members it needs, so a new field (the snapshot's `modes`, `tonics`, `outOfKey`,
/// `homeTriads`, `relationMode`, `relationReference` and the cells' `relation` / `relationText` were added
/// under schema 1) costs nothing to a reader that has not heard of it. The number bumps only
/// when an existing field changes meaning or shape, or a required one is removed.
constexpr int kJsonSchema = 1;

/// Editor window size written by `stateJson` when the caller gives none, and assumed by `loadStateJson`
/// when the state has no `editor` object.
constexpr int kDefaultEditorWidth = 900;
constexpr int kDefaultEditorHeight = 620;

/// Largest message either reader accepts, in bytes, and the deepest nesting of objects and arrays it
/// accepts (the top-level object is level 1). Longer or deeper input is rejected by a pre-scan before
/// the parser sees it, so a corrupt or hostile message cannot make the parser allocate without bound.
constexpr std::size_t kMaxJsonBytes = std::size_t{1} << 20;
constexpr int kMaxJsonDepth = 64;

/// Common encodings
/// ----------------
/// A key is `{"tonic":{"letter":"C","alter":0},"mode":"Ionian"}`: `letter` is a one-character string
/// 'A'..'G', `alter` -1/0/1, `mode` the `Mode` enumerator name (Ionian, Dorian, Phrygian, Lydian,
/// Mixolydian, Aeolian, Locrian, HarmonicMinor, MelodicMinor). A chord is a `root` pitch class 0..11
/// plus a `type`, the stable dictionary id of chord.h ("maj", "m7b5", ...). A key name is the spelled
/// tonic plus the mode name: "C Ionian", "Bb HarmonicMinor".
/// Beats and lengths are JSON numbers, ids and velocities integers. Strings are UTF-8 (symbols contain
/// the literal `Δ` and `°`).
///
/// Robustness of the two readers (`applyCommandJson`, `loadStateJson`)
/// --------------------------------------------------------------------
/// Both are noexcept and transactional: the whole message is parsed and validated into plain data first,
/// the parsed JSON tree is destroyed (nlohmann's destructor allocates, so it must not be the thing that
/// fails after a change), and only then is the store touched, through calls that give the strong
/// exception guarantee (document.h). A false result therefore always means the store, its history and
/// its id counter are exactly as they were. An exception the validation did not anticipate (an
/// allocation failure, say) is reported as `error == "internal error"`; by construction, not by test, it
/// too leaves the store unchanged.
/// Before parsing, the raw text is checked cheaply: longer than `kMaxJsonBytes`, nested deeper than
/// `kMaxJsonDepth`, or containing a raw NUL byte (nlohmann's lexer would read it as end of input and
/// silently drop whatever follows) is rejected. An escaped `\u0000` inside a string is just a
/// character: it never matches a command, type, mode or letter name, and is ignored in a field nothing
/// reads. A member that appears twice takes its last value (nlohmann's parse policy); the UI never
/// sends such messages, but the behaviour is pinned by a test.

/// The document as the UI shows it, with the given palette (normally
/// `buildPalette(doc.browsedKey, doc.projectKey)`), so that the UI never computes theory:
/// ```
/// {"schema":1,
///  "projectKey":<key>, "browsedKey":<key>, "projectKeyName":"C Ionian", "browsedKeyName":"G Ionian",
///  "progressionLoop":true, "followHost":true, "autoVoiceLead":false, "endBeat":4.0,
///  "selectedEventId":2|null,
///  "modes":["Ionian","Dorian","Phrygian","Lydian","Mixolydian","Aeolian","Locrian","HarmonicMinor",
///           "MelodicMinor"],
///  "tonics":{"Ionian":[{"letter":"C","alter":0,"text":"C"},{"letter":"D","alter":-1,"text":"Db"}, ...],
///            "Dorian":[...], ...},
///  "events":[{"id":1,"root":0,"type":"maj","symbol":"C","roman":"I","family":"Major","function":"T",
///             "tension":3,"startBeat":0.0,"lengthBeats":4.0,"velocity":100,
///             "inversion":0,"octave":0,"manualVoicing":false,"voicing":[60,64,67],"outOfKey":[],
///             "why":null}, ...],
///  "sections":[{"id":1,"name":"Verse","colourIndex":0,"firstEventIndex":0}, ...],
///  "suggestions":[{"root":5,"type":"maj","symbol":"F","roman":"IV","function":"PD","score":2.35,
///                  "reasons":["commonTones","smooth","diatonic","predominant"],
///                  "reasonTexts":["1 common tone (C)", ...]}, ...],
///  "palette":{"tiers":5,
///             "cells":[{"degree":0,"tier":1,"root":0,"type":"maj","symbol":"C","roman":"I",
///                       "family":"Major","function":"T","tension":3,"filled":true,
///                       "voicing":[60,64,67],"outOfKey":[],
///                       "relation":92,"relationText":"1 common tone (C) · up a fifth · cost 3"}, ...]},
///  "homeTriads":[],
///  "relationMode":"blend", "relationReference":{"eventId":3,"symbol":"F"}|null}
/// ```
/// Events are in playback order with `startBeat` accumulated as `Document::startBeatOf` does; `symbol` is
/// the Klimper-style symbol spelled in the project key, `roman` the Standard-style numeral relative to the
/// project key, `family` the type's family ("Major", "Minor" or "Other", the sequence bar's colour;
/// additive, no schema bump). Palette cells carry the palette's own `symbol` (spelled in the browsed key)
/// and `roman`, the same `family`, and the default `closeVoicing` as their `voicing`, in the palette's
/// order.
/// `outOfKey`, on events and cells alike, is the chord's pitch classes that are not in the project key's
/// scale, ascending and without repeats: what a hover popover lists as borrowed notes. It is empty
/// exactly when a cell is `filled`.
/// `modes` is every `Mode` name in enum order and `tonics` maps each of them to twelve entries indexed
/// by pitch class, the `canonicalTonic` of that pitch class in that mode with its ASCII `text`
/// ("Db", "F#"), so that the key pickers can label themselves without a copy of the theory. Both are
/// constant for a given engine build and are repeated in every snapshot for simplicity.
///
/// Phase 2 fields (P2-D8; all additive, schema still 1), events and cells alike:
/// - `function`: `functionShortName(functionOf(chord, projectKey))`, "T", "PD", "D" or "O".
/// - `tension`: `tension(chord, projectKey)`, 0..100.
///
/// Events only:
/// - `inversion`, `octave`, `manualVoicing`: the event's stored voicing fields (document.h).
/// - `voicing`: the notes `compile` gives the chord, `voicings(doc)[i]` (compiler.h): with
///   `autoVoiceLead` on, the inversion and octave auto voice-leading chose; otherwise, and for a manual
///   chord, `voicing(chord, inversion, octave)`. The sequence bar therefore shows what sounds.
/// - `why`: null on the first event; on every later one the `relate` (harmony.h) of its chord against the
///   chord before it, as data plus one sentence:
///   ```
///   {"commonTones":1, "commonPcs":[7], "commonNames":["G"], "rootMotion":"down a fifth",
///    "voiceLeadingCost":4, "borrowedFrom":<key>|null, "borrowedFromName":"C Aeolian"|null,
///    "text":"G7 → C: down a fifth, resolves (D→T), 1 common tone (G), smooth voice leading (cost 4)"}
///   ```
///   `commonNames` spells `commonPcs` in the project key; `rootMotion` is `rootMotionName`;
///   `voiceLeadingCost` is the octave-free `pcVoiceLeadingCost`; `borrowedFrom` is the key the chord is
///   borrowed from (`borrowedFrom`, harmony.h) and `borrowedFromName` its `keyName`, both null for a chord
///   in the key or in no parallel mode.
///   `text` is "<previous symbol> → <symbol>: <root motion>" (the symbols as the events' `symbol`, the
///   arrow a literal UTF-8 character) followed by these clauses, in this order, each only when it holds:
///     ", resolves (D→T)"                 `functionOf` the previous chord is Dominant and this one's Tonic;
///     ", 1 common tone (G)"              commonTones >= 1: the count, "tone" or "tones", and the
///     ", 2 common tones (C, E)"          `commonNames` comma-separated;
///     ", smooth voice leading (cost c)"  voiceLeadingCost <= the number of voices it sums over, the
///                                        larger of the two chords' pitch-class counts (every voice moves
///                                        one semitone on average, or less: G7 → C costs 4 over four
///                                        voices, C → D costs 6 over three). This is a looser bar than the
///                                        suggestions' `Reason::Smooth` (cost <= 2), which ranks
///                                        candidates rather than describes a move the user made;
///     ", borrowed from C harmonic minor" borrowedFrom is set; the key in prose as the suggestions'
///                                        `reasonTexts` write it (mode names as `Mode` spells them,
///                                        except "harmonic minor" and "melodic minor"), so that the two
///                                        popovers agree. `borrowedFromName` keeps the `keyName` form of
///                                        `projectKeyName` for a UI that looks keys up by name.
///
/// Top level:
/// - `autoVoiceLead`: `Document::autoVoiceLead`.
/// - `sections`: `Document::sections` in order, each `{id, name, colourIndex, firstEventIndex}`; empty
///   when the document has none.
/// - `selectedEventId`: `selectedEventId` when an event has that id, else null (so `kNoEventId`, the
///   default, and a stale id alike read as "no selection"). The plug-in passes the store's live
///   selection (`DocumentStore::selectedEventId`).
/// - `homeTriads` (additive, schema still 1): what a "home" compare row under the palette shows while
///   another key is browsed. Empty when `browsedKey == projectKey` (the keys as spelled, tonic and mode:
///   the palette is then the home key's own). Otherwise exactly seven entries, the diatonic triads of the
///   PROJECT key (`diatonicTriad(projectKey, degree)`) in degree order 0..6, each with every member of a
///   palette cell and the same meaning, plus an `id`:
///   ```
///   {"id":"home-0","degree":0,"tier":1,"root":0,"type":"maj","symbol":"C","roman":"I","family":"Major",
///    "function":"T","tension":3,"filled":true,"voicing":[60,64,67],"outOfKey":[]}
///   ```
///   `degree` is the degree of the project scale, `tier` always `kPaletteTriadTier` (1), `symbol` is
///   spelled in the project key and `roman`, `function`, `tension` are read against it, so an entry
///   equals the tier-1 cell the palette holds for that degree when the project key itself is browsed;
///   `filled` is therefore always true and `outOfKey` always empty. `id` is "home-<degree>"; palette
///   cells carry no `id` (the UI derives one from tier and degree), so the two cannot collide.
/// - `relationMode` (additive, schema still 1; always present): `relationModeName(doc.relationMode)`
///   (relatedness.h), one of "off", "commonTones", "voiceLeading", "function", "blend".
/// - `relationReference` (additive): the chord the palette is sized against (R1): the selected event when
///   `selectedEventId` names one, else the last event, as `{"eventId": <its id>, "symbol": <its symbol as
///   the events write it>}`; null when the document has no chords or `relationMode` is "off". It is the
///   same chord the suggestions follow (`previous` below).
/// - per cell, in `palette.cells` and `homeTriads` alike (additive), present exactly when
///   `relationReference` is not null and ABSENT otherwise (absent, not null):
///   `relation`, the cell's `Relatedness::score` 0..100 against the reference under `relationMode`, and
///   `relationText`, its `Relatedness::text` ("2 common tones (C, E) · down a third · cost 2", with
///   " · borrowed from C Aeolian" for a borrowed chord). All of them come from one `relatedness` call over
///   the palette cells followed by the home triads, so `blend`'s min–max normalisation spans both rows
///   together: the best of either row is 100, the worst 0, and a cell equal to the reference is 100.
/// - `suggestions`: `suggestNext(previous, projectKey, browsedKey)` (suggest.h, Pop style, at most 8),
///   where `previous` is the selected event's chord when `selectedEventId` names an event, else the last
///   event's chord, else null (the opening suggestions of an empty document). Each entry carries the
///   chord (`root`, `type`), the `Suggestion`'s `label` as `symbol` and its `roman`, `function` as the
///   events do (the chord's `functionOf` in the project key), the `score`, the `reasons` as `reasonName`
///   strings and the parallel `reasonTexts`.
/// Allocates (compiles, ranks): the UI/message thread only.
std::string snapshotJson(const Document& doc, const Palette& palette, int selectedEventId = -1);

/// A key as the snapshot names it: the spelled tonic and the mode name, "C Ionian", "Bb HarmonicMinor"
/// (the `projectKeyName` / `browsedKeyName` fields; the plug-in names its MIDI export after it).
/// Allocates: not for the audio thread.
std::string keyName(const Key& key);

/// Applies one UI command, `{"schema":1,"cmd":"<name>", ...fields}`, to the store:
/// - `setKey {letter,alter,mode}`: `setProjectKey` (the browsed key is left alone).
/// - `browseKey` with exactly one of `{"steps":n}` (`fifthsNeighbour` of the current browsed key),
///   `{"mode":"<Mode>"}` (the parallel mode on the same tonic pitch class, canonically spelled) or
///   `{"home":true}` (browsed key = project key).
/// - `appendChord {root,type,lengthBeats?,velocity?}`: `appendChord`, with the store's defaults
///   (4 beats, velocity 100) for the optional fields. Refused (false, an error naming the reason, no
///   change) when the document already holds `kMaxChords` chords or the id counter is exhausted.
/// - `moveChord {id,index}`, `setLength {id,lengthBeats}`, `setVelocity {id,velocity}`,
///   `removeChord {id}`: the matching store call.
/// - `setProgressionLoop {on}`, `setFollowHost {on}`: the matching live-state setter.
/// - `setInversion {id,inversion}`, `setOctave {id,octave}`, `resetVoicing {id}`: the matching store
///   call (the values are clamped by the store and the event is marked manual; see document.h).
/// - `setAutoVoiceLead {on}`: `setAutoVoiceLead`, undoable.
/// - `addSection {index,name?}`: `addSection(index, name)`; `name` defaults to the store's "Section <id>"
///   and, given, must be at most `kMaxSectionNameBytes` UTF-8 bytes (longer is rejected, not cut: the UI
///   limits its field and a longer name is a malformed message). An `index` that is no chord position,
///   or that already starts a section, is a valid no-op (the UI may name a chord an edit just moved).
///   Refused (false, an error naming the reason, no change) only when the section id counter is
///   exhausted, like `appendChord`.
/// - `renameSection {id,name}` (same bound on `name`), `setSectionColour {id,colourIndex}`,
///   `removeSection {id}`, `moveSectionStart {id,index}`: the matching store call (colours clamped, a
///   move the store declines is a no-op).
/// - `selectEvent {id}`: `setSelectedEvent`, live state; `id` is an integer or null (null, like
///   `kNoEventId`, clears the selection; an id no event has is a no-op). Pass the store's
///   `selectedEventId()` to `snapshotJson` so that the snapshot's `selectedEventId` and `suggestions`
///   follow it.
/// - `setRelationMode {mode}`: `setRelationMode`, live state (never history, kept across undo and redo,
///   persisted by `stateJson`); `mode` is one of "off", "commonTones", "voiceLeading", "function",
///   "blend" (`relationModeName`, relatedness.h). A missing, non-string or unknown `mode` is rejected with
///   an error that lists the five.
/// - `undo`, `redo`: no fields.
///
/// Every field is validated before anything is touched: an unknown `cmd`, a missing or mistyped field, a
/// root outside 0..11, an unknown chord type or mode, a bad letter or alter, a non-finite number or a
/// section name over the bound makes the call return false with a description in `error`, leaving the
/// document, its history and the selection exactly as they were. Fields the command does not use are ignored. Numbers are not
/// range-checked beyond that: lengths, velocities, inversions, octaves, colours and move targets are
/// clamped by the store, exactly as the direct calls do.
///
/// Returns true for every accepted command, including ones that change nothing: an edit naming an id no
/// event or section has, a move to the chord's current position, and `undo`/`redo` with nothing to undo
/// or redo are valid no-ops, not errors. `error` is cleared on success.
bool applyCommandJson(DocumentStore& store, std::string_view json, std::string& error) noexcept;

/// The persistent form of the store's document, without the palette:
/// ```
/// {"schema":1, "projectKey":<key>, "browsedKey":<key>, "progressionLoop":true, "followHost":true,
///  "autoVoiceLead":false, "relationMode":"blend",
///  "events":[{"id":1,"root":0,"type":"maj","lengthBeats":4.0,"velocity":100,
///             "inversion":0,"octave":0,"manualVoicing":false}, ...],
///  "sections":[{"id":1,"name":"Verse","colourIndex":0,"firstEventIndex":0}, ...],
///  "nextId":2, "nextSectionId":2, "editor":{"width":900,"height":620}}
/// ```
/// `nextId` is the store's id counter (`DocumentStore::nextId`), so that a reloaded document keeps
/// handing out ids no saved event ever had, and `nextSectionId` the section counter likewise.
/// `editorWidth` / `editorHeight` are the editor window size to remember; they are stored verbatim. The
/// selection (`DocumentStore::selectedEventId`) is live state and is not written.
/// `autoVoiceLead`, the events' `inversion`, `octave` and `manualVoicing`, `sections` and
/// `nextSectionId` were added under schema 1 (Phase 2), and `relationMode` (`relationModeName` of
/// `Document::relationMode`, live state that is nonetheless saved with the set, R5) after them: a reader
/// that predates them ignores them, and `loadStateJson` defaults them when they are missing, so states
/// cross both ways.
/// Size: the largest state the store can hold (`kMaxChords` chords, a section on each, every name at
/// `kMaxSectionNameBytes` bytes of characters that escape to two) is pinned by test_json_api.cpp at
/// well under a tenth of `kMaxJsonBytes`, so a saved state always loads.
std::string stateJson(const DocumentStore& store, int editorWidth = kDefaultEditorWidth,
                      int editorHeight = kDefaultEditorHeight);

/// Restores a document saved by `stateJson`. Atomic: the whole text is parsed and validated first; on any
/// problem (malformed JSON, wrong schema, a missing or mistyped field, an unknown chord type or mode, a
/// root outside 0..11, an id below 1 or repeated, more than `kMaxChords` events, a `nextId` below 1 or
/// not above every event id, a non-positive editor size, or sections that are not `sectionsWellFormed`
/// (document.h): not starting at 0, unsorted or sharing a start, an index outside the progression, a
/// section id outside 1..INT_MAX-1 or repeated, a name over `kMaxSectionNameBytes` bytes; or a
/// `nextSectionId` below 1 or not above every section id) it returns false with a description in `error` and the store, its history, its selection and the
/// out-params are untouched. Otherwise it replaces the store's document and both id counters
/// (`DocumentStore::replace`, which clears the undo/redo history and the selection), writes the editor
/// size into the out-params that are non-null, and returns true with `error` cleared.
/// Lengths, velocities, inversions, octaves and section colours are clamped to the store's ranges
/// rather than rejected (a mistyped value is still rejected). Optional, with their defaults: `editor`
/// (`kDefaultEditorWidth` x `kDefaultEditorHeight`), the events' `inversion` (0), `octave` (0) and
/// `manualVoicing` (false), `autoVoiceLead` (false), `relationMode` ("blend"; when given, one of the five
/// `relationModeName` strings, anything else rejecting the state), `sections` (none) and `nextSectionId`
/// (derived, one past the largest section id, 1 without sections; when given it must be at least that and
/// is kept, like `nextId`). Every other field is required.
bool loadStateJson(DocumentStore& store, std::string_view json, std::string& error, int* editorWidth = nullptr,
                   int* editorHeight = nullptr) noexcept;

}  // namespace chordlab
