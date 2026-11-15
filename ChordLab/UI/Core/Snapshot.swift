import Foundation

/// Codable mirror of the engine's snapshot JSON (`snapshotJson` in engine/include/chordlab/json_api.h,
/// written by engine/src/json_api.cpp). The UI never computes theory: everything it shows is in here.
/// Fields the engine added under schema 1 (`modes`, `tonics`, `outOfKey`, and the Phase 2 fields:
/// `function`, `tension`, the voicing fields, `why`, `sections`, `suggestions`, `autoVoiceLead`,
/// `selectedEventId`, P2-M3's `homeTriads`, and the chord relations' `relationMode`, `relationReference`
/// and per-cell `relation` / `relationText`, the progression presets' `presets`, and the applied chords'
/// `applied` with its cells' `label` / `target`, and the spelled note names `voicingNames` /
/// `outOfKeyNames`, and the strip editing's `selectedEventIds` and `newChordBeats`, and the tracks' `ticksPerBeat`,
/// `revision`, `tracks`, `chordsMuted`, `chordsSolo`, `chordsAudible`, `noteSelection` and `trackLimits` with the
/// events' `playStart`, `playEnd` and `guide`) are optional so that a UI built against this file still decodes a
/// snapshot from an older plug-in build.

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
/// (cost 4)"). `commonNames` spells `commonPcs` as tones of this chord, its root in the project key (E7's
/// G# reads G#, not the key's Ab); `borrowedFrom` / `borrowedFromName` are nil for a chord in the key or
/// in no parallel mode.
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
/// repeats). `voicingNames` and `outOfKeyNames` name those notes as the chord spells them (E7's third is
/// "G#", Fm's "Ab"; ASCII accidentals like `symbol`, octaves on the voicing: "G#4"), one per entry of
/// `voicing` / `outOfKey`. `function` is "T", "PD", "D" or "O", `tension` 0..100; `inversion`, `octave`
/// and `manualVoicing` the stored voicing fields; `why` the relation to the previous event, nil on the
/// first. Everything after `voicing` is absent from older plug-in builds.
/// `playStart` / `playEnd` (tracks design D6) are the chord's playback span in beats: the authored
/// `startBeat` and `startBeat + lengthBeats` unless a feel performs, when they are the tick-rounded boundaries
/// the scheduler plays. The strip draws the authored positions; the roll's guide bands and chord lookups use
/// the span. `guide` is the chord's harmonic guide against the project key (tracks design D9).
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
  let voicingNames: [String]?
  let outOfKey: [Int]?
  let outOfKeyNames: [String]?
  let function: String?
  let tension: Int?
  let inversion: Int?
  let octave: Int?
  let manualVoicing: Bool?
  let why: WhyJSON?
  var playStart: Double? = nil
  var playEnd: Double? = nil
  var guide: GuideJSON? = nil
}

