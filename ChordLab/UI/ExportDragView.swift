import SwiftUI

/// "♫ 8 beats · drag to Live": drag this onto a MIDI track and the progression lands there as a .mid
/// clip. The plug-in writes the file (`kMsgTagExport` → `kMsgTagExportReady` with its path) when the
/// mouse arrives over the control and again when the drag starts, so the clip is the document as it is
/// at that moment; the round trip is synchronous on the main thread, so the path is already there when
/// `onDrag` asks for it, and "preparing…" only shows if it ever is not. With no chords there is nothing
/// to export: the control is dimmed and inert.
struct ExportDragView: View {
  @EnvironmentObject var state: ChordLabState
  @State private var preparing = false

  private var endBeat: Double { state.snapshot?.endBeat ?? 0 }
  private var enabled: Bool { !(state.snapshot?.events.isEmpty ?? true) }

  var body: some View {
    Text(label)
      .foregroundStyle(.white)
      .padding(.horizontal, 12).padding(.vertical, 8)
      .background(RoundedRectangle(cornerRadius: 8).fill(Color(red: 0.20, green: 0.25, blue: 0.55)))
      .opacity(enabled ? 1 : 0.4)
      .onHover { over in
        if over && enabled { state.requestExport() }
      }
      .onDrag {
        guard enabled else { return NSItemProvider() }
        state.requestExport()
        guard let path = state.exportPath else {
          preparing = true
          return NSItemProvider()
        }
        preparing = false
        return NSItemProvider(contentsOf: URL(fileURLWithPath: path)) ?? NSItemProvider()
      }
      .onChange(of: state.exportPath) { _, path in
        if path != nil { preparing = false }
      }
      .allowsHitTesting(enabled)
      .help(enabled ? "Drag onto a MIDI track in Live" : "Add chords to export")
      .accessibilityLabel(enabled ? "Drag the progression to Live as a MIDI clip" : "Nothing to export")
  }

  private var label: String {
    guard enabled else { return "♫ nothing to export" }
    if preparing { return "♫ preparing…" }
    let beats = endBeat == endBeat.rounded() ? String(Int(endBeat)) : String(format: "%.2f", endBeat)
    return "♫ \(beats) beats · drag to Live"
  }
}

struct ExportDragView_Previews: PreviewProvider {
  static var previews: some View {
    ExportDragView().environmentObject(ChordLabState(numParams: kNumParams))
      .padding().background(PaletteView.background)
  }
}
