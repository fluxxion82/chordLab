import SwiftUI

/// The progression as a strip of blocks over a beat ruler, above the palette: one block per event,
/// `lengthBeats × pxPerBeat` wide, coloured by family, a ring instead of a fill when the chord has notes
/// outside the project key, labelled per the label mode. Everything drawn comes from the snapshot, and
/// every position comes from `SequenceGeometry`: the blocks, the ruler, the playhead and the reorder
/// arithmetic share one linear layout, so what is drawn is what is computed.
///
/// Editing, every step a command to the plugin (the snapshot it answers with redraws the strip):
/// - drop a palette cell anywhere on the strip → `appendChord`;
/// - click a block → selected (UI state); Delete / Forward Delete → `removeChord`;
/// - drag the handle at a block's right edge → `setLength`, snapped to quarter beats and clamped to
///   0.25…64, sent when the drag ends (the width follows the mouse meanwhile, as local state);
/// - drag a block sideways → `moveChord` to the index its centre lands on, sent when the drag ends
///   (only the dragged block moves meanwhile).
/// Undo/redo are the key bar's buttons (⌘Z / ⇧⌘Z), so they work whatever has focus.
///
/// The playhead (`state.transport`, ~15 Hz from the plug-in) is a 2-pt line at the position's beat, folded
/// into the progression when it loops; the block sounding there is drawn brighter. Hidden while the
/// progression is not playing (transport stopped or Follow Live off), before beat 0, with no progression,
/// or past the end when the loop is off.
struct SequenceBarView: View {
  @EnvironmentObject var state: ChordLabState
  @AppStorage(LabelMode.storageKey) private var labelMode: LabelMode = .both

  static let pxPerBeat: CGFloat = 24
  static let height: CGFloat = 96
  static let blockHeight: CGFloat = 56
  static let rulerHeight: CGFloat = 20
  static let handleWidth: CGFloat = 6
  static let beatsPerBar = 4
  static let snapBeats = 0.25
  static let lengthRange = 0.25...64.0   // kMinLengthBeats...kMaxLengthBeats
  static let cornerRadius: CGFloat = 6

  /// A resize in progress: the handle of `id` is being dragged and the block shows `lengthBeats`.
  struct Resize: Equatable {
    let id: Int
    var lengthBeats: Double
  }

  /// A reorder in progress: block `id` is drawn `offset` points from its place.
  struct Move: Equatable {
    let id: Int
    var offset: CGFloat
  }

  @State private var selectedEventId: Int?
  @State private var resize: Resize?
  @State private var move: Move?
  @State private var dropTargeted = false
  @FocusState private var focused: Bool

  private var events: [EventJSON] { state.snapshot?.events ?? [] }

  /// The beat the playhead sits at, or nil when it is hidden.
  private var playheadBeat: Double? {
    guard let transport = state.transport, transport.playing, let snapshot = state.snapshot,
          snapshot.endBeat > 0, transport.ppq >= 0 else { return nil }
    if snapshot.progressionLoop {
      return transport.ppq.truncatingRemainder(dividingBy: snapshot.endBeat)
    }
    return transport.ppq < snapshot.endBeat ? transport.ppq : nil
  }

  var body: some View {
    GeometryReader { geometry in
      // The layout of what is drawn: the snapshot's lengths, except for a block being resized, which
      // follows the mouse. The ruler's end marker and the content width follow it too.
      let layout = SequenceGeometry.layout(lengths: events.map(displayedLength), pxPerBeat: Self.pxPerBeat)
      // Room for the progression plus a bar of slack to drop into; never narrower than the strip.
      let contentWidth = max(geometry.size.width, layout.endX + CGFloat(Self.beatsPerBar) * Self.pxPerBeat)
      ScrollView(.horizontal) {
        ZStack(alignment: .topLeading) {
          VStack(alignment: .leading, spacing: 2) {
            ZStack(alignment: .leading) {
              Color.clear
                .frame(width: contentWidth, height: Self.blockHeight)
                .contentShape(Rectangle())
                .onTapGesture { selectedEventId = nil }       // click the empty strip: deselect
              HStack(spacing: 0) {
                ForEach(Array(events.enumerated()), id: \.element.id) { index, event in
                  block(event, frame: layout.frames[index])
                }
              }
              if events.isEmpty {
                Text("Drag chords here")
                  .foregroundStyle(.gray)
                  .frame(width: geometry.size.width, height: Self.blockHeight)
                  .allowsHitTesting(false)
              }
            }
            ruler(width: contentWidth, endX: layout.endX)
          }
          if let beat = playheadBeat {
            Rectangle()
              .fill(Color.white)
              .frame(width: 2, height: Self.blockHeight + 2 + Self.rulerHeight)
              .offset(x: SequenceGeometry.x(ofBeat: beat, pxPerBeat: Self.pxPerBeat) - 1)
              .allowsHitTesting(false)
              .accessibilityHidden(true)
          }
        }
        .padding(.vertical, 6)
        .padding(.horizontal, 4)
      }
    }
    .frame(height: Self.height)
    .background(RoundedRectangle(cornerRadius: 8).fill(Color.white.opacity(0.04)))
    .overlay(
      RoundedRectangle(cornerRadius: 8)
        .strokeBorder(Color.white.opacity(dropTargeted ? 0.8 : 0.12), lineWidth: dropTargeted ? 2 : 1))
    .focusable()
    .focused($focused)
    .focusEffectDisabled()
    .onKeyPress(.delete) { deleteSelection() }
    .onKeyPress(.deleteForward) { deleteSelection() }
    .dropDestination(for: ChordRef.self) { refs, _ in
      guard let ref = refs.first else { return false }
      state.send(command: ["cmd": "appendChord", "root": ref.root, "type": ref.type])
      return true
    } isTargeted: { dropTargeted = $0 }
    .onChange(of: state.snapshot) { _, snapshot in
      // The selection follows the id; a chord removed or undone away is simply no longer selected.
      if let id = selectedEventId, snapshot?.events.contains(where: { $0.id == id }) != true {
        selectedEventId = nil
      }
    }
    .accessibilityLabel("Progression")
  }

