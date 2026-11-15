import Foundation

/// One note on the clipboard (tracks design D15 "Keyboard"): its pitch, length and velocity, and its start in
/// ticks RELATIVE to the first copied note (the first is at 0).
struct ClipNote: Codable, Equatable {
  let pitch: Int
  let start: Int
  let length: Int
  let velocity: Int
}

/// The note clipboard in its two forms (tracks design D15 "Keyboard"): the notes as `{"schema":1,"notes":[{pitch,
/// start, length, velocity}]}` under `pasteboardType`, the only form ⌘V reads, and the note names as plain text for
/// other apps; written through the same injectable `ChordPasteboard` as the chord clipboard (`ClipboardChords`), one
/// item with both forms, so both are published or neither. Pure; `ChordLabState` passes the pasteboard.
///
/// Copied notes are sorted by start (then pitch), the first at 0, so a paste lands with the same spacing wherever it
/// goes. The plain-text line (decision; D15 says "a plain-text line of note names") names the notes in that order
/// with ASCII sharps and the octave, middle C = C4 = 60 (the keyboard column's convention): "C2 A2 F2".
///
/// Depends on nothing but Foundation; unit-tested by the `ChordLabUICore` package
/// (Tests/ChordLabUICoreTests/NoteClipboardTests.swift).
enum NoteClipboard {
  /// The general pasteboard's type for ChordLab's notes.
  static let pasteboardType = "com.SterlingAlbury.chordlab.notes"
  /// The payload's version; `decode` refuses any other.
  static let schema = 1

  private struct Payload: Codable {
    let schema: Int
    let notes: [ClipNote]
  }

  private static let pitchNames = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

  /// The notes of `notes` that `ids` names, sorted by (start, pitch), starts relative to the first; empty when no id
  /// names a note. Inactive notes are copied like any other.
  static func clipNotes(of notes: TrackNotesJSON, ids: Set<Int>) -> [ClipNote] {
    let picked = ordered(notes, ids: ids)
    guard let base = picked.first?.start else { return [] }
    return picked.map { ClipNote(pitch: $0.pitch, start: $0.start - base, length: $0.length, velocity: $0.velocity) }
  }

  /// The notes `ids` names in clipboard order: by (start, pitch), ties in note order.
  private static func ordered(_ notes: TrackNotesJSON, ids: Set<Int>) -> [RollNote] {
    notes.indices(ofIds: ids).map { notes[$0] }.enumerated().sorted {
      ($0.element.start, $0.element.pitch, $0.offset) < ($1.element.start, $1.element.pitch, $1.offset)
    }.map(\.element)
  }

  /// The custom type's bytes for `notes`, keys sorted; nil when there are no notes.
  static func encode(_ notes: [ClipNote]) -> Data? {
    guard !notes.isEmpty else { return nil }
    let encoder = JSONEncoder()
    encoder.outputFormatting = [.sortedKeys]
    return try? encoder.encode(Payload(schema: schema, notes: notes))
  }

  /// The notes in `data`, or nil when it is not a valid ChordLab note payload: not JSON, no or another `schema`, no
  /// notes, an entry missing or mistyping a field, or an entry outside the domain (pitch 0...127, velocity 1...127,
  /// relative start 0...`kMaxNoteTick − kMinTrackNoteTicks`, length `kMinTrackNoteTicks`...`kMaxNoteTick`; see
  /// `RollLimits`).
  static func decode(_ data: Data) -> [ClipNote]? {
    guard let payload = try? JSONDecoder().decode(Payload.self, from: data), payload.schema == schema,
          !payload.notes.isEmpty else { return nil }
    let maxTick = RollLimits.engineMaxNoteTick
    let minLength = RollLimits.engineMinNoteTicks
    let valid = payload.notes.allSatisfy {
      (0...127).contains($0.pitch) && (1...127).contains($0.velocity) && (0...(maxTick - minLength)).contains($0.start)
        && (minLength...maxTick).contains($0.length)
    }
    return valid ? payload.notes : nil
  }

