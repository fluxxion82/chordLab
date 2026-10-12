import SwiftUI

/// A block's voicing controls (P2-D5), opened from the block's "v" badge or its context menu: an
/// inversion stepper (0…n−1 for an n-note chord), an octave stepper (−1/0/+1), the notes that sound
/// (`voicing`, the compiled result) and "Auto", which drops the chord's manual voicing (`resetVoicing`)
/// so that auto voice-leading, when on, chooses again. Every step is a command (`setInversion`,
/// `setOctave`); the values shown are the snapshot's, looked up by id so the popover follows the
/// snapshot the plug-in answers with. The sequence bar dismisses it when the chord is gone.
///
/// The stored inversion and octave are what sounds only for a manual chord, or while auto voice-leading
/// is off (`HarmonyText.storedVoicingSounds`); then the steppers are labelled with the effective values
/// and the bass. With auto voice-leading on, a non-manual chord may sound in any inversion and octave the
/// voice-leading chose, so the popover says "Automatic voicing", lists the notes, and labels the steppers
/// as the *manual* settings they would become (changing one makes the chord manual).
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
        let manual = event.manualVoicing ?? false
        let storedSounds = HarmonyText.storedVoicingSounds(manual: manual, autoVoiceLead: autoVoiceLead)
        HStack(alignment: .firstTextBaseline, spacing: 10) {
          Text(ChordSymbol.glyphs(event.symbol)).font(.title3.weight(.semibold))
          Text(Self.status(manual: manual, autoVoiceLead: autoVoiceLead))
            .font(.callout).foregroundStyle(.secondary)
        }
        Text(HarmonyText.voicingText(event.voicing)).font(.body.monospacedDigit())
        if !storedSounds {
          Text("Auto voice-leading chose these notes; changing a setting below makes the chord manual.")
            .font(.caption).foregroundStyle(.secondary)
            .fixedSize(horizontal: false, vertical: true)
        }
        let maxInversion = max(0, event.voicing.count - 1)
        Stepper(value: inversion, in: 0...maxInversion) {
          Text(storedSounds ? HarmonyText.inversionLabel(event.inversion ?? 0, bass: event.voicing.first)
                            : "Manual inversion \(event.inversion ?? 0)")
            .font(.body.monospacedDigit())
        }
        .accessibilityLabel(storedSounds ? "Inversion" : "Manual inversion")
        Stepper(value: octave, in: -1...1) {
          Text((storedSounds ? "" : "Manual ") + HarmonyText.octaveLabel(event.octave ?? 0))
            .font(.body.monospacedDigit())
        }
        .accessibilityLabel(storedSounds ? "Octave" : "Manual octave")
        Button("Auto") { state.send(command: ["cmd": "resetVoicing", "id": eventId]) }
          .disabled(!manual)
          .help("Forget the manual inversion and octave; auto voice-leading chooses when it is on")
          .accessibilityLabel("Reset to automatic voicing")
      } else {
        // At most one frame: the sequence bar clears `voicingPopoverId` when the chord leaves the snapshot.
        Text("Chord removed").foregroundStyle(.secondary)
      }
    }
    .frame(minWidth: 220, maxWidth: 280, alignment: .leading)
  }

  /// "Manual voicing" for a chord with its own inversion and octave; "Automatic voicing" while auto
  /// voice-leading chooses for it; "Default voicing" otherwise (the stored fields sound as they are).
  static func status(manual: Bool, autoVoiceLead: Bool) -> String {
    manual ? "Manual voicing" : autoVoiceLead ? "Automatic voicing" : "Default voicing"
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
