import XCTest
@testable import ChordLabUICore

final class RulerMarksTests: XCTestCase {
  private func summary(_ marks: [RulerMark]) -> [String] {
    marks.map { "\($0.beat)" + ($0.bar.map { "#\($0)" } ?? "") }
  }

  func testFourFour() {
    let m = RulerMarks.marks(num: 4, den: 4, totalBeats: 8)
    XCTAssertEqual(m.map(\.beat), [0, 1, 2, 3, 4, 5, 6, 7])
    XCTAssertEqual(m.filter(\.isBarStart).map(\.beat), [0, 4])
    XCTAssertEqual(m.filter(\.isBarStart).map(\.bar), [1, 2])
    XCTAssertEqual(m.filter { !$0.isBarStart }.map(\.bar), [Int?](repeating: nil, count: 6))
  }

  func testThreeFour() {
    let m = RulerMarks.marks(num: 3, den: 4, totalBeats: 7)
    XCTAssertEqual(m.map(\.beat), [0, 1, 2, 3, 4, 5, 6])
    XCTAssertEqual(summary(m.filter(\.isBarStart)), ["0.0#1", "3.0#2", "6.0#3"])
  }

  func testSixEightIsCompoundWithPulsesOneAndAHalfApart() {
    let m = RulerMarks.marks(num: 6, den: 8, totalBeats: 6)
    XCTAssertEqual(m.map(\.beat), [0, 1.5, 3, 4.5])
    XCTAssertEqual(summary(m.filter(\.isBarStart)), ["0.0#1", "3.0#2"])
  }

  func testTwelveEightHasFourPulsesPerBar() {
    let m = RulerMarks.marks(num: 12, den: 8, totalBeats: 6)
    XCTAssertEqual(m.map(\.beat), [0, 1.5, 3, 4.5])
    XCTAssertEqual(m.filter(\.isBarStart).map(\.beat), [0], "a bar is 6 beats")
  }

  func testSevenEightPulsesEveryHalfBeat() {
    let m = RulerMarks.marks(num: 7, den: 8, totalBeats: 7)
    XCTAssertEqual(m.count, 14)
    XCTAssertEqual(m.prefix(4).map(\.beat), [0, 0.5, 1, 1.5])
    XCTAssertEqual(summary(m.filter(\.isBarStart)), ["0.0#1", "3.5#2"])
  }

  func testFiveFourAndTwoFour() {
    XCTAssertEqual(summary(RulerMarks.marks(num: 5, den: 4, totalBeats: 10).filter(\.isBarStart)), ["0.0#1", "5.0#2"])
    XCTAssertEqual(summary(RulerMarks.marks(num: 2, den: 4, totalBeats: 5).filter(\.isBarStart)), ["0.0#1", "2.0#2", "4.0#3"])
  }

  func testHalfNoteDenominatorPulsesEveryTwoBeats() {
    let m = RulerMarks.marks(num: 3, den: 2, totalBeats: 12)
    XCTAssertEqual(m.map(\.beat), [0, 2, 4, 6, 8, 10])
    XCTAssertEqual(summary(m.filter(\.isBarStart)), ["0.0#1", "6.0#2"])
  }

  func testSixFourIsNotCompound() {
    XCTAssertEqual(RulerMarks.marks(num: 6, den: 4, totalBeats: 3).map(\.beat), [0, 1, 2])
  }

  func testMarksStopBeforeTotalBeats() {
    XCTAssertEqual(RulerMarks.marks(num: 4, den: 4, totalBeats: 4).map(\.beat), [0, 1, 2, 3])
    XCTAssertEqual(RulerMarks.marks(num: 4, den: 4, totalBeats: 4.5).map(\.beat), [0, 1, 2, 3, 4])
  }

  func testNothingForNoLengthOrAnInvalidMeter() {
    XCTAssertTrue(RulerMarks.marks(num: 4, den: 4, totalBeats: 0).isEmpty)
    XCTAssertTrue(RulerMarks.marks(num: 0, den: 4, totalBeats: 8).isEmpty)
    XCTAssertTrue(RulerMarks.marks(num: 4, den: 0, totalBeats: 8).isEmpty)
  }
}
