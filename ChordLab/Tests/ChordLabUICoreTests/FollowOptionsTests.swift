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

  func testTheLeaderFixtureOffersItsOriginalUnfollowedLanesFirst() throws {
    let groups = FollowOptions.groups(try LinkStateTests.fixture("link-state-leader.json"))
    XCTAssertEqual(groups.map(\.leader), ["id-original"])
    XCTAssertEqual(groups[0].title, "ChordLab A")
    XCTAssertEqual(groups[0].options.map(\.name), ["Chords", "Lead", "Bass"])     // Bass has a follower
    XCTAssertEqual(groups[0].options.map(\.lane), ["chords", "uid-3", "uid-2"])
    XCTAssertEqual(groups[0].options.map(\.role), [nil, .lead, .bass])
    XCTAssertEqual(groups[0].options.map(\.leaderLineage), ["lineage-id-original", "lineage-id-original",
                                                            "lineage-id-original"])
    XCTAssertEqual(groups[0].options.map(\.isCurrent), [false, false, false])
    XCTAssertEqual(groups[0].options[2].detail, "(on 1 other track)")
    XCTAssertEqual(groups[0].options[2].menuTitle, "Bass  (on 1 other track)")
    XCTAssertNil(groups[0].options[0].detail)
    XCTAssertEqual(groups[0].options[0].menuTitle, "Chords")
  }

  func testTheFollowerFixtureMarksTheCurrentLane() throws {
    let groups = FollowOptions.groups(try LinkStateTests.fixture("link-state-follower.json"))
    XCTAssertEqual(groups.map(\.leader), ["id-leader", "uid-6"])
    XCTAssertEqual(groups.map(\.title), ["ChordLab A", "ChordLab C"])
    let current = groups[0].options.filter(\.isCurrent)
    XCTAssertEqual(current.map(\.lane), ["uid-2"])
    XCTAssertEqual(groups[0].options.map(\.name), ["Chords", "Lead", "Bass"])
    XCTAssertEqual(current[0].otherTracks, 1)                       // two follow it, this one among them
    XCTAssertEqual(current[0].menuTitle, "Bass  ✓  (on 1 other track)")
    XCTAssertEqual(groups[1].options.map(\.isCurrent), [false, false, false])  // the copy's uid-2 is another lane
    XCTAssertEqual(groups[1].options.map(\.name), ["Chords", "Bass", "Lead"])
  }

  func testTheWaitingFixtureOffersTheOtherLeader() throws {
    let groups = FollowOptions.groups(try LinkStateTests.fixture("link-state-waiting.json"))
    XCTAssertEqual(groups.map(\.leader), ["id-other"])
    XCTAssertEqual(groups[0].options.map(\.name), ["Chords", "Keys"])
    XCTAssertEqual(groups[0].options.map(\.role), [nil, .other])
  }

  // MARK: - What is not offered

  func testNoLeadersIsNoOptions() {
    XCTAssertEqual(FollowOptions.groups(state([])), [])
    XCTAssertEqual(FollowOptions.groups(nil), [])
  }

  func testALeaderThatHasNotPublishedIsNotOffered() {
    let groups = FollowOptions.groups(state([leader("a", "ChordLab A", lineage: ""),
                                             leader("b", "ChordLab B", lanes: []),
                                             leader("c", "ChordLab C")]))
    XCTAssertEqual(groups.map(\.leader), ["c"])
  }

  func testIdentifiersTheBridgeWouldRefuseAreNotOffered() {
    let long = String(repeating: "x", count: 65)
    let groups = FollowOptions.groups(state([
      leader(long, "ChordLab A"),
      leader("b", "ChordLab B", lineage: long),
      leader("c", "ChordLab C", lanes: [lane("", "Bass", "bass"), lane(long, "Lead", "lead"), lane("ok", "Keys", "other")]),
      leader("", "ChordLab D"),
    ]))
    XCTAssertEqual(groups.map(\.leader), ["c"])
    XCTAssertEqual(groups[0].options.map(\.lane), ["ok"])
  }

  // MARK: - Names, order, identity

  func testAnUnnamedTrackIsNamedByItsRoleAndTheChordsLaneIsAlwaysChords() {
    let groups = FollowOptions.groups(state([leader("a", "ChordLab A", lanes: [
      lane("chords", "Akkorde", ""), lane("u1", "", "bass"), lane("u2", "", "lead"), lane("u3", "", "other"),
      lane("u4", "My line", "lead"),
    ])]))
    XCTAssertEqual(groups[0].options.map(\.name), ["Chords", "Bass", "Lead", "Other", "My line"])
  }

  func testUnfollowedLanesComeFirstThenTheOthersEachInTheLeadersOrder() {
    let groups = FollowOptions.groups(state([leader("a", "ChordLab A", lanes: [
      lane("chords", "Chords", "", followers: 1), lane("u1", "One", "bass"), lane("u2", "Two", "lead", followers: 2),
      lane("u3", "Three", "other"), lane("u4", "Four", "bass", followers: 1),
    ])]))
    XCTAssertEqual(groups[0].options.map(\.lane), ["u1", "u3", "chords", "u2", "u4"])
    XCTAssertEqual(groups[0].options.map(\.detail), [nil, nil, "(on 1 other track)", "(on 2 other tracks)",
                                                     "(on 1 other track)"])
  }

  func testLeadersKeepTheirOrderAndACopysOriginalComesFirst() {
    let leaders = [leader("a", "ChordLab A"), leader("b", "ChordLab B"), leader("c", "ChordLab C")]
    XCTAssertEqual(FollowOptions.groups(state(leaders)).map(\.leader), ["a", "b", "c"])
    XCTAssertEqual(FollowOptions.groups(state(leaders, copyOf: "c")).map(\.leader), ["c", "a", "b"])
    XCTAssertEqual(FollowOptions.groups(state(leaders, copyOf: "gone")).map(\.leader), ["a", "b", "c"])
  }

  func testTheCurrentLaneIsMatchedByLeaderIdAndLaneKey() {
    let leaders = [leader("a", "ChordLab A", lanes: [lane("chords", "Chords", ""), lane("same", "Bass", "bass")]),
                   leader("b", "ChordLab B", lanes: [lane("chords", "Chords", ""), lane("same", "Bass", "bass")])]
    let groups = FollowOptions.groups(state(leaders, following: ("b", "same")))
    XCTAssertEqual(groups[0].options.map(\.isCurrent), [false, false])
    XCTAssertEqual(groups[1].options.map(\.isCurrent), [false, true])
    XCTAssertEqual(groups[1].options[1].id, FollowTarget(leader: "b", lane: "same"))
  }

  func testACurrentLaneFollowedOnlyByThisInstanceHasNoDetail() {
    let groups = FollowOptions.groups(state([leader("a", "ChordLab A", lanes: [lane("u", "Bass", "bass", followers: 1)])],
                                            following: ("a", "u")))
    XCTAssertEqual(groups[0].options[0].otherTracks, 0)
    XCTAssertNil(groups[0].options[0].detail)
    XCTAssertEqual(groups[0].options[0].menuTitle, "Bass  ✓")
    XCTAssertEqual(groups[0].options[0].followers, 1)
  }

  func testAnEmptyLabelIsAnotherChordLab() {
    let groups = FollowOptions.groups(state([leader("a", "")]))
    XCTAssertEqual(groups[0].title, "Another ChordLab")
    XCTAssertEqual(groups[0].options[0].leaderLabel, "")
  }

  func testTheGroupIsFoundByLeaderId() {
    let groups = FollowOptions.groups(state([leader("a", "ChordLab A"), leader("b", "ChordLab B")]))
    XCTAssertEqual(FollowOptions.group(leader: "b", in: groups)?.title, "ChordLab B")
    XCTAssertNil(FollowOptions.group(leader: "z", in: groups))
    XCTAssertEqual(groups.map(\.id), ["a", "b"])
  }

  func testTheMenuIsCalledFollow() {
    XCTAssertEqual(FollowOptions.menuTitle, "Follow")
  }

  // MARK: - The request

  func testAnOptionsRequestCarriesItsIdentityAndItsLeadersLineage() {
    let option = FollowOption(leader: "a", lane: "uid-bass", leaderLineage: "t", leaderLabel: "ChordLab A",
                              name: "Bass", role: .bass, followers: 0, isCurrent: false)
    XCTAssertEqual(FollowRequest(option), FollowRequest(leader: "a", lane: "uid-bass", leaderLabel: "ChordLab A"))
    XCTAssertEqual(option.command as NSDictionary,
                   ["op": "follow", "leader": "a", "lane": "uid-bass", "leaderLineage": "t"] as NSDictionary)
  }
}
