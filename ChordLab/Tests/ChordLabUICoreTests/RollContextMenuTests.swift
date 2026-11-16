import CoreGraphics
import XCTest
@testable import ChordLabUICore

/// The roll's context menu (UI/Core/RollContextMenu.swift, plan Task 20): Copy, Cut, Paste here, Duplicate, Delete;
/// what a right-click selects, which items are enabled, where Paste here lands, and what each item sends.
///
/// The snapshot-tracks fixture (see NoteKeysTests); geometry 48 pt per beat, 14-pt rows: tick t is x = t / 20, pitch p
/// is the row from y = (127 − p) × 14.
final class RollContextMenuTests: XCTestCase {
  private let geometry = RollGeometry(pxPerBeat: 48, rowHeight: 14, ticksPerBeat: 960)

  private func fixture(_ patch: (inout [String: Any]) -> Void = { _ in }) throws -> Snapshot {
    let url = try SnapshotTests.fixturesDirectory().appendingPathComponent("snapshot-tracks.json")
    var object = try XCTUnwrap(try JSONSerialization.jsonObject(with: Data(contentsOf: url)) as? [String: Any])
    patch(&object)
    return try JSONDecoder().decode(Snapshot.self, from: JSONSerialization.data(withJSONObject: object))
  }

  private func point(tick: Int, pitch: Int) -> CGPoint {
    CGPoint(x: geometry.x(tick: tick), y: geometry.y(pitch: pitch) + 7)
  }

  private func menu(_ snapshot: Snapshot, track: Int, at point: CGPoint, notesOnPasteboard: Bool = false,
                    grid: RollGrid = .eighth) -> RollContextMenu? {
    RollContextMenu.make(at: point, trackId: track, snapshot: snapshot, timing: RollTiming(snapshot: snapshot),
                         geometry: geometry, grid: grid, notesOnPasteboard: notesOnPasteboard)
  }

  private func enabled(_ menu: RollContextMenu?) -> [RollContextMenu.Item] {
    RollContextMenu.Item.allCases.filter { menu?.isEnabled($0) == true }
  }

  func testTheItemsInOrder() {
    XCTAssertEqual(RollContextMenu.Item.allCases.map(\.title), ["Copy", "Cut", "Paste here", "Duplicate", "Delete"])
  }

  // MARK: - What a right-click selects

  func testARightClickOnAnUnselectedNoteSelectsItFirst() throws {
    let m = try XCTUnwrap(menu(try fixture(), track: 1, at: point(tick: 4800, pitch: 45)))
    XCTAssertEqual(m.select, .selectNotes(trackId: 1, ids: [2]))
    XCTAssertEqual(m.targetIds, [2])
    XCTAssertEqual(enabled(m), [.copy, .cut, .duplicate, .delete], "no notes on the pasteboard")
  }

  func testARightClickOnASelectedNoteKeepsTheSelection() throws {
    let m = try XCTUnwrap(menu(try fixture(), track: 2, at: point(tick: 1200, pitch: 72)))
    XCTAssertNil(m.select)
    XCTAssertEqual(m.targetIds, [4, 5])
  }

  func testARightClickOnEmptySpaceKeepsTheSelection() throws {
    let m = try XCTUnwrap(menu(try fixture(), track: 2, at: point(tick: 3000, pitch: 60)))
    XCTAssertNil(m.select)
    XCTAssertEqual(m.targetIds, [4, 5])
    let none = try XCTUnwrap(menu(try fixture(), track: 1, at: point(tick: 3000, pitch: 60)))
    XCTAssertNil(none.select)
    XCTAssertEqual(none.targetIds, [])
    XCTAssertEqual(enabled(none), [], "nothing selected, nothing to paste")
  }

  func testARightClickOnAnInactiveNoteSelectsIt() throws {
    let m = try XCTUnwrap(menu(try fixture(), track: 1, at: point(tick: 9300, pitch: 41)))
    XCTAssertEqual(m.select, .selectNotes(trackId: 1, ids: [3]))
    XCTAssertEqual(m.targetIds, [3])
  }

  func testNoMenuForATrackTheSnapshotLacks() throws {
    XCTAssertNil(menu(try fixture(), track: 9, at: point(tick: 0, pitch: 60)))
  }

  // MARK: - Paste here

  func testPasteHereLandsOnTheClickFlooredToTheGrid() throws {
    let m = try XCTUnwrap(menu(try fixture(), track: 1, at: point(tick: 5300, pitch: 60), notesOnPasteboard: true))
    XCTAssertEqual(m.pasteTick, 5280)
    XCTAssertTrue(m.isEnabled(.pasteHere))
    let quarter = try XCTUnwrap(menu(try fixture(), track: 1, at: point(tick: 5300, pitch: 60), notesOnPasteboard: true, grid: .quarter))
    XCTAssertEqual(quarter.pasteTick, 4800)
  }

