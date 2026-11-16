import CoreGraphics
import Foundation

/// The roll's edit mode (design D14 toolbar): `notes` draws, moves, duplicates and resizes; `velocity` turns a
/// vertical drag on a note into a velocity change.
enum RollMode: Equatable {
  case notes, velocity
}

/// What the gesture machine reads besides the event, built by the view for every event from the CURRENT snapshot
/// and the open track's UI options (tracks design D14, D15): the open track's notes (only they are hit-testable),
/// its selected ids (the snapshot's `noteSelection` when it names this track, else none), the snapshot's
/// `revision` (0 when absent), the events (for the guides and play spans) and the timing, the geometry, and the
/// track's mode, grid and Snap pitch options.
struct RollContext {
  let trackId: Int
  let notes: TrackNotesJSON
  let selectedIds: Set<Int>
  let revision: UInt64
  let events: [EventJSON]
  let timing: RollTiming
  let geometry: RollGeometry
  var mode: RollMode
  var grid: RollGrid
  var snap: PitchSnap

  init(trackId: Int, notes: TrackNotesJSON, selectedIds: Set<Int>, revision: UInt64, events: [EventJSON],
       timing: RollTiming, geometry: RollGeometry, mode: RollMode = .notes, grid: RollGrid = .default,
       snap: PitchSnap = .default) {
    self.trackId = trackId
    self.notes = notes
    self.selectedIds = selectedIds
    self.revision = revision
    self.events = events
    self.timing = timing
    self.geometry = geometry
    self.mode = mode
    self.grid = grid
    self.snap = snap
  }

  /// The context of track `trackId` in `snapshot`; nil when the snapshot has no such track. `timing` is the
  /// snapshot's `RollTiming`, computed here when not given (a caller that keeps it per snapshot passes it, since its
  /// content end walks every track's notes).
  init?(snapshot: Snapshot, trackId: Int, timing: RollTiming? = nil, geometry: RollGeometry, mode: RollMode = .notes,
        grid: RollGrid = .default, snap: PitchSnap = .default) {
    guard let track = snapshot.track(id: trackId) else { return nil }
    self.init(trackId: trackId, notes: track.notes, selectedIds: track.selectedIds(in: snapshot.noteSelection),
              revision: snapshot.revision ?? 0, events: snapshot.events, timing: timing ?? RollTiming(snapshot: snapshot),
              geometry: geometry, mode: mode, grid: grid, snap: snap)
  }

  /// The note domain of this snapshot (`RollLimits(timing:)`).
  var limits: RollLimits { RollLimits(timing: timing) }

  /// `ids` in note order, ids that name no note dropped: the order `selectNotes` sends.
  func ordered(_ ids: Set<Int>) -> [Int] {
    notes.indices(ofIds: ids).map { notes.id[$0] }
  }

  /// The notes `ids` names, in note order.
  func notes(withIds ids: Set<Int>) -> [RollNote] {
    notes.indices(ofIds: ids).map { notes[$0] }
  }
}

/// A point on a note (tracks design D15): the note's position in the track's columns, its id, and the zone.
///
/// Decisions (D15 silent):
/// - Only the open track's notes are hit-testable; inactive (dashed) notes exactly like active ones.
/// - Draw order is note order and the TOPMOST note is the last in note order that contains the point, so the view
///   must draw the open track's notes in note order (later notes over earlier ones).
/// - A note's rectangle is `RollGeometry.noteRect`, half-open on both axes: x in `[start, end)`, and the row per
///   `RollGeometry` (a point on a row boundary belongs to the row below it on screen). Nothing is hit above pitch
///   127's row or below pitch 0's.
/// - Each edge zone is `min(4 pt, width / 4)` wide, so a narrow note always keeps a body (half its width).
struct RollHit: Equatable {
  enum Zone: Equatable {
    case body, startEdge, endEdge
  }

  let index: Int
  let id: Int
  let zone: Zone

  /// The widest an edge zone gets, in points.
  static let maxEdgeWidth: CGFloat = 4

