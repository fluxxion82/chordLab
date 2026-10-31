import Foundation

/// One tick of the sequence ruler: a pulse of the meter at `beat` (quarter-note beats from the start of
/// the progression). A bar start has `isBarStart` and its `bar` number from 1; other pulses have no bar.
struct RulerMark: Equatable {
  let beat: Double
  let isBarStart: Bool
  let bar: Int?
}

/// The ruler's marks for a meter (plan D4).
enum RulerMarks {
  /// Compound: den 8, num a multiple of 3, num >= 6 (the engine's `isCompound`); felt in dotted quarters.
  static func isCompound(num: Int, den: Int) -> Bool {
    den == 8 && num % 3 == 0 && num >= 6
  }

  /// A mark per pulse from beat 0 to before `totalBeats`. The pulse is 4 / den beats (a half note 2, a
  /// quarter 1, an eighth 0.5), or 1.5 beats when compound; a bar is num * 4 / den beats and starts at 0.
  /// Computed in the engine's 960-ticks-per-beat integers, so the beats are exact. Empty for a meter with
  /// a non-positive num or den, or for no length.
  static func marks(num: Int, den: Int, totalBeats: Double) -> [RulerMark] {
    guard num > 0, den > 0, totalBeats > 0 else { return [] }
    let ticksPerBeat = 960
    let pulse = isCompound(num: num, den: den) ? 1440 : 3840 / den
    let bar = num * 3840 / den
    guard pulse > 0, bar > 0 else { return [] }
    let limit = totalBeats * Double(ticksPerBeat)
    var result: [RulerMark] = []
    var tick = 0
    while Double(tick) < limit {
      let isBarStart = tick % bar == 0
      result.append(RulerMark(beat: Double(tick) / Double(ticksPerBeat), isBarStart: isBarStart,
                              bar: isBarStart ? tick / bar + 1 : nil))
      tick += pulse
    }
    return result
  }
}
