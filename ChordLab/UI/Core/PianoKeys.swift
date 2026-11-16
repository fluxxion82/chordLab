import CoreGraphics
import Foundation

/// The roll's keyboard column (tracks design D14: clicking a key previews it): which key a point is on, and the
/// velocity a key preview plays at. Unit-tested by the `ChordLabUICore` package
/// (Tests/ChordLabUICoreTests/PianoKeysTests.swift).
enum PianoKeys {
  /// A key click's velocity (an insert's, design D15).
  static let velocity = 100

  /// The pitch of the key at the column's view y (top-left origin) with the grid scrolled to content y `scrollY`;
  /// nil above pitch 127's row, below pitch 0's, or for a y that is not a number.
  static func pitch(atViewY y: CGFloat, geometry: RollGeometry, scrollY: CGFloat) -> Int? {
    let content = y + scrollY
    guard content.isFinite, content >= 0, content < CGFloat(RollGeometry.pitchCount) * geometry.rowHeight else { return nil }
    return geometry.pitch(atY: content)
  }
}
