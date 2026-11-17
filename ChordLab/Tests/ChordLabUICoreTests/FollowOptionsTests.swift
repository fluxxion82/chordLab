import Foundation
import XCTest
@testable import ChordLabUICore

/// What the Follow picker offers (UI/Core/FollowOptions.swift).
final class FollowOptionsTests: XCTestCase {
  private func lane(_ key: String, _ name: String, _ role: String, followers: Int = 0) -> LinkLaneJSON {
    LinkLaneJSON(lane: key, name: name, role: role, chords: key == "chords", audible: true, followers: followers)
  }

  private func leader(_ id: String, _ label: String, lineage: String = "t", lanes: [LinkLaneJSON]? = nil) -> LinkLeaderJSON {
    LinkLeaderJSON(id: id, label: label, lineage: lineage,
                   lanes: lanes ?? [lane("chords", "Chords", ""), lane("u-\(id)", "Bass", "bass")])
  }

  private func state(_ leaders: [LinkLeaderJSON], copyOf: String? = nil,
                     following: (String, String)? = nil) -> LinkStateJSON {
    LinkStateJSON(
      schema: 1,
      instance: LinkSelfJSON(id: "me", label: "ChordLab Z", copyOf: copyOf.map { LinkInstanceJSON(leader: $0, label: "") }),
      following: following.map {
        LinkFollowingJSON(leader: $0.0, leaderLabel: "", lane: $0.1, laneName: "", role: "", status: .playing,
                          switchTo: nil)
      },
      leaders: leaders, routed: [])
  }

  // MARK: - Fixtures

  func testTheLeaderFixtureOffersTheOtherLeadersUnfollowedLanesFirst() throws {
    let groups = FollowOptions.groups(try LinkStateTests.fixture("link-state-leader.json"), ownLineage: "own")
    XCTAssertEqual(groups.map(\.leader), ["id-original"])
    XCTAssertEqual(groups[0].title, "ChordLab A")
    XCTAssertEqual(groups[0].options.map(\.name), ["Chords", "Lead", "Bass"])     // Bass has a follower
    XCTAssertEqual(groups[0].options.map(\.lane), ["chords", "uid-3", "uid-2"])
    XCTAssertEqual(groups[0].options.map(\.role), [nil, .lead, .bass])
    XCTAssertEqual(groups[0].options.map(\.leaderLineage), ["lineage-id-original", "lineage-id-original",
                                                            "lineage-id-original"])
    XCTAssertEqual(groups[0].options.map(\.isCurrent), [false, false, false])
    XCTAssertEqual(groups[0].options[2].detail, "(1 other ChordLab plays it)")
    XCTAssertEqual(groups[0].options[2].menuTitle, "Bass  (1 other ChordLab plays it)")
    XCTAssertNil(groups[0].options[0].detail)
    XCTAssertEqual(groups[0].options[0].menuTitle, "Chords")
  }

  func testTheFollowerFixtureMarksTheCurrentLane() throws {
    let groups = FollowOptions.groups(try LinkStateTests.fixture("link-state-follower.json"), ownLineage: "own")
    XCTAssertEqual(groups.map(\.leader), ["id-leader", "uid-6"])
    XCTAssertEqual(groups.map(\.title), ["ChordLab A", "ChordLab C"])
    let current = groups[0].options.filter(\.isCurrent)
    XCTAssertEqual(current.map(\.lane), ["uid-2"])
    XCTAssertEqual(groups[0].options.map(\.name), ["Chords", "Lead", "Bass"])
    XCTAssertEqual(current[0].otherTracks, 1)                       // two follow it, this one among them
    XCTAssertEqual(current[0].menuTitle, "Bass  ✓  (1 other ChordLab plays it)")
    XCTAssertEqual(groups[1].options.map(\.isCurrent), [false, false, false])  // the copy's uid-2 is another lane
    XCTAssertEqual(groups[1].options.map(\.name), ["Chords", "Bass", "Lead"])
  }

