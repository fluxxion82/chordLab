import Foundation

/// Display formatting for the Phase 2 harmony fields (function, tension, "why", voicings), pure and in one
/// place so that the palette's tip, the block's tip and the suggestion chips word things the same
/// way. Nothing here computes theory: every input is a snapshot field. Depends on Foundation only; unit-tested
/// by the `ChordLabUICore` package (ChordLab/Package.swift, Tests/ChordLabUICoreTests/HarmonyTextTests.swift).
enum HarmonyText {
  /// The function word for the engine's short name: "T" Tonic, "PD" Predominant, "D" Dominant, "O" (and
  /// anything else) Other; nil for nil (an older plug-in build sends no function).
  static func functionWord(_ code: String?) -> String? {
    switch code {
    case nil: return nil
    case "T": return "Tonic"
    case "PD": return "Predominant"
    case "D": return "Dominant"
    default: return "Other"
    }
  }

  /// `tension` (0..100) as a fraction 0...1 for a bar, clamped; 0 for nil.
  static func tensionFraction(_ tension: Int?) -> Double {
    Double(min(100, max(0, tension ?? 0))) / 100
  }

  /// The first line of a block's hover tip: the engine's sentence with its accidentals as glyphs
  /// (`sentenceGlyphs`), or "first chord" for the first block (its `why` is null).
  static func whyHeadline(_ why: WhyJSON?) -> String {
    why.map { sentenceGlyphs($0.text) } ?? "first chord"
  }

  /// The chips under the sentence: each common tone ("common G♯"), the root motion, the voice-leading cost
  /// ("cost 4") and the key the chord is borrowed from ("borrowed from E♭ Aeolian"), when it is. Empty for
  /// the first block.
  static func whyChips(_ why: WhyJSON?) -> [String] {
    guard let why = why else { return [] }
    var chips = why.commonNames.map { "common " + ChordSymbol.glyphs($0) }
    chips.append(why.rootMotion)
    chips.append("cost \(why.voiceLeadingCost)")
    if let borrowed = why.borrowedFromName {
      chips.append("borrowed from " + sentenceGlyphs(borrowed))
    }
    return chips
  }

  /// An engine sentence (a cell's `relationText`, a block's `why.text`, a suggestion's `reasonTexts`,
  /// a key name) with the accidentals of its note names, chord symbols and roman numerals as glyphs,
  /// as `ChordSymbol.glyphs` shows them elsewhere: "1 common tone (G♯) · … · borrowed from E♭ Aeolian",
  /// "B♭m7 → F♯m7♭5: …", "cadence (♭VII→I)". The words stay as written. A token is a run of letters,
  /// digits and symbol marks; it is converted when it is a note or chord symbol (a letter A–G, then
  /// only accidentals, chord-suffix letters, digits, marks and slash-bass letters: "Bb", "Ebmaj7",
  /// "G7/B", not "Aeolian" or "Dorian") or a roman numeral (an optional "b"/"#" prefix, then only
  /// I, V, i, v and the same marks: "bVII", "V7/ii", not "borrowed"). Anything else is left alone, so a
  /// word's "b" is never a flat.
  static func sentenceGlyphs(_ sentence: String) -> String {
    var out = ""
    var token = ""
    func flush() {
      out += isSymbolToken(token) ? ChordSymbol.glyphs(token) : token
      token = ""
    }
    for character in sentence {
      if character.isLetter || character.isNumber || symbolMarks.contains(character) {
        token.append(character)
      } else {
        flush()
        out.append(character)
      }
    }
    flush()
    return out
  }

  /// Marks that belong to a chord symbol or numeral and are not letters or digits: the sharp, the
  /// diminished ring, the augmented plus and the slash of a slash chord or applied numeral. (ø and Δ are
  /// letters already.)
  private static let symbolMarks: Set<Character> = ["#", "°", "+", "/"]

  /// Characters allowed after the note letter of a chord symbol: accidentals ("b", "#", "x" for a double
  /// sharp), the suffix letters of the engine's symbols (m, M, maj, dim, aug, sus, add), digits, the
  /// marks, and A–G for a slash bass.
  private static let symbolTail: Set<Character> =
    Set("ABCDEFGabdgijmsuMx#0123456789°øΔ+/")

  /// Characters of a roman numeral after its accidental prefix: the numeral letters, more accidentals,
  /// digits and the marks ("vii°7/iii", "bVII", "V7/bII").
  private static let numeralBody: Set<Character> = Set("IViv#b0123456789°øΔ+/")

  /// Whether `token` reads as a note or chord symbol or as a roman numeral (see `sentenceGlyphs`).
  private static func isSymbolToken(_ token: String) -> Bool {
    guard let first = token.first else { return false }
    if ("A"..."G").contains(first) {
      return token.dropFirst().allSatisfy(symbolTail.contains)
    }
    let body = token.drop { $0 == "b" || $0 == "#" }
    guard let lead = body.first, "IViv".contains(lead) else { return false }
    return body.allSatisfy(numeralBody.contains)
  }

