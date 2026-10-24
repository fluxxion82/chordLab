import Foundation

/// How a clicked preset is loaded (plan P4, P7): appended after the last chord, or replacing the chords and
/// sections. The raw values are the wire names of `loadPreset {id, mode}`. UI-only choice, remembered across
/// sessions: every view that reads it declares
/// `@AppStorage(PresetLoadMode.storageKey) private var mode = PresetLoadMode.defaultMode`.
enum PresetLoadMode: String, CaseIterable, Identifiable {
  case append, replace
  static let storageKey = "chordlab.presetLoadMode"
  /// What a fresh install does: add to the end, which never loses work.
  static let defaultMode: PresetLoadMode = .append
  var id: String { rawValue }
  var title: String {
    switch self {
    case .append: return "Add to end"
    case .replace: return "Replace progression"
    }
  }
}

/// One genre heading of the Presets popover and its presets, in the snapshot's (the engine table's) order.
struct PresetGroup: Equatable, Identifiable {
  let genre: String
  let presets: [PresetJSON]
  var id: String { genre }
}

/// The Presets popover's text and grouping (plan P7). Nothing here computes theory: the numerals, the
/// tonality and `fits` are the snapshot's.
enum PresetStyle {
  /// The presets grouped by `genre` (compared exactly), groups in order of each genre's first appearance,
  /// presets within a group in their original order.
  static func groups(_ presets: [PresetJSON]) -> [PresetGroup] {
    var order: [String] = []
    var byGenre: [String: [PresetJSON]] = [:]
    for preset in presets {
      if byGenre[preset.genre] == nil { order.append(preset.genre) }
      byGenre[preset.genre, default: []].append(preset)
    }
    return order.map { PresetGroup(genre: $0, presets: byGenre[$0] ?? []) }
  }

  /// The row's numerals text: the engine's spelling with its ASCII accidentals as glyphs ("i – ♭VII – ♭VI
  /// – V7"), as every other numeral in the editor is shown.
  static func numerals(_ preset: PresetJSON) -> String { ChordSymbol.glyphs(preset.numerals) }

  /// The row's tonality badge: "major", "minor", "any"; a tonality this UI does not know, lowercased.
  static func badge(tonality: String) -> String { tonality.lowercased() }

  /// The line under a greyed row: "Written for a major key" / "… minor key" when the preset does not fit
  /// the project key; nil when it fits, for `Any` (which always fits) and for an unknown tonality.
  static func footnote(fits: Bool, tonality: String) -> String? {
    guard !fits, let kind = keyKind(tonality) else { return nil }
    return "Written for a \(kind) key"
  }

  /// VoiceOver's label for a row: "Axis, Pop, I – V – vi – IV, written for a major key", with
  /// ", does not fit the project key" when greyed; "fits any key" for `Any`.
  static func accessibilityLabel(_ preset: PresetJSON) -> String {
    var parts = [preset.name, preset.genre, numerals(preset)]
    if let kind = keyKind(preset.tonality) {
      parts.append("written for a \(kind) key")
    } else if preset.tonality == "Any" {
      parts.append("fits any key")
    }
    if !preset.fits && preset.tonality != "Any" { parts.append("does not fit the project key") }
    return parts.joined(separator: ", ")
  }

  /// The command a click sends (`json_api.h`, plan P4).
  static func loadCommand(id: String, mode: PresetLoadMode) -> [String: Any] {
    ["cmd": "loadPreset", "id": id, "mode": mode.rawValue]
  }

  private static func keyKind(_ tonality: String) -> String? {
    switch tonality {
    case "Major": return "major"
    case "Minor": return "minor"
    default: return nil
    }
  }
}

/// What became of a `loadPreset` the popover sent.
enum PresetLoadOutcome: Equatable {
  /// No answer yet.
  case pending
  /// A new snapshot arrived: the popover closes.
  case loaded
  /// The engine answered with an error instead: the popover stays open (the status line shows it).
  case refused
}

/// The snapshot and error the popover saw just before it sent `loadPreset`, compared with what the state
/// holds afterwards (plan P7). A changed snapshot is a load (a good snapshot also clears the error); an
/// error that changed while the snapshot did not is a refusal. The same error text twice in a row cannot
/// be told from no answer, and stays pending: the popover then simply stays open.
struct PresetLoadWatch<Value: Equatable> {
  let snapshot: Value?
  let error: String?

  func outcome(snapshot now: Value?, error nowError: String?) -> PresetLoadOutcome {
    if now != snapshot { return .loaded }
    if nowError != error && nowError != nil { return .refused }
    return .pending
  }
}
