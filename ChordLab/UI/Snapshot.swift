import Foundation

/// Codable mirror of the engine's snapshot JSON (`snapshotJson` in engine/include/chordlab/json_api.h,
/// written by engine/src/json_api.cpp). The UI never computes theory: everything it shows is in here.

/// `{"letter":"C","alter":0}`: a spelled tonic, `alter` -1 flat, 0 natural, 1 sharp.
struct TonicJSON: Codable, Equatable {
  let letter: String
  let alter: Int
}

/// `{"tonic":{...},"mode":"Ionian"}`; `mode` is the engine's `Mode` enumerator name.
struct KeyJSON: Codable, Equatable {
  let tonic: TonicJSON
  let mode: String
}

/// One chord of the progression, in playback order. `symbol` and `roman` are spelled in the project key;
/// `voicing` is the default close voicing as MIDI note numbers.
struct EventJSON: Codable, Equatable, Identifiable {
  let id: Int
  let root: Int
  let type: String
  let symbol: String
  let roman: String
  let startBeat: Double
  let lengthBeats: Double
  let velocity: Int
  let voicing: [Int]
}

/// One palette cell: the chord on scale degree `degree` (0..6) of the browsed key in row `tier`.
/// `filled` is false for a borrowed chord (a note outside the project key); `family` is "Major", "Minor"
/// or "Other".
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
  let events: [EventJSON]
  let palette: PaletteJSON
}

/// `kMsgTagError` payload: `{"schema":1,"error":"..."}`.
struct ErrorJSON: Codable {
  let schema: Int
  let error: String
}
