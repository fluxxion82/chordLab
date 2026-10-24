import SwiftUI

/// The Klimper-style palette grid of the browsed key: 7 columns (scale degrees), one row per tier with
/// tier 0 (power chords) at the bottom and higher tiers above. A degree with no chord in a tier leaves
/// an empty slot so the columns stay aligned. Everything shown comes from the snapshot. Pressing a cell
/// previews its voicing through `ChordLabState.pressCell` / `releaseCell` (press, release, cancel and
/// disappearance are all handled by `PressAction`); dragging a cell carries a `ChordRef` to the
/// sequence bar (both wired in `PaletteCellControl`). The press is a simultaneous gesture under `.draggable`: the preview starts on mouse
/// down and ends when the press gesture ends or is cancelled. Starting the native drag session is
/// expected, not guaranteed, to cancel it; if it does not, the preview lasts until the drop, and the
/// scheduler's 4 s watchdog is the backstop either way. A press that never moves far enough simply
/// releases on mouse up.
struct PaletteView: View {
  @EnvironmentObject var state: ChordLabState
  @AppStorage(LabelMode.storageKey) private var labelMode = LabelMode.defaultMode

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
        if let snapshot = state.snapshot {
          let rows = Self.layout(snapshot.palette)
          VStack(spacing: Self.spacing) {
            ForEach(rows.indices.reversed(), id: \.self) { tier in
              HStack(spacing: Self.spacing) {
                ForEach(0..<Self.columns, id: \.self) { degree in
                  if let cell = rows[tier][degree] {
                    PaletteCellControl(cell: cell, size: cellSize, labelMode: labelMode)
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
      .defaultScrollAnchor(.bottom)   // tier 0 visible first, and kept in view when the tier count changes
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

/// A cell with its gestures, as the palette grid and the home row (`HomeRowView`) both use it: the look
/// of `PaletteCellView` (with its hover popover), press to preview through `ChordLabState.pressCell` /
/// `releaseCell`, and a drag that carries the chord's `ChordRef` to the sequence bar under a
/// `ChordDragPreview`. The one place these are wired, so the two can not drift apart; the relatedness
/// look (`RelationStyle`) and the popover's relation line come with it, so home cells get them too.
struct PaletteCellControl: View {
  @EnvironmentObject var state: ChordLabState
  let cell: CellJSON
  let size: CGFloat
  let labelMode: LabelMode

  var body: some View {
    PaletteCellView(cell: cell, size: size, labelMode: labelMode, isPressed: state.heldCellId == cell.id,
                    reference: state.snapshot?.relationReference)
      .pressAction(onPress: { state.pressCell(cell) },
                   onRelease: { state.releaseCell(cell) })
      .draggable(ChordRef(root: cell.root, type: cell.type)) {
        ChordDragPreview(cell: cell, labelMode: labelMode)
      }
  }
}

/// One cell: a circle coloured by family, filled when every note is in the project key and a ring
/// otherwise (a borrowed chord), with the symbol and/or roman numeral inside and a 6-pt function dot
/// (`FunctionStyle`) at the bottom left, inside the circle. Label mode "both" writes the function's short
/// name after the numeral ("I · T"). While pressed it gets a white ring and a brighter fill (Klimper's
/// selected look). Resting the mouse on it for a moment opens a popover with the chord's details
/// (`HoverPopover`); the popover goes away on hover exit and while the cell is pressed.
///
/// Chord relations (plan R7): when the cell carries a `relation`, everything drawn (circle, ring, labels,
/// dot, pressed ring) is scaled by `RelationStyle.scale` about the slot's centre and faded to
/// `RelationStyle.opacity`, animated with `RelationStyle.animation`. Only the drawing changes: the view's
/// frame stays `size` × `size` and its content shape is the full-size circle, so the grid does not move
/// and the press, hover and drag hit area of a shrunken cell is the whole slot circle. A pressed cell is
/// drawn at full opacity, so the pressed look always shows. `reference` (the snapshot's
/// `relationReference`) names the chord in the popover's relation line.
struct PaletteCellView: View {
  let cell: CellJSON
  let size: CGFloat
  let labelMode: LabelMode
  var isPressed = false
  var reference: RelationReferenceJSON? = nil

  static let dotSize: CGFloat = 6

  static func color(family: String) -> Color {
    switch family {
    case "Major": return Color(red: 0.110, green: 0.431, blue: 0.490)   // #1C6E7D
    case "Minor": return Color(red: 0.357, green: 0.180, blue: 0.494)   // #5B2E7E
    default: return Color(red: 0.290, green: 0.310, blue: 0.416)        // #4A4F6A
    }
  }

  /// The numeral line: the roman numeral, plus " · T" (the function's short name) in label mode "both".
  static func numeralLabel(roman: String, function: String?, labelMode: LabelMode) -> String {
    guard labelMode == .both, let function = function else { return ChordSymbol.glyphs(roman) }
    return ChordSymbol.glyphs(roman) + " · " + function
  }

  var body: some View {
    let color = Self.color(family: cell.family)
    ZStack {
      if cell.filled {
        Circle().fill(color)
      } else {
        Circle().strokeBorder(color, lineWidth: max(2, size * 0.045))
      }
      if isPressed {
        Circle().fill(Color.white.opacity(cell.filled ? 0.25 : 0.15))
        Circle().strokeBorder(Color.white, lineWidth: 2)
      }
      VStack(spacing: 1) {
        if labelMode != .numerals {
          Text(ChordSymbol.glyphs(cell.symbol))
            .font(.system(size: size * 0.24, weight: .medium))
        }
        if labelMode != .names {
          Text(Self.numeralLabel(roman: cell.roman, function: cell.function, labelMode: labelMode))
            .font(.system(size: size * 0.16))
            .opacity(labelMode == .both ? 0.85 : 1)
        }
      }
      .foregroundStyle(.white)
      .lineLimit(1)
      .minimumScaleFactor(0.6)
      .padding(.horizontal, size * 0.08)
      // Bottom left on the circle's diagonal, 0.72 of the radius out: inside the ring at every cell size.
      FunctionDot(function: cell.function, size: Self.dotSize)
        .offset(x: -size * 0.255, y: size * 0.255)
    }
    .frame(width: size, height: size)
    .scaleEffect(RelationStyle.scale(relation: cell.relation))   // drawing only: layout keeps the slot
    .opacity(isPressed ? 1 : RelationStyle.opacity(relation: cell.relation))
    .animation(RelationStyle.animation, value: cell.relation)
    .frame(width: size, height: size)   // the unchanged slot
    .contentShape(Circle())             // full-size: the hit area ignores the relation scale
    .accessibilityLabel(ChordSymbol.spokenName(symbol: cell.symbol, type: cell.type) + (cell.filled ? ", in key" : ", borrowed")
                        + (cell.relation.map { ", relation \($0)" } ?? ""))
    .hoverPopover(suppressed: isPressed) {
      ChordDetailView(cell: cell, reference: reference).padding(12)
    }
  }
}

/// What travels under the mouse while a cell is dragged to the sequence bar: a compact block in the
/// cell's colour with the label the cell shows.
struct ChordDragPreview: View {
  let cell: CellJSON
  let labelMode: LabelMode

  var body: some View {
    let color = PaletteCellView.color(family: cell.family)
    ZStack {
      if cell.filled {
        RoundedRectangle(cornerRadius: 6).fill(color)
      } else {
        RoundedRectangle(cornerRadius: 6).strokeBorder(color, lineWidth: 2)
      }
      VStack(spacing: 1) {
        if labelMode != .numerals {
          Text(ChordSymbol.glyphs(cell.symbol)).font(.system(size: 13, weight: .medium))
        }
        if labelMode != .names {
          Text(PaletteCellView.numeralLabel(roman: cell.roman, function: cell.function, labelMode: labelMode))
            .font(.system(size: 11)).opacity(labelMode == .both ? 0.85 : 1)
        }
      }
      .foregroundStyle(.white)
    }
    .frame(width: 4 * SequenceBarView.pxPerBeat, height: SequenceBarView.blockHeight)   // a 4-beat block
  }
}

/// Popover body: symbol and numeral, family and function word, the voicing as note names, the tension
/// bar, and the engine's verdict on the project key: "In key" or the borrowed notes (`outOfKey`). The
/// function, tension and out-of-key lines are omitted when an older plug-in build sends no such field.
/// (A cell carries no "borrowed from" key, only its out-of-key notes; the block popover, whose event has a
/// `why`, names the key.) With a `relation` the popover ends with "Relation to G7 · 72" and the engine's
/// `relationText` below it.
struct ChordDetailView: View {
  let cell: CellJSON
  var reference: RelationReferenceJSON? = nil

  var body: some View {
    VStack(alignment: .leading, spacing: 6) {
      HStack(alignment: .firstTextBaseline, spacing: 10) {
        Text(ChordSymbol.glyphs(cell.symbol)).font(.title2.weight(.semibold))
        Text(ChordSymbol.glyphs(cell.roman)).font(.title3).foregroundStyle(.secondary)
      }
      HStack(spacing: 6) {
        Text(cell.family).font(.callout).foregroundStyle(.secondary)
        if let word = HarmonyText.functionWord(cell.function) {
          FunctionDot(function: cell.function)
          Text(word).font(.callout).foregroundStyle(.secondary)
        }
      }
      Text(HarmonyText.voicingText(cell.voicing)).font(.body.monospacedDigit())
      if let tension = cell.tension {
        TensionBar(tension: tension, function: cell.function)
      }
      if let outside = cell.outOfKey {
        if outside.isEmpty {
          Text("In key").font(.callout).foregroundStyle(.secondary)
        } else {
          Text("Out of key: " + outside.map(ChordSymbol.pitchClassName).joined(separator: ", "))
            .font(.callout).foregroundStyle(.orange)
        }
      }
      if let headline = RelationStyle.headline(reference: reference, relation: cell.relation) {
        Text(headline).font(.callout)
        if let text = cell.relationText {
          Text(text).font(.callout).foregroundStyle(.secondary)
            .fixedSize(horizontal: false, vertical: true)
        }
      }
    }
    .frame(minWidth: 160, maxWidth: 320, alignment: .leading)
  }
}

struct PaletteView_Previews: PreviewProvider {
  static var previews: some View {
    PaletteView().environmentObject(ChordLabState(numParams: kNumParams))
      .frame(width: 900, height: 460)
  }
}
