import Foundation
import XCTest
@testable import ChordLabUICore

/// The link state (UI/Core/LinkState.swift): decoding the engine's `linkStateJson` (the three golden fixtures, each
/// status, unknown strings, an older build without `copyOf` and `trackId`) and the marks on followed lanes' tabs.
final class LinkStateTests: XCTestCase {
  private func decode(_ json: String) throws -> LinkStateJSON {
    try JSONDecoder().decode(LinkStateJSON.self, from: Data(json.utf8))
  }

  static func fixture(_ name: String) throws -> LinkStateJSON {
    let url = try SnapshotTests.fixturesDirectory().appendingPathComponent(name)
    guard FileManager.default.fileExists(atPath: url.path) else {
      throw SnapshotTests.FixtureMissing(description: "missing fixture \(url.path)")
    }
    return try JSONDecoder().decode(LinkStateJSON.self, from: Data(contentsOf: url))
  }

  /// A link state as a build from before `copyOf` and `trackId` wrote it (M2 as built): `self` without `copyOf`,
  /// `routed` entries without `trackId`.
  static let older = """
    {"schema":1,"self":{"id":"me","label":"ChordLab B"},"following":null,
     "leaders":[{"id":"a","label":"ChordLab A","lineage":"t-a",
                 "lanes":[{"lane":"chords","name":"Chords","role":"","chords":true,"audible":true,"followers":0}]}],
     "routed":[{"lane":"uid-1","followers":1}]}
    """

  private func following(status: String) -> String {
    """
    {"schema":1,"self":{"id":"me","label":"ChordLab B","copyOf":null},
     "following":{"leader":"a","leaderLabel":"ChordLab A","lane":"uid-1","laneName":"Bass","role":"bass",
                  "status":"\(status)","switchTo":null},
     "leaders":[],"routed":[]}
    """
  }

  // MARK: - Fixtures

  func testLeaderFixture() throws {
    let state = try Self.fixture("link-state-leader.json")
    XCTAssertEqual(state.schema, 1)
    XCTAssertEqual(state.instance.id, "uid-5")                  // copyOf is tested inline: this fixture's will change
    XCTAssertEqual(state.instance.label, "ChordLab B")
    XCTAssertNil(state.following)
    XCTAssertEqual(state.leaders.count, 1)
    let leader = state.leaders[0]
    XCTAssertEqual(leader.id, "id-original")
    XCTAssertEqual(leader.label, "ChordLab A")
    XCTAssertEqual(leader.lineage, "lineage-id-original")
    XCTAssertEqual(leader.lanes.map(\.lane), ["chords", "uid-2", "uid-3"])
    XCTAssertEqual(leader.lanes[0], LinkLaneJSON(lane: "chords", name: "Chords", role: "", chords: true, audible: true,
                                                 followers: 0))
    XCTAssertEqual(leader.lanes[1].followers, 1)
    XCTAssertFalse(leader.lanes[2].audible)
    XCTAssertEqual(state.routed, [LinkRoutedJSON(lane: "chords", followers: 1, trackId: 0),
                                  LinkRoutedJSON(lane: "uid-3", followers: 1, trackId: 2)])
    XCTAssertFalse(state.isFollowing)
  }

  func testFollowerFixture() throws {
    let state = try Self.fixture("link-state-follower.json")
    XCTAssertEqual(state.instance, LinkSelfJSON(id: "uid-8", label: "", copyOf: nil))  // a follower has no label
    XCTAssertEqual(state.following, LinkFollowingJSON(leader: "id-leader", leaderLabel: "ChordLab A", lane: "uid-2",
                                                      laneName: "Bass", role: "bass", status: .playing,
                                                      switchTo: LinkInstanceJSON(leader: "uid-6", label: "ChordLab B")))
    XCTAssertEqual(state.leaders.map(\.id), ["id-leader", "uid-6"])
    XCTAssertEqual(state.routed, [])
    XCTAssertTrue(state.isFollowing)
    XCTAssertEqual(state.leader(id: "uid-6")?.lineage, "lineage-leader-copy-1")
    XCTAssertNil(state.leader(id: "nobody"))
  }

  func testWaitingFixture() throws {
    let state = try Self.fixture("link-state-waiting.json")
    let following = try XCTUnwrap(state.following)
    XCTAssertEqual(following.status, .waiting)
    XCTAssertEqual(following.leader, "id-leader")
    XCTAssertNil(following.switchTo)
    XCTAssertNil(state.leader(id: "id-leader"))                 // the leader is gone
    XCTAssertEqual(state.leaders.map(\.id), ["id-other"])
    XCTAssertEqual(state.leaders[0].lanes[1].trackRole, .other)
  }

