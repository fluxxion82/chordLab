import CoreGraphics
import Foundation

/// The modifier keys of a roll pointer event (tracks design D15). The gesture machine reads ⇧ (toggle, add to a
/// rubber band) and ⌥ (duplicate, read at the down only); ⌘ and ⌃ are carried for the input view and ignored by
/// `RollEditLogic` (whether a ⌃-click is a context-menu click is the input view's decision; it never sends one as
/// a down).
struct RollModifiers: Equatable {
  var shift = false
  var option = false
  var command = false
  var control = false
}

/// One pointer event of the piano roll's grid (tracks design D15 "Input adapter"): what `RollInputView` turns
/// `mouseDown` / `mouseDragged` / `mouseUp`, a lost key window and the view going away into, and the only input
/// `RollEditLogic` reads.
///
/// `location` is in the roll's CONTENT coordinates (`RollGeometry`: before any scroll, `x` from tick 0, `y` from
/// the top of pitch 127's row): the view applies its scroll and zoom through `RollGeometry.contentPoint(viewPoint:
/// scroll:)` before building the event. `clickCount` is AppKit's (1 for a single click, 2 for the second press of a
/// double-click; meaningful on `down` and `up`). `timestamp` is the event's `NSEvent.timestamp`. `cancel` ends any
/// gesture without committing (lost key window, view gone, a mouse-up that never arrives); its location, modifiers
/// and click count are ignored.
struct RollPointerEvent: Equatable {
  enum Phase: Equatable {
    case down, drag, up, cancel
  }

  var phase: Phase
  var location: CGPoint
  var modifiers: RollModifiers
  var clickCount: Int
  var timestamp: TimeInterval

  init(phase: Phase, location: CGPoint, modifiers: RollModifiers = RollModifiers(), clickCount: Int = 1,
       timestamp: TimeInterval = 0) {
    self.phase = phase
    self.location = location
    self.modifiers = modifiers
    self.clickCount = clickCount
    self.timestamp = timestamp
  }
}
