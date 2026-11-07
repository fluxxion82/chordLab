import CoreGraphics

/// Where a pressable view takes a press: its frame in `MouseButton.coordinateSpace`, clipped to what is
/// on screen (`visible`: the frame cut to the enclosing scroll view's viewport, so a cell scrolled out
/// of the palette's viewport, which stays mounted, never claims a click on whatever is drawn over that
/// spot), and whether the view's hit shape is the circle inscribed in the frame (a palette cell's
/// `.contentShape(Circle())`) rather than the frame itself.
public struct PressRegion: Equatable {
  public var frame: CGRect
  public var visible: CGRect
  public var circular: Bool

  public init(frame: CGRect, visible: CGRect, circular: Bool) {
    self.frame = frame
    self.visible = visible
    self.circular = circular
  }

  /// A view that is nowhere yet (before its first layout).
  public static let nowhere = PressRegion(frame: .zero, visible: .zero, circular: false)

  public func contains(_ point: CGPoint) -> Bool {
    guard visible.contains(point) else { return false }
    guard circular else { return true }
    let radius = min(frame.width, frame.height) / 2
    let dx = point.x - frame.midX
    let dy = point.y - frame.midY
    return dx * dx + dy * dy <= radius * radius
  }
}
