import SwiftUI

/// Key bar, the key transpose row (only while it has something to say), transport row, sequence bar, the lane
/// tab row (`LaneTabsView`, tracks design D13) and the open lane: with Chords open the suggestions strip, palette
/// grid and (only while another key is browsed) the home-key compare row; with a track open its piano roll
/// (`PianoRollView`, tracks design D14) in their place. A temporary status line and the
/// MIDI drag-out control run along the bottom. When the engine could not play the
/// feel (`feelLimited`, plan D11), the bottom row says so beside the drag-out control, since the clip falls
/// back to block chords too (in place of the counts line, which would leave the sentence too little room at
/// 700 pt).
///
/// While this ChordLab follows another one's lane (`FollowerPanelModel` of the link state is not nil) the follower's
/// panel (`FollowerPanelView`) takes the whole window in place of all of that; the geometry, the hover tips, the
/// snapshot request on appear and the release on disappear stay outside the branch.
///
/// The window's measured size decides three things once, here: where the Transpose with key switch is (the
/// key bar from `keyBarSwitchMinWidth` up, else the transport row; exactly one of them shows it), whether the
/// Diatonic / Applied switch keeps its row above the palette (not while the key transpose row shows in a
/// window shorter than `compactHeight`, unless the listener is active: the row then holds the Heard group and
/// always shows), and whether the key transpose row draws compact (in a window shorter than `compactHeight`).
/// The palette viewport (or, with a track open, the roll area) is the row that gives up height; the status and
/// export row along the bottom never does.
struct ContentView: View {
  @EnvironmentObject var state: ChordLabState

  /// The narrowest window in which the key bar's two-row layout keeps the Transpose with key switch beside
  /// the Key pickers (KeyBarView's candidate (2)). Measured offscreen (the render harness, the key bar at
  /// each window width, 850…900 pt): with the switch, candidate (2) first fits at 877 pt for C♯ Harmonic
  /// Minor (the widest tonic menu) and at 875 pt for C Major; 3 pt added for margin. Below it the switch moves
  /// to the transport row and the key bar keeps its two rows.
  static let keyBarSwitchMinWidth: CGFloat = 880
  /// Below this window height the key transpose row draws compact and takes the Diatonic / Applied switch's
  /// row while it shows.
  static let compactHeight: CGFloat = 700

  /// The window's size, as last measured (900 × 620, the default window, until the first measurement).
  @State private var size = CGSize(width: 900, height: 620)

  var body: some View {
    let switchInKeyBar = size.width >= Self.keyBarSwitchMinWidth
    ZStack {
      PaletteView.background
      // Following another ChordLab's lane: the panel takes the whole window (plan section 5). The editor's
      // document stays alive behind it, and no key reaches it (`LinkKeys`).
      if let panel = FollowerPanelModel(state.linkState, snapshot: state.snapshot) {
        FollowerPanelView(model: panel)
      } else {
        VStack(spacing: 12) {
          KeyBarView(showsTransposeSwitch: switchInKeyBar)

          // The transpose hint or the moved note, in a row of its own: the rows below move down while it
          // shows, so nothing (Follow Live, Loop, Panic) is covered.
          if let row = state.keyRow {
            KeyTransposeRow(row: row, compact: size.height < Self.compactHeight)
          }

          TransportBarView(showsTransposeSwitch: !switchInKeyBar)

          SequenceBarView()

          LaneTabsView()

          // Chords open: the suggestions, the palette and the home row. A track open: its roll in their place
          // (tracks design D13), so the palette's switch row, and the Heard group in it, hide with them.
          if case .track(let id) = state.openLane, let track = state.snapshot?.track(id: id) {
            // A fresh roll per track (`id`), so each one measures its grid, and first opens, when it shows.
            PianoRollView(rolls: state.rolls, track: track)
              .id(track.id)
          } else {
            SuggestionsView()

            // While the listener is active the switch row carries the Heard group and always shows (D15).
            PaletteView(showsViewSwitch: state.keyRow == nil || size.height >= Self.compactHeight
                          || state.heard?.active == true)

            HomeRowView()
          }

          HStack(spacing: 16) {
            if state.snapshot?.feelLimited == true {
              // In place of the counts line, so the whole sentence fits in two lines at the 700-pt minimum.
              Text("This feel is too dense for the progression, so playback and export use block chords. Try a simpler rhythm.")
                .foregroundStyle(.orange).font(.callout).lineLimit(2)
            } else if let snapshot = state.snapshot {
              Text("\(snapshot.palette.cells.count) cells · \(snapshot.events.count) events · ends at beat \(snapshot.endBeat, specifier: "%.2f") · follow \(snapshot.followHost ? "on" : "off") · loop \(snapshot.progressionLoop ? "on" : "off")")
                .foregroundStyle(.gray).font(.callout.monospacedDigit())
            } else {
              Text("Waiting for a snapshot…").foregroundStyle(.gray).font(.callout)
            }
            if let snapshot = state.snapshot, !snapshot.hasTonicTable {
              Text("snapshot missing tonics").foregroundStyle(.orange).font(.callout)
            }
            StatusLines()

            Spacer()

            ExportDragView()
          }
          // Never shortened: the palette viewport above gives up height instead.
          .fixedSize(horizontal: false, vertical: true)
          .layoutPriority(1)
        }
        .padding(12)
      }
    }
    .onGeometryChange(for: CGSize.self) { $0.size } action: { size = $0 }
    // Hover tips (`HoverTip`) are drawn here, over everything and inside the window, never as popovers.
    .tipHost()
    .edgesIgnoringSafeArea(.all)
    .onAppear { state.requestSnapshot() }
    .onDisappear { state.releaseHeld() }      // editor closed mid-press: never leave a note sounding
  }
}

/// The scheduler's incomplete blocks and the last error (`ChordLabState.lastError`), each only while it has
/// something to say: the editor's bottom row draws them, and the follower's panel, which replaces that row.
struct StatusLines: View {
  @EnvironmentObject var state: ChordLabState

  var body: some View {
    if state.incompleteBlocks > 0 {
      Text("incomplete blocks: \(state.incompleteBlocks)").foregroundStyle(.orange).font(.callout.monospacedDigit())
    }
    if let error = state.lastError {
      Text("Error: \(error)").foregroundStyle(.red).font(.callout).lineLimit(2)
    }
  }
}

struct SwiftUIView_Previews: PreviewProvider {
  static var previews: some View {
    ContentView().environmentObject(ChordLabState(numParams: kNumParams))
      .frame(width: 900, height: 620)
  }
}
