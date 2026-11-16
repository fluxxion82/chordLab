import CoreGraphics
import XCTest
@testable import ChordLabUICore

/// The roll toolbar's Generate menu (UI/Core/GenerateOptions.swift, tracks design D14, D16, D22, plan Tasks 23 and 27):
/// the six bass and six lead patterns, the families a track's role offers, Motif's takes, the scope from the chord
/// selection, the `generateBass` and `generateLead` commands, availability, the Walking note about Loop, the menu model
/// the view renders and the click-time check.
///
/// The snapshot-tracks fixture: chords 1 (C), 2 (Am), 3 (F), 4 (G7) in strip order, chords [2, 3] selected, Loop on;
/// tracks Bass (1) and Lead (2).
final class GenerateOptionsTests: XCTestCase {
  private func fixture(_ patch: (inout [String: Any]) -> Void = { _ in }) throws -> Snapshot {
    let url = try SnapshotTests.fixturesDirectory().appendingPathComponent("snapshot-tracks.json")
    var object = try XCTUnwrap(try JSONSerialization.jsonObject(with: Data(contentsOf: url)) as? [String: Any])
    patch(&object)
    return try JSONDecoder().decode(Snapshot.self, from: JSONSerialization.data(withJSONObject: object))
  }

  /// The chord selection `ids` (anchor the last one), or none for an empty array.
  private func selecting(_ ids: [Int]) -> (inout [String: Any]) -> Void {
    { object in
      object["selectedEventIds"] = ids
      object["selectedEventId"] = ids.last.map { $0 as Any } ?? NSNull()
    }
  }

  /// Track `trackId`'s role set to `role` (the fixture's tracks: Bass 1, Lead 2).
  private func role(_ role: String, of trackId: Int) -> (inout [String: Any]) -> Void {
    { object in
      var tracks = object["tracks"] as! [[String: Any]]
      let i = tracks.firstIndex { ($0["id"] as? Int) == trackId }!
      tracks[i]["role"] = role
      object["tracks"] = tracks
    }
  }

  private func both(_ a: @escaping (inout [String: Any]) -> Void,
                    _ b: @escaping (inout [String: Any]) -> Void) -> (inout [String: Any]) -> Void {
    { object in a(&object); b(&object) }
  }

  private func emptyProgression(_ object: inout [String: Any]) {
    object["events"] = [Any]()
    object["selectedEventIds"] = [Int]()
    object["selectedEventId"] = NSNull()
  }

  /// The menu of track `trackId` in `snapshot` with its role from the snapshot.
  private func menu(_ snapshot: Snapshot, trackId: Int, take: Int = 0, lineage: String = "") throws -> GenerateMenu {
    let track = try XCTUnwrap(snapshot.track(id: trackId))
    return GenerateOptions.menu(snapshot, trackId: trackId, role: track.role, take: take, lineage: lineage)
  }

  private func assertCommand(_ command: [String: Any], _ expected: [String: Any],
                             file: StaticString = #filePath, line: UInt = #line) {
    XCTAssertTrue(NSDictionary(dictionary: command).isEqual(to: expected), "\(command) != \(expected)",
                  file: file, line: line)
  }

  private func json(_ command: [String: Any]?) throws -> String {
    let command = try XCTUnwrap(command)
    let data = try JSONSerialization.data(withJSONObject: command, options: [.sortedKeys])
    return String(decoding: data, as: UTF8.self)
  }

  private let bassTitles = ["Roots", "Root pulse", "Root–fifth", "Octaves", "Follow feel", "Walking"]
  private func leadTitles(take: Int) -> [String] {
    ["Motif, take \(take + 1) of 64", "Motif, next take (\(GenerateOptions.nextTake(after: take) + 1) of 64)", "Held tone", "Guide tones", "Arpeggio", "Stepwise",
     "Follow feel"]
  }

  // MARK: - Patterns

  func testTheSixBassPatternsInMenuOrderWithTheirWireNamesAndTitles() {
    XCTAssertEqual(GenerateOptions.bassPatterns.map(\.rawValue),
                   ["roots", "rootPulse", "rootFifth", "octaves", "followFeel", "walking"],
                   "the engine's bassPatternName strings (json_api.h)")
    XCTAssertEqual(GenerateOptions.bassPatterns.map(\.title), bassTitles)
  }

  func testTheSixLeadPatternsInMenuOrderWithTheirWireNamesAndTitles() {
    XCTAssertEqual(GenerateOptions.leadPatterns.map(\.rawValue),
                   ["motif", "heldTone", "guideTones", "arpeggio", "stepwise", "followFeel"],
                   "Motif first (the suggested starting point); the engine's leadPatternName strings (design D22)")
    XCTAssertEqual(GenerateOptions.leadPatterns.map(\.title),
                   ["Motif", "Held tone", "Guide tones", "Arpeggio", "Stepwise", "Follow feel"])
  }

  func testAPatternKnowsItsFamilyWireNameAndTitle() {
    XCTAssertEqual(GeneratePattern.bass(.walking).family, .bass)
    XCTAssertEqual(GeneratePattern.lead(.motif).family, .lead)
    XCTAssertEqual(GeneratePattern.lead(.followFeel).wireName, "followFeel")
    XCTAssertEqual(GeneratePattern.bass(.followFeel).wireName, "followFeel")
    XCTAssertNotEqual(GeneratePattern.lead(.followFeel), GeneratePattern.bass(.followFeel),
                      "the same wire name in two families is two patterns")
    XCTAssertEqual(GeneratePattern.lead(.guideTones).title, "Guide tones")
    XCTAssertEqual(GenerateFamily.lead.title, "Lead line")
    XCTAssertEqual(GenerateFamily.bass.title, "Bass line")
  }

