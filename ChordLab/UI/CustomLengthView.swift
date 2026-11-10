import SwiftUI

/// The Length menu's Custom… popover (strip editing design D8): a beats field, 0.25–64 in quarter beats. Return
/// commits the value as `LengthOptions.custom` reads it (snapped and clamped); text that is no number keeps the
/// popover open; Escape closes it without a change. While it shows, the editor's key routes stand down
/// (`ChordLabState.editingText`), so typing a length never removes or appends chords.
struct CustomLengthView: View {
  @EnvironmentObject var state: ChordLabState
  /// The value the field starts with (the selection's first length, or the new-chord length).
  let initial: Double
  let commit: (Double) -> Void
  @State private var text = ""
  @FocusState private var focused: Bool
  @Environment(\.dismiss) private var dismiss

  var body: some View {
    VStack(alignment: .leading, spacing: 6) {
      HStack(spacing: 6) {
        TextField("Beats", text: $text)
          .frame(width: 64)
          .focused($focused)
          .onSubmit { if let beats = LengthOptions.custom(text) { commit(beats) } }
          .onExitCommand { dismiss() }
          .accessibilityLabel("Length in beats")
        Text("beats").foregroundStyle(.secondary)
      }
      Text("0.25 to 64, in quarter beats").font(.caption).foregroundStyle(.secondary)
    }
    .padding(12)
    .onAppear {
      text = LengthOptions.beatsText(initial)
      focused = true
      state.editingText = true
    }
    .onDisappear { state.editingText = false }
  }
}
