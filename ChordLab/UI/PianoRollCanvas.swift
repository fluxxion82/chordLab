import SwiftUI

/// The piano roll's grid body (tracks design D14): ONE `Canvas` that draws the open track's roll at the view
/// state's scroll and zoom, for the visible region only (`RollGeometry.visibleTicks` / `visiblePitches`). There is
/// no scroll view: the canvas is a viewport onto the content, `view = content − scroll`. Back to front:
///
/// 1. the grid's dark ground and the black-key rows, a subtle darker stripe (`RollColors.blackKeyRow`), under
/// 2. the guide bands from the per-revision-and-zoom layout (`GuideBands`): per visible chord span and visible row,
///    the role's fill (`GuideStyle.color`) at the track role's opacity (`GuideStyle.opacity`), Outside nothing;
///    the rects of one role are filled as one path;
/// 3. row lines (a faint line between rows from 10-pt rows, a stronger one under each C), then the time lines (`RollLines`: bar,
///    the meter's pulse and the grid, bar-relative, none denser than 4 pt) and the chord boundaries at the
///    playback span starts;
/// 4. the degree labels at the left of each span (kept at the viewport's left edge while the span runs on) on Root
///    and ChordTone rows from 12-pt rows (`GuideBands.labels`), small and low contrast;
/// 5. the hatch after the progression's end (`RollTiming.endTick`): a darker area with faint diagonals anchored to
///    the content, and the dashed end line of the sequence ruler;
/// 6. with Other tracks on, the other tracks' notes as faint outlines in their own colours;
/// 7. the open track's notes in note order as rounded rects in `TrackStyle.color(role:)` at
///    `RollNoteStyle.opacity(velocity:)`, with a thin dark edge so neighbours and bands stay apart; inactive notes
///    (design D3) as dashed outlines at their derived position; notes in the gesture state's `hiddenIds` left out
///    and its `previewNotes` drawn opaque with a light ring; in Velocity mode a light bar along each note's top
///    whose length is its velocity; selected notes (the snapshot's `noteSelection`) wear a white ring;
/// 8. the rubber band.
///
/// The playhead and the insert marker are not drawn here: `RollMarkersOverlay` sits over the canvas, so a transport
/// push or a hover redraws neither this canvas nor its inputs (the caller passes the view state without the insert
/// marker and compares the canvas with `.equatable()`).
///
/// **Stacking is note order**, the gesture machine's hit rule (the topmost hit is the last note in note order): each
/// note's fill (or inactive outline), velocity bar and ring are drawn before the next note's. Notes are batched
/// into runs drawn as one path per layer (fill, edge, bars, rings) while that cannot change the stacking: a run
/// holds consecutive notes of one kind (active of one velocity, or inactive) none of which overlaps an earlier
/// note of the run (only notes on one row can overlap, so a per-row right edge decides). Guide fills are one path
/// per role and the other tracks' outlines one per track. The UI computes no theory: roles, degrees, spans and note
/// positions all come from the snapshot.
struct PianoRollCanvas: View, Equatable {
  let snapshot: Snapshot
  let track: TrackJSON
  let view: RollViewState
  /// The snapshot's timing (`ChordLabState.rollTiming`, computed once per snapshot).
  let timing: RollTiming
  let bands: GuideBands

  var body: some View {
    Canvas { context, size in
      draw(&context, size: size)
    }
  }

  // MARK: - Drawing

