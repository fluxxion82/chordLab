import XCTest
@testable import ChordLabUICore

/// The keyboard with a track's notes targeted (UI/Core/NoteKeys.swift, tracks design D15 "Keyboard", plan Task 20):
/// Delete, ⌘C ⌘X ⌘V ⌘D ⌘A and the arrows, each at most one command (one undo step), nothing when there is nothing to
/// act on.
///
/// The snapshot-tracks fixture: revision 15, 15 beats (end tick 14400) in 4/4; Bass (1) notes 1 (36 at 0, 960 long),
/// 2 (45 at 3840, 1920 long, velocity 90), 3 (41 at 9120, 480 long, inactive, velocity 80); Lead (2) notes 4 (72 at
/// 960, 480 long, velocity 110) and 5 (79 at 9120, 720 long, velocity 127), selected.
final class NoteKeysTests: XCTestCase {
  private func fixture(_ patch: (inout [String: Any]) -> Void = { _ in }) throws -> Snapshot {
    let url = try SnapshotTests.fixturesDirectory().appendingPathComponent("snapshot-tracks.json")
    var object = try XCTUnwrap(try JSONSerialization.jsonObject(with: Data(contentsOf: url)) as? [String: Any])
    patch(&object)
    return try JSONDecoder().decode(Snapshot.self, from: JSONSerialization.data(withJSONObject: object))
  }

  private func selecting(_ trackId: Int, _ ids: [Int]) -> (inout [String: Any]) -> Void {
    { $0["noteSelection"] = ["trackId": trackId, "ids": ids] }
  }

  private func run(_ command: KeyCommand, _ snapshot: Snapshot, track: Int = 2, marker: Int? = nil,
                   grid: RollGrid = .eighth, board: NoteBoard = NoteBoard()) -> [RollCommand] {
    NoteKeys.commands(for: command, trackId: track, snapshot: snapshot, timing: RollTiming(snapshot: snapshot),
                      grid: grid, insertMarker: marker, pasteboard: board)
  }

  // MARK: - Delete

  func testDeleteRemovesTheSelectedNotesWithTheSnapshotsRevision() throws {
    let snapshot = try fixture()
    XCTAssertEqual(run(.deleteSelection, snapshot), [.removeNotes(trackId: 2, ids: [4, 5], baseRevision: 15)])
  }

  func testDeleteDoesNothingWithoutASelectionOnThatTrack() throws {
    let snapshot = try fixture()
    XCTAssertEqual(run(.deleteSelection, snapshot, track: 1), [], "the selection is Lead's")
    XCTAssertEqual(run(.deleteSelection, try fixture { $0["noteSelection"] = NSNull() }), [])
    XCTAssertEqual(run(.deleteSelection, snapshot, track: 9), [], "no such track")
  }

  func testDeleteRemovesAnInactiveNoteToo() throws {
    XCTAssertEqual(run(.deleteSelection, try fixture(selecting(1, [3])), track: 1),
                   [.removeNotes(trackId: 1, ids: [3], baseRevision: 15)])
  }

  // MARK: - Copy and cut

  func testCopyWritesTheSelectedNotesAndSendsNothing() throws {
    let board = NoteBoard()
    XCTAssertEqual(run(.copy, try fixture(), board: board), [])
    XCTAssertEqual(NoteClipboard.paste(from: board), [ClipNote(pitch: 72, start: 0, length: 480, velocity: 110),
                                                     ClipNote(pitch: 79, start: 8160, length: 720, velocity: 127)])
    XCTAssertEqual(board.items.first?.text, "C5 G5")
  }

  func testCopyWithoutASelectionLeavesThePasteboardAlone() throws {
    let board = NoteBoard()
    XCTAssertEqual(run(.copy, try fixture(), track: 1, board: board), [])
    XCTAssertEqual(board.cleared, 0)
  }

  func testCutRemovesOnlyAfterACheckedWrite() throws {
    let failing = NoteBoard()
    failing.accepts = false
    XCTAssertEqual(run(.cut, try fixture(), board: failing), [], "the write failed: nothing removed")
    let board = NoteBoard()
    XCTAssertEqual(run(.cut, try fixture(), board: board), [.removeNotes(trackId: 2, ids: [4, 5], baseRevision: 15)])
    XCTAssertNotNil(NoteClipboard.paste(from: board))
  }

  // MARK: - Paste

  func testPasteLandsAtTheMarkerWithoutABaseRevision() throws {
    let board = NoteBoard()
    _ = run(.copy, try fixture(), board: board)
    XCTAssertEqual(run(.paste, try fixture(), track: 1, marker: 1920, board: board), [
      .insertNotes(trackId: 1, notes: [RollNoteSpec(pitch: 72, start: 1920, length: 480, velocity: 110),
                                       RollNoteSpec(pitch: 79, start: 10080, length: 720, velocity: 127)], baseRevision: nil),
    ])
  }

  func testPasteWithoutAMarkerLandsAfterTheSelectionElseAtZero() throws {
    let board = NoteBoard()
    _ = run(.copy, try fixture(selecting(1, [1])), track: 1, board: board)
    XCTAssertEqual(run(.paste, try fixture(selecting(1, [1, 2])), track: 1, board: board),
                   [.insertNotes(trackId: 1, notes: [RollNoteSpec(pitch: 36, start: 5760, length: 960, velocity: 100)], baseRevision: nil)],
                   "after the selection's end (note 2 ends at 5760)")
    XCTAssertEqual(run(.paste, try fixture(), track: 1, board: board),
                   [.insertNotes(trackId: 1, notes: [RollNoteSpec(pitch: 36, start: 0, length: 960, velocity: 100)], baseRevision: nil)],
                   "no selection on Bass, no marker: tick 0")
  }

