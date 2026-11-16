import AppKit
import SwiftUI

/// The open track's piano roll (tracks design D14), in the place the suggestions, palette and home row take with
/// Chords open (D13), taking the flexible height. Top to bottom:
///
/// - **Toolbar** (`toolbarHeight`, the small controls of the lane tab row's size): Grid (`RollGrid`, the current one
///   checked), Snap pitch (`PitchSnap`), Mode (Notes | Velocity), zoom − / + (time, ×2 / ÷2 about the grid's
///   centre, each disabled at its limit), Other tracks (the faint outlines of the other tracks' notes, default on),
///   and the nudge buttons ◀ ▶ ▲ ▼ (`RollNudge`: one `updateNotes` per press for the selected notes, each enabled
///   only for a direction the selection can move in). Grid, Snap pitch, Mode, zoom and Other tracks change the
///   track's view state (`ChordLabState.rolls`), never the document: nothing is sent. Generate ▸ (after Mode, D14)
///   writes a bass line (`GenerateOptions`: one `generateBass` per choice, for the selected chords or every chord).
/// - **Chord ribbon** (`ribbonHeight`) over the grid: the meter's bar numbers and pulse ticks (the sequence ruler's
///   look), then the chord symbols over their playback spans on their sections' tints (`SectionStyle`), the chord
///   boundaries, the dashed end line, and the playhead.
/// - **Keyboard column** (`PianoKeyboardView`, `keyboardWidth`) left of the grid, a key press previewing its pitch
///   (`PianoKeyInputView`), and the **grid** (`PianoRollCanvas`).
///
/// The playhead (and over the grid the insert marker) is a light overlay (`RollMarkersOverlay`) above the ribbon's
/// and the grid's canvases, never an input of them: a transport push (display rate while playing) moves two thin
/// rectangles and redraws neither canvas, which only take the snapshot, the track, the view state and the timing
/// (`PianoRollCanvas` is `Equatable` and compared with `.equatable()`). The toolbar likewise takes plain values and
/// an actions struct, so it re-renders only when one of its values changes.
///
/// Scrolling has no `ScrollView`: the grid is a viewport that draws its content at the track's `scroll`, the ribbon
/// follows it horizontally and the keyboard vertically, all from the same value. The grid region is one view (a
/// `ZStack` holding the canvas, the markers overlay and `RollInputView` over them, covering the grid only, which
/// turns the grid's mouse, wheel and pinch into the gesture machine's events and the scroll and zoom setters); its
/// measured size is the viewport (`onGeometryChange` → `ChordLabState.setRollViewport`), which
/// also performs the track's first open (bar 1, the role's register). Scroll and zoom are set from outside through
/// `ChordLabState` (`setRollScroll`, `scrollRoll`, `zoomRoll`, `zoomRollIn/Out`; the input view and the toolbar). Each track gets a fresh view
/// (`.id(track.id)` in `ContentView`), so its grid measures itself when it opens.
struct PianoRollView: View {
  @EnvironmentObject var state: ChordLabState
  @ObservedObject var rolls: RollViewStore
  let track: TrackJSON

  static let toolbarHeight: CGFloat = 22
  static let ribbonHeight: CGFloat = 30
  static let ribbonBarStripHeight: CGFloat = 12
  static let keyboardWidth: CGFloat = 44
  static let spacing: CGFloat = 6
  static let cornerRadius: CGFloat = 6

