import SwiftUI

/// The sections (P2-D6) as a row of coloured spans above the blocks, in the blocks' own geometry: each
/// span runs from its first block's left edge to the next section's first block (`SequenceGeometry
/// .sectionSpans`), so a span sits exactly over its chords. Everything shown is the snapshot's `sections`;
/// every change is a command:
/// - double-click a span → rename it in place (`renameSection`, the name kept within the engine's 64
///   UTF-8 bytes); Return commits, Escape cancels, so does losing focus (commit);
/// - right-click → Rename…, the Colour submenu (`setSectionColour`) and Remove section (`removeSection`);
/// - drag a span's left edge → `moveSectionStart` to the nearest block boundary when the drag ends (the
///   edge follows the mouse meanwhile; the first section is pinned to the start and has no edge handle);
/// - drag the "♫" at a span's right end onto a MIDI track → that section alone as a .mid clip
///   (`kMsgTagExportSection` → `kMsgTagExportReady`, as `ExportDragView` does for the whole progression).
/// The sequence bar draws the matching boundary lines through the block row.
struct SectionsHeaderView: View {
  @EnvironmentObject var state: ChordLabState
  let sections: [SectionJSON]
  let layout: SequenceGeometry.Layout
  let width: CGFloat

  static let height: CGFloat = 20
  static let edgeWidth: CGFloat = 8
  static let cornerRadius: CGFloat = 4
  /// The header's own coordinate space: edge drags are measured in it, so the translation stays true
  /// while the span under the mouse is redrawn at its dragged position.
  static let coordinateSpace = "chordlab.sectionsHeader"

  /// A left-edge drag in progress: section `id`'s start is drawn `dx` points from its place.
  struct EdgeDrag: Equatable {
    let id: Int
    var dx: CGFloat
  }

  @State private var edgeDrag: EdgeDrag?
  @State private var renamingId: Int?
  @State private var draft = ""
  @FocusState private var renameFocused: Bool

  var body: some View {
    let spans = SequenceGeometry.sectionSpans(starts: sections.map(\.firstEventIndex), in: layout)
    ZStack(alignment: .topLeading) {
      Color.clear.frame(width: width, height: Self.height)
      ForEach(Array(sections.enumerated()), id: \.element.id) { i, section in
        let frame = displayedSpan(section, spans[i])
        if frame.width > 0 {
          span(section, frame: frame)
            .frame(width: frame.width, height: Self.height)
            .offset(x: frame.x)
        }
      }
    }
    .frame(width: width, height: Self.height, alignment: .topLeading)
    .coordinateSpace(name: Self.coordinateSpace)
    .accessibilityElement(children: .contain)
    .accessibilityLabel("Sections")
  }

  /// The span as drawn: the snapshot's, except for a left edge being dragged, which follows the mouse
  /// (its right end stays put; it never shrinks past a point).
  private func displayedSpan(_ section: SectionJSON, _ span: SequenceGeometry.SpanFrame) -> SequenceGeometry.SpanFrame {
    guard let drag = edgeDrag, drag.id == section.id else { return span }
    let end = span.x + span.width
    let x = min(end - 1, max(0, span.x + drag.dx))
    return SequenceGeometry.SpanFrame(x: x, width: end - x)
  }

  // MARK: - One span

  private func span(_ section: SectionJSON, frame: SequenceGeometry.SpanFrame) -> some View {
    let color = SectionStyle.color(section.colourIndex)
    let dragging = edgeDrag?.id == section.id
    let first = sections.first?.id == section.id
    return ZStack(alignment: .leading) {
      RoundedRectangle(cornerRadius: Self.cornerRadius).fill(color.opacity(dragging ? 0.55 : 0.35))
      RoundedRectangle(cornerRadius: Self.cornerRadius).strokeBorder(color.opacity(0.9), lineWidth: 1)
      HStack(spacing: 4) {
        if renamingId == section.id {
          renameField(section)
        } else {
          Text(section.name)
            .font(.system(size: 11, weight: .medium))
            .foregroundStyle(.white)
            .lineLimit(1)
            .truncationMode(.tail)
        }
        Spacer(minLength: 0)
        exportHandle(section)
      }
      .padding(.leading, first ? 6 : Self.edgeWidth)
      .padding(.trailing, 4)
    }
    .clipped()
    .overlay(alignment: .leading) { if !first { edgeHandle(section) } }
    .contentShape(Rectangle())
    .onTapGesture(count: 2) { beginRename(section) }
    .contextMenu { menu(section) }
    .help(section.name)
    .accessibilityElement(children: .contain)
    .accessibilityLabel("Section " + section.name)
  }

