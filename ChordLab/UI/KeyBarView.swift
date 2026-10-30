import SwiftUI

/// Project key pickers (tonic, mode) with the Transpose with key switch, browsed-key steppers with the
/// browsed-mode picker and home, undo/redo (the buttons; ⌘Z / ⇧⌘Z arrive through the key routes,
/// `EditorKeyRouter`), the label-mode toggle and the chord-relations chooser (plan R8). Everything shown
/// comes from the snapshot (the pickers' items are its `modes` and `tonics` tables); every change is a
/// command to the plugin. Without the tables (an older plug-in build) the project-key pickers are disabled
/// placeholders, the browsed-mode picker is absent, and ContentView says so.
struct KeyBarView: View {
  @EnvironmentObject var state: ChordLabState
  @AppStorage(LabelMode.storageKey) private var labelMode = LabelMode.defaultMode

  private var projectKey: KeyJSON {
    state.snapshot?.projectKey ?? KeyJSON(tonic: TonicJSON(letter: "C", alter: 0), mode: "Ionian")
  }

  private var modes: [String]? { state.snapshot?.modes }
  private var tonicRow: [TonicNameJSON]? { state.snapshot?.tonics(for: projectKey.mode) }

  /// Picking a tonic or a mode goes through `ChordLabState.changeProjectKey`, which also decides the
  /// transpose hint or the moved note shown under the bar.
  private var tonicSelection: Binding<Int> {
    Binding(get: { KeyNames.pitchClass(of: projectKey.tonic) },
            set: { state.changeProjectKey(pc: $0, mode: projectKey.mode) })
  }

  private var modeSelection: Binding<String> {
    Binding(get: { projectKey.mode },
            set: { state.changeProjectKey(pc: KeyNames.pitchClass(of: projectKey.tonic), mode: $0) })
  }

  /// The snapshot's `transposeWithKey`; the switch sends `setTransposeWithKey` and the next snapshot moves it.
  private var transposeWithKey: Binding<Bool> {
    Binding(get: { state.snapshot?.transposeWithKey ?? false },
            set: { state.send(command: ["cmd": "setTransposeWithKey", "on": $0]) })
  }

  /// The browsed key's mode; choosing one sends `browseKey {"mode"}`, which keeps the browsed tonic and
  /// switches to that parallel mode. The next snapshot moves the picker.
  private var browsedModeSelection: Binding<String> {
    Binding(get: { state.snapshot?.browsedKey.mode ?? "Ionian" },
            set: { state.send(command: ["cmd": "browseKey", "mode": $0]) })
  }

  /// The snapshot's relation mode (a `RelationMode` raw value); choosing one sends `setRelationMode` and the
  /// next snapshot moves the picker. Nothing here stores a mode.
  private var relationSelection: Binding<String> {
    Binding(get: { state.snapshot?.relationMode ?? RelationMode.blend.rawValue },
            set: { state.send(command: ["cmd": "setRelationMode", "mode": $0]) })
  }

  var body: some View {
    // The first candidate that fits: (1) one row; (2) the key pickers and the switch with browse, then
    // history, labels and relations (the 900-pt default); (3) at the 700-pt minimum, the key pickers and the
    // switch alone, then browse and history, then labels and relations. The switch stays with the pickers.
    ViewThatFits(in: .horizontal) {
      HStack(spacing: 16) {
        projectKeyControls
        browseControls
        Spacer(minLength: 0)
        historyControls
        labelModeControl
        relationControls
      }
      VStack(alignment: .leading, spacing: 8) {
        HStack(spacing: 16) {
          projectKeyControls
          Spacer(minLength: 0)
          browseControls
        }
        HStack(spacing: 16) {
          historyControls
          labelModeControl
          relationControls
        }
      }
      VStack(alignment: .leading, spacing: 8) {
        HStack(spacing: 16) {
          projectKeyControls
          Spacer(minLength: 0)
        }
        HStack(spacing: 16) {
          browseControls
          Spacer(minLength: 0)
          historyControls
        }
        HStack(spacing: 16) {
          labelModeControl
          relationControls
        }
      }
    }
    .font(.body)
  }

  private var projectKeyControls: some View {
    HStack(spacing: 8) {
      Text("Key").foregroundStyle(.secondary).fixedSize()
      Picker("Tonic", selection: tonicSelection) {
        if let row = tonicRow {
          ForEach(0..<12, id: \.self) { pc in
            Text(ChordSymbol.glyphs(row[pc].text)).tag(pc)
          }
        } else {
          Text("—").tag(KeyNames.pitchClass(of: projectKey.tonic))   // keeps the selection valid
        }
      }
      .labelsHidden().fixedSize()
      .disabled(tonicRow == nil)
      .accessibilityLabel("Project key tonic")
      Picker("Mode", selection: modeSelection) {
        if let modes = modes {
          ForEach(modes, id: \.self) { mode in
            Text(KeyNames.modeDisplayName(mode)).tag(mode)
          }
        } else {
          Text("—").tag(projectKey.mode)
        }
      }
      .labelsHidden().fixedSize()
      .disabled(modes == nil)
      .accessibilityLabel("Project key mode")
      Toggle(isOn: transposeWithKey) { Text("Transpose with key").foregroundStyle(.white) }
        .toggleStyle(.switch)
        .fixedSize()
        .padding(.leading, 8)
        .help(TransposeWithKeyHelp.text)
        .accessibilityLabel("Transpose with key")
    }
  }

