#pragma once

#include "chordlab/link_state.h"
#include "chordlab/tracks.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chordlab {

// Only references cross this interface, so the definitions (document.h, palette.h, link.h) are not needed here;
// tracks.h is included for `NoteSelection`, which `SnapshotExtras` holds by value, and link_state.h for
// `InstanceState`, which `loadStateJson` fills through an optional.
struct Document;
class DocumentStore;
struct Key;
class Listener;
struct LinkView;
struct Palette;

/// Version of the wire format. Every message (snapshot, command, state) carries `"schema": kJsonSchema`;
/// a message with a different or missing schema is rejected.
/// Adding a field is not a schema change: the readers here ignore members they do not know, and the UI
/// reads only the members it needs, so a new field (the snapshot's `modes`, `tonics`, `outOfKey`,
/// `homeTriads`, `relationMode`, `relationReference`, the cells' `relation` / `relationText` and
/// `presets` were added under schema 1, like the `loadPreset` command) costs nothing to a reader that
/// has not heard of it. The number bumps only
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

/// What `snapshotJson` publishes that lives in the `DocumentStore`, not the `Document` (tracks design D6): the live
/// note selection (`DocumentStore::noteSelection`) and the content revision (`DocumentStore::revision`). The shell
/// fills it from its store in the same critical section as the document it serialises. The default (no selection,
/// revision 0) is for callers that have no store.
struct SnapshotExtras {
    NoteSelection noteSelection;
    std::uint64_t revision = 0;
};

