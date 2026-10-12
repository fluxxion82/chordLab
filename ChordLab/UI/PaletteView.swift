import SwiftUI

/// The Klimper-style palette grid of the browsed key: 7 columns (scale degrees), one row per tier with
/// tier 0 (power chords) at the bottom and higher tiers above. A degree with no chord in a tier leaves
/// an empty slot so the columns stay aligned. Everything shown comes from the snapshot; the grid has no
/// interaction yet (preview is Task 2.4).
struct PaletteView: View {
  @EnvironmentObject var state: ChordLabState
  @AppStorage(LabelMode.storageKey) private var labelMode: LabelMode = .both

  static let columns = 7
  static let spacing: CGFloat = 10
  static let minCell: CGFloat = 56
  static let maxCell: CGFloat = 96
  static let background = Color(red: 0.043, green: 0.071, blue: 0.251)   // #0B1240

  var body: some View {
    GeometryReader { geometry in
      let available = geometry.size.width - Self.spacing * CGFloat(Self.columns - 1)
      let cellSize = min(Self.maxCell, max(Self.minCell, available / CGFloat(Self.columns)))
      ScrollView(.vertical) {
        if let palette = state.snapshot?.palette {
          let rows = Self.layout(palette)
          VStack(spacing: Self.spacing) {
            ForEach(rows.indices.reversed(), id: \.self) { tier in
              HStack(spacing: Self.spacing) {
                ForEach(0..<Self.columns, id: \.self) { degree in
                  if let cell = rows[tier][degree] {
                    PaletteCellView(cell: cell, size: cellSize, labelMode: labelMode)
                  } else {
                    Color.clear.frame(width: cellSize, height: cellSize)
                  }
                }
              }
            }
          }
          .frame(maxWidth: .infinity)
          .padding(.vertical, Self.spacing)
        } else {
          Text("Waiting for a snapshot…").foregroundStyle(.gray).frame(maxWidth: .infinity).padding()
        }
      }
    }
    .background(Self.background)
  }

  /// Cells indexed `[tier][degree]`, nil where the palette has no chord. Rows cover 0..<tiers (at least
  /// one); a cell outside the grid is ignored rather than trusted.
  static func layout(_ palette: PaletteJSON) -> [[CellJSON?]] {
    let tiers = max(1, palette.tiers)
    var rows = [[CellJSON?]](repeating: [CellJSON?](repeating: nil, count: columns), count: tiers)
    for cell in palette.cells where (0..<tiers).contains(cell.tier) && (0..<columns).contains(cell.degree) {
      rows[cell.tier][cell.degree] = cell
    }
    return rows
  }
}

/// One cell: a circle coloured by family, filled when every note is in the project key and a ring
/// otherwise (a borrowed chord), with the symbol and/or roman numeral inside.
struct PaletteCellView: View {
  let cell: CellJSON
  let size: CGFloat
  let labelMode: LabelMode

  static func color(family: String) -> Color {
    switch family {
    case "Major": return Color(red: 0.110, green: 0.431, blue: 0.490)   // #1C6E7D
    case "Minor": return Color(red: 0.357, green: 0.180, blue: 0.494)   // #5B2E7E
    default: return Color(red: 0.290, green: 0.310, blue: 0.416)        // #4A4F6A
    }
  }

  var body: some View {
    let color = Self.color(family: cell.family)
    ZStack {
      if cell.filled {
        Circle().fill(color)
      } else {
        Circle().strokeBorder(color, lineWidth: max(2, size * 0.045))
      }
      VStack(spacing: 1) {
        if labelMode != .numerals {
          Text(ChordSymbol.glyphs(cell.symbol))
            .font(.system(size: size * 0.24, weight: .medium))
        }
        if labelMode != .names {
          Text(ChordSymbol.glyphs(cell.roman))
            .font(.system(size: size * 0.16))
            .opacity(labelMode == .both ? 0.85 : 1)
        }
      }
      .foregroundStyle(.white)
      .lineLimit(1)
      .minimumScaleFactor(0.6)
      .padding(.horizontal, size * 0.08)
    }
    .frame(width: size, height: size)
    .accessibilityLabel("\(cell.symbol), \(cell.roman)")
  }
}

struct PaletteView_Previews: PreviewProvider {
  static var previews: some View {
    PaletteView().environmentObject(ChordLabState(numParams: kNumParams))
      .frame(width: 900, height: 460)
  }
}
