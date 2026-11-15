import Foundation

/// The note domain a group edit is clamped into (tracks design D1, D3): every note keeps pitch 0...127, velocity
/// 1...127, start ≥ 0, length ≥ `minNoteTicks`, end ≤ `maxNoteTick`, and a start BEFORE `endTick`, because the
/// engine refuses a start the progression does not own (ownership is half-open, D3: a start at or after the
/// progression's end has no chord).
struct RollLimits: Equatable {
  /// The engine's `kMaxNoteTick` (engine/include/chordlab/tracks.h): `kMaxChords × kMaxLengthBeats × ticks per
  /// beat` = 256 × 64 × 960 = 15,728,640, the end no note may pass.
  static let engineMaxNoteTick = 256 * 64 * 960
  /// The engine's `kMinTrackNoteTicks` (tracks.h): 30 ticks, a 1/32 beat. The snapshot's `trackLimits.minNoteTicks`
  /// carries the same number; this constant is the fallback for code with no snapshot (the clipboard's decode).
  static let engineMinNoteTicks = 30

  /// The shortest note (the snapshot's `trackLimits.minNoteTicks`).
  let minNoteTicks: Int
  /// The end no note may pass (`kMaxNoteTick`).
  let maxNoteTick: Int
  /// The progression's end tick (`RollTiming.endTick`): every start stays below it.
  let endTick: Int

  init(minNoteTicks: Int, endTick: Int, maxNoteTick: Int = RollLimits.engineMaxNoteTick) {
    self.minNoteTicks = minNoteTicks
    self.endTick = endTick
    self.maxNoteTick = maxNoteTick
  }

  /// The limits of a snapshot's timing: its `minNoteTicks` and `endTick`, the engine's `kMaxNoteTick`.
  init(timing: RollTiming) {
    self.init(minNoteTicks: timing.minNoteTicks, endTick: timing.endTick)
  }
}

/// The shared-delta clamps of a group edit (tracks design D15 "Group geometry"), used by the gesture machine
/// (`RollEditLogic`) and the arrow nudges (Task 20). A group moves, resizes or changes velocity by ONE delta for
/// every note, clamped against the intersection of the notes' valid ranges (`RollLimits`), so intervals, spacing
/// and velocity differences survive at the edges: pitches [126, 127] moved up stay [126, 127], starts [0, 480]
/// moved left stay put.
///
/// A bound a note ALREADY breaks never forces a move: each clamp's range always contains 0, so such a note keeps
/// its place and the delta can only not make it worse (an inactive note of a shortened last chord can start at or
/// after the progression's end, design D3; the group then cannot move right but can move left). An empty group
/// moves by 0.
///
/// Accepted limitation: because the range always contains 0, a group holding such an inactive note whose start is
/// at or after `endTick` can still take a delta that leaves that start unowned (a pitch move, a resize, a velocity
/// change, a move left that stays past the end, or a duplicate, whose copy starts there too); the engine refuses
/// that `updateNotes` / `insertNotes` whole and the roll shows the refusal like any other, leaving every note as it
/// was. The roll does not pre-filter such groups.
///
/// Depends on nothing but Foundation and Core's `ArrowDirection` (ArrowDirection.swift); unit-tested by the
/// `ChordLabUICore` package (Tests/ChordLabUICoreTests/RollGroupMathTests.swift).
enum RollGroupMath {
  /// Which edge a resize drags: the start (the end stays) or the end (the start stays).
  enum Edge: Equatable {
    case start, end
  }

  /// An arrow nudge (design D15 "Keyboard"): ↑/↓ a semitone (an octave with ⇧), ←/→ one grid step. The arrow
  /// key's own direction type, so `KeyCommand.nudge` passes straight through.
  typealias NudgeDirection = ArrowDirection

  /// `value` clamped into `low...high` widened to contain 0 (see the type comment).
  private static func clamp(_ value: Int, low: Int, high: Int) -> Int {
    min(max(value, min(low, 0)), max(high, 0))
  }

  /// The move delta every note of `notes` can take: `dPitch` clamped so every pitch stays in 0...127, `dTick` so
  /// every start stays ≥ 0 and below `limits.endTick` and every end ≤ `limits.maxNoteTick`.
  static func move(_ notes: [RollNote], dPitch: Int, dTick: Int, limits: RollLimits) -> (dPitch: Int, dTick: Int) {
    guard let first = notes.first else { return (0, 0) }
    var lowPitch = first.pitch, highPitch = first.pitch
    var earliest = first.start, latest = first.start, latestEnd = first.end
    for note in notes {
      lowPitch = min(lowPitch, note.pitch)
      highPitch = max(highPitch, note.pitch)
      earliest = min(earliest, note.start)
      latest = max(latest, note.start)
      latestEnd = max(latestEnd, note.end)
    }
    let pitch = clamp(dPitch, low: -lowPitch, high: 127 - highPitch)
    let tick = clamp(dTick, low: -earliest, high: min(limits.maxNoteTick - latestEnd, limits.endTick - 1 - latest))
    return (pitch, tick)
  }