/// The document as the UI shows it, with the given palette (normally
/// `buildPalette(doc.browsedKey, doc.projectKey)`), so that the UI never computes theory:
/// ```
/// {"schema":1,
///  "projectKey":<key>, "browsedKey":<key>, "projectKeyName":"C Ionian", "browsedKeyName":"G Ionian",
///  "progressionLoop":true, "followHost":true, "autoVoiceLead":false,
///  "transposeWithKey":false, "keyHintSeen":false, "midiThru":true, "endBeat":4.0,
///  "feel":{"rhythm":"block","strum":"off","strumSpeed":"medium","humanise":"off",
///          "arpPattern":"up","arpRate":"subdivision","arpRange":"one"},
///  "meter":{"num":4,"den":4}, "feelLimited":false,
///  "selectedEventId":2|null, "selectedEventIds":[2], "newChordBeats":null|1.5,
///  "modes":["Ionian","Dorian","Phrygian","Lydian","Mixolydian","Aeolian","Locrian","HarmonicMinor",
///           "MelodicMinor"],
///  "tonics":{"Ionian":[{"letter":"C","alter":0,"text":"C"},{"letter":"D","alter":-1,"text":"Db"}, ...],
///            "Dorian":[...], ...},
///  "events":[{"id":1,"root":0,"type":"maj","symbol":"C","roman":"I","family":"Major","function":"T",
///             "tension":3,"startBeat":0.0,"lengthBeats":4.0,"velocity":100,
///             "inversion":0,"octave":0,"manualVoicing":false,"voicing":[60,64,67],
///             "voicingNames":["C4","E4","G4"],"outOfKey":[],"outOfKeyNames":[],"why":null,
///             "playStart":0.0,"playEnd":4.0,
///             "guide":{"roles":["root","outside","tension", ...],"scale":2741,
///                      "degrees":["R","b2","2", ...],"names":["C","Db","D", ...]}}, ...],
///  "sections":[{"id":1,"name":"Verse","colourIndex":0,"firstEventIndex":0}, ...],
///  "suggestions":[{"root":5,"type":"maj","symbol":"F","roman":"IV","function":"PD","score":2.35,
///                  "reasons":["commonTones","smooth","diatonic","predominant"],
///                  "reasonTexts":["1 common tone (C)", ...]}, ...],
///  "palette":{"tiers":5,
///             "cells":[{"degree":0,"tier":1,"root":0,"type":"maj","symbol":"C","roman":"I",
///                       "family":"Major","function":"T","tension":3,"filled":true,
///                       "voicing":[60,64,67],"voicingNames":["C4","E4","G4"],"outOfKey":[],"outOfKeyNames":[],
///                       "relation":92,"relationText":"1 common tone (C) · up a fifth · cost 3"}, ...]},
///  "homeTriads":[],
///  "relationMode":"blend", "relationReference":{"eventId":3,"symbol":"F"}|null,
///  "presets":[{"id":"axis","name":"Axis","genre":"Pop","tonality":"Major","numerals":"I – V – vi – IV",
///              "fits":true}, ...],
///  "ticksPerBeat":960, "revision":7,
///  "tracks":[{"id":1,"name":"Bass","role":"bass","mute":false,"solo":false,"audible":true,
///             "notes":{"id":[1,...],"pitch":[36,...],"start":[0,...],"length":[960,...],"velocity":[100,...],
///                      "anchor":[1,...],"offset":[0,...],"active":[true,...]}}, ...],
///  "chordsMuted":false, "chordsSolo":false, "chordsAudible":true,
///  "noteSelection":{"trackId":1,"ids":[1]}|null,
///  "trackLimits":{"maxTracks":4,"maxNotesPerTrack":1024,"minNoteTicks":30,"maxPolyphony":8,"maxOnsetsPerBeat":15},
///  "applied":[{"id":"applied-1-1","label":"V7/ii","target":"Dm","degree":1,"tier":1,"root":9,"type":"7",
///              "symbol":"A7","roman":"VI7","family":"Major","function":"D","tension":43,"filled":false,
///              "voicing":[69,73,76,79],"voicingNames":["A4","C#5","E5","G5"],"outOfKey":[1],"outOfKeyNames":["C#"],
///              "relation":59,"relationText":"1 common tone (A) · up a third · cost 4"}, ...]}
/// ```
/// `endBeat` is the playback period, `compile(doc).endBeat` (compiler.h): the loop length the scheduler wraps at,
/// which the UI wraps its playhead at. With a performing feel it is the last chord's end tick / 960, so it is
/// tick-rounded (three 0.2506-beat chords give 722 / 960, not 0.7518); with block chords, and in the
/// over-budget fallback (`feelLimited`), it is the authored sum, `Document::endBeat`. Events keep their
/// authored `startBeat` and `lengthBeats` either way. `feelLimited` is `compile(doc).feelLimited`.
/// Events are in playback order with `startBeat` accumulated as `Document::startBeatOf` does; `symbol` is
/// the Klimper-style symbol spelled in the project key, `roman` the Standard-style numeral relative to the
/// project key, `family` the type's family ("Major", "Minor" or "Other", the sequence bar's colour;
/// additive, no schema bump). Palette cells carry the palette's own `symbol` (spelled in the browsed key)
/// and `roman`, the same `family`, and the default `closeVoicing` as their `voicing`, in the palette's
/// order.
/// `outOfKey`, on events and cells alike, is the chord's pitch classes that are not in the project key's
/// scale, ascending and without repeats: what a hover popover lists as borrowed notes. It is empty
/// exactly when a cell is `filled`. `outOfKeyNames` (additive) spells them as tones of the chord
/// (`chordToneSpelling`, symbol.h: E7's "G#", not the Ab the key would say for pc 8), with the root as the
/// `symbol` spells it (the project key for events and home triads, the browsed key for palette cells, an
/// applied chord's own spelling key); `voicingNames` (additive) names `voicing`'s notes the same way, with
/// octaves, middle C (60) being C4 ("G#3" for 56 in E7): the tip's three lines then agree on every note.
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
///   `commonNames` spells `commonPcs` as tones of this chord (`commonToneSpellings`, harmony.h, the root in
///   the project key); `rootMotion` is `rootMotionName`;
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
/// - `transposeWithKey`, `keyHintSeen`: `Document::transposeWithKey` (a tonic change transposes the
///   sequence) and `Document::keyHintSeen` (the one-time hint has shown); live state, false by default.
/// - `midiThru`: `Document::midiThru` (the plug-in passes the MIDI it hears to its output); live state,
///   true by default. With it, `selectedEventIds`, `newChordBeats` and the eight tracks keys below the snapshot has
///   37 top-level keys.
/// - `feel`, `meter`: `Document::feel` as seven wire-name strings (`rhythm` block|pulse|subdivided|offbeats|
///   lowThenUpper|arpeggio, `strum` off|down|up|downUp, `strumSpeed` tight|medium|loose, `humanise`
///   off|light|medium|heavy, `arpPattern` up|down|upDown|downUp|upAndDown|downAndUp|converge|diverge|
///   conAndDiverge|pinkyUp|pinkyUpDown|thumbUp|thumbUpDown|alberti|rootFirst|chord|random, `arpRate`
///   pulse|subdivision|half, `arpRange` one|two; the last three are heard only under `arpeggio` but always
///   written) and `Document::meter` as `{num, den}`; 4/4 and block chords by default. The snapshot never
///   writes the old rhythm names `arpUp` / `arpUpDown`.
/// - `feelLimited`: `performDocument(doc).feelLimited`: the feel performs but is over budget, so playback and
///   export use block chords instead (false for a feel that does not perform).
/// - `sections`: `Document::sections` in order, each `{id, name, colourIndex, firstEventIndex}`; empty
///   when the document has none.
/// - `selectedEventId`: `selectedEventId` when an event has that id, else null (so `kNoEventId`, the
///   default, and a stale id alike read as "no selection"). The plug-in passes the store's live
///   selection (`DocumentStore::selectedEventId`); normalised together with `selectedEventIds` (below), so it
///   is the anchor the snapshot reports.
/// - `selectedEventIds` (additive, schema still 1; always present): the selection in strip order, the ids of
///   `selectedEventIds` that name an event, or, when none does, `selectedEventId` alone if it names one (the
///   plug-in passes `DocumentStore::selectedEventIds`); empty when nothing is selected. `selectedEventId` stays
///   the anchor (strip editing design D2), so every reader of it, the suggestions and the relations, is
///   unchanged; the two are normalised together: a non-empty selection whose given anchor is absent, unknown or
///   not one of its blocks reports its last block as `selectedEventId`, and the suggestions and the relation
///   reference follow it.
/// - `newChordBeats` (additive): `Document::newChordBeats`, the length of a chord added without one, or null for
///   one bar of the meter (D3).
/// - `homeTriads` (additive, schema still 1): what a "home" compare row under the palette shows while
///   another key is browsed. Empty when `browsedKey == projectKey` (the keys as spelled, tonic and mode:
///   the palette is then the home key's own). Otherwise exactly seven entries, the diatonic triads of the
///   PROJECT key (`diatonicTriad(projectKey, degree)`) in degree order 0..6, each with every member of a
///   palette cell and the same meaning, plus an `id`:
///   ```
///   {"id":"home-0","degree":0,"tier":1,"root":0,"type":"maj","symbol":"C","roman":"I","family":"Major",
///    "function":"T","tension":3,"filled":true,"voicing":[60,64,67],"voicingNames":["C4","E4","G4"],
///    "outOfKey":[],"outOfKeyNames":[]}
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
/// - per cell, in `palette.cells`, `homeTriads` and `applied` alike (additive), present exactly when
///   `relationReference` is not null and ABSENT otherwise (absent, not null):
///   `relation`, the cell's `Relatedness::score` 0..100 against the reference under `relationMode`, and
///   `relationText`, `relationText(Relatedness::relation, spellingKey)` with the cell's common tones spelled
///   as its `symbol` is (the browsed key for a palette cell, the project key for a home triad, an applied
///   chord's own key: "2 common tones (C, E) · down a third · cost 2", with " · borrowed from C Aeolian"
///   for a borrowed chord). All of them come from one `relatedness` call over
///   the palette cells followed by the home triads, so `blend`'s min–max normalisation spans both rows
///   together: the best of either row is 100, the worst 0, and a cell equal to the reference is 100.
///   The `applied` cells are that call's extras (relatedness.h): scored on the same scale without
///   entering its range, so they leave every palette and home score as it would be without them, and
///   under `blend` an applied chord better than the population's best is 100, one worse than its worst 0
///   (clamped). Their blend provenance is the project key's, as all scoring: an applied chord of the
///   browsed key that is not one of `appliedChords(projectKey)` scores as the chromatic chord it is there.
/// - `presets` (additive, schema still 1; always present, 29 entries): the built-in progression library
///   (presets.h), every record of `allPresets()` in table order (so grouped by genre), each
///   `{id, name, genre, tonality, numerals, fits}`: `id` the handle `loadPreset` takes, `name` and
///   `genre` display text (UTF-8: "ii–V–I", "R&B / Neo-soul"), `tonality` `tonalityName` ("Major",
///   "Minor", "Any"), `numerals` `presetNumerals` ("iiø7 – V7 – i7 – i7": the steps read in C major, or
///   A minor for a Minor preset, in the events' `roman` spelling, joined with an en dash between spaces,
///   repeats kept) and `fits` `presetFits` against `projectKey` (a Major preset fits Ionian, Lydian and
///   Mixolydian, a Minor one the other six modes, Any all nine). Everything but `fits` is constant for a
///   build, like `modes` and `tonics`.
/// - `applied` (additive, schema still 1; always present, an array that a future key could leave empty):
///   the applied chords of the BROWSED key, `appliedChords(browsedKey)` (applied.h) in its order (by
///   target degree, `V7/x` before `vii°7/x`), laid out as a second palette grid: each entry has every
///   member of a palette cell, written by the same writer, plus `id`, `label` and `target`:
///   - `degree` is the applied chord's `targetDegree`, the column of the chord it resolves to (1..6 of
///     the browsed scale; the tonic's column and a diminished or augmented target's stay empty), and
///     `tier` is 1 for the dominant seventh `V7/x` and 0 for the leading-tone seventh `vii°7/x`;
///   - `symbol` is `chordSymbol(chord, spellingKey)`, Klimper style like every palette symbol, so the
///     leading-tone chord is spelled as the leading tone of its target (`C#dim7` for vii°7/ii in C, not
///     `Dbdim7`);
///   - `roman`, `function`, `tension`, `filled` and `outOfKey` are read against the PROJECT key as for
///     every cell (`roman` is `romanNumeral(chord, projectKey)`; most applied chords are not `filled`,
///     but G7 is V7/IV of G and V7 of C);
///   - `id` is "applied-<degree>-<tier>" ("applied-1-1" is V7/ii), which no palette or home cell has;
///   - `label` is `AppliedChord::label`, the name relative to the browsed key ("V7/ii", "vii°7/bVI",
///     numerals read against the parallel major as `romanNumeral` does); a UI shows it in place of `roman`;
///   - `target` is the tonicised triad's symbol in the browsed key, `chordSymbol(diatonicTriad(browsedKey,
///     degree), browsedKey)` ("Dm" for V7/ii in C).
///   C Ionian has ten: A7, C#dim7 (ii, column 1), B7, D#dim7 (iii), C7, Edim7 (IV), D7, F#dim7 (V), E7,
///   G#dim7 (vi, column 5). Harmonic and melodic minor have six (their bIII+ is not tonicised), Locrian
///   twelve, every other mode ten.
/// - `suggestions`: `suggestNext(previous, projectKey, browsedKey)` (suggest.h, Pop style, at most 8),
///   where `previous` is the selected event's chord when `selectedEventId` names an event, else the last
///   event's chord, else null (the opening suggestions of an empty document). Each entry carries the
///   chord (`root`, `type`), the `Suggestion`'s `label` as `symbol` and its `roman`, `function` as the
///   events do (the chord's `functionOf` in the project key), the `score`, the `reasons` as `reasonName`
///   strings and the parallel `reasonTexts`.
///
/// Tracks fields (tracks design D6; all additive, schema still 1; ticks are JSON integers on the 960-per-beat grid):
/// - per event, `playStart` and `playEnd`: the chord's playback span in beats, `chordPlaySpans(doc)` (compiler.h):
///   the tick-rounded boundaries over 960 when a feel performs within budget (three 0.2506-beat chords: 241 / 960 for
///   the second chord's start), the authored doubles otherwise, so on the block path and in the over-budget fallback
///   they equal `startBeat` and `startBeat + lengthBeats` as accumulated. `startBeat` and `lengthBeats` stay authored
///   (the strip draws them); what is shaded by playback (the roll's guide bands, chord lookups) uses these.
/// - per event, `guide`: the chord's harmonic guide, `harmonicGuide(chord, doc.projectKey)` (guide.h, tracks design
///   D9: the rules for roles, the chord scale, degrees and names live there), read against the PROJECT key like
///   `roman`, never the browsed one. Four views indexed by absolute pitch class 0..11 (C = 0, not relative to the
///   root): `roles` twelve `guideRoleName` strings ("root", "chord", "tension", "avoid", "outside"), `scale` the
///   chord scale as a 12-bit integer (bit `pc` set when `pc` is in it; C major's 2741), `degrees` twelve ASCII
///   degree labels above the root ("R", "b9", "#11", "bb7"), `names` twelve ASCII note names on the guide's
///   letters ("E#", "G#", "Bb").
/// - `ticksPerBeat`: 960 (`kFeelTicksPerBeat`), the grid of every tick field.
/// - `revision`: `extras.revision`, the store's content revision (`DocumentStore::revision`, document.h: it moves
///   with every content commit, undo, redo and state load, never with a selection or a live flag); 0 when the caller
///   gives none. A note command's `baseRevision` names it.
/// - `tracks`: `Document::tracks` in order, each `{id, name, role, mute, solo, audible, notes}`: `role`
///   `trackRoleName` ("bass", "lead", "other"), `audible` `trackAudible` (tracks.h: the mute/solo outcome, so the UI
///   never derives it), `notes` the track's notes column-wise (no object per note, a cheap decode): `{id, pitch,
///   start, length, velocity, anchor, offset, active}`, each an array of the same length (the track's note count) in
///   the track's note order, so note `i` is the `i`th entry of every array; an empty track keeps every key with an
///   empty array. `start` is the derived absolute tick, `noteStart` (the anchor's boundary plus the offset, so the UI
///   never resolves anchors), `anchor` the id of the chord it belongs to, `offset` the ticks into it, `active`
///   `noteActive` (false once its chord is shorter than its offset: kept and drawn, not played). `tracks` is empty
///   without tracks. (The state JSON keeps one object per note.)
/// - `chordsMuted`, `chordsSolo`: `Document::chordsMuted` / `chordsSolo`; `chordsAudible`: `chordsAudible(doc)`.
/// - `noteSelection`: `extras.noteSelection` as `{"trackId":n,"ids":[...]}`, normalised against `doc` as
///   `selectedEventIds` is: the ids the track holds, in its note order, once each; null when the selection is empty,
///   names no track of `doc`, or keeps no id.
/// - `trackLimits`: the caps by name, `{maxTracks: kMaxTracks, maxNotesPerTrack: kMaxNotesPerTrack, minNoteTicks:
///   kMinTrackNoteTicks, maxPolyphony: kMaxTrackPolyphony, maxOnsetsPerBeat: kMaxTrackOnsetsPerBeat}` (tracks.h).
/// Allocates (compiles, ranks): the UI/message thread only.
std::string snapshotJson(const Document& doc, const Palette& palette, int selectedEventId = -1,
                         const std::vector<int>& selectedEventIds = {}, const SnapshotExtras& extras = {});

