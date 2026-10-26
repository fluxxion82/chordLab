import CoreGraphics

/// One row of the palette grid; `diatonicRows` / `appliedRows` decide the top-to-bottom order.
struct PaletteRow: Equatable {
  let tier: Int
  let label: String
  let cells: [CellJSON?]   // indexed by degree (column)
}

/// The palette's geometry and row order, pure so it is tested. Rows: the diatonic grid shows the power
/// row (tier 0) on top, then tiers 4…1 so the triads are the bottom row as in Klimper; the applied grid
/// shows `V7/x` (tier 1) over `vii°7/x` (tier 0). Snapshot tier numbers are untouched: this is display
/// order only. Each row is the label gutter, a gap, then the seven columns with a gap between them:
/// `rowWidth`. Cell size: the largest square whose row fits the width and whose `rows` rows (with the
/// spacing and the grid's vertical padding) fit the height, clamped to `minCell…maxCell`.
enum PaletteLayout {
  static let columns = PaletteViewMode.columns
  static let spacing: CGFloat = 10
  static let minCell: CGFloat = 56
  static let maxCell: CGFloat = 96
  static let gutter: CGFloat = 44

  static func label(tier: Int) -> String {
    switch tier {
    case 0: return "power"
    case 1: return "triads"
    case 2: return "6 / 7"
    case 3: return "7 / add9"
    default: return "colour"
    }
  }

  static func rowWidth(cellSize: CGFloat) -> CGFloat {
    gutter + spacing * CGFloat(columns) + cellSize * CGFloat(columns)
  }

  static func diatonicRows(_ palette: PaletteJSON) -> [PaletteRow] {
    let tiers = max(1, palette.tiers)
    var cells = [[CellJSON?]](repeating: [CellJSON?](repeating: nil, count: columns), count: tiers)
    for cell in palette.cells where (0..<tiers).contains(cell.tier) && (0..<columns).contains(cell.degree) {
      cells[cell.tier][cell.degree] = cell
    }
    let order = [0] + Array((1..<tiers).reversed())
    return order.map { PaletteRow(tier: $0, label: label(tier: $0), cells: cells[$0]) }
  }

  static func appliedRows(_ applied: [CellJSON]) -> [PaletteRow] {
    let layout = PaletteViewMode.appliedLayout(applied)      // [tier][degree], tiers 0 and 1
    return [PaletteRow(tier: 1, label: "V7/x", cells: layout[1]),
            PaletteRow(tier: 0, label: "vii°7/x", cells: layout[0])]
  }

  static func cellSize(viewport: CGSize, rows: Int) -> CGFloat {
    let rows = max(1, rows)
    let widthFit = (viewport.width - gutter - spacing * CGFloat(columns)) / CGFloat(columns)
    let heightFit = (viewport.height - 2 * spacing - spacing * CGFloat(rows - 1)) / CGFloat(rows)
    return min(maxCell, max(minCell, min(widthFit, heightFit)))
  }
}
