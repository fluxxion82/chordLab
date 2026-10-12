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
/// Everything shown comes from the snapshot; every change is a command to the plugin.
struct KeyBarView: View {
  @EnvironmentObject var state: ChordLabState
  @AppStorage(LabelMode.storageKey) private var labelMode: LabelMode = .both

  private var projectKey: KeyJSON {
    state.snapshot?.projectKey ?? KeyJSON(tonic: TonicJSON(letter: "C", alter: 0), mode: "Ionian")
  }

  /// Picking a tonic or a mode sets the project key (spelled canonically for the mode) and brings the
  /// browsed key home, so the palette follows.
  private func setProjectKey(pc: Int, mode: String) {
    let tonic = KeyNames.canonicalTonic(pc: pc, mode: mode)
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
    HStack(spacing: 10) {
      Text("Key").foregroundStyle(.secondary)
      Picker("Tonic", selection: tonicSelection) {
        ForEach(0..<12, id: \.self) { pc in
          Text(ChordSymbol.glyphs(KeyNames.text(KeyNames.canonicalTonic(pc: pc, mode: projectKey.mode)))).tag(pc)
        }
      }
      .labelsHidden().fixedSize()
      Picker("Mode", selection: modeSelection) {
        ForEach(KeyNames.modes, id: \.self) { mode in
          Text(KeyNames.modeDisplayName(mode)).tag(mode)
        }
      }
      .labelsHidden().fixedSize()
      Text(KeyNames.displayName(projectKey)).foregroundStyle(.white).lineLimit(1)

      Spacer(minLength: 16)

      Text("Browse").foregroundStyle(.secondary)
      Button("<") { state.send(command: ["cmd": "browseKey", "steps": -1]) }
      Text(state.snapshot.map { KeyNames.displayName($0.browsedKey) } ?? "—")
        .foregroundStyle(.white).lineLimit(1).frame(minWidth: 140)
      Button(">") { state.send(command: ["cmd": "browseKey", "steps": 1]) }
      Button("Home") { state.send(command: ["cmd": "browseKey", "home": true]) }

      Spacer(minLength: 16)

      Picker("Labels", selection: $labelMode) {
        ForEach(LabelMode.allCases) { mode in Text(mode.title).tag(mode) }
      }
      .pickerStyle(.segmented).labelsHidden().fixedSize()
    }
    .font(.body)
  }
}

struct KeyBarView_Previews: PreviewProvider {
  static var previews: some View {
    KeyBarView().environmentObject(ChordLabState(numParams: kNumParams))
      .padding().background(Color(red: 0.04, green: 0.07, blue: 0.25)).frame(width: 900)
  }
}