  /// The topmost note of `notes` under content `point`, nil for empty space.
  static func hit(at point: CGPoint, notes: TrackNotesJSON, geometry: RollGeometry) -> RollHit? {
    guard point.x.isFinite, point.y >= 0, point.y < CGFloat(RollGeometry.pitchCount) * geometry.rowHeight else {
      return nil
    }
    let pitch = geometry.pitch(atY: point.y)
    for index in notes.indices.reversed() where notes.pitch[index] == pitch {
      let rect = geometry.noteRect(start: notes.start[index], length: notes.length[index], pitch: pitch)
      guard point.x >= rect.minX, point.x < rect.maxX else { continue }
      let edge = min(maxEdgeWidth, rect.width / 4)
      let zone: Zone = point.x < rect.minX + edge ? .startEdge : point.x >= rect.maxX - edge ? .endEdge : .body
      return RollHit(index: index, id: notes.id[index], zone: zone)
    }
    return nil
  }

  /// The topmost note of the context's track under `point`.
  static func hit(at point: CGPoint, context: RollContext) -> RollHit? {
    hit(at: point, notes: context.notes, geometry: context.geometry)
  }

  /// The ids of the notes a rubber band touches: every note whose half-open rectangle meets the CLOSED `band`, so a
  /// band of zero height (a purely horizontal drag) still selects along its row, and a band edge exactly on a row
  /// or note boundary takes the row or note that boundary belongs to.
  static func ids(inBand band: CGRect, context: RollContext) -> Set<Int> {
    var ids = Set<Int>()
    let notes = context.notes
    for index in notes.indices {
      let rect = context.geometry.noteRect(start: notes.start[index], length: notes.length[index], pitch: notes.pitch[index])
      if band.minX < rect.maxX, band.maxX >= rect.minX, band.minY < rect.maxY, band.maxY >= rect.minY {
        ids.insert(notes.id[index])
      }
    }
    return ids
  }
}

/// What the gesture machine asks the view to do (tracks design D15): send a command (`ChordLabState.send(command:
/// command.json)`), or drive the note preview through the existing preview path (one pitch, `kMsgTagPreview`, with
/// keep-alive). A held preview (`previewStart`) is ALWAYS followed by exactly one `previewStop` (on the up, on
/// `cancel`, on a revision cancel, on a lost up, or when a move changes the pitch: stop, then start the new pitch);
/// `previewOneShot` is a short preview the view times out itself (an insert, a velocity drag's release).
enum RollEffect: Equatable {
  case send(RollCommand)
  case previewStart(pitch: Int, velocity: Int)
  case previewStop
  case previewOneShot(pitch: Int, velocity: Int)
}

/// The gesture machine's state (tracks design D15): a persistent part that outlives gestures (`lastLength`,
/// `lastInsert`, `insertMarker`) and the gesture in progress. The view keeps one per open track, passes it to every
/// `RollEditLogic` call and draws from it: `previewNotes` (minus the snapshot's `hiddenIds`) during a drag, and
/// `rubberBand` and `insertMarker`.
struct RollEditState: Equatable {
  /// The last insert a click made (pitch, start tick): the double-click rules compare against it. Forgotten at the
  /// next single-click down (clickCount ≤ 1), so only the first click of the SAME double-click counts; AppKit's
  /// clickCount already encodes the double-click interval, so no timestamp is kept.
  struct Insert: Equatable {
    var pitch: Int
    var start: Int
  }

  /// What the down landed on, fixed for the gesture.
  enum Target: Equatable {
    case note(RollHit)
    case empty
  }

  enum DragKind: Equatable {
    case move, duplicate, resizeStart, resizeEnd, rubberBand, velocity
  }

  /// A press: everything captured at the down.
  struct Press: Equatable {
    var target: Target
    /// The down's content point in the down's geometry (`geometry`). The drag threshold, every delta and the rubber
    /// band's corner are measured from `origin(in:)`, this point in the CURRENT geometry.
    var at: CGPoint
    /// The geometry of the down: a zoom mid-gesture maps `at` to the same tick and row through `origin(in:)`.
    var geometry: RollGeometry
    var modifiers: RollModifiers
    var clickCount: Int
    var mode: RollMode
    var trackId: Int
    /// The snapshot revision at the down: every content command of the gesture carries it as `baseRevision`.
    var baseRevision: UInt64
    /// The selection before the down (an empty-space click deselects when it is not empty; ⇧-rubber band adds to it).
    var selection: Set<Int>
    /// The pressed note as it was at the down; nil on empty space.
    var anchor: RollNote?
    /// The notes a drag edits, in note order, as they were at the down: the selection AFTER the down (computed
    /// here, the echo may not have arrived). Empty when the press may not drag (empty space, a ⇧-down that toggled
    /// the note out).
    var group: [RollNote]
    /// The pitch of the held preview, nil when none is held.
    var heldPitch: Int?

