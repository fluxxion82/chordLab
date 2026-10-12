import SwiftUI

/// Key bar, transport row, sequence bar, suggestions strip and palette grid, with a temporary status
/// line and the MIDI drag-out control along the bottom.
struct ContentView: View {
  @EnvironmentObject var state: ChordLabState

  var body: some View {
    ZStack {
      PaletteView.background
      VStack(spacing: 12) {
        KeyBarView()

        TransportBarView()

        SequenceBarView()

        SuggestionsView()

        PaletteView()

        HStack(spacing: 16) {
          if let snapshot = state.snapshot {
            Text("\(snapshot.palette.cells.count) cells · \(snapshot.events.count) events · ends at beat \(snapshot.endBeat, specifier: "%.2f") · follow \(snapshot.followHost ? "on" : "off") · loop \(snapshot.progressionLoop ? "on" : "off")")
              .foregroundStyle(.gray).font(.callout.monospacedDigit())
          } else {
            Text("Waiting for a snapshot…").foregroundStyle(.gray).font(.callout)
          }
          Button("Append C") { state.send(command: ["cmd": "appendChord", "root": 0, "type": "maj"]) }
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
      }
      .padding(12)
    }
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
