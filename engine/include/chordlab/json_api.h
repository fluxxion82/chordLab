#pragma once

#include "chordlab/document.h"
#include "chordlab/palette.h"

#include <string>
#include <string_view>

namespace chordlab {

/// Version of the wire format. Every message (snapshot, command, state) carries `"schema": kJsonSchema`;
/// a message with a different or missing schema is rejected.
constexpr int kJsonSchema = 1;

/// Editor window size written by `stateJson` when the caller gives none, and assumed by `loadStateJson`
/// when the state has no `editor` object.
constexpr int kDefaultEditorWidth = 900;
constexpr int kDefaultEditorHeight = 620;

/// Common encodings
/// ----------------
/// A key is `{"tonic":{"letter":"C","alter":0},"mode":"Ionian"}`: `letter` is a one-character string
/// 'A'..'G', `alter` -1/0/1, `mode` the `Mode` enumerator name (Ionian, Dorian, Phrygian, Lydian,
/// Mixolydian, Aeolian, Locrian, HarmonicMinor, MelodicMinor). A chord is a `root` pitch class 0..11
/// plus a `type`, the stable dictionary id of chord.h ("maj", "m7b5", ...). A key name is the spelled
/// tonic plus the mode name: "C Ionian", "Bb HarmonicMinor".
/// Beats and lengths are JSON numbers, ids and velocities integers. Strings are UTF-8 (symbols contain
/// the literal `Δ` and `°`).

/// The document as the UI shows it, with the given palette (normally
/// `buildPalette(doc.browsedKey, doc.projectKey)`), so that the UI never computes theory:
/// ```
/// {"schema":1,
///  "projectKey":<key>, "browsedKey":<key>, "projectKeyName":"C Ionian", "browsedKeyName":"G Ionian",
///  "progressionLoop":true, "followHost":true, "endBeat":4.0,
///  "events":[{"id":1,"root":0,"type":"maj","symbol":"C","roman":"I","startBeat":0.0,
///             "lengthBeats":4.0,"velocity":100,"voicing":[60,64,67]}, ...],
///  "palette":{"tiers":5,
///             "cells":[{"degree":0,"tier":1,"root":0,"type":"maj","symbol":"C","roman":"I",
///                       "family":"Major","filled":true,"voicing":[60,64,67]}, ...]}}
/// ```
/// Events are in playback order with `startBeat` accumulated as `Document::startBeatOf` does; `symbol` is
/// the Klimper-style symbol spelled in the project key, `roman` the Standard-style numeral relative to the
/// project key, `voicing` the default `closeVoicing`. Palette cells carry the palette's own `symbol`
/// (spelled in the browsed key) and `roman`, the type's `family` ("Major", "Minor" or "Other") and the
/// same `voicing`, in the palette's order.
std::string snapshotJson(const Document& doc, const Palette& palette);

/// Applies one UI command, `{"schema":1,"cmd":"<name>", ...fields}`, to the store:
/// - `setKey {letter,alter,mode}`: `setProjectKey` (the browsed key is left alone).
/// - `browseKey` with exactly one of `{"steps":n}` (`fifthsNeighbour` of the current browsed key),
///   `{"mode":"<Mode>"}` (the parallel mode on the same tonic pitch class, canonically spelled) or
///   `{"home":true}` (browsed key = project key).
/// - `appendChord {root,type,lengthBeats?,velocity?}`: `appendChord`, with the store's defaults
///   (4 beats, velocity 100) for the optional fields.
/// - `moveChord {id,index}`, `setLength {id,lengthBeats}`, `setVelocity {id,velocity}`,
///   `removeChord {id}`: the matching store call.
/// - `setProgressionLoop {on}`, `setFollowHost {on}`: the matching live-state setter.
/// - `undo`, `redo`: no fields.
///
/// Every field is validated before anything is touched: an unknown `cmd`, a missing or mistyped field, a
/// root outside 0..11, an unknown chord type or mode, a bad letter or alter, or a non-finite number makes
/// the call return false with a description in `error`, leaving the document and its history exactly as
/// they were. Fields the command does not use are ignored. Numbers are not range-checked beyond that:
/// lengths, velocities and move targets are clamped by the store, exactly as the direct calls do.
///
/// Returns true for every accepted command, including ones that change nothing: an edit naming an id no
/// event has, a move to the chord's current position, and `undo`/`redo` with nothing to undo or redo are
/// valid no-ops, not errors. `error` is cleared on success.
bool applyCommandJson(DocumentStore& store, std::string_view json, std::string& error);

/// The persistent form of the store's document, without the palette:
/// ```
/// {"schema":1, "projectKey":<key>, "browsedKey":<key>, "progressionLoop":true, "followHost":true,
///  "events":[{"id":1,"root":0,"type":"maj","lengthBeats":4.0,"velocity":100}, ...],
///  "nextId":2, "editor":{"width":900,"height":620}}
/// ```
/// `nextId` is the store's id counter (`DocumentStore::nextId`), so that a reloaded document keeps
/// handing out ids no saved event ever had. `editorWidth` / `editorHeight` are the editor window size to
/// remember; they are stored verbatim.
std::string stateJson(const DocumentStore& store, int editorWidth = kDefaultEditorWidth,
                      int editorHeight = kDefaultEditorHeight);

/// Restores a document saved by `stateJson`. Atomic: the whole text is parsed and validated first; on any
/// problem (malformed JSON, wrong schema, a missing or mistyped field, an unknown chord type or mode, a
/// root outside 0..11, an id below 1 or repeated, a `nextId` below 1 or not above every event id, a
/// non-positive editor size) it returns false with a description in `error` and the store, its history
/// and the out-params are untouched. Otherwise it replaces the store's document and id counter
/// (`DocumentStore::replace`, which clears the undo/redo history), writes the editor size into the
/// out-params that are non-null, and returns true with `error` cleared.
/// Lengths and velocities are clamped to the store's ranges rather than rejected. `editor` is optional
/// and defaults to `kDefaultEditorWidth` x `kDefaultEditorHeight`; every other field is required.
bool loadStateJson(DocumentStore& store, std::string_view json, std::string& error, int* editorWidth = nullptr,
                   int* editorHeight = nullptr);

}  // namespace chordlab
