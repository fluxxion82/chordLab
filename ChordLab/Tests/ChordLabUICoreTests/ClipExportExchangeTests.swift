import Foundation
import XCTest
@testable import ChordLabUICore

/// The drag-outs' round trip (UI/Core/ClipExportExchange.swift, tracks design D12): a request goes out through the
/// injected sender, the shell's inline answer (a path or an error) decides the outcome, and only a file this request
/// produced ever reaches an item provider. An error answering an export is claimed by the exchange, so the state shows
/// its text without the refusal side effects of a rejected command.
final class ClipExportExchangeTests: XCTestCase {
  private let request = ClipExportRequest(lanes: .some([.chords]), sectionId: nil)

  /// A file that exists, so `NSItemProvider(contentsOf:)` can carry it.
  private func tempFile(_ name: String) throws -> String {
    let dir = FileManager.default.temporaryDirectory.appendingPathComponent("ClipExportExchangeTests-\(UUID().uuidString)")
    try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
    let url = dir.appendingPathComponent(name)
    try Data("MThd".utf8).write(to: url)
    addTeardownBlock { try? FileManager.default.removeItem(at: dir) }
    return url.path
  }

  func testANilRequestSendsNothingAndGivesAnEmptyProvider() {
    let exchange = ClipExportExchange()
    var sent = 0
    let drag = exchange.drag(nil) { _ in sent += 1; return true }
    XCTAssertEqual(sent, 0)
    XCTAssertEqual(drag.outcome, .nothing)
    XCTAssertTrue(drag.provider.registeredTypeIdentifiers.isEmpty)
    XCTAssertFalse(drag.outcome.isPending)
  }

  func testAPathAnswerIsReadyAndTheProviderCarriesThatFile() throws {
    let exchange = ClipExportExchange()
    let path = try tempFile("ChordLab-test-1.mid")
    var sentRequests: [ClipExportRequest] = []
    let drag = exchange.drag(request) { r in
      sentRequests.append(r)
      XCTAssertTrue(exchange.receivedPath(path), "an inline answer belongs to the request in flight")
      return true
    }
    XCTAssertEqual(sentRequests, [request], "exactly one send of exactly the request")
    XCTAssertEqual(drag.outcome, .ready(path: path))
    XCTAssertEqual(drag.outcome.fileURL?.path, path)
    XCTAssertTrue(drag.provider.registeredTypeIdentifiers.contains("public.midi-audio"),
                  "\(drag.provider.registeredTypeIdentifiers)")
  }

  func testAnErrorAnswerFailsAndIsClaimedByTheExport() {
    let exchange = ClipExportExchange()
    var claimed = false
    let drag = exchange.drag(request) { _ in
      claimed = exchange.receivedError("nothing to export")
      return true
    }
    XCTAssertTrue(claimed, "the error answers the export: the state shows its text, not a command refusal")
    XCTAssertEqual(drag.outcome, .failed)
    XCTAssertFalse(drag.outcome.isPending, "a failed export shows no 'preparing…'")
    XCTAssertTrue(drag.provider.registeredTypeIdentifiers.isEmpty)
  }

  func testNoAnswerIsPending() {
    let exchange = ClipExportExchange()
    let drag = exchange.drag(request) { _ in true }
    XCTAssertEqual(drag.outcome, .pending)
    XCTAssertTrue(drag.outcome.isPending)
    XCTAssertTrue(drag.provider.registeredTypeIdentifiers.isEmpty)
  }

  func testAMessageThatCouldNotBeSentFails() {
    let exchange = ClipExportExchange()
    XCTAssertEqual(exchange.drag(request) { _ in false }.outcome, .failed)
  }

  func testNoStalePathIsEverHandedOut() throws {
    let exchange = ClipExportExchange()
    let first = try tempFile("ChordLab-test-first.mid")
    XCTAssertEqual(exchange.drag(request) { _ in exchange.receivedPath(first); return true }.outcome, .ready(path: first))
    // The next request fails: the earlier file is not offered again.
    let failed = exchange.drag(request) { _ in _ = exchange.receivedError("refused tempo"); return true }
    XCTAssertEqual(failed.outcome, .failed)
    XCTAssertTrue(failed.provider.registeredTypeIdentifiers.isEmpty)
    // Nor when the next one gets no answer at all.
    let pending = exchange.drag(request) { _ in true }
    XCTAssertEqual(pending.outcome, .pending)
    XCTAssertTrue(pending.provider.registeredTypeIdentifiers.isEmpty)
    // A path arriving between requests (a late answer) belongs to none of them.
    XCTAssertFalse(exchange.receivedPath(first))
    XCTAssertEqual(exchange.drag(request) { _ in true }.outcome, .pending)
  }

  func testOnlyAnswersInsideARequestAreClaimed() {
    let exchange = ClipExportExchange()
    XCTAssertFalse(exchange.receivedError("no such track"), "outside a request an error is a refused command's")
    _ = exchange.drag(request) { _ in true }
    XCTAssertFalse(exchange.receivedError("no such track"), "after the request returned, too")
    XCTAssertFalse(exchange.receivedPath("/tmp/x.mid"))
  }

  func testTheFirstAnswerWins() throws {
    let exchange = ClipExportExchange()
    let path = try tempFile("ChordLab-test-2.mid")
    let drag = exchange.drag(request) { _ in
      _ = exchange.receivedError("nothing to export")
      exchange.receivedPath(path)
      return true
    }
    XCTAssertEqual(drag.outcome, .failed)
    XCTAssertTrue(drag.provider.registeredTypeIdentifiers.isEmpty)
  }
}
