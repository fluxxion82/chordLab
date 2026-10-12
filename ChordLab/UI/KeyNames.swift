import Foundation

/// Key naming for the key bar, mirroring the engine's rules in engine/src/pitch.cpp. The UI needs them
/// for two things only: to label the tonic picker with the spelling the engine will use, and to send a
/// spelled tonic in `setKey` (the engine takes `{letter, alter, mode}`, never a bare pitch class).
enum KeyNames {
  /// The engine's `Mode` enumerator names, in enum order: the wire form of a mode.
  static let modes = ["Ionian", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Aeolian", "Locrian",
                      "HarmonicMinor", "MelodicMinor"]

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

  static let letters: [Character] = ["A", "B", "C", "D", "E", "F", "G"]
  private static let naturalPc: [Character: Int] = ["A": 9, "B": 11, "C": 0, "D": 2, "E": 4, "F": 5, "G": 7]
  /// Circle-of-fifths index of each natural Ionian key.
  private static let fifthsIndex: [Character: Int] = ["A": 3, "B": 5, "C": 0, "D": 2, "E": 4, "F": -1, "G": 1]
  /// Signature offset of each mode relative to Ionian on the same tonic.
  private static let modeSignatureOffset: [String: Int] = [
    "Ionian": 0, "Dorian": -2, "Phrygian": -4, "Lydian": 1, "Mixolydian": -1, "Aeolian": -3, "Locrian": -5,
    "HarmonicMinor": -3, "MelodicMinor": -3]

  /// Pitch class 0..11 of a spelled tonic.
  static func pitchClass(of tonic: TonicJSON) -> Int {
    let natural = naturalPc[Character(tonic.letter)] ?? 0
    return ((natural + tonic.alter) % 12 + 12) % 12
  }

  /// Key signature as a circle-of-fifths index: sharps positive, flats negative (`signatureOf`).
  static func signature(letter: Character, alter: Int, mode: String) -> Int {
    (fifthsIndex[letter] ?? 0) + 7 * alter + (modeSignatureOffset[mode] ?? 0)
  }

  /// The tonic spelling of `pc` for `mode` with the fewest accidentals in its key signature, ties
  /// preferring the natural spelling, then the sharp (`canonicalTonic`): pc 1 Ionian is Db, pc 6 Ionian
  /// F#, pc 3 Locrian D#, pc 11 Lydian B.
  static func canonicalTonic(pc: Int, mode: String) -> TonicJSON {
    let target = ((pc % 12) + 12) % 12
    var best = TonicJSON(letter: "C", alter: 0)
    var bestSignature = 0
    var found = false
    for letter in letters {
      for alter in -1...1 {
        let candidate = TonicJSON(letter: String(letter), alter: alter)
        guard pitchClass(of: candidate) == target else { continue }
        let sig = signature(letter: letter, alter: alter, mode: mode)
        let fewer = abs(sig) < abs(bestSignature)
        let simplerOnTie = abs(sig) == abs(bestSignature) && spellingRank(alter) < spellingRank(best.alter)
        if !found || fewer || simplerOnTie {
          found = true
          best = candidate
          bestSignature = sig
        }
      }
    }
    return best
  }

  /// Natural, then sharp, then flat (lower wins).
  private static func spellingRank(_ alter: Int) -> Int {
    switch alter {
    case 0: return 0
    case 1: return 1
    default: return 2
    }
  }

  /// ASCII text of a tonic as the engine writes it: "C", "F#", "Bb".
  static func text(_ tonic: TonicJSON) -> String {
    tonic.alter > 0 ? tonic.letter + "#" : tonic.alter < 0 ? tonic.letter + "b" : tonic.letter
  }

  /// Display form of a key: glyph tonic plus the mode's display name, e.g. "B♭ Harmonic Minor".
  static func displayName(_ key: KeyJSON) -> String {
    ChordSymbol.glyphs(text(key.tonic)) + " " + modeDisplayName(key.mode)
  }
}

/// Scale pitch classes, mirroring `scalePitchClasses` in engine/src/scale.cpp. Used only to say which
/// notes of a hovered chord lie outside the project key; the snapshot carries no scale.
enum Scale {
  /// Semitones above the tonic of each mode, keyed by the engine's mode name.
  static let intervals: [String: [Int]] = [
    "Ionian": [0, 2, 4, 5, 7, 9, 11],
    "Dorian": [0, 2, 3, 5, 7, 9, 10],
    "Phrygian": [0, 1, 3, 5, 7, 8, 10],
    "Lydian": [0, 2, 4, 6, 7, 9, 11],
    "Mixolydian": [0, 2, 4, 5, 7, 9, 10],
    "Aeolian": [0, 2, 3, 5, 7, 8, 10],
    "Locrian": [0, 1, 3, 5, 6, 8, 10],
    "HarmonicMinor": [0, 2, 3, 5, 7, 8, 11],
    "MelodicMinor": [0, 2, 3, 5, 7, 9, 11],
  ]

  /// The seven pitch classes of `mode` on `tonicPc`, in scale order. An unknown mode reads as Ionian.
  static func pitchClasses(tonicPc: Int, mode: String) -> [Int] {
    (intervals[mode] ?? intervals["Ionian"]!).map { ((tonicPc + $0) % 12 + 12) % 12 }
  }

  /// Pitch classes of `voicing` (MIDI notes) that are not in `key`'s scale, in voicing order, once each.
  static func outOfKey(voicing: [Int], key: KeyJSON) -> [Int] {
    let scale = Set(pitchClasses(tonicPc: KeyNames.pitchClass(of: key.tonic), mode: key.mode))
    var seen = Set<Int>()
    return voicing.map { (($0 % 12) + 12) % 12 }.filter { !scale.contains($0) && seen.insert($0).inserted }
  }
}
