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
/// `setFeel`). Record (before Presets, `ChordLabState.recording`, D9) turns palette presses into blocks as long
/// as the cell was held. Length (after Feel, D8) is a menu titled by `LengthOptions.title` (the selection's common
/// length, "Mixed", or "New: …" with nothing selected), with a dot beside it while new chords have a chosen
/// length; a pick sends `setLengths` for the selection, or `setNewChordLength` with none; Custom… opens
/// `CustomLengthView`. Both are hidden for an older plug-in (no `selectedEventIds` in the snapshot).
///
/// Below the width the key bar needs to keep it beside the Key pickers (`ContentView.keyBarSwitchMinWidth`),
/// the Transpose with key switch (`TransposeWithKeyToggle`) is here instead, right after Auto voice-lead
/// (`showsTransposeSwitch`).
///
/// Three layouts, the first that fits (`ViewThatFits`): one row (the 900-pt default, measured in Task 8 of the
/// strip-editing plan); the playback controls (Follow Live, Loop, Thru, Panic) and the editing ones (Auto
/// voice-lead, Transpose with key, + Section, Record, Presets, Feel, Length) a row each; or the editing ones
/// split once more, Auto voice-lead, Transpose with key and + Section on one row and Record, Presets, Feel and
/// Length on the next, so the 700-pt minimum never clips a control. With the Length title at its widest the
/// editing controls alone need a window about 795 pt wide and all of them with the playback controls about 975,
/// so the 700-pt minimum gets the third layout and the 900-pt default the second (measured by the render
/// harness). The third row costs 32 pt; the minimum height (`PLUG_MIN_HEIGHT`, 632 pt since the lane tab row) pays
/// for it, keeping one full palette row (76 pt or more) in view in every state.
struct TransportBarView: View {
  @EnvironmentObject var state: ChordLabState
  @State private var showingPresets = false
  @State private var showingFeel = false
  @State private var showingCustomLength = false
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
      VStack(alignment: .leading, spacing: 8) {
        HStack(spacing: 16) {
          playbackControls
          Spacer(minLength: 0)
        }
        HStack(spacing: 16) {
          voicingControls
          Spacer(minLength: 0)
        }
        HStack(spacing: 16) {
          stripControls
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
      voicingControls
      stripControls
    }
  }

  /// Auto voice-lead, the Transpose with key switch when it is here, and + Section.
  private var voicingControls: some View {
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
    }
  }

  /// Record, Presets, Feel and Length; each hidden for an older plug-in that lacks what it needs.
  private var stripControls: some View {
    HStack(spacing: 16) {
      if state.snapshot?.selectedEventIds != nil {
        Toggle(isOn: $state.recording) { Text("Record").foregroundStyle(.white) }
          .toggleStyle(.accentSwitch)
          .help("Pressing a palette chord adds it after the selected one, as long as you hold it (a press under 0.2 s: the new-chord length)")
          .accessibilityLabel("Record palette presses")
      }
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
      if state.snapshot?.selectedEventIds != nil {
        lengthMenu
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

  /// "Length" (D8): a menu titled by `LengthOptions.title`, the note values, the bars of the meter, "Default (one
  /// bar)" with nothing selected, and Custom…; the entry that matches is checked. A dot beside it while new chords
  /// have a chosen length, like Feel's. The Custom popover opens to its left, like Feel's: Length ends the row.
  private var lengthMenu: some View {
    let snapshot = state.snapshot
    let selection = snapshot?.selection ?? []
    let lengths = LengthOptions.lengths(of: selection, in: snapshot?.events ?? [])
    let newChordBeats = snapshot?.newChordBeats
    let meter = snapshot?.meter
    let title = LengthOptions.title(selectionLengths: lengths, newChordBeats: newChordBeats, meter: meter)
    return HStack(spacing: 5) {
      Menu {
        ForEach(Array(LengthOptions.groups(meter: meter).enumerated()), id: \.offset) { index, group in
          if index > 0 { Divider() }
          ForEach(group) { entry in
            let checked = LengthOptions.isChecked(entry, selectionLengths: lengths, newChordBeats: newChordBeats)
            Button(entry.title + (checked ? "  ✓" : "")) {
              state.send(command: LengthOptions.command(beats: entry.beats, selection: selection))
            }
          }
        }
        if selection.isEmpty {
          Divider()
          Button("Default (one bar)" + (newChordBeats == nil ? "  ✓" : "")) {
            state.send(command: LengthOptions.command(beats: nil, selection: []))
          }
        }
        Divider()
        Button("Custom…") { showingCustomLength = true }
      } label: {
        Text(title)
      }
      .fixedSize()
      if newChordBeats != nil {
        Circle().fill(Color.accentColor).frame(width: 6, height: 6).accessibilityHidden(true)
      }
    }
    .help(selection.isEmpty ? "The length of the chords you add from now on" : "Set the length of the selected chords")
    .accessibilityElement(children: .contain)
    .accessibilityLabel("Length")
    .accessibilityValue(title)
    .popover(isPresented: $showingCustomLength, arrowEdge: .leading) {
      CustomLengthView(initial: lengths.first ?? LengthOptions.newChordBeats(newChordBeats, meter: meter)) { beats in
        showingCustomLength = false
        state.send(command: LengthOptions.command(beats: beats, selection: selection))
      }
      .environmentObject(state)
    }
  }
}

struct TransportBarView_Previews: PreviewProvider {
  static var previews: some View {
    TransportBarView().environmentObject(ChordLabState(numParams: kNumParams))
      .padding().background(PaletteView.background).frame(width: 900)
  }
}