  // MARK: - Families by role

  func testTheFamiliesATrackOffersByItsRole() {
    XCTAssertEqual(GenerateOptions.families(for: .bass), [.bass])
    XCTAssertEqual(GenerateOptions.families(for: .lead), [.lead])
    XCTAssertEqual(GenerateOptions.families(for: .other), [.lead, .bass], "both, lead first")
  }

  // MARK: - Motif takes

  func testTakesWrapAt64() {
    XCTAssertEqual(GenerateOptions.takeCount, 64)
    XCTAssertEqual(GenerateOptions.nextTake(after: 0), 1)
    XCTAssertEqual(GenerateOptions.nextTake(after: 5), 6)
    XCTAssertEqual(GenerateOptions.nextTake(after: 62), 63)
    XCTAssertEqual(GenerateOptions.nextTake(after: 63), 0, "63 wraps to 0")
    XCTAssertEqual(GenerateOptions.take(64), 0)
    XCTAssertEqual(GenerateOptions.take(-1), 63)
    XCTAssertEqual(GenerateOptions.take(130), 2)
  }

  func testATracksTakeLivesInItsRollViewStateDefaultZero() {
    var state = RollViewState()
    XCTAssertEqual(state.motifTake, 0)
    state.motifTake = 63
    XCTAssertEqual(state.motifTake, 63)
    state.motifTake = 64
    XCTAssertEqual(state.motifTake, 0, "kept in 0…63")
    state.motifTake = -1
    XCTAssertEqual(state.motifTake, 63)
    var states = RollViewStates()
    XCTAssertEqual(states[2].motifTake, 0, "a track without an entry reads take 0")
    let timing = RollTiming(ticksPerBeat: 960, barTicks: 3840, endTick: 15360, minNoteTicks: 30)
    states.edit(trackId: 2, timing: timing) { state, _, _ in state.motifTake = 5 }
    XCTAssertEqual(states[2].motifTake, 5)
    XCTAssertEqual(states[1].motifTake, 0, "per track")
  }

