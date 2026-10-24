import Foundation

/// Display formatting for keys. Spelling and scales come from the engine through the snapshot (`tonics`,
/// `outOfKey`); nothing here computes theory beyond reading a tonic's pitch class off its letter.
enum KeyNames {
  /// What the mode picker shows: Ionian "Major", Aeolian "Minor", the rest spaced out.
  static func modeDisplayName(_ mode: String) -> String {
    switch mode {
    case "Ionian": return "Major"
    case "Aeolian": return "Minor"
    case "HarmonicMinor": return "Harmonic Minor"
    case "MelodicMinor": return "Melodic Minor"
    default: return mode
    }
  }

  private static let naturalPc: [Character: Int] = ["A": 9, "B": 11, "C": 0, "D": 2, "E": 4, "F": 5, "G": 7]

  /// Pitch class 0..11 of a spelled tonic (the tonic picker's selection).
  static func pitchClass(of tonic: TonicJSON) -> Int {
    let natural = naturalPc[Character(tonic.letter)] ?? 0
    return ((natural + tonic.alter) % 12 + 12) % 12
  }

  /// ASCII text of a tonic as the engine writes it: "C", "F#", "Bb".
  static func text(_ tonic: TonicJSON) -> String {
    tonic.alter > 0 ? tonic.letter + "#" : tonic.alter < 0 ? tonic.letter + "b" : tonic.letter
  }

  /// Display form of a tonic alone, with glyph accidentals: "C", "F♯", "B♭" (the browse bar's tonic, beside
  /// its mode picker).
  static func tonicName(_ tonic: TonicJSON) -> String {
    ChordSymbol.glyphs(text(tonic))
  }

  /// Display form of a key: glyph tonic plus the mode's display name, e.g. "B♭ Harmonic Minor".
  static func displayName(_ key: KeyJSON) -> String {
    tonicName(key.tonic) + " " + modeDisplayName(key.mode)
  }
}
