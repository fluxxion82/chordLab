import SwiftUI

/// Key bar over the palette grid, with a temporary status line, the Spike 1 preview button and the
/// Spike 3 drag-out label along the bottom until their Live verification and M3/M4 replace them.
struct ContentView: View {
  @EnvironmentObject var state: ChordLabState
  private let cMajor: [UInt8] = [60, 64, 67]

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
          if let error = state.lastError {
            Text("Error: \(error)").foregroundStyle(.red).font(.callout).lineLimit(2)
          }

          Spacer()

          // Spike 1: preview a chord while the mouse is held (Live verification still pending).
          Text("C major (hold)")
            .foregroundStyle(.white)
            .padding(.horizontal, 16).padding(.vertical, 8)
            .background(Capsule().fill(Color(red: 0.11, green: 0.44, blue: 0.50)))
            .pressAction(onPress: { state.sendChord(cMajor, on: true) },
                         onRelease: { state.sendChord(cMajor, on: false) })

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
  }
}

struct SwiftUIView_Previews: PreviewProvider {
  static var previews: some View {
    ContentView().environmentObject(ChordLabState(numParams: kNumParams))
      .frame(width: 900, height: 620)
  }
}
