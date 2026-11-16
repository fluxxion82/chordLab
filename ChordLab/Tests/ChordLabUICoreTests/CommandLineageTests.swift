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

  // MARK: - One action, one lineage (Task 24a review)

  func testAnActionKeepsTheLineageItBeganWith() {
    var action = ActionLineage()
    XCTAssertEqual(action.lineage(current: "A"), "A", "outside an action a command carries the token current now")
    XCTAssertTrue(action.begin(token: "A"), "the outermost call opens the action")
    // The shell's stale reply renewed the state's token inline, inside the first send.
    XCTAssertEqual(action.lineage(current: "B"), "A", "the action's later commands keep the lineage it began with")
    action.end()
    XCTAssertEqual(action.lineage(current: "B"), "B", "the next action takes the new token")
  }

  func testANestedActionDoesNotReplaceTheOuterLineage() {
    var action = ActionLineage()
    XCTAssertTrue(action.begin(token: "A"))
    XCTAssertFalse(action.begin(token: "B"), "a helper's own action inside another opens nothing")
    XCTAssertEqual(action.lineage(current: "B"), "A")
    action.end()
    XCTAssertNil(action.captured)
  }

  // MARK: - A lineage change (Task 24a review: pending replies do not survive a load)

  func testATokenChangeIsReportedOnlyWhenTheResolvedValueDiffers() {
    XCTAssertEqual(ClipboardToken.change(from: "A", to: "A").token, "A")
    XCTAssertFalse(ClipboardToken.change(from: "A", to: "A").changed, "the shell re-setting the same token is no load")
    XCTAssertTrue(ClipboardToken.change(from: "A", to: "B").changed)
    XCTAssertEqual(ClipboardToken.change(from: "A", to: "B").token, "B")
    let blank = ClipboardToken.change(from: "A", to: "  ")
    XCTAssertTrue(blank.changed, "a blank token resolves to a fresh one: another lineage")
    XCTAssertNotEqual(blank.token, "A")
    XCTAssertFalse(blank.token.trimmingCharacters(in: .whitespaces).isEmpty)
    XCTAssertTrue(ClipboardToken.change(from: "A", to: nil).changed)
  }
}