  /// The grab area at the left edge: dragging it moves the section's start (`moveSectionStart`) to the
  /// block boundary nearest to where it is dropped, computed from the section's original boundary plus
  /// the translation in the header's coordinate space (`SequenceGeometry.droppedBoundaryIndex`), as block
  /// moves do; the span as drawn mid-drag already carries the translation and is never the origin. A child
  /// of the span, so its gesture wins.
  private func edgeHandle(_ section: SectionJSON) -> some View {
    RoundedRectangle(cornerRadius: 1)
      .fill(Color.white.opacity(edgeDrag?.id == section.id ? 0.7 : 0.3))
      .frame(width: 3, height: Self.height - 8)
      .padding(.horizontal, (Self.edgeWidth - 3) / 2)
      .contentShape(Rectangle())
      .gesture(
        DragGesture(minimumDistance: 2, coordinateSpace: .named(Self.coordinateSpace))
          .onChanged { value in edgeDrag = EdgeDrag(id: section.id, dx: value.translation.width) }
          .onEnded { value in
            edgeDrag = nil
            let index = SequenceGeometry.droppedBoundaryIndex(from: section.firstEventIndex,
                                                               draggedBy: value.translation.width, in: layout)
            if index != section.firstEventIndex {
              state.send(command: ["cmd": "moveSectionStart", "id": section.id, "index": index])
            }
          })
      .onHover { over in
        if over { NSCursor.resizeLeftRight.push() } else { NSCursor.pop() }
      }
      .accessibilityHidden(true)
  }

  /// "♫": drag it onto a MIDI track and the section lands there as a .mid clip. The plug-in writes the
  /// file when the mouse arrives over the handle and again when the drag starts (synchronous round trip,
  /// like `ExportDragView`), so the clip is the section as it is at that moment.
  private func exportHandle(_ section: SectionJSON) -> some View {
    Text("♫")
      .font(.system(size: 11))
      .foregroundStyle(.white.opacity(0.8))
      .padding(.horizontal, 3)
      .contentShape(Rectangle())
      .onHover { over in
        if over { state.requestExportSection(id: section.id) }
      }
      .onDrag {
        state.requestExportSection(id: section.id)
        guard let path = state.exportPath else { return NSItemProvider() }
        return NSItemProvider(contentsOf: URL(fileURLWithPath: path)) ?? NSItemProvider()
      }
      .help("Drag this section onto a MIDI track in Live")
      .accessibilityLabel("Drag the section " + section.name + " to Live as a MIDI clip")
  }

  // MARK: - Context menu

  @ViewBuilder private func menu(_ section: SectionJSON) -> some View {
    Button("Rename…") { beginRename(section) }
    Menu("Colour") {
      ForEach(0..<SectionStyle.colors.count, id: \.self) { index in
        Button(SectionStyle.names[index] + (index == section.colourIndex ? "  ✓" : "")) {
          state.send(command: ["cmd": "setSectionColour", "id": section.id, "colourIndex": index])
        }
      }
    }
    Divider()
    Button("Remove section") { state.send(command: ["cmd": "removeSection", "id": section.id]) }
  }

  // MARK: - Rename

  private func renameField(_ section: SectionJSON) -> some View {
    TextField("Name", text: $draft)
      .textFieldStyle(.plain)
      .font(.system(size: 11, weight: .medium))
      .foregroundStyle(.white)
      .focused($renameFocused)
      .onChange(of: draft) { _, now in
        let bounded = HarmonyText.boundedSectionName(now)
        if bounded != now { draft = bounded }
      }
      .onSubmit { commitRename(section) }
      .onExitCommand { cancelRename() }
      .onChange(of: renameFocused) { _, focused in
        if !focused { commitRename(section) }
      }
      .onDisappear { cancelRename() }
      .accessibilityLabel("Section name")
  }

  private func beginRename(_ section: SectionJSON) {
    draft = section.name
    renamingId = section.id
    state.editingText = true
    DispatchQueue.main.async { renameFocused = true }   // the field exists on the next pass
  }

  /// Sends the new name if it is non-blank and differs; an unchanged or blank draft is a cancel.
  private func commitRename(_ section: SectionJSON) {
    guard renamingId == section.id else { return }
    renamingId = nil
    state.editingText = false
    let name = HarmonyText.boundedSectionName(draft.trimmingCharacters(in: .whitespacesAndNewlines))
    if !name.isEmpty && name != section.name {
      state.send(command: ["cmd": "renameSection", "id": section.id, "name": name])
    }
  }

  private func cancelRename() {
    guard renamingId != nil else { return }
    renamingId = nil
    state.editingText = false
  }
}

struct SectionsHeaderView_Previews: PreviewProvider {
  static var previews: some View {
    let layout = SequenceGeometry.layout(lengths: [4, 4, 2, 4], pxPerBeat: SequenceBarView.pxPerBeat)
    SectionsHeaderView(sections: [SectionJSON(id: 1, name: "Verse", colourIndex: 0, firstEventIndex: 0),
                                  SectionJSON(id: 2, name: "Chorus", colourIndex: 1, firstEventIndex: 2)],
                       layout: layout, width: 600)
      .environmentObject(ChordLabState(numParams: kNumParams))
      .padding().background(PaletteView.background)
  }
}