  // MARK: - Geometry (SequenceGeometry, with this view's constants)

  /// The strip's layout for `events` at the snapshot's lengths: what the ruler, the playhead and the
  /// reorder arithmetic agree on.
  static func layout(events: [EventJSON], pxPerBeat: CGFloat = pxPerBeat) -> SequenceGeometry.Layout {
    SequenceGeometry.layout(lengths: events.map(\.lengthBeats), pxPerBeat: pxPerBeat)
  }

  /// `lengthBeats` after dragging its handle by `dx` points.
  static func resizedLength(_ lengthBeats: Double, by dx: CGFloat) -> Double {
    SequenceGeometry.resizedLength(lengthBeats, by: dx, pxPerBeat: pxPerBeat, snap: snapBeats, range: lengthRange)
  }

  /// The length a block is drawn with: the resize in progress if it is this block's, else the snapshot's.
  private func displayedLength(_ event: EventJSON) -> Double {
    if let resize = resize, resize.id == event.id { return resize.lengthBeats }
    return event.lengthBeats
  }

  // MARK: - Blocks

  /// One block, exactly `frame.width` wide: the hairline between neighbours is inset inside that frame,
  /// never added to it, so the HStack lands every block where the layout says.
  private func block(_ event: EventJSON, frame: SequenceGeometry.BlockFrame) -> some View {
    let color = PaletteCellView.color(family: event.family ?? "Other")
    let filled = (event.outOfKey ?? []).isEmpty
    let selected = selectedEventId == event.id
    let moving = move?.id == event.id
    let offset = moving ? (move?.offset ?? 0) : 0
    let sounding = playheadBeat.map { $0 >= event.startBeat && $0 < event.startBeat + event.lengthBeats } ?? false
    return ZStack {
      if filled {
        RoundedRectangle(cornerRadius: Self.cornerRadius).fill(color)
      } else {
        RoundedRectangle(cornerRadius: Self.cornerRadius).strokeBorder(color, lineWidth: 2)
      }
      if sounding {
        RoundedRectangle(cornerRadius: Self.cornerRadius).fill(Color.white.opacity(filled ? 0.25 : 0.15))
      }
      if selected {
        RoundedRectangle(cornerRadius: Self.cornerRadius).strokeBorder(Color.white, lineWidth: 2)
      }
      labels(event)
    }
    .padding(.horizontal, 0.5)
    .frame(width: frame.width, height: Self.blockHeight)
    .clipped()
    .overlay(alignment: .trailing) { handle(event, selected: selected, blockWidth: frame.width) }
    .contentShape(Rectangle())
    .onTapGesture { select(event) }
    .gesture(moveGesture(event))
    .offset(x: offset)
    .zIndex(moving ? 1 : 0)
    .opacity(moving ? 0.85 : 1)
    .accessibilityElement(children: .ignore)
    .accessibilityLabel(ChordSymbol.spokenName(symbol: event.symbol, type: event.type) +
                        ", \(event.lengthBeats.formatted()) beats" + (selected ? ", selected" : ""))
  }

