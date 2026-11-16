import CoreGraphics
import Foundation

/// One track's piano roll view state (tracks design D14: "Each track's scroll, zoom, grid, snap and mode are
/// UI-only, remembered per track id"): where the grid is scrolled (`scroll`, content coordinates of the viewport's
/// top-left, see `RollGeometry`), the zoom (`pxPerBeat`, `rowHeight`), the toolbar's Grid, Snap pitch, Mode and
/// Other tracks, the gesture machine's state (`editState`, `RollEditLogic`; plan Task 18 feeds it), and whether
/// the first open has scrolled to the role's register yet. Never saved and never sent to the engine.
///
/// Every scroll it stores is clamped to the content (`RollGeometry.clampedScroll`): the progression, or the latest
/// note end past it (`RollTiming.contentEndTick`), plus one bar of the meter (`RollTiming.barTicks`, like the
/// strip's trailing slack) wide, the 128 rows tall. `scroll` is read-only outside: every write goes through
/// `fit`, `setScroll`, `scroll(by:)` or a zoom, which clamp. The callers pass the viewport and the snapshot's timing
/// in, so a resize, a zoom or a shorter progression (`fit`) always lands the scroll inside the content.
///
/// **First open** (design D2, D14): the first `fit` with a real viewport (both sides positive) scrolls to bar 1 and
/// centres the role's register (`RollGeometry.initialScroll(register:viewport:)`), once; a later role change
/// does not scroll again, and a scroll the user (or the input view) set before that counts as the first open.
///
/// Depends on nothing but CoreGraphics and Foundation; unit-tested by the `ChordLabUICore` package
/// (Tests/ChordLabUICoreTests/RollViewStateTests.swift).
struct RollViewState: Equatable {
  /// Content coordinates of the viewport's top-left; always clamped (see the type comment).
  private(set) var scroll: CGPoint = .zero
  /// Horizontal zoom, `RollGeometry.pxPerBeatRange` (clamped through `RollGeometry`).
  private(set) var pxPerBeat: CGFloat = RollGeometry.defaultPxPerBeat
  /// Vertical zoom, `RollGeometry.rowHeightRange`.
  private(set) var rowHeight: CGFloat = RollGeometry.defaultRowHeight
  var grid: RollGrid = .default
  var snap: PitchSnap = .default
  var mode: RollMode = .notes
  /// Whether the other tracks' notes are drawn as faint outlines (design D14: a toggle, default on).
  var showsOtherTracks = true
  /// The gesture machine's state: the drag preview, the hidden notes, the rubber band and the insert marker the
  /// canvas draws.
  var editState = RollEditState()
  /// Whether the first open has scrolled to the register.
  private(set) var initialScrollApplied = false

  init() {}

  /// The geometry of this zoom on the snapshot's tick grid.
  func geometry(ticksPerBeat: Int) -> RollGeometry {
    RollGeometry(pxPerBeat: pxPerBeat, rowHeight: rowHeight, ticksPerBeat: ticksPerBeat)
  }

  /// The roll's content: the content end (`RollTiming.contentEndTick`: the progression's end, or the latest note
  /// end of any track when a note runs past it) plus one bar of slack, never narrower than the viewport, 128 rows
  /// tall (`RollGeometry.contentSize`).
  static func contentSize(geometry: RollGeometry, timing: RollTiming, viewport: CGSize) -> CGSize {
    geometry.contentSize(endTick: timing.contentEndTick, slackTicks: timing.barTicks, viewport: viewport)
  }

  /// After a measurement or a snapshot: the first open's scroll when it is due (see the type comment), then the
  /// scroll clamped to the content of this viewport and timing.
  mutating func fit(viewport: CGSize, timing: RollTiming, role: TrackRole) {
    let geometry = geometry(ticksPerBeat: timing.ticksPerBeat)
    if !initialScrollApplied, viewport.width > 0, viewport.height > 0 {
      scroll = geometry.initialScroll(register: role.register, viewport: viewport)
      initialScrollApplied = true
    }
    adopt(geometry, scroll: scroll, viewport: viewport, timing: timing)
  }

  /// Sets the scroll, clamped; counts as the first open.
  mutating func setScroll(_ scroll: CGPoint, viewport: CGSize, timing: RollTiming) {
    initialScrollApplied = true
    adopt(geometry(ticksPerBeat: timing.ticksPerBeat), scroll: scroll, viewport: viewport, timing: timing)
  }

  /// Moves the scroll by `delta` points (a scroll wheel's content delta), clamped.
  mutating func scroll(by delta: CGSize, viewport: CGSize, timing: RollTiming) {
    setScroll(CGPoint(x: scroll.x + delta.width, y: scroll.y + delta.height), viewport: viewport, timing: timing)
  }

