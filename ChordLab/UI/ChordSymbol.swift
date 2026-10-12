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

  /// Spoken quality of each dictionary type id, for accessibility.
  static let spokenQualities: [String: String] = [
    "5": "power chord", "maj": "major", "min": "minor", "dim": "diminished", "aug": "augmented",
    "sus2": "suspended second", "sus4": "suspended fourth", "6": "sixth", "m6": "minor sixth",
    "maj7": "major seventh", "7": "dominant seventh", "m7": "minor seventh", "m7b5": "half-diminished seventh",
    "dim7": "diminished seventh", "mMaj7": "minor major seventh", "augMaj7": "augmented major seventh",
    "add9": "add nine", "madd9": "minor add nine", "9": "ninth", "maj9": "major ninth", "m9": "minor ninth",
    "69": "six nine", "7sus4": "seventh suspended fourth",
  ]

  /// Spoken form of a chord for accessibility: "C major", "F sharp half-diminished seventh". The root is
  /// read off the symbol (a letter plus at most one accidental); an unknown type id reads its suffix as is.
  static func spokenName(symbol: String, type: String) -> String {
    var rest = Substring(symbol)
    var root = rest.first.map(String.init) ?? ""
    rest = rest.dropFirst()
    if rest.first == "b" {
      root += " flat"
      rest = rest.dropFirst()
    } else if rest.first == "#" {
      root += " sharp"
      rest = rest.dropFirst()
    }
    return root + " " + (spokenQualities[type] ?? String(rest))
  }

  /// Pitch-class names with sharps for the black keys (the UI has no per-note spelling yet).
  static let sharpNames = ["C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B"]

  static func pitchClassName(_ pc: Int) -> String {
    sharpNames[((pc % 12) + 12) % 12]
  }

  /// Note name with octave, middle C (MIDI 60) being C4: 60 → "C4", 61 → "C♯4", 59 → "B3".
  static func noteName(midi: Int) -> String {
    pitchClassName(midi) + String(Int((Double(midi) / 12).rounded(.down)) - 1)
  }
}
