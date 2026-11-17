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

  private func model(_ state: LinkStateJSON, snapshot: Snapshot? = nil) throws -> FollowerPanelModel {
    try XCTUnwrap(FollowerPanelModel(state, snapshot: snapshot, ownLineage: "own"))
  }

  private func snapshot(_ name: String) throws -> Snapshot {
    let url = try SnapshotTests.fixturesDirectory().appendingPathComponent(name)
    return try JSONDecoder().decode(Snapshot.self, from: Data(contentsOf: url))
  }

  private let change = FollowerRepairButton(repair: .change, title: "Change…")
  private let stop = FollowerRepairButton(repair: .stopFollowing, title: "Stop following")

  // MARK: - Not following

  func testNoModelWhenNotFollowing() throws {
    XCTAssertNil(FollowerPanelModel(nil, snapshot: nil, ownLineage: "own"))
    XCTAssertNil(FollowerPanelModel(try LinkStateTests.fixture("link-state-leader.json"), snapshot: nil, ownLineage: "own"))
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

  // MARK: - Status lines and actions

  func testPlaying() throws {
    let m = try model(state(status: .playing))
    XCTAssertEqual(m.status, .playing)
    XCTAssertEqual(m.statusLine, "Playing.")
    XCTAssertEqual(m.actions, [change, stop])
    XCTAssertNil(m.emphasised)
  }

  func testMuted() throws {
    let m = try model(state(status: .muted))
    XCTAssertEqual(m.statusLine, "Bass is muted in ChordLab A (or another track is soloed).")
    XCTAssertEqual(m.actions, [change, stop])
    XCTAssertNil(m.emphasised)
    XCTAssertEqual(try model(state(status: .muted, lane: "chords", laneName: "", role: "")).statusLine,
                   "The chords are muted in ChordLab A (or a track is soloed).")
  }

  func testWaiting() throws {
    let m = try model(state(status: .waiting, leaders: []))
    XCTAssertEqual(m.statusLine, "Waiting for ChordLab A. It is not in this set, or it now plays from another ChordLab.")
    XCTAssertEqual(m.actions, [change, stop])
    XCTAssertEqual(m.emphasised, .change)
    XCTAssertNil(m.editNote)                                       // ChordLab A is not there to edit in
    XCTAssertEqual(try model(state(status: .waiting, leaderLabel: "", leaders: [])).statusLine,
                   "Waiting for another ChordLab. It is not in this set, or it now plays from another ChordLab.")
  }

  /// Labels are given by load order: while unbound, a stored "ChordLab A" may now name another instance. When a
  /// listed leader carries it, the sentences say "the other ChordLab" instead of the letter.
  func testAStaleStoredLabelIsNotUsedWhileWaiting() throws {
    let impostor = LinkLeaderJSON(id: "x", label: "ChordLab A", lineage: "t", lanes: [lane("chords", "Chords", "")])
    let m = try model(state(status: .waiting, leaders: [impostor]))
    XCTAssertEqual(m.headline, [TextRun("Set to play "), TextRun("Bass", bold: true), TextRun(" of the other ChordLab")])
    XCTAssertEqual(m.statusLine,
                   "Waiting for the other ChordLab. It is not in this set, or it now plays from another ChordLab.")
    let other = LinkLeaderJSON(id: "x", label: "ChordLab C", lineage: "t", lanes: [lane("chords", "Chords", "")])
    XCTAssertEqual(try model(state(status: .waiting, leaders: [other])).headlineText, "Set to play Bass of ChordLab A")
  }

  func testABoundLeadersLabelIsItsOwnWhateverElseIsListed() throws {
    XCTAssertEqual(try model(state(status: .muted)).headlineText, "Plays Bass of ChordLab A")
  }

  func testLaneMissing() throws {
    let m = try model(state(status: .laneMissing))
    XCTAssertEqual(m.statusLine, "Bass was deleted in ChordLab A. Undo there brings it back.")
    XCTAssertEqual(m.actions, [change, stop])
    XCTAssertEqual(m.emphasised, .change)
    XCTAssertEqual(try model(state(status: .laneMissing, laneName: "", role: "", leaderLabel: "", leaders: [])).statusLine,
                   "The track was deleted in another ChordLab. Undo there brings it back.")
  }

  func testProjectChanged() throws {
    let m = try model(state(status: .projectChanged))
    XCTAssertEqual(m.statusLine, "ChordLab A holds a different project now.")
    XCTAssertEqual(m.actions, [FollowerRepairButton(repair: .change, title: "Pick a track…"), stop])
    XCTAssertEqual(m.emphasised, .change)
    XCTAssertEqual(try model(state(status: .projectChanged, leaderLabel: "", leaders: [])).statusLine,
                   "Another ChordLab holds a different project now.")
  }

  func testUnknown() throws {
    let m = try model(state(status: .unknown))
    XCTAssertEqual(m.statusLine, "Not playing.")
    XCTAssertEqual(m.actions, [change, stop])
    XCTAssertEqual(m.emphasised, .change)
  }

  /// The view draws `actions` and nothing else: change and stop following, each exactly once, in every status.
  func testEveryStatusHasChangeAndStopExactlyOnce() throws {
    for status in LinkStatus.allCases {
      let actions = try model(state(status: status)).actions
      XCTAssertEqual(actions.map(\.repair), [.change, .stopFollowing], "\(status)")
      XCTAssertEqual(Set(actions.map(\.title)).count, actions.count, "\(status)")
    }
  }

  func testTheWaitingFixtureUsesTheStoredHints() throws {
    let m = try model(try LinkStateTests.fixture("link-state-waiting.json"))
    XCTAssertEqual(m.headlineText, "Set to play Bass of ChordLab A")   // the listed ChordLab C is another label
    XCTAssertEqual(m.statusLine, "Waiting for ChordLab A. It is not in this set, or it now plays from another ChordLab.")
    XCTAssertEqual(m.role, .bass)
    XCTAssertNil(m.switchOffer)
  }

  // MARK: - Fixed texts and notes

  func testFixedTexts() throws {
    let m = try model(state())
    XCTAssertEqual(m.editNote, "Edit in ChordLab A's window.")
    XCTAssertEqual(FollowerPanelModel.routingReminder,
                   "On the instrument's track, set MIDI From to the track this ChordLab is on, choose this ChordLab "
                   + "below it (in a rack, by its chain's name), and set Monitor to In.")
    XCTAssertEqual(FollowerPanelModel.changeTitle, "Change…")
    XCTAssertEqual(FollowerPanelModel.stopTitle, "Stop following")
  }

  func testTheEditNoteWithoutALabel() throws {
    XCTAssertEqual(try model(state(status: .muted, leaderLabel: "", leaders: [])).editNote,
                   "Edit in the other ChordLab's window.")
  }

  func testTheKeptNoteOnlyWhenThisChordLabHeldSomething() throws {
    let kept = "This ChordLab's own chords and tracks are kept. They come back when you stop following."
    XCTAssertEqual(try model(state(), snapshot: try snapshot("snapshot-home-progression.json")).keptNote, kept)
    XCTAssertEqual(try model(state(), snapshot: try snapshot("snapshot-tracks.json")).keptNote, kept)
    XCTAssertNil(try model(state(), snapshot: try snapshot("snapshot-home-empty.json")).keptNote)
    XCTAssertNil(try model(state(), snapshot: nil).keptNote)        // nothing known yet: say nothing
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
    XCTAssertEqual(offer.line, "ChordLab C was copied together with this one, but this one still plays from ChordLab A.")
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

  /// Nothing to open: no switch offer at all.
  func testACopyThatIsNotOfferedGivesNoSwitchOffer() throws {
    let unpublished = state(leaders: [leaderA, copy([lane("x", "Bass", "bass")], lineage: "")], switchTo: copyRef)
    XCTAssertNil(try model(unpublished).switchOffer)
    XCTAssertNil(try model(state(leaders: [leaderA], switchTo: copyRef)).switchOffer)
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
    XCTAssertEqual(offer.line, "Another ChordLab was copied together with this one, but this one still plays from ChordLab A.")
    XCTAssertEqual(offer.button, "Play Bass of the copy instead")
  }

  func testAnUnknownTrackSwitchButtonSaysTheSameTrack() throws {
    let s = state(laneName: "", role: "", leaders: [copy([lane("x", "Bass", "bass")])], switchTo: copyRef)
    XCTAssertEqual(try XCTUnwrap(try model(s).switchOffer).button, "Play a track of ChordLab C instead")
  }

  // MARK: - The headline's verb and the status's tone (Task 11 review)

  func testTheHeadlineSaysPlaysOnlyWhileItPlaysOrIsMuted() throws {
    XCTAssertEqual(try model(state(status: .playing)).headlineText, "Plays Bass of ChordLab A")
    XCTAssertEqual(try model(state(status: .muted)).headlineText, "Plays Bass of ChordLab A")
    for status in [LinkStatus.waiting, .laneMissing, .projectChanged, .unknown] {
      let m = try model(state(status: status))
      XCTAssertEqual(m.headline, [TextRun("Set to play "), TextRun("Bass", bold: true), TextRun(" of "),
                                  TextRun("ChordLab A", bold: true)], "\(status)")
    }
    XCTAssertEqual(try model(state(status: .laneMissing, lane: "chords", laneName: "", role: "")).headlineText,
                   "Set to play the chords of ChordLab A")
    XCTAssertEqual(try model(state(status: .unknown, laneName: "", role: "", leaderLabel: "", leaders: [])).headline,
                   [TextRun("Set to play a track of another ChordLab")])
  }

  func testTheToneIsAttentionWhenSomethingMustBeRepaired() throws {
    XCTAssertEqual(try model(state(status: .playing)).statusTone, .fine)
    XCTAssertEqual(try model(state(status: .muted)).statusTone, .fine)
    for status in [LinkStatus.waiting, .laneMissing, .projectChanged, .unknown] {
      XCTAssertEqual(try model(state(status: status)).statusTone, .attention, "\(status)")
    }
  }

  // MARK: - The switch button's title and lineage (Task 11 review)

  /// `.choose` opens the copy's lanes: the title names no track (the copy may not have one of that name).
  func testAChooseButtonNamesNoTrack() throws {
    let s = state(leaders: [leaderA, copy([lane("chords", "Chords", ""), lane("x", "Bass 2", "bass")])], switchTo: copyRef)
    let offer = try XCTUnwrap(try model(s).switchOffer)
    XCTAssertEqual(offer.action, .choose(leader: "c"))
    XCTAssertEqual(offer.button, "Play a track of ChordLab C instead")
  }

  func testAFollowButtonNamesTheCopysLaneBounded() throws {
    let long = String(repeating: "B", count: 64)
    let a = LinkLeaderJSON(id: "a", label: "ChordLab A", lineage: "t", lanes: [lane("u-bass", long, "bass")])
    let s = state(laneName: long, leaders: [a, copy([lane("u-bass", long, "bass")])], switchTo: copyRef)
    let offer = try XCTUnwrap(try model(s).switchOffer)
    XCTAssertEqual(offer.button, "Play \(FollowLabels.short(long)) of ChordLab C instead")
    guard case .follow(let option) = offer.action else { return XCTFail("expected a follow") }
    XCTAssertEqual(option.ownLineage, "own")
  }
}