    /// The down's point in the content coordinates of `current`: the same (fractional) tick and row, so a zoom
    /// during the gesture (`pxPerBeat` or `rowHeight` changed, D14) moves neither the drag's origin nor its delta.
    /// Exactly `at` while the geometry is the down's.
    func origin(in current: RollGeometry) -> CGPoint {
      guard current != geometry else { return at }
      return CGPoint(x: at.x / geometry.pxPerBeat * current.pxPerBeat, y: at.y / geometry.rowHeight * current.rowHeight)
    }
  }

  /// A drag in progress: its kind, the clamped delta, the local preview and the rubber band.
  struct Drag: Equatable {
    var kind: DragKind
    var dPitch = 0
    var dTick = 0
    var dVelocity = 0
    var preview: [RollNote] = []
    var band: CGRect? = nil
  }

  enum Gesture: Equatable {
    case idle
    case pressed(Press)
    case dragging(Press, Drag)
  }

  /// The length the last insert or resize left (design D15: an insert takes the last used length); nil until one.
  var lastLength: Int?
  var lastInsert: Insert?
  /// The insert marker (design D15 paste: a paste lands here, else after the selection's end, else at 0): the grid
  /// tick (`grid.snapDown`, bar-relative) of the last PLAIN click on empty space inside the progression (an up
  /// without a drag: the deselect click and the insert click, either mode). Nil before one; cleared by a press on a
  /// note (any zone, either mode, at the down), by a committed rubber band and by a plain click at or after the
  /// progression's end. A down alone, a drag in progress and a cancelled gesture leave it as it was.
  var insertMarker: Int?
  var gesture: Gesture = .idle

  init(lastLength: Int? = nil, lastInsert: Insert? = nil, insertMarker: Int? = nil, gesture: Gesture = .idle) {
    self.lastLength = lastLength
    self.lastInsert = lastInsert
    self.insertMarker = insertMarker
    self.gesture = gesture
  }

  /// The press in progress (pressed or dragging), nil when idle.
  var press: Press? {
    switch gesture {
    case .idle: return nil
    case .pressed(let press), .dragging(let press, _): return press
    }
  }

  /// The drag in progress, nil when idle or merely pressed.
  var drag: Drag? {
    if case .dragging(_, let drag) = gesture { return drag }
    return nil
  }

  var isIdle: Bool { gesture == .idle }

  /// The notes the view draws as the drag's local preview: the edited notes (move, resize, velocity) or the
  /// duplicated copies (carrying their originals' ids), as `RollNote` values; empty when no note drag is in
  /// progress.
  var previewNotes: [RollNote] { drag?.preview ?? [] }

  /// The snapshot notes the view leaves out while the preview replaces them: the group of a move, resize or
  /// velocity drag; empty for a duplicate (the originals stay) and when no note drag is in progress.
  var hiddenIds: Set<Int> {
    guard let drag, let press else { return [] }
    switch drag.kind {
    case .move, .resizeStart, .resizeEnd, .velocity: return Set(press.group.map(\.id))
    case .duplicate, .rubberBand: return []
    }
  }

  /// The rubber band in content coordinates while one is dragged, else nil (drawn only; it selects on the up).
  var rubberBand: CGRect? { drag?.band }
}