  /// A voicing as note names with octaves, middle C (MIDI 60) being C4: "C4  E4  G4". With the engine's
  /// `names` (`voicingNames`: one per note, spelled as the chord is, ASCII accidentals) those are shown, as
  /// glyphs: "E4  G♯4  B4  D5" for E7, "F4  A♭4  C5" for Fm. Without them, or with a list of another
  /// length (which cannot be trusted to line up), the sharp-spelled MIDI names.
  static func voicingText(_ voicing: [Int], names: [String]? = nil) -> String {
    noteNames(voicing, names: names).joined(separator: "  ")
  }

  /// `voicing`'s note names: the engine's `names` as glyphs when they match it one to one, else
  /// `ChordSymbol.noteName` of each note.
  static func noteNames(_ voicing: [Int], names: [String]?) -> [String] {
    if let names = names, names.count == voicing.count {
      return names.map(ChordSymbol.glyphs)
    }
    return voicing.map(ChordSymbol.noteName(midi:))
  }

  /// A tip's verdict on the project key: "In key" while `outOfKey` is empty, else "Out of key: " and the
  /// borrowed notes, named by the engine's `names` (`outOfKeyNames`, as the chord spells them: "G♯" in
  /// E7, "A♭" in Fm) when they match one to one, else by their sharp-spelled pitch-class names.
  static func outOfKeyText(_ outOfKey: [Int], names: [String]?) -> String {
    if outOfKey.isEmpty {
      return "In key"
    }
    let list = names.flatMap { $0.count == outOfKey.count ? $0.map(ChordSymbol.glyphs) : nil }
      ?? outOfKey.map(ChordSymbol.pitchClassName)
    return "Out of key: " + list.joined(separator: ", ")
  }

  /// The bass note's name: the engine's spelled `name` as glyphs when there is one, else the MIDI note's
  /// sharp-spelled name; nil without either.
  private static func bassText(_ bass: Int?, _ name: String?) -> String? {
    if let name = name {
      return ChordSymbol.glyphs(name)
    }
    return bass.map(ChordSymbol.noteName(midi:))
  }

  /// "Inversion 1 · bass E4" (the bass is the voicing's lowest note, what sounds; `bassName` its spelled
  /// name from the engine, the first of `voicingNames`, when there is one).
  static func inversionLabel(_ inversion: Int, bass: Int?, bassName: String? = nil) -> String {
    "Inversion \(inversion)" + (bassText(bass, bassName).map { " · bass " + $0 } ?? "")
  }

  /// "Octave +1", "Octave 0", "Octave −1".
  static func octaveLabel(_ octave: Int) -> String {
    "Octave " + (octave > 0 ? "+\(octave)" : octave < 0 ? "−\(-octave)" : "0")
  }

  /// Whether the stored `inversion` and `octave` are what sounds: for a manual chord always, and for any
  /// chord while auto voice-leading is off (`compile` then voices `voicing(chord, inversion, octave)`).
  /// With auto voice-leading on, a non-manual chord's stored fields are only what a stepper would start
  /// from; the compiled `voicing` may be any inversion and octave.
  static func storedVoicingSounds(manual: Bool, autoVoiceLead: Bool) -> Bool {
    manual || !autoVoiceLead
  }

  /// One line on a chord's voicing, worded so that the stored fields are never shown as if they sounded:
  /// "Manual voicing · Inversion 1 · bass E4 · Octave 0", "Default voicing · Inversion 0 · bass C4 ·
  /// Octave 0" (auto off), or "Automatic voicing · bass E4" (auto on, not manual: the notes say the
  /// rest, the stored inversion and octave are not what sounds).
  static func voicingStatus(inversion: Int, octave: Int, manual: Bool, autoVoiceLead: Bool, bass: Int?,
                            bassName: String? = nil) -> String {
    if storedVoicingSounds(manual: manual, autoVoiceLead: autoVoiceLead) {
      return (manual ? "Manual voicing" : "Default voicing") + " · " +
             inversionLabel(inversion, bass: bass, bassName: bassName) + " · " + octaveLabel(octave)
    }
    return "Automatic voicing" + (bassText(bass, bassName).map { " · bass " + $0 } ?? "")
  }

  /// A suggestion's reasons as one hover line: the `reasonTexts` joined with " · ", accidentals as
  /// glyphs (`sentenceGlyphs`): "1 common tone (G♯) · cadence (♭VII→I)".
  static func reasonsLine(_ reasonTexts: [String]) -> String {
    sentenceGlyphs(reasonTexts.joined(separator: " · "))
  }

  /// The engine's bound on a section name (`kMaxSectionNameBytes`, document.h): `renameSection` rejects a
  /// longer one as malformed, so the rename field keeps its draft within it.
  static let maxSectionNameBytes = 64

  /// `name` cut down to `maxBytes` of UTF-8 at a character boundary (a character straddling the bound is
  /// dropped whole), as the store itself cuts a long name.
  static func boundedSectionName(_ name: String, maxBytes: Int = maxSectionNameBytes) -> String {
    guard name.utf8.count > maxBytes else { return name }
    var result = ""
    var bytes = 0
    for character in name {
      let size = character.utf8.count
      if bytes + size > maxBytes { break }
      result.append(character)
      bytes += size
    }
    return result
  }
}
