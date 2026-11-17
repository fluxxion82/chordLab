import Foundation
import XCTest
@testable import ChordLabUICore

/// What may leave the editor while following (UI/Core/LinkGate.swift).
final class LinkGateTests: XCTestCase {
  private let editorCommands: [[String: Any]] = [
    ["cmd": "appendChord", "root": 0, "type": "maj"], ["cmd": "removeChords", "ids": [1]], ["cmd": "undo"],
    ["cmd": "redo"], ["cmd": "patchTrack", "id": 1, "name": "X"], ["cmd": "removeTrack", "id": 1],
    ["cmd": "selectEvent", "id": 1], ["cmd": "updateNotes", "notes": []], ["cmd": "setTrackMute", "id": 1, "on": true],
    ["cmd": "setFollowHost", "on": true], ["cmd": "setKeyHintSeen", "on": true], ["cmd": "generateBass"],
  ]

  func testEveryCommandPassesWhileNotFollowing() {
    for command in editorCommands + [["cmd": "setMidiThru", "on": true]] {
      XCTAssertTrue(LinkGate.allowsCommand(command, following: false), "\(command)")
    }
    XCTAssertTrue(LinkGate.allowsCommand([:], following: false))
  }

  func testOnlyThePanelsThruPassesWhileFollowing() {
    XCTAssertEqual(LinkGate.commandsWhileFollowing, ["setMidiThru"])
    XCTAssertTrue(LinkGate.allowsCommand(["cmd": "setMidiThru", "on": false, "lineage": "t"], following: true))
    for command in editorCommands {
      XCTAssertFalse(LinkGate.allowsCommand(command, following: true), "\(command)")
    }
    XCTAssertFalse(LinkGate.allowsCommand([:], following: true))                 // no cmd: nothing to let through
    XCTAssertFalse(LinkGate.allowsCommand(["cmd": 7], following: true))
  }

  func testNoPreviewStartsWhileFollowingButStopsAndExtendsPass() {
    let start = PreviewMessage.start(id: 1, pitches: [60], velocity: 100, lane: .chords).json
    let trackStart = PreviewMessage.start(id: 2, pitches: [40], velocity: 100, lane: .track(2)).json
    let stop = PreviewMessage.stop(id: 1).json
    let extend = PreviewMessage.extend(id: 1).json
    XCTAssertTrue(LinkGate.allowsPreview(start, following: false))
    XCTAssertFalse(LinkGate.allowsPreview(start, following: true))
    XCTAssertTrue(LinkGate.allowsPreview(trackStart, following: false))
    XCTAssertFalse(LinkGate.allowsPreview(trackStart, following: true), "a track's start is a start too")
    XCTAssertTrue(LinkGate.allowsPreview(stop, following: true))
    XCTAssertTrue(LinkGate.allowsPreview(extend, following: true))
  }

  /// A new hold (a palette cell, a key of the roll's keyboard, a roll note, a one-shot) starts only while not
  /// following: the state asks before any preview bookkeeping changes, so a refused press leaves nothing for its
  /// release (no held cell, no keep-alive, no Record insert).
  func testNoNewHoldWhileFollowing() {
    XCTAssertTrue(LinkGate.allowsHold(following: false))
    XCTAssertFalse(LinkGate.allowsHold(following: true))
  }
}
