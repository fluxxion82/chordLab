import SwiftUI

/// Transport row above the sequence bar: Follow Live (the progression plays while Live's transport runs;
/// off, it is silent and previews still work), Loop (the progression repeats; off, it plays once and stops
/// at its end), Panic, and Auto voice-lead (`compile` picks each non-manual chord's inversion and octave
/// for the smoothest motion from the chord before; P2-D5). The toggles show the snapshot's `followHost`
/// / `progressionLoop` / `autoVoiceLead` and send `setFollowHost` / `setProgressionLoop` /
/// `setAutoVoiceLead`; the snapshot the plug-in answers with is what flips them, so they never drift from
/// the document (and stay put if a command is rejected). "+ Section" starts a section (P2-D6) on the
/// selected block, else the last one (`addSection {index, name: ""}`; the engine names it).
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

  private var autoVoiceLead: Binding<Bool> {
    Binding(get: { state.snapshot?.autoVoiceLead ?? false },
            set: { state.send(command: ["cmd": "setAutoVoiceLead", "on": $0]) })
  }

  /// Where "+ Section" starts one: the selected block's index, else the last block's; nil without chords.
  private var sectionIndex: Int? {
    guard let events = state.snapshot?.events, !events.isEmpty else { return nil }
    return events.firstIndex { $0.id == state.snapshot?.selectedEventId } ?? events.count - 1
  }

  private func addSection() {
    guard let index = sectionIndex else { return }
    state.send(command: ["cmd": "addSection", "index": index, "name": ""])
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
      Toggle(isOn: autoVoiceLead) { Text("Auto voice-lead").foregroundStyle(.white) }
        .toggleStyle(.switch)
        .help("Choose each chord's inversion and octave for the smoothest motion from the chord before (manual chords keep theirs)")
        .accessibilityLabel("Auto voice-lead")
      Button("+ Section") { addSection() }
        .disabled(sectionIndex == nil)
        .help("Start a section on the selected chord (else the last one)")
        .accessibilityLabel("Add a section")
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