  var body: some View {
    let name = LaneTabs.label(for: track)
    VStack(spacing: Self.spacing) {
      if let snapshot = state.snapshot {
        let view = rolls.states[track.id]
        let timing = state.rollTiming ?? RollTiming(snapshot: snapshot)
        let geometry = view.geometry(ticksPerBeat: timing.ticksPerBeat)
        let playhead = Playhead.beat(transport: state.transport, endBeat: snapshot.endBeat,
                                     loop: snapshot.progressionLoop)
        RollToolbar(trackId: track.id, grid: view.grid, snap: view.snap, mode: view.mode,
                    showsOtherTracks: view.showsOtherTracks, canZoomIn: geometry.canZoomIn,
                    canZoomOut: geometry.canZoomOut,
                    nudges: RollNudge.availability(trackId: track.id, snapshot: snapshot, timing: timing, grid: view.grid),
                    generate: GenerateOptions.menu(snapshot),
                    actions: .roll(of: state, trackId: track.id))
          .equatable()
        VStack(spacing: 0) {
          HStack(spacing: 0) {
            Color.clear.frame(width: Self.keyboardWidth)
            RollRibbonView(snapshot: snapshot, timing: timing, geometry: geometry, scrollX: view.scroll.x)
              .equatable()
              .overlay(RollMarkersOverlay(geometry: geometry, scrollX: view.scroll.x,
                                          ticksPerBeat: timing.ticksPerBeat, playheadBeat: playhead))
          }
          .frame(height: Self.ribbonHeight)
          .background(RollColors.ribbonGround)
          HStack(spacing: 0) {
            PianoKeyboardView(geometry: geometry, scrollY: view.scroll.y)
              .overlay(PianoKeyInputView(trackId: track.id, state: state).accessibilityHidden(true))
              .frame(width: Self.keyboardWidth)
            grid(snapshot: snapshot, view: view, timing: timing, geometry: geometry, playhead: playhead, name: name)
          }
        }
        .clipShape(RoundedRectangle(cornerRadius: Self.cornerRadius))
        .overlay(RoundedRectangle(cornerRadius: Self.cornerRadius).strokeBorder(RollColors.frame, lineWidth: 1))
      }
    }
    .frame(maxWidth: .infinity, maxHeight: .infinity)
    .accessibilityElement(children: .contain)
    .accessibilityLabel("Piano roll — \(name)")
    .accessibilityIdentifier("chordlab.rollArea")
  }

  // MARK: - Grid

  /// The grid region: the canvas, the markers overlay (playhead, insert marker; never hit-tested) and the input view
  /// (`RollInputView`) over them, in one frame that is the viewport. The canvas gets the view state without the
  /// insert marker, so a hover that only moves the marker redraws the overlay alone.
  private func grid(snapshot: Snapshot, view: RollViewState, timing: RollTiming, geometry: RollGeometry,
                    playhead: Double?, name: String) -> some View {
    var drawn = view
    drawn.editState.insertMarker = nil
    return ZStack(alignment: .topLeading) {
      PianoRollCanvas(snapshot: snapshot, track: track, view: drawn, timing: timing,
                      bands: rolls.bands.bands(snapshot: snapshot, geometry: geometry, trackRole: track.role))
        .equatable()
      RollMarkersOverlay(geometry: geometry, scrollX: view.scroll.x, ticksPerBeat: timing.ticksPerBeat,
                         playheadBeat: playhead, insertMarker: view.editState.insertMarker)
      // The input view covers the grid region only (the overlay beneath takes no events): mouse, wheel, pinch and
      // cursor for this track, into ChordLabState.handleRollPointer and the roll setters.
      RollInputView(trackId: track.id, state: state)
    }
    .clipped()
    .contentShape(Rectangle())
    .onGeometryChange(for: CGSize.self) { $0.size } action: { size in
      state.setRollViewport(trackId: track.id, size)
    }
    .accessibilityElement(children: .ignore)
    .accessibilityLabel("\(name) notes")
    .accessibilityValue("\(track.notes.count) notes")
  }
}

/// What the roll's toolbar does (`RollToolbar`): the `ChordLabState` roll setters of one track, its nudge and its
/// Generate menu (the toolbar actions that send a command).
struct RollToolbarActions {
  let setGrid: @MainActor (RollGrid) -> Void
  let setSnap: @MainActor (PitchSnap) -> Void
  let setMode: @MainActor (RollMode) -> Void
  let setShowsOtherTracks: @MainActor (Bool) -> Void
  let zoomIn: @MainActor () -> Void
  let zoomOut: @MainActor () -> Void
  let nudge: @MainActor (ArrowDirection) -> Void
  let generate: @MainActor (GenerateEntry) -> Void

  /// Track `trackId`'s setters on `state`.
  static func roll(of state: ChordLabState, trackId: Int) -> RollToolbarActions {
    RollToolbarActions(setGrid: { state.setRollGrid(trackId: trackId, $0) },
                       setSnap: { state.setRollSnap(trackId: trackId, $0) },
                       setMode: { state.setRollMode(trackId: trackId, $0) },
                       setShowsOtherTracks: { state.setRollShowsOtherTracks(trackId: trackId, $0) },
                       zoomIn: { state.zoomRollIn(trackId: trackId) },
                       zoomOut: { state.zoomRollOut(trackId: trackId) },
                       nudge: { state.nudgeRoll(trackId: trackId, $0) },
                       generate: { state.generateBass(trackId: trackId, $0) })
  }
}

