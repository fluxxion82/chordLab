import SwiftUI

/// The Heard group (design D15) at the trailing end of the palette switch row: what the plug-in hears on its
/// MIDI input, from `state.heard` (`kMsgTagHeard`). Nothing at all until the listener is active (its first
/// event of the session; × makes it inactive again). Then, in one line:
/// `Heard · A Minor · 87% [Use] · Am7 · vi7 [Add] ×`. Before a key qualifies the key part reads
/// `listening · 5 notes` and has no Use; with no chord identified the chord part is the held notes
/// (`C, E, G♯`, nothing when none is held) and has no Add; Add is disabled once the heard chord is released.
/// The texts are `HeardText`'s and truncate at the tail when the row is short of width; the buttons never do.
///
/// Use is the Key pickers' change (`ChordLabState.useHeardKey`), Add appends the chord as played
/// (`addHeardChord`), × forgets everything (`clearHeard`).
struct HeardView: View {
  @EnvironmentObject var state: ChordLabState

  var body: some View {
    if let heard = state.heard, heard.active {
      HStack(spacing: 6) {
        Text("Heard ·").foregroundStyle(.secondary).fixedSize()
          .accessibilityHidden(true)
        keyPart(heard)
        chordPart(heard)
        Button { state.clearHeard() } label: {
          // The glyph is small; the frame gives the click a target the height of the row's buttons.
          Image(systemName: "xmark").frame(width: 16, height: 18).contentShape(Rectangle())
        }
        .buttonStyle(.borderless)
        .fixedSize()
        .help("Forget what was heard")
        .accessibilityLabel("Forget what was heard")
      }
      .font(.callout)
      .lineLimit(1)
      .controlSize(.small)
      .accessibilityElement(children: .contain)
      .accessibilityLabel("Heard")
    }
  }

  @ViewBuilder private func keyPart(_ heard: HeardJSON) -> some View {
    if let line = HeardText.keyLine(heard), let key = heard.key {
      Text(line).foregroundStyle(.white).truncationMode(.tail)
      let name = KeyNames.displayName(KeyJSON(tonic: key.tonic, mode: key.mode))
      Button("Use") { state.useHeardKey() }
        .fixedSize()
        .help("Make \(name) the project key")
        .accessibilityLabel("Make \(name) the project key")
    } else {
      Text(HeardText.listeningLine(heard)).foregroundStyle(.white).truncationMode(.tail)
    }
  }

  @ViewBuilder private func chordPart(_ heard: HeardJSON) -> some View {
    if let line = HeardText.chordLine(heard) {
      Text("·").foregroundStyle(.secondary).fixedSize().accessibilityHidden(true)
      Text(line).foregroundStyle(.white).truncationMode(.tail)
      Button("Add") { state.addHeardChord() }
        .fixedSize()
        .disabled(!heard.chordHeld)
        .help(heard.chordHeld ? "Add the heard chord, as played, to the end of the progression"
                              : "Hold the chord to add it")
        .accessibilityLabel("Add the heard chord")
    } else {
      let held = HeardText.heldLine(heard)
      if !held.isEmpty {
        Text("·").foregroundStyle(.secondary).fixedSize().accessibilityHidden(true)
        Text(held).foregroundStyle(.white).truncationMode(.tail)
      }
    }
  }
}