/// A key as the snapshot names it: the spelled tonic and the mode name, "C Ionian", "Bb HarmonicMinor"
/// (the `projectKeyName` / `browsedKeyName` fields; the plug-in names its MIDI export after it).
/// Allocates: not for the audio thread.
std::string keyName(const Key& key);

/// The `kMsgTagHeard` payload (design D14): {"schema":1,"active":bool,"held":[..],"heldNames":[..],
/// "chord":{"root","type","symbol","roman","inversion","bass"}|null,"chordHeld":bool,
/// "key":{"tonic":{"letter","alter"},"mode","name","percent"}|null,"onsets":n}.
/// `held` is the held MIDI pitches ascending and `heldNames` their ASCII spellings in the same order. A held
/// note that is a tone of the latched chord while `chordHeld` is true is spelled by
/// `chordToneSpelling(chord, pc, projectKey)` (E7's G# reads "G#"), any other by `spell(pc, projectKey)` (a
/// lone G# in C major reads "Ab"). The chord's `type` is its `ChordType::id`, `symbol` is
/// `chordSymbol(chord, projectKey)` and `roman` is `romanNumeral(chord, projectKey)`; `inversion` and `bass`
/// (a MIDI note) are the listener's. The key is the one the listener shows, named as the snapshot names a key
/// (`keyName`, "C Ionian"): the project key only spells the notes and the chord, it never renames the heard
/// key. `percent` is `round(100 * r)` as an integer clamped to 0..100
/// (a defined but failing `r` below 0 is reported for one tick before the key is dropped). `onsets` is `onsetsInWindow()`. Accidentals are ASCII
/// throughout (the UI adds glyphs). Allocates: the UI/message thread only.
std::string heardJson(const Listener& listener, const Key& projectKey);