  /// The toolbar's +: time zoom ×2 about the viewport's centre (`RollGeometry.zoomIn`), then clamped. Nothing at
  /// the limit.
  mutating func zoomIn(viewport: CGSize, timing: RollTiming) {
    let zoomed = geometry(ticksPerBeat: timing.ticksPerBeat).zoomIn(viewport: viewport, scroll: scroll)
    adopt(zoomed.geometry, scroll: zoomed.scroll, viewport: viewport, timing: timing)
  }

  /// The toolbar's −: time zoom ÷2 about the viewport's centre, then clamped.
  mutating func zoomOut(viewport: CGSize, timing: RollTiming) {
    let zoomed = geometry(ticksPerBeat: timing.ticksPerBeat).zoomOut(viewport: viewport, scroll: scroll)
    adopt(zoomed.geometry, scroll: zoomed.scroll, viewport: viewport, timing: timing)
  }

  /// Time zoom by `factor` keeping the tick under view x `aboutViewX` in place where the content allows (⇧⌥-scroll
  /// and pinch, plan Task 18), then clamped.
  mutating func zoom(horizontally factor: CGFloat, aboutViewX: CGFloat, viewport: CGSize, timing: RollTiming) {
    let zoomed = geometry(ticksPerBeat: timing.ticksPerBeat)
      .zoomedHorizontally(by: factor, aboutViewX: aboutViewX, scroll: scroll)
    adopt(zoomed.geometry, scroll: zoomed.scroll, viewport: viewport, timing: timing)
  }

  /// Row zoom by `factor` keeping the row under view y `aboutViewY` in place where the content allows (⌥-scroll,
  /// plan Task 18), then clamped.
  mutating func zoom(vertically factor: CGFloat, aboutViewY: CGFloat, viewport: CGSize, timing: RollTiming) {
    let zoomed = geometry(ticksPerBeat: timing.ticksPerBeat)
      .zoomedVertically(by: factor, aboutViewY: aboutViewY, scroll: scroll)
    adopt(zoomed.geometry, scroll: zoomed.scroll, viewport: viewport, timing: timing)
  }

  private mutating func adopt(_ geometry: RollGeometry, scroll: CGPoint, viewport: CGSize, timing: RollTiming) {
    pxPerBeat = geometry.pxPerBeat
    rowHeight = geometry.rowHeight
    let content = Self.contentSize(geometry: geometry, timing: timing, viewport: viewport)
    self.scroll = geometry.clampedScroll(scroll, viewport: viewport, contentSize: content)
  }
}

/// Every track's `RollViewState` by track id, plus the roll's viewport (the grid's size, the same for every track
/// since the roll takes the same place in the editor): what `ChordLabState` keeps for the rolls (tracks design D14).
/// UI-only and never saved.
///
/// An entry is made by the roll's first measurement of that track (`setViewport`, from the grid's
/// `onGeometryChange`), which is also its first open; a snapshot (`snapshotApplied`) drops the entries of removed
/// tracks, clears insert markers the progression no longer reaches and re-fits the open track's (its content may
/// have shrunk). An id without an entry reads the defaults.
///
/// Depends on nothing but CoreGraphics and Foundation; unit-tested by the `ChordLabUICore` package
/// (Tests/ChordLabUICoreTests/RollViewStateTests.swift).
struct RollViewStates: Equatable {
  private var byTrack: [Int: RollViewState] = [:]
  /// The grid's size as last measured; zero before the first roll appears.
  private(set) var viewport: CGSize = .zero

  init() {}

  /// Track `trackId`'s state; the defaults for a track without one yet.
  subscript(trackId: Int) -> RollViewState { byTrack[trackId] ?? RollViewState() }

  /// The ids with an entry, ascending.
  var trackIds: [Int] { byTrack.keys.sorted() }

  /// The roll of track `trackId` measured its grid: stores the viewport and fits that track (its first open when
  /// this is the first measurement with a real size).
  mutating func setViewport(_ viewport: CGSize, trackId: Int, role: TrackRole, timing: RollTiming) {
    self.viewport = viewport
    byTrack[trackId, default: RollViewState()].fit(viewport: viewport, timing: timing, role: role)
  }

  /// Changes track `trackId`'s state with the current viewport and `timing` (a setter, the toolbar, the input
  /// view); makes the entry when there is none.
  mutating func edit(trackId: Int, timing: RollTiming, _ change: (inout RollViewState, CGSize, RollTiming) -> Void) {
    change(&byTrack[trackId, default: RollViewState()], viewport, timing)
  }

