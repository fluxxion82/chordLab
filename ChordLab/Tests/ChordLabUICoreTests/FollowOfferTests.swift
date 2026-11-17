import Foundation
import XCTest
@testable import ChordLabUICore

/// When the Follow banner shows and what it says (UI/Core/FollowOffer.swift).
final class FollowOfferTests: XCTestCase {
  private func snapshot(_ name: String, edit: ((inout [String: Any]) -> Void)? = nil) throws -> Snapshot {
    let url = try SnapshotTests.fixturesDirectory().appendingPathComponent(name)
    var object = try XCTUnwrap(try JSONSerialization.jsonObject(with: Data(contentsOf: url)) as? [String: Any])
    edit?(&object)
    return try JSONDecoder().decode(Snapshot.self, from: JSONSerialization.data(withJSONObject: object))
  }

  private func fresh() throws -> Snapshot { try snapshot("snapshot-home-empty.json") }

  private func lane(_ key: String, _ name: String, _ role: String, followers: Int = 0) -> LinkLaneJSON {
    LinkLaneJSON(lane: key, name: name, role: role, chords: key == "chords", audible: true, followers: followers)
  }

  private func leader(_ id: String, _ label: String, lineage: String = "t", lanes: [LinkLaneJSON]? = nil) -> LinkLeaderJSON {
    LinkLeaderJSON(id: id, label: label, lineage: lineage,
                   lanes: lanes ?? [lane("chords", "Chords", ""), lane("u-b", "Bass", "bass"), lane("u-l", "Lead", "lead")])
  }

  /// Inline, not the leader fixture (its `copyOf` will change with the engine's tightened rules).
  private func link(_ leaders: [LinkLeaderJSON], copyOf: String? = nil, following: Bool = false,
                    routed: [LinkRoutedJSON] = []) -> LinkStateJSON {
    LinkStateJSON(
      schema: 1,
      instance: LinkSelfJSON(id: "me", label: "ChordLab B",
                             copyOf: copyOf.map { LinkInstanceJSON(leader: $0, label: "ChordLab ?") }),
      following: following
        ? LinkFollowingJSON(leader: "a", leaderLabel: "ChordLab A", lane: "u-b", laneName: "Bass", role: "bass",
                            status: .playing, switchTo: nil)
        : nil,
      leaders: leaders, routed: routed)
  }

  private func offer(_ link: LinkStateJSON?, _ snapshot: Snapshot?, dismissed: String? = nil) -> FollowOffer? {
    FollowOffer.make(link: link, snapshot: snapshot, lineage: "doc-1", dismissedLineage: dismissed)
  }

  // MARK: - Fresh

  func testAFreshInstanceIsOfferedTheFirstLeadersLanes() throws {
    let o = try XCTUnwrap(offer(link([leader("a", "ChordLab A")]), try fresh()))
    XCTAssertFalse(o.isCopy)
    XCTAssertEqual(o.leader, "a")
    XCTAssertEqual(o.text(compact: false), "Make this ChordLab play one track of ChordLab A:")
    XCTAssertEqual(o.text(compact: true), "Play from ChordLab A:")
    XCTAssertEqual(o.buttons.map(\.name), ["Chords", "Bass", "Lead"])
    XCTAssertEqual(o.buttons.map(\.id), [FollowTarget(leader: "a", lane: "chords"), FollowTarget(leader: "a", lane: "u-b"),
                                         FollowTarget(leader: "a", lane: "u-l")])
    XCTAssertFalse(o.showsMore)
  }

  func testTheButtonsFollowThePickersOrder() throws {
    let a = leader("a", "ChordLab A", lanes: [lane("chords", "Chords", ""), lane("u-b", "Bass", "bass", followers: 1),
                                              lane("u-l", "Lead", "lead")])
    XCTAssertEqual(try XCTUnwrap(offer(link([a]), try fresh())).buttons.map(\.name), ["Chords", "Lead", "Bass"])
  }