  private func draw(_ context: inout GraphicsContext, size: CGSize) {
    let geometry = view.geometry(ticksPerBeat: snapshot.ticksPerBeatOrDefault)
    let scroll = view.scroll
    let ticks = geometry.visibleTicks(viewport: size, scroll: scroll)
    let pitches = geometry.visiblePitches(viewport: size, scroll: scroll)
    let bounds = CGRect(origin: .zero, size: size)
    let visibleBands = bands.indices(intersecting: ticks)

    context.fill(Path(bounds), with: .color(RollColors.gridGround))
    drawRows(&context, size: size, geometry: geometry, scroll: scroll, pitches: pitches)
    drawBands(&context, size: size, geometry: geometry, scroll: scroll, pitches: pitches, visible: visibleBands)
    drawRowLines(&context, size: size, geometry: geometry, scroll: scroll, pitches: pitches)
    drawTimeLines(&context, size: size, geometry: geometry, scroll: scroll, ticks: ticks, timing: timing)
    drawChordBoundaries(&context, size: size, scroll: scroll, visible: visibleBands)
    drawLabels(&context, geometry: geometry, scroll: scroll, pitches: pitches, visible: visibleBands)
    drawHatch(&context, size: size, geometry: geometry, scroll: scroll, timing: timing)
    if view.showsOtherTracks {
      drawOtherTracks(&context, geometry: geometry, scroll: scroll, ticks: ticks, pitches: pitches)
    }
    drawNotes(&context, geometry: geometry, scroll: scroll, ticks: ticks, pitches: pitches)
    drawRubberBand(&context, scroll: scroll)
  }

  /// A crisp 1-pt vertical or horizontal line position: the pixel centre at or before `value`.
  private func crisp(_ value: CGFloat) -> CGFloat { value.rounded(.down) + 0.5 }

  private func drawRows(_ context: inout GraphicsContext, size: CGSize, geometry: RollGeometry, scroll: CGPoint,
                        pitches: ClosedRange<Int>) {
    var black = Path()
    for pitch in pitches where RollGeometry.isBlackKey(pitch: pitch) {
      black.addRect(CGRect(x: 0, y: geometry.y(pitch: pitch) - scroll.y, width: size.width, height: geometry.rowHeight))
    }
    context.fill(black, with: .color(RollColors.blackKeyRow))
  }

  private func drawBands(_ context: inout GraphicsContext, size: CGSize, geometry: RollGeometry, scroll: CGPoint,
                         pitches: ClosedRange<Int>, visible: Range<Int>) {
    var fills: [GuideRole: Path] = [:]
    var opacity: [GuideRole: Double] = [:]
    for index in visible {
      let band = bands.bands[index]
      let x0 = max(band.minX - scroll.x, 0)
      let x1 = min(band.maxX - scroll.x, size.width)
      guard x1 > x0 else { continue }
      for pitch in pitches {
        let pc = pitch % 12
        guard band.opacities[pc] > 0 else { continue }
        let role = band.roles[pc]
        opacity[role] = band.opacities[pc]
        fills[role, default: Path()].addRect(CGRect(x: x0, y: geometry.y(pitch: pitch) - scroll.y, width: x1 - x0,
                                                    height: geometry.rowHeight))
      }
    }
    for role in [GuideRole.avoid, .tension, .chord, .root] {
      guard let path = fills[role], let alpha = opacity[role] else { continue }
      context.fill(path, with: .color(GuideStyle.color(role: role).opacity(alpha)))
    }
  }

  private func drawRowLines(_ context: inout GraphicsContext, size: CGSize, geometry: RollGeometry, scroll: CGPoint,
                            pitches: ClosedRange<Int>) {
    var rows = Path()
    var octaves = Path()
    // Under 10-pt rows the black-key stripes alone mark the rows; a line under every one would turn into a wash.
    let everyRow = geometry.rowHeight >= RollColors.minRowLineHeight
    for pitch in pitches {
      // The line under the row: the boundary to the pitch below; under a C it is the octave line.
      let y = crisp(geometry.y(pitch: pitch) + geometry.rowHeight - scroll.y)
      if pitch % 12 == 0 {
        octaves.move(to: CGPoint(x: 0, y: y))
        octaves.addLine(to: CGPoint(x: size.width, y: y))
      } else if everyRow {
        rows.move(to: CGPoint(x: 0, y: y))
        rows.addLine(to: CGPoint(x: size.width, y: y))
      }
    }
    context.stroke(rows, with: .color(RollColors.rowLine), lineWidth: 1)
    context.stroke(octaves, with: .color(RollColors.octaveLine), lineWidth: 1)
  }

