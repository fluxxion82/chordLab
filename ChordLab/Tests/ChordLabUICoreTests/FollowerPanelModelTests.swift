import Foundation
import SwiftUI
import XCTest
@testable import ChordLabUICore

/// The follower's panel (UI/Core/FollowerPanelModel.swift): every text and every button, by status.
final class FollowerPanelModelTests: XCTestCase {
  private func lane(_ key: String, _ name: String, _ role: String) -> LinkLaneJSON {
    LinkLaneJSON(lane: key, name: name, role: role, chords: key == "chords", audible: true, followers: 0)
  }

  private let leaderA = LinkLeaderJSON(id: "a", label: "ChordLab A", lineage: "t-a",
                                       lanes: [LinkLaneJSON(lane: "chords", name: "Chords", role: "", chords: true,
                                                            audible: true, followers: 0),
                                               LinkLaneJSON(lane: "u-bass", name: "Bass", role: "bass", chords: false,
                                                            audible: true, followers: 1)])

  private func state(status: LinkStatus = .playing, lane: String = "u-bass", laneName: String = "Bass",
                     role: String = "bass", leaderLabel: String = "ChordLab A", leaders: [LinkLeaderJSON]? = nil,
                     switchTo: LinkInstanceJSON? = nil) -> LinkStateJSON {
    LinkStateJSON(
      schema: 1, instance: LinkSelfJSON(id: "me", label: "ChordLab B", copyOf: nil),
      following: LinkFollowingJSON(leader: "a", leaderLabel: leaderLabel, lane: lane, laneName: laneName, role: role,
                                   status: status, switchTo: switchTo),
      leaders: leaders ?? [leaderA], routed: [])
  }

  private func model(_ state: LinkStateJSON) throws -> FollowerPanelModel {
    try XCTUnwrap(FollowerPanelModel(state))
  }

  // MARK: - Not following

  func testNoModelWhenNotFollowing() throws {
    XCTAssertNil(FollowerPanelModel(nil))
    XCTAssertNil(FollowerPanelModel(try LinkStateTests.fixture("link-state-leader.json")))
  }

  // MARK: - Headline

  func testHeadlineForATrack() throws {
    let m = try model(state())
    XCTAssertEqual(m.headline, [TextRun("Plays "), TextRun("Bass", bold: true), TextRun(" of "),
                                TextRun("ChordLab A", bold: true)])
    XCTAssertEqual(m.headlineText, "Plays Bass of ChordLab A")
    XCTAssertEqual(m.role, .bass)
    XCTAssertEqual(m.roleColor, TrackStyle.bass)
  }

  func testHeadlineForTheChordsLane() throws {
    let m = try model(state(lane: "chords", laneName: "Chords", role: ""))
    XCTAssertEqual(m.headline, [TextRun("Plays the "), TextRun("chords", bold: true), TextRun(" of "),
                                TextRun("ChordLab A", bold: true)])
    XCTAssertNil(m.role)
    XCTAssertNil(m.roleColor)
  }

  func testNamesComeFromTheLeadersCurrentLanesBeforeTheHints() throws {
    let renamed = LinkLeaderJSON(id: "a", label: "ChordLab A", lineage: "t", lanes: [lane("u-bass", "Low end", "lead")])
    let m = try model(state(leaderLabel: "", leaders: [renamed]))
    XCTAssertEqual(m.headlineText, "Plays Low end of ChordLab A")
    XCTAssertEqual(m.role, .lead)
  }

  func testAnUnnamedTrackIsNamedByItsRole() throws {
    XCTAssertEqual(try model(state(laneName: "", leaders: [])).headlineText, "Plays Bass of ChordLab A")
    let unnamed = LinkLeaderJSON(id: "a", label: "ChordLab A", lineage: "t", lanes: [lane("u-bass", "", "other")])
    XCTAssertEqual(try model(state(leaders: [unnamed])).headlineText, "Plays Other of ChordLab A")
  }

  func testEmptyHintsFallBack() throws {
    let m = try model(state(status: .muted, laneName: "", role: "", leaderLabel: "", leaders: []))
    XCTAssertEqual(m.headline, [TextRun("Plays a track of another ChordLab")])
    XCTAssertEqual(m.statusLine, "The track is muted in another ChordLab (or another track is soloed).")
    XCTAssertEqual(m.editNote, "Edit in the other ChordLab's window.")
    XCTAssertNil(m.role)
  }

  func testEmptyLabelHintTakesTheListedLeadersLabel() throws {
    XCTAssertEqual(try model(state(leaderLabel: "")).headlineText, "Plays Bass of ChordLab A")
  }

  // MARK: - Status lines and repairs

  func testPlaying() throws {
    let m = try model(state(status: .playing))
    XCTAssertEqual(m.status, .playing)
    XCTAssertEqual(m.statusLine, "Playing.")
    XCTAssertEqual(m.repairs, [])
  }