/// The piano roll's gesture machine (tracks design D15 "Gesture state machine"), pure: `handle` takes one
/// `RollPointerEvent`, the state and the current `RollContext` and returns the next state and the effects, in order.
/// `idle → pressed(target, at, modifiers, clickCount) → dragging(kind) → idle`, the target fixed at the down.
///
/// - **Down on a note** (any zone): sends `selectNotes` at once: the note alone; with ⇧ toggled into or out of the
///   selection; a press on an already selected note keeps the selection and sends nothing. Notes mode starts the
///   note's held preview. The drag group is the selection after this down, computed locally. Decisions: a ⇧-down
///   that toggles the note OUT neither previews nor drags; an edge press previews like a body press; velocity mode
///   has no edge zones.
/// - **Drag past 3 pt** from the down (velocity mode: 3 pt vertically) starts the drag: body → `move` (⌥ held AT THE
///   DOWN → `duplicate`), start/end edge → `resizeStart`/`resizeEnd`, velocity mode → `velocity`, empty space →
///   `rubberBand`. Once dragging, a return inside the threshold stays a drag (and commits nothing at zero delta).
/// - **Deltas and zoom:** every delta, the threshold and the rubber band's corner are measured from the down's point
///   mapped into the CURRENT geometry (`Press.origin(in:)`: the down's tick and row), so a zoom mid-gesture neither
///   makes the delta jump nor starts a drag by itself.
/// - **Move / duplicate:** pitch delta = the row under the pointer minus the row under the down; time delta from the
///   anchor (the pressed note): its new start = `grid.snapNearest(start + Δx in ticks)` (bar-relative), the delta
///   the difference (none at all while the pointer is at the down's x: a zero raw displacement never snaps, so an
///   off-grid note dragged back to its origin stays put), through the group clamp (`RollGroupMath.move`). When the clamp's upper bound bites (a start
///   would reach the progression's end, or an end `kMaxNoteTick`) and the grid is not Off, the anchor goes to the
///   last grid point the clamp allows (`grid.snapDown(anchor.start + clamped)`), unless that point is behind the
///   anchor (an off-grid anchor in its last cell): then the raw clamp (the last tick before the end), so a drag
///   right never moves left. With Snap pitch on, a SINGLE-note move or duplicate snaps its pitch by the guide of
///   the chord at its new start (`PlaySpans`) once it actually moves (the row or the clamped tick delta is not 0;
///   decision: a purely horizontal move of an out-of-set note snaps it too, a drag back to its origin does not);
///   a group moves by exact rows; then the clamp again for the pitch.
/// - **Resize:** the anchor's dragged edge snaps to the nearest grid point (not at a zero raw displacement); the shared delta through
///   `RollGroupMath.resize` (shortest ≥ `minNoteTicks`, start ≥ 0, starts before the end, end ≤ `kMaxNoteTick`).
/// - **Velocity:** 1 pt up = one step louder; the shared delta through `RollGroupMath.velocity` (1...127).
/// - **Up after a drag** commits: the preview is dropped and EXACTLY ONE command is sent, carrying `baseRevision` =
///   the down's revision: `updateNotes` (move, resize, velocity; edits carry only the changed fields, absolute
///   values), `insertNotes` (duplicate, the moved copies), or nothing when the clamped delta is zero; a rubber band
///   sends `selectNotes` of every note it touches (⇧ at the down: added to the selection before the down), or
///   nothing when those ids equal the selection at the down, and clears `insertMarker`. A resize
///   sets `lastLength` to the anchor's new length; a velocity commit also previews the anchor at its new velocity
///   (`previewOneShot`). The up's location is applied as a last drag first, so an up away from the last drag event
///   still counts.
/// - **Up without a drag:** on a note, clickCount 2 sends `removeNotes [that note]`, unless that note is the one the
///   first click of the same double-click inserted (its pitch and start equal `lastInsert`'s); decisions: exactly 2
///   (a third click would send a stale removal), in any zone and either mode. On empty space: with a selection
///   before the down, `selectNotes []` (deselect); with none, in notes mode, ONE note is inserted (any click count)
///   at the cell: start = `grid.snapDown` of the down's tick (bar-relative), never at or after `timing.endTick`
///   (the hatched area), pitch = the row (snapped by the guide at the start when Snap pitch is on), length =
///   `grid.insertLength(lastLength:...)` clamped into the domain, velocity 100, then `previewOneShot`; decisions: in
///   velocity mode an empty click never inserts; a second click of a double-click (clickCount ≥ 2) on empty space at
///   the same cell as the insert its first click made inserts nothing (the snapshot with the new note may not have
///   arrived).
/// - **The insert marker** (decision, D15 says only where a paste lands): set by a plain empty-space click inside
///   the progression to its grid tick, cleared by a note press, a committed rubber band and a plain click at or
///   after the end (see `RollEditState.insertMarker`), so after a band ⌘C ⌘V pastes after the selection.
/// - **Cancel** (`cancel` phase, or `snapshotChanged` with a revision other than the down's): the gesture and its
///   preview are dropped, a held preview stops, nothing is sent. A same-revision snapshot (a selection echo)
///   changes nothing. Decision: a `down` while not idle (a lost up) cancels the old gesture first, then starts the
///   new one.
/// - **Lane switch:** an event whose context names another track than the press (a gesture parked on a lane that
///   is no longer open) first cancels that gesture (its held preview stops, nothing is sent), then is handled as
///   if idle. The view must still send `cancel` itself when the open lane changes, so the old preview stops at once
///   rather than at the next event.
///
/// Depends on nothing but Foundation and CoreGraphics; unit-tested on whole event sequences by the `ChordLabUICore`
/// package (Tests/ChordLabUICoreTests/RollEditLogicTests.swift).
enum RollEditLogic {
  /// How far (in points) a press travels before it becomes a drag (design D15).
  static let dragThreshold: CGFloat = 3
  /// The velocity an insert takes (design D15).
  static let insertVelocity = 100

