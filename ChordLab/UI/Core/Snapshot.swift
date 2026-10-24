import Foundation

/// Codable mirror of the engine's snapshot JSON (`snapshotJson` in engine/include/chordlab/json_api.h,
/// written by engine/src/json_api.cpp). The UI never computes theory: everything it shows is in here.
/// Fields the engine added under schema 1 (`modes`, `tonics`, `outOfKey`, and the Phase 2 fields:
/// `function`, `tension`, the voicing fields, `why`, `sections`, `suggestions`, `autoVoiceLead`,
/// `selectedEventId`, P2-M3's `homeTriads`, and the chord relations' `relationMode`, `relationReference`
/// and per-cell `relation` / `relationText`, and the progression presets' `presets`) are optional so that a UI built against this file still decodes a snapshot from an
/// older plug-in build.

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

/// An event's `why`: how its chord relates to the one before it (P2-D2), as data plus the engine's one
/// sentence (`text`, "G7 → C: down a fifth, resolves (D→T), 1 common tone (G), smooth voice leading
/// (cost 4)"). `commonNames` spells `commonPcs` in the project key; `borrowedFrom` / `borrowedFromName`
/// are nil for a chord in the key or in no parallel mode.
struct WhyJSON: Codable, Equatable {
  let commonTones: Int
  let commonPcs: [Int]
  let commonNames: [String]
  let rootMotion: String
  let voiceLeadingCost: Int
  let borrowedFrom: KeyJSON?
  let borrowedFromName: String?
  let text: String
}

/// One chord of the progression, in playback order. `symbol` and `roman` are spelled in the project key;
/// `family` is the type's "Major", "Minor" or "Other" (the sequence bar's colour); `voicing` is what
/// `compile` gives the chord (the auto voice-leading's choice, or the stored inversion and octave) as MIDI
/// note numbers; `outOfKey` the chord's pitch classes outside the project key's scale (ascending, no
/// repeats). `function` is "T", "PD", "D" or "O", `tension` 0..100; `inversion`, `octave` and
/// `manualVoicing` the stored voicing fields; `why` the relation to the previous event, nil on the first.
/// Everything after `voicing` is absent from older plug-in builds.
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
  let function: String?
  let tension: Int?
  let inversion: Int?
  let octave: Int?
  let manualVoicing: Bool?
  let why: WhyJSON?
}

/// One palette cell: the chord on scale degree `degree` (0..6) of the browsed key in row `tier`.
/// `filled` is false for a borrowed chord (a note outside the project key); `family` is "Major", "Minor"
/// or "Other"; `outOfKey` lists the borrowed pitch classes (empty exactly when `filled`); `function` and
/// `tension` as on events. `outOfKey`, `function` and `tension` are absent from older plug-in builds.
/// `relation` (0...100) and `relationText` are the cell's relatedness to the snapshot's
/// `relationReference` under its `relationMode` (plan R6): present only when there is a reference chord
/// and the mode is not off, and absent from older plug-in builds.
/// The snapshot's `homeTriads` entries are cells too: the project key's triads (`degree` of the project
/// scale, `tier` 1), with the engine's own `id` ("home-0" … "home-6"), which palette cells do not carry.
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
  let function: String?
  let tension: Int?
  /// The wire `id`: nil on palette cells, "home-<degree>" on `homeTriads` entries.
  let engineId: String?
  /// How related the chord is to the reference chord, 0...100; drives `RelationStyle`.
  let relation: Int?
  /// The engine's short account of that relation ("2 common tones (C, E) · up a fourth · cost 3").
  let relationText: String?

  /// The engine's id when it sent one, else the grid position ("1/0": unique within a palette, one cell
  /// per tier and degree). The two forms cannot collide, so a held home cell never lights a palette cell.
  var id: String { engineId ?? "\(tier)/\(degree)" }

  enum CodingKeys: String, CodingKey {
    case degree, tier, root, type, symbol, roman, family, filled, voicing, outOfKey, function, tension
    case relation, relationText
    case engineId = "id"
  }
}

/// The chord the palette's relatedness is measured against (plan R1: the selected block's chord, else the
/// last chord): the event's id and its symbol in the project key.
struct RelationReferenceJSON: Codable, Equatable {
  let eventId: Int
  let symbol: String
}