  private func drawTimeLines(_ context: inout GraphicsContext, size: CGSize, geometry: RollGeometry, scroll: CGPoint,
                             ticks: Range<Int>, timing: RollTiming) {
    let lines = RollLines.lines(ticks: ticks, barTicks: timing.barTicks,
                                pulseTicks: RollLines.pulseTicks(meter: snapshot.meter, ticksPerBeat: timing.ticksPerBeat),
                                gridTicks: view.grid.ticks(ticksPerBeat: timing.ticksPerBeat), geometry: geometry)
    var bars = Path()
    var beats = Path()
    var grid = Path()
    for line in lines {
      let x = crisp(geometry.x(tick: line.tick) - scroll.x)
      switch line.kind {
      case .bar:
        bars.move(to: CGPoint(x: x, y: 0))
        bars.addLine(to: CGPoint(x: x, y: size.height))
      case .beat:
        beats.move(to: CGPoint(x: x, y: 0))
        beats.addLine(to: CGPoint(x: x, y: size.height))
      case .grid:
        grid.move(to: CGPoint(x: x, y: 0))
        grid.addLine(to: CGPoint(x: x, y: size.height))
      }
    }
    context.stroke(grid, with: .color(RollColors.gridLine), lineWidth: 1)
    context.stroke(beats, with: .color(RollColors.beatLine), lineWidth: 1)
    context.stroke(bars, with: .color(RollColors.barLine), lineWidth: 1)
  }

  private func drawChordBoundaries(_ context: inout GraphicsContext, size: CGSize, scroll: CGPoint, visible: Range<Int>) {
    var path = Path()
    for index in visible where bands.bands[index].ticks.lowerBound > 0 {
      let x = crisp(bands.bands[index].minX - scroll.x)
      guard x >= 0, x <= size.width else { continue }
      path.move(to: CGPoint(x: x, y: 0))
      path.addLine(to: CGPoint(x: x, y: size.height))
    }
    context.stroke(path, with: .color(RollColors.chordBoundary), lineWidth: 1)
  }

  private func drawLabels(_ context: inout GraphicsContext, geometry: RollGeometry, scroll: CGPoint,
                          pitches: ClosedRange<Int>, visible: Range<Int>) {
    guard bands.showsLabels else { return }
    // A frame's labels use a handful of strings ("R", "3", "♭7"): each is resolved once.
    var resolved: [String: GraphicsContext.ResolvedText] = [:]
    func text(_ string: String, root: Bool) -> GraphicsContext.ResolvedText {
      let key = (root ? "r" : "c") + string
      if let done = resolved[key] { return done }
      let made = context.resolve(Text(string).font(RollColors.labelFont)
        .foregroundColor(root ? RollColors.rootLabel : RollColors.chordLabel))
      resolved[key] = made
      return made
    }
    for index in visible {
      let band = bands.bands[index]
      let left = max(band.minX - scroll.x, 0) + 3
      guard band.maxX - scroll.x - left >= RollColors.minLabelSpan else { continue }
      for label in bands.labels(in: band, pitches: pitches) {
        let y = geometry.y(pitch: label.pitch) - scroll.y + geometry.rowHeight / 2
        context.draw(text(label.text, root: label.role == .root), at: CGPoint(x: left, y: y), anchor: .leading)
      }
    }
  }