  func testPasteDoesNothingWithoutChordLabNotesOnThePasteboard() throws {
    XCTAssertEqual(run(.paste, try fixture(), marker: 0), [])
  }

  // MARK: - Duplicate

  func testDuplicatePlacesACopyRightAfterTheSelectionsEndWithTheRevision() throws {
    // Lead [4, 5]: spans 960…9840, so the copy starts at 9840 and keeps the spacing (8160 between the starts).
    XCTAssertEqual(run(.duplicate, try fixture(), marker: 0), [
      .insertNotes(trackId: 2, notes: [RollNoteSpec(pitch: 72, start: 9840, length: 480, velocity: 110),
                                       RollNoteSpec(pitch: 79, start: 18000, length: 720, velocity: 127)], baseRevision: 15),
    ], "the marker does not move a duplicate")
    XCTAssertEqual(run(.duplicate, try fixture(selecting(1, [1]))), [], "the selection is Bass's")
  }

  func testDuplicateOfOneNoteFollowsIt() throws {
    XCTAssertEqual(run(.duplicate, try fixture(selecting(1, [2])), track: 1),
                   [.insertNotes(trackId: 1, notes: [RollNoteSpec(pitch: 45, start: 5760, length: 1920, velocity: 90)], baseRevision: 15)])
  }

  func testDuplicateLeavesThePasteboardAlone() throws {
    let board = NoteBoard()
    _ = run(.duplicate, try fixture(), board: board)
    XCTAssertEqual(board.cleared, 0)
    XCTAssertTrue(board.items.isEmpty)
  }

  // MARK: - Select all

  func testSelectAllSelectsEveryNoteOfTheTrack() throws {
    XCTAssertEqual(run(.selectAll, try fixture(), track: 1), [.selectNotes(trackId: 1, ids: [1, 2, 3])])
  }

  func testSelectAllDoesNothingWhenEverythingIsSelectedOrThereAreNoNotes() throws {
    XCTAssertEqual(run(.selectAll, try fixture()), [], "Lead's two notes are already selected")
    let empty = try fixture { object in
      var tracks = object["tracks"] as! [[String: Any]]
      tracks[0]["notes"] = ["id": [], "pitch": [], "start": [], "length": [], "velocity": [], "anchor": [], "offset": [], "active": []]
      object["tracks"] = tracks
    }
    XCTAssertEqual(run(.selectAll, empty, track: 1), [])
  }

  // MARK: - Arrows

  func testArrowsNudgeBySemitoneOctaveAndGridStep() throws {
    let snapshot = try fixture()
    XCTAssertEqual(run(.nudge(.up, octave: false), snapshot),
                   [.updateNotes(trackId: 2, edits: [RollNoteEdit(id: 4, pitch: 73), RollNoteEdit(id: 5, pitch: 80)], baseRevision: 15)])
    XCTAssertEqual(run(.nudge(.down, octave: true), snapshot),
                   [.updateNotes(trackId: 2, edits: [RollNoteEdit(id: 4, pitch: 60), RollNoteEdit(id: 5, pitch: 67)], baseRevision: 15)])
    XCTAssertEqual(run(.nudge(.right, octave: false), snapshot),
                   [.updateNotes(trackId: 2, edits: [RollNoteEdit(id: 4, start: 1440), RollNoteEdit(id: 5, start: 9600)], baseRevision: 15)])
    XCTAssertEqual(run(.nudge(.left, octave: true), snapshot, grid: .quarter),
                   [.updateNotes(trackId: 2, edits: [RollNoteEdit(id: 4, start: 0), RollNoteEdit(id: 5, start: 8160)], baseRevision: 15)],
                   "⇧ is ignored horizontally: one grid step")
  }

  func testAnOctaveThatDoesNotFitMovesNothing() throws {
    XCTAssertEqual(run(.nudge(.up, octave: true), try fixture { object in
      object["noteSelection"] = ["trackId": 2, "ids": [5]]
      var tracks = object["tracks"] as! [[String: Any]]
      var notes = tracks[1]["notes"] as! [String: Any]
      notes["pitch"] = [72, 120]
      tracks[1]["notes"] = notes
      object["tracks"] = tracks
    }), [])
  }

  func testArrowsDoNothingWithoutASelection() throws {
    XCTAssertEqual(run(.nudge(.up, octave: false), try fixture(), track: 1), [])
  }

  // MARK: - Not note commands

  func testUndoRedoAndDigitsAreNotNoteCommands() throws {
    let snapshot = try fixture()
    for command in [KeyCommand.undo, .redo, .appendSuggestion(1)] {
      XCTAssertEqual(run(command, snapshot), [], "\(command)")
    }
  }
}

/// A pasteboard for the note type; `accepts` false makes every write fail.
final class NoteBoard: ChordPasteboard {
  var accepts = true
  private(set) var cleared = 0
  private(set) var items: [ClipboardItem] = []

  func clear() {
    cleared += 1
    items = []
  }

  func write(_ item: ClipboardItem) -> Bool {
    guard accepts else { return false }
    items.append(item)
    return true
  }

  func data(forType type: String) -> Data? {
    items.last { $0.type == type }?.data
  }
}