/// Applies one UI command, `{"schema":1,"cmd":"<name>", ...fields}`, to the store:
/// - `setKey {letter,alter,mode}`: `setProjectKey` (a browsed key equal to the project key follows it, so
///   the palette stays home; any other browsed key is left alone). With `transposeWithKey` on and a tonic
///   change, the track notes move with the chords in the same undo step (D18, see `setProjectKey`); with it
///   off, or for a mode-only change, no note moves.
/// - `browseKey` with exactly one of `{"steps":n}` (`fifthsNeighbour` of the current browsed key),
///   `{"mode":"<Mode>"}` (the parallel mode on the same tonic pitch class, canonically spelled) or
///   `{"home":true}` (browsed key = project key).
/// - `appendChord {root,type,lengthBeats?,velocity?,inversion?}`: `appendChord`, with velocity 100 and,
///   without `lengthBeats`, the document's new-chord length (`Document::newChordLength`: `newChordBeats` when
///   set, else one bar of the meter, barTicks(meter) / 960 beats: 4 in 4/4, 3.5 in 7/8). With `inversion` (an integer in 0..tones - 1 of the chord, else rejected naming the field) the
///   new chord has that inversion and is manual, so it sounds as given even under auto voice-leading;
///   without, root position and not manual. The new chord is selected (document.h), so the next
///   snapshot's `selectedEventId`, `suggestions` and `relationReference` follow it. Refused (false, an
///   error naming the reason, no change) when the document already holds `kMaxChords` chords or the id
///   counter is exhausted, and, like every chord timing command, by the track invariant (below).
/// - `moveChord {id,index}`, `setLength {id,lengthBeats}`, `setVelocity {id,velocity}`,
///   `removeChord {id}`: the matching store call. An unknown id or a move to the current place is a valid no-op.
/// - **Chord timing commands carry the notes** (tracks design D3): `appendChord`, `insertChords`, `loadPreset`,
///   `moveChord`, `setLength`, `setLengths`, `removeChord` and `removeChords` move each note with its chord (a
///   shorter chord leaves the notes past its end inactive, a longer one reactivates them), a removed chord takes
///   its notes along and a "replace" preset removes every note, keeping the tracks (document.h). Each is refused
///   (false, no change, no history, `documentChanged` false) when the result would break the track invariant,
///   with the store's reason in the error: `"<command>" refused: <reason>`, in chord terms (D17a), for example
///   `"setLength" refused: That change would leave more than 8 track notes sounding at once`, `... would leave
///   more than 15 track notes starting within one beat` or `... would move a track note past the end of what
///   ChordLab can hold` (the numbers are `kMaxTrackPolyphony` and `kMaxTrackOnsetsPerBeat`; any other
///   `validateTracks` failure keeps its text). The note commands keep the validator's per-note messages.
/// - `setProgressionLoop {on}`, `setFollowHost {on}`: the matching live-state setter.
/// - `setInversion {id,inversion}`, `setOctave {id,octave}`, `resetVoicing {id}`: the matching store
///   call (the values are clamped by the store and the event is marked manual; see document.h).
/// - `setAutoVoiceLead {on}`: `setAutoVoiceLead`, undoable.
/// - `setTransposeWithKey {on}`, `setKeyHintSeen {on}`, `setMidiThru {on}`: the matching live-state setter
///   (never history, kept across undo and redo, persisted by `stateJson`).
/// - `setFeel {rhythm,strum,strumSpeed,humanise[,arpPattern,arpRate,arpRange]}`: the first four required strings
///   (names as in the snapshot), the arpeggio three optional (up, subdivision, one when missing; a present one
///   must be a valid name or the message is rejected). `rhythm` also accepts the legacy `arpUp` and `arpUpDown`
///   (builds before the patterns): `arpeggio` with pattern up / upDown unless `arpPattern` is given;
///   `setMeter {num,den}`: JSON integers (3.0 and "3" are rejected like any integer field), `num` 1..16, `den`
///   2, 4 or 8. Both are undoable (`DocumentStore::setFeel` / `setMeter`); an equal value pushes no history.
/// - `transposeFromKey {fromTonicPc}` (0..11): `transposeFromKey`, every chord moved from that tonic to the
///   project tonic, one undo step; `transposeProgression {semitones}` (-11..11): `transposeProgression`,
///   every chord moved by the signed interval, one undo step. Both carry the track notes in the same step
///   (pitches only; D18): `transposeFromKey` by the interval the chords take, `transposeProgression` by
///   exactly `semitones`. Neither changes the key. A move of nothing (interval 0, no chords) is a valid no-op
///   that pushes no history and leaves the revision. An out-of-range value is rejected.
///   With `transposeWithKey` on, `setKey` also transposes the chords and the notes (document.h), in the
///   same undo step.
/// - `addSection {index,name?}`: `addSection(index, name)`; `name` defaults to the store's "Section <n>"
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
///   `selectedEventId()` and `selectedEventIds()` to `snapshotJson` so that the snapshot's `selectedEventId`,
///   `selectedEventIds` and `suggestions` follow it.
/// - `selectEvents {ids, anchor?}`: `setSelectedEvents`, live state; `ids` an array of integers (empty clears;
///   unknown ids are dropped), `anchor` an integer or null (absent or null: the last selected block in strip
///   order).
/// - `insertChords {index, chords}` (strip editing design D1): `insertChords(index, chords)`, ONE undo step;
///   `index` an integer (clamped to 0..size by the store); `chords` a non-empty array of at most `kMaxChords`
///   objects `{root, type, lengthBeats?, velocity?, inversion?, octave?, manualVoicing?, notes?}`: `root` and `type` as
///   for `appendChord`, `lengthBeats` a finite number (the new-chord length when absent), `velocity` an integer,
///   `inversion` an integer in 0..tones - 1 of the chord, `octave` an integer in -1..1, `manualVoicing` a
///   boolean (a given inversion or octave makes the chord manual unless it says otherwise), `notes` the chord's
///   track notes (below, D21). Every entry is
///   validated before anything changes, and an error names the field with its index ("chords[2].root must be an
///   integer in 0..11"). The inserted chords become the selection, the last the anchor. Refused (false, an
///   error naming the reason, no change, no id consumed) when the result would hold more than `kMaxChords`
///   chords or the id counter cannot hand out one id per chord, and by the track invariant (above).
/// - `removeChords {ids}`, `setLengths {ids, lengthBeats}`: the store calls, ONE undo step each; `ids` a
///   non-empty array of integers (repeats count once; ids no event has are skipped, and a call that finds none,
///   or changes nothing, is a valid no-op that pushes no history); `lengthBeats` a finite number, clamped by
///   the store. Both can be refused by the track invariant (above).
/// - `setNewChordLength {lengthBeats}`: `setNewChordLength`, live state (never history, kept across undo and
///   redo, persisted by `stateJson`); `lengthBeats` (required) a finite number, snapped to the quarter-beat grid
///   and clamped to 0.25..64 by the store, or null for one bar of the meter.
/// - `setRelationMode {mode}`: `setRelationMode`, live state (never history, kept across undo and redo,
///   persisted by `stateJson`); `mode` is one of "off", "commonTones", "voiceLeading", "function",
///   "blend" (`relationModeName`, relatedness.h). A missing, non-string or unknown `mode` is rejected with
///   an error that lists the five.
/// - `loadPreset {id, mode}`: `id` a preset id of the snapshot's `presets` (exact, case-sensitive; an
///   unknown id is rejected with an error naming it), `mode` "append" or "replace" (anything else, or
///   none, is rejected with an error listing both). The preset is resolved in the project key
///   (`resolvePreset`) and loaded as ONE undoable step: "append" is `appendProgression` (after the last
///   chord, in a new section named after the preset; onto an unsectioned non-empty progression the
///   chords already there first get a default "Section <n>" at 0), "replace" is `replaceProgression`
///   (every chord and section replaced, one section at 0 named after the preset; keys and live state
///   untouched, history kept). Either way the loaded chords become the selection, the last of them the
///   anchor. Refused (false, an error
///   naming the reason, no change, no id consumed) for the store's reasons (`progressionRefusal`,
///   document.h): the result would hold more than `kMaxChords` chords, or the event or section id
///   counter cannot hand out the ids the load needs; and by the track invariant (above). "append" keeps every
///   note, "replace" removes every note of every track (the tracks stay).
/// - **Tracks and notes** (tracks design D4, D5, D21; tracks.h, document.h). Integers: `trackId`, `id`, `index`,
///   `pitch`, `velocity` and the entries of `ids` and `scope` are JSON integers that must fit an `int` (2147483648 is
///   an error, not a wrapped value); the tick fields `start`, `length` and `offset` are read at 64 bits and may be any JSON
///   integer that fits an `int64_t` (9223372036854775808 and -9223372036854775809 are errors), which the store then
///   clamps into D1's domain (start first, then the length against the space left); `pitch` and `velocity` are
///   clamped by the store, as an `insertChords` velocity is. Floats, strings and null are never integers. Every
///   error names the field with its index ("notes[2].start must be an integer in -9223372036854775808..
///   9223372036854775807", "chords[1].notes[0].lane.role must be \"bass\", \"lead\" or \"other\"", "missing
///   notes[1].pitch").
///   - `addTrack {role, name?}`: `addTrack`, one undo step. `role` is the lowercase string "bass", "lead" or "other"
///     (as the snapshot will publish roles); `name` a string of at most `kMaxTrackNameBytes` UTF-8 bytes (longer is
///     rejected, not cut: as a section name), empty or absent for the role's default name.
///   - `removeTrack {id}`, `patchTrack {id, name?, role?}`, `moveTrack {id, index}`: the store calls, one undo step
///     each (`name` bounded as above, empty allowed and stored empty; `index` clamped by the store). `patchTrack`
///     with neither field is the store's refusal ("Give a name or a role"); a patch that changes nothing, and a move
///     to the current place, are valid no-ops.
///   - `setTrackMute {id, on}`, `setTrackSolo {id, on}`, `setChordsMute {on}`, `setChordsSolo {on}`: live state (no
///     history, no revision, kept by undo and redo), persisted, so a toggle that changes a flag sets
///     `documentChanged`. The value already held is a valid no-op; an `id` no track has is a refusal ("No track with
///     id 9"), since the store's return cannot tell the two apart.
///   - `insertNotes {trackId, notes, baseRevision?}`: `insertNotes`, one undo step; the inserted notes become the
///     note selection. `notes` is an array of at most `kMaxNotesPerTrack` objects `{pitch, start, length,
///     velocity?}` (`velocity` 100 when absent; `start` the absolute tick, resolved to its chord and offset by the
///     store); an empty array is the store's refusal ("No notes to insert").
///   - `removeNotes {trackId, ids, baseRevision?}`: `removeNotes`; `ids` at most `kMaxNotesPerTrack` integers (a longer
///     list is an error before the store is touched); unknown ids are skipped and an empty list is a valid no-op.
///   - `updateNotes {trackId, notes, baseRevision?}`: `updateNotes`, one undo step; `notes` at most `kMaxNotesPerTrack`
///     objects `{id, pitch?, start?, length?, velocity?}` with absolute values (an absent field keeps the note's, null
///     is an error); an empty array is a valid no-op, an unknown or repeated id the store's refusal.
///   - `generateBass {trackId, pattern, scope?}` (D16): `DocumentStore::generateBass`, ONE undo step: the notes the
///     scope's chords own in the track are replaced by the bass line `pattern` gives them (generate.h), anchored to
///     their chords; notes of other chords and other tracks stay. `pattern` is one of "roots", "rootPulse",
///     "rootFifth", "octaves", "followFeel", "walking" (`bassPatternName`; missing is `missing "pattern"`, anything
///     else, a wrong case or type included, `"pattern" must be "roots", "rootPulse", "rootFifth", "octaves",
///     "followFeel" or "walking"`). `scope` is absent or an array of at most `kMaxChords` chord ids (integers as
///     `ids`, errors naming the entry: "scope[2] must be an integer"; longer is an error before the store is
///     touched); absent or empty means every chord, a repeat counts once. No `baseRevision` (not a drag; one sent is
///     ignored, as any unused field). The generated notes are not selected. Store refusals, as
///     `"generateBass" refused: <reason>`: "No track with id 9", "Add chords first", "No chord with id 42" (a scope id
///     no chord has), "No note ids are left", and the track invariant's own words ("At most 1024 notes per track", "At
///     most 8 track notes can sound at once", "At most 15 notes can start within one beat across all tracks"). Nothing
///     owned and nothing generated (Follow feel finding no bass-class hit) is a valid no-op, `documentChanged` false;
///     generating the same pattern again is a new step (fresh note ids). Walking reads the document's Loop when it
///     runs: the line is baked.
///   - `generateLead {trackId, pattern, scope?, variation?}` (D22): `DocumentStore::generateLead`, ONE undo step, the
///     same transaction as `generateBass` with the lead line `pattern` gives (generate.h): the line runs over the whole
///     progression and the scope's chords keep their part of it. `pattern` is one of "heldTone", "guideTones",
///     "arpeggio", "stepwise", "motif", "followFeel" (`leadPatternName`; missing is `missing "pattern"`, anything else,
///     a bass pattern's name, a wrong case or type included, `"pattern" must be "heldTone", "guideTones", "arpeggio",
///     "stepwise", "motif" or "followFeel"`). `scope` exactly as `generateBass`'s. `variation` is absent (0) or a JSON
///     integer in 0..2147483647, read at 64 bits before narrowing: a negative, boolean, null, string, fractional
///     representation (`1.0` and `-0.0` included) or larger value is `"variation" must be an integer in
///     0..2147483647`; `-0`, written without a fraction or exponent, is the JSON integer 0 and accepted as 0. Motif
///     reduces it modulo 64 (64 takes: 73 plays as 9); any other pattern with a non-zero variation is the store's
///     refusal `"generateLead" refused: variation is only supported for motif`. No `baseRevision` (ignored, as for
///     `generateBass`). The generated notes are not selected. Store refusals, as `"generateLead" refused: <reason>`, are
///     `generateBass`'s with the same words ("No track with id 9", "Add chords first", "No chord with id 42", "No note
///     ids are left", the track invariant's words) plus the variation refusal above, which is checked first. Nothing
///     owned and nothing generated (Follow feel finding no top-class hit) is a valid no-op, `documentChanged` false;
///     generating again is a new step (fresh note ids). Loop never changes a lead line.
///   - `baseRevision` (optional, on `insertNotes`, `removeNotes` and `updateNotes`): a non-negative JSON integer
///     (read as `uint64_t`; negative, fractional or beyond 2^64 - 1 is an error). When present and not equal to
///     `DocumentStore::revision()` the command is refused before anything else, store untouched, no history,
///     `documentChanged` false: `"insertNotes" refused: The notes changed while you were dragging` (the wire name of
///     the command). Absent: no check. The check lives in this layer: the store is not called for a stale command (it
///     has no notion of a base revision).
///   - `selectNotes {trackId, ids}`: `setNoteSelection`, live state (never history, never the revision, never
///     `documentChanged`); `ids` at most `kMaxNotesPerTrack` integers, unknown ones dropped, repeats collapsed, the rest
///     in the track's note order; an empty result clears. `trackId` is an integer or null: null clears the selection
///     whatever `ids` holds (the documented form; the integer -1, `kNoTrackId`, is the same value and clears too),
///     and a track no track has is a no-op, as `selectEvent` with an unknown id.
///   - `insertChords` entries (above) take an optional `notes` array of objects `{lane, offset, length, pitch,
///     velocity?}` (D21, the chord clipboard): `lane` is `{trackId?, name, role}` (`trackId` an integer, null or absent;
///     `name` at most `kMaxTrackNameBytes` bytes; `role` as for `addTrack`), `offset` the ticks after the chord's
///     start (`offset` and `length` 64-bit), `pitch` and `velocity` as above. All the notes of one message together
///     number at most `kMaxTracks * kMaxNotesPerTrack` (an error beyond, naming the entry, before the store is
///     touched). The store resolves each lane inside the one commit: the track with that id when it exists, else the
///     unique track with that name and role, else a new track (one per absent name and role), anchoring each note to
///     the chord it comes with, offsets past the chord's end carried as inactive notes. Refused, with the store's
///     reason, when a name and role match more than one track, when a new track would pass `kMaxTracks`, or when the
///     track invariant fails.
///   Every store refusal above is `"<command>" refused: <reason>` with no change, no history and `documentChanged`
///   false; every accepted command that changes content or a persisted flag sets `documentChanged`, `selectNotes` and
///   no-ops do not.
/// - `undo`, `redo`: no fields. A browsed key that is the project key stays so when they restore another
///   project key (document.h), so the UI never has to follow them with `browseKey {home}`.
///
/// Every field is validated before anything is touched: an unknown `cmd`, a missing or mistyped field, a
/// root outside 0..11, an unknown chord type, mode or preset, a bad letter or alter, a non-finite number or a
/// section name over the bound makes the call return false with a description in `error`, leaving the
/// document, its history and the selection exactly as they were. Fields the command does not use are ignored. A
/// top-level `lineage` belongs to the plug-in shell (the document lineage it checks before calling this, tracks plan
/// Task 24a) and is ignored here like any unused field, whatever its type. Numbers are not
/// range-checked beyond that: lengths, velocities, inversions, octaves, colours and move targets are
/// clamped by the store, exactly as the direct calls do.
///
/// Returns true for every accepted command (a refusal by the store, listed above, is not one), including ones that
/// change nothing: an edit naming an id no
/// event or section has, a move to the chord's current position, and `undo`/`redo` with nothing to undo
/// or redo are valid no-ops, not errors. `error` is cleared on success.
///
/// `documentChanged`, when given, is set on every call: true when `store.doc()` differs from before the
/// command (compared field by field, live state included: everything `stateJson` persists apart from the
/// editor size and the id counters, which only move with the document), false for a rejected command, for
/// an accepted no-op, for a setter given the value already held, and for the selection commands (the
/// selection is not part of the document). It is what the plug-in forwards to the host as "the saved state
/// changed", so a set whose only edits are ChordLab's gets saved.
bool applyCommandJson(DocumentStore& store, std::string_view json, std::string& error,
                      bool* documentChanged = nullptr) noexcept;

