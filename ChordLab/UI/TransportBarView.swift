import SwiftUI

/// Transport row above the sequence bar: Follow Live (the progression plays while Live's transport runs;
/// off, it is silent and previews still work), Loop (the progression repeats; off, it plays once and stops
/// at its end), Thru (the notes on the MIDI input pass to the output; `ChordLabState.thru` over the
/// snapshot's `midiThru`, sending `setMidiThru`; hidden when the snapshot has no `midiThru`, an older
/// plug-in), Panic, and Auto voice-lead (`compile` picks each non-manual chord's inversion and octave for the
/// smoothest motion from the chord before; P2-D5). The toggles show the snapshot's `followHost`
/// / `progressionLoop` / `autoVoiceLead` and send `setFollowHost` / `setProgressionLoop` /
/// `setAutoVoiceLead`; the snapshot the plug-in answers with is what flips them, so they never drift from
/// the document (and stay put if a command is rejected). They are drawn by `AccentSwitchToggleStyle`
/// (SwitchToggleStyle.swift), not the system switch, whose on state greys out while the plug-in window is
/// not Live's key window. "+ Section" starts a section (P2-D6) on the
/// selected block, else the last one (`addSection {index, name: ""}`; the engine names it). "Presets"
/// opens `PresetsPopoverView` (plan P7); it is hidden when the snapshot has no `presets` (an older
/// plug-in build). "Feel" opens `FeelPopoverView` (rhythm, strum, humanise and the meter, plan D1, D2); a
/// dot beside its title says the progression plays with a feel (`FeelOptions.isDefault` is false). Like
/// Presets, it is hidden when the snapshot has no `feel` (an older plug-in build, which would refuse
/// `setFeel`).
///
/// Below the width the key bar needs to keep it beside the Key pickers (`ContentView.keyBarSwitchMinWidth`),
/// the Transpose with key switch (`TransposeWithKeyToggle`) is here instead, right after Auto voice-lead
/// (`showsTransposeSwitch`).
///
/// One row when it fits (the 900-pt default). With the Thru switch shown the row wraps to two lines between
/// about 856 and 879 pt wide and the palette viewport loses 32 pt there; from 880 pt it is one line again,
/// and the 568-pt minimum height still holds with one full palette row (worst viewport 106 pt in that band).
/// At the 700-pt minimum the playback controls (Follow Live,
/// Loop, Thru, Panic) and the editing ones (Auto voice-lead, Transpose with key, + Section, Presets, Feel)
/// take a row each.
struct TransportBarView: View {
  @EnvironmentObject var state: ChordLabState
  @State private var showingPresets = false
  @State private var showingFeel = false
  /// Whether the Transpose with key switch sits here, after Auto voice-lead (ContentView's width rule).
  var showsTransposeSwitch = false

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
    ViewThatFits(in: .horizontal) {
      HStack(spacing: 16) {
        playbackControls
        editingControls
        Spacer(minLength: 0)
      }
      VStack(alignment: .leading, spacing: 8) {
        HStack(spacing: 16) {
          playbackControls
          Spacer(minLength: 0)
        }
        HStack(spacing: 16) {
          editingControls
          Spacer(minLength: 0)
        }
      }
    }
    .font(.body)
  }

  private var playbackControls: some View {
    HStack(spacing: 16) {
      Toggle(isOn: followHost) { Text("Follow Live").foregroundStyle(.white) }
        .toggleStyle(.accentSwitch)
        .help("Play the progression while Live's transport runs")
        .accessibilityLabel("Follow Live")
      Toggle(isOn: loop) { Text("Loop").foregroundStyle(.white) }
        .toggleStyle(.accentSwitch)
        .help("Repeat the progression; off, it plays once and stops at its end")
        .accessibilityLabel("Loop the progression")
      if state.snapshot?.midiThru != nil {
        Toggle(isOn: state.thru) { Text("Thru").foregroundStyle(.white) }
          .toggleStyle(.accentSwitch)
          .help("Pass incoming MIDI notes to the output")
          .accessibilityLabel("Pass MIDI through")
      }
      Button("Panic") { state.panic() }
        .help("Silence every sounding note")
        .accessibilityLabel("Silence every sounding note")
    }
  }

  private var editingControls: some View {
    HStack(spacing: 16) {
      Toggle(isOn: autoVoiceLead) { Text("Auto voice-lead").foregroundStyle(.white) }
        .toggleStyle(.accentSwitch)
        .help("Choose each chord's inversion and octave for the smoothest motion from the chord before (manual chords keep theirs)")
        .accessibilityLabel("Auto voice-lead")
      if showsTransposeSwitch {
        TransposeWithKeyToggle()
      }
      Button("+ Section") { addSection() }
        .disabled(sectionIndex == nil)
        .help("Start a section on the selected chord (else the last one)")
        .accessibilityLabel("Add a section")
      if state.snapshot?.presets != nil {
        Button("Presets") { showingPresets.toggle() }
          .help("Load a well-known progression in the project key")
          .accessibilityLabel("Progression presets")
          .popover(isPresented: $showingPresets, arrowEdge: .bottom) {
            PresetsPopoverView { showingPresets = false }
          }
      }
      if state.snapshot?.feel != nil {
        feelButton
      }
    }
  }

  /// "Feel", with a dot while the progression plays with one; opens the feel popover.
  private var feelButton: some View {
    let isDefault = FeelOptions.isDefault(state.snapshot?.feel)
    return Button { showingFeel.toggle() } label: {
      HStack(spacing: 5) {
        Text("Feel")
        if !isDefault {
          Circle().fill(Color.accentColor).frame(width: 6, height: 6).accessibilityHidden(true)
        }
      }
    }
    .help("Rhythm, strum, humanise and the meter, for playback and the dragged-out clip")
    .accessibilityLabel("Feel")
    .accessibilityValue(isDefault ? "Block chords" : "On")
    // Beside the button, to its left: the button ends the row, and a popover below it would run past the
    // window's right edge at 700 pt (FeelPopoverView).
    .popover(isPresented: $showingFeel, arrowEdge: .leading) {
      FeelPopoverView()
    }
  }
}

struct TransportBarView_Previews: PreviewProvider {
  static var previews: some View {
    TransportBarView().environmentObject(ChordLabState(numParams: kNumParams))
      .padding().background(PaletteView.background).frame(width: 900)
  }
}
