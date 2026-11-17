import Foundation
import XCTest
@testable import ChordLabUICore

/// Which key commands stand down while the instance follows (UI/Core/LinkKeys.swift).
final class LinkKeysTests: XCTestCase {
  private let every: [KeyCommand] = [
    .appendSuggestion(1), .appendSuggestion(8), .deleteSelection, .undo, .redo, .copy, .cut, .paste, .duplicate,
    .selectAll, .nudge(.up, octave: false), .nudge(.left, octave: true),
  ]

  func testEveryCommandIsDeliveredWhileNotFollowing() {
    for command in every { XCTAssertTrue(LinkKeys.delivers(command, following: false), "\(command)") }
  }

  func testNoCommandIsDeliveredWhileFollowing() {
    for command in every { XCTAssertFalse(LinkKeys.delivers(command, following: true), "\(command)") }
  }

  private func key(_ k: KeyInput.Key, command: Bool = false, shift: Bool = false) -> KeyInput {
    KeyInput(key: k, command: command, shift: shift)
  }

  /// Delete, arrows, undo, copy and paste, digits: mapped while not following, the host's while following.
  func testASampleOfKeysWithBothTargets() {
    let samples: [(KeyInput, KeyTarget, KeyCommand)] = [
      (key(.delete), .chords, .deleteSelection),
      (key(.forwardDelete), .notes(trackId: 1), .deleteSelection),
      (key(.arrow(.up)), .notes(trackId: 1), .nudge(.up, octave: false)),
      (key(.arrow(.right), shift: true), .notes(trackId: 1), .nudge(.right, octave: true)),
      (key(.character("z"), command: true), .chords, .undo),
      (key(.character("z"), command: true, shift: true), .notes(trackId: 1), .redo),
      (key(.character("c"), command: true), .chords, .copy),
      (key(.character("v"), command: true), .notes(trackId: 1), .paste),
      (key(.character("x"), command: true), .chords, .cut),
      (key(.character("a"), command: true), .chords, .selectAll),
      (key(.character("3")), .chords, .appendSuggestion(3)),
    ]
    for (input, target, command) in samples {
      XCTAssertEqual(LinkKeys.command(for: input, target: target, following: false), command, "\(input)")
      XCTAssertNil(LinkKeys.command(for: input, target: target, following: true), "\(input)")
    }
  }

  /// Space is not the editor's key at all (it neither previews nor plays here): the host keeps it either way.
  func testSpaceIsTheHostsEitherWay() {
    XCTAssertNil(LinkKeys.command(for: key(.character(" ")), target: .chords, following: false))
    XCTAssertNil(LinkKeys.command(for: key(.character(" ")), target: .chords, following: true))
  }

  func testFollowingIsReadFromTheLinkState() throws {
    XCTAssertFalse(LinkKeys.isFollowing(nil))
    XCTAssertTrue(LinkKeys.isFollowing(try LinkStateTests.fixture("link-state-follower.json")))
    XCTAssertTrue(LinkKeys.isFollowing(try LinkStateTests.fixture("link-state-waiting.json")))
    XCTAssertFalse(LinkKeys.isFollowing(try LinkStateTests.fixture("link-state-leader.json")))
  }
}
