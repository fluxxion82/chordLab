import Foundation

/// One chord on the clipboard: exactly an `insertChords` entry (strip editing design D1, D5), filled from a
/// snapshot event. The voicing fields are nil for an event of an older plug-in build that sent none.
struct ClipChord: Codable, Equatable {
  let root: Int
  let type: String
  let lengthBeats: Double
  let velocity: Int
  var inversion: Int? = nil
  var octave: Int? = nil
  var manualVoicing: Bool? = nil
}

/// The clipboard's contents in their two forms (D5): the chords as `{"schema":1,"chords":[…]}` under
/// `pasteboardType`, the only form ⌘V reads, and the symbols joined by " – " as plain text for other apps.
/// Pure; `ChordLabState` makes the NSPasteboard calls.
enum ClipboardChords {
  /// The general pasteboard's type for ChordLab's chords.
  static let pasteboardType = "com.SterlingAlbury.chordlab.chords"
  /// The payload's version; `decode` refuses any other.
  static let schema = 1

  private struct Payload: Codable {
    let schema: Int
    let chords: [ClipChord]
  }

  /// The chords of the events `ids` names, in strip order (the order of `events`, whatever the order of `ids`).
  static func chords(of events: [EventJSON], ids: [Int]) -> [ClipChord] {
    events.filter { ids.contains($0.id) }.map {
      ClipChord(root: $0.root, type: $0.type, lengthBeats: $0.lengthBeats, velocity: $0.velocity,
                inversion: $0.inversion, octave: $0.octave, manualVoicing: $0.manualVoicing)
    }
  }

  /// The custom type's bytes for `chords`, keys sorted, nil fields left out; nil when there are no chords.
  static func encode(_ chords: [ClipChord]) -> Data? {
    guard !chords.isEmpty else { return nil }
    let encoder = JSONEncoder()
    encoder.outputFormatting = [.sortedKeys]
    return try? encoder.encode(Payload(schema: schema, chords: chords))
  }

  /// The chords in `data`, or nil when it is not ChordLab's payload: not JSON, no or another `schema`, an entry
  /// without `root`, `type`, `lengthBeats` or `velocity` (or with one mistyped), or no chords.
  static func decode(_ data: Data) -> [ClipChord]? {
    guard let payload = try? JSONDecoder().decode(Payload.self, from: data), payload.schema == schema,
          !payload.chords.isEmpty else { return nil }
    return payload.chords
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
      return entry
    }
    return ["cmd": "insertChords", "index": index, "chords": entries]
  }

  /// `removeChords` of `ids` (Delete, the block menu's Remove, and ⌘X once its copy is published).
  static func removeCommand(_ ids: [Int]) -> [String: Any] {
    ["cmd": "removeChords", "ids": ids]
  }

  /// ⌘C: clears `pasteboard` and writes the chords `ids` (strip order) as ONE item in both forms. True only when
  /// the pasteboard took the item; false, without touching the pasteboard, when none of `ids` is on the strip.
  static func copy(_ ids: [Int], from events: [EventJSON], to pasteboard: ChordPasteboard) -> Bool {
    guard let data = encode(chords(of: events, ids: ids)) else { return false }
    pasteboard.clear()
    return pasteboard.write(ClipboardItem(type: pasteboardType, data: data, text: symbolLine(of: events, ids: ids)))
  }

  /// ⌘X: the copy, then the `removeChords` to send, only when the copy was published; nil (nothing is removed)
  /// when it failed or had nothing to copy, so a cut can never lose chords.
  static func cut(_ ids: [Int], from events: [EventJSON], to pasteboard: ChordPasteboard) -> [String: Any]? {
    copy(ids, from: events, to: pasteboard) ? removeCommand(ids) : nil
  }

  /// ⌘V and Paste after: ChordLab's chords on `pasteboard`, or nil (text only, nothing, another payload).
  static func paste(from pasteboard: ChordPasteboard) -> [ClipChord]? {
    pasteboard.data(forType: pasteboardType).flatMap(decode)
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