  /// The resize delta every note can take. `.end`: the lengths change by `dTick`, clamped so the shortest stays
  /// ≥ `limits.minNoteTicks` and every end ≤ `limits.maxNoteTick`. `.start`: the starts move by `dTick` and the
  /// lengths by −`dTick` (the ends stay), clamped so every start stays ≥ 0 and below `limits.endTick` and the
  /// shortest note ≥ `limits.minNoteTicks`.
  static func resize(_ notes: [RollNote], edge: Edge, dTick: Int, limits: RollLimits) -> Int {
    guard let first = notes.first else { return 0 }
    var shortest = first.length, earliest = first.start, latest = first.start, latestEnd = first.end
    for note in notes {
      shortest = min(shortest, note.length)
      earliest = min(earliest, note.start)
      latest = max(latest, note.start)
      latestEnd = max(latestEnd, note.end)
    }
    switch edge {
    case .end:
      return clamp(dTick, low: limits.minNoteTicks - shortest, high: limits.maxNoteTick - latestEnd)
    case .start:
      return clamp(dTick, low: -earliest, high: min(shortest - limits.minNoteTicks, limits.endTick - 1 - latest))
    }
  }

  /// The velocity delta every note can take: clamped so every velocity stays in 1...127.
  static func velocity(_ notes: [RollNote], dVelocity: Int) -> Int {
    guard let first = notes.first else { return 0 }
    var softest = first.velocity, loudest = first.velocity
    for note in notes {
      softest = min(softest, note.velocity)
      loudest = max(loudest, note.velocity)
    }
    return clamp(dVelocity, low: 1 - softest, high: 127 - loudest)
  }

  /// An arrow nudge of `notes` (design D15 "Keyboard"): ↑/↓ one semitone, or with `octave` (⇧) twelve; ←/→
  /// `gridTicks` (the roll's `RollGrid.ticks(ticksPerBeat:)`; `octave` is ignored horizontally), as one shared
  /// delta through `move`. Returns the `updateNotes` edits (absolute values, only the changed field), or nil when
  /// the clamp leaves nothing to do (nil for an empty group too).
  ///
  /// Decision (D15 silent): an octave nudge moves a whole octave or nothing, never a clamped part of one, so it
  /// cannot change the notes' pitch classes; a semitone or grid step is clamped like a drag (a partial step to
  /// tick 0, or to the last tick before the end, is taken).
  static func nudge(_ notes: [RollNote], direction: NudgeDirection, octave: Bool, gridTicks: Int,
                    limits: RollLimits) -> [RollNoteEdit]? {
    let step = octave ? 12 : 1
    let wanted: (dPitch: Int, dTick: Int)
    switch direction {
    case .up: wanted = (step, 0)
    case .down: wanted = (-step, 0)
    case .left: wanted = (0, -max(1, gridTicks))
    case .right: wanted = (0, max(1, gridTicks))
    }
    let delta = move(notes, dPitch: wanted.dPitch, dTick: wanted.dTick, limits: limits)
    if octave, direction == .up || direction == .down, delta.dPitch != wanted.dPitch { return nil }
    let edits = moveEdits(notes, dPitch: delta.dPitch, dTick: delta.dTick)
    return edits.isEmpty ? nil : edits
  }

  // MARK: - Edits

  /// The `updateNotes` edits that move `notes` by the (already clamped) delta: `pitch` when `dPitch` ≠ 0, `start`
  /// when `dTick` ≠ 0; empty when both are 0.
  static func moveEdits(_ notes: [RollNote], dPitch: Int, dTick: Int) -> [RollNoteEdit] {
    guard dPitch != 0 || dTick != 0 else { return [] }
    return notes.map {
      RollNoteEdit(id: $0.id, pitch: dPitch != 0 ? $0.pitch + dPitch : nil, start: dTick != 0 ? $0.start + dTick : nil)
    }
  }

  /// The `updateNotes` edits of a resize by the (already clamped) `dTick`: `.end` changes `length`; `.start`
  /// changes `start` and `length` (the end stays). Empty when `dTick` is 0.
  static func resizeEdits(_ notes: [RollNote], edge: Edge, dTick: Int) -> [RollNoteEdit] {
    guard dTick != 0 else { return [] }
    return notes.map {
      switch edge {
      case .end: return RollNoteEdit(id: $0.id, length: $0.length + dTick)
      case .start: return RollNoteEdit(id: $0.id, start: $0.start + dTick, length: $0.length - dTick)
      }
    }
  }

  /// The `updateNotes` edits of a velocity change by the (already clamped) `dVelocity`; empty when it is 0.
  static func velocityEdits(_ notes: [RollNote], dVelocity: Int) -> [RollNoteEdit] {
    guard dVelocity != 0 else { return [] }
    return notes.map { RollNoteEdit(id: $0.id, velocity: $0.velocity + dVelocity) }
  }

  /// `notes` with an edit applied: the preview the roll draws during a drag (anchor, offset and active as they
  /// were; the engine re-resolves them on commit).
  static func applying(_ notes: [RollNote], dPitch: Int = 0, dStart: Int = 0, dLength: Int = 0, dVelocity: Int = 0)
    -> [RollNote] {
    notes.map {
      RollNote(id: $0.id, pitch: $0.pitch + dPitch, start: $0.start + dStart, length: $0.length + dLength,
               velocity: $0.velocity + dVelocity, anchor: $0.anchor, offset: $0.offset, active: $0.active)
    }
  }
}
