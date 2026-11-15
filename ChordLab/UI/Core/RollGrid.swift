import Foundation

/// The roll's grid (tracks design D14, D15): the step a drawn, inserted or moved note's start snaps to, and the
/// length an insert takes. Seven options; the default is the eighth.
///
/// Steps are fractions of a beat, and every one divides 960 (the engine's ticks per beat), so the grid counts
/// from tick 0 and is beat-aligned in every meter: a bar of 3/4, 6/8 or 7/8 starts on a beat, hence on a step of
/// every grid. `off` is not truly off: it snaps to 10 ticks (1/96 of a beat), the finest the roll offers, so
/// every start is an integer and a note can still be placed freely by eye.
///
/// Depends on nothing but Foundation; unit-tested by the `ChordLabUICore` package
/// (Tests/ChordLabUICoreTests/RollGridTests.swift).
enum RollGrid: CaseIterable {
  case quarter, eighth, sixteenth, thirtySecond, eighthTriplet, sixteenthTriplet, off

  /// The grid a track's roll starts with (design D14's Grid control).
  static let `default`: RollGrid = .eighth

  /// The shortest track note when the snapshot carries no `trackLimits` (`kMinTrackNoteTicks`, a 1/32 beat).
  static let defaultMinNoteTicks = 30

  /// The menu title: "1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T", "Off".
  var title: String {
    switch self {
    case .quarter: return "1/4"
    case .eighth: return "1/8"
    case .sixteenth: return "1/16"
    case .thirtySecond: return "1/32"
    case .eighthTriplet: return "1/8T"
    case .sixteenthTriplet: return "1/16T"
    case .off: return "Off"
    }
  }

  /// The step in ticks: at 960 per beat 960, 480, 240, 120, 320, 160 and, for Off, 10. Computed from the
  /// snapshot's `ticksPerBeat` (the divisors are 1, 2, 4, 8, 3, 6, 96) by integer division, never below 1.
  func ticks(ticksPerBeat: Int = 960) -> Int {
    let divisor: Int
    switch self {
    case .quarter: divisor = 1
    case .eighth: divisor = 2
    case .sixteenth: divisor = 4
    case .thirtySecond: divisor = 8
    case .eighthTriplet: divisor = 3
    case .sixteenthTriplet: divisor = 6
    case .off: divisor = 96
    }
    return max(1, max(1, ticksPerBeat) / divisor)
  }

  /// The grid step at or before `tick`, counted from tick 0 (a tick below 0 is 0): where a click's cell starts.
  func floor(_ tick: Int, ticksPerBeat: Int = 960) -> Int {
    let step = ticks(ticksPerBeat: ticksPerBeat)
    let tick = max(0, tick)
    return tick - tick % step
  }

  /// The grid step nearest to `tick`, counted from tick 0 (a tick below 0 is 0); a tick exactly halfway between
  /// two steps goes to the later one ("ties up", as `PitchSnap`).
  func round(_ tick: Int, ticksPerBeat: Int = 960) -> Int {
    let step = ticks(ticksPerBeat: ticksPerBeat)
    let tick = max(0, tick)
    let remainder = tick % step
    return tick - remainder + (remainder * 2 >= step ? step : 0)
  }

  /// The length of a note an insert draws (design D15): the last used length when there is one, else one grid
  /// step, and never below `minNoteTicks` (the snapshot's `trackLimits.minNoteTicks`, see
  /// `Snapshot.minNoteTicksOrDefault`); so Off with nothing used yet gives `minNoteTicks` (30 beside Off's 10).
  func insertLength(lastLength: Int?, minNoteTicks: Int, ticksPerBeat: Int = 960) -> Int {
    max(minNoteTicks, lastLength ?? ticks(ticksPerBeat: ticksPerBeat))
  }
}

extension Snapshot {
  /// The shortest note the engine accepts: `trackLimits.minNoteTicks`, or 30 (`RollGrid.defaultMinNoteTicks`)
  /// when an older plug-in build sends no limits.
  var minNoteTicksOrDefault: Int { trackLimits?.minNoteTicks ?? RollGrid.defaultMinNoteTicks }
}
