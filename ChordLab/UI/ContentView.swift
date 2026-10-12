import SwiftUI

/// Key bar over a temporary M2.1 status area (replaced by the palette grid in Task 2.3), with the
/// Spike 1 preview button and the Spike 3 drag-out label kept until their Live verification.
struct ContentView: View {
  @EnvironmentObject var state: ChordLabState
  private let cMajor: [UInt8] = [60, 64, 67]

  var body: some View {
    ZStack {
      Color(red: 0.04, green: 0.07, blue: 0.25)
      VStack(spacing: 16) {
        KeyBarView()

        if let snapshot = state.snapshot {
          Grid(alignment: .leading, horizontalSpacing: 16, verticalSpacing: 6) {
            GridRow { Text("Project key"); Text(snapshot.projectKeyName) }
            GridRow { Text("Browsed key"); Text(snapshot.browsedKeyName) }
            GridRow { Text("Palette"); Text("\(snapshot.palette.cells.count) cells, \(snapshot.palette.tiers) tiers") }
            GridRow { Text("Events"); Text("\(snapshot.events.count), ending at beat \(snapshot.endBeat, specifier: "%.2f")") }
          }
          .foregroundStyle(.white)
          .font(.body.monospacedDigit())
        } else {
          Text("Waiting for a snapshot…").foregroundStyle(.gray)
        }

        Button("Append C") { state.send(command: ["cmd": "appendChord", "root": 0, "type": "maj"]) }

        if let error = state.lastError {
          Text("Error: \(error)").foregroundStyle(.red).font(.callout)
        }

        Spacer()

        // Spike 1: preview a chord while the mouse is held (Live verification still pending).
        Text("C major (hold)")
          .font(.title3).foregroundStyle(.white)
          .padding(.horizontal, 28).padding(.vertical, 16)
          .background(Circle().fill(Color(red: 0.11, green: 0.44, blue: 0.50)))
          .pressAction(onPress: { state.sendChord(cMajor, on: true) },
                       onRelease: { state.sendChord(cMajor, on: false) })

        // Spike 3: drag a prepared .mid out to the DAW.
        Text("♫ Drag MIDI clip to Live")
          .foregroundStyle(.white)
          .padding(12)
          .background(RoundedRectangle(cornerRadius: 8).fill(Color(red: 0.20, green: 0.25, blue: 0.55)))
          .onDrag {
            guard let url = ExportFixture.prepareExportFile() else { return NSItemProvider() }
            return NSItemProvider(contentsOf: url) ?? NSItemProvider()
          }
      }
      .padding()
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
