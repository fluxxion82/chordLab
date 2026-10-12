import Foundation

/// Display formatting for the Phase 2 harmony fields (function, tension, "why", voicings), pure and in one
/// place so that the palette's popover, the block's popover and the suggestion chips word things the same
/// way. Nothing here computes theory: every input is a snapshot field. Depends on Foundation only, so it
/// also compiles under plain `swiftc` for the scratchpad checks (scripts/geometry_check).
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

  /// The first line of a block's hover popover: the engine's sentence, or "first chord" for the first
  /// block (its `why` is null).
  static func whyHeadline(_ why: WhyJSON?) -> String {
    why?.text ?? "first chord"
  }

  /// The chips under the sentence: each common tone ("common G"), the root motion, the voice-leading cost
  /// ("cost 4") and the key the chord is borrowed from, when it is. Empty for the first block.
  static func whyChips(_ why: WhyJSON?) -> [String] {
    guard let why = why else { return [] }
    var chips = why.commonNames.map { "common " + $0 }
    chips.append(why.rootMotion)
    chips.append("cost \(why.voiceLeadingCost)")
    if let borrowed = why.borrowedFromName {
      chips.append("borrowed from " + borrowed)
    }
    return chips
  }

  /// A voicing as note names with octaves, middle C (MIDI 60) being C4: "C4  E4  G4".
  static func voicingText(_ voicing: [Int]) -> String {
    voicing.map(ChordSymbol.noteName(midi:)).joined(separator: "  ")
  }

  /// A suggestion's reasons as one hover line: the `reasonTexts` joined with " · ".
  static func reasonsLine(_ reasonTexts: [String]) -> String {
    reasonTexts.joined(separator: " · ")
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
