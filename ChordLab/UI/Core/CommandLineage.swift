import Foundation

/// The document lineage every editor command carries (tracks plan Task 24a, design D21). In an AU host the shell may
/// load state off the main thread: a command the editor computed for the document it shows (a chord index, a paste
/// with that document's track ids, a Generate entry) could otherwise reach the shell after the load and change the
/// loaded document before the editor has seen it. So `ChordLabState.send(command:)` stamps each `kMsgTagCommand` with
/// the state's clipboard token (`ClipboardToken`, the shell's `mClipboardToken`, renewed by every state load), and the
/// shell applies a command only when that is its current document's token (ChordLab/bridge/command_lineage.h); a stale
/// one applies nothing and brings the current snapshot, with its token first. Previews, export requests, snapshot
/// requests and the heard group's clear carry no lineage: none of them edits the document.
///
/// Depends on nothing but Foundation; unit-tested by the `ChordLabUICore` package
/// (Tests/ChordLabUICoreTests/CommandLineageTests.swift).
enum CommandLineage {
  /// The member the lineage travels in (the bridge's `commandLineage` reads it; the engine ignores it).
  static let key = "lineage"

  /// `command` with `key` set to `token`, replacing whatever the command held there. An empty token is stamped as is,
  /// never dropped: the shell's token is never empty, so such a command is stale (nothing applied) rather than
  /// unchecked. `ClipboardToken.resolve` never yields one.
  static func stamp(_ command: [String: Any], token: String) -> [String: Any] {
    var stamped = command
    stamped[key] = token
    return stamped
  }
}