/// `tiers` is one more than the highest tier present; `cells` holds only the cells that exist, ordered by
/// tier then degree.
struct PaletteJSON: Codable, Equatable {
  let tiers: Int
  let cells: [CellJSON]
}

/// One section (P2-D6): a named, coloured span of the progression from `firstEventIndex` up to the next
/// section's first event. `colourIndex` indexes the UI's 8-colour palette; `id` is stable for the
/// section's life.
struct SectionJSON: Codable, Equatable, Identifiable {
  let id: Int
  let name: String
  let colourIndex: Int
  let firstEventIndex: Int
}

/// One next-chord suggestion (P2-D3) for the selected (else the last) chord: the chord as the engine
/// identifies it (`root`, `type`, what `appendChord` takes), its `symbol` and `roman` in the project key,
/// `function` as on events, the ranking `score`, and the `reasons` (engine names) with their parallel
/// prose `reasonTexts` ("1 common tone (C)").
struct SuggestionJSON: Codable, Equatable, Identifiable {
  let root: Int
  let type: String
  let symbol: String
  let roman: String
  let function: String
  let score: Double
  let reasons: [String]
  let reasonTexts: [String]

  /// The chord; unique within one list (the engine never suggests the same chord twice).
  var id: String { "\(root)/\(type)" }
}

/// One progression preset (plan P6): the engine's library entry, constant per build except `fits`. `id`
/// is what `loadPreset {id, mode}` takes; `genre` the list's group; `tonality` "Major", "Minor" or "Any";
/// `numerals` the steps as the engine spells them, resolved in C major (Major, Any) or A minor (Minor),
/// joined with " – "; `fits` whether `tonality` suits the project key's mode (P3).
struct PresetJSON: Codable, Equatable, Identifiable {
  let id: String
  let name: String
  let genre: String
  let tonality: String
  let numerals: String
  let fits: Bool
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
  /// Whether `compile` picks each non-manual chord's inversion and octave (P2-D5).
  let autoVoiceLead: Bool?
  /// The selected event (the store's live selection), nil (JSON null, or absent) for none. The sequence
  /// bar's selection ring and the suggestions follow it.
  let selectedEventId: Int?
  /// The sections in order of `firstEventIndex`; empty (or absent) when the document has none.
  let sections: [SectionJSON]?
  /// Up to 8 next-chord suggestions after the selected (else the last) chord, best first; the opening
  /// suggestions when the progression is empty.
  let suggestions: [SuggestionJSON]?
  /// The project key's seven diatonic triads while another key is browsed, else empty; absent from older
  /// plug-in builds. Read it through `homeRow`.
  let homeTriads: [CellJSON]?
  /// The relation mode (a `RelationMode` raw value), always sent by plug-ins that have chord relations;
  /// absent from older builds, whose UI then hides the Relations picker. Decoded as a string so that a
  /// mode this UI does not know never loses the snapshot; read the case through `relationModeValue`.
  let relationMode: String?
  /// The chord the cells' `relation` is measured against; nil (JSON null, or absent) for none.
  let relationReference: RelationReferenceJSON?
  /// The progression presets in table order (genres grouped); absent from older plug-in builds, whose UI
  /// then hides the Presets button.
  let presets: [PresetJSON]?

  /// `relationMode` as a case; nil when absent or unknown.
  var relationModeValue: RelationMode? { relationMode.flatMap(RelationMode.init(rawValue:)) }

  /// The cells of the home compare row: empty when the project key is the one browsed, and when an older
  /// plug-in build sends no `homeTriads`.
  var homeRow: [CellJSON] { homeTriads ?? [] }

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

/// `kMsgTagTransport` payload: `{"schema":1,"ppq":beats,"playing":bool}`, the host position at the start
/// of the last audio block and whether the progression plays there (transport running and Follow Live
/// on). Pushed at ~15 Hz while it changes; the sequence bar's playhead.
struct TransportJSON: Codable, Equatable {
  let schema: Int
  let ppq: Double
  let playing: Bool
}

/// `kMsgTagExportReady` payload: `{"schema":1,"path":"…"}`, the MIDI file the plug-in just wrote for a
/// drag-out (the answer to `kMsgTagExport`; a failure comes back as `kMsgTagError` instead).
struct ExportReadyJSON: Codable, Equatable {
  let schema: Int
  let path: String
}
