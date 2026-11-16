import Foundation

/// Which of the roll toolbar's nudge buttons are enabled (`RollNudge.availability`): a direction the open track's
/// note selection can move in.
struct RollNudgeAvailability: Equatable {
  var left = false
  var right = false
  var up = false
  var down = false

  /// Every button disabled (no selection on the open track).
  static let none = RollNudgeAvailability()

  subscript(direction: ArrowDirection) -> Bool {
    switch direction {
    case .left: return left
    case .right: return right
    case .up: return up
    case .down: return down
    }
  }
}

/// The roll toolbar's nudge buttons ◀ ▶ ▲ ▼ (tracks design D15 "Keyboard": the fallback for hosts that keep the arrow
/// keys; plan Task 19), and the arrow keys' nudge (plan Task 20, with `octave` for ⇧↑/⇧↓): the open track's selected
/// notes (the snapshot's `noteSelection` when it names this track) moved by one semitone or one grid step as ONE
/// shared delta through `RollGroupMath.nudge` (the group clamp of D15 "Group geometry"), sent as one `updateNotes`
/// (one undo step) carrying the snapshot's revision as `baseRevision`, so a nudge computed from an older snapshot is
/// refused like a stale drag.
///
/// Depends on nothing but Foundation and Core's snapshot, timing, grid and group math; unit-tested by the
/// `ChordLabUICore` package (Tests/ChordLabUICoreTests/RollNudgeTests.swift).
enum RollNudge {
  /// Track `trackId`'s selected notes in note order: empty when the snapshot's selection names another track, none,
  /// or only ids that name no note.
  static func selectedNotes(trackId: Int, snapshot: Snapshot) -> [RollNote] {
    guard let track = snapshot.track(id: trackId) else { return [] }
    let notes = track.notes
    return notes.indices(ofIds: track.selectedIds(in: snapshot.noteSelection)).map { notes[$0] }
  }

  /// The one command a nudge of `direction` sends, nil when there is no selection on the track or the clamp leaves
  /// nothing to move (the button is then disabled). Horizontally the step is `grid`'s (`RollGrid.ticks`; Off steps 10
  /// ticks); `octave` makes ↑/↓ twelve semitones, a whole octave or nothing.
  static func command(_ direction: ArrowDirection, octave: Bool = false, trackId: Int, snapshot: Snapshot,
                      timing: RollTiming, grid: RollGrid) -> RollCommand? {
    let notes = selectedNotes(trackId: trackId, snapshot: snapshot)
    guard let edits = RollGroupMath.nudge(notes, direction: direction, octave: octave,
                                          gridTicks: grid.ticks(ticksPerBeat: timing.ticksPerBeat),
                                          limits: RollLimits(timing: timing)) else { return nil }
    return .updateNotes(trackId: trackId, edits: edits, baseRevision: snapshot.revision ?? 0)
  }

  /// Which nudge buttons are enabled for track `trackId`: each direction whose `command` is not nil.
  static func availability(trackId: Int, snapshot: Snapshot, timing: RollTiming, grid: RollGrid) -> RollNudgeAvailability {
    let notes = selectedNotes(trackId: trackId, snapshot: snapshot)
    guard !notes.isEmpty else { return .none }
    let limits = RollLimits(timing: timing)
    let step = grid.ticks(ticksPerBeat: timing.ticksPerBeat)
    func can(_ direction: ArrowDirection) -> Bool {
      RollGroupMath.nudge(notes, direction: direction, octave: false, gridTicks: step, limits: limits) != nil
    }
    return RollNudgeAvailability(left: can(.left), right: can(.right), up: can(.up), down: can(.down))
  }
}
