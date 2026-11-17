import Foundation

/// The documents in which the user dismissed the Follow banner ("Not now", or Stop following), for the life of the
/// process: the editor's state is made anew on every window open, and a dismissal must outlive it, or the banner comes
/// back on every reopen. Keyed by the document lineage (the instance's clipboard token: unique per instance and per
/// document, renewed by every state load), so a new document may offer again. Bounded: the last `capacity` tokens are
/// kept, the oldest dropped first. Main thread only, like the editor that uses it; every `ChordLabState` shares
/// `shared`, a harness or a test makes its own.
///
/// Depends on nothing but Foundation; unit-tested by the `ChordLabUICore` package
/// (Tests/ChordLabUICoreTests/FollowDismissalsTests.swift).
final class FollowDismissals {
  /// The process's dismissals, shared by every editor.
  static let shared = FollowDismissals()
  static let defaultCapacity = 256

  let capacity: Int
  /// The tokens in the order they were (last) dismissed, oldest first.
  private var order: [String] = []
  private var tokens = Set<String>()

  init(capacity: Int = FollowDismissals.defaultCapacity) {
    self.capacity = max(1, capacity)
  }

  /// The banner was dismissed in the document of lineage `token`.
  func dismiss(_ token: String) {
    if tokens.contains(token) {
      order.removeAll { $0 == token }
    } else {
      tokens.insert(token)
    }
    order.append(token)
    while order.count > capacity {
      tokens.remove(order.removeFirst())
    }
  }

  /// Whether the banner was dismissed in the document of lineage `token`.
  func isDismissed(_ token: String) -> Bool { tokens.contains(token) }

  var count: Int { order.count }
}
