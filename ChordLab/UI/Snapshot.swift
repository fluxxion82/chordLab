import Foundation

/// Codable mirror of the engine's snapshot JSON (`snapshotJson` in engine/include/chordlab/json_api.h,
/// written by engine/src/json_api.cpp). The UI never computes theory: everything it shows is in here.
/// Fields the engine added under schema 1 (`modes`, `tonics`, `outOfKey`) are optional so that a UI
/// built against this file still decodes a snapshot from an older plug-in build.

/// `{"letter":"C","alter":0}`: a spelled tonic, `alter` -1 flat, 0 natural, 1 sharp.
struct TonicJSON: Codable, Equatable {
  let letter: String
  let alter: Int
}

/// One entry of the snapshot's `tonics` table: the canonical tonic of a pitch class in a mode, with its
/// ASCII spelling (`"Db"`, `"F#"`).
struct TonicNameJSON: Codable, Equatable {
  let letter: String
  let alter: Int
  let text: String
}

/// `{"tonic":{...},"mode":"Ionian"}`; `mode` is the engine's `Mode` enumerator name.
struct KeyJSON: Codable, Equatable {
  let tonic: TonicJSON
  let mode: String
}

/// One chord of the progression, in playback order. `symbol` and `roman` are spelled in the project key;
/// `family` is the type's "Major", "Minor" or "Other" (the sequence bar's colour); `voicing` is the
/// default close voicing as MIDI note numbers; `outOfKey` the chord's pitch classes outside the project
/// key's scale (ascending, no repeats). `family` and `outOfKey` are absent from older plug-in builds.
struct EventJSON: Codable, Equatable, Identifiable {
  let id: Int
  let root: Int
  let type: String
  let symbol: String
  let roman: String
  let family: String?
  let startBeat: Double
  let lengthBeats: Double
  let velocity: Int
  let voicing: [Int]
  let outOfKey: [Int]?
}

/// One palette cell: the chord on scale degree `degree` (0..6) of the browsed key in row `tier`.
/// `filled` is false for a borrowed chord (a note outside the project key); `family` is "Major", "Minor"
/// or "Other"; `outOfKey` lists the borrowed pitch classes (empty exactly when `filled`; absent from
/// older plug-in builds).
struct CellJSON: Codable, Equatable, Identifiable {
  let degree: Int
  let tier: Int
  let root: Int
  let type: String
  let symbol: String
  let roman: String
  let family: String
  let filled: Bool
  let voicing: [Int]
  let outOfKey: [Int]?

  /// Grid position; unique within a palette (one cell per tier and degree).
  var id: String { "\(tier)/\(degree)" }
}

/// `tiers` is one more than the highest tier present; `cells` holds only the cells that exist, ordered by
/// tier then degree.
struct PaletteJSON: Codable, Equatable {
  let tiers: Int
  let cells: [CellJSON]
}

/// The whole document as the UI shows it, plus the palette of the browsed key.
struct Snapshot: Codable, Equatable {
  let schema: Int
  let projectKey: KeyJSON
  let browsedKey: KeyJSON
  let projectKeyName: String
  let browsedKeyName: String
  let progressionLoop: Bool
  let followHost: Bool
  let endBeat: Double
  /// Every engine mode name, in enum order: the mode picker's items.
  let modes: [String]?
  /// Per mode name, the canonical tonic of each pitch class 0..11: the tonic picker's items and the
  /// spelling `setKey` is sent with.
  let tonics: [String: [TonicNameJSON]]?
  let events: [EventJSON]
  let palette: PaletteJSON

  /// Whether the key pickers can be built from this snapshot.
  var hasTonicTable: Bool { modes != nil && tonics != nil }

  /// The twelve canonical tonics of `mode`, by pitch class, when the table has them.
  func tonics(for mode: String) -> [TonicNameJSON]? {
    guard let row = tonics?[mode], row.count == 12 else { return nil }
    return row
  }
}

/// `kMsgTagError` payload: `{"schema":1,"error":"..."}`.
struct ErrorJSON: Codable {
  let schema: Int
  let error: String
}

/// `kMsgTagStatus` payload: `{"schema":1,"incompleteBlocks":n}`, audio blocks the scheduler could not
/// complete (a debug aid; the count only grows).
struct StatusJSON: Codable {
  let schema: Int
  let incompleteBlocks: Int
}
