import SwiftUI

/// M0 host-contract spikes (plan Tasks 0.3–0.6). Replaced by the real UI in M2.
struct ContentView: View {
  @EnvironmentObject var state: ChordLabState
  private let cMajor: [UInt8] = [60, 64, 67]

  var body: some View {
    ZStack {
      Color(red: 0.06, green: 0.08, blue: 0.22)
      VStack(spacing: 24) {
        Text("ChordLab — M0 spikes").font(.title2).foregroundStyle(.white)

        // Spike 1: preview a chord while the mouse is held.
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

        // Spike 4: a value that must survive save/reload through the state chunk.
        HStack {
          Text("Chunk test value: \(state.testValue)").foregroundStyle(.white)
          Stepper("", value: Binding(get: { state.testValue },
                                     set: { state.setTestValue($0) }))
            .labelsHidden()
        }
        // Spike 2: ppq-driven test arpeggio (one 1/8-note per beat, C D E F) while Live plays.
        Toggle("Spike 2: ppq test arp (route track 2, press play in Live)",
               isOn: Binding(get: { state.spikeArp }, set: { state.setSpikeArp($0) }))
          .toggleStyle(.switch).foregroundStyle(.white)

        Text(state.stateJson).font(.caption.monospaced()).foregroundStyle(.gray)
          .textSelection(.enabled)
      }
      .padding()
    }
    .edgesIgnoringSafeArea(.all)
  }
}

struct SwiftUIView_Previews: PreviewProvider {
  static var previews: some View {
    ContentView().environmentObject(ChordLabState(numParams: kNumParams))
      .frame(width: 600, height: 400)
  }
}
