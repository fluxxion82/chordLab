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

/// One pattern of the lead generator (tracks design D22), in the Generate menu's order: Motif first, the suggested
/// starting point (D22: the default lead pattern), then the others in the design's order. The raw value is the
/// engine's wire name (`leadPatternName`), `title` what the menu shows. Only Motif takes a `variation` (its take).
enum LeadPattern: String, CaseIterable, Identifiable {
  case motif
  case heldTone
  case guideTones
  case arpeggio
  case stepwise
  case followFeel

  var id: String { rawValue }

  var title: String {
    switch self {
    case .motif: return "Motif"
    case .heldTone: return "Held tone"
    case .guideTones: return "Guide tones"
    case .arpeggio: return "Arpeggio"
    case .stepwise: return "Stepwise"
    case .followFeel: return "Follow feel"
    }
  }
}

/// A pattern family: which generator, and so which command, an entry uses (`generateBass` or `generateLead`). The
/// menu nests each family in a submenu titled `title` when a track offers both (`GenerateMenu.nestsFamilies`).
enum GenerateFamily: String, CaseIterable {
  case lead
  case bass

  /// The submenu's title: "Lead line", "Bass line".
  var title: String {
    switch self {
    case .lead: return "Lead line"
    case .bass: return "Bass line"
    }
  }
}

/// A pattern of either family. The same wire name in two families ("followFeel") is two patterns.
enum GeneratePattern: Hashable {
  case bass(BassPattern)
  case lead(LeadPattern)

  var family: GenerateFamily {
    switch self {
    case .bass: return .bass
    case .lead: return .lead
    }
  }

  /// The engine's name for it: the command's `pattern`.
  var wireName: String {
    switch self {
    case .bass(let pattern): return pattern.rawValue
    case .lead(let pattern): return pattern.rawValue
    }
  }

  var title: String {
    switch self {
    case .bass(let pattern): return pattern.title
    case .lead(let pattern): return pattern.title
    }
  }
}

/// Which chords a generate command writes: every chord of the progression, or the given chord ids in strip order.
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

/// One entry of the Generate menu: a pattern for a scope on one track, and the command choosing it sends. What
/// identifies it, so the click-time check (`GenerateOptions.command(for:in:take:lineage:)`) can tell whether the menu
/// current at the click still offers it: the pattern (and so the family), the scope, `take` (the Motif take it writes,
/// nil for every other pattern), whether it is "Motif, another take", the track and `lineage`, the document it was
/// offered for (the state's clipboard token, which a state load renews: "every chord" and reused chord ids look the
/// same in any document).
struct GenerateEntry: Equatable, Identifiable {
  let pattern: GeneratePattern
  let scope: GenerateScope
  let trackId: Int
  /// The Motif take this entry writes (the command's `variation`), 0…63; nil for every other pattern.
  let take: Int?
  /// "Motif, next take": writes the take after the track's current one and makes it the track's take
  /// (`GenerateOutcome.takeToStore(for:)`).
  let isAnotherTake: Bool
  let lineage: String

  init(pattern: GeneratePattern, scope: GenerateScope, trackId: Int, take: Int? = nil, isAnotherTake: Bool = false,
       lineage: String = "") {
    self.pattern = pattern
    self.scope = scope
    self.trackId = trackId
    self.take = take
    self.isAnotherTake = isAnotherTake
    self.lineage = lineage
  }

  var family: GenerateFamily { pattern.family }

  /// Unique across the menu's groups and families (the same pattern appears for the selection and for the whole
  /// progression; Follow feel in both families; Motif twice).
  var id: String {
    let chords: String
    switch scope {
    case .everyChord: chords = "all"
    case .chords(let ids): chords = ids.map(String.init).joined(separator: ",")
    }
    return "\(chords)/\(family.rawValue)/\(pattern.wireName)" + (isAnotherTake ? "/another" : "")
  }

  /// What the menu shows: exactly what the entry writes, so it cannot be misread as "you are on take N" (after an
  /// undo, say): the pattern's title; for plain Motif "Motif, take 3 of 64" (counting from 1), for the other Motif
  /// entry "Motif, next take (4 of 64)" (after 64 comes 1).
  var title: String {
    guard let take else { return pattern.title }
    let count = GenerateOptions.takeCount
    return isAnotherTake ? "\(pattern.title), next take (\(take + 1) of \(count))"
      : "\(pattern.title), take \(take + 1) of \(count)"
  }

