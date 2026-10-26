import XCTest
@testable import ChordLabUICore

/// The keep-alive decision for a held palette cell (UI/Core/PreviewKeepAlive.swift): a hold that began
/// with a mouse button down follows the button; one that began without (touch input) is renewed for
/// `maxTicksWithoutButton` ticks and then released.
final class PreviewKeepAliveTests: XCTestCase {
  func testButtonHoldWithTheButtonDownIsExtended() {
    XCTAssertEqual(PreviewKeepAlive.action(tick: 1, holdHasButton: true, buttonDown: true), .extend)
    XCTAssertEqual(PreviewKeepAlive.action(tick: 500, holdHasButton: true, buttonDown: true), .extend)
  }

  func testButtonHoldWithNoButtonDownIsReleasedAtOnce() {
    XCTAssertEqual(PreviewKeepAlive.action(tick: 1, holdHasButton: true, buttonDown: false), .release)
  }

  func testHoldWithoutAButtonIsRenewedForSixtyTicksWhateverTheMask() {
    XCTAssertEqual(PreviewKeepAlive.maxTicksWithoutButton, 60)
    for buttonDown in [false, true] {
      XCTAssertEqual(PreviewKeepAlive.action(tick: 1, holdHasButton: false, buttonDown: buttonDown), .extend)
      XCTAssertEqual(PreviewKeepAlive.action(tick: 60, holdHasButton: false, buttonDown: buttonDown), .extend)
      XCTAssertEqual(PreviewKeepAlive.action(tick: 61, holdHasButton: false, buttonDown: buttonDown), .release)
    }
  }
}
