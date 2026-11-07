import CoreGraphics

/// What a pressable view (a palette cell) does when the window's button changes: the decisions of
/// `PressAction`, kept here so they can be tested. A press is the view's own when the latest down lies in
/// its `PressRegion` (one owner per press, whatever the cached hover states say). Every change is
/// reconciled from the whole `MouseButton` value, old against new, because SwiftUI may deliver several
/// events in one update: a down and its up together still give `.press` then `.release`; a new press while
/// one is still counted as held (its up was missed, the state's keep-alive has long released it) first
/// releases that one; an earlier press's drag never touches a new press (`pressDragged` is per press). A
/// release is also owed when the press turns into a drag, when the button is up, and on disappearance.
/// `release` never comes without a `press` before it, and never twice.
public struct PressLogic: Equatable {
  public enum Event: Equatable {
    case press
    case release
  }

  public private(set) var pressed = false

  public init() {}

  /// The events, in order, for the change from `old` to `new`; `region` is where the view takes a press.
  public mutating func update(from old: MouseButton, to new: MouseButton, region: PressRegion) -> [Event] {
    var events: [Event] = []
    if new.presses != old.presses {
      if pressed {
        pressed = false
        events.append(.release)
      }
      if let point = new.pressLocation, region.contains(point) {
        pressed = true
        events.append(.press)
      }
    }
    if pressed && (new.pressDragged || !new.isDown) {
      pressed = false
      events.append(.release)
    }
    return events
  }

  /// The view goes away (the grid is rebuilt, the window closes): a held press is released.
  public mutating func disappear() -> [Event] {
    guard pressed else { return [] }
    pressed = false
    return [.release]
  }
}