/// One palette cell: the chord on scale degree `degree` (0..6) of the browsed key in row `tier`.
/// `filled` is false for a borrowed chord (a note outside the project key); `family` is "Major", "Minor"
/// or "Other"; `outOfKey` lists the borrowed pitch classes (empty exactly when `filled`); `voicingNames`
/// and `outOfKeyNames` name the voicing and the borrowed notes as the symbol spells them; `function` and
/// `tension` as on events. `outOfKey`, `function`, `tension` and the names are absent from older plug-in
/// builds.
/// `relation` (0...100) and `relationText` are the cell's relatedness to the snapshot's
/// `relationReference` under its `relationMode` (plan R6): present only when there is a reference chord
/// and the mode is not off, and absent from older plug-in builds.
/// The snapshot's `homeTriads` entries are cells too: the project key's triads (`degree` of the project
/// scale, `tier` 1), with the engine's own `id` ("home-0" … "home-6"), which palette cells do not carry.
/// So are the snapshot's `applied` entries (plan A3): `degree` is the column of the chord they lead to
/// (1..6), `tier` 1 for `V7/x` and 0 for `vii°7/x`, `id` "applied-<degree>-<tier>", plus `label` (the
/// numeral relative to the browsed key, "V7/ii") and `target` (the chord resolved to, "Dm"). `roman`
/// stays the numeral against the project key, like every cell's.
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
  let voicingNames: [String]?
  let outOfKey: [Int]?
  let outOfKeyNames: [String]?
  let function: String?
  let tension: Int?
  /// The wire `id`: nil on palette cells, "home-<degree>" on `homeTriads` entries, "applied-<degree>-<tier>"
  /// on `applied` entries.
  let engineId: String?
  /// How related the chord is to the reference chord, 0...100; drives `RelationStyle`.
  let relation: Int?
  /// The engine's short account of that relation ("2 common tones (C, E) · up a fourth · cost 3").
  let relationText: String?
  /// Applied chords only: the name musicians use, relative to the browsed key ("V7/ii"). Shown in place
  /// of `roman` (`PaletteViewMode.numeralText`).
  let label: String?
  /// Applied chords only: the symbol of the chord this one resolves to ("Dm").
  let target: String?

  /// The engine's id when it sent one, else the grid position ("1/0": unique within a palette, one cell
  /// per tier and degree). The forms cannot collide, so a held home or applied cell never lights a palette
  /// cell (or each other).
  var id: String { engineId ?? "\(tier)/\(degree)" }

  enum CodingKeys: String, CodingKey {
    case degree, tier, root, type, symbol, roman, family, filled, voicing, voicingNames, outOfKey, outOfKeyNames
    case function, tension
    case relation, relationText, label, target
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

/// The snapshot's `feel`: the engine's feel settings by their JSON names (`rhythm`, `strum`, `strumSpeed`,
/// `humanise`, and for the arpeggio `arpPattern`, `arpRate`, `arpRange`); see `FeelOptions` for the accepted
/// values. The three arpeggio fields are optional so that a four-key feel (a plug-in built before them) still
/// decodes; the engine of this build always writes all seven and never the old `arpUp` / `arpUpDown` names
/// (it normalises them on load), so `FeelOptions` reads the three with the engine's defaults and needs no
/// legacy mapping.
struct FeelJSON: Codable, Equatable {
  let rhythm: String
  let strum: String
  let strumSpeed: String
  let humanise: String
  var arpPattern: String? = nil
  var arpRate: String? = nil
  var arpRange: String? = nil
}

/// The snapshot's `meter`: the document's time signature, `num` 1..16 over `den` 2, 4 or 8.
struct MeterJSON: Codable, Equatable {
  let num: Int
  let den: Int
}

/// A guide role: how a pitch class sounds over a chord (tracks design D9), decoded from the engine's
/// `guideRoleName` strings "root", "chord", "tension", "avoid" and "outside". An unknown string is `.outside`,
/// so a role a newer plug-in adds never loses the snapshot.
enum GuideRole: String, Codable {
  case root, chord, tension, avoid, outside

  init(from decoder: Decoder) throws {
    let text = try decoder.singleValueContainer().decode(String.self)
    self = GuideRole(rawValue: text) ?? .outside
  }

  func encode(to encoder: Encoder) throws {
    var container = encoder.singleValueContainer()
    try container.encode(rawValue)
  }
}

/// An event's `guide` (tracks design D9): four views of the chord's harmonic guide indexed by absolute pitch
/// class (C = 0, not relative to the root), against the project key. `scale` is the chord scale as a 12-bit
/// mask (bit `pc` set when `pc` is in it; C major's 2741). `degrees` ("R", "b9", "#11", "bb7") and `names`
/// ("E#", "G#", "Bb") stay ASCII like every engine string; `displayDegrees` / `displayNames` convert with
/// `ChordSymbol.glyphs` for drawing. The lookups take any integer as a pitch class (taken mod 12, so a MIDI note
/// number works), and a short `roles` array reads as `.outside` rather than trapping.
struct GuideJSON: Codable, Equatable {
  let roles: [GuideRole]
  let scale: Int
  let degrees: [String]
  let names: [String]

  private static func pitchClass(_ value: Int) -> Int { ((value % 12) + 12) % 12 }

  /// The role of `pc` (mod 12); `.outside` when `roles` has no entry for it.
  func role(pc: Int) -> GuideRole {
    let index = Self.pitchClass(pc)
    return index < roles.count ? roles[index] : .outside
  }

  /// Whether `pc` (mod 12) is in the chord scale: its bit of `scale` is set.
  func inScale(pc: Int) -> Bool { (scale >> Self.pitchClass(pc)) & 1 == 1 }

  /// `names` with the accidentals as glyphs ("Db" reads "D♭", "F#" "F♯").
  var displayNames: [String] { names.map(ChordSymbol.glyphs) }

  /// `degrees` with the accidentals as glyphs ("b9" reads "♭9", "#11" "♯11").
  var displayDegrees: [String] { degrees.map(ChordSymbol.glyphs) }

  /// The glyph name of `pc` (mod 12); empty when `names` has no entry for it.
  func displayName(pc: Int) -> String {
    let index = Self.pitchClass(pc)
    return index < names.count ? ChordSymbol.glyphs(names[index]) : ""
  }

  /// The glyph degree of `pc` (mod 12) ("b2" reads "♭2", "R" stays "R"); empty when `degrees` has no entry for it.
  /// Converts the one string, as `displayName(pc:)` does, never the whole array as `displayDegrees` does.
  func displayDegree(pc: Int) -> String {
    let index = Self.pitchClass(pc)
    return index < degrees.count ? ChordSymbol.glyphs(degrees[index]) : ""
  }
}

/// One track's notes, column-wise (tracks design D6): eight parallel arrays, entry `i` of each being note `i` in
/// the track's note order (derived start, pitch, id), so decoding builds no object per note. `start` is the derived
/// absolute tick, `anchor` the id of the note's chord, `offset` the ticks into it, `active` false once the chord
/// is shorter than the offset. A well-formed snapshot has equal lengths; `count` is the shortest column's, so a
/// ragged one never traps `subscript`, `indices` or `index(ofId:)`.
///
/// A `RandomAccessCollection` of `RollNote` (indices `0..<count`, each element built on demand), so views can
/// iterate, `reversed()`, `first(where:)` and slice it. The engine keeps every track's notes sorted by (derived
/// start, pitch, id) after each store edit (`Track::notes`, engine/include/chordlab/tracks.h; `sortTrackNotes`), so
/// `start` is non-decreasing in note order and a view may binary-search the notes that overlap a visible tick window.
struct TrackNotesJSON: Codable, Equatable, RandomAccessCollection {
  let id: [Int]
  let pitch: [Int]
  let start: [Int]
  let length: [Int]
  let velocity: [Int]
  let anchor: [Int]
  let offset: [Int]
  let active: [Bool]

  /// The number of notes: the minimum of the column counts.
  var count: Int {
    Swift.min(id.count, pitch.count, start.count, length.count, velocity.count, anchor.count, offset.count, active.count)
  }

  var startIndex: Int { 0 }
  var endIndex: Int { count }

  /// `0..<count`.
  var indices: Range<Int> { 0..<count }

  /// Note `i` built from the columns; `i` must be in `indices` (as an `Array`'s subscript).
  subscript(i: Int) -> RollNote {
    RollNote(id: id[i], pitch: pitch[i], start: start[i], length: length[i], velocity: velocity[i], anchor: anchor[i],
             offset: offset[i], active: active[i])
  }

  /// The position of the note with this id in the columns, nil for none. O(n): for a single lookup; use
  /// `indices(ofIds:)` for many.
  func index(ofId noteId: Int) -> Int? {
    id.prefix(count).firstIndex(of: noteId)
  }

  /// The positions of the notes whose ids are in `ids`, ascending (note order), in one pass over the columns; ids
  /// that name no note are skipped.
  func indices(ofIds ids: Set<Int>) -> [Int] {
    guard !ids.isEmpty else { return [] }
    return (0..<count).filter { ids.contains(id[$0]) }
  }
}

/// One track of the snapshot (tracks design D1, D6). `audible` is the mute/solo outcome as the engine
/// computes it, so the UI never derives it; `notes` are column-wise.
struct TrackJSON: Codable, Equatable, Identifiable {
  let id: Int
  let name: String
  let role: TrackRole
  let mute: Bool
  let solo: Bool
  let audible: Bool
  let notes: TrackNotesJSON
}

/// The snapshot's `noteSelection` (tracks design D5): the selected notes of one track, in the track's note order.
struct NoteSelectionJSON: Codable, Equatable {
  let trackId: Int
  let ids: [Int]
}

/// The snapshot's `trackLimits` (tracks design D1, D10): the caps the engine enforces, so the UI never repeats a
/// number: at most `maxTracks` tracks of `maxNotesPerTrack` notes, each at least `minNoteTicks` long, no more
/// than `maxPolyphony` sounding at once and `maxOnsetsPerBeat` onsets in a beat.
struct TrackLimitsJSON: Codable, Equatable {
  let maxTracks: Int
  let maxNotesPerTrack: Int
  let minNoteTicks: Int
  let maxPolyphony: Int
  let maxOnsetsPerBeat: Int

  /// The shortest track note when the snapshot carries no `trackLimits` (`kMinTrackNoteTicks`, a 1/32 beat).
  static let defaultMinNoteTicks = 30
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
  /// Whether a project-key change moves the sequence's chords with it; absent from older plug-in builds
  /// (read as off).
  let transposeWithKey: Bool?
  /// Whether the one-time "chords kept their notes" hint has been shown in this project; absent from older
  /// plug-in builds.
  let keyHintSeen: Bool?
  /// The document's feel (rhythm, strum, humanise); absent from older plug-in builds (read as the default).
  let feel: FeelJSON?
  /// The document's meter; absent from older plug-in builds (read as 4/4).
  let meter: MeterJSON?
  /// True when the feel was too dense to play, so playback and export use block chords; absent from older
  /// plug-in builds (read as false).
  let feelLimited: Bool?
  /// The selected event (the store's live selection), nil (JSON null, or absent) for none. The sequence
  /// bar's selection ring and the suggestions follow it.
  let selectedEventId: Int?
  /// Every selected block in strip order (the store's multi-selection, strip editing design D2), the anchor
  /// `selectedEventId` among them; absent from older plug-in builds. Read it through `selection`.
  let selectedEventIds: [Int]?
  /// The length in beats a chord added without one gets (design D3); nil (JSON null, or absent from an older
  /// plug-in build) for one bar of the meter. The Length menu's title and dot show it.
  let newChordBeats: Double?
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
  /// The browsed key's applied chords (plan A5): secondary dominants and leading-tone sevenths as cells,
  /// in the engine's order; empty when the key has none. Absent from older plug-in builds, whose UI then
  /// hides the Diatonic / Applied switch. Laid out by `PaletteViewMode.appliedLayout`.
  let applied: [CellJSON]?
  /// Whether incoming MIDI notes are passed on to the plug-in's output (the transport row's Thru switch);
  /// absent from older plug-in builds, whose UI then hides the switch.
  let midiThru: Bool?
  /// The tick grid of every tick field (960); absent from builds before tracks (read it through
  /// `ticksPerBeatOrDefault`). The tracks fields below are all absent from such builds.
  var ticksPerBeat: Int? = nil
  /// The store's content revision: moves with every content commit, undo, redo and state load, never with a
  /// selection or a live flag. A note command's `baseRevision` names it.
  var revision: UInt64? = nil
  /// The tracks in order, empty without tracks; each track's notes are column-wise.
  var tracks: [TrackJSON]? = nil
  /// The chords lane's mute and solo flags and their outcome (`chordsAudible`, as a track's `audible`).
  var chordsMuted: Bool? = nil
  var chordsSolo: Bool? = nil
  var chordsAudible: Bool? = nil
  /// The selected notes of one track (live engine state); nil (JSON null, or absent) for none.
  var noteSelection: NoteSelectionJSON? = nil
  /// The engine's track and note caps.
  var trackLimits: TrackLimitsJSON? = nil

  /// `relationMode` as a case; nil when absent or unknown.
  var relationModeValue: RelationMode? { relationMode.flatMap(RelationMode.init(rawValue:)) }

  /// The cells of the home compare row: empty when the project key is the one browsed, and when an older
  /// plug-in build sends no `homeTriads`.
  var homeRow: [CellJSON] { homeTriads ?? [] }

  /// The selected blocks in strip order: `selectedEventIds`, or for an older plug-in build the one
  /// `selectedEventId` (none when that is nil).
  var selection: [Int] { selectedEventIds ?? selectedEventId.map { [$0] } ?? [] }

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

/// `kMsgTagStatus` payload: `{"schema":1,"incompleteBlocks":n,"droppedHeard":m}`, audio blocks the
/// scheduler could not complete and heard-ring overflows, plus raw MIDI ring overflows under AU (debug
/// aids; the counts only grow). Only `incompleteBlocks` is decoded here.
struct StatusJSON: Codable {
  let schema: Int
  let incompleteBlocks: Int
}

/// `kMsgTagTransport` payload: `{"schema":1,"ppq":beats,"playing":bool,"bpm":tempo}`, the host position at the
/// start of the last audio block, whether the progression plays there (transport running and Follow Live on)
/// and the host tempo (120 before the first block; strip editing design D4). Pushed at ~15 Hz while it changes
/// and whenever the tempo moves by 0.01 bpm or more; the sequence bar's playhead and Record's beats.
struct TransportJSON: Codable, Equatable {
  let schema: Int
  let ppq: Double
  let playing: Bool
  /// Absent from older plug-in builds; read it through `tempo`.
  var bpm: Double? = nil

  /// The tempo in beats per minute: `bpm` when it is a positive finite number, else 120 (an older plug-in, or a
  /// host that reported none).
  var tempo: Double {
    guard let bpm, bpm.isFinite, bpm > 0 else { return 120 }
    return bpm
  }
}

/// `kMsgTagExportReady` payload: `{"schema":1,"path":"…"}`, the MIDI file the plug-in just wrote for a
/// drag-out (the answer to `kMsgTagExport`; a failure comes back as `kMsgTagError` instead).
struct ExportReadyJSON: Codable, Equatable {
  let schema: Int
  let path: String
}