  func testTheWaitingFixtureOffersTheOtherLeader() throws {
    let groups = FollowOptions.groups(try LinkStateTests.fixture("link-state-waiting.json"), ownLineage: "own")
    XCTAssertEqual(groups.map(\.leader), ["id-other"])
    XCTAssertEqual(groups[0].options.map(\.name), ["Chords", "Keys"])
    XCTAssertEqual(groups[0].options.map(\.role), [nil, .other])
  }

  // MARK: - What is not offered

  func testNoLeadersIsNoOptions() {
    XCTAssertEqual(FollowOptions.groups(state([]), ownLineage: "own"), [])
    XCTAssertEqual(FollowOptions.groups(nil, ownLineage: "own"), [])
  }

  func testALeaderThatHasNotPublishedIsNotOffered() {
    let groups = FollowOptions.groups(state([leader("a", "ChordLab A", lineage: ""),
                                             leader("b", "ChordLab B", lanes: []),
                                             leader("c", "ChordLab C")]), ownLineage: "own")
    XCTAssertEqual(groups.map(\.leader), ["c"])
  }

  func testIdentifiersTheBridgeWouldRefuseAreNotOffered() {
    let long = String(repeating: "x", count: 65)
    let groups = FollowOptions.groups(state([
      leader(long, "ChordLab A"),
      leader("b", "ChordLab B", lineage: long),
      leader("c", "ChordLab C", lanes: [lane("", "Bass", "bass"), lane(long, "Lead", "lead"), lane("ok", "Keys", "other")]),
      leader("", "ChordLab D"),
    ]), ownLineage: "own")
    XCTAssertEqual(groups.map(\.leader), ["c"])
    XCTAssertEqual(groups[0].options.map(\.lane), ["ok"])
  }

  // MARK: - Names, order, identity

  func testAnUnnamedTrackIsNamedByItsRoleAndTheChordsLaneIsAlwaysChords() {
    let groups = FollowOptions.groups(state([leader("a", "ChordLab A", lanes: [
      lane("chords", "Akkorde", ""), lane("u1", "", "bass"), lane("u2", "", "lead"), lane("u3", "", "other"),
      lane("u4", "My line", "lead"),
    ])]), ownLineage: "own")
    XCTAssertEqual(groups[0].options.map(\.name), ["Chords", "Bass", "Lead", "Other", "My line"])
  }

  func testUnfollowedLanesComeFirstThenTheOthersEachInTheLeadersOrder() {
    let groups = FollowOptions.groups(state([leader("a", "ChordLab A", lanes: [
      lane("chords", "Chords", "", followers: 1), lane("u1", "One", "bass"), lane("u2", "Two", "lead", followers: 2),
      lane("u3", "Three", "other"), lane("u4", "Four", "bass", followers: 1),
    ])]), ownLineage: "own")
    XCTAssertEqual(groups[0].options.map(\.lane), ["u1", "u3", "chords", "u2", "u4"])
    XCTAssertEqual(groups[0].options.map(\.detail), [nil, nil, "(1 other ChordLab plays it)", "(2 other ChordLabs play it)",
                                                     "(1 other ChordLab plays it)"])
  }

  /// The lane this instance plays, when nobody else plays it, stays among the free ones.
  func testTheOrderIsNobodyElsePlaysItFirst() {
    let groups = FollowOptions.groups(state([leader("a", "ChordLab A", lanes: [
      lane("chords", "Chords", "", followers: 1), lane("u1", "One", "bass", followers: 1),
      lane("u2", "Two", "lead", followers: 2), lane("u3", "Three", "other"),
    ])], following: ("a", "u1")), ownLineage: "own")
    XCTAssertEqual(groups[0].options.map(\.lane), ["u1", "u3", "chords", "u2"])
    XCTAssertEqual(groups[0].options.map(\.otherTracks), [0, 0, 1, 2])
  }

