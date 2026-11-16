import CoreGraphics
import Foundation

/// What one scroll-wheel or trackpad scroll event does to the piano roll (tracks design D14, D15; plan Task 18), the
/// pure half of `RollInputView.scrollWheel`:
///
/// - **⌥** zooms the rows about the pointer's y; **⇧⌥** zooms time about the pointer's x; anything else (⇧ alone,
///   ⌘, ⌃, none) scrolls both axes. ⇧ with a plain mouse wheel already arrives as a horizontal delta (macOS moves it).
/// - **Scroll:** AppKit's `scrollingDeltaX/Y` say where the content moves (natural scrolling already applied), so the
///   roll's scroll moves against them: `(−dx, −dy)` points. A wheel without precise deltas reports lines, scaled by
///   `lineStep` points.
/// - **Zoom:** exponential in the delta, `zoomBase ^ delta` (delta in points; a line counts `lineStep`), so equal
///   motions in opposite directions cancel and a gesture's speed does not matter. The delta is the larger axis (a
///   ⇧ mouse wheel's delta comes in x) taken in the DEVICE's direction (natural scrolling undone), so a wheel turned
///   away from the user, or two fingers pushed up, zooms in whatever the scrolling preference. One wheel notch is
///   `1.01^10` ≈ ×1.105; the whole time range (×16) is about 280 pt of trackpad travel.
/// - **Momentum** (the events a flick keeps sending after the fingers lift, `NSEvent.momentumPhase` not empty) never
///   zooms: ⌥ pressed while a flick coasts would otherwise zoom by a motion the user no longer makes. Plain scrolling
///   keeps its momentum.
///
/// Depends on nothing but CoreGraphics; unit-tested by the `ChordLabUICore` package
/// (Tests/ChordLabUICoreTests/RollInputTests.swift).
enum RollWheel {
  enum Action: Equatable {
    case none
    /// Move the roll's scroll by this many points (content coordinates).
    case scroll(CGSize)
    /// Multiply the row height by `factor` about the pointer's y.
    case zoomRows(factor: CGFloat)
    /// Multiply the points per beat by `factor` about the pointer's x.
    case zoomTime(factor: CGFloat)
  }

  /// Points per line of a wheel without precise deltas.
  static let lineStep: CGFloat = 10
  /// The zoom factor per point of delta.
  static let zoomBase: CGFloat = 1.01

  /// The action of one event: its scrolling deltas, whether they are precise (points) or lines, whether AppKit
  /// inverted them from the device (natural scrolling), whether it is a momentum event, and the modifiers held.
  static func action(deltaX: CGFloat, deltaY: CGFloat, precise: Bool, invertedFromDevice: Bool, momentum: Bool,
                     modifiers: RollModifiers) -> Action {
    let scale = precise ? 1 : lineStep
    let dx = deltaX * scale
    let dy = deltaY * scale
    guard dx != 0 || dy != 0, dx.isFinite, dy.isFinite else { return .none }
    guard modifiers.option else { return .scroll(CGSize(width: -dx, height: -dy)) }
    guard !momentum else { return .none }
    let dominant = abs(dy) >= abs(dx) ? dy : dx
    let factor = pow(zoomBase, invertedFromDevice ? -dominant : dominant)
    return modifiers.shift ? .zoomTime(factor: factor) : .zoomRows(factor: factor)
  }
}

/// The roll's cursor over a point (tracks design D15; plan Task 18): the left-right resize cursor over a note's start
/// or end edge in Notes mode, the arrow everywhere else (a note's body included: it moves on a drag, which needs no
/// cursor of its own to discover; Velocity mode has no edges).
enum RollCursor: Equatable {
  case arrow, resizeLeftRight

  static func cursor(zone: RollHit.Zone?, mode: RollMode) -> RollCursor {
    guard mode == .notes, let zone else { return .arrow }
    switch zone {
    case .startEdge, .endEdge: return .resizeLeftRight
    case .body: return .arrow
    }
  }
}
