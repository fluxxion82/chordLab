import Foundation

/// The document lineage every editor command carries (tracks plan Task 24a, design D21). In an AU host the shell may
/// load state off the main thread: a command the editor computed for the document it shows (a chord index, a paste
/// with that document's track ids, a Generate entry) could otherwise reach the shell after the load and change the
/// loaded document before the editor has seen it. So `ChordLabState.send(command:)` stamps each `kMsgTagCommand` with
/// the state's clipboard token (`ClipboardToken`, the shell's `mClipboardToken`, renewed by every state load), and the
/// shell applies a command only when that is its current document's token (ChordLab/bridge/command_lineage.h); a stale
/// one applies nothing and brings the current snapshot, with its token first. Export requests (`kMsgTagExportClip`)
/// carry it too: they read the document, and a stale one writes no file. Previews, panic, snapshot requests and the
/// heard group's clear carry none: none of them edits or reads the document by id.
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

/// One user action, one lineage (Task 24a review). The shell answers a command inline, inside the send, and a stale
/// command's answer is the loaded document's token and snapshot: by the time an action's first `send(command:)`
/// returns, the state's token may already be the new document's. An action that sends more than one command (a key
/// change's `setKey` and `browseKey`, Move and always transpose, a loop over note commands, a roll gesture's effects)
/// computed all of them from the document it began in, so all of them carry the token read once at its start, and
/// the shell drops the later ones as stale too. `ChordLabState` opens one around every such action; a nested one
/// (a helper that is also an entry point) keeps the outer lineage. Deferred work carries the lineage captured when it
/// was queued, through `ChordLabState.send(command:lineage:)`.
struct ActionLineage {
  /// The open action's lineage; nil between actions.
  private(set) var captured: String?

  /// Opens an action with `token` unless one is open; true when this call opened it (and must call `end`).
  mutating func begin(token: String) -> Bool {
    guard captured == nil else { return false }
    captured = token
    return true
  }

  /// Closes the action the matching `begin` opened.
  mutating func end() { captured = nil }

  /// What a command or export request sent now carries: the open action's lineage, else `current` (the state's token).
  func lineage(current: String) -> String { captured ?? current }
}

extension ClipboardToken {
  /// The token `given` resolves to (`resolve`) and whether it is another lineage than `old`. Only a real change (a
  /// state load) may clear what was waiting for a reply; setting the same token again (a harness, an older shell)
  /// changes nothing.
  static func change(from old: String, to given: String?) -> (token: String, changed: Bool) {
    let token = resolve(given)
    return (token, token != old)
  }
}
