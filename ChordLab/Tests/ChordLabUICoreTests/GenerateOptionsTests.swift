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
  }

  func testTheScopeTitleSaysWhatWillBeReplaced() {
    XCTAssertEqual(GenerateOptions.scopeTitle(.everyChord), "Bass line for every chord")
    XCTAssertEqual(GenerateOptions.scopeTitle(.chords([3])), "Bass line for the selected chord")
    XCTAssertEqual(GenerateOptions.scopeTitle(.chords([1, 2, 4])), "Bass line for the 3 selected chords")
    XCTAssertEqual(GenerateOptions.scopeTitle(.chords([2, 3])), "Bass line for the 2 selected chords")
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
    XCTAssertEqual(menu.groups.map(\.title), ["Bass line for every chord"])
    XCTAssertEqual(menu.groups[0].entries.map(\.title),
                   ["Roots", "Root pulse", "Root–fifth", "Octaves", "Follow feel", "Walking"])
    XCTAssertEqual(menu.groups[0].entries.map(\.scope), Array(repeating: .everyChord, count: 6))
    XCTAssertEqual(menu.groups[0].entries.map(\.pattern), GenerateOptions.patterns)
    XCTAssertEqual(menu.value, "Bass line for every chord")
  }

  func testWithASelectionTheMenuAddsTheWholeProgression() throws {
    let menu = GenerateOptions.menu(try fixture())
    XCTAssertTrue(menu.isEnabled)
    XCTAssertEqual(menu.groups.map(\.title), ["Bass line for the 2 selected chords", "Whole progression"])
    XCTAssertEqual(menu.groups[0].entries.map(\.scope), Array(repeating: .chords([2, 3]), count: 6))
    XCTAssertEqual(menu.groups[1].entries.map(\.scope), Array(repeating: .everyChord, count: 6))
    for group in menu.groups { XCTAssertEqual(group.entries.map(\.pattern), GenerateOptions.patterns) }
    XCTAssertEqual(menu.value, "Bass line for the 2 selected chords")

    let one = GenerateOptions.menu(try fixture(selecting([4])))
    XCTAssertEqual(one.groups.map(\.title), ["Bass line for the selected chord", "Whole progression"])
    XCTAssertEqual(one.groups[0].entries.map(\.scope), Array(repeating: .chords([4]), count: 6))
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
  }

  func testTheHelpSaysWhatGeneratingDoes() throws {
    XCTAssertEqual(GenerateOptions.menu(try fixture()).help, GenerateOptions.help)
    XCTAssertTrue(GenerateOptions.help.contains("replac"), GenerateOptions.help)
  }

  func testTheMenuChangesWithTheSelectionAndLoopOnly() throws {
    let base = GenerateOptions.menu(try fixture())
    XCTAssertEqual(GenerateOptions.menu(try fixture { $0["revision"] = 99 }), base,
                   "a content revision alone changes nothing the menu shows")
    XCTAssertNotEqual(GenerateOptions.menu(try fixture(selecting([1]))), base)
    XCTAssertNotEqual(GenerateOptions.menu(try fixture { $0["progressionLoop"] = false }), base)
  }
}
