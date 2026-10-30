import Foundation

/// When the one-time hint shows, and whether a hint still matches the sequence it was shown for.
enum KeyTransposeHint {
  /// What the hint was shown for: the project tonic after the change and the sequence's chords.
  struct Basis: Equatable {
    let tonicPc: Int
    let chords: [Chord]
    struct Chord: Equatable {
      let id: Int
      let root: Int
      let type: String
    }
  }

  static func basis(of snapshot: Snapshot) -> Basis {
    Basis(tonicPc: KeyNames.pitchClass(of: snapshot.projectKey.tonic),
          chords: snapshot.events.map { .init(id: $0.id, root: $0.root, type: $0.type) })
  }

  /// A tonic change, the switch off, chords in the sequence, and the hint not yet shown in this project.
  static func shows(fromPc: Int, toPc: Int, transposeWithKey: Bool, eventCount: Int, hintSeen: Bool) -> Bool {
    fromPc != toPc && !transposeWithKey && eventCount > 0 && !hintSeen
  }

  /// Whether the project key is the one browsed (`KeyJSON` equality: tonic letter and alter, and mode).
  /// The key row belongs to the moment of the key change: browsing away closes it.
  static func isHome(_ snapshot: Snapshot) -> Bool {
    snapshot.browsedKey == snapshot.projectKey
  }

  /// The hint survives a snapshot only if the sequence and tonic are as it was shown for, the switch is
  /// still off and the project key is still the one browsed.
  static func stillValid(_ basis: Basis, in snapshot: Snapshot) -> Bool {
    !(snapshot.transposeWithKey ?? false) && isHome(snapshot) && self.basis(of: snapshot) == basis
  }
}