  /// Identities are keys of the views' `ForEach`: a repeated lane uid inside a leader, or a repeated leader id, keeps
  /// its first entry.
  func testARepeatedLaneOrLeaderKeepsTheFirst() {
    let groups = FollowOptions.groups(state([
      leader("a", "ChordLab A", lanes: [lane("chords", "Chords", ""), lane("u", "Bass", "bass"), lane("u", "Lead", "lead"),
                                        lane("chords", "Chords", "")]),
      leader("a", "ChordLab A again", lanes: [lane("v", "Keys", "other")]),
      leader("b", "ChordLab B"),
    ]), ownLineage: "own")
    XCTAssertEqual(groups.map(\.leader), ["a", "b"])
    XCTAssertEqual(groups[0].title, "ChordLab A")
    XCTAssertEqual(groups[0].options.map(\.lane), ["chords", "u"])
    XCTAssertEqual(groups[0].options.map(\.name), ["Chords", "Bass"])
  }

  // MARK: - The menu's warning

  func testTheMenuWarnsWhenOtherTracksPlayFromThisChordLab() throws {
    XCTAssertEqual(FollowOptions.warning(try LinkStateTests.fixture("link-state-leader.json")),
                   "Tracks that play from this ChordLab will stop.")
    XCTAssertNil(FollowOptions.warning(try LinkStateTests.fixture("link-state-copy.json")))
    XCTAssertNil(FollowOptions.warning(state([leader("a", "ChordLab A")])))
    XCTAssertNil(FollowOptions.warning(nil))
  }

  // MARK: - The picker's disabled first entries

  func testThePickersNotesAreTheRefusalThenTheWarning() throws {
    let leaderState = try LinkStateTests.fixture("link-state-leader.json")
    let refusal = FollowAttempt.refusalText(leaderLabel: "ChordLab A")
    XCTAssertEqual(FollowOptions.menuNotes(leaderState, notice: refusal),
                   [refusal, "Tracks that play from this ChordLab will stop."])
    XCTAssertEqual(FollowOptions.menuNotes(leaderState, notice: nil), ["Tracks that play from this ChordLab will stop."])
    XCTAssertEqual(FollowOptions.menuNotes(state([leader("a", "ChordLab A")]), notice: refusal), [refusal])
    XCTAssertEqual(FollowOptions.menuNotes(state([leader("a", "ChordLab A")]), notice: nil), [])
  }

  func testAPickerWithNothingToOfferSaysSo() throws {
    // The follower's Change… while the only other ChordLab is gone: the menu is never empty.
    XCTAssertEqual(FollowOptions.menuNotes(state([]), notice: nil), ["No other ChordLab to play from right now."])
    XCTAssertEqual(FollowOptions.menuNotes(state([leader("a", "ChordLab A", lineage: "")]), notice: nil),
                   [FollowOptions.nothingToPick])
    XCTAssertEqual(FollowOptions.menuNotes(nil, notice: nil), [FollowOptions.nothingToPick])
    let refusal = FollowAttempt.refusalText(leaderLabel: "")
    XCTAssertEqual(FollowOptions.menuNotes(state([]), notice: refusal), [refusal, FollowOptions.nothingToPick])
  }

  func testTheMenusTooltipSpeaksOfTracksNotLanes() {
    XCTAssertEqual(FollowOptions.menuHelp, "Make this ChordLab play one track of another ChordLab")
    for word in ["lane", "leader", "follow"] {
      XCTAssertFalse(FollowOptions.menuHelp.lowercased().contains(word), word)
      XCTAssertFalse(FollowOptions.nothingToPick.lowercased().contains(word), word)
    }
  }

  // MARK: - The copy fixture

  func testTheCopyFixtureOffersItsOriginalFirstWithBassPlayedByOneOther() throws {
    let groups = FollowOptions.groups(try LinkStateTests.fixture("link-state-copy.json"), ownLineage: "own")
    XCTAssertEqual(groups.map(\.leader), ["id-original"])
    XCTAssertEqual(groups[0].options.map(\.name), ["Chords", "Lead", "Bass"])
    XCTAssertEqual(groups[0].options[2].detail, "(1 other ChordLab plays it)")
  }

