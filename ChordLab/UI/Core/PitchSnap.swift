import Foundation

/// The roll's Snap pitch option (tracks design D14, D15): which pitch classes a drawn or moved note may land on,
/// judged by the chord sounding at its start (`PlaySpans.event(atTick:in:ticksPerBeat:)` finds it). It applies to a single-note move and to an insert only; a group
/// moves by exact rows (design D15).
///
/// - `off`: every pitch.
/// - `scale`: the chord scale, the guide's `scale` mask (design D9). It is the guide's scale, not the roles: in
///   E7 over C the G is a Tension by the dominant exception yet not in the scale (G♯ replaced it).
/// - `chordAndTensions`: roles Root, ChordTone and Tension (so E7's G stays, its A, an Avoid, does not).
/// - `chordTones`: roles Root and ChordTone.
///
/// Depends on nothing but Foundation; unit-tested by the `ChordLabUICore` package
/// (Tests/ChordLabUICoreTests/PitchSnapTests.swift).
enum PitchSnap: CaseIterable {
  case off, scale, chordAndTensions, chordTones

  /// A track's roll starts with Snap pitch off.
  static let `default`: PitchSnap = .off

  /// The menu title: "Off", "Scale", "Chord + tensions", "Chord tones".
  var title: String {
    switch self {
    case .off: return "Off"
    case .scale: return "Scale"
    case .chordAndTensions: return "Chord + tensions"
    case .chordTones: return "Chord tones"
    }
  }

  /// Whether pitch class `pc` (any integer, taken mod 12, so a MIDI note works) is allowed over `guide`.
  func allows(pc: Int, guide: GuideJSON) -> Bool {
    switch self {
    case .off:
      return true
    case .scale:
      return guide.inScale(pc: pc)
    case .chordAndTensions:
      let role = guide.role(pc: pc)
      return role == .root || role == .chord || role == .tension
    case .chordTones:
      let role = guide.role(pc: pc)
      return role == .root || role == .chord
    }
  }

  /// The pitch a note at `pitch` snaps to over `guide`: the pitch itself when allowed, else the nearest allowed
  /// pitch in 0...127, a tie going UP (a pitch exactly between two allowed ones takes the higher). With Off or
  /// no guide the pitch comes back unchanged (even outside 0...127); otherwise the input is clamped to
  /// 0...127 first. A guide that allows nothing (no pitch class passes) leaves the clamped pitch alone.
  func snap(pitch: Int, guide: GuideJSON?) -> Int {
    guard self != .off, let guide else { return pitch }
    let pitch = min(max(pitch, 0), 127)
    for distance in 0...127 {
      let up = pitch + distance
      if up <= 127, allows(pc: up, guide: guide) { return up }
      let down = pitch - distance
      if down >= 0, allows(pc: down, guide: guide) { return down }
    }
    return pitch
  }
}
