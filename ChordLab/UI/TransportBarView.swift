import SwiftUI

/// Transport row above the sequence bar: Follow Live (the progression plays while Live's transport runs;
/// off, it is silent and previews still work), Loop (the progression repeats; off, it plays once and stops
/// at its end) and Panic. The toggles show the snapshot's `followHost` / `progressionLoop` and send
/// `setFollowHost` / `setProgressionLoop`; the snapshot the plug-in answers with is what flips them, so
/// they never drift from the document (and stay put if a command is rejected).
struct TransportBarView: View {
  @EnvironmentObject var state: ChordLabState

  private var followHost: Binding<Bool> {
    Binding(get: { state.snapshot?.followHost ?? true },
            set: { state.send(command: ["cmd": "setFollowHost", "on": $0]) })
  }

  private var loop: Binding<Bool> {
    Binding(get: { state.snapshot?.progressionLoop ?? true },
            set: { state.send(command: ["cmd": "setProgressionLoop", "on": $0]) })
  }

  var body: some View {
    HStack(spacing: 16) {
      Toggle(isOn: followHost) { Text("Follow Live").foregroundStyle(.white) }
        .toggleStyle(.switch)
        .help("Play the progression while Live's transport runs")
        .accessibilityLabel("Follow Live")
      Toggle(isOn: loop) { Text("Loop").foregroundStyle(.white) }
        .toggleStyle(.switch)
        .help("Repeat the progression; off, it plays once and stops at its end")
        .accessibilityLabel("Loop the progression")
      Button("Panic") { state.panic() }
        .help("Silence every sounding note")
        .accessibilityLabel("Silence every sounding note")
      Spacer(minLength: 0)
    }
    .font(.body)
  }
}

struct TransportBarView_Previews: PreviewProvider {
  static var previews: some View {
    TransportBarView().environmentObject(ChordLabState(numParams: kNumParams))
      .padding().background(PaletteView.background).frame(width: 900)
  }
}