  private func drawHatch(_ context: inout GraphicsContext, size: CGSize, geometry: RollGeometry, scroll: CGPoint,
                         timing: RollTiming) {
    let endX = geometry.x(tick: timing.endTick) - scroll.x
    guard endX < size.width else { return }
    let area = CGRect(x: max(endX, 0), y: 0, width: size.width - max(endX, 0), height: size.height)
    context.fill(Path(area), with: .color(RollColors.afterEnd))
    // Diagonals x − y = c in content coordinates, so the hatch scrolls with the content.
    let spacing = RollColors.hatchSpacing
    let low = area.minX + scroll.x - (area.maxY + scroll.y)
    let high = area.maxX + scroll.x - (area.minY + scroll.y)
    var hatch = Path()
    var c = (low / spacing).rounded(.down) * spacing
    while c <= high {
      hatch.move(to: CGPoint(x: c + area.minY + scroll.y - scroll.x, y: area.minY))
      hatch.addLine(to: CGPoint(x: c + area.maxY + scroll.y - scroll.x, y: area.maxY))
      c += spacing
    }
    context.drawLayer { layer in
      layer.clip(to: Path(area))
      layer.stroke(hatch, with: .color(RollColors.hatch), lineWidth: 1)
    }
    if endX >= 0 {
      var end = Path()
      end.move(to: CGPoint(x: crisp(endX), y: 0))
      end.addLine(to: CGPoint(x: crisp(endX), y: size.height))
      context.stroke(end, with: .color(RollColors.endLine), style: StrokeStyle(lineWidth: 1, dash: [2, 2]))
    }
  }

  /// A note's rect in view coordinates: one row inset by a point (half a point under 12-pt rows) top and bottom,
  /// and a point shorter, so neighbours never merge.
  private func noteRect(start: Int, length: Int, pitch: Int, geometry: RollGeometry, scroll: CGPoint) -> CGRect {
    let rect = geometry.noteRect(start: start, length: length, pitch: pitch).offsetBy(dx: -scroll.x, dy: -scroll.y)
    let inset: CGFloat = geometry.rowHeight >= 12 ? 1 : 0.5
    return CGRect(x: rect.minX, y: rect.minY + inset, width: max(rect.width - 1, 2), height: rect.height - 2 * inset)
  }

  private func roundedNote(_ rect: CGRect) -> Path {
    Path(roundedRect: rect, cornerRadius: min(RollColors.noteCorner, rect.height / 2, rect.width / 2))
  }

  private func drawOtherTracks(_ context: inout GraphicsContext, geometry: RollGeometry, scroll: CGPoint,
                               ticks: Range<Int>, pitches: ClosedRange<Int>) {
    for other in snapshot.tracks ?? [] where other.id != track.id {
      var path = Path()
      let notes = other.notes
      for i in RollVisibleNotes.indices(notes, ticks: ticks, pitches: pitches) {
        let rect = noteRect(start: notes.start[i], length: notes.length[i], pitch: notes.pitch[i], geometry: geometry,
                            scroll: scroll)
        path.addPath(roundedNote(rect.insetBy(dx: 0.5, dy: 0.5)))
      }
      context.stroke(path, with: .color(TrackStyle.color(role: other.role).opacity(RollColors.otherTrackOpacity)),
                     lineWidth: 1)
    }
  }