  func testMuted() throws {
    let m = try model(state(status: .muted))
    XCTAssertEqual(m.statusLine, "Bass is muted in ChordLab A (or another track is soloed).")
    XCTAssertEqual(m.repairs, [])
    XCTAssertEqual(try model(state(status: .muted, lane: "chords", laneName: "", role: "")).statusLine,
                   "The chords are muted in ChordLab A (or a track is soloed).")
  }

  func testWaiting() throws {
    let m = try model(state(status: .waiting, leaders: []))
    XCTAssertEqual(m.statusLine, "Waiting for ChordLab A. It is not in this set right now.")
    XCTAssertEqual(m.repairs, [FollowerRepairButton(repair: .change, title: "Change…"),
                               FollowerRepairButton(repair: .stopFollowing, title: "Stop following")])
    XCTAssertEqual(try model(state(status: .waiting, leaderLabel: "", leaders: [])).statusLine,
                   "Waiting for another ChordLab. It is not in this set right now.")
  }

  func testLaneMissing() throws {
    let m = try model(state(status: .laneMissing))
    XCTAssertEqual(m.statusLine, "Bass was deleted in ChordLab A. Undo there brings it back.")
    XCTAssertEqual(m.repairs, [FollowerRepairButton(repair: .change, title: "Change…")])
    XCTAssertEqual(try model(state(status: .laneMissing, laneName: "", role: "", leaderLabel: "", leaders: [])).statusLine,
                   "The track was deleted in another ChordLab. Undo there brings it back.")
  }

  func testProjectChanged() throws {
    let m = try model(state(status: .projectChanged))
    XCTAssertEqual(m.statusLine, "ChordLab A holds a different project now.")
    XCTAssertEqual(m.repairs, [FollowerRepairButton(repair: .change, title: "Pick a track…")])
    XCTAssertEqual(try model(state(status: .projectChanged, leaderLabel: "", leaders: [])).statusLine,
                   "Another ChordLab holds a different project now.")
  }

  func testUnknown() throws {
    let m = try model(state(status: .unknown))
    XCTAssertEqual(m.statusLine, "Not playing.")
    XCTAssertEqual(m.repairs, [FollowerRepairButton(repair: .change, title: "Change…")])
  }

  func testTheWaitingFixtureUsesTheStoredHints() throws {
    let m = try model(try LinkStateTests.fixture("link-state-waiting.json"))
    XCTAssertEqual(m.headlineText, "Plays Bass of ChordLab A")
    XCTAssertEqual(m.statusLine, "Waiting for ChordLab A. It is not in this set right now.")
    XCTAssertEqual(m.role, .bass)
    XCTAssertNil(m.switchOffer)
  }

  // MARK: - Fixed texts

  func testFixedTexts() throws {
    let m = try model(state())
    XCTAssertEqual(m.editNote, "Edit in ChordLab A's window.")
    XCTAssertEqual(FollowerPanelModel.routingReminder,
                   "On the instrument's track choose MIDI From: this track, then this ChordLab, and set Monitor to In.")
    XCTAssertEqual(FollowerPanelModel.changeTitle, "Change…")
    XCTAssertEqual(FollowerPanelModel.stopTitle, "Stop following")
    XCTAssertEqual(FollowerPanelModel.keptNote,
                   "This ChordLab's own chords and tracks are kept. They come back when you stop following.")
  }

  // MARK: - switchTo

  private let copyRef = LinkInstanceJSON(leader: "c", label: "ChordLab C")

  private func copy(_ lanes: [LinkLaneJSON], lineage: String = "t-c") -> LinkLeaderJSON {
    LinkLeaderJSON(id: "c", label: "ChordLab C", lineage: lineage, lanes: lanes)
  }

  private func switchAction(lane: String = "u-bass", copyLanes: [LinkLaneJSON], lineage: String = "t-c",
                            laneName: String = "Bass", role: String = "bass", leaders: [LinkLeaderJSON]? = nil)
    throws -> FollowerSwitchAction {
    let s = state(lane: lane, laneName: laneName, role: role, leaders: leaders ?? [leaderA, copy(copyLanes, lineage: lineage)],
                  switchTo: copyRef)
    return try XCTUnwrap(try model(s).switchOffer).action
  }

  func testTheFollowerFixtureOffersTheCopysBass() throws {
    let m = try model(try LinkStateTests.fixture("link-state-follower.json"))
    let offer = try XCTUnwrap(m.switchOffer)
    XCTAssertEqual(offer.line, "ChordLab C is a copy of ChordLab A.")
    XCTAssertEqual(offer.button, "Play Bass of ChordLab C instead")
    guard case .follow(let option) = offer.action else { return XCTFail("expected a follow, got \(offer.action)") }
    XCTAssertEqual(option.id, FollowTarget(leader: "uid-6", lane: "uid-2"))
    XCTAssertEqual(option.leaderLineage, "lineage-leader-copy-1")
    XCTAssertEqual(option.command as NSDictionary, ["op": "follow", "leader": "uid-6", "lane": "uid-2",
                                                    "leaderLineage": "lineage-leader-copy-1"] as NSDictionary)
  }

