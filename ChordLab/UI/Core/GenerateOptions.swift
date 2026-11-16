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

  /// The chords whose notes a choice writes, as the anchors the roll reveals afterwards: the given ids, or nil for
  /// every chord.
  var chordIds: Set<Int>? {
    switch self {
    case .everyChord: return nil
    case .chords(let ids): return Set(ids)
    }
  }
}

/// One entry of the Generate menu: a pattern for a scope, and the command choosing it sends. `lineage` names the
/// document the entry was offered for (the state's clipboard token, which a state load renews): an entry of another
/// document is never sent (`GenerateOptions.command(for:trackId:in:lineage:)`), since "every chord" and reused chord
/// ids look the same in any document.
struct GenerateEntry: Equatable, Identifiable {
  let pattern: BassPattern
  let scope: GenerateScope
  let lineage: String

  init(pattern: BassPattern, scope: GenerateScope, lineage: String = "") {
    self.pattern = pattern
    self.scope = scope
    self.lineage = lineage
  }

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
    /// The group's header: what its entries replace ("Replace notes in the 2 selected chords", "Replace notes in all
    /// 4 chords").
    let title: String
    let entries: [GenerateEntry]
    var id: String { title }
  }

  let isEnabled: Bool
  /// The selection's (or every chord's) group, then every chord's only when some, not all, chords are selected;
  /// empty when the menu is disabled.
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

  /// The enabled menu's tooltip.
  static let help = "Generate a bass line on this track, replacing its notes in the chosen chords (one undo step)"

  /// What a choice writes: the selected chords (`Snapshot.selection`) in strip order, ids no chord has dropped (the
  /// engine would refuse them), or every chord when none remains or when all of them are selected (the same notes
  /// either way, so the menu shows one group and the command carries no `scope`).
  static func scope(_ snapshot: Snapshot) -> GenerateScope {
    let selected = Set(snapshot.selection)
    guard !selected.isEmpty else { return .everyChord }
    let ids = snapshot.events.map(\.id).filter { selected.contains($0) }
    return ids.isEmpty || ids.count == snapshot.events.count ? .everyChord : .chords(ids)
  }

  /// A group's header for `scope` in a progression of `chordCount` chords: that the track's notes there are replaced,
  /// whether in the selection or in every chord, and in how many chords. One wording for every chord, whether or not
  /// chords are selected.
  static func scopeTitle(_ scope: GenerateScope, chordCount: Int) -> String {
    switch scope {
    case .everyChord where chordCount == 1: return "Replace notes in the only chord"
    case .everyChord where chordCount == 2: return "Replace notes in both chords"
    case .everyChord: return "Replace notes in all \(chordCount) chords"
    case .chords(let ids) where ids.count == 1: return "Replace notes in the selected chord"
    case .chords(let ids): return "Replace notes in the \(ids.count) selected chords"
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
  /// of the six patterns, plus every chord's when the scope is a selection (a click on the strip usually leaves a
  /// chord selected, so the whole progression stays one choice away without deselecting first), and the Walking note.
  /// Every entry carries `lineage`, the document it is offered for (`ChordLabState.clipboardToken`; the default is
  /// for tests that load no second document).
  static func menu(_ snapshot: Snapshot, lineage: String = "") -> GenerateMenu {
    guard isAvailable(snapshot) else {
      return GenerateMenu(isEnabled: false, groups: [], walkingNote: nil, value: unavailableReason,
                          help: unavailableReason)
    }
    let scope = scope(snapshot)
    func entries(_ scope: GenerateScope) -> [GenerateEntry] {
      patterns.map { GenerateEntry(pattern: $0, scope: scope, lineage: lineage) }
    }
    func group(_ scope: GenerateScope) -> GenerateMenu.Group {
      GenerateMenu.Group(title: scopeTitle(scope, chordCount: snapshot.events.count), entries: entries(scope))
    }
    var groups = [group(scope)]
    if scope != .everyChord { groups.append(group(.everyChord)) }
    return GenerateMenu(isEnabled: true, groups: groups, walkingNote: walkingNote(loop: snapshot.progressionLoop),
                        value: groups[0].title, help: help)
  }

  /// The command choosing `entry` sends for track `trackId`, or nil when the menu of the current `snapshot` and
  /// `lineage` no longer offers it. An open menu keeps the entries it was drawn with; a host state load can replace
  /// the document meanwhile. The loaded document has another lineage, so no entry of the old one is sent, not even
  /// "every chord" (which any progression offers) or chord ids the new document happens to reuse; a changed selection
  /// in the same document drops the old selection's entries. A dropped click does nothing: the reopened menu shows the
  /// new scope. The scope is not resolved again at the click: that would write a scope the header the user just read
  /// did not describe.
  static func command(for entry: GenerateEntry, trackId: Int, in snapshot: Snapshot,
                      lineage: String) -> [String: Any]? {
    menu(snapshot, lineage: lineage).groups.contains { $0.entries.contains(entry) }
      ? entry.command(trackId: trackId) : nil
  }
}