/// The roll's toolbar (tracks design D14; see `PianoRollView`): plain values and an actions struct, no observed
/// object, and `Equatable` over the values (the actions are the same setters of the same track, `trackId`), so
/// with `.equatable()` it re-renders only when a value changes: never on a scroll or a transport push. The Generate
/// menu's model is one of the values, so a chord selection or Loop change updates the menu.
///
/// The row is laid out twice in a `ViewThatFits`: Generate reads "Generate" when the row has room for it, and shrinks
/// to its icon (same menu, same accessibility label) when not, as at the 700-pt minimum width with the widest Grid
/// (1/16T) and Snap (Chord + tensions) titles.
struct RollToolbar: View, Equatable {
  let trackId: Int
  let grid: RollGrid
  let snap: PitchSnap
  let mode: RollMode
  let showsOtherTracks: Bool
  let canZoomIn: Bool
  let canZoomOut: Bool
  /// Which nudge buttons are enabled (`RollNudge.availability`).
  let nudges: RollNudgeAvailability
  /// The Generate menu (`GenerateOptions.menu`): its groups, the Walking note, whether it is enabled, value and help.
  let generate: GenerateMenu
  let actions: RollToolbarActions

  static func == (a: RollToolbar, b: RollToolbar) -> Bool {
    a.trackId == b.trackId && a.grid == b.grid && a.snap == b.snap && a.mode == b.mode
      && a.showsOtherTracks == b.showsOtherTracks && a.canZoomIn == b.canZoomIn && a.canZoomOut == b.canZoomOut
      && a.nudges == b.nudges && a.generate == b.generate
  }

  /// An icon button's square (zoom, nudges): borderless, like the lane tab row's +, so the widest toolbar (Grid
  /// 1/16T, Snap Chord + tensions) still fits the 700-pt minimum width.
  static let iconSize: CGFloat = 20

  var body: some View {
    ViewThatFits(in: .horizontal) {
      row(compactGenerate: false)
      row(compactGenerate: true)
    }
    .font(.system(size: 12))
    .controlSize(.small)
    .frame(height: PianoRollView.toolbarHeight)
  }

  /// One layout of the row: `compactGenerate` draws Generate as its icon and tightens the gaps from 8 to 6 pt, which
  /// leaves the widest row at the 700-pt minimum width some room (with 8-pt gaps it would fit with none to spare).
  private func row(compactGenerate: Bool) -> some View {
    HStack(spacing: compactGenerate ? 6 : 8) {
      Menu {
        ForEach(RollGrid.allCases, id: \.self) { option in
          Button(option.title + (option == grid ? "  ✓" : "")) { actions.setGrid(option) }
        }
      } label: {
        Text("Grid \(grid.title)")
      }
      .fixedSize()
      .help("The grid notes are drawn and moved on")
      .accessibilityLabel("Grid")
      .accessibilityValue(grid.title)

      Menu {
        ForEach(PitchSnap.allCases, id: \.self) { option in
          Button(option.title + (option == snap ? "  ✓" : "")) { actions.setSnap(option) }
        }
      } label: {
        Text("Snap \(snap.title)")
      }
      .fixedSize()
      .help("Snap drawn and moved notes to the chord's scale or tones")
      .accessibilityLabel("Snap pitch")
      .accessibilityValue(snap.title)

      Picker("Mode", selection: Binding(get: { mode }, set: { actions.setMode($0) })) {
        Text("Notes").tag(RollMode.notes)
        Text("Velocity").tag(RollMode.velocity)
      }
      .pickerStyle(.segmented)
      .labelsHidden()
      .fixedSize()
      .help("Notes: draw, move and resize; Velocity: drag a note up or down to change how hard it plays")

      generateMenu(compact: compactGenerate)

      HStack(spacing: 0) {
        iconButton("minus.magnifyingglass", label: "Zoom out", help: "Zoom out in time",
                   enabled: canZoomOut) { actions.zoomOut() }
        iconButton("plus.magnifyingglass", label: "Zoom in", help: "Zoom in in time",
                   enabled: canZoomIn) { actions.zoomIn() }
      }

      Spacer(minLength: 0)

      Toggle(isOn: Binding(get: { showsOtherTracks },
                           set: { actions.setShowsOtherTracks($0) })) {
        Text("Other tracks").foregroundStyle(.white)
      }
      .toggleStyle(.accentSwitch)
      .fixedSize()
      .help("Show the other tracks' notes as faint outlines")
      .accessibilityLabel("Show other tracks")

      HStack(spacing: 0) {
        // The arrow keys' nudges as buttons, for hosts that keep the arrow keys (design D15 "Keyboard").
        iconButton("arrowtriangle.left.fill", label: "Nudge left", help: "Move the selected notes one grid step earlier",
                   enabled: nudges.left) { actions.nudge(.left) }
        iconButton("arrowtriangle.right.fill", label: "Nudge right", help: "Move the selected notes one grid step later",
                   enabled: nudges.right) { actions.nudge(.right) }
        iconButton("arrowtriangle.up.fill", label: "Nudge up", help: "Move the selected notes up a semitone",
                   enabled: nudges.up) { actions.nudge(.up) }
        iconButton("arrowtriangle.down.fill", label: "Nudge down", help: "Move the selected notes down a semitone",
                   enabled: nudges.down) { actions.nudge(.down) }
      }
    }
  }