  /// After a snapshot: drops the entries of tracks it no longer has, clears every remaining track's insert marker at
  /// or after the progression's new end (`timing.endTick`: no chord owns that tick, so a paste there would be refused,
  /// and a click there sets no marker either; the open track's and the parked ones', which paste there once
  /// reopened), and re-fits the open track's entry (when it has one) to the new content; a role change keeps the
  /// scroll (the first open is done).
  mutating func snapshotApplied(tracks: [TrackJSON], timing: RollTiming, openTrackId: Int?) {
    let ids = Set(tracks.map(\.id))
    byTrack = byTrack.filter { ids.contains($0.key) }
    for (trackId, state) in byTrack {
      if let marker = state.editState.insertMarker, marker >= timing.endTick {
        byTrack[trackId]?.editState.insertMarker = nil
      }
    }
    if let openTrackId, let track = tracks.first(where: { $0.id == openTrackId }), byTrack[openTrackId] != nil {
      byTrack[openTrackId]?.fit(viewport: viewport, timing: timing, role: track.role)
    }
  }
}

// MARK: - The input view's seam (plan Task 18)

extension RollViewStates {
  /// Track `trackId`'s view point (the grid's own coordinates, top-left origin) in content coordinates, through the
  /// track's CURRENT zoom and scroll (`RollGeometry.contentPoint`): what `RollInputView` turns every event's location
  /// into at the moment of the event.
  func contentPoint(viewPoint: CGPoint, trackId: Int, ticksPerBeat: Int) -> CGPoint {
    let state = self[trackId]
    return state.geometry(ticksPerBeat: ticksPerBeat).contentPoint(viewPoint: viewPoint, scroll: state.scroll)
  }

  /// Runs one pointer event of track `trackId`'s roll through the gesture machine (`RollEditLogic.handle`) with the
  /// context built from `snapshot` (its notes, selection and revision) and the track's own geometry, mode, grid and
  /// Snap pitch, stores the next edit state (the drag preview, hidden notes, rubber band and insert marker the roll
  /// draws) and returns the effects, in order, for the one effects seam (`ChordLabState.performRollEffects`). A
  /// track the snapshot no longer has gets a cancel (its held preview stops) whatever the event.
  mutating func handle(_ event: RollPointerEvent, trackId: Int, snapshot: Snapshot, timing: RollTiming) -> [RollEffect] {
    let state = self[trackId]
    guard event.phase != .cancel,
          let context = RollContext(snapshot: snapshot, trackId: trackId, timing: timing,
                                    geometry: state.geometry(ticksPerBeat: timing.ticksPerBeat),
                                    mode: state.mode, grid: state.grid, snap: state.snap) else {
      return cancelGesture(trackId: trackId)
    }
    let (next, effects) = RollEditLogic.handle(event, state: state.editState, context: context)
    byTrack[trackId, default: state].editState = next
    return effects
  }

  /// Cancels track `trackId`'s gesture (`RollEditLogic.cancel`): the open lane left it, its window resigned key,
  /// its roll went away. Returns the effects (a held preview's stop); nothing, and no entry made, when the track has
  /// no state or its state is idle.
  mutating func cancelGesture(trackId: Int) -> [RollEffect] {
    guard let state = byTrack[trackId], !state.editState.isIdle else { return [] }
    let (next, effects) = RollEditLogic.cancel(state.editState)
    byTrack[trackId]?.editState = next
    return effects
  }

  /// A snapshot with `revision` arrived: every track's stored gesture, not only the open one's, goes through
  /// `RollEditLogic.snapshotChanged`, so a different revision cancels a stale gesture everywhere (a press parked on
  /// a lane that is no longer open included) and the same revision (a selection echo) changes nothing. Returns the
  /// tracks whose state produced effects (held previews' stops), by ascending id.
  mutating func snapshotChanged(revision: UInt64) -> [(trackId: Int, effects: [RollEffect])] {
    var result: [(trackId: Int, effects: [RollEffect])] = []
    for trackId in trackIds {
      guard let state = byTrack[trackId], !state.editState.isIdle else { continue }
      let (next, effects) = RollEditLogic.snapshotChanged(revision: revision, state: state.editState)
      if next != state.editState { byTrack[trackId]?.editState = next }
      if !effects.isEmpty { result.append((trackId: trackId, effects: effects)) }
    }
    return result
  }

  /// The cursor over track `trackId`'s view point (`RollCursor`): the resize cursor over a note's edge in Notes mode
  /// (`RollHit.hit` on the track's notes at its zoom and scroll), the arrow elsewhere and for a track the snapshot
  /// lacks.
  func cursor(atView viewPoint: CGPoint, trackId: Int, snapshot: Snapshot, timing: RollTiming) -> RollCursor {
    let state = self[trackId]
    guard state.mode == .notes, let track = snapshot.track(id: trackId) else { return .arrow }
    let geometry = state.geometry(ticksPerBeat: timing.ticksPerBeat)
    let point = geometry.contentPoint(viewPoint: viewPoint, scroll: state.scroll)
    return RollCursor.cursor(zone: RollHit.hit(at: point, notes: track.notes, geometry: geometry)?.zone, mode: state.mode)
  }
}