/// The persistent form of the store's document, without the palette:
/// ```
/// {"schema":1, "projectKey":<key>, "browsedKey":<key>, "progressionLoop":true, "followHost":true,
///  "autoVoiceLead":false, "transposeWithKey":false, "keyHintSeen":false, "midiThru":true, "newChordBeats":null, "relationMode":"blend",
///  "feel":{"rhythm":"block","strum":"off","strumSpeed":"medium","humanise":"off","arpPattern":"up",
///          "arpRate":"subdivision","arpRange":"one"}, "meter":{"num":4,"den":4},
///  "events":[{"id":1,"root":0,"type":"maj","lengthBeats":4.0,"velocity":100,
///             "inversion":0,"octave":0,"manualVoicing":false}, ...],
///  "sections":[{"id":1,"name":"Verse","colourIndex":0,"firstEventIndex":0}, ...],
///  "tracks":[{"id":1,"name":"Bass","role":"bass","mute":false,"solo":false,
///             "notes":[{"id":1,"pitch":36,"anchor":1,"offset":0,"length":960,"velocity":100}, ...]}, ...],
///  "chordsMuted":false, "chordsSolo":false,
///  "nextId":2, "nextSectionId":2, "nextTrackId":2, "nextNoteId":2, "editor":{"width":900,"height":620}}
/// ```
/// `nextId` is the store's id counter (`DocumentStore::nextId`), so that a reloaded document keeps
/// handing out ids no saved event ever had, and `nextSectionId` the section counter likewise.
/// `editorWidth` / `editorHeight` are the editor window size to remember; they are stored verbatim. The
/// selection (`DocumentStore::selectedEventId`) is live state and is not written.
/// `autoVoiceLead`, the events' `inversion`, `octave` and `manualVoicing`, `sections` and
/// `nextSectionId` were added under schema 1 (Phase 2), `relationMode` (`relationModeName` of
/// `Document::relationMode`, live state that is nonetheless saved with the set, R5) later, and
/// `transposeWithKey` and `keyHintSeen` (also live state) after it, and `midiThru` (live state) after
/// those, and `newChordBeats` (live state, the length of a chord added without one, null for one bar of the meter) after that: a reader that predates them ignores them, and `loadStateJson` defaults them when they are
/// missing, so states cross both ways. `tracks`, `chordsMuted`, `chordsSolo`, `nextTrackId` and `nextNoteId` (tracks
/// design D7) came after all of them: the tracks in order with their live `mute` and `solo`, each note as stored
/// (`anchor` its chord's id, `offset` and `length` in ticks, no derived start), and the two counters
/// (`DocumentStore::nextTrackId` / `nextNoteId`) so ids stay unused after a reload; the note selection and the revision
/// are live and not written. A build from before tracks ignores these keys and drops the tracks on its next save.
/// Size: the largest state the store can hold (`kMaxChords` chords, a section on each, every section and track name at
/// its byte bound of control characters, which escape to six bytes each, `kMaxTracks` tracks of `kMaxNotesPerTrack`
/// inactive notes with eight-digit offsets and seven-digit lengths, every id ten digits) is pinned by
/// test_json_api.cpp under `kMaxJsonBytes`, so a saved state always loads.
///
/// The instance block (per-track instruments plan I11). With `instance == nullptr` the text is exactly the above,
/// byte for byte. With an instance it is that text with one more top-level member appended after everything else
/// (the closing brace moves after it; every other byte stays as it is, so removing `,"instance":{...}` gives the text
/// without it):
/// ```
/// "instance":{"doc":"<documentId>","follow":null,"id":"<id>","lanes":{"1":"<uid>","2":"<uid>"}}
/// "instance":{"doc":"...","follow":{"doc":"<documentId>","lane":"chords"|"<uid>","laneName":"...",
///             "leader":"<leaderId>","leaderLabel":"...","role":"..."},"id":"...","lanes":{}}
/// ```
/// Inside the block members are in nlohmann's order (sorted by name), as in every other object here; `lanes` maps
/// each track number, as a decimal string, to its uid (`InstanceState::lanes`); `follow` is null without a follow
/// and otherwise writes all six fields of `LinkFollow`, the hints as they are (`laneRole` as `role`). The block is
/// written as given: the caller passes one that `loadStateJson` accepts (the rules there; `LinkEndpoint::state`
/// gives such a block when its uid generator keeps to them), since a state whose block breaks a rule does not load
/// at all. Size: the largest valid block on the largest state is pinned by test_json_api.cpp under `kMaxJsonBytes`.
std::string stateJson(const DocumentStore& store, int editorWidth = kDefaultEditorWidth,
                      int editorHeight = kDefaultEditorHeight, const InstanceState* instance = nullptr);

