import Foundation

/// The roll's grid (tracks design D14, D15): the step a drawn, inserted or moved note's start snaps to, and the
/// length an insert takes. Seven options; the default is the eighth.
///
/// Steps are fractions of a beat, and every one divides 960 (the engine's ticks per beat). The grid is counted
/// from each BAR's start, not from tick 0 (`snapDown`, `snapNearest`): a bar is `num × 3840 / den` ticks, which
/// is not a whole number of quarters in 7/8 (3360), 5/8 (2400) or 3/8 (1440), nor of eighth triplets (320) in
/// those, so a grid from tick 0 would miss their bar lines; the bar line is always a grid point and no
/// snap crosses the next one. `off` is not truly off: it snaps to 10 ticks (1/96 of a beat), the finest the roll
/// offers, so every start is an integer and a note can still be placed freely by eye (every valid meter's bar is
/// a multiple of 10 ticks at 960).
///
/// Depends on nothing but Foundation; unit-tested by the `ChordLabUICore` package
/// (Tests/ChordLabUICoreTests/RollGridTests.swift).
enum RollGrid: CaseIterable {
  case quarter, eighth, sixteenth, thirtySecond, eighthTriplet, sixteenthTriplet, off

  /// The grid a track's roll starts with (design D14's Grid control).
  static let `default`: RollGrid = .eighth

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

  /// One bar in ticks, the engine's `barTicks` (meter.cpp): `num × 4 × ticksPerBeat / den` by integer division
  /// (3840 in 4/4 at 960, 2880 in 3/4 and 6/8, 3360 in 7/8, 2400 in 5/8, 1440 in 3/8). A non-positive `num` or
  /// `den` counts as 4/4 (the snapshot's default meter), and `ticksPerBeat` as at least 1.
  static func barTicks(num: Int, den: Int, ticksPerBeat: Int) -> Int {
    guard num > 0, den > 0 else { return 4 * max(1, ticksPerBeat) }
    return max(1, num * 4 * max(1, ticksPerBeat) / den)
  }

  /// The grid point at or before `tick`: where a click's cell starts. A tick below 0 is 0.
  ///
  /// The grid is counted from each bar's start, not from tick 0: `barTicks` is the bar length (`barTicks(num:den:
  /// ticksPerBeat:)`, below 1 counts as 1), the bar of `tick` starts at `tick / barTicks × barTicks`, and the
  /// points are that start plus whole steps. The two coincide when a bar is a whole number of steps (4/4, 3/4
  /// and 6/8 on every grid), but not in 7/8 (3360), 5/8 (2400) or 3/8 (1440) on a quarter or eighth-triplet grid.
  /// The last point of a bar is its last whole step, so the result never reaches the next bar's start (that is
  /// the next bar's own first point).
  func snapDown(_ tick: Int, barTicks: Int, ticksPerBeat: Int = 960) -> Int {
    let step = ticks(ticksPerBeat: ticksPerBeat)
    let tick = max(0, tick)
    let bar = max(1, barTicks)
    let start = tick / bar * bar
    let inBar = tick - start
    return start + inBar - inBar % step
  }

  /// The grid point nearest to `tick`, bar-relative as `snapDown` (a tick below 0 is 0); a tick exactly halfway
  /// between two points goes to the later one ("ties up", as `PitchSnap`). The bar line is itself a grid point:
  /// the candidates are the point at or before `tick` and the one after it: the next whole step in the bar or,
  /// when that would reach the next bar, the next bar's start. So the result never passes the next bar start (7/8 on a quarter grid
  /// takes 3300 to 3360, not to 3840).
  func snapNearest(_ tick: Int, barTicks: Int, ticksPerBeat: Int = 960) -> Int {
    let step = ticks(ticksPerBeat: ticksPerBeat)
    let tick = max(0, tick)
    let bar = max(1, barTicks)
    let earlier = snapDown(tick, barTicks: bar, ticksPerBeat: ticksPerBeat)
    let later = min(earlier + step, (tick / bar + 1) * bar)
    return later - tick <= tick - earlier ? later : earlier
  }

  /// The length of a note an insert draws (design D15): the last used length when there is one, else one grid
  /// step, and never below `minNoteTicks` (the snapshot's `trackLimits.minNoteTicks`, see
  /// `Snapshot.minNoteTicksOrDefault`, or `RollTiming`); so Off with nothing used yet gives `minNoteTicks` (30 beside Off's 10).
  func insertLength(lastLength: Int?, minNoteTicks: Int, ticksPerBeat: Int = 960) -> Int {
    max(minNoteTicks, lastLength ?? ticks(ticksPerBeat: ticksPerBeat))
  }
}