  /// A copy nobody plays from: `copyOf` names its original, `routed` is empty.
  func testCopyFixture() throws {
    let state = try Self.fixture("link-state-copy.json")
    XCTAssertEqual(state.instance, LinkSelfJSON(id: "uid-5", label: "ChordLab B",
                                                copyOf: LinkInstanceJSON(leader: "id-original", label: "ChordLab A")))
    XCTAssertNil(state.following)
    XCTAssertEqual(state.routed, [])
    XCTAssertEqual(state.leaders.map(\.id), ["id-original"])
    XCTAssertEqual(state.leaders[0].lanes.map(\.followers), [0, 1, 0])
  }

  func testTheLeaderFixtureIsNoCopyNow() throws {
    XCTAssertNil(try Self.fixture("link-state-leader.json").instance.copyOf)
  }

  // MARK: - Statuses

  func testEveryStatusString() throws {
    let cases: [(String, LinkStatus)] = [("playing", .playing), ("muted", .muted), ("waiting", .waiting),
                                         ("projectChanged", .projectChanged), ("laneMissing", .laneMissing)]
    for (text, status) in cases {
      XCTAssertEqual(try decode(following(status: text)).following?.status, status, text)
    }
  }

  func testUnknownStatusStringsDecodeAsUnknown() throws {
    for text in ["paused", "", "Playing", "notFollowing", "lane_missing"] {
      XCTAssertEqual(try decode(following(status: text)).following?.status, .unknown, text)
    }
  }

  func testAStatusThatIsNotAStringStillFailsTheDecode() {
    let json = following(status: "x").replacingOccurrences(of: "\"status\":\"x\"", with: "\"status\":3")
    XCTAssertThrowsError(try decode(json))
  }

  // MARK: - Older builds and absent keys

  func testAnOlderBuildWithoutCopyOfAndTrackIdDecodes() throws {
    let state = try decode(Self.older)
    XCTAssertNil(state.instance.copyOf)
    XCTAssertEqual(state.routed, [LinkRoutedJSON(lane: "uid-1", followers: 1, trackId: nil)])
  }

