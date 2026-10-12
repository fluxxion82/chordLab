import Foundation

/// Display mapping for the engine's chord symbols, roman numerals and key names.
enum ChordSymbol {
  /// Maps the engine's ASCII accidentals to glyphs: `b` → ♭ and `#` → ♯. Everything else passes through
  /// (`Δ`, `°`, `ø`, digits, `/`). Safe as a plain substitution because no dictionary suffix uses the
  /// letter b or the sign # for anything but an accidental ("m7b5", "mb5"), roots are single letters
  /// A..G, and numerals are made of i/v/I/V.
  static func glyphs(_ ascii: String) -> String {
    String(ascii.map { c -> Character in
      switch c {
      case "b": return "♭"
      case "#": return "♯"
      default: return c
      }
    })
  }
}