  /// The SF Symbol of the compact Generate menu.
  static let generateSymbol = "wand.and.stars"

  /// The compact Generate menu's icon at the icon buttons' 12-pt medium weight. Made as an `NSImage` with its own
  /// accessibility description, because the menu button reports its image's description ("Magic wand with sparkles"
  /// for `Image(systemName:)`) before the menu's label.
  static let generateIcon: Image = {
    let config = NSImage.SymbolConfiguration(pointSize: 12, weight: .medium)
    guard let image = NSImage(systemSymbolName: generateSymbol, accessibilityDescription: "Generate")?
      .withSymbolConfiguration(config) else { return Image(systemName: generateSymbol) }
    return Image(nsImage: image)
  }()

  /// Generate ▸ (`GenerateOptions.menu`): a header per group naming what it replaces, the six patterns under each, the
  /// Walking note last; choosing an entry sends its one `generateBass`. Disabled on an empty progression, its tooltip
  /// then saying why. `compact` draws only the icon, borderless and without the menu arrow, in an icon button's square.
  @ViewBuilder private func generateMenu(compact: Bool) -> some View {
    let menu = Menu {
      ForEach(generate.groups) { group in
        Section(group.title) {
          ForEach(group.entries) { entry in
            Button(entry.title) { actions.generate(entry) }
          }
        }
      }
      if let note = generate.walkingNote {
        Section { Text(note) }
      }
    } label: {
      if compact {
        Self.generateIcon
          .frame(width: Self.iconSize, height: Self.iconSize)
          .contentShape(Rectangle())
      } else {
        Text("Generate")
      }
    }
    Group {
      if compact {
        menu.menuStyle(.borderlessButton).menuIndicator(.hidden)
          .frame(width: Self.iconSize, height: Self.iconSize)
          .contentShape(Rectangle())
      } else {
        menu
      }
    }
    .fixedSize()
    .disabled(!generate.isEnabled)
    .help(generate.help)
    .accessibilityLabel("Generate")
    .accessibilityValue(generate.value)
  }

  private func iconButton(_ symbol: String, label: String, help: String, enabled: Bool,
                          action: @escaping () -> Void) -> some View {
    Button(action: action) {
      Image(systemName: symbol)
        .font(.system(size: 12, weight: .medium))
        .frame(width: Self.iconSize, height: Self.iconSize)
        .contentShape(Rectangle())
    }
    .buttonStyle(.borderless)
    .disabled(!enabled)
    .help(help)
    .accessibilityLabel(label)
  }
}

/// The chord ribbon above the grid (tracks design D14), one `Canvas` at the grid's horizontal scroll: a
/// `PianoRollView.ribbonBarStripHeight` strip of bar numbers and pulse ticks (the sequence ruler's look: bar ticks
/// 9 pt at 60 %, pulses 4 pt at 30 %, 9-pt numbers), then the chord strip: section tints (`SectionStyle` at
/// `RollColors.sectionTint`) behind each chord's symbol over its playback span, boundary lines at the span starts,
/// the area after the end dimmed with the ruler's dashed end line. A symbol stays at the left edge while its span
/// runs on past it, clipped to its span. The playhead is `RollMarkersOverlay` over it (not an input, so a transport
/// push does not redraw it).
struct RollRibbonView: View, Equatable {
  let snapshot: Snapshot
  let timing: RollTiming
  let geometry: RollGeometry
  let scrollX: CGFloat

