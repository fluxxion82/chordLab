import XCTest
@testable import ChordLabUICore

/// The press decisions of a palette cell (UI/Core/PressLogic.swift).
final class PressLogicTests: XCTestCase {
  private let frame = CGRect(x: 100, y: 100, width: 40, height: 40)
  private var region: PressRegion { PressRegion(frame: frame, visible: frame, circular: false) }
  private let inside = CGPoint(x: 120, y: 120)
  private let outside = CGPoint(x: 10, y: 10)

  func testPressInsideThenReleaseOnUp() {
    var logic = PressLogic()
    var before = MouseButton()
    var after = before
    after.down(at: inside)
    XCTAssertEqual(logic.update(from: before, to: after, region: region), [.press])
    XCTAssertTrue(logic.pressed)
    before = after
    after.up()
    XCTAssertEqual(logic.update(from: before, to: after, region: region), [.release])
    XCTAssertFalse(logic.pressed)
    before = after
    XCTAssertEqual(logic.update(from: before, to: after, region: region), [], "nothing changed, nothing owed")
  }

  func testPressOutsideIsNotOurs() {
    var logic = PressLogic()
    let before = MouseButton()
    var after = before
    after.down(at: outside)
    XCTAssertEqual(logic.update(from: before, to: after, region: region), [])
    var up = after
    up.up()
    XCTAssertEqual(logic.update(from: after, to: up, region: region), [], "an up of a press that was never ours")
  }

  func testOnlyTheCellUnderTheDownPresses() {
    var left = PressLogic()
    var right = PressLogic()
    let before = MouseButton()
    var after = before
    after.down(at: inside)
    XCTAssertEqual(left.update(from: before, to: after, region: region), [.press])
    XCTAssertEqual(right.update(from: before, to: after, region: PressRegion(frame: frame.offsetBy(dx: 40, dy: 0), visible: frame.offsetBy(dx: 40, dy: 0), circular: false)), [])
  }

  func testDownAndUpInOneUpdateStillPressAndRelease() {
    var logic = PressLogic()
    let before = MouseButton()
    var after = before
    after.down(at: inside)
    after.up()                                                      // coalesced: the button is already back up
    XCTAssertEqual(logic.update(from: before, to: after, region: region), [.press, .release])
    XCTAssertFalse(logic.pressed)
  }

  func testDragReleasesWhileTheButtonIsStillDown() {
    var logic = PressLogic()
    var before = MouseButton()
    var after = before
    after.down(at: inside)
    XCTAssertEqual(logic.update(from: before, to: after, region: region), [.press])
    before = after
    after.dragged(to: CGPoint(x: inside.x + 1, y: inside.y))
    XCTAssertEqual(logic.update(from: before, to: after, region: region), [], "a wobble is still a hold")
    before = after
    after.dragged(to: CGPoint(x: inside.x + 10, y: inside.y))
    XCTAssertEqual(logic.update(from: before, to: after, region: region), [.release])
    XCTAssertTrue(after.isDown)
    before = after
    after.up()
    XCTAssertEqual(logic.update(from: before, to: after, region: region), [], "released once, at the drag")
  }

  func testAnEarlierPressesDragNeverReleasesANewPress() {
    var logic = PressLogic()
    var before = MouseButton()
    var after = before
    after.down(at: inside)
    XCTAssertEqual(logic.update(from: before, to: after, region: region), [.press])
    before = after
    after.dragged(to: CGPoint(x: inside.x + 20, y: inside.y))      // one update holds: the drag, its up, and
    after.up()                                                      // a new down in this cell that has not moved
    after.down(at: inside)
    XCTAssertEqual(logic.update(from: before, to: after, region: region), [.release, .press])
    XCTAssertTrue(logic.pressed)
    XCTAssertTrue(after.isDown)
    before = after
    XCTAssertEqual(logic.update(from: before, to: after, region: region), [])
  }

  func testAHiddenCellTakesNoPress() {
    var logic = PressLogic()
    let before = MouseButton()
    var after = before
    after.down(at: inside)
    let hidden = PressRegion(frame: frame, visible: .zero, circular: false)       // scrolled out of the viewport
    XCTAssertEqual(logic.update(from: before, to: after, region: hidden), [])
    let half = PressRegion(frame: frame, visible: CGRect(x: 100, y: 100, width: 40, height: 10), circular: false)
    XCTAssertEqual(logic.update(from: before, to: after, region: half), [], "the down is in the clipped part")
    let shown = PressRegion(frame: frame, visible: CGRect(x: 100, y: 100, width: 40, height: 25), circular: false)
    XCTAssertEqual(logic.update(from: before, to: after, region: shown), [.press])
  }

  func testAMissedUpDoesNotBlockTheNextPress() {
    var logic = PressLogic()
    var before = MouseButton()
    var after = before
    after.down(at: inside)
    XCTAssertEqual(logic.update(from: before, to: after, region: region), [.press])
    before = after
    after.down(at: inside)                                          // the up went to a drag session; a new press
    XCTAssertEqual(logic.update(from: before, to: after, region: region), [.release, .press])
    XCTAssertTrue(logic.pressed)
  }

  func testAMissedUpThenAPressElsewhereReleases() {
    var logic = PressLogic()
    var before = MouseButton()
    var after = before
    after.down(at: inside)
    _ = logic.update(from: before, to: after, region: region)
    before = after
    after.down(at: outside)
    XCTAssertEqual(logic.update(from: before, to: after, region: region), [.release])
    XCTAssertFalse(logic.pressed)
  }

  func testDisappearReleasesAHeldPressOnly() {
    var logic = PressLogic()
    XCTAssertEqual(logic.disappear(), [])
    let before = MouseButton()
    var after = before
    after.down(at: inside)
    _ = logic.update(from: before, to: after, region: region)
    XCTAssertEqual(logic.disappear(), [.release])
    XCTAssertEqual(logic.disappear(), [], "never twice")
  }
}
