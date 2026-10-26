import Foundation

/// What one keep-alive tick does for a held preview (`ChordLabState` sends an `extend` once a second
/// while a palette cell is held, so the scheduler's four-second watchdog does not end it).
///
/// The tick also stands in for a release the view never reported. A hold that began with a mouse button
/// down follows the button: no button down now means the release was lost, and the hold is released. Touch
/// input (Sidecar, for instance) is outside `NSEvent.pressedMouseButtons` (Apple TN3212), so a hold that
/// began with no button reported cannot be checked that way; it is renewed for `maxTicksWithoutButton`
/// ticks and then released, so a lost release on such a hold still ends within about a minute.
enum PreviewKeepAlive {
  enum Action: Equatable { case extend, release }

  /// Ticks a hold that began without a reported mouse button is renewed before it is released.
  static let maxTicksWithoutButton = 60

  /// `holdHasButton`: a button was down when the hold began. `buttonDown`: the mask now. `tick`: 1-based.
  static func action(tick: Int, holdHasButton: Bool, buttonDown: Bool) -> Action {
    if holdHasButton { return buttonDown ? .extend : .release }
    return tick <= maxTicksWithoutButton ? .extend : .release
  }
}
