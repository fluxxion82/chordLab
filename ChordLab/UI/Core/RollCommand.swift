import Foundation

/// One note an `insertNotes` creates (engine/include/chordlab/json_api.h): `start` the absolute tick (the engine
/// resolves it to the owning chord and offset), `length` in ticks, `velocity` 1...127.
struct RollNoteSpec: Equatable {
  var pitch: Int
  var start: Int
  var length: Int
  var velocity: Int
}

/// One entry of an `updateNotes` (json_api.h): the note `id` and the fields that change, as ABSOLUTE values (the UI
/// computes them from the snapshot it showed, tracks design D4). A nil field is left out of the JSON, so the
/// engine keeps the note's value; the roll sends only the fields an edit changed (a move without a time change
/// carries no `start`, so an inactive note's anchor is not re-resolved for a pitch-only move).
struct RollNoteEdit: Equatable {
  var id: Int
  var pitch: Int? = nil
  var start: Int? = nil
  var length: Int? = nil
  var velocity: Int? = nil
}

/// A piano-roll command, 1:1 with the engine's JSON note commands (engine/include/chordlab/json_api.h, tracks
/// design D4, D5): `json` is exactly the dictionary `ChordLabState.send(command:)` takes (it adds `schema`).
///
/// The content commands carry `baseRevision`, the snapshot revision the gesture started on (D4): the engine refuses
/// one whose base is no longer current ("The notes changed while you were dragging"), so a gesture computed from
/// an older snapshot never overwrites a newer edit. nil leaves the key out (no check), as a paste does.
enum RollCommand: Equatable {
  /// `selectNotes {trackId, ids}` (live state, never the revision). An empty `ids` sends the documented clearing
  /// form, `trackId: null` with `ids: []`.
  case selectNotes(trackId: Int, ids: [Int])
  /// `insertNotes {trackId, notes:[{pitch, start, length, velocity}], baseRevision?}`; the engine selects what it
  /// inserted (D5).
  case insertNotes(trackId: Int, notes: [RollNoteSpec], baseRevision: UInt64?)
  /// `removeNotes {trackId, ids, baseRevision?}`.
  case removeNotes(trackId: Int, ids: [Int], baseRevision: UInt64?)
  /// `updateNotes {trackId, notes:[{id, pitch?, start?, length?, velocity?}], baseRevision?}`.
  case updateNotes(trackId: Int, edits: [RollNoteEdit], baseRevision: UInt64?)

  /// The engine's command dictionary: `cmd` and the fields above (`baseRevision` as an unsigned 64-bit number,
  /// omitted when nil).
  var json: [String: Any] {
    switch self {
    case .selectNotes(let trackId, let ids):
      return ["cmd": "selectNotes", "trackId": ids.isEmpty ? NSNull() : trackId as Any, "ids": ids]
    case .insertNotes(let trackId, let notes, let base):
      let entries: [[String: Any]] = notes.map {
        ["pitch": $0.pitch, "start": $0.start, "length": $0.length, "velocity": $0.velocity]
      }
      return Self.with(base, ["cmd": "insertNotes", "trackId": trackId, "notes": entries])
    case .removeNotes(let trackId, let ids, let base):
      return Self.with(base, ["cmd": "removeNotes", "trackId": trackId, "ids": ids])
    case .updateNotes(let trackId, let edits, let base):
      let entries: [[String: Any]] = edits.map { edit in
        var entry: [String: Any] = ["id": edit.id]
        if let pitch = edit.pitch { entry["pitch"] = pitch }
        if let start = edit.start { entry["start"] = start }
        if let length = edit.length { entry["length"] = length }
        if let velocity = edit.velocity { entry["velocity"] = velocity }
        return entry
      }
      return Self.with(base, ["cmd": "updateNotes", "trackId": trackId, "notes": entries])
    }
  }

  private static func with(_ base: UInt64?, _ command: [String: Any]) -> [String: Any] {
    guard let base else { return command }
    var command = command
    command["baseRevision"] = NSNumber(value: base)
    return command
  }
}
