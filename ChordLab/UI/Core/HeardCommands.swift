import Foundation

/// What the Heard group's buttons send (design D10-D12), decided here so it is unit-tested;
/// `ChordLabState.addHeardChord` and `useHeardKey` only send the result.
enum HeardCommands {
  /// Add's `appendChord`: the heard chord's root, dictionary type and inversion as played (the engine marks
  /// a chord appended with an inversion manual, D11).
  struct Append: Equatable {
    let root: Int
    let type: String
    let inversion: Int

    /// The command payload `ChordLabState` sends (it adds the schema).
    var command: [String: Any] { ["cmd": "appendChord", "root": root, "type": type, "inversion": inversion] }
  }

  /// Use's project key: the heard tonic's pitch class and the engine's mode string, passed through.
  struct UseKey: Equatable {
    let pc: Int
    let mode: String
  }

  /// Add: the heard chord, or nil without one. Whether it is still held does not matter (D10: the released
  /// chord stays addable; a two-hand chord cannot be held while clicking).
  static func add(_ heard: HeardJSON?) -> Append? {
    guard let chord = heard?.chord else { return nil }
    return Append(root: chord.root, type: chord.type, inversion: chord.inversion)
  }

  /// Use: the heard key, or nil while no key qualifies.
  static func use(_ heard: HeardJSON?) -> UseKey? {
    guard let key = heard?.key else { return nil }
    return UseKey(pc: KeyNames.pitchClass(of: key.tonic), mode: key.mode)
  }
}
