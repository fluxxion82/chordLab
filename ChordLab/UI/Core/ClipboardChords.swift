import Foundation

/// One chord on the clipboard: exactly an `insertChords` entry (strip editing design D1, D5), filled from a
/// snapshot event. The voicing fields are nil for an event of an older plug-in build that sent none. `notes` (tracks
/// design D21) are the track notes anchored to the chord, nil when it has none (and in an older payload).
struct ClipChord: Codable, Equatable {
  let root: Int
  let type: String
  let lengthBeats: Double
  let velocity: Int
  var inversion: Int? = nil
  var octave: Int? = nil
  var manualVoicing: Bool? = nil
  var notes: [ClipChordNote]? = nil

  /// The same chord with every note's lane matched by name and role only (`ClipLane.trackId` nil): a paste from
  /// another instance (D21), whose track ids mean nothing here.
  func droppingTrackIds() -> ClipChord {
    var chord = self
    chord.notes = notes?.map { note in
      var note = note
      note.lane.trackId = nil
      return note
    }
    return chord
  }
}

/// The lane a clipboard note belongs to (tracks design D21): the copying document's track id, name and role.
/// `insertChords` resolves it to the track with that id, else the unique track with that name and role, else a new
/// track; `trackId` nil (a foreign paste) leaves the id out. Decoding an unknown role gives `.other` (`TrackRole`).
struct ClipLane: Codable, Equatable {
  var trackId: Int?
  let name: String
  let role: TrackRole
}

/// One note of a chord on the clipboard (tracks design D21), as an `insertChords` entry's `notes` element takes it:
/// its lane, `offset` the ticks after the chord's start (the snapshot's `offset`; an inactive note's offset is past
/// the chord's end and stays so), `length` in ticks, `pitch`, and `velocity`, nil for the engine's default 100 (the
/// clipboard leaves it out to stay under the JSON cap at the worst case).
struct ClipChordNote: Codable, Equatable {
  var lane: ClipLane
  let offset: Int
  let length: Int
  let pitch: Int
  var velocity: Int?
}

/// The clipboard's contents in their two forms (D5): the chords as `{"schema":1,"chords":[…],"token":…}` under
/// `pasteboardType`, the only form ⌘V reads, and the symbols joined by " – " as plain text for other apps.
/// Each chord carries its track notes with their lanes, and the payload the copying instance's token (tracks design
/// D21). Pure; `ChordLabState` passes the pasteboard.
enum ClipboardChords {
  /// The general pasteboard's type for ChordLab's chords.
  static let pasteboardType = "com.SterlingAlbury.chordlab.chords"
  /// The payload's version; `decode` refuses any other.
  static let schema = 1

  /// The payload: `token` is the copying instance's (`ChordLabState.clipboardToken`, D21), nil in an older payload.
  private struct Payload: Codable {
    let schema: Int
    let chords: [ClipChord]
    var token: String? = nil
  }

  /// A decoded payload: the chords and the token of the instance that copied them (nil when it carried none).
  struct Contents: Equatable {
    let chords: [ClipChord]
    let token: String?
  }

  /// The chords of the events `ids` names, in strip order (the order of `events`, whatever the order of `ids`), each
  /// with the notes of `tracks` anchored to it (D21): track order, then each track's note order, inactive notes
  /// included, the default velocity 100 left out; nil notes for a chord with none (or without `tracks`).
  static func chords(of events: [EventJSON], ids: [Int], tracks: [TrackJSON]? = nil) -> [ClipChord] {
    let wanted = Set(ids)
    var notesByChord: [Int: [ClipChordNote]] = [:]
    for track in tracks ?? [] {
      let lane = ClipLane(trackId: track.id, name: track.name, role: track.role)
      for note in track.notes where wanted.contains(note.anchor) {
        notesByChord[note.anchor, default: []].append(
          ClipChordNote(lane: lane, offset: note.offset, length: note.length, pitch: note.pitch,
                        velocity: note.velocity == defaultNoteVelocity ? nil : note.velocity))
      }
    }
    return events.filter { wanted.contains($0.id) }.map {
      ClipChord(root: $0.root, type: $0.type, lengthBeats: $0.lengthBeats, velocity: $0.velocity,
                inversion: $0.inversion, octave: $0.octave, manualVoicing: $0.manualVoicing, notes: notesByChord[$0.id])
    }
  }