  func testLeadersKeepTheirOrderAndACopysOriginalComesFirst() {
    let leaders = [leader("a", "ChordLab A"), leader("b", "ChordLab B"), leader("c", "ChordLab C")]
    XCTAssertEqual(FollowOptions.groups(state(leaders), ownLineage: "own").map(\.leader), ["a", "b", "c"])
    XCTAssertEqual(FollowOptions.groups(state(leaders, copyOf: "c"), ownLineage: "own").map(\.leader), ["c", "a", "b"])
    XCTAssertEqual(FollowOptions.groups(state(leaders, copyOf: "gone"), ownLineage: "own").map(\.leader), ["a", "b", "c"])
  }

  func testTheCurrentLaneIsMatchedByLeaderIdAndLaneKey() {
    let leaders = [leader("a", "ChordLab A", lanes: [lane("chords", "Chords", ""), lane("same", "Bass", "bass")]),
                   leader("b", "ChordLab B", lanes: [lane("chords", "Chords", ""), lane("same", "Bass", "bass")])]
    let groups = FollowOptions.groups(state(leaders, following: ("b", "same")), ownLineage: "own")
    XCTAssertEqual(groups[0].options.map(\.isCurrent), [false, false])
    XCTAssertEqual(groups[1].options.map(\.isCurrent), [false, true])
    XCTAssertEqual(groups[1].options[1].id, FollowTarget(leader: "b", lane: "same"))
  }

  func testACurrentLaneFollowedOnlyByThisInstanceHasNoDetail() {
    let groups = FollowOptions.groups(state([leader("a", "ChordLab A", lanes: [lane("u", "Bass", "bass", followers: 1)])],
                                            following: ("a", "u")), ownLineage: "own")
    XCTAssertEqual(groups[0].options[0].otherTracks, 0)
    XCTAssertNil(groups[0].options[0].detail)
    XCTAssertEqual(groups[0].options[0].menuTitle, "Bass  ✓")
    XCTAssertEqual(groups[0].options[0].followers, 1)
  }

  func testAnEmptyLabelIsAnotherChordLab() {
    let groups = FollowOptions.groups(state([leader("a", "")]), ownLineage: "own")
    XCTAssertEqual(groups[0].title, "Another ChordLab")
    XCTAssertEqual(groups[0].options[0].leaderLabel, "")
  }

  func testTheGroupIsFoundByLeaderId() {
    let groups = FollowOptions.groups(state([leader("a", "ChordLab A"), leader("b", "ChordLab B")]), ownLineage: "own")
    XCTAssertEqual(FollowOptions.group(leader: "b", in: groups)?.title, "ChordLab B")
    XCTAssertNil(FollowOptions.group(leader: "z", in: groups))
    XCTAssertEqual(groups.map(\.id), ["a", "b"])
  }

  func testTheMenuIsCalledFollow() {
    XCTAssertEqual(FollowOptions.menuTitle, "Follow")
  }

  // MARK: - The request

  func testAnOptionsRequestCarriesItsIdentityAndItsLeadersLineage() {
    let option = FollowOption(leader: "a", lane: "uid-bass", leaderLineage: "t", ownLineage: "own", leaderLabel: "ChordLab A",
                              name: "Bass", role: .bass, followers: 0, isCurrent: false)
    XCTAssertEqual(FollowRequest(option), FollowRequest(leader: "a", lane: "uid-bass", leaderLabel: "ChordLab A"))
    XCTAssertEqual(option.command as NSDictionary,
                   ["op": "follow", "leader": "a", "lane": "uid-bass", "leaderLineage": "t"] as NSDictionary)
  }

  // MARK: - The own lineage, labels, tooltips (Task 11 review)