  private func drawNotes(_ context: inout GraphicsContext, geometry: RollGeometry, scroll: CGPoint, ticks: Range<Int>,
                         pitches: ClosedRange<Int>) {
    let notes = track.notes
    let colour = TrackStyle.color(role: track.role)
    let selected = track.selectedIds(in: snapshot.noteSelection)
    let hidden = view.editState.hiddenIds
    let velocityMode = view.mode == .velocity
    // The current run (see the type comment): its kind (nil: empty; an active note's velocity, or `inactiveKind`,
    // one style whatever the velocity), its layers, and per row the right edge of its notes so far (rows not in the
    // run are -infinity; `rows` lists the ones to reset).
    let inactiveKind = -1
    var runKind: Int?
    var fills = Path()
    var bars = Path()
    var rings = Path()
    var rightEdge = [CGFloat](repeating: -.infinity, count: RollGeometry.pitchCount)
    var rows: [Int] = []
    func flush() {
      guard let kind = runKind else { return }
      if kind != inactiveKind {
        context.fill(fills, with: .color(colour.opacity(RollNoteStyle.opacity(velocity: kind))))
        context.stroke(fills, with: .color(RollColors.noteEdge), lineWidth: 0.75)
      } else {
        context.fill(fills, with: .color(colour.opacity(RollColors.inactiveFill)))
        context.stroke(fills, with: .color(colour.opacity(0.9)), style: StrokeStyle(lineWidth: 1, dash: [3, 2]))
      }
      if !bars.isEmpty { context.fill(bars, with: .color(RollColors.velocityBar)) }
      if !rings.isEmpty { context.stroke(rings, with: .color(.white), lineWidth: 1.5) }
      fills = Path()
      bars = Path()
      rings = Path()
      for row in rows { rightEdge[row] = -.infinity }
      rows.removeAll(keepingCapacity: true)
      runKind = nil
    }
    for i in RollVisibleNotes.indices(notes, ticks: ticks, pitches: pitches) where !hidden.contains(notes.id[i]) {
      let rect = noteRect(start: notes.start[i], length: notes.length[i], pitch: notes.pitch[i], geometry: geometry,
                          scroll: scroll)
      let active = notes.active[i]
      let kind = active ? max(notes.velocity[i], 0) : inactiveKind
      let row = min(max(notes.pitch[i], 0), RollGeometry.pitchCount - 1)
      // A different kind, or an overlap with an earlier note of the run on this row (within a point: the edge and
      // the ring reach that far), starts a new run, so this note draws over that one.
      if let current = runKind, current != kind || rect.minX < rightEdge[row] + 1 { flush() }
      runKind = kind
      if rightEdge[row] == -.infinity { rows.append(row) }
      rightEdge[row] = max(rightEdge[row], rect.maxX)
      fills.addPath(active ? roundedNote(rect) : roundedNote(rect.insetBy(dx: 0.5, dy: 0.5)))
      if velocityMode {
        bars.addRect(velocityBar(rect, velocity: notes.velocity[i]))
      }
      if selected.contains(notes.id[i]) {
        rings.addPath(roundedNote(rect.insetBy(dx: 0.75, dy: 0.75)))
      }
    }
    flush()

    // The drag preview: the edited notes (or the duplicates) drawn opaque with a light ring, over every note, their
    // velocity bars on top.
    var preview = Path()
    var previewBars = Path()
    for note in view.editState.previewNotes {
      let rect = noteRect(start: note.start, length: note.length, pitch: note.pitch, geometry: geometry, scroll: scroll)
      preview.addPath(roundedNote(rect))
      if velocityMode { previewBars.addRect(velocityBar(rect, velocity: note.velocity)) }
    }
    context.fill(preview, with: .color(colour))
    context.stroke(preview, with: .color(.white.opacity(0.85)), lineWidth: 1)
    context.fill(previewBars, with: .color(RollColors.velocityBar))
  }

  /// Velocity mode's bar: 2 pt along the note's top, as long as the velocity is of 127.
  private func velocityBar(_ rect: CGRect, velocity: Int) -> CGRect {
    let fraction = CGFloat(min(max(velocity, 0), 127)) / 127
    return CGRect(x: rect.minX + 1, y: rect.minY + 1, width: max(1, (rect.width - 2) * fraction), height: 2)
  }

  private func drawRubberBand(_ context: inout GraphicsContext, scroll: CGPoint) {
    guard let band = view.editState.rubberBand else { return }
    let rect = band.offsetBy(dx: -scroll.x, dy: -scroll.y)
    context.fill(Path(rect), with: .color(.white.opacity(0.06)))
    context.stroke(Path(rect), with: .color(.white.opacity(0.8)), lineWidth: 1)
  }
}