  typealias Press = RollEditState.Press
  typealias Drag = RollEditState.Drag

  /// The next state and the effects of `event`.
  static func handle(_ event: RollPointerEvent, state: RollEditState, context: RollContext)
    -> (RollEditState, [RollEffect]) {
    var state = state
    var parked: [RollEffect] = []
    if let press = state.press, press.trackId != context.trackId {
      (state, parked) = cancelled(state)
    }
    let (next, effects): (RollEditState, [RollEffect])
    switch event.phase {
    case .down: (next, effects) = down(event, state: state, context: context)
    case .drag: (next, effects) = drag(event, state: state, context: context)
    case .up: (next, effects) = up(event, state: state, context: context)
    case .cancel: (next, effects) = cancelled(state)
    }
    return (next, parked + effects)
  }

  /// The `cancel` phase without a context (the open lane changed, the window resigned key, the view went away): the
  /// gesture and its preview are dropped and a held preview stops; idle stays idle with no effects. The same as
  /// `handle` with a `cancel` event, for callers that have no context of the track at hand.
  static func cancel(_ state: RollEditState) -> (RollEditState, [RollEffect]) {
    cancelled(state)
  }

  /// A new snapshot arrived with `revision` (0 when it has none): a revision other than the one the press started
  /// on cancels the gesture and its preview (and stops a held preview); the same revision (a selection echo) and an
  /// idle state change nothing.
  static func snapshotChanged(revision: UInt64, state: RollEditState) -> (RollEditState, [RollEffect]) {
    guard let press = state.press, press.baseRevision != revision else { return (state, []) }
    return cancelled(state)
  }

  // MARK: - Phases

  /// Drops the gesture; a held preview stops.
  private static func cancelled(_ state: RollEditState) -> (RollEditState, [RollEffect]) {
    var next = state
    next.gesture = .idle
    return (next, state.press?.heldPitch != nil ? [.previewStop] : [])
  }

  private static func down(_ event: RollPointerEvent, state: RollEditState, context: RollContext)
    -> (RollEditState, [RollEffect]) {
    var (next, effects) = cancelled(state)
    if event.clickCount <= 1 { next.lastInsert = nil }
    let point = event.location
    var press = Press(target: .empty, at: point, geometry: context.geometry, modifiers: event.modifiers,
                      clickCount: event.clickCount, mode: context.mode, trackId: context.trackId, baseRevision: context.revision,
                      selection: context.selectedIds, anchor: nil, group: [], heldPitch: nil)
    if let hit = RollHit.hit(at: point, context: context) {
      let note = context.notes[hit.index]
      next.insertMarker = nil
      var selection = context.selectedIds
      var toggledOut = false
      if event.modifiers.shift {
        if selection.contains(note.id) {
          selection.remove(note.id)
          toggledOut = true
        } else {
          selection.insert(note.id)
        }
        effects.append(.send(.selectNotes(trackId: context.trackId, ids: context.ordered(selection))))
      } else if !selection.contains(note.id) {
        selection = [note.id]
        effects.append(.send(.selectNotes(trackId: context.trackId, ids: [note.id])))
      }
      press.target = .note(hit)
      press.anchor = note
      if !toggledOut {
        press.group = context.notes(withIds: selection)
        if context.mode == .notes {
          press.heldPitch = note.pitch
          effects.append(.previewStart(pitch: note.pitch, velocity: note.velocity))
        }
      }
    }
    next.gesture = .pressed(press)
    return (next, effects)
  }