  private func labels(_ event: EventJSON) -> some View {
    VStack(spacing: 1) {
      if labelMode != .numerals {
        Text(ChordSymbol.glyphs(event.symbol)).font(.system(size: 13, weight: .medium))
      }
      if labelMode != .names {
        Text(ChordSymbol.glyphs(event.roman)).font(.system(size: 11)).opacity(labelMode == .both ? 0.85 : 1)
      }
    }
    .foregroundStyle(.white)
    .lineLimit(1)
    .minimumScaleFactor(0.6)
    .padding(.horizontal, 4)
    .padding(.trailing, Self.handleWidth)
  }

  /// The grab handle at the right edge, inside the block's frame (narrower on a narrow block). A child
  /// of the block, so its gesture wins over the block's move gesture.
  private func handle(_ event: EventJSON, selected: Bool, blockWidth: CGFloat) -> some View {
    let width = min(Self.handleWidth, max(2, blockWidth / 2))
    let margin: CGFloat = blockWidth >= 4 * Self.handleWidth ? 3 : 0   // a wider hit area when there is room
    return RoundedRectangle(cornerRadius: 2)
      .fill(Color.white.opacity(selected || resize?.id == event.id ? 0.6 : 0.25))
      .frame(width: width, height: Self.blockHeight - 16)
      .padding(.horizontal, margin)
      .contentShape(Rectangle())
      .gesture(resizeGesture(event))
      .onHover { over in
        if over { NSCursor.resizeLeftRight.push() } else { NSCursor.pop() }
      }
      .accessibilityHidden(true)
  }

  // MARK: - Gestures

  private func select(_ event: EventJSON) {
    selectedEventId = event.id
    focused = true
  }

  private func resizeGesture(_ event: EventJSON) -> some Gesture {
    DragGesture(minimumDistance: 1)
      .onChanged { value in
        if resize?.id != event.id { select(event) }
        resize = Resize(id: event.id, lengthBeats: Self.resizedLength(event.lengthBeats, by: value.translation.width))
      }
      .onEnded { value in
        resize = nil
        let length = Self.resizedLength(event.lengthBeats, by: value.translation.width)
        if length != event.lengthBeats {
          state.send(command: ["cmd": "setLength", "id": event.id, "lengthBeats": length])
        }
      }
  }

  private func moveGesture(_ event: EventJSON) -> some Gesture {
    DragGesture(minimumDistance: 4)
      .onChanged { value in
        if move?.id != event.id { select(event) }
        move = Move(id: event.id, offset: value.translation.width)
      }
      .onEnded { value in
        move = nil
        let events = self.events
        guard let from = events.firstIndex(where: { $0.id == event.id }) else { return }
        let target = SequenceGeometry.targetIndex(from: from, droppedBy: value.translation.width,
                                                  in: Self.layout(events: events))
        if target != from {
          state.send(command: ["cmd": "moveChord", "id": event.id, "index": target])
        }
      }
  }

  private func deleteSelection() -> KeyPress.Result {
    guard let id = selectedEventId else { return .ignored }
    selectedEventId = nil
    state.send(command: ["cmd": "removeChord", "id": id])
    return .handled
  }

  // MARK: - Ruler

  /// A tick per beat (taller on bar lines) with the bar number at every bar, and a line where the
  /// progression ends (`endX`, from the same layout as the blocks).
  private func ruler(width: CGFloat, endX: CGFloat) -> some View {
    Canvas { context, size in
      let beats = Int(size.width / Self.pxPerBeat)
      for beat in 0...beats {
        let x = SequenceGeometry.x(ofBeat: Double(beat), pxPerBeat: Self.pxPerBeat) + 0.5
        let bar = beat % Self.beatsPerBar == 0
        var tick = Path()
        tick.move(to: CGPoint(x: x, y: 0))
        tick.addLine(to: CGPoint(x: x, y: bar ? 9 : 4))
        context.stroke(tick, with: .color(.white.opacity(bar ? 0.6 : 0.3)), lineWidth: 1)
        if bar {
          context.draw(Text("\(beat / Self.beatsPerBar + 1)").font(.system(size: 9)).foregroundStyle(.white.opacity(0.6)),
                       at: CGPoint(x: x + 3, y: 10), anchor: .topLeading)
        }
      }
      if endX > 0 {
        let x = endX + 0.5
        var end = Path()
        end.move(to: CGPoint(x: x, y: 0))
        end.addLine(to: CGPoint(x: x, y: size.height))
        context.stroke(end, with: .color(.white.opacity(0.5)), style: StrokeStyle(lineWidth: 1, dash: [2, 2]))
      }
    }
    .frame(width: width, height: Self.rulerHeight)
    .accessibilityHidden(true)
  }
}

struct SequenceBarView_Previews: PreviewProvider {
  static var previews: some View {
    SequenceBarView().environmentObject(ChordLabState(numParams: kNumParams))
      .padding().background(PaletteView.background).frame(width: 900)
  }
}
