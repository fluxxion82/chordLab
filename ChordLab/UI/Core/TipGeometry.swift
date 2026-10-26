import CoreGraphics

/// Where a hover tip goes relative to the view it describes, inside the editor's bounds. The tip is
/// centred on the anchor and sits `gap` above it (or below when asked, or when the preferred side has no
/// room); when neither side has room it takes the larger side; then it is clamped inside the container
/// with a `margin`. Pure, so it is tested.
enum TipGeometry {
  static let margin: CGFloat = 4

  static func frame(anchor: CGRect, tipSize: CGSize, in container: CGRect, preferAbove: Bool,
                    gap: CGFloat) -> CGRect {
    let roomAbove = anchor.minY - container.minY - gap
    let roomBelow = container.maxY - anchor.maxY - gap
    let above: Bool
    if preferAbove {
      above = tipSize.height <= roomAbove || (tipSize.height > roomBelow && roomAbove >= roomBelow)
    } else {
      above = tipSize.height > roomBelow && (tipSize.height <= roomAbove || roomAbove > roomBelow)
    }
    var origin = CGPoint(x: anchor.midX - tipSize.width / 2,
                         y: above ? anchor.minY - gap - tipSize.height : anchor.maxY + gap)
    origin.x = min(max(origin.x, container.minX + margin), container.maxX - margin - tipSize.width)
    origin.y = min(max(origin.y, container.minY + margin), container.maxY - margin - tipSize.height)
    return CGRect(origin: origin, size: tipSize)
  }
}
