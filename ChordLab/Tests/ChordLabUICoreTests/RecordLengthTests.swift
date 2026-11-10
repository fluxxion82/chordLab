import XCTest
@testable import ChordLabUICore

/// What a press becomes while Record is on (UI/Core/RecordLength.swift, strip editing design D9).
final class RecordLengthTests: XCTestCase {
  func testATapIsAPressUnderAFifthOfASecond() {
    XCTAssertEqual(RecordLength.beats(heldSeconds: 0.199, bpm: 120, defaultBeats: 4), 4, "a tap: the default")
    XCTAssertEqual(RecordLength.beats(heldSeconds: 0.2, bpm: 120, defaultBeats: 4), 0.5, "0.4 beats")
    XCTAssertEqual(RecordLength.beats(heldSeconds: 0.25, bpm: 120, defaultBeats: 4), 0.5, "0.5 beats")
    XCTAssertEqual(RecordLength.beats(heldSeconds: 0.199, bpm: 90, defaultBeats: 3.5), 3.5, "a tap at 90 too")
    XCTAssertEqual(RecordLength.beats(heldSeconds: 0.2, bpm: 90, defaultBeats: 3.5), 0.25, "0.3 beats")
    XCTAssertEqual(RecordLength.beats(heldSeconds: 0, bpm: 120, defaultBeats: 1.5), 1.5)
    XCTAssertEqual(RecordLength.beats(heldSeconds: -1, bpm: 120, defaultBeats: 1.5), 1.5, "negative: the default")
    XCTAssertEqual(RecordLength.beats(heldSeconds: .nan, bpm: 120, defaultBeats: 1.5), 1.5)
  }

  func testHalvesRoundUp() {
    XCTAssertEqual(RecordLength.beats(heldSeconds: 0.25, bpm: 90, defaultBeats: 4), 0.5, "0.375 beats")
    XCTAssertEqual(RecordLength.beats(heldSeconds: 0.3125, bpm: 120, defaultBeats: 4), 0.75, "0.625 beats")
  }

  func testTheHeldTimeAtTheTempoOnTheQuarterBeatGrid() {
    XCTAssertEqual(RecordLength.beats(heldSeconds: 0.5, bpm: 120, defaultBeats: 4), 1)
    XCTAssertEqual(RecordLength.beats(heldSeconds: 1.0, bpm: 120, defaultBeats: 4), 2)
    XCTAssertEqual(RecordLength.beats(heldSeconds: 2.0, bpm: 120, defaultBeats: 4), 4)
    XCTAssertEqual(RecordLength.beats(heldSeconds: 1.1, bpm: 120, defaultBeats: 4), 2.25, "2.2 beats")
    XCTAssertEqual(RecordLength.beats(heldSeconds: 2.0, bpm: 90, defaultBeats: 4), 3)
    XCTAssertEqual(RecordLength.beats(heldSeconds: 1.0, bpm: 90, defaultBeats: 4), 1.5)
  }

  func testTheLimitsAndAMissingTempo() {
    XCTAssertEqual(RecordLength.beats(heldSeconds: 60, bpm: 120, defaultBeats: 4), 64)
    XCTAssertEqual(RecordLength.beats(heldSeconds: .infinity, bpm: 120, defaultBeats: 4), 64, "an endless hold: the longest")
    XCTAssertEqual(RecordLength.beats(heldSeconds: 1.0, bpm: 0, defaultBeats: 4), 2, "no tempo: 120")
    XCTAssertEqual(RecordLength.beats(heldSeconds: 1.0, bpm: .nan, defaultBeats: 4), 2)
    XCTAssertEqual(RecordLength.beats(heldSeconds: 1.0, bpm: -90, defaultBeats: 4), 2)
  }

  func testOnlyAButtonUpWhileRecordingRecords() {
    let held = PressRelease(reason: .buttonUp, heldSeconds: 1)
    XCTAssertEqual(RecordLength.recordedBeats(recording: true, release: held, bpm: 120, defaultBeats: 4), 2)
    XCTAssertNil(RecordLength.recordedBeats(recording: false, release: held, bpm: 120, defaultBeats: 4), "Record off")
    for reason in [PressRelease.Reason.dragged, .superseded, .disappeared] {
      XCTAssertNil(RecordLength.recordedBeats(recording: true, release: PressRelease(reason: reason, heldSeconds: 1),
                                              bpm: 120, defaultBeats: 4), "\(reason)")
    }
  }

  /// How a press on a 40-pt cell ends, held from `down` to `up` (event timestamps), dragged or not, its events
  /// delivered to `PressLogic` in one update or in two.
  private func endOfPress(down: TimeInterval, up: TimeInterval, dragged: Bool, coalesced: Bool) -> PressRelease? {
    let frame = CGRect(x: 0, y: 0, width: 40, height: 40)
    let region = PressRegion(frame: frame, visible: frame, circular: false)
    var logic = PressLogic()
    let start = MouseButton()
    var pressed = start
    pressed.down(at: CGPoint(x: 20, y: 20), time: down)
    var ended = pressed
    if dragged { ended.dragged(to: CGPoint(x: 60, y: 20)) }
    ended.up(time: up)
    let events = coalesced
      ? logic.update(from: start, to: ended, region: region)
      : logic.update(from: start, to: pressed, region: region) + logic.update(from: pressed, to: ended, region: region)
    for case .release(let release) in events { return release }
    return nil
  }

  func testAHoldRecordsTheSameLengthWhetherItsEventsCoalesceOrNot() throws {
    for coalesced in [false, true] {
      let release = try XCTUnwrap(endOfPress(down: 1.0, up: 1.8, dragged: false, coalesced: coalesced))
      XCTAssertEqual(release, PressRelease(reason: .buttonUp, heldSeconds: 0.8))
      XCTAssertEqual(RecordLength.recordedBeats(recording: true, release: release, bpm: 120, defaultBeats: 4), 1.5,
                     "0.8 s at 120 bpm is 1.6 beats: 1.5 (coalesced: \(coalesced))")
      let tap = try XCTUnwrap(endOfPress(down: 1.0, up: 1.1, dragged: false, coalesced: coalesced))
      XCTAssertEqual(RecordLength.recordedBeats(recording: true, release: tap, bpm: 120, defaultBeats: 4), 4,
                     "0.1 s is a tap either way (coalesced: \(coalesced))")
    }
  }

  func testAPressThatBecameADropRecordsNothing() throws {
    for coalesced in [false, true] {
      let release = try XCTUnwrap(endOfPress(down: 1.0, up: 1.8, dragged: true, coalesced: coalesced))
      XCTAssertEqual(release.reason, .dragged)
      XCTAssertNil(RecordLength.recordedBeats(recording: true, release: release, bpm: 120, defaultBeats: 4),
                   "the drop appends its one chord; Record adds none (coalesced: \(coalesced))")
    }
  }

  func testTheCommand() throws {
    let data = try JSONSerialization.data(withJSONObject: RecordLength.command(root: 9, type: "min", beats: 1.5, index: 2),
                                          options: [.sortedKeys])
    XCTAssertEqual(String(data: data, encoding: .utf8),
                   #"{"chords":[{"lengthBeats":1.5,"root":9,"type":"min"}],"cmd":"insertChords","index":2}"#)
  }
}
