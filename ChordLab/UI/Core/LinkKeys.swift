import Foundation

/// The keyboard while the instance follows (M3 map section 10, item 3). The follower's panel replaces the editor, but
/// the editor's document stays alive behind it (plan I1: kept, not played, back on Stop following), and the shell
/// still applies commands while following. So every key command stands down: each one edits the hidden document
/// (Delete, the arrows' nudge, ⌘Z/⇧⌘Z, ⌘X, ⌘V, ⌘D, digits appending a suggestion) or acts on its hidden selection
/// (⌘C, ⌘A), and none of them has anything to act on in the panel. Nothing is allowed while following; Panic stays
/// available through the panel's button, and no key previews or plays (Space is the host's in every state).
///
/// A command that stands down is not the editor's key: `command(for:target:following:)` gives nil, so the key routes
/// leave it to the host, as they do any unmapped key.
///
/// Depends on nothing but Foundation; unit-tested by the `ChordLabUICore` package
/// (Tests/ChordLabUICoreTests/LinkKeysTests.swift).
enum LinkKeys {
  /// Whether the instance follows (its window is the panel).
  static func isFollowing(_ state: LinkStateJSON?) -> Bool { state?.isFollowing ?? false }

  /// Whether `command` reaches `ChordLabState.perform`. The switch names every command, so a new one must be decided
  /// here.
  static func delivers(_ command: KeyCommand, following: Bool) -> Bool {
    guard following else { return true }
    switch command {
    case .appendSuggestion, .deleteSelection, .undo, .redo, .cut, .paste, .duplicate, .nudge:
      return false   // edits the hidden document
    case .copy, .selectAll:
      return false   // reads or changes the hidden document's selection
    }
  }

  /// `KeyCommandMap.command(for:target:)` with the commands that stand down removed (nil: the host keeps the key).
  static func command(for input: KeyInput, target: KeyTarget, following: Bool) -> KeyCommand? {
    guard let command = KeyCommandMap.command(for: input, target: target),
          delivers(command, following: following) else { return nil }
    return command
  }
}
