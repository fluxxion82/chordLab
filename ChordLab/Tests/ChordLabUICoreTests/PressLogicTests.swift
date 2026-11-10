import XCTest
@testable import ChordLabUICore

/// The press decisions of a palette cell (UI/Core/PressLogic.swift): when a press starts and ends, why it ended,
/// and how long a press released by its own button was held (strip editing design D9).
final class PressLogicTests: XCTestCase {
  private let frame = CGRect(x: 100, y: 100, width: 40, height: 40)
  private var region: PressRegion { PressRegion(frame: frame, visible: frame, circular: false) }
  private let inside = CGPoint(x: 120, y: 120)
  private let outside = CGPoint(x: 10, y: 10)

  private func released(_ reason: PressRelease.Reason, after seconds: TimeInterval = 0) -> PressLogic.Event {
    .release(PressRelease(reason: reason, heldSeconds: seconds))
  }

  func testPressInsideThenReleaseOnUpWithTheHeldTime() {
    var logic = PressLogic()
    var before = MouseButton()
    var after = before
    after.down(at: inside, time: 1.0)
    XCTAssertEqual(logic.update(from: before, to: after, region: region), [.press])
    XCTAssertTrue(logic.pressed)
    before = after
    after.up(time: 1.8)
    XCTAssertEqual(logic.update(from: before, to: after, region: region), [released(.buttonUp, after: 0.8)])
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

  func testDownAndUpInOneUpdateStillPressAndReleaseWithTheRealHeldTime() {
    var logic = PressLogic()
    let before = MouseButton()
    var after = before
    after.down(at: inside, time: 1.0)
    after.up(time: 1.8)                                             // coalesced: the button is already back up
    XCTAssertEqual(logic.update(from: before, to: after, region: region), [.press, released(.buttonUp, after: 0.8)])
    XCTAssertFalse(logic.pressed)
  }

  func testSeparateAndCoalescedDeliveryGiveTheSameRelease() {
    let start = MouseButton()
    var down = start
    down.down(at: inside, time: 2.0)
    var up = down
    up.up(time: 2.5)
    var separate = PressLogic()
    let first = separate.update(from: start, to: down, region: region) + separate.update(from: down, to: up, region: region)
    var coalesced = PressLogic()
    XCTAssertEqual(first, [.press, released(.buttonUp, after: 0.5)])
    XCTAssertEqual(coalesced.update(from: start, to: up, region: region), first)
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
    XCTAssertEqual(logic.update(from: before, to: after, region: region), [released(.dragged)])
    XCTAssertTrue(after.isDown)
    before = after
    after.up()
    XCTAssertEqual(logic.update(from: before, to: after, region: region), [], "released once, at the drag")
  }

  func testADragAndItsUpInOneUpdateReleaseOnceAsADrag() {
    var logic = PressLogic()
    var before = MouseButton()
    var after = before
    after.down(at: inside, time: 1.0)
    XCTAssertEqual(logic.update(from: before, to: after, region: region), [.press])
    before = after
    after.dragged(to: CGPoint(x: inside.x + 20, y: inside.y))
    after.up(time: 1.8)                                             // coalesced: the drop's up came with the drag
    XCTAssertEqual(logic.update(from: before, to: after, region: region), [released(.dragged)], "a drag, never a click")
    before = after
    XCTAssertEqual(logic.update(from: before, to: after, region: region), [], "released once")
  }

  func testADownItsDragAndItsUpInOneUpdateAreAPressAndADrag() {
    var logic = PressLogic()
    let before = MouseButton()
    var after = before
    after.down(at: inside, time: 1.0)
    after.dragged(to: CGPoint(x: inside.x + 20, y: inside.y))
    after.up(time: 1.8)
    XCTAssertEqual(logic.update(from: before, to: after, region: region), [.press, released(.dragged)])
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
    XCTAssertEqual(logic.update(from: before, to: after, region: region), [released(.superseded), .press])
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
    XCTAssertEqual(logic.update(from: before, to: after, region: region), [released(.superseded), .press])
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
    XCTAssertEqual(logic.update(from: before, to: after, region: region), [released(.superseded)])
    XCTAssertFalse(logic.pressed)
  }

  func testDisappearReleasesAHeldPressOnly() {
    var logic = PressLogic()
    XCTAssertEqual(logic.disappear(), [])
    let before = MouseButton()
    var after = before
    after.down(at: inside)
    _ = logic.update(from: before, to: after, region: region)
    XCTAssertEqual(logic.disappear(), [released(.disappeared)])
    XCTAssertEqual(logic.disappear(), [], "never twice")
  }
}
