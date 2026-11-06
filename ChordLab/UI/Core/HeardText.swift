import Foundation

/// The texts of the Heard group (UI/Core/Heard.swift), with the engine's ASCII accidentals shown as glyphs.
enum HeardText {
  /// The heard key as the Key picker would name it with its fit: "A Minor · 87%", "E♭ Major · 71%". Nil
  /// while no key qualifies.
  static func keyLine(_ heard: HeardJSON) -> String? {
    guard let key = heard.key else { return nil }
    return KeyNames.tonicName(key.tonic) + " " + KeyNames.modeDisplayName(key.mode) + " · \(key.percent)%"
  }

  /// What the group says while no key qualifies: "listening · 5 notes", "listening · 1 note", and plain
  /// "listening" before any note.
  static func listeningLine(_ heard: HeardJSON) -> String {
    switch heard.onsets {
    case ..<1: return "listening"
    case 1: return "listening · 1 note"
    default: return "listening · \(heard.onsets) notes"
    }
  }

  /// The heard chord's symbol and numeral: "Am7 · vi7", "F♯m7♭5 · ♯ivø7". Nil without a chord.
  static func chordLine(_ heard: HeardJSON) -> String? {
    guard let chord = heard.chord else { return nil }
    return ChordSymbol.glyphs(chord.symbol) + " · " + ChordSymbol.glyphs(chord.roman)
  }

  /// The names of the held notes, comma separated: "C, E, G♯". Empty when none is held.
  static func heldLine(_ heard: HeardJSON) -> String {
    heard.heldNames.map(ChordSymbol.glyphs).joined(separator: ", ")
  }
}
