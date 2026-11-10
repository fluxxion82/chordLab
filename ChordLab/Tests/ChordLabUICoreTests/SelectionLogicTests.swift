import XCTest
@testable import ChordLabUICore

/// The strip's selection rules and positions (UI/Core/SelectionLogic.swift, strip editing design D5, D7).
final class SelectionLogicTests: XCTestCase {
  private let strip = [10, 11, 12, 13, 14]
  private let plain = ClickModifiers()
  private let shift = ClickModifiers(shift: true)
  private let command = ClickModifiers(command: true)

  private func click(_ id: Int?, _ modifiers: ClickModifiers, current: [Int], anchor: Int?) -> StripSelection {
    SelectionLogic.resolve(click: id, modifiers: modifiers, ids: strip, current: current, anchor: anchor)
  }

  func testAPlainClickSelectsThatBlockAlone() {
    XCTAssertEqual(click(12, plain, current: [10, 11], anchor: 11), StripSelection(ids: [12], anchor: 12))
    XCTAssertEqual(click(12, plain, current: [], anchor: nil), StripSelection(ids: [12], anchor: 12))
  }

  func testAShiftClickSelectsTheRangeFromTheAnchorEitherWayAndKeepsTheAnchor() {
    XCTAssertEqual(click(14, shift, current: [11], anchor: 11), StripSelection(ids: [11, 12, 13, 14], anchor: 11))
    XCTAssertEqual(click(10, shift, current: [12, 14], anchor: 12), StripSelection(ids: [10, 11, 12], anchor: 12))
    XCTAssertEqual(click(12, shift, current: [12], anchor: 12), StripSelection(ids: [12], anchor: 12))
  }

  func testAShiftClickWithoutAnAnchorIsAPlainClick() {
    XCTAssertEqual(click(13, shift, current: [], anchor: nil), StripSelection(ids: [13], anchor: 13))
  }

  func testACommandClickAddsABlockAsTheAnchor() {
    XCTAssertEqual(click(13, command, current: [10, 11], anchor: 11), StripSelection(ids: [10, 11, 13], anchor: 13))
    XCTAssertEqual(click(12, command, current: [], anchor: nil), StripSelection(ids: [12], anchor: 12))
  }

  func testACommandClickRemovesABlockAndARemovedAnchorMovesAfterElseBefore() {
    XCTAssertEqual(click(11, command, current: [10, 11, 13], anchor: 11), StripSelection(ids: [10, 13], anchor: 13))
    XCTAssertEqual(click(13, command, current: [10, 11, 13], anchor: 13), StripSelection(ids: [10, 11], anchor: 11))
    XCTAssertEqual(click(10, command, current: [10, 11, 13], anchor: 13), StripSelection(ids: [11, 13], anchor: 13))
    XCTAssertEqual(click(12, command, current: [12], anchor: 12), StripSelection.none)
  }

  func testShiftWinsOverCommand() {
    XCTAssertEqual(click(13, ClickModifiers(shift: true, command: true), current: [11], anchor: 11),
                   StripSelection(ids: [11, 12, 13], anchor: 11))
  }

  func testTheEmptyStripDeselectsAndAStaleClickChangesNothing() {
    XCTAssertEqual(click(nil, plain, current: [10, 11], anchor: 11), StripSelection.none)
    XCTAssertEqual(click(nil, command, current: [10, 11], anchor: 11), StripSelection.none)
    XCTAssertEqual(click(99, plain, current: [10, 11], anchor: 11), StripSelection(ids: [10, 11], anchor: 11))
  }

  func testSelectAllTakesEveryBlockWithTheLastAsAnchor() {
    XCTAssertEqual(SelectionLogic.all(strip), StripSelection(ids: [10, 11, 12, 13, 14], anchor: 14))
    XCTAssertEqual(SelectionLogic.all([]), StripSelection.none)
  }

  func testTheCommandForASelection() throws {
    let none = SelectionLogic.command(.none)
    XCTAssertEqual(none["cmd"] as? String, "selectEvent")
    XCTAssertTrue(none["id"] is NSNull)
    let one = SelectionLogic.command(StripSelection(ids: [12], anchor: 12))
    XCTAssertEqual(one["cmd"] as? String, "selectEvent")
    XCTAssertEqual(one["id"] as? Int, 12)
    let many = SelectionLogic.command(StripSelection(ids: [10, 12], anchor: 10))
    let data = try JSONSerialization.data(withJSONObject: many, options: [.sortedKeys])
    XCTAssertEqual(String(data: data, encoding: .utf8), #"{"anchor":10,"cmd":"selectEvents","ids":[10,12]}"#)
  }

  func testContextMenuTargetsAreTheSelectionWhenTheBlockIsInIt() {
    XCTAssertEqual(SelectionLogic.targets(block: 11, selection: [10, 11, 13]), [10, 11, 13])
    XCTAssertEqual(SelectionLogic.targets(block: 12, selection: [10, 11, 13]), [12])
    XCTAssertEqual(SelectionLogic.targets(block: 12, selection: []), [12])
  }

  func testInsertionIsAfterTheAnchorElseAtTheEnd() {
    XCTAssertEqual(SelectionLogic.insertionIndex(ids: strip, anchor: 11), 2)
    XCTAssertEqual(SelectionLogic.insertionIndex(ids: strip, anchor: 14), 5)
    XCTAssertEqual(SelectionLogic.insertionIndex(ids: strip, anchor: nil), 5)
    XCTAssertEqual(SelectionLogic.insertionIndex(ids: strip, anchor: 99), 5)
    XCTAssertEqual(SelectionLogic.insertionIndex(ids: [], anchor: nil), 0)
  }

  func testADuplicateGoesAfterTheLastTarget() {
    XCTAssertEqual(SelectionLogic.index(after: [13, 10], ids: strip), 4)
    XCTAssertEqual(SelectionLogic.index(after: [14], ids: strip), 5)
    XCTAssertEqual(SelectionLogic.index(after: [99], ids: strip), 5)
  }
}