  private static func drag(_ event: RollPointerEvent, state: RollEditState, context: RollContext)
    -> (RollEditState, [RollEffect]) {
    switch state.gesture {
    case .idle:
      return (state, [])
    case .pressed(let press):
      guard let kind = dragKind(press, at: event.location, geometry: context.geometry) else { return (state, []) }
      return update(press, kind: kind, at: event.location, state: state, context: context)
    case .dragging(let press, let drag):
      return update(press, kind: drag.kind, at: event.location, state: state, context: context)
    }
  }

  private static func up(_ event: RollPointerEvent, state: RollEditState, context: RollContext)
    -> (RollEditState, [RollEffect]) {
    // The up's own location counts as a last drag (an up past the threshold with no drag event before it too).
    var current = state
    switch state.gesture {
    case .idle:
      return (state, [])
    case .pressed(let press):
      if let kind = dragKind(press, at: event.location, geometry: context.geometry) {
        current = update(press, kind: kind, at: event.location, state: state, context: context).0
      }
    case .dragging(let press, let drag):
      current = update(press, kind: drag.kind, at: event.location, state: state, context: context).0
    }
    guard let press = current.press else { return (state, []) }
    var next = current
    next.gesture = .idle
    var effects: [RollEffect] = press.heldPitch != nil ? [.previewStop] : []
    if let drag = current.drag {
      effects += commit(press, drag, state: &next, context: context)
    } else {
      effects += click(press, state: &next, context: context)
    }
    return (next, effects)
  }

  // MARK: - Dragging

  /// The drag a press turns into at `point` (content coordinates of `geometry`), nil while it is still a click (or
  /// may not drag).
  private static func dragKind(_ press: Press, at point: CGPoint, geometry: RollGeometry) -> RollEditState.DragKind? {
    let origin = press.origin(in: geometry)
    let dx = point.x - origin.x
    let dy = point.y - origin.y
    let travelled = (dx * dx + dy * dy).squareRoot() > dragThreshold
    switch press.target {
    case .empty:
      return travelled ? .rubberBand : nil
    case .note(let hit):
      guard !press.group.isEmpty else { return nil }
      if press.mode == .velocity { return abs(dy) > dragThreshold ? .velocity : nil }
      guard travelled else { return nil }
      switch hit.zone {
      case .body: return press.modifiers.option ? .duplicate : .move
      case .startEdge: return .resizeStart
      case .endEdge: return .resizeEnd
      }
    }
  }

  /// The ticks a horizontal travel of `dx` points stands for, rounded to the nearest tick.
  private static func ticks(_ dx: CGFloat, geometry: RollGeometry) -> Int {
    let value = Double(dx) / Double(geometry.pxPerBeat) * Double(geometry.ticksPerBeat)
    guard value.isFinite else { return 0 }
    return Int(min(max(value, -1e12), 1e12).rounded())
  }

