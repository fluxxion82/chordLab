import XCTest
@testable import ChordLabUICore

/// The roll's grid options and the insert length rule (UI/Core/RollGrid.swift, tracks design D14, D15).
final class RollGridTests: XCTestCase {
  func testStepsAt960TicksPerBeat() {
    XCTAssertEqual(RollGrid.quarter.ticks(), 960)
    XCTAssertEqual(RollGrid.eighth.ticks(), 480)
    XCTAssertEqual(RollGrid.sixteenth.ticks(), 240)
    XCTAssertEqual(RollGrid.thirtySecond.ticks(), 120)
    XCTAssertEqual(RollGrid.eighthTriplet.ticks(), 320)
    XCTAssertEqual(RollGrid.sixteenthTriplet.ticks(), 160)
    XCTAssertEqual(RollGrid.off.ticks(), 10)
  }

  func testStepsFollowTheSnapshotsTicksPerBeat() {
    XCTAssertEqual(RollGrid.quarter.ticks(ticksPerBeat: 480), 480)
    XCTAssertEqual(RollGrid.eighthTriplet.ticks(ticksPerBeat: 480), 160)
    XCTAssertEqual(RollGrid.off.ticks(ticksPerBeat: 480), 5)
    XCTAssertEqual(RollGrid.thirtySecond.ticks(ticksPerBeat: 4), 1, "never below one tick")
    XCTAssertEqual(RollGrid.off.ticks(ticksPerBeat: 4), 1)
    XCTAssertEqual(RollGrid.quarter.ticks(ticksPerBeat: 0), 1)
  }

  func testEveryStepDividesABeatSoTheGridIsBeatAlignedInEveryMeter() {
    for grid in RollGrid.allCases {
      XCTAssertEqual(960 % grid.ticks(), 0, "\(grid)")
    }
    // A bar of 3/4, 6/8 or 7/8 starts on a beat, so on a step of every grid.
    for beats in [3, 4, 7] {
      for grid in RollGrid.allCases {
        XCTAssertEqual(grid.floor(beats * 960), beats * 960, "\(grid) at beat \(beats)")
      }
    }
  }

  func testTitlesAndTheDefault() {
    XCTAssertEqual(RollGrid.allCases.map(\.title), ["1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T", "Off"])
    XCTAssertEqual(RollGrid.default, .eighth)
    XCTAssertEqual(RollGrid.allCases.count, 7)
  }

  func testFloorOnStraightGrids() {
    XCTAssertEqual(RollGrid.eighth.floor(0), 0)
    XCTAssertEqual(RollGrid.eighth.floor(479), 0)
    XCTAssertEqual(RollGrid.eighth.floor(480), 480)
    XCTAssertEqual(RollGrid.eighth.floor(1439), 960)
    XCTAssertEqual(RollGrid.quarter.floor(1919), 960)
    XCTAssertEqual(RollGrid.sixteenth.floor(250), 240)
    XCTAssertEqual(RollGrid.thirtySecond.floor(239), 120)
    XCTAssertEqual(RollGrid.off.floor(19), 10)
    XCTAssertEqual(RollGrid.off.floor(9), 0)
  }

  func testFloorOnTripletGrids() {
    XCTAssertEqual(RollGrid.eighthTriplet.floor(319), 0)
    XCTAssertEqual(RollGrid.eighthTriplet.floor(320), 320)
    XCTAssertEqual(RollGrid.eighthTriplet.floor(959), 640)
    XCTAssertEqual(RollGrid.eighthTriplet.floor(960), 960)
    XCTAssertEqual(RollGrid.sixteenthTriplet.floor(479), 320)
    XCTAssertEqual(RollGrid.sixteenthTriplet.floor(480), 480)
  }

  func testRoundGoesToTheNearestStepWithTiesUp() {
    XCTAssertEqual(RollGrid.eighth.round(0), 0)
    XCTAssertEqual(RollGrid.eighth.round(239), 0)
    XCTAssertEqual(RollGrid.eighth.round(240), 480, "a tie goes to the later step")
    XCTAssertEqual(RollGrid.eighth.round(241), 480)
    XCTAssertEqual(RollGrid.eighth.round(480), 480)
    XCTAssertEqual(RollGrid.eighthTriplet.round(159), 0)
    XCTAssertEqual(RollGrid.eighthTriplet.round(160), 320)
    XCTAssertEqual(RollGrid.eighthTriplet.round(479), 320)
    XCTAssertEqual(RollGrid.eighthTriplet.round(480), 640)
    XCTAssertEqual(RollGrid.sixteenthTriplet.round(80), 160)
    XCTAssertEqual(RollGrid.off.round(4), 0)
    XCTAssertEqual(RollGrid.off.round(5), 10)
  }

