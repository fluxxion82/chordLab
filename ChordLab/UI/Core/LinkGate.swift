import Foundation

/// What may leave the editor while this instance follows another ChordLab's lane (per-track instruments plan section
/// 5). The editor's document is hidden behind the follower's panel, kept but not played; a callback the editor left
/// outstanding when the panel appeared (a context-menu choice, a rename field losing focus, a deferred send) must not
/// edit it or sound it. So `ChordLabState`'s one sender asks this before every editor command (`kMsgTagCommand`) and
/// every preview message (`kMsgTagPreview`):
/// - commands: only `commandsWhileFollowing` pass, the panel's own: `setMidiThru` (its Thru switch); every other
///   command, edits, selections, the transport switches and the key hint's flag among them, stays in the editor;
/// - previews: no start (`"on": true`) passes; stops (`"on": false`) and extends pass, so a hold that was sounding
///   when the panel appeared still ends.
/// Not gated, because the panel needs them or they do not touch the document: Panic, the snapshot request, the link
/// messages, the heard group's clear, and an export request (it only reads the document).
///
/// Keys are `LinkKeys`' (while following, every key goes to the host).
///
/// Depends on nothing but Foundation; unit-tested by the `ChordLabUICore` package
/// (Tests/ChordLabUICoreTests/LinkGateTests.swift).
enum LinkGate {
  /// The commands that pass while following, by their `cmd`.
  static let commandsWhileFollowing: Set<String> = ["setMidiThru"]

  /// Whether the command `fields` (with its `cmd`) may be sent.
  static func allowsCommand(_ fields: [String: Any], following: Bool) -> Bool {
    guard following else { return true }
    guard let name = fields["cmd"] as? String else { return false }
    return commandsWhileFollowing.contains(name)
  }

  /// Whether the preview message `fields` may be sent: anything but a start while following.
  static func allowsPreview(_ fields: [String: Any], following: Bool) -> Bool {
    guard following else { return true }
    return (fields["on"] as? Bool) != true
  }
}