  var body: some View {
    Canvas { context, size in
      let strip = PianoRollView.ribbonBarStripHeight
      let scroll = CGPoint(x: scrollX, y: 0)
      let ticks = geometry.visibleTicks(viewport: size, scroll: scroll)
      let events = snapshot.events
      let rate = timing.ticksPerBeat
      func x(_ tick: Int) -> CGFloat { geometry.x(tick: tick) - scrollX }
      let chordStrip = CGRect(x: 0, y: strip, width: size.width, height: size.height - strip)

      for span in RollRibbon.sectionSpans(snapshot) where span.ticks.overlaps(ticks) {
        let rect = CGRect(x: x(span.ticks.lowerBound), y: chordStrip.minY, width: geometry.x(tick: span.ticks.count),
                          height: chordStrip.height)
        context.fill(Path(rect), with: .color(SectionStyle.color(span.colourIndex).opacity(RollColors.sectionTint)))
      }
      let endX = x(timing.endTick)
      if endX < size.width {
        context.fill(Path(CGRect(x: max(endX, 0), y: 0, width: size.width - max(endX, 0), height: size.height)),
                     with: .color(RollColors.afterEnd))
      }

      var boundaries = Path()
      for index in PlaySpans.eventIndices(intersecting: ticks, in: events, ticksPerBeat: rate) {
        guard let span = events[index].playTicks(ticksPerBeat: rate) else { continue }
        let start = x(span.lowerBound)
        let end = x(span.upperBound)
        if span.lowerBound > 0 {
          boundaries.move(to: CGPoint(x: start.rounded(.down) + 0.5, y: chordStrip.minY))
          boundaries.addLine(to: CGPoint(x: start.rounded(.down) + 0.5, y: chordStrip.maxY))
        }
        let left = max(start, 0) + 4
        guard end - left > 10 else { continue }
        let symbol = Text(ChordSymbol.glyphs(events[index].symbol))
          .font(RollColors.chordFont).foregroundColor(RollColors.chordText)
        context.drawLayer { layer in
          layer.clip(to: Path(CGRect(x: left, y: chordStrip.minY, width: end - left - 2, height: chordStrip.height)))
          layer.draw(symbol, at: CGPoint(x: left, y: chordStrip.midY), anchor: .leading)
        }
      }
      context.stroke(boundaries, with: .color(RollColors.ribbonBoundary), lineWidth: 1)

      // Bar numbers and pulse ticks: every bar is numbered unless bars are closer than 24 pt.
      let barWidth = geometry.x(tick: timing.barTicks)
      let every = max(1, Int((24 / max(barWidth, 1)).rounded(.up)))
      let marks = RollLines.lines(ticks: ticks, barTicks: timing.barTicks,
                                  pulseTicks: RollLines.pulseTicks(meter: snapshot.meter, ticksPerBeat: rate),
                                  gridTicks: timing.barTicks, geometry: geometry)
      var barTicks = Path()
      var pulseTicks = Path()
      for mark in marks {
        let px = x(mark.tick).rounded(.down) + 0.5
        if mark.kind == .bar {
          barTicks.move(to: CGPoint(x: px, y: 0))
          barTicks.addLine(to: CGPoint(x: px, y: 9))
          let bar = mark.tick / max(1, timing.barTicks)
          if bar % every == 0 {
            context.draw(Text("\(bar + 1)").font(.system(size: 9)).foregroundColor(.white.opacity(0.6)),
                         at: CGPoint(x: px + 3, y: 0), anchor: .topLeading)
          }
        } else {
          pulseTicks.move(to: CGPoint(x: px, y: 0))
          pulseTicks.addLine(to: CGPoint(x: px, y: 4))
        }
      }
      context.stroke(barTicks, with: .color(.white.opacity(0.6)), lineWidth: 1)
      context.stroke(pulseTicks, with: .color(.white.opacity(0.3)), lineWidth: 1)

      if endX >= 0, endX <= size.width {
        var end = Path()
        end.move(to: CGPoint(x: endX.rounded(.down) + 0.5, y: 0))
        end.addLine(to: CGPoint(x: endX.rounded(.down) + 0.5, y: size.height))
        context.stroke(end, with: .color(RollColors.endLine), style: StrokeStyle(lineWidth: 1, dash: [2, 2]))
      }
      // The line between the ribbon and the grid.
      context.fill(Path(CGRect(x: 0, y: size.height - 1, width: size.width, height: 1)), with: .color(RollColors.frame))
    }
    .accessibilityHidden(true)
  }
}