  /// The drag of `kind` with the pointer at `point`: the clamped delta, the preview, the band; a move whose
  /// anchor changes pitch stops the held preview and starts the new pitch.
  private static func update(_ press: Press, kind: RollEditState.DragKind, at point: CGPoint, state: RollEditState,
                             context: RollContext) -> (RollEditState, [RollEffect]) {
    var press = press
    var drag = Drag(kind: kind)
    var effects: [RollEffect] = []
    let timing = context.timing
    let limits = context.limits
    let origin = press.origin(in: context.geometry)
    let dxTicks = ticks(point.x - origin.x, geometry: context.geometry)
    /// The delta that puts the anchor edge at `tick` on the grid after the pointer travelled `dxTicks`; a zero
    /// travel is no time change at all, so a drag back to where it began leaves an off-grid note where it is (D15).
    func snappedDelta(from tick: Int) -> Int {
      guard dxTicks != 0 else { return 0 }
      return context.grid.snapNearest(tick + dxTicks, barTicks: timing.barTicks, ticksPerBeat: timing.ticksPerBeat) - tick
    }
    switch kind {
    case .rubberBand:
      drag.band = CGRect(x: min(origin.x, point.x), y: min(origin.y, point.y), width: abs(point.x - origin.x),
                         height: abs(point.y - origin.y))
    case .velocity:
      let wanted = Int(min(max(Double(origin.y - point.y), -1000), 1000).rounded())
      drag.dVelocity = RollGroupMath.velocity(press.group, dVelocity: wanted)
      drag.preview = RollGroupMath.applying(press.group, dVelocity: drag.dVelocity)
    case .move, .duplicate:
      guard let anchor = press.anchor else { break }
      var dPitch = context.geometry.pitch(atY: point.y) - context.geometry.pitch(atY: origin.y)
      let dTick = moveTicks(press.group, anchor: anchor, wanted: snappedDelta(from: anchor.start),
                            context: context)
      if press.group.count == 1, context.snap != .off, dPitch != 0 || dTick != 0 {
        let guide = PlaySpans.event(atTick: anchor.start + dTick, in: context.events, ticksPerBeat: timing.ticksPerBeat)?.guide
        dPitch = context.snap.snap(pitch: min(max(anchor.pitch + dPitch, 0), 127), guide: guide) - anchor.pitch
      }
      let delta = RollGroupMath.move(press.group, dPitch: dPitch, dTick: dTick, limits: limits)
      drag.dPitch = delta.dPitch
      drag.dTick = delta.dTick
      drag.preview = RollGroupMath.applying(press.group, dPitch: delta.dPitch, dStart: delta.dTick)
      let pitch = anchor.pitch + delta.dPitch
      if let held = press.heldPitch, held != pitch {
        effects += [.previewStop, .previewStart(pitch: pitch, velocity: anchor.velocity)]
        press.heldPitch = pitch
      }
    case .resizeStart:
      guard let anchor = press.anchor else { break }
      drag.dTick = RollGroupMath.resize(press.group, edge: .start, dTick: snappedDelta(from: anchor.start),
                                        limits: limits)
      drag.preview = RollGroupMath.applying(press.group, dStart: drag.dTick, dLength: -drag.dTick)
    case .resizeEnd:
      guard let anchor = press.anchor else { break }
      drag.dTick = RollGroupMath.resize(press.group, edge: .end, dTick: snappedDelta(from: anchor.end),
                                        limits: limits)
      drag.preview = RollGroupMath.applying(press.group, dLength: drag.dTick)
    }
    var next = state
    next.gesture = .dragging(press, drag)
    return (next, effects)
  }

  /// The shared time delta of a move whose anchor wants to move by `wanted` (already on the grid): the group clamp,
  /// and when its upper bound bites on a grid other than Off, the anchor's last grid point the clamp allows
  /// (bar-relative `snapDown`), unless that point is behind the anchor (then the raw clamp). Sound: the result lies
  /// in `0...clamped`, inside the clamp's range (an interval containing 0), so it satisfies every note's bound.
  private static func moveTicks(_ group: [RollNote], anchor: RollNote, wanted: Int, context: RollContext) -> Int {
    let clamped = RollGroupMath.move(group, dPitch: 0, dTick: wanted, limits: context.limits).dTick
    guard clamped < wanted, context.grid != .off else { return clamped }
    let timing = context.timing
    let onGrid = context.grid.snapDown(anchor.start + clamped, barTicks: timing.barTicks,
                                       ticksPerBeat: timing.ticksPerBeat) - anchor.start
    return onGrid >= 0 ? onGrid : clamped
  }

  // MARK: - Committing