  func testPasteHereNeedsNotesOnThePasteboardAndAPositionInsideTheProgression() throws {
    XCTAssertFalse(try XCTUnwrap(menu(try fixture(), track: 1, at: point(tick: 5000, pitch: 60))).isEnabled(.pasteHere))
    let after = try XCTUnwrap(menu(try fixture(), track: 1, at: point(tick: 14400, pitch: 60), notesOnPasteboard: true))
    XCTAssertNil(after.pasteTick, "at the end (14400) and after it nothing owns the tick")
    XCTAssertFalse(after.isEnabled(.pasteHere))
    let last = try XCTUnwrap(menu(try fixture(), track: 1, at: point(tick: 14399, pitch: 60), notesOnPasteboard: true))
    XCTAssertEqual(last.pasteTick, 13920)
  }

  // MARK: - What each item sends

  private func commands(_ item: RollContextMenu.Item, _ m: RollContextMenu, _ snapshot: Snapshot,
                        board: NoteBoard = NoteBoard()) -> [RollCommand] {
    m.commands(for: item, snapshot: snapshot, timing: RollTiming(snapshot: snapshot), pasteboard: board)
  }

  func testEachItemActsOnItsTargets() throws {
    let snapshot = try fixture()
    let m = try XCTUnwrap(menu(snapshot, track: 2, at: point(tick: 3000, pitch: 60), notesOnPasteboard: true))
    let board = NoteBoard()
    XCTAssertEqual(commands(.copy, m, snapshot, board: board), [])
    XCTAssertEqual(NoteClipboard.paste(from: board)?.count, 2)
    XCTAssertEqual(commands(.pasteHere, m, snapshot, board: board), [
      .insertNotes(trackId: 2, notes: [RollNoteSpec(pitch: 72, start: 2880, length: 480, velocity: 110),
                                       RollNoteSpec(pitch: 79, start: 11040, length: 720, velocity: 127)], baseRevision: nil),
    ])
    XCTAssertEqual(commands(.cut, m, snapshot), [.removeNotes(trackId: 2, ids: [4, 5], baseRevision: 15)])
    XCTAssertEqual(commands(.delete, m, snapshot), [.removeNotes(trackId: 2, ids: [4, 5], baseRevision: 15)])
    XCTAssertEqual(commands(.duplicate, m, snapshot), [
      .insertNotes(trackId: 2, notes: [RollNoteSpec(pitch: 72, start: 9840, length: 480, velocity: 110),
                                       RollNoteSpec(pitch: 79, start: 18000, length: 720, velocity: 127)], baseRevision: 15),
    ])
  }

  func testACutWhoseWriteFailsRemovesNothing() throws {
    let snapshot = try fixture()
    let m = try XCTUnwrap(menu(snapshot, track: 2, at: point(tick: 3000, pitch: 60)))
    let failing = NoteBoard()
    failing.accepts = false
    XCTAssertEqual(commands(.cut, m, snapshot, board: failing), [])
  }

  func testItemsReadTheSnapshotCurrentWhenChosen() throws {
    let m = try XCTUnwrap(menu(try fixture(), track: 1, at: point(tick: 4800, pitch: 45)))
    XCTAssertEqual(m.targetIds, [2])
    // The selection echo has arrived (note 2 selected) and the revision moved on.
    let later = try fixture { $0["noteSelection"] = ["trackId": 1, "ids": [2]]; $0["revision"] = 16 }
    XCTAssertEqual(commands(.delete, m, later), [.removeNotes(trackId: 1, ids: [2], baseRevision: 16)])
    // Note 2 is gone by then: nothing to delete.
    let gone = try fixture { object in
      var tracks = object["tracks"] as! [[String: Any]]
      tracks[0]["notes"] = ["id": [1, 3], "pitch": [36, 41], "start": [0, 9120], "length": [960, 480], "velocity": [100, 80],
                            "anchor": [1, 3], "offset": [0, 1440], "active": [true, false]]
      object["tracks"] = tracks
    }
    XCTAssertEqual(commands(.delete, m, gone), [])
    XCTAssertEqual(commands(.duplicate, m, gone), [])
  }

  func testPasteHereSendsNothingWhenDisabled() throws {
    let snapshot = try fixture()
    let m = try XCTUnwrap(menu(snapshot, track: 1, at: point(tick: 14400, pitch: 60), notesOnPasteboard: true))
    let board = NoteBoard()
    _ = NoteKeys.copy(ids: [4], trackId: 2, snapshot: snapshot, pasteboard: board)
    XCTAssertEqual(commands(.pasteHere, m, snapshot, board: board), [])
  }
}