  func testTheMotifEntriesShowTheTakeTheyWrite() throws {
    let snapshot = try fixture(selecting([]))
    for (take, title, nextTitle, next) in [(0, "Motif, take 1 of 64", "Motif, next take (2 of 64)", 1),
                                           (5, "Motif, take 6 of 64", "Motif, next take (7 of 64)", 6),
                                           (63, "Motif, take 64 of 64", "Motif, next take (1 of 64)", 0)] {
      let entries = try menu(snapshot, trackId: 2, take: take).groups[0].entries
      XCTAssertEqual(entries[0].title, title)
      XCTAssertEqual(entries[0].take, take, "plain Motif rewrites the current take")
      XCTAssertFalse(entries[0].isAnotherTake)
      XCTAssertNil(entries[0].takeAfterSending, "plain Motif leaves the track's take alone")
      XCTAssertEqual(entries[1].title, nextTitle, "named by the take it writes; after 64 comes 1")
      XCTAssertEqual(entries[1].take, next, "another take writes take + 1, wrapping 63 to 0")
      XCTAssertTrue(entries[1].isAnotherTake)
      XCTAssertEqual(entries[1].takeAfterSending, next, "and makes it the track's take")
      XCTAssertEqual(try json(entries[0].command),
                     #"{"cmd":"generateLead","pattern":"motif","trackId":2,"variation":\#(take)}"#)
      XCTAssertEqual(try json(entries[1].command),
                     #"{"cmd":"generateLead","pattern":"motif","trackId":2,"variation":\#(next)}"#)
      for entry in entries.dropFirst(2) {
        XCTAssertNil(entry.take, "\(entry.title) carries no take")
        XCTAssertNil(entry.command["variation"], "\(entry.title) sends no variation")
      }
    }
  }

  // MARK: - Scope

  func testTheScopeIsTheSelectedChordsInStripOrder() throws {
    XCTAssertEqual(GenerateOptions.scope(try fixture()), .chords([2, 3]))
    XCTAssertEqual(GenerateOptions.scope(try fixture(selecting([4, 1]))), .chords([1, 4]),
                   "strip order, whatever order the selection arrives in")
    XCTAssertEqual(GenerateOptions.scope(try fixture(selecting([3]))), .chords([3]))
  }

  func testWithoutASelectionTheScopeIsEveryChord() throws {
    XCTAssertEqual(GenerateOptions.scope(try fixture(selecting([]))), .everyChord)
    let legacy = try fixture { object in
      object.removeValue(forKey: "selectedEventIds")
      object["selectedEventId"] = NSNull()
    }
    XCTAssertEqual(GenerateOptions.scope(legacy), .everyChord, "an older build without selectedEventIds, nothing selected")
    let legacyOne = try fixture { object in
      object.removeValue(forKey: "selectedEventIds")
      object["selectedEventId"] = 4
    }
    XCTAssertEqual(GenerateOptions.scope(legacyOne), .chords([4]), "an older build's one selectedEventId")
  }

  func testSelectedIdsNoChordHasAreDropped() throws {
    XCTAssertEqual(GenerateOptions.scope(try fixture(selecting([3, 42]))), .chords([3]))
    XCTAssertEqual(GenerateOptions.scope(try fixture(selecting([42]))), .everyChord,
                   "a selection naming no chord is no selection (the engine would refuse it: No chord with id 42)")
    XCTAssertEqual(GenerateOptions.scope(try fixture(selecting([3, 3]))), .chords([3]), "a repeat counts once")
    let legacyUnknown = try fixture { object in
      object.removeValue(forKey: "selectedEventIds")
      object["selectedEventId"] = 42
    }
    XCTAssertEqual(GenerateOptions.scope(legacyUnknown), .everyChord, "an older build's one id, naming no chord")
  }

  func testEveryChordSelectedIsEveryChord() throws {
    // Selecting all four chords and selecting none write the same notes: one scope, one group, no `scope` sent.
    let all = try fixture(selecting([1, 2, 3, 4]))
    XCTAssertEqual(GenerateOptions.scope(all), .everyChord)
    XCTAssertEqual(GenerateOptions.scope(try fixture(selecting([4, 2, 1, 3]))), .everyChord)
    XCTAssertEqual(GenerateOptions.scope(try fixture(selecting([1, 2, 3, 4, 42]))), .everyChord)
    XCTAssertEqual(GenerateOptions.scope(try fixture(selecting([1, 2, 3]))), .chords([1, 2, 3]), "one short of all")
    for trackId in [1, 2] {
      let menu = try menu(all, trackId: trackId)
      XCTAssertEqual(menu.groups.map(\.title), ["Replace notes in all 4 chords"])
      XCTAssertNil(menu.groups[0].entries[0].command["scope"])
    }
  }

  func testTheScopeTitleSaysWhatWillBeReplaced() {
    XCTAssertEqual(GenerateOptions.scopeTitle(.everyChord, chordCount: 4), "Replace notes in all 4 chords")
    XCTAssertEqual(GenerateOptions.scopeTitle(.everyChord, chordCount: 2), "Replace notes in both chords")
    XCTAssertEqual(GenerateOptions.scopeTitle(.everyChord, chordCount: 1), "Replace notes in the only chord")
    XCTAssertEqual(GenerateOptions.scopeTitle(.chords([3]), chordCount: 4), "Replace notes in the selected chord")
    XCTAssertEqual(GenerateOptions.scopeTitle(.chords([1, 2, 4]), chordCount: 4),
                   "Replace notes in the 3 selected chords")
    XCTAssertEqual(GenerateOptions.scopeTitle(.chords([2, 3]), chordCount: 4), "Replace notes in the 2 selected chords")
  }

  // MARK: - Commands

  func testTheBassCommandForEveryChordHasNoScope() {
    let command = GenerateOptions.command(trackId: 1, pattern: .walking, scope: .everyChord)
    assertCommand(command, ["cmd": "generateBass", "trackId": 1, "pattern": "walking"])
    XCTAssertNil(command["scope"], "absent, never an empty array")
    XCTAssertNil(command["baseRevision"], "generateBass takes no baseRevision")
    XCTAssertNil(command["variation"])
  }

  func testTheBassCommandForASelectionCarriesItsIds() throws {
    assertCommand(GenerateOptions.command(trackId: 1, pattern: .walking, scope: .chords([3, 5])),
                  ["cmd": "generateBass", "trackId": 1, "pattern": "walking", "scope": [3, 5]])
    assertCommand(GenerateOptions.command(trackId: 2, pattern: .rootFifth, scope: .chords([4])),
                  ["cmd": "generateBass", "trackId": 2, "pattern": "rootFifth", "scope": [4]])
  }

  func testTheBassCommandIsExactlyTheDocumentedJSON() throws {
    XCTAssertEqual(try json(GenerateOptions.command(trackId: 1, pattern: .walking, scope: .chords([3, 5]))),
                   #"{"cmd":"generateBass","pattern":"walking","scope":[3,5],"trackId":1}"#)
  }

  func testEveryLeadPatternsCommandIsExactlyTheDocumentedJSON() throws {
    // Every chord: no scope; variation only on Motif.
    XCTAssertEqual(try json(GenerateOptions.command(trackId: 2, pattern: .motif, scope: .everyChord, take: 0)),
                   #"{"cmd":"generateLead","pattern":"motif","trackId":2,"variation":0}"#)
    XCTAssertEqual(try json(GenerateOptions.command(trackId: 2, pattern: .motif, scope: .chords([1, 3]), take: 9)),
                   #"{"cmd":"generateLead","pattern":"motif","scope":[1,3],"trackId":2,"variation":9}"#,
                   "the design's example (D22)")
    for (pattern, wire) in [(LeadPattern.heldTone, "heldTone"), (.guideTones, "guideTones"), (.arpeggio, "arpeggio"),
                            (.stepwise, "stepwise"), (.followFeel, "followFeel")] {
      XCTAssertEqual(try json(GenerateOptions.command(trackId: 2, pattern: pattern, scope: .everyChord)),
                     #"{"cmd":"generateLead","pattern":"\#(wire)","trackId":2}"#)
      XCTAssertEqual(try json(GenerateOptions.command(trackId: 3, pattern: pattern, scope: .chords([2, 3]), take: 7)),
                     #"{"cmd":"generateLead","pattern":"\#(wire)","scope":[2,3],"trackId":3}"#,
                     "a take never reaches another pattern (the engine refuses a non-zero variation there)")
    }
  }

  func testEveryEntrysCommandInTheMenu() throws {
    let snapshot = try fixture(role("other", of: 2))
    let menu = try menu(snapshot, trackId: 2, take: 4)
    let lead = menu.groups[0].sections[0].entries.map { try! json($0.command) }
    XCTAssertEqual(lead, [
      #"{"cmd":"generateLead","pattern":"motif","scope":[2,3],"trackId":2,"variation":4}"#,
      #"{"cmd":"generateLead","pattern":"motif","scope":[2,3],"trackId":2,"variation":5}"#,
      #"{"cmd":"generateLead","pattern":"heldTone","scope":[2,3],"trackId":2}"#,
      #"{"cmd":"generateLead","pattern":"guideTones","scope":[2,3],"trackId":2}"#,
      #"{"cmd":"generateLead","pattern":"arpeggio","scope":[2,3],"trackId":2}"#,
      #"{"cmd":"generateLead","pattern":"stepwise","scope":[2,3],"trackId":2}"#,
      #"{"cmd":"generateLead","pattern":"followFeel","scope":[2,3],"trackId":2}"#,
    ])
    let bass = menu.groups[1].sections[1].entries.map { try! json($0.command) }
    XCTAssertEqual(bass, ["roots", "rootPulse", "rootFifth", "octaves", "followFeel", "walking"].map {
      #"{"cmd":"generateBass","pattern":"\#($0)","trackId":2}"#
    }, "every chord: no scope, and never a variation on a bass entry")
  }

  // MARK: - Availability

  func testAnEmptyProgressionCanGenerateNothing() throws {
    let empty = try fixture(emptyProgression)
    XCTAssertFalse(GenerateOptions.isAvailable(empty))
    XCTAssertTrue(GenerateOptions.isAvailable(try fixture()))
    XCTAssertTrue(GenerateOptions.isAvailable(try fixture(selecting([]))))
    XCTAssertEqual(GenerateOptions.unavailableReason, "Add chords first", "the engine's own refusal text")
  }

  // MARK: - Walking and Loop

  func testTheWalkingNoteFollowsLoop() throws {
    XCTAssertEqual(GenerateOptions.walkingNote(loop: true),
                   "Loop is on now, so Walking leads the last chord back into the first")
    XCTAssertEqual(GenerateOptions.walkingNote(loop: false),
                   "Loop is off now, so Walking lets the last chord walk on")
    XCTAssertEqual(try menu(try fixture(), trackId: 1).walkingNote, GenerateOptions.walkingNote(loop: true))
    XCTAssertEqual(try menu(try fixture { $0["progressionLoop"] = false }, trackId: 1).walkingNote,
                   GenerateOptions.walkingNote(loop: false))
  }

  func testTheWalkingNoteAppearsOnlyWithBassPatterns() throws {
    XCTAssertNotNil(try menu(try fixture(), trackId: 1).walkingNote, "Bass")
    XCTAssertNil(try menu(try fixture(), trackId: 2).walkingNote, "Lead offers no Walking")
    let other = try menu(try fixture(role("other", of: 2)), trackId: 2)
    XCTAssertNil(other.walkingNote, "nested: not at the top level")
    for group in other.groups {
      XCTAssertEqual(group.sections.map(\.note), [nil, GenerateOptions.walkingNote(loop: true)],
                     "Other: the note is the last item inside each Bass line submenu")
    }
    let bass = try menu(try fixture(), trackId: 1)
    XCTAssertTrue(bass.groups.allSatisfy { $0.sections.allSatisfy { $0.note == nil } },
                  "flat Bass: the note stays at the end of the menu, in no section")
    XCTAssertNotEqual(try menu(try fixture(both({ $0["progressionLoop"] = false }, role("other", of: 2))), trackId: 2), other,
                      "Loop still changes an Other menu (its Bass line note)")
  }

  // MARK: - The menu by role

  func testABassTracksMenuIsTheBassPatternsFlat() throws {
    let none = try menu(try fixture(selecting([])), trackId: 1)
    XCTAssertTrue(none.isEnabled)
    XCTAssertEqual(none.families, [.bass])
    XCTAssertFalse(none.nestsFamilies, "one family: listed flat")
    XCTAssertEqual(none.groups.map(\.title), ["Replace notes in all 4 chords"])
    XCTAssertEqual(none.groups[0].sections.map(\.family), [.bass])
    XCTAssertEqual(none.groups[0].entries.map(\.title), bassTitles)
    XCTAssertEqual(none.groups[0].entries.map(\.pattern), GenerateOptions.bassPatterns.map(GeneratePattern.bass))
    XCTAssertEqual(none.groups[0].entries.map(\.scope), Array(repeating: .everyChord, count: 6))
    XCTAssertEqual(none.value, "Replace notes in all 4 chords")
    XCTAssertEqual(none.help, "Generate a bass line on this track, replacing its notes in the chosen chords (one undo step)")

    let partial = try menu(try fixture(), trackId: 1)
    XCTAssertEqual(partial.groups.map(\.title), ["Replace notes in the 2 selected chords", "Replace notes in all 4 chords"],
                   "the same words for every chord, whether or not chords are selected")
    XCTAssertEqual(partial.groups[0].entries.map(\.scope), Array(repeating: .chords([2, 3]), count: 6))
    XCTAssertEqual(partial.groups[1].entries.map(\.scope), Array(repeating: .everyChord, count: 6))
    for group in partial.groups { XCTAssertEqual(group.entries.map(\.title), bassTitles) }
    XCTAssertEqual(partial.value, "Replace notes in the 2 selected chords")
    XCTAssertNotNil(partial.walkingNote)
  }

  func testALeadTracksMenuIsTheLeadPatternsFlat() throws {
    let none = try menu(try fixture(selecting([])), trackId: 2)
    XCTAssertTrue(none.isEnabled)
    XCTAssertEqual(none.families, [.lead])
    XCTAssertFalse(none.nestsFamilies)
    XCTAssertEqual(none.groups.map(\.title), ["Replace notes in all 4 chords"])
    XCTAssertEqual(none.groups[0].sections.map(\.family), [.lead])
    XCTAssertEqual(none.groups[0].entries.map(\.title), leadTitles(take: 0))
    XCTAssertTrue(none.groups[0].entries.allSatisfy { $0.family == .lead && $0.trackId == 2 })
    XCTAssertNil(none.walkingNote, "no bass patterns, no Walking note")
    XCTAssertEqual(none.value, "Replace notes in all 4 chords")
    XCTAssertEqual(none.help, "Generate a lead line on this track, replacing its notes in the chosen chords (one undo step)")

    let partial = try menu(try fixture(), trackId: 2)
    XCTAssertEqual(partial.groups.map(\.title), ["Replace notes in the 2 selected chords", "Replace notes in all 4 chords"])
    XCTAssertEqual(partial.groups[0].entries.map(\.scope), Array(repeating: .chords([2, 3]), count: 7))
    XCTAssertEqual(partial.groups[1].entries.map(\.scope), Array(repeating: .everyChord, count: 7))
    for group in partial.groups {
      XCTAssertEqual(group.entries.map(\.title), leadTitles(take: 0), "each scope group has both Motif entries")
    }
    XCTAssertNil(partial.walkingNote)
  }

  func testAnOtherTracksMenuNestsBothFamiliesLeadFirst() throws {
    for snapshot in [try fixture(both(selecting([]), role("other", of: 2))), try fixture(role("other", of: 2))] {
      let menu = try menu(snapshot, trackId: 2, take: 2)
      XCTAssertEqual(menu.families, [.lead, .bass])
      XCTAssertTrue(menu.nestsFamilies, "two families: each in its submenu")
      for group in menu.groups {
        XCTAssertEqual(group.sections.map(\.family), [.lead, .bass])
        XCTAssertEqual(group.sections.map(\.title), ["Lead line", "Bass line"])
        XCTAssertEqual(group.sections[0].entries.map(\.title), leadTitles(take: 2))
        XCTAssertEqual(group.sections[1].entries.map(\.title), bassTitles)
        XCTAssertEqual(group.entries.count, 13)
      }
      XCTAssertNil(menu.walkingNote, "nested: the note sits inside each Bass line submenu")
      XCTAssertTrue(menu.groups.allSatisfy { $0.sections[1].note == GenerateOptions.walkingNote(loop: true) })
      XCTAssertEqual(menu.help,
                     "Generate a bass or lead line on this track, replacing its notes in the chosen chords (one undo step)")
    }
    let partial = try menu(try fixture(role("other", of: 2)), trackId: 2)
    XCTAssertEqual(partial.groups.map(\.title), ["Replace notes in the 2 selected chords", "Replace notes in all 4 chords"])
    XCTAssertEqual(partial.value, "Replace notes in the 2 selected chords")
    let one = try menu(try fixture(both(selecting([4]), role("other", of: 2))), trackId: 2)
    XCTAssertEqual(one.groups.map(\.title), ["Replace notes in the selected chord", "Replace notes in all 4 chords"])
    XCTAssertEqual(one.groups[0].entries.map(\.scope), Array(repeating: .chords([4]), count: 13))
  }

  func testAnUnknownRoleReadsAsOther() throws {
    let snapshot = try fixture(role("drums", of: 2))
    XCTAssertEqual(snapshot.track(id: 2)?.role, .other)
    XCTAssertEqual(try menu(snapshot, trackId: 2).families, [.lead, .bass])
  }

  func testEntryIdsAreUniqueAcrossGroupsAndFamilies() throws {
    let ids = try menu(try fixture(role("other", of: 2)), trackId: 2).groups.flatMap { $0.entries.map(\.id) }
    XCTAssertEqual(ids.count, 26)
    XCTAssertEqual(Set(ids).count, 26)
    let lead = try menu(try fixture(), trackId: 2).groups.flatMap { $0.entries.map(\.id) }
    XCTAssertEqual(Set(lead).count, 14)
    let bass = try menu(try fixture(), trackId: 1).groups.flatMap { $0.entries.map(\.id) }
    XCTAssertEqual(Set(bass).count, 12)
    for group in try menu(try fixture(role("other", of: 2)), trackId: 2).groups {
      XCTAssertEqual(Set(group.sections.map(\.id)).count, 2)
    }
  }

  func testEntriesCarryTheTrackAndLineage() throws {
    let menu = try menu(try fixture(), trackId: 2, take: 3, lineage: "A")
    for entry in menu.groups.flatMap(\.entries) {
      XCTAssertEqual(entry.trackId, 2)
      XCTAssertEqual(entry.lineage, "A")
      XCTAssertEqual(entry.command["trackId"] as? Int, 2)
    }
  }

  func testAnEmptyProgressionDisablesTheMenuWithTheReasonOnEveryRole() throws {
    for (trackId, roleName) in [(1, "bass"), (2, "lead"), (2, "other")] {
      let snapshot = try fixture(both(emptyProgression, role(roleName, of: trackId)))
      let menu = try menu(snapshot, trackId: trackId)
      XCTAssertFalse(menu.isEnabled, roleName)
      XCTAssertEqual(menu.groups, [], roleName)
      XCTAssertNil(menu.walkingNote, roleName)
      XCTAssertEqual(menu.value, "Add chords first", roleName)
      XCTAssertEqual(menu.help, "Add chords first", roleName)
    }
    let stillSelected = try fixture { object in
      object["events"] = [Any]()
      object["selectedEventIds"] = [2]
      object["selectedEventId"] = 2
    }
    XCTAssertEqual(try menu(stillSelected, trackId: 1), try menu(try fixture(emptyProgression), trackId: 1),
                   "a selection naming no chord changes nothing")
    XCTAssertEqual(GenerateOptions.scope(stillSelected), .everyChord)
  }

  // MARK: - Choosing an entry (the click-time check)

  func testAnEntryTheCurrentMenuOffersGivesItsCommand() throws {
    let snapshot = try fixture()
    let bass = try menu(snapshot, trackId: 1)
    XCTAssertEqual(try json(GenerateOptions.command(for: bass.groups[0].entries[5], in: snapshot, take: 0, lineage: "")),
                   #"{"cmd":"generateBass","pattern":"walking","scope":[2,3],"trackId":1}"#)
    XCTAssertEqual(try json(GenerateOptions.command(for: bass.groups[1].entries[0], in: snapshot, take: 0, lineage: "")),
                   #"{"cmd":"generateBass","pattern":"roots","trackId":1}"#)
    let lead = try menu(snapshot, trackId: 2, take: 5)
    XCTAssertEqual(try json(GenerateOptions.command(for: lead.groups[0].entries[0], in: snapshot, take: 5, lineage: "")),
                   #"{"cmd":"generateLead","pattern":"motif","scope":[2,3],"trackId":2,"variation":5}"#)
    XCTAssertEqual(try json(GenerateOptions.command(for: lead.groups[1].entries[1], in: snapshot, take: 5, lineage: "")),
                   #"{"cmd":"generateLead","pattern":"motif","trackId":2,"variation":6}"#)
    XCTAssertEqual(try json(GenerateOptions.command(for: lead.groups[1].entries[4], in: snapshot, take: 5, lineage: "")),
                   #"{"cmd":"generateLead","pattern":"arpeggio","trackId":2}"#)
  }

  func testARoleChangeWhileTheMenuIsOpenDropsTheClick() throws {
    // The menu was drawn for Lead track 2; the track became Bass (the Role submenu, an undo) before the click.
    let asLead = try fixture()
    let lead = try menu(asLead, trackId: 2)
    let asBass = try fixture(role("bass", of: 2))
    for entry in lead.groups.flatMap(\.entries) {
      XCTAssertNotNil(GenerateOptions.command(for: entry, in: asLead, take: 0, lineage: ""), entry.title)
      XCTAssertNil(GenerateOptions.command(for: entry, in: asBass, take: 0, lineage: ""),
                   "\(entry.title): a lead entry on a track that is Bass now sends nothing")
    }
    // A Bass track's entries once it is Lead.
    let bass = try menu(asLead, trackId: 1)
    let bassNowLead = try fixture(role("lead", of: 1))
    for entry in bass.groups.flatMap(\.entries) {
      XCTAssertNil(GenerateOptions.command(for: entry, in: bassNowLead, take: 0, lineage: ""), entry.title)
    }
    // An Other track's bass entries once it is Lead, its lead entries once it is Bass.
    let other = try menu(try fixture(role("other", of: 2)), trackId: 2)
    let otherBass = other.groups[0].sections[1].entries, otherLead = other.groups[0].sections[0].entries
    XCTAssertTrue(otherBass.allSatisfy { GenerateOptions.command(for: $0, in: asLead, take: 0, lineage: "") == nil })
    XCTAssertTrue(otherLead.allSatisfy { GenerateOptions.command(for: $0, in: asBass, take: 0, lineage: "") == nil })
    // Other to Lead keeps the lead family offered: an Other menu's lead entry still sends exactly what it said.
    XCTAssertTrue(otherLead.allSatisfy { GenerateOptions.command(for: $0, in: asLead, take: 0, lineage: "") != nil },
                  "Other to Lead: the lead family is still offered, the lead entry sends")
    // Lead to Other likewise: a Lead menu's entry clicked after the role became Other is still sent.
    let asOther = try fixture(role("other", of: 2))
    for entry in lead.groups.flatMap(\.entries) {
      XCTAssertNotNil(GenerateOptions.command(for: entry, in: asOther, take: 0, lineage: ""),
                      "Lead to Other: \(entry.title) is still offered")
    }
  }

  func testATakeChangeWhileTheMenuIsOpenDropsTheMotifEntries() throws {
    let snapshot = try fixture(selecting([]))
    let entries = try menu(snapshot, trackId: 2, take: 3).groups[0].entries
    XCTAssertNotNil(GenerateOptions.command(for: entries[0], in: snapshot, take: 3, lineage: ""))
    XCTAssertNotNil(GenerateOptions.command(for: entries[1], in: snapshot, take: 3, lineage: ""))
    XCTAssertNil(GenerateOptions.command(for: entries[0], in: snapshot, take: 4, lineage: ""),
                 "Motif, take 4 after the take became 5 elsewhere: the entry no longer describes it")
    XCTAssertNil(GenerateOptions.command(for: entries[1], in: snapshot, take: 4, lineage: ""),
                 "another take would write take 5 again")
    for entry in entries.dropFirst(2) {
      XCTAssertNotNil(GenerateOptions.command(for: entry, in: snapshot, take: 4, lineage: ""),
                      "\(entry.title) does not depend on the take")
    }
  }

  func testAnEntryForAnotherTrackOrAMissingTrackSendsNothing() throws {
    let snapshot = try fixture()
    let lead = try menu(snapshot, trackId: 2).groups[0].entries[2]
    let gone = try fixture { object in
      var tracks = object["tracks"] as! [[String: Any]]
      tracks.removeAll { ($0["id"] as? Int) == 2 }
      object["tracks"] = tracks
    }
    XCTAssertNil(GenerateOptions.command(for: lead, in: gone, take: 0, lineage: ""), "the track was removed")
    let forged = GenerateEntry(pattern: .lead(.heldTone), scope: .chords([2, 3]), trackId: 1)
    XCTAssertNil(GenerateOptions.command(for: forged, in: snapshot, take: 0, lineage: ""),
                 "track 1 is Bass: no lead entry is offered for it")
  }

  func testAnEntryFromAnotherDocumentSendsNothing() throws {
    // Document A's menu is open with nothing selected; the host loads document B, which reuses the track and chord ids.
    // Every progression offers "every chord", so only the document lineage (the state's clipboard token, renewed by a
    // load) tells the two apart.
    let snapshot = try fixture(selecting([]))
    for trackId in [1, 2] {
      let offeredForA = try menu(snapshot, trackId: trackId, lineage: "A").groups[0].entries[0]
      XCTAssertEqual(offeredForA.scope, .everyChord)
      XCTAssertEqual(offeredForA.lineage, "A")
      XCTAssertNotNil(GenerateOptions.command(for: offeredForA, in: snapshot, take: 0, lineage: "A"))
      XCTAssertNil(GenerateOptions.command(for: offeredForA, in: snapshot, take: 0, lineage: "B"),
                   "the same ids in another document: nothing is replaced there")
      XCTAssertNotEqual(try menu(snapshot, trackId: trackId, lineage: "A"), try menu(snapshot, trackId: trackId, lineage: "B"),
                        "a loaded document redraws the toolbar's menu")
    }
    let selected = try fixture()
    let walking = try menu(selected, trackId: 1, lineage: "A").groups[0].entries[5]
    XCTAssertEqual(walking.scope, .chords([2, 3]))
    XCTAssertNil(GenerateOptions.command(for: walking, in: selected, take: 0, lineage: "B"))
  }

  func testAnEntryFromAnOlderSelectionSendsNothing() throws {
    // The menu was drawn for chords [2, 3]; the selection (or a loaded document) changed while it was open. The
    // entry's ids may name other chords now, so it is dropped: the reopened menu shows the new scope.
    for (trackId, pattern) in [(1, GeneratePattern.bass(.walking)), (2, .lead(.stepwise))] {
      let stale = GenerateEntry(pattern: pattern, scope: .chords([2, 3]), trackId: trackId)
      XCTAssertNotNil(GenerateOptions.command(for: stale, in: try fixture(), take: 0, lineage: ""))
      XCTAssertNil(GenerateOptions.command(for: stale, in: try fixture(selecting([])), take: 0, lineage: ""),
                   "nothing selected any more")
      XCTAssertNil(GenerateOptions.command(for: stale, in: try fixture(selecting([2])), take: 0, lineage: ""),
                   "another selection")
      XCTAssertNil(GenerateOptions.command(for: stale, in: try fixture { $0["events"] = [Any]() }, take: 0, lineage: ""),
                   "no chords left")
      let everyChord = GenerateEntry(pattern: pattern, scope: .everyChord, trackId: trackId)
      XCTAssertNotNil(GenerateOptions.command(for: everyChord, in: try fixture(selecting([])), take: 0, lineage: ""))
      XCTAssertNotNil(GenerateOptions.command(for: everyChord, in: try fixture(selecting([1])), take: 0, lineage: ""),
                      "every chord is offered under any selection")
      XCTAssertNil(GenerateOptions.command(for: everyChord, in: try fixture(emptyProgression), take: 0, lineage: ""),
                   "an empty progression offers nothing")
    }
  }

  // MARK: - What changes the menu

  func testTheMenuChangesWithTheSelectionLoopRoleTakeAndLineageOnly() throws {
    for trackId in [1, 2] {
      let base = try menu(try fixture(), trackId: trackId)
      XCTAssertEqual(try menu(try fixture { $0["revision"] = 99 }, trackId: trackId), base,
                     "a content revision alone changes nothing the menu shows")
      let edited = try fixture { object in
        var events = object["events"] as! [[String: Any]]
        events[0]["symbol"] = "Dm7"
        events[0]["root"] = 2
        events[0]["velocity"] = 64
        events[0]["lengthBeats"] = 2.0
        object["events"] = events
      }
      XCTAssertEqual(try menu(edited, trackId: trackId), base, "a chord edit that keeps the ids changes nothing either")
      XCTAssertNotEqual(try menu(try fixture(selecting([1])), trackId: trackId), base)
      XCTAssertNotEqual(try menu(try fixture(), trackId: trackId, lineage: "B"), base)
      XCTAssertNotEqual(try menu(try fixture(role("other", of: trackId)), trackId: trackId), base)
    }
    XCTAssertNotEqual(try menu(try fixture { $0["progressionLoop"] = false }, trackId: 1), try menu(try fixture(), trackId: 1),
                      "Loop changes the Walking note")
    XCTAssertEqual(try menu(try fixture { $0["progressionLoop"] = false }, trackId: 2), try menu(try fixture(), trackId: 2),
                   "a Lead menu has no Walking note: Loop changes nothing there")
    XCTAssertNotEqual(try menu(try fixture(), trackId: 2, take: 1), try menu(try fixture(), trackId: 2, take: 0),
                      "the take changes a Lead menu")
    XCTAssertEqual(try menu(try fixture(), trackId: 1, take: 1), try menu(try fixture(), trackId: 1, take: 0),
                   "and nothing on a Bass menu, which has no Motif")
  }

  func testAScrollOrZoomLeavesTheMenuAlone() throws {
    // The roll view state's take is the only part of it the menu reads.
    let snapshot = try fixture()
    let timing = RollTiming(snapshot: snapshot)
    var states = RollViewStates()
    states.setViewport(CGSize(width: 600, height: 300), trackId: 2, role: .lead, timing: timing)
    states.edit(trackId: 2, timing: timing) { state, _, _ in state.motifTake = 7 }
    let before = GenerateOptions.menu(snapshot, trackId: 2, role: .lead, take: states[2].motifTake)
    states.edit(trackId: 2, timing: timing) { state, viewport, timing in
      state.scroll(by: CGSize(width: 200, height: 150), viewport: viewport, timing: timing)
      state.zoomIn(viewport: viewport, timing: timing)
      state.grid = .sixteenthTriplet
    }
    XCTAssertEqual(GenerateOptions.menu(snapshot, trackId: 2, role: .lead, take: states[2].motifTake), before)
    XCTAssertEqual(states[2].motifTake, 7)
  }

  // MARK: - What happened to a sent generate, and which take the track keeps

  private func outcome(errors: (Int, Int) = (3, 3), snapshots: (Int, Int) = (7, 7),
                       lineage: (String, String) = ("A", "A")) -> GenerateOutcome {
    GenerateOutcome(errorsBefore: errors.0, errorsAfter: errors.1, snapshotsBefore: snapshots.0,
                    snapshotsAfter: snapshots.1, lineageBefore: lineage.0, lineageAfter: lineage.1)
  }

  func testTheOutcomeOfASentGenerate() {
    XCTAssertEqual(outcome(snapshots: (7, 8)), .applied, "a new snapshot, no error, the same document")
    XCTAssertEqual(outcome(errors: (3, 4)), .refused, "an error, the same document")
    XCTAssertEqual(outcome(errors: (3, 4), snapshots: (7, 8)), .refused,
                   "an error with a snapshot (the current one re-sent before it) is still a refusal")
    XCTAssertEqual(outcome(snapshots: (7, 8), lineage: ("A", "B")), .otherDocument,
                   "a stale command answered with the loaded document's snapshot and a new token")
    XCTAssertEqual(outcome(errors: (3, 4), snapshots: (7, 8), lineage: ("A", "B")), .otherDocument)
    XCTAssertEqual(outcome(lineage: ("A", "B")), .otherDocument)
    XCTAssertEqual(outcome(), .noReply, "nothing arrived (no plug-in answering)")
  }

  func testOnlyAnAppliedGenerateReveals() {
    XCTAssertTrue(GenerateOutcome.applied.reveals)
    XCTAssertFalse(GenerateOutcome.refused.reveals)
    XCTAssertFalse(GenerateOutcome.otherDocument.reveals)
    XCTAssertFalse(GenerateOutcome.noReply.reveals)
  }

  func testTheTakeTheTrackKeepsAfterAGenerate() throws {
    let entries = try menu(try fixture(selecting([])), trackId: 2, take: 5).groups[0].entries
    let plain = entries[0], next = entries[1], heldTone = entries[2]
    let bass = try menu(try fixture(selecting([])), trackId: 1).groups[0].entries[0]
    // "Motif, next take": the take it carried, applied or refused (a refused take must not trap the track on it:
    // Motif's note count depends on the take, so take 6 can hit a limit while take 7 fits).
    XCTAssertEqual(GenerateOutcome.applied.takeToStore(for: next), 6)
    XCTAssertEqual(GenerateOutcome.refused.takeToStore(for: next), 6, "refused: the next click offers take 7")
    XCTAssertNil(GenerateOutcome.otherDocument.takeToStore(for: next), "another document's answer: the take stays")
    XCTAssertNil(GenerateOutcome.noReply.takeToStore(for: next), "no answer: the take stays")
    for entry in [plain, heldTone, bass] {
      for result in [GenerateOutcome.applied, .refused, .otherDocument, .noReply] {
        XCTAssertNil(result.takeToStore(for: entry), "\(entry.title), \(result): the take stays")
      }
    }
  }

  func testALoadedDocumentStartsEveryTrackAtTake1() {
    let timing = RollTiming(ticksPerBeat: 960, barTicks: 3840, endTick: 15360, minNoteTicks: 30)
    var states = RollViewStates()
    states.edit(trackId: 1, timing: timing) { state, _, _ in state.motifTake = 9; state.grid = .sixteenthTriplet }
    states.edit(trackId: 2, timing: timing) { state, _, _ in state.motifTake = 63 }
    states.resetMotifTakes()
    XCTAssertEqual(states[1].motifTake, 0)
    XCTAssertEqual(states[2].motifTake, 0)
    XCTAssertEqual(states[1].grid, .sixteenthTriplet, "only the takes are reset")
    XCTAssertEqual(states.trackIds, [1, 2])
    var untouched = RollViewStates()
    untouched.resetMotifTakes()
    XCTAssertEqual(untouched, RollViewStates(), "no entry is made")
  }

  // MARK: - Which chords' notes the roll reveals afterwards

  func testAScopesChordIdsAreItsChordsOrNilForEveryChord() {
    XCTAssertNil(GenerateScope.everyChord.chordIds)
    XCTAssertEqual(GenerateScope.chords([3, 1]).chordIds, [1, 3])
    XCTAssertEqual(GenerateEntry(pattern: .bass(.roots), scope: .chords([2]), trackId: 1).scope.chordIds, [2])
  }
}