  /// An entry keeps the own lineage it was drawn under, and its request carries that one, whatever the token is at the
  /// click: a state load in between makes the shell answer stale instead of applying it over the loaded follow.
  func testAnEntryKeepsTheOwnLineageItWasDrawnUnder() {
    let groups = FollowOptions.groups(state([leader("a", "ChordLab A")]), ownLineage: "doc-1")
    let option = groups[0].options[1]
    XCTAssertEqual(option.ownLineage, "doc-1")
    XCTAssertEqual(option.stampedCommand as NSDictionary,
                   ["op": "follow", "leader": "a", "lane": "u-a", "leaderLineage": "t", "lineage": "doc-1"] as NSDictionary)
    XCTAssertNil(option.command["lineage"])
    XCTAssertEqual(FollowOptions.groups(state([leader("a", "ChordLab A")]), ownLineage: "doc-2")[0].options[1].ownLineage,
                   "doc-2")
  }

  func testALongNameIsShortenedInTheMiddle() {
    XCTAssertEqual(FollowLabels.maxCharacters, 16)
    XCTAssertEqual(FollowLabels.short("Bass"), "Bass")
    XCTAssertEqual(FollowLabels.short("Sixteen chars ok"), "Sixteen chars ok")
    XCTAssertEqual(FollowLabels.short("Seventeen chars!!"), "Seventee…chars!!")
    XCTAssertEqual(FollowLabels.short("Seventeen chars!!").count, 16)
    let long = String(repeating: "W", count: 64)
    XCTAssertEqual(FollowLabels.short(long), String(repeating: "W", count: 8) + "…" + String(repeating: "W", count: 7))
    XCTAssertEqual(FollowLabels.short("ä🎸ä🎸ä🎸ä🎸ä🎸ä🎸ä🎸ä🎸ä🎸", max: 5), "ä🎸…ä🎸")   // by characters, never splitting one
    let option = FollowOptions.groups(state([leader("a", "ChordLab A", lanes: [lane("u", long, "bass")])]),
                                      ownLineage: "own")[0].options[0]
    XCTAssertEqual(option.label.count, 16)
    XCTAssertEqual(option.name, long)                               // the menu keeps the whole name
  }

  func testTheTooltipSaysWhoElsePlaysIt() {
    let lanes = [lane("chords", "Chords", ""), lane("u1", "Bass", "bass", followers: 1), lane("u2", "Lead", "lead", followers: 2)]
    let options = FollowOptions.groups(state([leader("a", "ChordLab A", lanes: lanes)]), ownLineage: "own")[0].options
    XCTAssertEqual(options.map(\.tooltip), [nil, "Another ChordLab already plays it.", "2 other ChordLabs already play it."])
  }

  func testTheMenuShowsOnlyWhileThePickerOffersSomething() throws {
    XCTAssertTrue(FollowOptions.showsMenu(try LinkStateTests.fixture("link-state-leader.json")))
    XCTAssertFalse(FollowOptions.showsMenu(state([])))
    XCTAssertFalse(FollowOptions.showsMenu(state([leader("a", "ChordLab A", lineage: "")])))
    XCTAssertFalse(FollowOptions.showsMenu(nil))
  }

  func testThePickerGroupsAreEveryGroupOrOneLeaders() {
    let s = state([leader("a", "ChordLab A"), leader("b", "ChordLab B")])
    XCTAssertEqual(FollowOptions.pickerGroups(s, ownLineage: "own").map(\.leader), ["a", "b"])
    XCTAssertEqual(FollowOptions.pickerGroups(s, ownLineage: "own", leader: "b").map(\.leader), ["b"])
    XCTAssertEqual(FollowOptions.pickerGroups(s, ownLineage: "own", leader: "z").map(\.leader), [])
    XCTAssertEqual(FollowOptions.pickerGroups(s, ownLineage: "doc-9")[1].options[0].ownLineage, "doc-9")
  }
}