  /// `<` tonic mode `>` Home. With the snapshot's `modes` the browsed key reads as its tonic beside a mode
  /// picker (the parallel modes); without them (an older plug-in build) as one name, as before.
  private var browseControls: some View {
    HStack(spacing: 8) {
      Text("Browse").foregroundStyle(.secondary)
      Button("<") { state.send(command: ["cmd": "browseKey", "steps": -1]) }
        .accessibilityLabel("Browse a fifth down")
      if let modes = modes, let browsed = state.snapshot?.browsedKey {
        Text(KeyNames.tonicName(browsed.tonic))
          .foregroundStyle(.white).lineLimit(1).frame(minWidth: 26)
          .accessibilityLabel("Browsed key")
          .accessibilityValue(KeyNames.displayName(browsed))
        Picker("Browsed mode", selection: browsedModeSelection) {
          ForEach(modes, id: \.self) { mode in
            Text(KeyNames.modeDisplayName(mode)).tag(mode)
          }
        }
        .labelsHidden().fixedSize()
        .accessibilityLabel("Browsed mode")
      } else {
        Text(state.snapshot.map { KeyNames.displayName($0.browsedKey) } ?? "—")
          .foregroundStyle(.white).lineLimit(1).frame(minWidth: 130)
          .accessibilityLabel("Browsed key")
      }
      Button(">") { state.send(command: ["cmd": "browseKey", "steps": 1]) }
        .accessibilityLabel("Browse a fifth up")
      Button("Home") { state.send(command: ["cmd": "browseKey", "home": true]) }
        .accessibilityLabel("Browse the project key")
    }
  }

  private var historyControls: some View {
    HStack(spacing: 4) {
      Button { state.send(command: ["cmd": "undo"]) } label: { Image(systemName: "arrow.uturn.backward") }
        .help("Undo (⌘Z)")
        .accessibilityLabel("Undo")
      Button { state.send(command: ["cmd": "redo"]) } label: { Image(systemName: "arrow.uturn.forward") }
        .help("Redo (⇧⌘Z)")
        .accessibilityLabel("Redo")
    }
  }

  private var labelModeControl: some View {
    Picker("Labels", selection: $labelMode) {
      ForEach(LabelMode.allCases) { mode in Text(mode.title).tag(mode) }
    }
    .pickerStyle(.segmented).labelsHidden().fixedSize()
    .accessibilityLabel("Cell labels")
  }

  /// Which relation sizes the palette cells, and the chord they are measured against ("to G7"). Absent
  /// (no space taken) when the snapshot has no `relationMode`, i.e. an older plug-in build. The caption
  /// keeps its width while there is no reference chord, so the row does not reflow when one appears.
  @ViewBuilder private var relationControls: some View {
    if state.snapshot?.relationMode != nil {
      HStack(spacing: 8) {
        Text("Relations").foregroundStyle(.secondary)
        Picker("Relations", selection: relationSelection) {
          ForEach(RelationMode.allCases) { mode in Text(mode.title).tag(mode.rawValue) }
        }
        .labelsHidden().fixedSize()
        .accessibilityLabel("Chord relations")
        Text(RelationStyle.caption(reference: state.snapshot?.relationReference) ?? "")
          .foregroundStyle(.secondary).lineLimit(1)
          .frame(minWidth: 64, alignment: .leading)
          .accessibilityHidden(state.snapshot?.relationReference == nil)
      }
      .help("Sizes each palette chord by its relation to the selected (else the last) chord. "
            + "Off: no sizing. Common tones: the notes the two chords share. "
            + "Voice leading: how little the voices have to move. "
            + "Function: how naturally its harmonic function follows. "
            + "Blend: the suggestion score, as in the suggestions strip.")
    }
  }
}

struct KeyBarView_Previews: PreviewProvider {
  static var previews: some View {
    KeyBarView().environmentObject(ChordLabState(numParams: kNumParams))
      .padding().background(Color(red: 0.04, green: 0.07, blue: 0.25)).frame(width: 900)
  }
}

/// The tooltip for the Transpose with key switch, shared by every view that shows the switch.
enum TransposeWithKeyHelp {
  static let text = "When on, changing the key's tonic transposes every chord in the sequence by the same "
    + "interval and keeps each chord's type: C – Am – F – G in C becomes D – Bm – G – A in D. With Auto "
    + "voice-lead off every chord moves by exactly that interval (one already an octave up or down can land "
    + "an octave off); with it on, the first chord and chords you voiced yourself move exactly and the others "
    + "are re-voiced as usual, so they may change inversion. Changing only the mode relabels the chords. To hear the same chords as the relative "
    + "minor, turn this off first."
}
