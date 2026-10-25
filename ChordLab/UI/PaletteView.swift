import SwiftUI

/// The Klimper-style palette grid of the browsed key: 7 columns (scale degrees), one row per tier with
/// tier 0 (power chords) at the bottom and higher tiers above. A degree with no chord in a tier leaves
/// an empty slot so the columns stay aligned. Everything shown comes from the snapshot.
///
/// A compact *Diatonic / Applied* switch (plan A6) sits at the top left, above the scroll view, and
/// picks the grid: the diatonic one above, or the applied one, the snapshot's `applied` cells in two
/// rows (`V7/x` on top, `vii°7/x` below) in the column of the chord each resolves to
/// (`PaletteViewMode.appliedLayout`), at the same cell size and through the same `PaletteCellControl`.
/// The choice is remembered under `PaletteViewMode.storageKey`. A snapshot without `applied` (an older
/// plug-in build) hides the switch and shows the diatonic grid, without touching the saved choice; an
/// empty `applied` shows "No applied chords in this key". The scroll view is rebuilt per view (`.id`), so
/// switching removes the old grid's cells and `PressAction`'s disappearance path releases a held one
/// (never the same views reused for other chords, whose release would not match the held id), and each
/// view opens at its own anchor: the diatonic grid at tier 0, the applied grid at its `V7/x` row. Pressing a cell
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
  @AppStorage(PaletteViewMode.storageKey) private var savedView = PaletteViewMode.defaultMode

  static let columns = PaletteViewMode.columns
  static let spacing: CGFloat = 10
  static let minCell: CGFloat = 56
  static let maxCell: CGFloat = 96
  static let background = Color(red: 0.043, green: 0.071, blue: 0.251)   // #0B1240

  var body: some View {
    let hasApplied = state.snapshot?.applied != nil
    let view = PaletteViewMode.effective(saved: savedView, snapshotHasApplied: hasApplied)
    VStack(alignment: .leading, spacing: 6) {
      if hasApplied {
        Picker("Palette view", selection: $savedView) {
          ForEach(PaletteViewMode.allCases) { Text($0.title).tag($0) }
        }
        .pickerStyle(.segmented)
        .labelsHidden()
        .controlSize(.small)
        .fixedSize()
        .help("Diatonic: the key's chords. Applied: the chords that lead into them (V7/x, vii°7/x).")
      }
      GeometryReader { geometry in
        let available = geometry.size.width - Self.spacing * CGFloat(Self.columns - 1)
        let cellSize = min(Self.maxCell, max(Self.minCell, available / CGFloat(Self.columns)))
        ScrollView(.vertical) {
          if let snapshot = state.snapshot {
            if view == .applied, let applied = snapshot.applied {
              if applied.isEmpty {
                Text("No applied chords in this key")
                  .foregroundStyle(.gray)
                  .frame(maxWidth: .infinity, minHeight: geometry.size.height)
              } else {
                grid(PaletteViewMode.appliedLayout(applied), cellSize: cellSize)
              }
            } else {
              grid(Self.layout(snapshot.palette), cellSize: cellSize)
            }
          } else {
            Text("Waiting for a snapshot…").foregroundStyle(.gray).frame(maxWidth: .infinity).padding()
          }
        }
        // Diatonic: tier 0 visible first, and kept in view when the tier count changes. Applied: the V7/x row
        // (top) first when both rows do not fit.
        .defaultScrollAnchor(view == .applied ? .top : .bottom)
        // A related cell is drawn up to 1.10× its slot (`RelationStyle`), and at the 700-pt minimum width the
        // grid fills the scroll view edge to edge, so an enlarged first- or last-column circle overhangs the
        // scroll view by up to 4.4 pt. Clip vertically as before, but 5 pt wider on each side (the window's
        // 12-pt padding has room); the grid geometry is unchanged.
        .scrollClipDisabled()
        .mask { Rectangle().padding(.horizontal, -5) }
        // A fresh scroll view per view: the default anchor applies only to a new one, and the old grid's
        // cells disappear (releasing a held one) rather than being reused.
        .id(view)
      }
    }
    .background(Self.background)
  }

  /// `rows` (indexed `[tier][degree]`) top tier first, an empty slot where a row has no chord.
  private func grid(_ rows: [[CellJSON?]], cellSize: CGFloat) -> some View {
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
/// (`HoverPopover`); the popover goes away on hover exit and while the cell is pressed. The numeral is
/// `PaletteViewMode.numeralText`: an applied chord's `label` ("V7/ii · D"), else the `roman`.
///
/// Chord relations (plan R7): when the cell carries a `relation`, everything drawn (circle, ring, labels,
/// dot, pressed ring) is scaled by `RelationStyle.scale` about the slot's centre and faded to
/// `RelationStyle.opacity`, animated with `RelationStyle.animation`. Only the drawing changes: the view's
/// frame stays `size` × `size` and its content shape is the full-size circle, so the grid does not move
/// and the press, hover and drag hit area of a shrunken cell is the whole slot circle. At the 1.10 maximum
/// (plan R7, kept on purpose) two neighbouring score-100 cells at the 96-pt maximum size come within 0.4 pt
/// of each other across the 10-pt spacing; they never overlap. The palette's scroll view widens its
/// horizontal clip so enlarged edge columns are not cut. A pressed cell is
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
          Text(Self.numeralLabel(roman: PaletteViewMode.numeralText(cell), function: cell.function, labelMode: labelMode))
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
          Text(PaletteCellView.numeralLabel(roman: PaletteViewMode.numeralText(cell), function: cell.function,
                                            labelMode: labelMode))
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
/// `relationText` below it. The numeral is `PaletteViewMode.numeralText`; an applied chord adds "Leads to
/// Dm (V7/ii)" under the header.
struct ChordDetailView: View {
  let cell: CellJSON
  var reference: RelationReferenceJSON? = nil

  var body: some View {
    VStack(alignment: .leading, spacing: 6) {
      HStack(alignment: .firstTextBaseline, spacing: 10) {
        Text(ChordSymbol.glyphs(cell.symbol)).font(.title2.weight(.semibold))
        Text(ChordSymbol.glyphs(PaletteViewMode.numeralText(cell))).font(.title3).foregroundStyle(.secondary)
      }
      if let leads = Self.leadsToLine(cell) {
        Text(leads).font(.callout)
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

  /// An applied chord's "Leads to Dm (V7/ii)" (glyphs applied); nil for a cell without a `target`.
  static func leadsToLine(_ cell: CellJSON) -> String? {
    guard let target = cell.target else { return nil }
    return "Leads to " + ChordSymbol.glyphs(target) + (cell.label.map { " (" + ChordSymbol.glyphs($0) + ")" } ?? "")
  }
}

struct PaletteView_Previews: PreviewProvider {
  static var previews: some View {
    PaletteView().environmentObject(ChordLabState(numParams: kNumParams))
      .frame(width: 900, height: 460)
  }
}