/// Restores a document saved by `stateJson`. Atomic: the whole text is parsed and validated first; on any
/// problem (malformed JSON, wrong schema, a missing or mistyped field, an unknown chord type or mode, a
/// root outside 0..11, an id below 1 or repeated, more than `kMaxChords` events, a `nextId` below 1 or
/// not above every event id, a non-positive editor size, or sections that are not `sectionsWellFormed`
/// (document.h): not starting at 0, unsorted or sharing a start, an index outside the progression, a
/// section id outside 1..INT_MAX-1 or repeated, a name over `kMaxSectionNameBytes` bytes; or a
/// `nextSectionId` below 1 or not above every section id; a malformed track or note, or tracks that fail
/// `validateTracks`, below) it returns false with a description in `error` and the store, its history, its selection and the
/// out-params are untouched. Otherwise it replaces the store's document and its four id counters
/// (`DocumentStore::replace`, which clears the undo/redo history and both selections and moves the revision), writes the editor
/// size into the out-params that are non-null, and returns true with `error` cleared.
/// Lengths, velocities, inversions, octaves and section colours are clamped to the store's ranges
/// rather than rejected (a mistyped value is still rejected). Optional, with their defaults: `editor`
/// (`kDefaultEditorWidth` x `kDefaultEditorHeight`), the events' `inversion` (0), `octave` (0) and
/// `manualVoicing` (false), `feel` (block chords; its `arpPattern`, `arpRate` and `arpRange` optional too, and the legacy `arpUp` /
/// `arpUpDown` rhythm names accepted) and `meter` (4/4; when given, valid as in `setFeel` /
/// `setMeter`, else the whole state is rejected), `autoVoiceLead` (false), `transposeWithKey` and
/// `keyHintSeen` (false; when given, booleans), `midiThru` (true; when given, a boolean), `newChordBeats` (null; when given, null or a finite number, snapped and clamped as `snapNewChordBeats` does), `relationMode`
/// ("blend"; when given, one of the five `relationModeName` strings, anything else rejecting the state),
/// `sections` (none) and `nextSectionId`
/// (derived, one past the largest section id, 1 without sections; when given it must be at least that and
/// is kept, like `nextId`), `tracks` (none), `chordsMuted` and `chordsSolo` (false; when given, booleans), and
/// `nextTrackId` / `nextNoteId` (derived, one past the largest track / note id, 1 without; when given, at least that
/// and kept, like `nextSectionId`). Every other field is required.
/// Tracks: `tracks` must be an array of objects with every member `stateJson` writes: `id` an integer in
/// 1..INT_MAX-1, `name` a string, `role` "bass", "lead" or "other", `mute` and `solo` booleans, `notes` an array of
/// objects `{id, pitch, anchor, offset, length, velocity}`: `id` an integer in 1..INT_MAX-1, `pitch`, `anchor` and
/// `velocity` integers that fit an `int`, `offset` and `length` read at 64 bits and required to fit an `int32_t`
/// (anything else, 2^63 included, is an error naming the field, never a wrapped value). Nothing of a track is clamped:
/// after the chords are read and clamped, the candidate document runs `validateTracks` (tracks.h) with the candidate
/// counters (given or derived), and any failure (a note anchored to no chord, a pitch, velocity, offset or length
/// outside D1's domain, repeated ids, notes out of their (start, pitch, id) order, a name over `kMaxTrackNameBytes`,
/// more than `kMaxTracks` tracks or `kMaxNotesPerTrack` notes, or the playback limits over the active notes of every
/// track, muted ones included) rejects the whole state as `"tracks" rejected: <validateTracks' reason>`.
///
/// The instance block (`instance`, optional; see `stateJson`). On success `*instance` (when `instance` is non-null)
/// is set: to the block when the state has one, else to empty (a state saved before linking, or by a caller that
/// gave none: the caller then keeps its fresh ids and no link). A block is validated whether or not `instance` is
/// given, and an invalid one rejects the whole state like every other malformed field (false, an error naming the
/// field, the store and every out-param untouched). Valid means, with every error naming the field as shown:
/// - `instance` an object (`"instance" must be an object`);
/// - `id` and `doc` (into `documentId`) required ids: a string of 1..`kMaxLinkIdBytes` bytes, each printable ASCII
///   0x21..0x7E, so no space and no control character (`missing instance.id`, `instance.id must be a string of 1..64
///   printable ASCII characters (0x21..0x7E)`, likewise `instance.doc`);
/// - `lanes` a required object (`instance.lanes must be an object`) of at most `kMaxTracks + kMaxVanishedLaneUids`
///   members (`instance.lanes must hold at most 68 entries`) whose keys are track numbers written canonically,
///   decimal digits only, no sign, no leading zero, in 1..INT_MAX-1 (`instance.lanes keys must be track numbers in
///   1..2147483646, decimal without sign or leading zeros`: "01", "-1", "+1", "1.0", " 1", "x", "0" and
///   "2147483647" are all refused) and whose values are ids as above (`instance.lanes.<n> must be a string of ...`),
///   never `"chords"` (`instance.lanes.<n> must not be "chords"`) and pairwise different (`instance.lanes.<n>
///   repeats the uid of instance.lanes.<m>`, members visited in nlohmann's order, by key string);
/// - `follow` required, null or an object (`instance.follow must be null or an object`): `leader` (into `leaderId`),
///   `doc` (into `documentId`) and `lane` (into `laneUid`; `"chords"` passes as an id) required ids as above
///   (`missing instance.follow.leader`, `instance.follow.leader must be a string of ...`); `laneName`, `role` (into
///   `laneRole`) and `leaderLabel` optional strings, empty when absent, of at most `kMaxTrackNameBytes`, 16 and
///   `kMaxLinkLabelBytes` bytes (`instance.follow.laneName must be a string of at most 64 bytes`, and so on); the
///   hints are not checked further (any UTF-8 the parser accepted).
/// Members the block, or its `follow`, does not know are ignored (forward compatibility), as elsewhere. Member order
/// means nothing to the reader; a member given twice takes its last value (nlohmann's policy, as above), so two
/// `instance` members are the last one, and a track number written twice in `lanes` is its last uid.
bool loadStateJson(DocumentStore& store, std::string_view json, std::string& error, int* editorWidth = nullptr,
                   int* editorHeight = nullptr, std::optional<InstanceState>* instance = nullptr) noexcept;