  func testFloorAndRoundNeverGoBelowTickZero() {
    for grid in RollGrid.allCases {
      XCTAssertEqual(grid.floor(-1), 0)
      XCTAssertEqual(grid.floor(-5000), 0)
      XCTAssertEqual(grid.round(-1), 0)
      XCTAssertEqual(grid.round(-5000), 0)
    }
  }

  func testFloorAndRoundAreOnTheGridAndFloorIsNeverAboveTheTick() {
    for grid in RollGrid.allCases {
      let step = grid.ticks()
      for tick in stride(from: 0, through: 5000, by: 7) {
        XCTAssertEqual(grid.floor(tick) % step, 0)
        XCTAssertEqual(grid.round(tick) % step, 0)
        XCTAssertLessThanOrEqual(grid.floor(tick), tick)
        XCTAssertGreaterThan(grid.floor(tick) + step, tick)
        XCTAssertLessThanOrEqual(abs(grid.round(tick) - tick), step / 2)
      }
    }
  }

  func testFloorAndRoundHonourTicksPerBeat() {
    XCTAssertEqual(RollGrid.eighth.floor(250, ticksPerBeat: 480), 240)
    XCTAssertEqual(RollGrid.eighth.round(120, ticksPerBeat: 480), 240)
  }

  // MARK: - Insert length

  func testInsertLengthIsOneGridStepWhenNothingWasUsed() {
    XCTAssertEqual(RollGrid.eighth.insertLength(lastLength: nil, minNoteTicks: 30), 480)
    XCTAssertEqual(RollGrid.quarter.insertLength(lastLength: nil, minNoteTicks: 30), 960)
    XCTAssertEqual(RollGrid.sixteenthTriplet.insertLength(lastLength: nil, minNoteTicks: 30), 160)
  }

  func testInsertLengthWithNoGridAndNoLastLengthIsTheMinimum() {
    XCTAssertEqual(RollGrid.off.insertLength(lastLength: nil, minNoteTicks: 30), 30)
    XCTAssertEqual(RollGrid.off.insertLength(lastLength: nil, minNoteTicks: 5), 10)
  }

  func testInsertLengthIsTheLastUsedLengthWhateverTheGrid() {
    XCTAssertEqual(RollGrid.eighth.insertLength(lastLength: 700, minNoteTicks: 30), 700)
    XCTAssertEqual(RollGrid.off.insertLength(lastLength: 1234, minNoteTicks: 30), 1234)
    XCTAssertEqual(RollGrid.quarter.insertLength(lastLength: 30, minNoteTicks: 30), 30)
  }

  func testInsertLengthIsNeverBelowTheMinimum() {
    XCTAssertEqual(RollGrid.eighth.insertLength(lastLength: 12, minNoteTicks: 30), 30)
    XCTAssertEqual(RollGrid.eighth.insertLength(lastLength: 0, minNoteTicks: 30), 30)
    XCTAssertEqual(RollGrid.thirtySecond.insertLength(lastLength: nil, minNoteTicks: 200), 200)
  }

  func testInsertLengthFollowsTicksPerBeat() {
    XCTAssertEqual(RollGrid.eighth.insertLength(lastLength: nil, minNoteTicks: 30, ticksPerBeat: 480), 240)
  }

  // MARK: - The minimum from the snapshot

  func testTheMinimumNoteLengthComesFromTheSnapshotsLimits() throws {
    let url = try SnapshotTests.fixturesDirectory().appendingPathComponent("snapshot-tracks.json")
    var snapshot = try JSONDecoder().decode(Snapshot.self, from: Data(contentsOf: url))
    XCTAssertEqual(snapshot.minNoteTicksOrDefault, 30)
    snapshot.trackLimits = TrackLimitsJSON(maxTracks: 4, maxNotesPerTrack: 1024, minNoteTicks: 60, maxPolyphony: 8,
                                           maxOnsetsPerBeat: 15)
    XCTAssertEqual(snapshot.minNoteTicksOrDefault, 60)
  }

  func testTheMinimumNoteLengthIs30WithoutLimits() throws {
    let url = try SnapshotTests.fixturesDirectory().appendingPathComponent("snapshot-legacy-phase1.json")
    let snapshot = try JSONDecoder().decode(Snapshot.self, from: Data(contentsOf: url))
    XCTAssertNil(snapshot.trackLimits)
    XCTAssertEqual(snapshot.minNoteTicksOrDefault, 30)
    XCTAssertEqual(RollGrid.defaultMinNoteTicks, 30)
  }
}