  func testAProgressionOrATrackIsNotFresh() throws {
    XCTAssertNil(offer(link([leader("a", "ChordLab A")]), try snapshot("snapshot-home-progression.json")))
    let tracksOnly = try snapshot("snapshot-tracks.json") { $0["events"] = [] }
    XCTAssertNil(offer(link([leader("a", "ChordLab A")]), tracksOnly))
    XCTAssertTrue(FollowOffer.isFresh(try fresh()))
    XCTAssertFalse(FollowOffer.isFresh(tracksOnly))
  }

  func testASnapshotWithoutTracksAndWithoutChordsIsFresh() throws {
    let older = try snapshot("snapshot-home-empty.json") { $0.removeValue(forKey: "tracks") }
    XCTAssertTrue(FollowOffer.isFresh(older))
  }

  func testNoSnapshotYetIsNotFresh() {
    XCTAssertFalse(FollowOffer.isFresh(nil))
    XCTAssertNil(offer(link([leader("a", "ChordLab A")]), nil))
  }

  // MARK: - Copy

  func testACopyIsOfferedItsOriginalFirstWhateverItHolds() throws {
    let leaders = [leader("a", "ChordLab A"), leader("c", "ChordLab C")]
    let o = try XCTUnwrap(offer(link(leaders, copyOf: "c"), try snapshot("snapshot-tracks.json")))
    XCTAssertTrue(o.isCopy)
    XCTAssertEqual(o.leader, "c")
    XCTAssertEqual(o.text(compact: false), "This ChordLab is a copy of ChordLab C. Make it play only one of its tracks:")
    XCTAssertEqual(o.text(compact: true), "A copy of ChordLab C. Play only:")
    XCTAssertTrue(o.showsMore)                                     // two leaders
    XCTAssertTrue(try XCTUnwrap(offer(link(leaders, copyOf: "c"), nil)).isCopy)
  }

  func testACopyOfALeaderThatIsNotOfferedHasNoCopyWording() throws {
    let leaders = [leader("a", "ChordLab A"), leader("c", "ChordLab C", lineage: "")]
    XCTAssertNil(offer(link(leaders, copyOf: "c"), try snapshot("snapshot-tracks.json")))   // not fresh either
    let o = try XCTUnwrap(offer(link(leaders, copyOf: "c"), try fresh()))                     // the fresh rule applies
    XCTAssertFalse(o.isCopy)
    XCTAssertEqual(o.leader, "a")
    XCTAssertEqual(o.text(compact: false), "Make this ChordLab play one track of ChordLab A:")
  }

  // MARK: - Never

  func testNeverWhileFollowing() throws {
    XCTAssertNil(offer(link([leader("a", "ChordLab A")], copyOf: "a", following: true), try fresh()))
  }

  func testNeverWithoutOptions() throws {
    XCTAssertNil(offer(link([]), try fresh()))
    XCTAssertNil(offer(link([leader("a", "ChordLab A", lineage: "")]), try fresh()))
    XCTAssertNil(offer(nil, try fresh()))
  }

  func testNeverAfterTheUserDismissedItForThisDocument() throws {
    let l = link([leader("a", "ChordLab A")], copyOf: "a")
    XCTAssertNil(offer(l, try fresh(), dismissed: "doc-1"))
    XCTAssertNotNil(offer(l, try fresh(), dismissed: "doc-0"))   // another document may offer again
  }

  /// Other tracks play from this ChordLab: following would silence them, so the banner never asks.
  func testNeverWhileOtherTracksPlayFromThisChordLab() throws {
    let routed = [LinkRoutedJSON(lane: "chords", followers: 1, trackId: 0)]
    XCTAssertNil(offer(link([leader("a", "ChordLab A")], routed: routed), try fresh()))
    XCTAssertNil(offer(link([leader("a", "ChordLab A")], copyOf: "a", routed: routed), try fresh()))
  }

