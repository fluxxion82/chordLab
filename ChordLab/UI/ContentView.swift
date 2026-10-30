import SwiftUI

/// Key bar, the key transpose row (only while it has something to say), transport row, sequence bar,
/// suggestions strip, palette grid and (only while another key is browsed) the home-key compare row, with a
/// temporary status line and the MIDI drag-out control along the bottom.
///
/// The window's measured size decides two things once, here: where the Transpose with key switch is (the key
/// bar from `keyBarSwitchMinWidth` up, else the transport row; exactly one of them shows it), and whether the
/// Diatonic / Applied switch keeps its row above the palette (not while the key transpose row shows in a
/// window shorter than `compactHeight`). The palette viewport is the row that gives up height; the status
/// and export row along the bottom never does.
struct ContentView: View {
  @EnvironmentObject var state: ChordLabState

  /// The narrowest window in which the key bar's two-row layout keeps the Transpose with key switch beside
  /// the Key pickers (KeyBarView's candidate (2)). Measured offscreen (the render harness, the key bar at
  /// each window width, 850…900 pt): with the switch, candidate (2) first fits at 877 pt for C♯ Harmonic
  /// Minor (the widest tonic menu) and at 875 pt for C Major; 3 pt added for margin. Below it the switch moves
  /// to the transport row and the key bar keeps its two rows.
  static let keyBarSwitchMinWidth: CGFloat = 880
  /// Below this window height the key transpose row takes the Diatonic / Applied switch's row while it shows.
  static let compactHeight: CGFloat = 700

  /// The window's size, as last measured (900 × 620, the default window, until the first measurement).
  @State private var size = CGSize(width: 900, height: 620)

  var body: some View {
    let switchInKeyBar = size.width >= Self.keyBarSwitchMinWidth
    ZStack {
      PaletteView.background
      VStack(spacing: 12) {
        KeyBarView(showsTransposeSwitch: switchInKeyBar)

        // The transpose hint or the moved note, in a row of its own: the rows below move down while it
        // shows, so nothing (Follow Live, Loop, Panic) is covered.
        if let row = state.keyRow {
          KeyTransposeRow(row: row)
        }

        TransportBarView(showsTransposeSwitch: !switchInKeyBar)

        SequenceBarView()

        SuggestionsView()

        PaletteView(showsViewSwitch: state.keyRow == nil || size.height >= Self.compactHeight)

        HomeRowView()

        HStack(spacing: 16) {
          if let snapshot = state.snapshot {
            Text("\(snapshot.palette.cells.count) cells · \(snapshot.events.count) events · ends at beat \(snapshot.endBeat, specifier: "%.2f") · follow \(snapshot.followHost ? "on" : "off") · loop \(snapshot.progressionLoop ? "on" : "off")")
              .foregroundStyle(.gray).font(.callout.monospacedDigit())
          } else {
            Text("Waiting for a snapshot…").foregroundStyle(.gray).font(.callout)
          }
          if let snapshot = state.snapshot, !snapshot.hasTonicTable {
            Text("snapshot missing tonics").foregroundStyle(.orange).font(.callout)
          }
          if state.incompleteBlocks > 0 {
            Text("incomplete blocks: \(state.incompleteBlocks)").foregroundStyle(.orange).font(.callout.monospacedDigit())
          }
          if let error = state.lastError {
            Text("Error: \(error)").foregroundStyle(.red).font(.callout).lineLimit(2)
          }

          Spacer()

          ExportDragView()
        }
        // Never shortened: the palette viewport above gives up height instead.
        .fixedSize(horizontal: false, vertical: true)
        .layoutPriority(1)
      }
      .padding(12)
    }
    .onGeometryChange(for: CGSize.self) { $0.size } action: { size = $0 }
    // Hover tips (`HoverTip`) are drawn here, over everything and inside the window, never as popovers.
    .tipHost()
    .edgesIgnoringSafeArea(.all)
    .onAppear { state.requestSnapshot() }
    .onDisappear { state.releaseHeld() }      // editor closed mid-press: never leave a note sounding
  }
}

struct SwiftUIView_Previews: PreviewProvider {
  static var previews: some View {
    ContentView().environmentObject(ChordLabState(numParams: kNumParams))
      .frame(width: 900, height: 620)
  }
}
