import XCTest
@testable import ChordLabUICore

/// The document lineage every editor command carries (UI/Core/CommandLineage.swift, tracks plan Task 24a, design D21):
/// `ChordLabState.send(command:)` stamps the state's clipboard token under `lineage`, and the shell applies a command
/// only when that is its current document's token.
final class CommandLineageTests: XCTestCase {
  func testTheKeyIsLineage() {
    XCTAssertEqual(CommandLineage.key, "lineage", "the member the shell's bridge (command_lineage.h) reads")
  }

  func testStampAddsTheTokenAndKeepsEveryOtherField() {
    let command: [String: Any] = ["cmd": "removeChords", "ids": [3, 1], "baseRevision": 7]
    let stamped = CommandLineage.stamp(command, token: "6F1C-A")
    XCTAssertEqual(stamped["lineage"] as? String, "6F1C-A")
    XCTAssertEqual(stamped["cmd"] as? String, "removeChords")
    XCTAssertEqual(stamped["ids"] as? [Int], [3, 1])
    XCTAssertEqual(stamped["baseRevision"] as? Int, 7)
    XCTAssertEqual(stamped.count, 4, "only lineage is added")
  }

  func testAnExistingLineageIsOverwritten() {
    let stamped = CommandLineage.stamp(["cmd": "undo", "lineage": "old-document"], token: "current")
    XCTAssertEqual(stamped["lineage"] as? String, "current", "the state's token wins over whatever the command held")
    XCTAssertEqual(stamped.count, 2)
    let notAString = CommandLineage.stamp(["cmd": "undo", "lineage": 5], token: "current")
    XCTAssertEqual(notAString["lineage"] as? String, "current")
  }

  func testAnEmptyTokenIsStampedAsIsNeverDropped() {
    // ClipboardToken.resolve never yields an empty token; were one to reach the stamp anyway, the command still carries
    // the key, so the shell sees a lineage that differs from its own (never empty) and applies nothing, rather than
    // a command without lineage, which it would apply unchecked.
    let stamped = CommandLineage.stamp(["cmd": "undo"], token: "")
    XCTAssertEqual(stamped["lineage"] as? String, "")
  }

  func testTheStampedCommandIsValidJSON() throws {
    let stamped = CommandLineage.stamp(["cmd": "setChordsMute", "on": true, "schema": 1], token: "A-1")
    XCTAssertTrue(JSONSerialization.isValidJSONObject(stamped))
    let data = try JSONSerialization.data(withJSONObject: stamped, options: [.sortedKeys])
    XCTAssertEqual(String(decoding: data, as: UTF8.self), #"{"cmd":"setChordsMute","lineage":"A-1","on":true,"schema":1}"#)
  }
}