  func testNoSwitchOfferWithoutSwitchTo() throws {
    XCTAssertNil(try model(state()).switchOffer)
  }

  func testWithoutTheSameUidTheOneLaneWithTheSameNameAndRoleIsFollowed() throws {
    let action = try switchAction(copyLanes: [lane("chords", "Chords", ""), lane("x-1", "Bass", "bass"),
                                              lane("x-2", "Lead", "lead")])
    XCTAssertEqual(action.target, FollowTarget(leader: "c", lane: "x-1"))
  }

  func testTheChordsLaneSwitchesToTheCopysChords() throws {
    let s = state(lane: "chords", laneName: "", role: "",
                  leaders: [leaderA, copy([lane("chords", "Chords", ""), lane("x", "Bass", "bass")])], switchTo: copyRef)
    let offer = try XCTUnwrap(try model(s).switchOffer)
    XCTAssertEqual(offer.button, "Play the chords of ChordLab C instead")
    XCTAssertEqual(offer.action.target, FollowTarget(leader: "c", lane: "chords"))
  }

  /// A copy loads its original's lane table, so it lists the same uids: the same uid is the first rule, whatever the
  /// lane is called in the copy now.
  func testTheSameUidInTheCopyIsFollowedFirst() throws {
    let action = try switchAction(copyLanes: [lane("x-1", "Bass", "bass"), lane("u-bass", "Low end", "lead")])
    XCTAssertEqual(action.target, FollowTarget(leader: "c", lane: "u-bass"))
  }

  func testSeveralLanesWithTheSameNameAndRoleAndNoSameUidOpenThePicker() throws {
    XCTAssertEqual(try switchAction(copyLanes: [lane("x-1", "Bass", "bass"), lane("x-2", "Bass", "bass")]),
                   .choose(leader: "c"))
  }

  func testTheSameNameWithAnotherRoleIsNoMatch() throws {
    XCTAssertEqual(try switchAction(copyLanes: [lane("x", "Bass", "lead")]), .choose(leader: "c"))
  }

  func testNoMatchOpensThePickerOnTheCopy() throws {
    XCTAssertEqual(try switchAction(copyLanes: [lane("chords", "Chords", ""), lane("x", "Lead", "lead")]),
                   .choose(leader: "c"))
  }

  func testACopyThatIsNotOfferedOpensThePicker() throws {
    XCTAssertEqual(try switchAction(copyLanes: [lane("x", "Bass", "bass")], lineage: ""), .choose(leader: "c"))
    XCTAssertEqual(try switchAction(copyLanes: [], leaders: [leaderA]), .choose(leader: "c"))
  }

  func testAnUnknownNameOpensThePicker() throws {
    let action = try switchAction(copyLanes: [lane("x", "Bass", "bass")], laneName: "", role: "",
                                  leaders: [copy([lane("x", "Bass", "bass")])])
    XCTAssertEqual(action, .choose(leader: "c"))
  }

  func testAnUnknownRoleMatchesByNameAlone() throws {
    let action = try switchAction(copyLanes: [], laneName: "Bass", role: "",
                                  leaders: [copy([lane("x", "Bass", "lead")])])
    XCTAssertEqual(action.target, FollowTarget(leader: "c", lane: "x"))
  }

  func testAnUnnamedTrackMatchesAnUnnamedTrackOfTheSameRole() throws {
    let a = LinkLeaderJSON(id: "a", label: "ChordLab A", lineage: "t", lanes: [lane("u-bass", "", "bass")])
    let action = try switchAction(copyLanes: [], laneName: "", leaders: [a, copy([lane("y", "", "bass")])])
    XCTAssertEqual(action.target, FollowTarget(leader: "c", lane: "y"))
  }

  func testACopyWithoutALabelIsTheCopy() throws {
    let s = state(leaders: [leaderA, copy([lane("x", "Bass", "bass")])], switchTo: LinkInstanceJSON(leader: "c", label: ""))
    let offer = try XCTUnwrap(try model(s).switchOffer)
    XCTAssertEqual(offer.line, "There is a copy of ChordLab A in this set.")
    XCTAssertEqual(offer.button, "Play Bass of the copy instead")
  }

  func testAnUnknownTrackSwitchButtonSaysTheSameTrack() throws {
    let s = state(laneName: "", role: "", leaders: [copy([lane("x", "Bass", "bass")])], switchTo: copyRef)
    XCTAssertEqual(try XCTUnwrap(try model(s).switchOffer).button, "Play the same track of ChordLab C instead")
  }
}
