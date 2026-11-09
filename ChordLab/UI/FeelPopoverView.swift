import SwiftUI

/// The feel and the meter (plan D1, D2), opened from the transport row's Feel button: a Rhythm menu (its
/// "Subdivided" entry named for the meter's subdivision) beside a Meter menu; then Strum and Speed segmented
/// controls, or, under the Arpeggio rhythm, a Pattern menu and the Rate and Range controls in their place;
/// Humanise; and a short note. Everything shown comes from the snapshot's `feel` and `meter` (the defaults,
/// block chords in 4/4, from an older plug-in build); each control sends exactly one command when its value
/// changes: `setFeel` with all seven fields (the snapshot's, with the one changed) or `setMeter`
/// (`FeelOptions`). The snapshot the plug-in answers with is what moves the controls, so they never drift
/// from the document.
///
/// Speed is disabled while no strum plays (strum off); the Strum and Speed rows give way to Pattern and Rate
/// under an arpeggio and keep their values for when the rhythm changes back.
///
/// Short on purpose (the same four rows under every rhythm and a line of notes, so the popover keeps its
/// height): it opens beside the Feel button, to its left (the button ends the transport row, so a popover
/// below it would run past the window's right edge at 700 pt), and stays inside the 700 x 568 minimum window.
struct FeelPopoverView: View {
  @EnvironmentObject var state: ChordLabState

  private var feel: FeelJSON { state.snapshot?.feel ?? FeelOptions.defaultFeel }
  private var meter: MeterOption { FeelOptions.currentMeter(state.snapshot?.meter) }

  /// A control's binding: reads `field` of the snapshot's feel; a new value sends one `setFeel`.
  private func feelBinding(_ field: KeyPath<FeelJSON, String>,
                           _ change: @escaping (FeelJSON, String) -> FeelJSON) -> Binding<String> {
    Binding(get: { feel[keyPath: field] },
            set: { value in
              guard value != feel[keyPath: field] else { return }
              state.send(command: FeelOptions.command(change(feel, value)))
            })
  }

  /// A binding for an arpeggio field: reads it through `FeelOptions` (the default when the snapshot lacks it).
  private func arpBinding(_ read: @escaping (FeelJSON?) -> String,
                          _ change: @escaping (FeelJSON, String) -> FeelJSON) -> Binding<String> {
    Binding(get: { read(feel) },
            set: { value in
              guard value != read(feel) else { return }
              state.send(command: FeelOptions.command(change(feel, value)))
            })
  }

  private var meterBinding: Binding<String> {
    Binding(get: { meter.id },
            set: { id in
              guard id != meter.id,
                    let option = FeelOptions.meterMenu(current: state.snapshot?.meter).first(where: { $0.id == id })
              else { return }
              state.send(command: FeelOptions.meterCommand(num: option.num, den: option.den))
            })
  }

  var body: some View {
    VStack(alignment: .leading, spacing: 6) {
      Grid(alignment: .leading, horizontalSpacing: 8, verticalSpacing: 6) {
        GridRow {
          label("Rhythm")
          HStack(spacing: 8) {
            Picker("Rhythm", selection: feelBinding(\.rhythm) { FeelOptions.changed($0, rhythm: $1) }) {
              ForEach(FeelOptions.rhythms(num: meter.num, den: meter.den)) { Text($0.title).tag($0.id) }
            }
            .pickerStyle(.menu)
            .labelsHidden()
            .fixedSize()
            .accessibilityLabel("Rhythm")
            Spacer(minLength: 12)
            Text("Meter").foregroundStyle(.secondary).fixedSize()
            Picker("Meter", selection: meterBinding) {
              ForEach(FeelOptions.meterMenu(current: state.snapshot?.meter)) { Text($0.title).tag($0.id) }
            }
            .pickerStyle(.menu)
            .labelsHidden()
            .fixedSize()
            .help("New chords last one bar of the meter; existing lengths stay")
            .accessibilityLabel("Meter")
          }
        }
        if FeelOptions.isArpeggio(feel) {
          GridRow {
            label("Pattern")
            Picker("Pattern", selection: arpBinding(FeelOptions.arpPattern) { FeelOptions.changed($0, arpPattern: $1) }) {
              ForEach(Array(FeelOptions.arpPatterns.enumerated()), id: \.offset) { index, group in
                if index > 0 { Divider() }
                ForEach(group) { Text($0.title).tag($0.id) }
              }
            }
            .pickerStyle(.menu)
            .labelsHidden()
            .fixedSize()
            .help("Thumb up is a pedal bass; Converge is outside-in; Chord plays every note each step; Random is fixed per chord")
            .accessibilityLabel("Pattern")
          }
          GridRow {
            label("Rate")
            HStack(spacing: 10) {
              segmented("Arpeggio rate", FeelOptions.arpRates(num: meter.num, den: meter.den),
                        arpBinding(FeelOptions.arpRate) { FeelOptions.changed($0, arpRate: $1) })
              Text("Range").foregroundStyle(.secondary).fixedSize()
              segmented("Arpeggio range", FeelOptions.arpRanges,
                        arpBinding(FeelOptions.arpRange) { FeelOptions.changed($0, arpRange: $1) })
            }
          }
        } else {
          GridRow {
            label("Strum")
            segmented("Strum", FeelOptions.strums, feelBinding(\.strum) { FeelOptions.changed($0, strum: $1) })
          }
          GridRow {
            label("Speed")
            segmented("Strum speed", FeelOptions.strumSpeeds,
                      feelBinding(\.strumSpeed) { FeelOptions.changed($0, strumSpeed: $1) })
              .disabled(!FeelOptions.speedApplies(feel))
          }
        }
        GridRow {
          label("Humanise")
          segmented("Humanise", FeelOptions.humanises, feelBinding(\.humanise) { FeelOptions.changed($0, humanise: $1) })
        }
      }
      note("Playback and the dragged-out clip use the same feel; previews stay block chords.")
    }
    .padding(.horizontal, 12)
    .padding(.vertical, 10)
    .fixedSize()
  }

  private func label(_ text: String) -> some View {
    Text(text).foregroundStyle(.secondary).fixedSize().gridColumnAlignment(.trailing)
  }

  private func segmented(_ name: String, _ options: [FeelOption], _ selection: Binding<String>) -> some View {
    Picker(name, selection: selection) {
      ForEach(options) { Text($0.title).tag($0.id) }
    }
    .pickerStyle(.segmented)
    .labelsHidden()
    .fixedSize()
    .accessibilityLabel(name)
  }

  private func note(_ text: String) -> some View {
    Text(text).font(.caption).foregroundStyle(.secondary)
  }
}
