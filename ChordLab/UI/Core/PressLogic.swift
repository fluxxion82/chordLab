import CoreGraphics
import Foundation

/// Why a press ended and, when it ended with its own button-up, after how long (strip editing design D9: Record
/// inserts a chord only for a press released by its button, for as long as it was held).
public struct PressRelease: Equatable {
  public enum Reason: Equatable {
    /// The button came up on this press: the click or hold Record records.
    case buttonUp
    /// The press travelled past the drag threshold: a native drag session took over (a drop on the strip
    /// appends by itself).
    case dragged
    /// A newer press began before this one was released (its up was missed, or the events coalesced).
    case superseded
    /// The view went away mid-press.
    case disappeared
  }

  public let reason: Reason
  /// For `.buttonUp`, the press's down-to-up time from the window's event timestamps (`MouseButton.heldSeconds`),
  /// the same whether the down and the up arrived in one update or two; 0 for the other reasons.
  public let heldSeconds: TimeInterval

  public init(reason: Reason, heldSeconds: TimeInterval = 0) {
    self.reason = reason
    self.heldSeconds = heldSeconds
  }
}

/// What a pressable view (a palette cell) does when the window's button changes: the decisions of
/// `PressAction`, kept here so they can be tested. A press is the view's own when the latest down lies in
/// its `PressRegion` (one owner per press, whatever the cached hover states say). Every change is
/// reconciled from the whole `MouseButton` value, old against new, because SwiftUI may deliver several
/// events in one update: a down and its up together still give `.press` then a button-up `.release`, with the
/// held time from the events' timestamps; a new press while one is still counted as held (its up was missed,
/// the state's keep-alive has long released it) first releases that one as superseded; an earlier press's drag
/// never touches a new press (`pressDragged` is per press). A release is also owed when the press turns into a
/// drag, when the button is up, and on disappearance; when several apply in one update, a drag, a newer press
/// or a disappearance wins over the button-up, so a press that became a drag never reads as a click.
/// `release` never comes without a `press` before it, and never twice.
public struct PressLogic: Equatable {
  public enum Event: Equatable {
    case press
    case release(PressRelease)
  }

  public private(set) var pressed = false

  public init() {}

  /// The events, in order, for the change from `old` to `new`; `region` is where the view takes a press.
  public mutating func update(from old: MouseButton, to new: MouseButton, region: PressRegion) -> [Event] {
    var events: [Event] = []
    if new.presses != old.presses {
      if pressed {
        pressed = false
        events.append(.release(PressRelease(reason: .superseded)))
      }
      if let point = new.pressLocation, region.contains(point) {
        pressed = true
        events.append(.press)
      }
    }
    // A press still held here is `new`'s latest press, so its timestamps are the ones to read.
    if pressed && new.pressDragged {
      pressed = false
      events.append(.release(PressRelease(reason: .dragged)))
    } else if pressed && !new.isDown {
      pressed = false
      events.append(.release(PressRelease(reason: .buttonUp, heldSeconds: new.heldSeconds ?? 0)))
    }
    return events
  }

  /// The view goes away (the grid is rebuilt, the window closes): a held press is released.
  public mutating func disappear() -> [Event] {
    guard pressed else { return [] }
    pressed = false
    return [.release(PressRelease(reason: .disappeared))]
  }
}