  /// The velocity `insertChords` gives a note that names none (json_api.h), left out of the clipboard.
  static let defaultNoteVelocity = 100

  /// The custom type's bytes for `chords` and the copying instance's `token`, keys sorted, nil fields left out; nil
  /// when there are no chords.
  static func encode(_ chords: [ClipChord], token: String? = nil) -> Data? {
    guard !chords.isEmpty else { return nil }
    let encoder = JSONEncoder()
    encoder.outputFormatting = [.sortedKeys]
    return try? encoder.encode(Payload(schema: schema, chords: chords, token: token))
  }

  /// The chords and token in `data`, or nil when it is not ChordLab's payload: not JSON, no or another `schema`, an
  /// entry without `root`, `type`, `lengthBeats` or `velocity` (or with one mistyped, a note's fields included), or no
  /// chords. An older payload (no notes, no token) decodes.
  static func decodeContents(_ data: Data) -> Contents? {
    guard let payload = try? JSONDecoder().decode(Payload.self, from: data), payload.schema == schema,
          !payload.chords.isEmpty else { return nil }
    return Contents(chords: payload.chords, token: payload.token)
  }

  /// The chords in `data` as they were copied (track ids kept), or nil (see `decodeContents`).
  static func decode(_ data: Data) -> [ClipChord]? {
    decodeContents(data)?.chords
  }

  /// The plain-text form: the events' symbols as the strip shows them (`ChordSymbol.glyphs`), in strip order,
  /// joined by " – " ("C – Am – F – G7").
  static func symbolLine(of events: [EventJSON], ids: [Int]) -> String {
    events.filter { ids.contains($0.id) }.map { ChordSymbol.glyphs($0.symbol) }.joined(separator: " – ")
  }

  /// `insertChords` of `chords` at `index`; each entry carries the fields it has (a nil voicing field is left out,
  /// so the engine's default applies).
  static func insertCommand(_ chords: [ClipChord], at index: Int) -> [String: Any] {
    let entries: [[String: Any]] = chords.map { chord in
      var entry: [String: Any] = ["root": chord.root, "type": chord.type, "lengthBeats": chord.lengthBeats,
                                  "velocity": chord.velocity]
      if let inversion = chord.inversion { entry["inversion"] = inversion }
      if let octave = chord.octave { entry["octave"] = octave }
      if let manual = chord.manualVoicing { entry["manualVoicing"] = manual }
      if let notes = chord.notes, !notes.isEmpty { entry["notes"] = notes.map(noteEntry) }
      return entry
    }
    return ["cmd": "insertChords", "index": index, "chords": entries]
  }

  /// One `notes` element of an `insertChords` entry (json_api.h, D21): `{lane:{trackId?, name, role}, offset, length,
  /// pitch, velocity?}`, `trackId` and `velocity` left out when nil.
  private static func noteEntry(_ note: ClipChordNote) -> [String: Any] {
    var lane: [String: Any] = ["name": note.lane.name, "role": note.lane.role.rawValue]
    if let trackId = note.lane.trackId { lane["trackId"] = trackId }
    var entry: [String: Any] = ["lane": lane, "offset": note.offset, "length": note.length, "pitch": note.pitch]
    if let velocity = note.velocity { entry["velocity"] = velocity }
    return entry
  }

  /// ⌘D and the block menu's Duplicate (D21): the `insertChords` of the chords `ids` with their notes, right after
  /// the last of them, track ids kept (the same document, so each note goes back to its own track); nil when none of
  /// `ids` is on the strip. The pasteboard is not touched.
  static func duplicateCommand(_ ids: [Int], events: [EventJSON], tracks: [TrackJSON]?) -> [String: Any]? {
    let chords = chords(of: events, ids: ids, tracks: tracks)
    guard !chords.isEmpty else { return nil }
    return insertCommand(chords, at: SelectionLogic.index(after: ids, ids: events.map(\.id)))
  }

  /// `removeChords` of `ids` (Delete, the block menu's Remove, and ⌘X once its copy is published).
  static func removeCommand(_ ids: [Int]) -> [String: Any] {
    ["cmd": "removeChords", "ids": ids]
  }

