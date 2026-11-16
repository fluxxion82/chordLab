import XCTest
@testable import ChordLabUICore

/// The roll toolbar's nudge buttons (UI/Core/RollNudge.swift, tracks design D15 "Group geometry" and "Keyboard", plan
/// Task 19): one `updateNotes` per press for the open track's selected notes, one shared clamped delta, carrying the
/// snapshot's revision; enabled only for a direction the selection can move in.
///
/// The snapshot-tracks fixture: revision 15, 15 beats (end tick 14400) in 4/4; Bass (1) notes 1 (36 at 0), 2 (45 at
/// 3840), 3 (41 at 9120, inactive); Lead (2) notes 4 (72 at 960) and 5 (79 at 9120), selected.
final class RollNudgeTests: XCTestCase {
  private func fixture(_ patch: (inout [String: Any]) -> Void = { _ in }) throws -> Snapshot {
    let url = try SnapshotTests.fixturesDirectory().appendingPathComponent("snapshot-tracks.json")
    var object = try XCTUnwrap(try JSONSerialization.jsonObject(with: Data(contentsOf: url)) as? [String: Any])
    patch(&object)
    return try JSONDecoder().decode(Snapshot.self, from: JSONSerialization.data(withJSONObject: object))
  }

  private func selecting(_ trackId: Int, _ ids: [Int]) -> (inout [String: Any]) -> Void {
    { $0["noteSelection"] = ["trackId": trackId, "ids": ids] }
  }

  private func command(_ direction: ArrowDirection, _ snapshot: Snapshot, track: Int = 2, grid: RollGrid = .eighth,
                       octave: Bool = false) -> RollCommand? {
    RollNudge.command(direction, octave: octave, trackId: track, snapshot: snapshot, timing: RollTiming(snapshot: snapshot),
                      grid: grid)
  }

  func testEachDirectionIsOneUpdateOfTheSelectionWithTheSnapshotsRevision() throws {
    let snapshot = try fixture()
    XCTAssertEqual(command(.up, snapshot), .updateNotes(trackId: 2, edits: [RollNoteEdit(id: 4, pitch: 73), RollNoteEdit(id: 5, pitch: 80)],
                                                         baseRevision: 15))
    XCTAssertEqual(command(.down, snapshot), .updateNotes(trackId: 2, edits: [RollNoteEdit(id: 4, pitch: 71), RollNoteEdit(id: 5, pitch: 78)],
                                                           baseRevision: 15))
    XCTAssertEqual(command(.right, snapshot), .updateNotes(trackId: 2, edits: [RollNoteEdit(id: 4, start: 1440), RollNoteEdit(id: 5, start: 9600)],
                                                            baseRevision: 15), "one eighth (480 ticks)")
    XCTAssertEqual(command(.left, snapshot), .updateNotes(trackId: 2, edits: [RollNoteEdit(id: 4, start: 480), RollNoteEdit(id: 5, start: 8640)],
                                                           baseRevision: 15))
    XCTAssertEqual(command(.right, snapshot, grid: .sixteenthTriplet),
                   .updateNotes(trackId: 2, edits: [RollNoteEdit(id: 4, start: 1120), RollNoteEdit(id: 5, start: 9280)], baseRevision: 15),
                   "the track's grid step (1/16T = 160 ticks)")
    XCTAssertEqual(command(.left, snapshot, grid: .off),
                   .updateNotes(trackId: 2, edits: [RollNoteEdit(id: 4, start: 950), RollNoteEdit(id: 5, start: 9110)], baseRevision: 15),
                   "Off steps 10 ticks")
    XCTAssertEqual(command(.up, snapshot, octave: true),
                   .updateNotes(trackId: 2, edits: [RollNoteEdit(id: 4, pitch: 84), RollNoteEdit(id: 5, pitch: 91)], baseRevision: 15),
                   "the octave form the arrow keys will use (plan Task 20)")
  }

  func testNothingWithoutASelectionOnThatTrack() throws {
    let snapshot = try fixture()
    for direction in [ArrowDirection.left, .right, .up, .down] {
      XCTAssertNil(command(direction, snapshot, track: 1), "Bass has no selection (Lead's is not Bass's)")
    }
    XCTAssertEqual(RollNudge.availability(trackId: 1, snapshot: snapshot, timing: RollTiming(snapshot: snapshot), grid: .eighth),
                   .none)
    let cleared = try fixture { $0["noteSelection"] = NSNull() }
    XCTAssertNil(command(.up, cleared))
    let stale = try fixture(selecting(2, [99]))
    XCTAssertNil(command(.up, stale), "ids that name no note")
    XCTAssertEqual(RollNudge.availability(trackId: 2, snapshot: stale, timing: RollTiming(snapshot: stale), grid: .eighth), .none)
    let noTracks = try fixture { $0.removeValue(forKey: "tracks"); $0.removeValue(forKey: "noteSelection") }
    XCTAssertNil(command(.up, noTracks))
  }

  func testTheGroupClampsAsOneAndADirectionWithNoRoomIsDisabled() throws {
    // Note 1 starts at tick 0: the group cannot move left at all; right, up and down are free.
    let atZero = try fixture(selecting(1, [1, 2]))
    XCTAssertNil(command(.left, atZero, track: 1))
    XCTAssertEqual(RollNudge.availability(trackId: 1, snapshot: atZero, timing: RollTiming(snapshot: atZero), grid: .eighth),
                   RollNudgeAvailability(left: false, right: true, up: true, down: true))
    // A partial step is taken (the group clamp, as a drag): with note 1 at tick 15, a left nudge of 480 moves the
    // whole group by −15.
    let near = try fixture {
      var tracks = $0["tracks"] as! [[String: Any]]
      var notes = tracks[0]["notes"] as! [String: Any]
      notes["start"] = [15, 3840, 9120]
      notes["offset"] = [15, 0, 1440]
      notes["pitch"] = [127, 45, 41]
      tracks[0]["notes"] = notes
      $0["tracks"] = tracks
      $0["noteSelection"] = ["trackId": 1, "ids": [1, 2]]
    }
    XCTAssertEqual(command(.left, near, track: 1),
                   .updateNotes(trackId: 1, edits: [RollNoteEdit(id: 1, start: 0), RollNoteEdit(id: 2, start: 3825)], baseRevision: 15),
                   "one shared delta: −15 for both, so the spacing survives")
    XCTAssertNil(command(.up, near, track: 1), "pitch 127 cannot go up, so the group stays")
    XCTAssertEqual(RollNudge.availability(trackId: 1, snapshot: near, timing: RollTiming(snapshot: near), grid: .eighth),
                   RollNudgeAvailability(left: true, right: true, up: false, down: true))
  }

  func testAvailabilityReadsByDirection() {
    let some = RollNudgeAvailability(left: true, right: false, up: true, down: false)
    XCTAssertTrue(some[.left])
    XCTAssertFalse(some[.right])
    XCTAssertTrue(some[.up])
    XCTAssertFalse(some[.down])
    XCTAssertFalse(RollNudgeAvailability.none[.up])
  }
}