  /// The take this entry makes the track's take once its command was SENT and answered by the same document, applied
  /// or refused (`GenerateOutcome.takeToStore(for:)`): the take "Motif, next take" wrote. A refused take is stored too:
  /// Motif's note count depends on the take, so one take can hit a track limit while the next fits, and keeping the
  /// old take would re-send the refused one forever. Nil (the take stays) for every other entry, plain Motif included.
  var takeAfterSending: Int? { isAnotherTake ? take : nil }

  /// The command: `generateBass` for a bass pattern, `generateLead` (with `variation` only for Motif) for a lead one.
  var command: [String: Any] {
    switch pattern {
    case .bass(let bass): return GenerateOptions.command(trackId: trackId, pattern: bass, scope: scope)
    case .lead(let lead): return GenerateOptions.command(trackId: trackId, pattern: lead, scope: scope, take: take ?? 0)
    }
  }
}

/// What the roll toolbar's Generate menu shows for one track (`GenerateOptions.menu`), so the view decides nothing:
/// whether it is enabled, the families it offers and whether it nests them, its groups (a header, then a section per
/// family), the Walking note, its accessibility value and its tooltip. `Equatable` over plain values: the toolbar
/// compares it to decide whether to re-render.
struct GenerateMenu: Equatable {
  /// One family's entries inside a group: listed flat under the group's header with one family, in a submenu titled
  /// `title` with two (`nestsFamilies`).
  struct Section: Equatable, Identifiable {
    let family: GenerateFamily
    let entries: [GenerateEntry]
    /// The last item inside the section's submenu: the Walking note in a nested Bass line submenu; nil otherwise
    /// (a flat menu shows the note at its end, `GenerateMenu.walkingNote`).
    let note: String?
    var title: String { family.title }
    var id: String { family.rawValue }
  }

  struct Group: Equatable, Identifiable {
    /// The group's header: what its entries replace ("Replace notes in the 2 selected chords", "Replace notes in all
    /// 4 chords").
    let title: String
    /// One per family the track offers, in `GenerateMenu.families`' order.
    let sections: [Section]
    var id: String { title }
    /// Every entry of the group, family by family.
    var entries: [GenerateEntry] { sections.flatMap(\.entries) }
  }

  let isEnabled: Bool
  /// The families the track's role offers (`GenerateOptions.families(for:)`).
  let families: [GenerateFamily]
  /// The selection's (or every chord's) group, then every chord's only when some, not all, chords are selected;
  /// empty when the menu is disabled.
  let groups: [Group]
  /// The one-line note on Walking and Loop (`GenerateOptions.walkingNote`) at the end of the menu, for a flat menu of
  /// bass patterns; nil when the menu offers no bass pattern, nests its families (the note is then the last item of
  /// each Bass line submenu, `Section.note`) or is disabled.
  let walkingNote: String?
  /// The accessibility value: the first group's header, or the reason the menu is disabled.
  let value: String
  /// The tooltip: what generating writes on this track, or the reason the menu is disabled.
  let help: String

  /// Whether each family's patterns sit in their own submenu ("Lead line", "Bass line"): when the track offers two
  /// families (an Other track); with one they are listed flat under the group's header.
  var nestsFamilies: Bool { families.count > 1 }
}

/// The roll toolbar's Generate menu (tracks design D14, D16, D22, plan Tasks 23 and 27): the families a track's role
/// offers (Bass → bass lines, Lead → lead lines, Other → both, lead first), the six patterns of each, Motif's takes,
/// the scope a choice writes (the chord selection, else every chord), the `generateBass` and `generateLead` commands
/// in the shape engine/include/chordlab/json_api.h documents, the menu model and the click-time check. Pure; the
/// engine does all the music. The role limits only what the menu offers: every command still takes any track (D2, D22).
///
/// **Motif takes:** each track has a take number 0…63 (`RollViewState.motifTake`, UI-only, default 0). The lead
/// family offers "Motif, take N of 64", which rewrites the current take, and "Motif, next take (N+1 of 64)", which
/// writes the next one (63 wraps to 0) and, once sent and answered by the same document (applied or refused), makes
/// it the track's take (`GenerateOutcome`). A loaded document starts every track at take 0 (`RollViewStates.resetMotifTakes`).
///
/// One command per choice, ONE undo step, no confirmation: the engine replaces the notes the scope's chords own in
/// the track and leaves every other note alone; a refusal ("Add chords first", the track limits) comes back as any
/// refused command does. The output is baked: Walking reads Loop when it runs, and a later chord, Loop or role change
/// does not regenerate anything.
///
/// Depends on nothing but Foundation and Core's snapshot and tracks; unit-tested by the `ChordLabUICore` package
/// (Tests/ChordLabUICoreTests/GenerateOptionsTests.swift).
enum GenerateOptions {
  /// The bass family's patterns, in menu order.
  static let bassPatterns: [BassPattern] = BassPattern.allCases

