import Foundation

/// What a palette press becomes while Record is on (strip editing design D9). Pure; `ChordLabState` passes the
/// press's release (its reason and its held time, from the event timestamps) and sends the command.
enum RecordLength {
  /// A press shorter than this is a tap: it gets the new-chord length instead of its held time.
  static let tapSeconds: TimeInterval = 0.2
  /// The strip's grid and the engine's longest length.
  static let grid = 0.25
  static let longest = 64.0
  /// The tempo when the host has given none.
  static let fallbackBpm = 120.0

  /// The recorded length of a press held `heldSeconds` at `bpm`: a tap (under `tapSeconds`, or a negative or NaN
  /// time) is `defaultBeats`; otherwise `heldSeconds * bpm / 60` rounded to the nearest quarter beat (halves up),
  /// at least 0.25 and at most 64 (an endless hold is 64). A tempo that is not a positive finite number counts as
  /// 120.
  static func beats(heldSeconds: TimeInterval, bpm: Double, defaultBeats: Double) -> Double {
    guard heldSeconds >= tapSeconds else { return defaultBeats }
    let tempo = bpm.isFinite && bpm > 0 ? bpm : fallbackBpm
    return min(max((heldSeconds * tempo / 60 / grid).rounded() * grid, grid), longest)
  }

  /// The length to record for a released cell, or nil when nothing is recorded: Record is off, or the press did
  /// not end with its own button-up (it became a drag, whose drop appends by itself; a newer press superseded it;
  /// the cell went away). The held time is the release's (`PressRelease.heldSeconds`).
  static func recordedBeats(recording: Bool, release: PressRelease, bpm: Double, defaultBeats: Double) -> Double? {
    guard recording, release.reason == .buttonUp else { return nil }
    return beats(heldSeconds: release.heldSeconds, bpm: bpm, defaultBeats: defaultBeats)
  }

  /// The `insertChords` that records `root`/`type` for `beats` at `index` (velocity and voicing left to the
  /// engine's defaults, so auto voice-leading applies).
  static func command(root: Int, type: String, beats: Double, index: Int) -> [String: Any] {
    let entry: [String: Any] = ["root": root, "type": type, "lengthBeats": beats]  // typed: a mixed literal needs it
    return ["cmd": "insertChords", "index": index, "chords": [entry]]
  }
}
