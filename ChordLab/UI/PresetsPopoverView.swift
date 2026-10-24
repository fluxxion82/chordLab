import SwiftUI

/// The progression presets (plan P7), opened from the transport row's Presets button: a segmented
/// *Add to end / Replace progression* choice (remembered per user), then the snapshot's `presets` grouped
/// by genre in table order. Each row is a button: the name, the numerals (read against the project key
/// once loaded; the engine's spelling, wrapping to two lines), a tonality badge, and, when the preset does
/// not fit the project key's mode, half opacity and a footnote ("Written for a minor key"); a greyed preset
/// still loads.
///
/// Clicking a row sends `loadPreset {id, mode}` and remembers the snapshot and error it saw
/// (`PresetLoadWatch`): the next snapshot means it loaded, and the popover closes; an error instead means
/// the engine refused it, and the popover stays open while the status line shows why. Escape closes it.
struct PresetsPopoverView: View {
  @EnvironmentObject var state: ChordLabState
  @AppStorage(PresetLoadMode.storageKey) private var mode = PresetLoadMode.defaultMode
  /// Closes the popover (the transport row's presentation flag).
  let dismiss: () -> Void
  /// Set from a click until its answer arrives.
  @State private var watch: PresetLoadWatch<Snapshot>?

  static let width: CGFloat = 360
  static let maxListHeight: CGFloat = 440

  private var groups: [PresetGroup] { PresetStyle.groups(state.snapshot?.presets ?? []) }

  var body: some View {
    VStack(alignment: .leading, spacing: 10) {
      Picker("Load as", selection: $mode) {
        ForEach(PresetLoadMode.allCases) { Text($0.title).tag($0) }
      }
      .pickerStyle(.segmented)
      .labelsHidden()
      .accessibilityLabel("How a preset is loaded")
      if let key = state.snapshot?.projectKey {
        Text("Numerals play in \(KeyNames.displayName(key)), the project key.")
          .font(.caption).foregroundStyle(.secondary)
      }
      ScrollView {
        VStack(alignment: .leading, spacing: 12) {
          ForEach(groups) { group in
            VStack(alignment: .leading, spacing: 2) {
              Text(group.genre)
                .font(.subheadline.weight(.semibold)).foregroundStyle(.secondary)
                .padding(.horizontal, 6)
                .accessibilityAddTraits(.isHeader)
              ForEach(group.presets) { preset in
                PresetRowView(preset: preset) { load(preset) }
              }
            }
          }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
      }
      .frame(maxHeight: Self.maxListHeight)
    }
    .padding(12)
    .frame(width: Self.width)
    .onChange(of: state.snapshot) { _, _ in checkAnswer() }
    .onChange(of: state.lastError) { _, _ in checkAnswer() }
    .onExitCommand(perform: dismiss)
  }

  private func load(_ preset: PresetJSON) {
    watch = PresetLoadWatch(snapshot: state.snapshot, error: state.lastError)
    state.send(command: PresetStyle.loadCommand(id: preset.id, mode: mode))
    checkAnswer()   // the plug-in usually answers inside `send`, before any onChange
  }

  private func checkAnswer() {
    guard let watch = watch else { return }
    switch watch.outcome(snapshot: state.snapshot, error: state.lastError) {
    case .pending: break
    case .loaded:
      self.watch = nil
      dismiss()
    case .refused:
      self.watch = nil
    }
  }
}

/// One preset row: name, numerals and the tonality badge, half opacity when the preset does not fit, then
/// the footnote at full strength so it stays legible.
private struct PresetRowView: View {
  let preset: PresetJSON
  let action: () -> Void
  @State private var hovering = false

  var body: some View {
    Button(action: action) {
      VStack(alignment: .leading, spacing: 2) {
        HStack(alignment: .firstTextBaseline, spacing: 8) {
          VStack(alignment: .leading, spacing: 1) {
            Text(preset.name).font(.body).foregroundStyle(.primary)
            Text(PresetStyle.numerals(preset))
              .font(.callout).foregroundStyle(.secondary)
              .lineLimit(2)
              .fixedSize(horizontal: false, vertical: true)
          }
          Spacer(minLength: 0)
          Text(PresetStyle.badge(tonality: preset.tonality))
            .font(.caption).foregroundStyle(.secondary)
            .padding(.horizontal, 6).padding(.vertical, 1)
            .background(Capsule().fill(Color.primary.opacity(0.1)))
        }
        .opacity(preset.fits ? 1 : 0.5)
        if let footnote = PresetStyle.footnote(fits: preset.fits, tonality: preset.tonality) {
          Text(footnote).font(.caption).foregroundStyle(.secondary)
        }
      }
      .padding(.vertical, 4).padding(.horizontal, 6)
      .frame(maxWidth: .infinity, alignment: .leading)
      .background(RoundedRectangle(cornerRadius: 5).fill(Color.primary.opacity(hovering ? 0.08 : 0)))
      .contentShape(Rectangle())
    }
    .buttonStyle(.plain)
    .onHover { hovering = $0 }
    .accessibilityLabel(PresetStyle.accessibilityLabel(preset))
  }
}