  /// The lead family's patterns, in menu order (Motif first).
  static let leadPatterns: [LeadPattern] = LeadPattern.allCases

  /// How many Motif takes there are (the engine reduces `variation` modulo 64).
  static let takeCount = 64

  /// Why nothing can be generated on an empty progression: the engine's own refusal text.
  static let unavailableReason = "Add chords first"

  /// `take` in 0…63 (modulo `takeCount`, negatives wrapping up).
  static func take(_ take: Int) -> Int {
    let reduced = take % takeCount
    return reduced < 0 ? reduced + takeCount : reduced
  }

  /// The take "Motif, another take" writes after `take`: the next, 63 wrapping to 0.
  static func nextTake(after take: Int) -> Int { self.take(take + 1) }

  /// The families a track of `role` offers: Bass → bass; Lead → lead; Other → both, lead first.
  static func families(for role: TrackRole) -> [GenerateFamily] {
    switch role {
    case .bass: return [.bass]
    case .lead: return [.lead]
    case .other: return [.lead, .bass]
    }
  }

  /// The enabled menu's tooltip for `families`: which kind of line it writes.
  static func help(for families: [GenerateFamily]) -> String {
    let line: String
    switch (families.contains(.bass), families.contains(.lead)) {
    case (true, true): line = "a bass or lead line"
    case (false, true): line = "a lead line"
    default: line = "a bass line"
    }
    return "Generate \(line) on this track, replacing its notes in the chosen chords (one undo step)"
  }

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
  /// chord (the engine reads absent as every chord). No `baseRevision`, no `variation`.
  static func command(trackId: Int, pattern: BassPattern, scope: GenerateScope) -> [String: Any] {
    var command: [String: Any] = ["cmd": "generateBass", "trackId": trackId, "pattern": pattern.rawValue]
    if case .chords(let ids) = scope { command["scope"] = ids }
    return command
  }