  /// A pitch's name: ASCII sharps and the octave, C4 = 60 ("C-1" for 0, "C#4" for 61, "G9" for 127).
  static func noteName(pitch: Int) -> String {
    let pc = ((pitch % 12) + 12) % 12
    let octave = Int((Double(pitch) / 12).rounded(.down)) - 1
    return "\(pitchNames[pc])\(octave)"
  }

  /// The plain-text form: the names of the notes `ids` names in clipboard order, joined by spaces ("C2 A2 F2").
  static func textLine(of notes: TrackNotesJSON, ids: Set<Int>) -> String {
    ordered(notes, ids: ids).map { noteName(pitch: $0.pitch) }.joined(separator: " ")
  }

  /// ⌘C: clears `pasteboard` and writes the notes `ids` names as ONE item in both forms. True only when the
  /// pasteboard took the item; false, without touching the pasteboard, when no id names a note.
  static func copy(_ ids: Set<Int>, from notes: TrackNotesJSON, to pasteboard: ChordPasteboard) -> Bool {
    guard let data = encode(clipNotes(of: notes, ids: ids)) else { return false }
    pasteboard.clear()
    return pasteboard.write(ClipboardItem(type: pasteboardType, data: data, text: textLine(of: notes, ids: ids)))
  }

  /// ⌘X: the copy, then the `removeNotes` of the copied notes (ids in note order, carrying `baseRevision`, the
  /// revision of the snapshot the ids came from), only when the copy was published; nil (nothing is removed) when it
  /// failed or had nothing to copy, so a cut can never lose notes.
  static func cut(_ ids: Set<Int>, trackId: Int, from notes: TrackNotesJSON, baseRevision: UInt64?,
                  to pasteboard: ChordPasteboard) -> RollCommand? {
    guard copy(ids, from: notes, to: pasteboard) else { return nil }
    let removed = notes.indices(ofIds: ids).map { notes.id[$0] }
    return .removeNotes(trackId: trackId, ids: removed, baseRevision: baseRevision)
  }

  /// ⌘V: ChordLab's notes on `pasteboard`, or nil (text only, nothing, chords, an invalid payload).
  static func paste(from pasteboard: ChordPasteboard) -> [ClipNote]? {
    pasteboard.data(forType: pasteboardType).flatMap(decode)
  }

  /// Where a paste lands (design D15): the insert marker when there is one (`RollEditState.insertMarker`), else
  /// right after the selection's end (the latest end of the selected notes still in `notes`), else tick 0.
  static func pastePosition(marker: Int?, notes: TrackNotesJSON, selectedIds: Set<Int>) -> Int {
    if let marker { return marker }
    return notes.indices(ofIds: selectedIds).map { notes[$0].end }.max() ?? 0
  }

  /// The `insertNotes` of a paste at `base`: each note at `base` + its relative start with its pitch, velocity and
  /// length, the length clamped into the domain (at most `limits.maxNoteTick` − start, never below
  /// `limits.minNoteTicks`). No `baseRevision` (decision: a paste names no existing ids, so a newer snapshot cannot
  /// make it wrong). A start the progression does not own is the engine's refusal, shown like any other. nil for no
  /// notes.
  static func pasteCommand(_ clip: [ClipNote], trackId: Int, at base: Int, limits: RollLimits) -> RollCommand? {
    guard !clip.isEmpty else { return nil }
    let specs = clip.map { note -> RollNoteSpec in
      let start = base + note.start
      let length = max(limits.minNoteTicks, min(note.length, limits.maxNoteTick - start))
      return RollNoteSpec(pitch: note.pitch, start: start, length: length, velocity: note.velocity)
    }
    return .insertNotes(trackId: trackId, notes: specs, baseRevision: nil)
  }
}