  /// After Stop following the instance is not following any more, and may be fresh: the banner must not come back at
  /// once. The state records `dismissedLineage(afterStopFollowing:)` as the dismissal.
  func testTheBannerDoesNotComeBackRightAfterStopFollowing() throws {
    let l = link([leader("a", "ChordLab A")])
    XCTAssertNotNil(offer(l, try fresh()))
    let recorded = FollowOffer.dismissedLineage(afterStopFollowing: "doc-1")
    XCTAssertNil(offer(l, try fresh(), dismissed: recorded))
    XCTAssertNotNil(FollowOffer.make(link: l, snapshot: try fresh(), lineage: "doc-2", dismissedLineage: recorded))
  }

  // MARK: - Fixtures

  /// A copy nobody plays from, seeing its original: the copy banner whatever the copy holds.
  func testTheCopyFixtureGivesTheCopyBanner() throws {
    let copy = try LinkStateTests.fixture("link-state-copy.json")
    let o = try XCTUnwrap(offer(copy, try snapshot("snapshot-tracks.json")))
    XCTAssertTrue(o.isCopy)
    XCTAssertEqual(o.leader, "id-original")
    XCTAssertEqual(o.text(compact: false), "This ChordLab is a copy of ChordLab A. Make it play only one of its tracks:")
    XCTAssertEqual(o.buttons.map(\.name), ["Chords", "Lead", "Bass"])
    XCTAssertFalse(o.showsMore)
  }

  /// Lanes of this ChordLab play elsewhere (`routed`): no banner, even for a fresh snapshot.
  func testTheLeaderFixtureGivesNoBanner() throws {
    let leader = try LinkStateTests.fixture("link-state-leader.json")
    XCTAssertNil(offer(leader, try fresh()))
    XCTAssertNil(offer(leader, try snapshot("snapshot-tracks.json")))
  }

  // MARK: - Buttons and More…

  func testAtMostFiveButtonsAndMoreForTheRest() throws {
    let many = leader("a", "ChordLab A", lanes: [lane("chords", "Chords", "")] + (1...5).map { lane("u\($0)", "T\($0)", "other") })
    let o = try XCTUnwrap(offer(link([many]), try fresh()))
    XCTAssertEqual(o.buttons.map(\.name), ["Chords", "T1", "T2", "T3", "T4"])
    XCTAssertTrue(o.showsMore)
    XCTAssertEqual(FollowOffer.moreTitle, "More…")
  }

  func testMoreLeadersShowMore() throws {
    let o = try XCTUnwrap(offer(link([leader("a", "ChordLab A"), leader("b", "ChordLab B")]), try fresh()))
    XCTAssertEqual(o.leader, "a")
    XCTAssertTrue(o.showsMore)
  }

  func testAnEmptyLabelIsAnotherChordLab() throws {
    let o = try XCTUnwrap(offer(link([leader("a", "")]), try fresh()))
    XCTAssertEqual(o.text(compact: false), "Make this ChordLab play one track of another ChordLab:")
    let c = try XCTUnwrap(offer(link([leader("a", "")], copyOf: "a"), try fresh()))
    XCTAssertEqual(c.text(compact: false), "This ChordLab is a copy of another ChordLab. Make it play only one of its tracks:")
  }

  func testTheDismissButton() {
    XCTAssertEqual(FollowOffer.dismissTitle, "Not now")
  }

  // MARK: - Compact

  func testTheCompactRuleIsTheEditorsCompactHeight() {
    XCTAssertEqual(FollowOffer.compactHeight, 700)
    XCTAssertTrue(FollowOffer.isCompact(height: 632))
    XCTAssertTrue(FollowOffer.isCompact(height: 699.5))
    XCTAssertFalse(FollowOffer.isCompact(height: 700))
    XCTAssertFalse(FollowOffer.isCompact(height: 800))
  }
}