  /// The one command a finished drag sends (none at zero delta), and its effect on the persistent state.
  private static func commit(_ press: Press, _ drag: Drag, state: inout RollEditState, context: RollContext)
    -> [RollEffect] {
    let trackId = press.trackId
    let base = press.baseRevision
    switch drag.kind {
    case .rubberBand:
      state.insertMarker = nil
      guard let band = drag.band else { return [] }
      var ids = RollHit.ids(inBand: band, context: context)
      if press.modifiers.shift { ids.formUnion(press.selection) }
      guard ids != press.selection else { return [] }
      return [.send(.selectNotes(trackId: trackId, ids: context.ordered(ids)))]
    case .move:
      let edits = RollGroupMath.moveEdits(press.group, dPitch: drag.dPitch, dTick: drag.dTick)
      return edits.isEmpty ? [] : [.send(.updateNotes(trackId: trackId, edits: edits, baseRevision: base))]
    case .duplicate:
      guard drag.dPitch != 0 || drag.dTick != 0 else { return [] }
      let copies = drag.preview.map { RollNoteSpec(pitch: $0.pitch, start: $0.start, length: $0.length, velocity: $0.velocity) }
      return [.send(.insertNotes(trackId: trackId, notes: copies, baseRevision: base))]
    case .resizeStart, .resizeEnd:
      let edge: RollGroupMath.Edge = drag.kind == .resizeStart ? .start : .end
      let edits = RollGroupMath.resizeEdits(press.group, edge: edge, dTick: drag.dTick)
      guard !edits.isEmpty, let anchor = press.anchor else { return [] }
      state.lastLength = edge == .start ? anchor.length - drag.dTick : anchor.length + drag.dTick
      return [.send(.updateNotes(trackId: trackId, edits: edits, baseRevision: base))]
    case .velocity:
      let edits = RollGroupMath.velocityEdits(press.group, dVelocity: drag.dVelocity)
      guard !edits.isEmpty, let anchor = press.anchor else { return [] }
      return [.send(.updateNotes(trackId: trackId, edits: edits, baseRevision: base)),
              .previewOneShot(pitch: anchor.pitch, velocity: anchor.velocity + drag.dVelocity)]
    }
  }

  /// What an up without a drag does: a double-click delete, a deselect, or an insert.
  private static func click(_ press: Press, state: inout RollEditState, context: RollContext) -> [RollEffect] {
    switch press.target {
    case .note:
      guard press.clickCount == 2, let anchor = press.anchor else { return [] }
      if let insert = state.lastInsert, insert.pitch == anchor.pitch, insert.start == anchor.start { return [] }
      return [.send(.removeNotes(trackId: press.trackId, ids: [anchor.id], baseRevision: press.baseRevision))]
    case .empty:
      let timing = context.timing
      let tick = context.geometry.tick(atX: press.origin(in: context.geometry).x)
      state.insertMarker = tick < timing.endTick
        ? context.grid.snapDown(tick, barTicks: timing.barTicks, ticksPerBeat: timing.ticksPerBeat) : nil
      if !press.selection.isEmpty { return [.send(.selectNotes(trackId: press.trackId, ids: []))] }
      guard press.mode == .notes, let spec = insertion(at: press.origin(in: context.geometry), lastLength: state.lastLength, context: context) else {
        return []
      }
      if press.clickCount >= 2, let insert = state.lastInsert, insert.pitch == spec.pitch, insert.start == spec.start {
        return []
      }
      state.lastLength = spec.length
      state.lastInsert = RollEditState.Insert(pitch: spec.pitch, start: spec.start)
      return [.send(.insertNotes(trackId: press.trackId, notes: [spec], baseRevision: press.baseRevision)),
              .previewOneShot(pitch: spec.pitch, velocity: spec.velocity)]
    }
  }

  /// The note an empty-space click at content `point` inserts, nil at or after the progression's end (or where no
  /// note fits before `kMaxNoteTick`).
  static func insertion(at point: CGPoint, lastLength: Int?, context: RollContext) -> RollNoteSpec? {
    let timing = context.timing
    let limits = context.limits
    let tick = context.geometry.tick(atX: point.x)
    guard tick < timing.endTick else { return nil }
    let start = context.grid.snapDown(tick, barTicks: timing.barTicks, ticksPerBeat: timing.ticksPerBeat)
    guard limits.maxNoteTick - start >= limits.minNoteTicks else { return nil }
    var pitch = context.geometry.pitch(atY: point.y)
    if context.snap != .off {
      let guide = PlaySpans.event(atTick: start, in: context.events, ticksPerBeat: timing.ticksPerBeat)?.guide
      pitch = context.snap.snap(pitch: pitch, guide: guide)
    }
    let length = context.grid.insertLength(lastLength: lastLength, minNoteTicks: limits.minNoteTicks,
                                           ticksPerBeat: timing.ticksPerBeat)
    return RollNoteSpec(pitch: pitch, start: start, length: min(length, limits.maxNoteTick - start),
                        velocity: insertVelocity)
  }
}