/// The playhead (2 pt white, the sequence bar's) and, over the grid, the insert marker (1 pt in the accent colour)
/// at the roll's horizontal scroll: two positioned rectangles over the ribbon's or the grid's canvas, so that a
/// transport push or a hover moves them without redrawing a canvas. Never hit-tested (`RollInputView` owns the
/// grid's events) and hidden from accessibility; the canvas beneath clips them to its frame.
struct RollMarkersOverlay: View {
  let geometry: RollGeometry
  let scrollX: CGFloat
  let ticksPerBeat: Int
  /// The playhead's beat (`Playhead.beat`), nil when hidden.
  let playheadBeat: Double?
  /// The insert marker's tick (`RollEditState.insertMarker`), nil for none.
  var insertMarker: Int? = nil

  var body: some View {
    ZStack(alignment: .topLeading) {
      Color.clear
      if let marker = insertMarker {
        let x = (geometry.x(tick: marker) - scrollX).rounded(.down) + 0.5
        Rectangle().fill(Color.accentColor.opacity(0.9)).frame(width: 1).offset(x: x - 0.5)
      }
      if let beat = playheadBeat {
        let x = geometry.x(tick: Int((beat * Double(ticksPerBeat)).rounded())) - scrollX
        Rectangle().fill(Color.white).frame(width: 2).offset(x: x - 1)
      }
    }
    .clipped()
    .allowsHitTesting(false)
    .accessibilityHidden(true)
  }
}

/// The roll's colours, alphas, sizes and fonts in one place (tracks design D14's visual language: the app's dark
/// ground, the sequence ruler's ticks and playhead, low-contrast lines, guide bands as the main information and notes
/// readable over any band).
enum RollColors {
  /// The grid's ground over the editor's navy (the roll placeholder's 25 % black, a little deeper).
  static let gridGround = Color.black.opacity(0.30)
  /// A black key's row: a subtle darker stripe, under the guide bands.
  static let blackKeyRow = Color.black.opacity(0.16)
  static let rowLine = Color.white.opacity(0.035)
  /// Rows shorter than this get no line of their own (the octave lines stay).
  static let minRowLineHeight: CGFloat = 10
  static let octaveLine = Color.white.opacity(0.10)
  static let gridLine = Color.white.opacity(0.045)
  static let beatLine = Color.white.opacity(0.09)
  static let barLine = Color.white.opacity(0.20)
  static let chordBoundary = Color.white.opacity(0.30)
  /// After the progression's end: a darker area, faint diagonals every `hatchSpacing` points, the dashed end line.
  static let afterEnd = Color.black.opacity(0.22)
  static let hatch = Color.white.opacity(0.07)
  static let hatchSpacing: CGFloat = 10
  static let endLine = Color.white.opacity(0.5)
  /// Notes: corner radius, the dark edge, an inactive note's faint fill, Velocity mode's bar, other tracks' outlines.
  static let noteCorner: CGFloat = 2.5
  static let noteEdge = Color.black.opacity(0.45)
  static let inactiveFill = 0.10
  static let velocityBar = Color.white.opacity(0.75)
  static let otherTrackOpacity = 0.45
  /// Degree labels: 9-pt monospaced, the root's a little brighter; none on a span narrower than `minLabelSpan`.
  static let labelFont = Font.system(size: 9, weight: .medium, design: .monospaced)
  static let rootLabel = Color.white.opacity(0.62)
  static let chordLabel = Color.white.opacity(0.42)
  static let minLabelSpan: CGFloat = 14
  /// The ribbon: its ground (the sequence bar's 4 % white), section tints, boundaries, chord symbols.
  static let ribbonGround = Color.white.opacity(0.04)
  static let sectionTint = 0.30
  static let ribbonBoundary = Color.white.opacity(0.35)
  static let chordFont = Font.system(size: 11, weight: .medium)
  static let chordText = Color.white.opacity(0.92)
  /// The keyboard column.
  static let whiteKey = Color(red: 0.80, green: 0.82, blue: 0.88)
  static let blackKey = Color(red: 0.10, green: 0.11, blue: 0.16)
  static let keySeam = Color.black.opacity(0.22)
  static let keyLabel = Color.black.opacity(0.55)
  static let keyLabelFont = Font.system(size: 9, weight: .medium)
  static let keyEdge = Color.black.opacity(0.35)
  /// The roll's outline and the ribbon's bottom line.
  static let frame = Color.white.opacity(0.10)
}