  /// ⌘C: clears `pasteboard` and writes the chords `ids` (strip order) with their notes in `tracks` (D21) and the
  /// copying instance's `token` as ONE item in both forms. True only when the pasteboard took the item; false,
  /// without touching the pasteboard, when none of `ids` is on the strip.
  static func copy(_ ids: [Int], from events: [EventJSON], tracks: [TrackJSON]? = nil, token: String? = nil,
                   to pasteboard: ChordPasteboard) -> Bool {
    guard let data = encode(chords(of: events, ids: ids, tracks: tracks), token: token) else { return false }
    pasteboard.clear()
    return pasteboard.write(ClipboardItem(type: pasteboardType, data: data, text: symbolLine(of: events, ids: ids)))
  }

  /// ⌘X: the copy, then the `removeChords` to send, only when the copy was published; nil (nothing is removed)
  /// when it failed or had nothing to copy, so a cut can never lose chords.
  static func cut(_ ids: [Int], from events: [EventJSON], tracks: [TrackJSON]? = nil, token: String? = nil,
                  to pasteboard: ChordPasteboard) -> [String: Any]? {
    copy(ids, from: events, tracks: tracks, token: token, to: pasteboard) ? removeCommand(ids) : nil
  }

  /// ⌘V and Paste after: ChordLab's chords on `pasteboard` as copied, or nil (text only, nothing, another payload).
  static func paste(from pasteboard: ChordPasteboard) -> [ClipChord]? {
    pasteboard.data(forType: pasteboardType).flatMap(decode)
  }

  /// ⌘V and Paste after under `ownToken`, the token of the pasting instance's current document lineage (D21,
  /// `ClipboardToken`): the chords, with every note's track id dropped unless the payload's token is `ownToken`
  /// (another instance numbers its tracks from 1 too, and so does other state loaded into this one since the copy,
  /// so their ids would land notes in unrelated tracks here; a payload without a token is treated as foreign); nil
  /// as `paste`.
  static func paste(from pasteboard: ChordPasteboard, ownToken: String) -> [ClipChord]? {
    guard let contents = pasteboard.data(forType: pasteboardType).flatMap(decodeContents) else { return nil }
    return contents.token == ownToken ? contents.chords : contents.chords.map { $0.droppingTrackIds() }
  }
}

/// The clipboard token of one plug-in instance's document lineage (tracks design D21, Task 20 reviews): track ids
/// mean something only within the document they were copied from and its edits, so the token names that lineage. The
/// C++ shell makes it in the constructor (`ChordLab::mClipboardToken`, a UUID) and makes a new one whenever loaded
/// state replaces the document (`ChordLab::UnserializeState`: a host preset, a restored set), where track 1 may be
/// another lane. `ChordLab::OpenWindow` hands it to the view controller before the editor's state exists, so every
/// editor window of the same lineage (Live closes and reopens them routinely) carries the same token and pastes its
/// own copies with their track ids; a renewal reaches an open editor before the loaded state's snapshot, so a copy
/// made before the load pastes as foreign. Without one (a harness, a test, an older shell) each state makes its own.
enum ClipboardToken {
  /// `given` when it holds anything but whitespace, else a fresh UUID string.
  static func resolve(_ given: String?) -> String {
    guard let given, !given.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else { return UUID().uuidString }
    return given
  }
}

/// The two forms of one copy (strip editing D5, tracks design D15): ChordLab's payload under its custom `type`
/// (`ClipboardChords.pasteboardType` with `ClipboardChords.encode`'s bytes, or `NoteClipboard.pasteboardType` with
/// `NoteClipboard.encode`'s) and the plain-text line (the chord symbols, or the note names). A pasteboard writes
/// them as one item, so both are published or neither is.
struct ClipboardItem: Equatable {
  let type: String
  let data: Data
  let text: String
}

/// Where the clipboard lives: the general `NSPasteboard` in the plug-in (UI/SystemPasteboard.swift), a fake in
/// the tests; one protocol for both ChordLab types (chords and notes). `clear` empties it; `write` publishes one
/// item in both forms, the payload under the item's `type`, and says whether it did (every representation set and
/// the item accepted); `data(forType:)` reads the bytes under a type, ChordLab's or another.
protocol ChordPasteboard {
  func clear()
  func write(_ item: ClipboardItem) -> Bool
  func data(forType type: String) -> Data?
}
