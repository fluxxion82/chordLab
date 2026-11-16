import SwiftUI

/// How a harmonic-guide band is drawn behind the piano roll's notes (tracks design D14): an opacity per guide role
/// and track role, one fill colour per role, and when a row carries its degree label. The guide roles come from
/// the engine (design D9: Root, ChordTone, Tension, Avoid, Outside); the track role decides what the roll
/// emphasises, the playing a role suits:
///
/// - **Bass** emphasises the Root, then the chord tones: root > chord > tension > avoid.
/// - **Lead** shows the Root, the chord tones and the tensions equally, and differs from Other by strength
///   (0.30 against 0.20): a melody lives on the colour tones, so its bands are stronger.
/// - **Other** is even: Root, chord tones and tensions the same.
/// - **Avoid** is a faint warm tint on every track role, below every other drawn role.
/// - **Outside** is not drawn.
///
/// The opacities (the design gives the ordering, these are the values):
///
/// | track role | root | chord | tension | avoid | outside |
/// |------------|------|-------|---------|-------|---------|
/// | bass       | 0.36 | 0.24  | 0.12    | 0.06  | 0       |
/// | lead       | 0.30 | 0.30  | 0.30    | 0.06  | 0       |
/// | other      | 0.20 | 0.20  | 0.20    | 0.06  | 0       |
///
/// They are alphas for a fill over the roll's dark grid, tuned so that notes in the track colour stay readable
/// over the strongest band (0.36) and the weakest (0.06) is still visible as a tint.
///
/// Depends on nothing but SwiftUI's `Color` value type; unit-tested by the `ChordLabUICore` package
/// (Tests/ChordLabUICoreTests/GuideStyleTests.swift).
enum GuideStyle {
  /// The fill alpha of a band of `role` in a track of role `track`: the table above; 0 for Outside.
  static func opacity(role: GuideRole, track: TrackRole) -> Double {
    switch (track, role) {
    case (_, .outside): return 0
    case (_, .avoid): return avoidOpacity
    case (.bass, .root): return 0.36
    case (.bass, .chord): return 0.24
    case (.bass, .tension): return 0.12
    case (.lead, .root), (.lead, .chord), (.lead, .tension): return 0.30
    case (.other, .root), (.other, .chord), (.other, .tension): return 0.20
    }
  }

  /// The faint warm tint every track role gives an Avoid note.
  private static let avoidOpacity = 0.06

  /// The cool fill of the stable roles (Root, ChordTone, Tension; their opacity tells them apart), the warm fill
  /// of Avoid, and clear for Outside.
  static let cool = Color(red: 0.431, green: 0.561, blue: 0.847)   // #6E8FD8
  static let warm = Color(red: 0.851, green: 0.565, blue: 0.290)   // #D9904A

  static func color(role: GuideRole) -> Color {
    switch role {
    case .root, .chord, .tension: return cool
    case .avoid: return warm
    case .outside: return .clear
    }
  }

  /// The row height from which a degree label is drawn (design D14: at least 12 pt).
  static let minLabelRowHeight: CGFloat = 12

  /// Whether a row of `role` carries its degree label ("R", "3", "♭7") at the left of the chord's span: only the
  /// Root and ChordTone rows, and only when the rows are at least `minLabelRowHeight` tall (design D14).
  static func showsDegreeLabel(role: GuideRole, rowHeight: CGFloat) -> Bool {
    (role == .root || role == .chord) && rowHeight >= minLabelRowHeight
  }
}
