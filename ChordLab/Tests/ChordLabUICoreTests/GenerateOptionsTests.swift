import XCTest
@testable import ChordLabUICore

/// The roll toolbar's Generate menu (UI/Core/GenerateOptions.swift, tracks design D14, D16, plan Task 23): the six bass
/// patterns, the scope from the chord selection, the `generateBass` command, availability, the Walking note about Loop,
/// and the menu model the view renders.
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

  private func assertCommand(_ command: [String: Any], _ expected: [String: Any],
                             file: StaticString = #filePath, line: UInt = #line) {
    XCTAssertTrue(NSDictionary(dictionary: command).isEqual(to: expected), "\(command) != \(expected)",
                  file: file, line: line)
  }

  // MARK: - Patterns

  func testTheSixPatternsInMenuOrderWithTheirWireNamesAndTitles() {
    XCTAssertEqual(GenerateOptions.patterns.map(\.rawValue),
                   ["roots", "rootPulse", "rootFifth", "octaves", "followFeel", "walking"],
                   "the engine's bassPatternName strings (json_api.h)")
    XCTAssertEqual(GenerateOptions.patterns.map(\.title),
                   ["Roots", "Root pulse", "Root–fifth", "Octaves", "Follow feel", "Walking"])
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
    let menu = GenerateOptions.menu(all)
    XCTAssertEqual(menu.groups.map(\.title), ["Replace notes in all 4 chords"])
    XCTAssertNil(menu.groups[0].entries[0].command(trackId: 1)["scope"])
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

  // MARK: - Command

  func testTheCommandForEveryChordHasNoScope() {
    let command = GenerateOptions.command(trackId: 1, pattern: .walking, scope: .everyChord)
    assertCommand(command, ["cmd": "generateBass", "trackId": 1, "pattern": "walking"])
    XCTAssertNil(command["scope"], "absent, never an empty array")
    XCTAssertNil(command["baseRevision"], "generateBass takes no baseRevision")
  }

  func testTheCommandForASelectionCarriesItsIds() throws {
    assertCommand(GenerateOptions.command(trackId: 1, pattern: .walking, scope: .chords([3, 5])),
                  ["cmd": "generateBass", "trackId": 1, "pattern": "walking", "scope": [3, 5]])
    assertCommand(GenerateOptions.command(trackId: 2, pattern: .rootFifth, scope: .chords([4])),
                  ["cmd": "generateBass", "trackId": 2, "pattern": "rootFifth", "scope": [4]])
  }

  func testTheCommandIsExactlyTheDocumentedJSON() throws {
    let command = GenerateOptions.command(trackId: 1, pattern: .walking, scope: .chords([3, 5]))
    let data = try JSONSerialization.data(withJSONObject: command, options: [.sortedKeys])
    XCTAssertEqual(String(decoding: data, as: UTF8.self),
                   #"{"cmd":"generateBass","pattern":"walking","scope":[3,5],"trackId":1}"#)
  }

  // MARK: - Availability

  func testAnEmptyProgressionCanGenerateNothing() throws {
    let empty = try fixture { object in
      object["events"] = [Any]()
      object["selectedEventIds"] = [Int]()
      object["selectedEventId"] = NSNull()
    }
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
    XCTAssertEqual(GenerateOptions.menu(try fixture()).walkingNote, GenerateOptions.walkingNote(loop: true))
    XCTAssertEqual(GenerateOptions.menu(try fixture { $0["progressionLoop"] = false }).walkingNote,
                   GenerateOptions.walkingNote(loop: false))
  }

  // MARK: - Menu

  func testWithoutASelectionTheMenuIsOneGroupForEveryChord() throws {
    let menu = GenerateOptions.menu(try fixture(selecting([])))
    XCTAssertTrue(menu.isEnabled)
    XCTAssertEqual(menu.groups.map(\.title), ["Replace notes in all 4 chords"])
    XCTAssertEqual(menu.groups[0].entries.map(\.title),
                   ["Roots", "Root pulse", "Root–fifth", "Octaves", "Follow feel", "Walking"])
    XCTAssertEqual(menu.groups[0].entries.map(\.scope), Array(repeating: .everyChord, count: 6))
    XCTAssertEqual(menu.groups[0].entries.map(\.pattern), GenerateOptions.patterns)
    XCTAssertEqual(menu.value, "Replace notes in all 4 chords")
  }

  func testWithASelectionTheMenuAddsTheWholeProgression() throws {
    let menu = GenerateOptions.menu(try fixture())
    XCTAssertTrue(menu.isEnabled)
    XCTAssertEqual(menu.groups.map(\.title), ["Replace notes in the 2 selected chords", "Replace notes in all 4 chords"],
                   "the same words for every chord, whether or not chords are selected")
    XCTAssertEqual(menu.groups[0].entries.map(\.scope), Array(repeating: .chords([2, 3]), count: 6))
    XCTAssertEqual(menu.groups[1].entries.map(\.scope), Array(repeating: .everyChord, count: 6))
    for group in menu.groups { XCTAssertEqual(group.entries.map(\.pattern), GenerateOptions.patterns) }
    XCTAssertEqual(menu.value, "Replace notes in the 2 selected chords")

    let one = GenerateOptions.menu(try fixture(selecting([4])))
    XCTAssertEqual(one.groups.map(\.title), ["Replace notes in the selected chord", "Replace notes in all 4 chords"])
    XCTAssertEqual(one.groups[0].entries.map(\.scope), Array(repeating: .chords([4]), count: 6))

    let dropped = GenerateOptions.menu(try fixture(selecting([3, 42])))
    XCTAssertEqual(dropped.groups[0].title, "Replace notes in the selected chord", "counted after the unknown id is dropped")
  }

  func testEntryIdsAreUniqueAcrossGroups() throws {
    let ids = GenerateOptions.menu(try fixture()).groups.flatMap { $0.entries.map(\.id) }
    XCTAssertEqual(ids.count, 12)
    XCTAssertEqual(Set(ids).count, 12)
  }

  func testAnEntrysCommand() throws {
    let menu = GenerateOptions.menu(try fixture())
    assertCommand(menu.groups[0].entries[5].command(trackId: 1),
                  ["cmd": "generateBass", "trackId": 1, "pattern": "walking", "scope": [2, 3]])
    assertCommand(menu.groups[1].entries[0].command(trackId: 1),
                  ["cmd": "generateBass", "trackId": 1, "pattern": "roots"])
  }

  func testAnEmptyProgressionDisablesTheMenuWithTheReason() throws {
    let menu = GenerateOptions.menu(try fixture { object in
      object["events"] = [Any]()
      object["selectedEventIds"] = [Int]()
      object["selectedEventId"] = NSNull()
    })
    XCTAssertFalse(menu.isEnabled)
    XCTAssertEqual(menu.groups, [])
    XCTAssertNil(menu.walkingNote)
    XCTAssertEqual(menu.value, "Add chords first")
    XCTAssertEqual(menu.help, "Add chords first")

    let stillSelected = try fixture { object in
      object["events"] = [Any]()
      object["selectedEventIds"] = [2]
      object["selectedEventId"] = 2
    }
    XCTAssertEqual(GenerateOptions.menu(stillSelected), menu, "a selection naming no chord changes nothing")
    XCTAssertEqual(GenerateOptions.scope(stillSelected), .everyChord)
  }

  // MARK: - Choosing an entry

  func testAnEntryTheCurrentMenuOffersGivesItsCommand() throws {
    let snapshot = try fixture()
    let menu = GenerateOptions.menu(snapshot)
    assertCommand(try XCTUnwrap(GenerateOptions.command(for: menu.groups[0].entries[5], trackId: 2, in: snapshot, lineage: "")),
                  ["cmd": "generateBass", "trackId": 2, "pattern": "walking", "scope": [2, 3]])
    assertCommand(try XCTUnwrap(GenerateOptions.command(for: menu.groups[1].entries[0], trackId: 2, in: snapshot, lineage: "")),
                  ["cmd": "generateBass", "trackId": 2, "pattern": "roots"])
  }

  func testAnEntryFromAnotherDocumentSendsNothing() throws {
    // Document A's menu is open with nothing selected; the host loads document B, which reuses the track and chord ids.
    // Every progression offers "every chord", so only the document lineage (the state's clipboard token, renewed by a
    // load) tells the two apart.
    let snapshot = try fixture(selecting([]))
    let offeredForA = GenerateOptions.menu(snapshot, lineage: "A").groups[0].entries[0]
    XCTAssertEqual(offeredForA.scope, .everyChord)
    XCTAssertEqual(offeredForA.lineage, "A")
    XCTAssertNotNil(GenerateOptions.command(for: offeredForA, trackId: 1, in: snapshot, lineage: "A"))
    XCTAssertNil(GenerateOptions.command(for: offeredForA, trackId: 1, in: snapshot, lineage: "B"),
                 "the same ids in another document: nothing is replaced there")
    let selected = try fixture()
    let walking = GenerateOptions.menu(selected, lineage: "A").groups[0].entries[5]
    XCTAssertEqual(walking.scope, .chords([2, 3]))
    XCTAssertNil(GenerateOptions.command(for: walking, trackId: 1, in: selected, lineage: "B"))
    XCTAssertNotEqual(GenerateOptions.menu(snapshot, lineage: "A"), GenerateOptions.menu(snapshot, lineage: "B"),
                      "a loaded document redraws the toolbar's menu")
    assertCommand(try XCTUnwrap(GenerateOptions.command(for: offeredForA, trackId: 1, in: snapshot, lineage: "A")),
                  ["cmd": "generateBass", "trackId": 1, "pattern": "roots"])
  }

  func testAnEntryFromAnOlderMenuSendsNothing() throws {
    // The menu was drawn for chords [2, 3]; a host state load then replaced the document while it was open. The
    // entry's ids may name other chords now, so it is dropped: the reopened menu shows the new scope.
    let stale = GenerateEntry(pattern: .walking, scope: .chords([2, 3]))
    XCTAssertNotNil(GenerateOptions.command(for: stale, trackId: 1, in: try fixture(), lineage: ""))
    XCTAssertNil(GenerateOptions.command(for: stale, trackId: 1, in: try fixture(selecting([])), lineage: ""),
                 "nothing selected any more")
    XCTAssertNil(GenerateOptions.command(for: stale, trackId: 1, in: try fixture(selecting([2])), lineage: ""),
                 "another selection")
    XCTAssertNil(GenerateOptions.command(for: stale, trackId: 1, in: try fixture { object in
      object["events"] = [Any]()
    }, lineage: ""), "no chords left")
    let everyChord = GenerateEntry(pattern: .roots, scope: .everyChord)
    XCTAssertNotNil(GenerateOptions.command(for: everyChord, trackId: 1, in: try fixture(selecting([])), lineage: ""))
    XCTAssertNotNil(GenerateOptions.command(for: everyChord, trackId: 1, in: try fixture(selecting([1])), lineage: ""),
                    "every chord is offered under any selection")
    XCTAssertNil(GenerateOptions.command(for: everyChord, trackId: 1, in: try fixture { object in
      object["events"] = [Any]()
      object["selectedEventIds"] = [Int]()
      object["selectedEventId"] = NSNull()
    }, lineage: ""), "an empty progression offers nothing")
  }

  func testTheHelpSaysWhatGeneratingDoes() throws {
    XCTAssertEqual(GenerateOptions.menu(try fixture()).help, GenerateOptions.help)
    XCTAssertTrue(GenerateOptions.help.contains("replac"), GenerateOptions.help)
  }

  func testTheMenuChangesWithTheSelectionAndLoopOnly() throws {
    let base = GenerateOptions.menu(try fixture())
    XCTAssertEqual(GenerateOptions.menu(try fixture { $0["revision"] = 99 }), base,
                   "a content revision alone changes nothing the menu shows")
    let edited = try fixture { object in
      var events = object["events"] as! [[String: Any]]
      events[0]["symbol"] = "Dm7"
      events[0]["root"] = 2
      events[0]["velocity"] = 64
      events[0]["lengthBeats"] = 2.0
      object["events"] = events
    }
    XCTAssertEqual(GenerateOptions.menu(edited), base, "a chord edit that keeps the ids changes nothing either")
    XCTAssertNotEqual(GenerateOptions.menu(try fixture(selecting([1]))), base)
    XCTAssertNotEqual(GenerateOptions.menu(try fixture { $0["progressionLoop"] = false }), base)
  }

  // MARK: - Which chords' notes the roll reveals afterwards

  func testAScopesChordIdsAreItsChordsOrNilForEveryChord() {
    XCTAssertNil(GenerateScope.everyChord.chordIds)
    XCTAssertEqual(GenerateScope.chords([3, 1]).chordIds, [1, 3])
    XCTAssertEqual(GenerateEntry(pattern: .roots, scope: .chords([2])).scope.chordIds, [2])
  }
}
