import SwiftUI

/// How a harmonic-guide band is drawn behind the piano roll's notes (tracks design D14): one hue per guide role, so
/// a colour means the same thing on every track, an opacity per role, and the degree labels. The guide roles come
/// from the engine (design D9: Root, ChordTone, Tension, Avoid, Outside).
///
/// | role    | fill             | opacity | label                    |
/// |---------|------------------|---------|--------------------------|
/// | root    | gold `#F7C65A`   | 0.60    | "R", heavy, pale gold    |
/// | chord   | ice `#C9DEFF`    | 0.48    | "3", "♭7", bold, white   |
/// | tension | aqua `#4FD1BF`   | 0.32    | "9", "13", medium, dimmer|
/// | avoid   | taupe `#A39B8C`  | 0.16    | none                     |
/// | outside | not drawn        | 0       | none                     |
///
/// Decisions (Sterling, 2026-11-16, after trying the roll in Live): the first style, one blue at different
/// strengths per track role, could not be read ("all shades of blue"), so each role has its own hue, light enough
/// to stand off the roll's dark blue ground; the opacities step down root > chord tone > tension > avoid, the same
/// on a Bass, a Lead and an Other track. Avoid is a neutral taupe, never red: red already means something on the
/// chords (the dominant's dot) and would read as an error. The hues stay away from the track colours the notes are
/// drawn in (green, pink, lavender), and the notes sit on top with their dark edge.
///
/// Depends on nothing but SwiftUI's `Color` and `Font` value types; unit-tested by the `ChordLabUICore` package
/// (Tests/ChordLabUICoreTests/GuideStyleTests.swift).
enum GuideStyle {
  /// The fill alpha of a band of `role`: the table above, 0 for Outside. The same for every `track` role; the
  /// parameter stays so that a role can be given its own emphasis again without touching the callers.
  static func opacity(role: GuideRole, track: TrackRole) -> Double {
    switch role {
    case .root: return 0.60
    case .chord: return 0.48
    case .tension: return 0.32
    case .avoid: return 0.16
    case .outside: return 0
    }
  }

  static let gold = Color(red: 0xF7 / 255.0, green: 0xC6 / 255.0, blue: 0x5A / 255.0)    // #F7C65A
  static let ice = Color(red: 0xC9 / 255.0, green: 0xDE / 255.0, blue: 0xFF / 255.0)     // #C9DEFF
  static let aqua = Color(red: 0x4F / 255.0, green: 0xD1 / 255.0, blue: 0xBF / 255.0)    // #4FD1BF
  static let taupe = Color(red: 0xA3 / 255.0, green: 0x9B / 255.0, blue: 0x8C / 255.0)   // #A39B8C

  /// The fill hue of `role`: gold, ice, aqua, taupe; clear for Outside.
  static func color(role: GuideRole) -> Color {
    switch role {
    case .root: return gold
    case .chord: return ice
    case .tension: return aqua
    case .avoid: return taupe
    case .outside: return .clear
    }
  }

  /// The row height from which a degree label is drawn (design D14: at least 12 pt).
  static let minLabelRowHeight: CGFloat = 12

  /// Whether a row of `role` carries its degree label ("R", "3", "♭7", "9") at the left of the chord's span: the
  /// Root, ChordTone and Tension rows, and only when the rows are at least `minLabelRowHeight` tall.
  static func showsDegreeLabel(role: GuideRole, rowHeight: CGFloat) -> Bool {
    (role == .root || role == .chord || role == .tension) && rowHeight >= minLabelRowHeight
  }

  /// A degree label's colour: pale gold on the root's band, near-white on a chord tone's, a step dimmer on a
  /// tension's (opaque, so the label reads at full strength over its band).
  static func labelColor(role: GuideRole) -> Color {
    switch role {
    case .root: return Color(red: 0xFF / 255.0, green: 0xE9 / 255.0, blue: 0xB0 / 255.0)      // #FFE9B0
    case .chord: return Color(red: 0xF4 / 255.0, green: 0xF7 / 255.0, blue: 0xFF / 255.0)     // #F4F7FF
    default: return Color(red: 0xC3 / 255.0, green: 0xCD / 255.0, blue: 0xE8 / 255.0)         // #C3CDE8
    }
  }

  /// A degree label's font: the root's heaviest, a chord tone's bold, a tension's lighter and a little smaller.
  static func labelFont(role: GuideRole) -> Font {
    switch role {
    case .root: return .system(size: 10.5, weight: .heavy, design: .rounded)
    case .chord: return .system(size: 10.5, weight: .bold, design: .rounded)
    default: return .system(size: 9.5, weight: .medium, design: .rounded)
    }
  }
}
