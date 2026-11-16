import Foundation

/// What one drag-out's export request came to (tracks design D12): the shell answers `kMsgTagExportClip` inline,
/// inside the send (`ChordLab::OnMessage` runs synchronously on the main thread), with `kMsgTagExportReady` or
/// `kMsgTagError`.
enum ExportOutcome: Equatable {
  /// Nothing was sent: the grip had nothing to export (`ClipExport.request` gave nil).
  case nothing
  /// The shell wrote this file for this request.
  case ready(path: String)
  /// The shell answered with an error ("nothing to export", a refused tempo), or the message could not be sent.
  case failed
  /// The request went out and no answer arrived before the send returned (a host where the answer is not inline).
  case pending

  /// The file to hand the drag: only a file this request produced.
  var fileURL: URL? {
    if case .ready(let path) = self { return URL(fileURLWithPath: path) }
    return nil
  }

  /// Whether the bottom drag-out says "preparing…" (`ClipExport.dragOutTitle`): only while an answer is outstanding,
  /// never after a failure.
  var isPending: Bool { self == .pending }
}

/// One drag's result: the outcome and the item provider handed to `.onDrag` (empty unless `.ready`).
struct ExportDrag {
  let outcome: ExportOutcome
  let provider: NSItemProvider
}

/// The drag-outs' round trip (tracks design D12), so `ChordLabState` only wires it: `drag` sends a request through the
/// injected sender and decides the outcome from the answer that arrived inside that send (`receivedPath`,
/// `receivedError`, which the state calls from `applyExportReady` / `applyError`). An answer outside a request belongs
/// to none: no request is ever handed an earlier request's file. An error that answers a request is the export's
/// (`receivedError` returns true): the state shows its text and skips a refused command's side effects (the pending key
/// change, the pending track add).
///
/// Depends on nothing but Foundation; unit-tested by the `ChordLabUICore` package
/// (Tests/ChordLabUICoreTests/ClipExportExchangeTests.swift).
final class ClipExportExchange {
  /// True while `drag` is inside its send.
  private var inFlight = false
  /// The first answer that arrived during the send in flight.
  private var answer: ExportOutcome?

  /// Sends `request` through `send` (which returns whether the message went out) and returns the outcome with its item
  /// provider; a nil request sends nothing (`.nothing`).
  func drag(_ request: ClipExportRequest?, send: (ClipExportRequest) -> Bool) -> ExportDrag {
    let outcome = run(request, send: send)
    return ExportDrag(outcome: outcome, provider: Self.itemProvider(for: outcome))
  }

  /// `drag`'s outcome alone.
  func run(_ request: ClipExportRequest?, send: (ClipExportRequest) -> Bool) -> ExportOutcome {
    guard let request else { return .nothing }
    inFlight = true
    answer = nil
    let delivered = send(request)
    inFlight = false
    defer { answer = nil }
    guard delivered else { return .failed }
    return answer ?? .pending
  }

  /// `kMsgTagExportReady`: true when it answers the request in flight (the first answer wins).
  @discardableResult func receivedPath(_ path: String) -> Bool {
    guard inFlight else { return false }
    if answer == nil { answer = .ready(path: path) }
    return true
  }

  /// `kMsgTagError`: true when it answers the request in flight, an export failure (the first answer wins); false for
  /// an error outside a request, a refused command's.
  func receivedError(_ message: String) -> Bool {
    guard inFlight else { return false }
    if answer == nil { answer = .failed }
    return true
  }

  /// The provider for `outcome`: the file of a `.ready` outcome, otherwise an empty provider (the drop carries
  /// nothing).
  static func itemProvider(for outcome: ExportOutcome) -> NSItemProvider {
    outcome.fileURL.flatMap { NSItemProvider(contentsOf: $0) } ?? NSItemProvider()
  }
}
