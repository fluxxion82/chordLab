import SwiftUI

/// The two-keys-at-once compare row (P2-M3): while a key other than the project key is browsed, the
/// project ("home") key's seven diatonic triads sit under the palette, so the browsed chords can be
/// compared with the home ones and a home chord can still be auditioned or dropped. The cells are the
/// snapshot's `homeTriads` (`Snapshot.homeRow`) drawn by `PaletteCellControl`, the palette's own cell, so
/// press-to-preview, drag to the sequence bar, the function dot, the hover tip (with its tension bar)
/// and the label mode behave exactly as in the grid, at a smaller size.
/// With nothing to show (home is browsed, or an older plug-in build) the body is empty and the row takes
/// no space, not even the parent stack's spacing; when shown it sits outside the palette's scroll view,
/// so the grid's cell size and columns are untouched (the palette viewport is that much shorter).
struct HomeRowView: View {
  @EnvironmentObject var state: ChordLabState
  @AppStorage(LabelMode.storageKey) private var labelMode = LabelMode.defaultMode

  static let cellSize: CGFloat = 48

  var body: some View {
    if let snapshot = state.snapshot, !snapshot.homeRow.isEmpty {
      HStack(spacing: PaletteView.spacing) {
        Text("Home · " + KeyNames.displayName(snapshot.projectKey))
          .font(.callout).foregroundStyle(.secondary)
          .lineLimit(1).fixedSize()
          .accessibilityLabel("Home key " + KeyNames.displayName(snapshot.projectKey))
        ForEach(snapshot.homeRow) { cell in
          PaletteCellControl(cell: cell, size: Self.cellSize, labelMode: labelMode)
        }
        Spacer(minLength: 0)
        if snapshot.applied != nil {
          PaletteViewSwitch()
        }
      }
    }
  }
}
