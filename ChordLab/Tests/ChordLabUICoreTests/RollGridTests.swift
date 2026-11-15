import XCTest
@testable import ChordLabUICore

/// The roll's grid options, bar-relative snapping and the insert length rule (UI/Core/RollGrid.swift, tracks design D14, D15).
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

  func testEveryStepDividesABeat() {
    for grid in RollGrid.allCases {
      XCTAssertEqual(960 % grid.ticks(), 0, "\(grid)")
    }
  }

  // MARK: - Bar length

  func testBarTicksIsTheEnginesFormula() {
    XCTAssertEqual(RollGrid.barTicks(num: 4, den: 4, ticksPerBeat: 960), 3840)
    XCTAssertEqual(RollGrid.barTicks(num: 3, den: 4, ticksPerBeat: 960), 2880)
    XCTAssertEqual(RollGrid.barTicks(num: 6, den: 8, ticksPerBeat: 960), 2880)
    XCTAssertEqual(RollGrid.barTicks(num: 7, den: 8, ticksPerBeat: 960), 3360)
    XCTAssertEqual(RollGrid.barTicks(num: 5, den: 8, ticksPerBeat: 960), 2400)
    XCTAssertEqual(RollGrid.barTicks(num: 3, den: 8, ticksPerBeat: 960), 1440)
    XCTAssertEqual(RollGrid.barTicks(num: 12, den: 8, ticksPerBeat: 960), 5760)
    XCTAssertEqual(RollGrid.barTicks(num: 3, den: 2, ticksPerBeat: 960), 5760)
    XCTAssertEqual(RollGrid.barTicks(num: 4, den: 4, ticksPerBeat: 480), 1920)
  }

  func testBarTicksOfAnUnusableMeterIs44() {
    XCTAssertEqual(RollGrid.barTicks(num: 0, den: 4, ticksPerBeat: 960), 3840)
    XCTAssertEqual(RollGrid.barTicks(num: 4, den: 0, ticksPerBeat: 960), 3840)
    XCTAssertEqual(RollGrid.barTicks(num: -3, den: 4, ticksPerBeat: 960), 3840)
    XCTAssertEqual(RollGrid.barTicks(num: 4, den: 4, ticksPerBeat: 0), 4)
  }

  func testEveryValidMeterHasABarThatIsAMultipleOfTenTicksAt960() {
    for den in [2, 4, 8] {
      for num in 1...16 {
        let bar = RollGrid.barTicks(num: num, den: den, ticksPerBeat: 960)
        XCTAssertEqual(bar % 10, 0, "\(num)/\(den)")
        XCTAssertEqual(bar, num * 3840 / den, "\(num)/\(den)")
      }
    }
  }

  func testTitlesAndTheDefault() {
    XCTAssertEqual(RollGrid.allCases.map(\.title), ["1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T", "Off"])
    XCTAssertEqual(RollGrid.default, .eighth)
    XCTAssertEqual(RollGrid.allCases.count, 7)
  }

  private let bar44 = 3840
  private let bar34 = 2880
  private let bar78 = 3360
  private let bar58 = 2400
  private let bar68 = 2880

  func testSnapDownOnStraightGridsIn44() {
    func down(_ grid: RollGrid, _ tick: Int) -> Int { grid.snapDown(tick, barTicks: bar44) }
    XCTAssertEqual(down(.eighth, 0), 0)
    XCTAssertEqual(down(.eighth, 479), 0)
    XCTAssertEqual(down(.eighth, 480), 480)
    XCTAssertEqual(down(.eighth, 1439), 960)
    XCTAssertEqual(down(.quarter, 1919), 960)
    XCTAssertEqual(down(.sixteenth, 250), 240)
    XCTAssertEqual(down(.thirtySecond, 239), 120)
    XCTAssertEqual(down(.off, 19), 10)
    XCTAssertEqual(down(.off, 9), 0)
  }

  func testSnapDownOnTripletGridsIn44() {
    func down(_ grid: RollGrid, _ tick: Int) -> Int { grid.snapDown(tick, barTicks: bar44) }
    XCTAssertEqual(down(.eighthTriplet, 319), 0)
    XCTAssertEqual(down(.eighthTriplet, 320), 320)
    XCTAssertEqual(down(.eighthTriplet, 959), 640)
    XCTAssertEqual(down(.eighthTriplet, 960), 960)
    XCTAssertEqual(down(.sixteenthTriplet, 479), 320)
    XCTAssertEqual(down(.sixteenthTriplet, 480), 480)
  }

  func testSnapNearestGoesToTheNearestPointWithTiesLaterIn44() {
    func near(_ grid: RollGrid, _ tick: Int) -> Int { grid.snapNearest(tick, barTicks: bar44) }
    XCTAssertEqual(near(.eighth, 0), 0)
    XCTAssertEqual(near(.eighth, 239), 0)
    XCTAssertEqual(near(.eighth, 240), 480, "a tie goes to the later point")
    XCTAssertEqual(near(.eighth, 241), 480)
    XCTAssertEqual(near(.eighth, 480), 480)
    XCTAssertEqual(near(.eighthTriplet, 159), 0)
    XCTAssertEqual(near(.eighthTriplet, 160), 320)
    XCTAssertEqual(near(.eighthTriplet, 479), 320)
    XCTAssertEqual(near(.eighthTriplet, 480), 640)
    XCTAssertEqual(near(.sixteenthTriplet, 80), 160)
    XCTAssertEqual(near(.off, 4), 0)
    XCTAssertEqual(near(.off, 5), 10)
  }

  func test34IsThe44GridWithAShorterBar() {
    XCTAssertEqual(RollGrid.quarter.snapDown(2879, barTicks: bar34), 1920)
    XCTAssertEqual(RollGrid.quarter.snapDown(2880, barTicks: bar34), 2880)
    XCTAssertEqual(RollGrid.quarter.snapDown(3839, barTicks: bar34), 2880)
    XCTAssertEqual(RollGrid.quarter.snapDown(3840, barTicks: bar34), 3840)
    XCTAssertEqual(RollGrid.eighth.snapNearest(2700, barTicks: bar34), 2880)
    XCTAssertEqual(RollGrid.eighthTriplet.snapDown(5000, barTicks: bar34), 4800, "bar 2 starts at 2880; 1920 in, on 320")
  }

  func test78BarsStartOffTheQuarterGrid() {
    // Bar 2 starts at 3360, which is not a multiple of 960: the old tick-0 grid floored 3400 to 2880.
    XCTAssertEqual(RollGrid.quarter.snapDown(3400, barTicks: bar78), 3360)
    XCTAssertEqual(RollGrid.quarter.snapDown(3360, barTicks: bar78), 3360)
    XCTAssertEqual(RollGrid.quarter.snapDown(3359, barTicks: bar78), 2880)
    XCTAssertEqual(RollGrid.quarter.snapDown(3360 + 959, barTicks: bar78), 3360)
    XCTAssertEqual(RollGrid.quarter.snapDown(3360 + 960, barTicks: bar78), 3360 + 960)
    XCTAssertEqual(RollGrid.quarter.snapDown(6720 + 10, barTicks: bar78), 6720, "bar 3 starts at 6720")
    XCTAssertEqual(RollGrid.eighthTriplet.snapDown(3400, barTicks: bar78), 3360)
    XCTAssertEqual(RollGrid.sixteenth.snapDown(3601, barTicks: bar78), 3600)
  }

  func test78NearestNeverPassesTheNextBarStart() {
    XCTAssertEqual(RollGrid.quarter.snapNearest(3300, barTicks: bar78), 3360, "not 3840")
    XCTAssertEqual(RollGrid.quarter.snapNearest(2900, barTicks: bar78), 2880)
    XCTAssertEqual(RollGrid.quarter.snapNearest(3120, barTicks: bar78), 3360, "a tie goes to the later point, here the bar line")
    XCTAssertEqual(RollGrid.quarter.snapNearest(3119, barTicks: bar78), 2880)
    XCTAssertEqual(RollGrid.quarter.snapNearest(3360, barTicks: bar78), 3360)
    XCTAssertEqual(RollGrid.quarter.snapNearest(3400, barTicks: bar78), 3360)
    XCTAssertEqual(RollGrid.quarter.snapNearest(3360 + 480, barTicks: bar78), 3360 + 960, "a tie inside bar 2")
    XCTAssertEqual(RollGrid.eighth.snapNearest(3359, barTicks: bar78), 3360)
    XCTAssertEqual(RollGrid.eighth.snapNearest(3130, barTicks: bar78), 3360, "230 from the bar line, 250 from 2880")
  }

  func test58WithEighthTripletsCountsFromEachBarStart() {
    let points = [0, 320, 640, 960, 1280, 1600, 1920, 2240]
    for point in points {
      XCTAssertEqual(RollGrid.eighthTriplet.snapDown(point, barTicks: bar58), point)
      XCTAssertEqual(RollGrid.eighthTriplet.snapDown(point + 319 > 2399 ? 2399 : point + 319, barTicks: bar58), point)
      XCTAssertEqual(RollGrid.eighthTriplet.snapNearest(point, barTicks: bar58), point)
      XCTAssertEqual(RollGrid.eighthTriplet.snapDown(2400 + point, barTicks: bar58), 2400 + point, "bar 2")
    }
    XCTAssertEqual(RollGrid.eighthTriplet.snapDown(2399, barTicks: bar58), 2240)
    XCTAssertEqual(RollGrid.eighthTriplet.snapDown(2400, barTicks: bar58), 2400)
    XCTAssertEqual(RollGrid.eighthTriplet.snapNearest(2399, barTicks: bar58), 2400)
    XCTAssertEqual(RollGrid.eighthTriplet.snapNearest(2320, barTicks: bar58), 2400, "80 from the bar line, 80 from 2240: tie later")
    XCTAssertEqual(RollGrid.eighthTriplet.snapNearest(2319, barTicks: bar58), 2240)
    XCTAssertEqual(RollGrid.eighthTriplet.snapDown(2400 + 330, barTicks: bar58), 2720)
  }

  func test68IsTwoDottedQuartersPerBar() {
    XCTAssertEqual(RollGrid.eighth.snapDown(2879, barTicks: bar68), 2400)
    XCTAssertEqual(RollGrid.eighth.snapDown(2880 + 479, barTicks: bar68), 2880)
    XCTAssertEqual(RollGrid.quarter.snapNearest(2500, barTicks: bar68), 2880)
    XCTAssertEqual(RollGrid.quarter.snapNearest(1900, barTicks: bar68), 1920)
    XCTAssertEqual(RollGrid.sixteenthTriplet.snapDown(2879, barTicks: bar68), 2720)
  }

  func testOffIsTenTicksInEveryValidMeterBecauseBarsAreMultiplesOfTen() {
    for den in [2, 4, 8] {
      for num in 1...16 {
        let bar = RollGrid.barTicks(num: num, den: den, ticksPerBeat: 960)
        XCTAssertEqual(RollGrid.off.snapDown(bar + 17, barTicks: bar), bar + 10, "\(num)/\(den)")
        XCTAssertEqual(RollGrid.off.snapDown(bar - 1, barTicks: bar), bar - 10, "\(num)/\(den)")
        XCTAssertEqual(RollGrid.off.snapNearest(bar - 4, barTicks: bar), bar, "\(num)/\(den)")
      }
    }
  }

  func testBarStartsAreFixedPointsAndNothingPassesTheNextBar() {
    for den in [2, 4, 8] {
      for num in 1...16 {
        let bar = RollGrid.barTicks(num: num, den: den, ticksPerBeat: 960)
        for grid in RollGrid.allCases {
          let step = grid.ticks()
          for index in 0...3 {
            let start = index * bar
            XCTAssertEqual(grid.snapDown(start, barTicks: bar), start, "\(grid) \(num)/\(den) bar \(index)")
            XCTAssertEqual(grid.snapNearest(start, barTicks: bar), start, "\(grid) \(num)/\(den) bar \(index)")
            for offset in stride(from: 0, to: bar, by: 97) {
              let tick = start + offset
              let down = grid.snapDown(tick, barTicks: bar)
              let near = grid.snapNearest(tick, barTicks: bar)
              XCTAssertLessThanOrEqual(down, tick)
              XCTAssertGreaterThanOrEqual(down, start, "never before the tick's own bar start")
              XCTAssertEqual((down - start) % step, 0)
              XCTAssertLessThanOrEqual(near, start + bar, "\(grid) \(num)/\(den) tick \(tick)")
              XCTAssertGreaterThanOrEqual(near, down)
              XCTAssertTrue((near - start) % step == 0 || near == start + bar)
              XCTAssertLessThanOrEqual(abs(near - tick), step / 2 + 0, "\(grid) \(num)/\(den) tick \(tick)")
            }
          }
        }
      }
    }
  }

  func testSnapNeverGoesBelowTickZeroAndAnOddBarLengthIsHandled() {
    for grid in RollGrid.allCases {
      XCTAssertEqual(grid.snapDown(-1, barTicks: 3840), 0)
      XCTAssertEqual(grid.snapDown(-5000, barTicks: 3840), 0)
      XCTAssertEqual(grid.snapNearest(-1, barTicks: 3840), 0)
      XCTAssertEqual(grid.snapNearest(-5000, barTicks: 3840), 0)
    }
    // A bar that is not a multiple of the step (never from the engine at 960): the rule still holds.
    XCTAssertEqual(RollGrid.quarter.snapDown(1100, barTicks: 1000), 1000)
    XCTAssertEqual(RollGrid.quarter.snapNearest(990, barTicks: 1000), 1000)
    // An unusable bar length counts as 1.
    XCTAssertEqual(RollGrid.eighth.snapDown(500, barTicks: 0), 500)
  }

  func testSnapHonoursTicksPerBeat() {
    let bar = RollGrid.barTicks(num: 7, den: 8, ticksPerBeat: 480)   // 1680
    XCTAssertEqual(bar, 1680)
    XCTAssertEqual(RollGrid.eighth.snapDown(250, barTicks: 1920, ticksPerBeat: 480), 240)
    XCTAssertEqual(RollGrid.eighth.snapNearest(120, barTicks: 1920, ticksPerBeat: 480), 240)
    XCTAssertEqual(RollGrid.quarter.snapDown(1700, barTicks: bar, ticksPerBeat: 480), 1680)
    XCTAssertEqual(RollGrid.quarter.snapNearest(1600, barTicks: bar, ticksPerBeat: 480), 1680)
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
}
