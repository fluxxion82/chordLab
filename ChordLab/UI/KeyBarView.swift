import SwiftUI

/// How palette cells are labelled; UI-only, remembered across sessions in UserDefaults.
enum LabelMode: String, CaseIterable, Identifiable {
  case names, numerals, both
  static let storageKey = "chordlab.labelMode"
  var id: String { rawValue }
  var title: String {
    switch self {
    case .names: return "Names"
    case .numerals: return "Numerals"
    case .both: return "Both"
    }
  }
}

/// Project key pickers (tonic, mode), browsed-key steppers with home, and the label-mode toggle.
/// Everything shown comes from the snapshot (the pickers' items are its `modes` and `tonics` tables);
/// every change is a command to the plugin. Without the tables (an older plug-in build) the pickers are
/// disabled placeholders and ContentView says so.
struct KeyBarView: View {
  @EnvironmentObject var state: ChordLabState
  @AppStorage(LabelMode.storageKey) private var labelMode: LabelMode = .both

  private var projectKey: KeyJSON {
    state.snapshot?.projectKey ?? KeyJSON(tonic: TonicJSON(letter: "C", alter: 0), mode: "Ionian")
  }

  private var modes: [String]? { state.snapshot?.modes }
  private var tonicRow: [TonicNameJSON]? { state.snapshot?.tonics(for: projectKey.mode) }

  /// Picking a tonic or a mode sets the project key, spelled as the snapshot's table spells that pitch
  /// class in that mode, and brings the browsed key home, so the palette follows.
  private func setProjectKey(pc: Int, mode: String) {
    guard let tonic = state.snapshot?.tonics(for: mode)?[pc] else { return }
    state.send(command: ["cmd": "setKey", "letter": tonic.letter, "alter": tonic.alter, "mode": mode])
    state.send(command: ["cmd": "browseKey", "home": true])
  }

  private var tonicSelection: Binding<Int> {
    Binding(get: { KeyNames.pitchClass(of: projectKey.tonic) },
            set: { setProjectKey(pc: $0, mode: projectKey.mode) })
  }

  private var modeSelection: Binding<String> {
    Binding(get: { projectKey.mode },
            set: { setProjectKey(pc: KeyNames.pitchClass(of: projectKey.tonic), mode: $0) })
  }

  var body: some View {
    // One row when it fits (the 900-pt default), two at the 700-pt minimum.
    ViewThatFits(in: .horizontal) {
      HStack(spacing: 16) {
        projectKeyControls
        browseControls
        Spacer(minLength: 0)
        labelModeControl
      }
      VStack(alignment: .leading, spacing: 8) {
        HStack(spacing: 16) {
          projectKeyControls
          Spacer(minLength: 0)
          browseControls
        }
        labelModeControl
      }
    }
    .font(.body)
  }

  private var projectKeyControls: some View {
    HStack(spacing: 8) {
      Text("Key").foregroundStyle(.secondary)
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
    }
  }

  private var browseControls: some View {
    HStack(spacing: 8) {
      Text("Browse").foregroundStyle(.secondary)
      Button("<") { state.send(command: ["cmd": "browseKey", "steps": -1]) }
        .accessibilityLabel("Browse a fifth down")
      Text(state.snapshot.map { KeyNames.displayName($0.browsedKey) } ?? "—")
        .foregroundStyle(.white).lineLimit(1).frame(minWidth: 130)
        .accessibilityLabel("Browsed key")
      Button(">") { state.send(command: ["cmd": "browseKey", "steps": 1]) }
        .accessibilityLabel("Browse a fifth up")
      Button("Home") { state.send(command: ["cmd": "browseKey", "home": true]) }
        .accessibilityLabel("Browse the project key")
    }
  }

  private var labelModeControl: some View {
    Picker("Labels", selection: $labelMode) {
      ForEach(LabelMode.allCases) { mode in Text(mode.title).tag(mode) }
    }
    .pickerStyle(.segmented).labelsHidden().fixedSize()
    .accessibilityLabel("Cell labels")
  }
}

struct KeyBarView_Previews: PreviewProvider {
  static var previews: some View {
    KeyBarView().environmentObject(ChordLabState(numParams: kNumParams))
      .padding().background(Color(red: 0.04, green: 0.07, blue: 0.25)).frame(width: 900)
  }
}
