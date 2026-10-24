import SwiftUI

/// Which relation drives the palette's relatedness look (plan R2): the engine's `relationMode`, live state
/// on the document, sent with `setRelationMode {mode}`. The raw values are the wire names. The UI never
/// stores a mode of its own: the Relations picker shows the snapshot's and sends the command.
enum RelationMode: String, CaseIterable, Identifiable {
  case off, commonTones, voiceLeading, function, blend
  var id: String { rawValue }
  var title: String {
    switch self {
    case .off: return "Off"
    case .commonTones: return "Common tones"
    case .voiceLeading: return "Voice leading"
    case .function: return "Function"
    case .blend: return "Blend"
    }
  }
}

/// How a palette (or home-row) cell shows its relatedness to the reference chord (plan R7): the circle is
/// drawn at `scale` times its diameter, centred in the unchanged cell slot, at `opacity`. Related chords
/// grow (to 1.10 at 100), unrelated ones shrink and dim (0.70 and 0.55 at 0). A cell without a `relation`
/// (no reference chord, mode off, or an older plug-in) is drawn as before. Out-of-range values are clamped
/// to 0...100.
enum RelationStyle {
  static let minScale: CGFloat = 0.70
  static let scaleRange: CGFloat = 0.40
  static let minOpacity = 0.55
  static let opacityRange = 0.45
  /// The change from one score to the next.
  static let animation = Animation.easeOut(duration: 0.18)

  static func scale(relation: Int?) -> CGFloat {
    guard let relation = relation else { return 1.0 }
    return minScale + scaleRange * CGFloat(clamped(relation)) / 100
  }

  static func opacity(relation: Int?) -> Double {
    guard let relation = relation else { return 1.0 }
    return minOpacity + opacityRange * Double(clamped(relation)) / 100
  }

  /// The popover's line for a cell: "Relation to G7 · 72" (glyphs applied); nil without a relation.
  static func headline(reference: RelationReferenceJSON?, relation: Int?) -> String? {
    guard let relation = relation else { return nil }
    guard let reference = reference else { return "Relation · \(relation)" }
    return "Relation to \(ChordSymbol.glyphs(reference.symbol)) · \(relation)"
  }

  /// The key bar's caption after the Relations picker: "to G7"; nil without a reference chord.
  static func caption(reference: RelationReferenceJSON?) -> String? {
    reference.map { "to " + ChordSymbol.glyphs($0.symbol) }
  }

  private static func clamped(_ relation: Int) -> Int { min(100, max(0, relation)) }
}
