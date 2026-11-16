import SwiftUI

/// "♫ 8 beats · drag to Live": drag this onto a MIDI track and the OPEN lane lands there as a .mid clip (tracks
/// design D13): the chords with Chords open ("♫ 8 beats · drag to Live"), the track's own notes with a track open
/// ("♫ Bass · 8 beats"). The title is never cut (`.fixedSize()`): the counts line and an error beside it in the bottom
/// row wrap instead. The plug-in writes the file when the drag starts (`kMsgTagExportClip` →
/// `kMsgTagExportReady` with its path, `ChordLabState.dragExport`), so the clip is the document as it is at that
/// moment; the round trip is synchronous on the main thread, so the outcome is known when `onDrag` returns. Only an
/// outcome still waiting for its answer (`ExportOutcome.pending`, a host that does not answer inline) shows
/// "preparing…", until a file or an error arrives; a failed export (the shell's "nothing to export", a refused tempo)
/// keeps the title, and its error shows in the row. With nothing to export (no chords, or an open track without
/// sounding notes) the control is dimmed and inert, and its help says why.
struct ExportDragView: View {
  @EnvironmentObject var state: ChordLabState
  @State private var preparing = false

  var body: some View {
    let text = ClipExport.text(for: .openLane, in: state.snapshot, open: state.openLane)
    Text(ClipExport.dragOutTitle(in: state.snapshot, open: state.openLane, preparing: preparing))
      .foregroundStyle(.white)
      .lineLimit(1)
      .fixedSize()
      .padding(.horizontal, 12).padding(.vertical, 8)
      .background(RoundedRectangle(cornerRadius: 8).fill(Color(red: 0.20, green: 0.25, blue: 0.55)))
      .opacity(text.enabled ? 1 : 0.4)
      .exportDragSource(enabled: text.enabled) {
        let drag = state.dragExport(.openLane)
        preparing = drag.outcome.isPending
        return drag.provider
      }
      // A late answer to a pending export: a file, or any error.
      .onChange(of: state.exportPath) { _, path in
        if path != nil { preparing = false }
      }
      .onChange(of: state.errorSerial) { _, _ in preparing = false }
      .help(text.help)
      .accessibilityLabel(text.label)
  }
}

extension View {
  /// A drag-out source (tracks design D13): `.onDrag(provide)` while `enabled`; while not, no drag at all (the view
  /// stays hit-testable, so its help still says why), so an inert grip never starts a drag session.
  @ViewBuilder func exportDragSource(enabled: Bool, _ provide: @escaping () -> NSItemProvider) -> some View {
    if enabled {
      onDrag(provide)
    } else {
      self
    }
  }
}

struct ExportDragView_Previews: PreviewProvider {
  static var previews: some View {
    ExportDragView().environmentObject(ChordLabState(numParams: kNumParams))
      .padding().background(PaletteView.background)
  }
}