/// The link as the editor shows it (per-track instruments plan I12), from one `LinkView` (link.h):
/// ```
/// {"schema":1,
///  "self":{"id":"<id>","label":"ChordLab A"},
///  "following":null | {"leader":"<id>","leaderLabel":"...","lane":"chords"|"<uid>","laneName":"...","role":"...",
///                      "status":"playing"|"muted"|"waiting"|"laneMissing"|"projectChanged",
///                      "switchTo":null|{"leader":"<id>","label":"..."}},
///  "leaders":[{"id":"...","label":"...","lineage":"...",
///              "lanes":[{"lane":"chords"|"<uid>","name":"...","role":"...","chords":true,"audible":true,
///                        "followers":0}, ...]}, ...],
///  "routed":[{"lane":"<uid>","followers":2}, ...]}
/// ```
/// `self` is `view.id` and `view.label`. `following` is null when `view.follow` is empty or `view.status` is
/// `NotFollowing` (no registry view has one without the other); otherwise its fields are the follow's `leaderId`,
/// `leaderLabel`, `laneUid`, `laneName` and `laneRole`, `status` names `view.status`, and `switchTo` is
/// `view.switchTo` (its `id` as `leader`) or null. `leaders` and `routed` are `view.leaders` and `view.routed` in
/// their order, each lane's `lane` its uid and `role` its `trackRoleName`, empty for the chords lane (whose role
/// means nothing, as the registry's hints have it). Member order inside objects is nlohmann's (sorted by name).
/// Strings are written as UTF-8; invalid UTF-8 in any of them (a name, a label) is replaced by U+FFFD, never thrown
/// on (`error_handler_t::replace`, as the shell's error messages). Allocates: the UI/message thread only.
std::string linkStateJson(const LinkView& view);

}  // namespace chordlab
