import CoreGraphics

/// Where a hover tip goes relative to the view it describes, inside the editor's bounds. The tip is
/// centred on the anchor and sits `gap` above it (or below when asked, or when the preferred side has no
/// room); when neither side has room it takes the larger side, the preferred one on a tie; then it is
/// clamped inside the container with a `margin`. A side's room counts the gap and the margin, so a tip
/// that fits keeps its full gap. A tip larger than the container is pinned to the top leading margin, so
/// the start of its content stays in view. Pure, so it is tested.
enum TipGeometry {
  static let margin: CGFloat = 4

  static func frame(anchor: CGRect, tipSize: CGSize, in container: CGRect, preferAbove: Bool,
                    gap: CGFloat) -> CGRect {
    let roomAbove = anchor.minY - container.minY - gap - margin
    let roomBelow = container.maxY - anchor.maxY - gap - margin
    let above: Bool
    if preferAbove {
      above = tipSize.height <= roomAbove || (tipSize.height > roomBelow && roomAbove >= roomBelow)
    } else {
      above = tipSize.height > roomBelow && (tipSize.height <= roomAbove || roomAbove > roomBelow)
    }
    var origin = CGPoint(x: anchor.midX - tipSize.width / 2,
                         y: above ? anchor.minY - gap - tipSize.height : anchor.maxY + gap)
    origin.x = max(min(origin.x, container.maxX - margin - tipSize.width), container.minX + margin)
    origin.y = max(min(origin.y, container.maxY - margin - tipSize.height), container.minY + margin)
    return CGRect(origin: origin, size: tipSize)
  }
}