  func testCopyOfDecodesAndNullIsNil() throws {
    let copy = """
      {"schema":1,"self":{"id":"me","label":"ChordLab B","copyOf":{"leader":"a","label":"ChordLab A"}},
       "following":null,"leaders":[],"routed":[]}
      """
    XCTAssertEqual(try decode(copy).instance.copyOf, LinkInstanceJSON(leader: "a", label: "ChordLab A"))
    let none = copy.replacingOccurrences(of: #"{"leader":"a","label":"ChordLab A"}"#, with: "null")
    XCTAssertNil(try decode(none).instance.copyOf)
  }

  func testAFollowingWithoutSwitchToDecodes() throws {
    let json = following(status: "playing").replacingOccurrences(of: ",\"switchTo\":null", with: "")
    XCTAssertNil(try decode(json).following?.switchTo)
  }

  func testExtraKeysAreIgnored() throws {
    let json = Self.older.replacingOccurrences(of: "\"schema\":1,", with: "\"schema\":1,\"summary\":\"x\",")
    XCTAssertEqual(try decode(json).instance.id, "me")
  }

  // MARK: - Roles

  func testLaneRoles() {
    let chords = LinkLaneJSON(lane: "chords", name: "Chords", role: "", chords: true, audible: true, followers: 0)
    XCTAssertNil(chords.trackRole)
    XCTAssertEqual(LinkLaneJSON(lane: "u", name: "", role: "bass", chords: false, audible: true, followers: 0).trackRole,
                   .bass)
    XCTAssertEqual(LinkLaneJSON(lane: "u", name: "", role: "lead", chords: false, audible: true, followers: 0).trackRole,
                   .lead)
    XCTAssertEqual(LinkLaneJSON(lane: "u", name: "", role: "drums", chords: false, audible: true, followers: 0).trackRole,
                   .other)
  }

  func testFollowingRoleIsNilForTheChordsLaneAndForAnEmptyHint() {
    func f(lane: String, role: String) -> LinkFollowingJSON {
      LinkFollowingJSON(leader: "a", leaderLabel: "", lane: lane, laneName: "", role: role, status: .playing,
                        switchTo: nil)
    }
    XCTAssertNil(f(lane: "chords", role: "bass").trackRole)
    XCTAssertNil(f(lane: "uid", role: "").trackRole)
    XCTAssertEqual(f(lane: "uid", role: "lead").trackRole, .lead)
    XCTAssertEqual(f(lane: "uid", role: "new").trackRole, .other)
    XCTAssertTrue(f(lane: "chords", role: "").isChords)
  }

  // MARK: - RoutedMarks

  private func tabs(trackIds: [Int]) -> [LaneTab] {
    [LaneTab(lane: .chords, label: "Chords", role: nil, muted: false, soloed: false, audible: true, hasMuteSolo: true,
             isOpen: true)]
      + trackIds.map {
        LaneTab(lane: .track($0), label: "T\($0)", role: .bass, muted: false, soloed: false, audible: true,
                hasMuteSolo: true, isOpen: false)
      }
  }

  private func state(routed: [LinkRoutedJSON]) -> LinkStateJSON {
    LinkStateJSON(schema: 1, instance: LinkSelfJSON(id: "me", label: "ChordLab A", copyOf: nil), following: nil,
                  leaders: [], routed: routed)
  }

  func testTheLeaderFixtureMarksTheChordsTabAndTrackTwo() throws {
    let marks = RoutedMarks.marks(try Self.fixture("link-state-leader.json"), tabs: tabs(trackIds: [1, 2]))
    XCTAssertEqual(marks[.chords], RoutedMark(followers: 1, tooltip: "Not played here: another ChordLab plays it on its own instrument."))
    XCTAssertEqual(marks[.track(2)], RoutedMark(followers: 1, tooltip: "Not played here: another ChordLab plays it on its own instrument."))
    XCTAssertNil(marks[.track(1)])
    XCTAssertEqual(marks.count, 2)
  }

  func testTooltipCountsTheTracks() {
    XCTAssertEqual(RoutedMarks.tooltip(followers: 1), "Not played here: another ChordLab plays it on its own instrument.")
    XCTAssertEqual(RoutedMarks.tooltip(followers: 2), "Not played here: 2 other ChordLabs play it on their own instruments.")
    XCTAssertEqual(RoutedMarks.tooltip(followers: 5), "Not played here: 5 other ChordLabs play it on their own instruments.")
  }

  func testAnEntryWithoutTrackIdMarksNothing() throws {
    XCTAssertEqual(RoutedMarks.marks(try decode(Self.older), tabs: tabs(trackIds: [1])), [:])
  }

  func testATrackIdOfZeroOrOfATrackWithoutATabMarksNothing() {
    let s = state(routed: [LinkRoutedJSON(lane: "uid-9", followers: 1, trackId: 0),
                           LinkRoutedJSON(lane: "uid-7", followers: 2, trackId: 7)])
    XCTAssertEqual(RoutedMarks.marks(s, tabs: tabs(trackIds: [1, 2])), [:])
  }

  func testTheChordsLaneIsMarkedByItsKeyNotByTrackId() {
    let withoutId = state(routed: [LinkRoutedJSON(lane: "chords", followers: 2, trackId: nil)])
    XCTAssertEqual(RoutedMarks.marks(withoutId, tabs: tabs(trackIds: [])),
                   [.chords: RoutedMark(followers: 2, tooltip: "Not played here: 2 other ChordLabs play it on their own instruments.")])
  }

  func testNoFollowersAndNoLinkStateMarkNothing() {
    XCTAssertEqual(RoutedMarks.marks(state(routed: [LinkRoutedJSON(lane: "u", followers: 0, trackId: 1)]),
                                     tabs: tabs(trackIds: [1])), [:])
    XCTAssertEqual(RoutedMarks.marks(nil, tabs: tabs(trackIds: [1])), [:])
  }

  func testAFollowerMarksNothingEvenWithRoutedEntries() {
    let follower = LinkStateJSON(
      schema: 1, instance: LinkSelfJSON(id: "me", label: "ChordLab B", copyOf: nil),
      following: LinkFollowingJSON(leader: "a", leaderLabel: "ChordLab A", lane: "chords", laneName: "", role: "",
                                   status: .playing, switchTo: nil),
      leaders: [], routed: [LinkRoutedJSON(lane: "chords", followers: 1, trackId: 0)])
    XCTAssertEqual(RoutedMarks.marks(follower, tabs: tabs(trackIds: [])), [:])
  }
}
