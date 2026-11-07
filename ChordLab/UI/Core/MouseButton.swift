import CoreGraphics

/// The left mouse button as the editor's window sees it, kept by the root from the window's own mouse
/// events and read by the views that act on a press: a palette cell (`PressAction`, through `PressLogic`)
/// and a hover tip (`HoverTip`). It replaces a zero-distance `DragGesture` on those views: such a gesture,
/// even a simultaneous one, keeps `.draggable` on the same view from ever starting a drag session (a
/// palette cell could not be dragged at all, in Live and in the standalone), while a press read from the
/// window's events leaves the drag alone.
///
/// `presses` counts the downs, so a view comparing two values sees a new press even when the button is
/// already back up (SwiftUI may deliver a down and its up in one update); `pressDragged` says whether the
/// latest press travelled past `dragThreshold` (per press, never carried into the next one); `isDown` is
/// the button itself and `pressLocation` where the latest press began, in `coordinateSpace`, so a view
/// can tell whether the press is its own. A down with the button already down (its up was missed)
/// counts as a new press.
public struct MouseButton: Equatable {
  /// The name of the coordinate space `pressLocation` is in: the editor root's, which `TipHost` names
  /// and the root's event monitor reports in. A view reads its own frame `in: .named(coordinateSpace)`.
  public static let coordinateSpace = "ChordLab.editorRoot"
  /// Points of travel from the down that make a press a drag, an application threshold: the preview of a
  /// pressed cell ends here, before or as the native drag session (which has its own threshold) begins.
  public static let dragThreshold: CGFloat = 3

  public private(set) var isDown = false
  public private(set) var presses = 0
  public private(set) var pressDragged = false
  public private(set) var pressLocation: CGPoint?
  /// Where the current press began, until it has travelled past the threshold.
  private var origin: CGPoint?

  public init() {}

  public mutating func down(at point: CGPoint) {
    isDown = true
    presses += 1
    pressDragged = false
    pressLocation = point
    origin = point
  }

  public mutating func dragged(to point: CGPoint) {
    guard isDown, let origin else { return }
    let dx = point.x - origin.x
    let dy = point.y - origin.y
    if dx * dx + dy * dy > Self.dragThreshold * Self.dragThreshold {
      pressDragged = true
      self.origin = nil
    }
  }

  public mutating func up() {
    isDown = false
    origin = nil
  }
}
