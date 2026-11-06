import Foundation

/// Matching the heard chord against the palette (design D15).
enum PaletteMatch {
  /// Index in `cells` of the first cell whose root and type equal the heard chord's (its inversion and bass
  /// do not matter); nil without a chord, or when the browsed palette has no such cell.
  static func heardIndex(_ cells: [CellJSON], heard: HeardChordJSON?) -> Int? {
    guard let heard else { return nil }
    return cells.firstIndex { $0.root == heard.root && $0.type == heard.type }
  }
}
