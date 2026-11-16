import Foundation

/// One pattern of the bass generator (tracks design D16), in the Generate menu's order: the raw value is the engine's
/// wire name (`bassPatternName`, engine/include/chordlab/json_api.h), `title` what the menu shows.
enum BassPattern: String, CaseIterable, Identifiable {
  case roots
  case rootPulse
  case rootFifth
  case octaves
  case followFeel
  case walking

  var id: String { rawValue }

  var title: String {
    switch self {
    case .roots: return "Roots"
    case .rootPulse: return "Root pulse"
    case .rootFifth: return "Root–fifth"
    case .octaves: return "Octaves"
    case .followFeel: return "Follow feel"
    case .walking: return "Walking"
    }
  }
}

/// Which chords a `generateBass` writes: every chord of the progression, or the given chord ids in strip order.
enum GenerateScope: Equatable {
  case everyChord
  case chords([Int])
}

/// One entry of the Generate menu: a pattern for a scope, and the command choosing it sends.
struct GenerateEntry: Equatable, Identifiable {
  let pattern: BassPattern
  let scope: GenerateScope

  /// Unique across the menu's groups (the same pattern appears for the selection and for the whole progression).
  var id: String {
    switch scope {
    case .everyChord: return "all/\(pattern.rawValue)"
    case .chords(let ids): return "\(ids.map(String.init).joined(separator: ","))/\(pattern.rawValue)"
    }
  }

  var title: String { pattern.title }

  /// The `generateBass` command for track `trackId` (`GenerateOptions.command`).
  func command(trackId: Int) -> [String: Any] {
    GenerateOptions.command(trackId: trackId, pattern: pattern, scope: scope)
  }
}

/// What the roll toolbar's Generate menu shows (`GenerateOptions.menu`), so the view decides nothing: whether it is
/// enabled, its groups (a header, then the six patterns), the Walking note, its accessibility value and its tooltip.
/// `Equatable` over plain values: the toolbar compares it to decide whether to re-render.
struct GenerateMenu: Equatable {
  struct Group: Equatable, Identifiable {
    /// The group's header: what its entries replace ("Bass line for the 2 selected chords", "Whole progression").
    let title: String
    let entries: [GenerateEntry]
    var id: String { title }
  }

  let isEnabled: Bool
  /// The selection's (or every chord's) group, then "Whole progression" only when chords are selected; empty when
  /// the menu is disabled.
  let groups: [Group]
  /// The one-line note on Walking and Loop (`GenerateOptions.walkingNote`); nil when the menu is disabled.
  let walkingNote: String?
  /// The accessibility value: the first group's header, or the reason the menu is disabled.
  let value: String
  /// The tooltip: what generating does, or the reason the menu is disabled.
  let help: String
}

/// The roll toolbar's Generate menu (tracks design D14, D16, plan Task 23): the six bass patterns, the scope a choice
/// writes (the chord selection, else every chord), the `generateBass` command in the shape
/// engine/include/chordlab/json_api.h documents, and the menu model. Pure; the engine does all the music. A track's
/// role never limits it (design D2: a role sets defaults only), so the menu takes no track.
///
/// One command per choice, ONE undo step, no confirmation: the engine replaces the notes the scope's chords own in
/// the track and leaves every other note alone; a refusal ("Add chords first", the track limits) comes back as any
/// refused command does. The output is baked: Walking reads Loop when it runs, and a later chord or Loop change does
/// not regenerate anything.
///
/// Depends on nothing but Foundation and Core's snapshot; unit-tested by the `ChordLabUICore` package
/// (Tests/ChordLabUICoreTests/GenerateOptionsTests.swift).
enum GenerateOptions {
  /// The menu's patterns, in order.
  static let patterns: [BassPattern] = BassPattern.allCases

  /// Why nothing can be generated on an empty progression: the engine's own refusal text.
  static let unavailableReason = "Add chords first"

  /// The second group's header, offered while chords are selected (a click on the strip usually leaves one selected,
  /// so the whole progression stays one choice away without deselecting first).
  static let wholeProgressionTitle = "Whole progression"

  /// The enabled menu's tooltip.
  static let help = "Generate a bass line on this track, replacing its notes in the chosen chords (one undo step)"

  /// What a choice writes: the selected chords (`Snapshot.selection`) in strip order, ids no chord has dropped (the
  /// engine would refuse them), or every chord when none remains.
  static func scope(_ snapshot: Snapshot) -> GenerateScope {
    let selected = Set(snapshot.selection)
    guard !selected.isEmpty else { return .everyChord }
    let ids = snapshot.events.map(\.id).filter { selected.contains($0) }
    return ids.isEmpty ? .everyChord : .chords(ids)
  }

  /// A group's header for `scope`: whether the selection or every chord, and how many chords are selected.
  static func scopeTitle(_ scope: GenerateScope) -> String {
    switch scope {
    case .everyChord: return "Bass line for every chord"
    case .chords(let ids) where ids.count == 1: return "Bass line for the selected chord"
    case .chords(let ids): return "Bass line for the \(ids.count) selected chords"
    }
  }

  /// `generateBass {trackId, pattern, scope?}`: `scope` holds the chord ids for a selection and is left out for every
  /// chord (the engine reads absent as every chord). No `baseRevision`.
  static func command(trackId: Int, pattern: BassPattern, scope: GenerateScope) -> [String: Any] {
    var command: [String: Any] = ["cmd": "generateBass", "trackId": trackId, "pattern": pattern.rawValue]
    if case .chords(let ids) = scope { command["scope"] = ids }
    return command
  }

  /// Whether anything can be generated: the progression has a chord.
  static func isAvailable(_ snapshot: Snapshot) -> Bool { !snapshot.events.isEmpty }

  /// The Walking note for the snapshot's Loop (`progressionLoop`, read when Walking runs): with Loop on the last chord
  /// approaches the first, with it off the walk simply continues.
  static func walkingNote(loop: Bool) -> String {
    loop ? "Loop is on now, so Walking leads the last chord back into the first"
      : "Loop is off now, so Walking lets the last chord walk on"
  }

  /// The menu for `snapshot`: disabled with `unavailableReason` on an empty progression; otherwise the scope's group
  /// of the six patterns, plus the whole progression's when chords are selected, and the Walking note.
  static func menu(_ snapshot: Snapshot) -> GenerateMenu {
    guard isAvailable(snapshot) else {
      return GenerateMenu(isEnabled: false, groups: [], walkingNote: nil, value: unavailableReason,
                          help: unavailableReason)
    }
    let scope = scope(snapshot)
    func entries(_ scope: GenerateScope) -> [GenerateEntry] {
      patterns.map { GenerateEntry(pattern: $0, scope: scope) }
    }
    var groups = [GenerateMenu.Group(title: scopeTitle(scope), entries: entries(scope))]
    if scope != .everyChord {
      groups.append(GenerateMenu.Group(title: wholeProgressionTitle, entries: entries(.everyChord)))
    }
    return GenerateMenu(isEnabled: true, groups: groups, walkingNote: walkingNote(loop: snapshot.progressionLoop),
                        value: scopeTitle(scope), help: help)
  }
}
