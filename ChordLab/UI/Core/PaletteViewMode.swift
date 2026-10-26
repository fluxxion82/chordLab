import Foundation

/// Which grid the palette shows (plan A6): the browsed key's diatonic chords or its applied chords
/// (secondary dominants `V7/x` and leading-tone sevenths `vii°7/x`, the snapshot's `applied`). UI-only,
/// remembered across sessions in UserDefaults under `storageKey`; the engine has no command for it.
enum PaletteViewMode: String, CaseIterable, Identifiable {
  case diatonic, applied
  static let storageKey = "chordlab.paletteView"
  /// What a fresh install shows.
  static let defaultMode: PaletteViewMode = .diatonic
  /// Columns of either grid: one per scale degree of the browsed key.
  static let columns = 7
  /// Rows of the applied grid: tier 0 `vii°7/x` (bottom), tier 1 `V7/x` (top).
  static let appliedTiers = 2

  var id: String { rawValue }
  var title: String {
    switch self {
    case .diatonic: return "Diatonic"
    case .applied: return "Applied"
    }
  }

  /// The view to show for the user's `saved` choice: the diatonic grid when the snapshot has no `applied`
  /// field (an older plug-in build), else the saved one. Only a function of its inputs: the caller never
  /// writes the result back, so the preference survives a session with an older build.
  static func effective(saved: PaletteViewMode, snapshotHasApplied: Bool) -> PaletteViewMode {
    snapshotHasApplied ? saved : .diatonic
  }

  /// The applied cells indexed `[tier][degree]` (2 × 7), nil where there is no applied chord (the tonic
  /// column, a diminished target). `degree` is the column of the chord the cell leads to, so each applied
  /// chord sits in the column of its target. A cell outside the grid is ignored rather than trusted.
  static func appliedLayout(_ cells: [CellJSON]) -> [[CellJSON?]] {
    var rows = [[CellJSON?]](repeating: [CellJSON?](repeating: nil, count: columns), count: appliedTiers)
    for cell in cells where (0..<appliedTiers).contains(cell.tier) && (0..<columns).contains(cell.degree) {
      rows[cell.tier][cell.degree] = cell
    }
    return rows
  }

  /// The numeral a cell shows wherever one is shown (the cell, its drag preview, its tip): an applied
  /// chord's `label` ("V7/ii", relative to the browsed key), else the `roman` against the project key.
  /// ASCII as the engine writes it; the views apply `ChordSymbol.glyphs`.
  static func numeralText(_ cell: CellJSON) -> String {
    cell.label ?? cell.roman
  }
}