  /// `generateLead {trackId, pattern, scope?, variation?}`: `scope` as for `generateBass`; `variation` is `take`
  /// (0…63) for Motif and left out for every other pattern, which the engine refuses with a non-zero one. No
  /// `baseRevision`.
  static func command(trackId: Int, pattern: LeadPattern, scope: GenerateScope, take: Int = 0) -> [String: Any] {
    var command: [String: Any] = ["cmd": "generateLead", "trackId": trackId, "pattern": pattern.rawValue]
    if case .chords(let ids) = scope { command["scope"] = ids }
    if pattern == .motif { command["variation"] = self.take(take) }
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

  /// The entries of `family` for `scope` on track `trackId` whose Motif take is `take`: the six bass patterns; or the
  /// lead patterns with Motif as two entries, "Motif, take N of 64" (the current take) and "Motif, next take (N+1 of
  /// 64)".
  static func entries(_ family: GenerateFamily, scope: GenerateScope, trackId: Int, take: Int,
                      lineage: String) -> [GenerateEntry] {
    switch family {
    case .bass:
      return bassPatterns.map { GenerateEntry(pattern: .bass($0), scope: scope, trackId: trackId, lineage: lineage) }
    case .lead:
      return leadPatterns.flatMap { pattern -> [GenerateEntry] in
        guard pattern == .motif else {
          return [GenerateEntry(pattern: .lead(pattern), scope: scope, trackId: trackId, lineage: lineage)]
        }
        return [GenerateEntry(pattern: .lead(.motif), scope: scope, trackId: trackId, take: self.take(take),
                              lineage: lineage),
                GenerateEntry(pattern: .lead(.motif), scope: scope, trackId: trackId, take: nextTake(after: take),
                              isAnotherTake: true, lineage: lineage)]
      }
    }
  }

  /// The menu of track `trackId`, whose role is `role` and Motif take `take`, for `snapshot`: disabled with
  /// `unavailableReason` on an empty progression; otherwise the scope's group, plus every chord's when the scope is a
  /// selection (a click on the strip usually leaves a chord selected, so the whole progression stays one choice away
  /// without deselecting first), each with a section per family the role offers; the Walking note when bass patterns
  /// are offered (at the end of a flat menu, as the last item of each Bass line submenu of a nested one); the tooltip
  /// naming the kind of line. Every entry carries the track and `lineage`, the document it is
  /// offered for (`ChordLabState.clipboardToken`; the default is for tests that load no second document).
  static func menu(_ snapshot: Snapshot, trackId: Int, role: TrackRole, take: Int,
                   lineage: String = "") -> GenerateMenu {
    let families = families(for: role)
    guard isAvailable(snapshot) else {
      return GenerateMenu(isEnabled: false, families: families, groups: [], walkingNote: nil, value: unavailableReason,
                          help: unavailableReason)
    }
    let scope = scope(snapshot)
    let note = walkingNote(loop: snapshot.progressionLoop)
    let nests = families.count > 1
    func group(_ scope: GenerateScope) -> GenerateMenu.Group {
      GenerateMenu.Group(title: scopeTitle(scope, chordCount: snapshot.events.count), sections: families.map {
        GenerateMenu.Section(family: $0, entries: entries($0, scope: scope, trackId: trackId, take: take,
                                                          lineage: lineage),
                             note: nests && $0 == .bass ? note : nil)
      })
    }
    var groups = [group(scope)]
    if scope != .everyChord { groups.append(group(.everyChord)) }
    return GenerateMenu(isEnabled: true, families: families, groups: groups,
                        walkingNote: families.contains(.bass) && !nests ? note : nil,
                        value: groups[0].title, help: help(for: families))
  }

  /// The click-time check: the command choosing `entry` sends, or nil when the menu CURRENT now no longer offers it.
  /// That menu is rebuilt for the entry's track from `snapshot` (the track's role there, its chords and selection),
  /// `take` (the track's Motif take now) and `lineage` (the document now). An open menu keeps the entries it was drawn
  /// with, so a click is dropped when meanwhile the track changed role so its family is no longer offered (a Lead
  /// track became Bass), its take changed (a Motif entry would no longer write the take its title named), the
  /// selection changed, a host state load replaced the document (another lineage: not even "every chord", which any
  /// progression offers, or chord ids the new document happens to reuse, is sent), or the track was removed. A dropped
  /// click does nothing: the reopened menu shows the current choices. Nothing is resolved again at the click: that
  /// would write something the entry the user just read did not describe.
  static func command(for entry: GenerateEntry, in snapshot: Snapshot, take: Int, lineage: String) -> [String: Any]? {
    guard let track = snapshot.track(id: entry.trackId) else { return nil }
    let current = menu(snapshot, trackId: track.id, role: track.role, take: take, lineage: lineage)
    return current.groups.contains { $0.entries.contains(entry) } ? entry.command : nil
  }
}

/// What happened to a generate command the state sent (`ChordLabState.generate`), read from the state's counters
/// around the send (the engine answers inline, inside it), in the style of `PresetLoadOutcome`. Pure; unit-tested by
/// the `ChordLabUICore` package (Tests/ChordLabUICoreTests/GenerateOptionsTests.swift).
enum GenerateOutcome: Equatable {
  /// A new snapshot and no error, in the same document.
  case applied
  /// An error arrived (`errorSerial` bumped) in the same document, whether or not a snapshot came with it.
  case refused
  /// The document lineage changed during the send: a stale command answered with the loaded document's snapshot (and
  /// perhaps an error). Nothing that belonged to the old document applies.
  case otherDocument
  /// Nothing arrived (no plug-in answering).
  case noReply

  /// The outcome from the error serial, the snapshot serial and the document lineage before and after the send. A
  /// changed lineage wins, then an error, then a snapshot.
  init(errorsBefore: Int, errorsAfter: Int, snapshotsBefore: Int, snapshotsAfter: Int, lineageBefore: String,
       lineageAfter: String) {
    if lineageAfter != lineageBefore {
      self = .otherDocument
    } else if errorsAfter != errorsBefore {
      self = .refused
    } else if snapshotsAfter != snapshotsBefore {
      self = .applied
    } else {
      self = .noReply
    }
  }

  /// Whether the roll reveals what was written: only an applied generate.
  var reveals: Bool { self == .applied }

  /// The take the track keeps after `entry` had this outcome: `entry.takeAfterSending` ("Motif, next take": the take it
  /// carried) when the same document answered, applied or refused; nil (the take stays) for another document's answer,
  /// no answer, and every other entry.
  func takeToStore(for entry: GenerateEntry) -> Int? {
    switch self {
    case .applied, .refused: return entry.takeAfterSending
    case .otherDocument, .noReply: return nil
    }
  }
}
