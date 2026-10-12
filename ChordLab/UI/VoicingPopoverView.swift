import SwiftUI

/// A block's voicing controls (P2-D5), opened from the block's "v" badge or its context menu: an
/// inversion stepper (0…n−1 for an n-note chord, labelled with the bass note that sounds, `voicing[0]`),
/// an octave stepper (−1/0/+1) and "Auto", which drops the chord's manual voicing (`resetVoicing`) so
/// that auto voice-leading, when on, chooses again. Every step is a command (`setInversion`,
/// `setOctave`); the values shown are the snapshot's, looked up by id so the popover follows the
/// snapshot the plug-in answers with (and closes itself if the chord is gone).
struct VoicingPopoverView: View {
  @EnvironmentObject var state: ChordLabState
  let eventId: Int

  private var event: EventJSON? { state.snapshot?.events.first { $0.id == eventId } }
  private var autoVoiceLead: Bool { state.snapshot?.autoVoiceLead ?? false }

  private var inversion: Binding<Int> {
    Binding(get: { event?.inversion ?? 0 },
            set: { state.send(command: ["cmd": "setInversion", "id": eventId, "inversion": $0]) })
  }

  private var octave: Binding<Int> {
    Binding(get: { event?.octave ?? 0 },
            set: { state.send(command: ["cmd": "setOctave", "id": eventId, "octave": $0]) })
  }

  var body: some View {
    VStack(alignment: .leading, spacing: 10) {
      if let event = event {
        HStack(alignment: .firstTextBaseline, spacing: 10) {
          Text(ChordSymbol.glyphs(event.symbol)).font(.title3.weight(.semibold))
          Text(Self.status(manual: event.manualVoicing ?? false, autoVoiceLead: autoVoiceLead))
            .font(.callout).foregroundStyle(.secondary)
        }
        let maxInversion = max(0, event.voicing.count - 1)
        Stepper(value: inversion, in: 0...maxInversion) {
          Text(Self.inversionLabel(event.inversion ?? 0, bass: event.voicing.first))
            .font(.body.monospacedDigit())
        }
        .accessibilityLabel("Inversion")
        Stepper(value: octave, in: -1...1) {
          Text(Self.octaveLabel(event.octave ?? 0)).font(.body.monospacedDigit())
        }
        .accessibilityLabel("Octave")
        Text(HarmonyText.voicingText(event.voicing)).font(.callout.monospacedDigit()).foregroundStyle(.secondary)
        Button("Auto") { state.send(command: ["cmd": "resetVoicing", "id": eventId]) }
          .disabled(!(event.manualVoicing ?? false))
          .help("Forget the manual inversion and octave; auto voice-leading chooses when it is on")
          .accessibilityLabel("Reset to automatic voicing")
      } else {
        Text("Chord removed").foregroundStyle(.secondary)
      }
    }
    .frame(minWidth: 220, alignment: .leading)
  }

  /// "Manual voicing" for a chord with its own inversion and octave; otherwise what decides its voicing.
  static func status(manual: Bool, autoVoiceLead: Bool) -> String {
    manual ? "Manual voicing" : autoVoiceLead ? "Auto voice-led" : "Default voicing"
  }

  /// "Inversion 1 · bass E4" (the bass is the voicing's lowest note, what sounds).
  static func inversionLabel(_ inversion: Int, bass: Int?) -> String {
    "Inversion \(inversion)" + (bass.map { " · bass " + ChordSymbol.noteName(midi: $0) } ?? "")
  }

  /// "Octave +1", "Octave 0", "Octave −1".
  static func octaveLabel(_ octave: Int) -> String {
    "Octave " + (octave > 0 ? "+\(octave)" : octave < 0 ? "−\(-octave)" : "0")
  }

  /// Menu wording of an inversion: "Root position", "1st inversion", "2nd inversion", "3rd inversion".
  static func inversionName(_ inversion: Int) -> String {
    switch inversion {
    case 0: return "Root position"
    case 1: return "1st inversion"
    case 2: return "2nd inversion"
    case 3: return "3rd inversion"
    default: return "\(inversion)th inversion"
    }
  }
}
