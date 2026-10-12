import SwiftUI

/// Key bar over the palette grid, with a temporary status line and the Spike 3 drag-out label along the
/// bottom until M3/M4 replace them.
struct ContentView: View {
  @EnvironmentObject var state: ChordLabState

  var body: some View {
    ZStack {
      PaletteView.background
      VStack(spacing: 12) {
        KeyBarView()

        PaletteView()

        HStack(spacing: 16) {
          if let snapshot = state.snapshot {
            Text("\(snapshot.palette.cells.count) cells · \(snapshot.events.count) events · ends at beat \(snapshot.endBeat, specifier: "%.2f")")
              .foregroundStyle(.gray).font(.callout.monospacedDigit())
          } else {
            Text("Waiting for a snapshot…").foregroundStyle(.gray).font(.callout)
          }
          Button("Append C") { state.send(command: ["cmd": "appendChord", "root": 0, "type": "maj"]) }
          Button("Panic") { state.panic() }
            .accessibilityLabel("Silence every sounding note")
          if state.incompleteBlocks > 0 {
            Text("incomplete blocks: \(state.incompleteBlocks)").foregroundStyle(.orange).font(.callout.monospacedDigit())
          }
          if let error = state.lastError {
            Text("Error: \(error)").foregroundStyle(.red).font(.callout).lineLimit(2)
          }

          Spacer()

          // Spike 3: drag a prepared .mid out to the DAW.
          Text("♫ Drag MIDI clip to Live")
            .foregroundStyle(.white)
            .padding(.horizontal, 12).padding(.vertical, 8)
            .background(RoundedRectangle(cornerRadius: 8).fill(Color(red: 0.20, green: 0.25, blue: 0.55)))
            .onDrag {
              guard let url = ExportFixture.prepareExportFile() else { return NSItemProvider() }
              return NSItemProvider(contentsOf: url) ?? NSItemProvider()
            }
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
