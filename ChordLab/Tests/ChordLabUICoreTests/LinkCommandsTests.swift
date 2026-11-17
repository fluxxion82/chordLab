import Foundation
import XCTest
@testable import ChordLabUICore

/// The link requests (UI/Core/LinkCommands.swift) against the grammar of ChordLab/bridge/link_request.h, whose
/// accepted texts are in ChordLab/bridge/tests/test_link_request.cpp.
final class LinkCommandsTests: XCTestCase {
  /// What `ChordLabState` sends: the fields stamped with the own lineage (`CommandLineage.stamp`), plus `schema`
  /// (`ChordLabState.send(_:tag:)`), through `JSONSerialization`; read back as an object.
  private func wire(_ fields: [String: Any], lineage: String = "own-token") throws -> NSDictionary {
    var message = CommandLineage.stamp(fields, token: lineage)
    message["schema"] = 1
    XCTAssertTrue(JSONSerialization.isValidJSONObject(message))
    let data = try JSONSerialization.data(withJSONObject: message)
    return try XCTUnwrap(try JSONSerialization.jsonObject(with: data) as? NSDictionary)
  }

  private func object(_ text: String) throws -> NSDictionary {
    try XCTUnwrap(try JSONSerialization.jsonObject(with: Data(text.utf8)) as? NSDictionary)
  }

  func testFollowFieldsAreExactlyWhatTheBridgeReads() {
    let fields = LinkCommands.follow(leader: "leader-1", lane: "uid-7", leaderLineage: "lineage-3")
    XCTAssertEqual(fields as NSDictionary,
                   ["op": "follow", "leader": "leader-1", "lane": "uid-7", "leaderLineage": "lineage-3"] as NSDictionary)
  }

  func testUnfollowFieldsAreTheOpAlone() {
    XCTAssertEqual(LinkCommands.unfollow() as NSDictionary, ["op": "unfollow"] as NSDictionary)
  }

  /// test_link_request.cpp: `follow("leader-1", "uid-7", "lineage-3")` and the chords lane, accepted; plus the own
  /// lineage the shell reads separately (readLineage), which the parser ignores.
  func testAFollowRoundTripMatchesTheBridgeTestsAcceptedText() throws {
    let sent = try wire(LinkCommands.follow(leader: "leader-1", lane: "uid-7", leaderLineage: "lineage-3"))
    XCTAssertEqual(sent, try object(
      #"{"schema":1,"op":"follow","leader":"leader-1","lane":"uid-7","leaderLineage":"lineage-3","lineage":"own-token"}"#))
    let chords = try wire(LinkCommands.follow(leader: "leader-1", lane: "chords", leaderLineage: "l"), lineage: "t")
    XCTAssertEqual(chords, try object(
      #"{"schema":1,"op":"follow","leader":"leader-1","lane":"chords","leaderLineage":"l","lineage":"t"}"#))
    XCTAssertEqual(sent["schema"] as? Int, 1)                      // an integer, as boundedIntField requires
  }

  func testAnUnfollowRoundTripMatchesTheBridgeTestsAcceptedText() throws {
    XCTAssertEqual(try wire(LinkCommands.unfollow()), try object(#"{"schema":1,"op":"unfollow","lineage":"own-token"}"#))
  }

  func testTheOwnLineageDoesNotReplaceTheLeadersLineage() throws {
    let sent = try wire(LinkCommands.follow(leader: "x", lane: "u", leaderLineage: "leader-token"), lineage: "mine")
    XCTAssertEqual(sent["leaderLineage"] as? String, "leader-token")
    XCTAssertEqual(sent["lineage"] as? String, "mine")
  }

  func testIdentifiersAreSendableWithinOneToSixtyFourBytes() {
    XCTAssertTrue(LinkCommands.isSendable("a"))
    XCTAssertTrue(LinkCommands.isSendable(String(repeating: "a", count: 64)))
    XCTAssertFalse(LinkCommands.isSendable(""))
    XCTAssertFalse(LinkCommands.isSendable(String(repeating: "a", count: 65)))
    XCTAssertTrue(LinkCommands.isSendable(String(repeating: "é", count: 32)))     // 64 bytes of UTF-8
    XCTAssertFalse(LinkCommands.isSendable(String(repeating: "é", count: 32) + "a"))
  }

  // MARK: - FollowAttempt

  private let bass = FollowRequest(leader: "a", lane: "uid-bass", leaderLabel: "ChordLab A")

  private func state(following leader: String?, lane: String = "uid-bass") -> LinkStateJSON {
    LinkStateJSON(
      schema: 1, instance: LinkSelfJSON(id: "me", label: "ChordLab B", copyOf: nil),
      following: leader.map {
        LinkFollowingJSON(leader: $0, leaderLabel: "ChordLab A", lane: lane, laneName: "Bass", role: "bass",
                          status: .playing, switchTo: nil)
      },
      leaders: [], routed: [])
  }

  func testAFollowThatTookEffectIsApplied() {
    var attempt = FollowAttempt()
    attempt.sent(bass)
    XCTAssertEqual(attempt.pending, bass)
    XCTAssertEqual(attempt.received(state(following: "a")), .applied)
    XCTAssertNil(attempt.pending)
    XCTAssertNil(attempt.notice)
  }

  func testAnAnswerThatStillFollowsNothingIsARefusal() {
    var attempt = FollowAttempt()
    attempt.sent(bass)
    XCTAssertEqual(attempt.received(state(following: nil)), .refused)
    XCTAssertNil(attempt.pending)
    XCTAssertEqual(attempt.notice, "ChordLab A changed while you were choosing. Pick again.")
  }

  func testAnotherLaneOfTheSameLeaderIsARefusal() {
    var attempt = FollowAttempt()
    attempt.sent(bass)
    XCTAssertEqual(attempt.received(state(following: "a", lane: "uid-lead")), .refused)
  }

  func testTheSameLaneUidOfAnotherLeaderIsARefusal() {
    var attempt = FollowAttempt()
    attempt.sent(bass)
    XCTAssertEqual(attempt.received(state(following: "b")), .refused)
  }

  /// A stale own lineage: the shell answers with a snapshot and the link state, nothing applied, so the previous
  /// follow is still there; the same text.
  func testAStaleOwnLineageLeavesThePreviousFollowAndIsARefusal() {
    var attempt = FollowAttempt()
    attempt.sent(FollowRequest(leader: "a", lane: "uid-lead", leaderLabel: "ChordLab A"))
    XCTAssertEqual(attempt.received(state(following: "a", lane: "uid-bass")), .refused)
    XCTAssertEqual(attempt.notice, "ChordLab A changed while you were choosing. Pick again.")
  }

  func testAnEmptyLabelNamesTheOtherChordLab() {
    var attempt = FollowAttempt()
    attempt.sent(FollowRequest(leader: "a", lane: "chords", leaderLabel: ""))
    attempt.received(state(following: nil))
    XCTAssertEqual(attempt.notice, "The other ChordLab changed while you were choosing. Pick again.")
  }

  func testALinkStateBeforeAnyRequestJudgesNothing() {
    var attempt = FollowAttempt()
    XCTAssertNil(attempt.received(state(following: nil)))
    XCTAssertNil(attempt.notice)
  }

  /// The shell answers inside the send, so the first link state after the request is the answer; one that arrives
  /// later, for another reason (an idle push, another instance's change), is not judged again.
  func testTheAttemptIsJudgedOnceOnTheFirstLinkStateAfterTheRequest() {
    var attempt = FollowAttempt()
    attempt.sent(bass)
    XCTAssertEqual(attempt.received(state(following: nil)), .refused)
    XCTAssertNil(attempt.received(state(following: nil)))
    XCTAssertEqual(attempt.notice, "ChordLab A changed while you were choosing. Pick again.")
  }

  func testAnAppliedAttemptIsNotJudgedAgainWhenALaterStateFollowsNothing() {
    var attempt = FollowAttempt()
    attempt.sent(bass)
    XCTAssertEqual(attempt.received(state(following: "a")), .applied)
    XCTAssertNil(attempt.received(state(following: nil)))         // stopped following later: no refusal text
    XCTAssertNil(attempt.notice)
  }

  func testTheNextChoiceClearsTheNotice() {
    var attempt = FollowAttempt()
    attempt.sent(bass)
    attempt.received(state(following: nil))
    attempt.sent(FollowRequest(leader: "a", lane: "chords", leaderLabel: "ChordLab A"))
    XCTAssertNil(attempt.notice)
  }

  func testTheUserClearsTheNotice() {
    var attempt = FollowAttempt()
    attempt.sent(bass)
    attempt.received(state(following: nil))
    attempt.dismiss()
    XCTAssertNil(attempt.notice)
    XCTAssertNil(attempt.pending)
  }

  func testStartingToFollowByAnyRouteClearsTheNotice() {
    var attempt = FollowAttempt()
    attempt.sent(bass)
    attempt.received(state(following: nil))
    XCTAssertNil(attempt.received(state(following: "a", lane: "chords")))   // e.g. a state load made it follow
    XCTAssertNil(attempt.notice)
  }

  func testStoppingToFollowAfterARefusedChangeClearsTheNotice() {
    var attempt = FollowAttempt()
    attempt.sent(FollowRequest(leader: "a", lane: "uid-lead", leaderLabel: "ChordLab A"))
    attempt.received(state(following: "a", lane: "uid-bass"))
    XCTAssertNil(attempt.received(state(following: "a", lane: "uid-bass")))  // an idle push, nothing changed
    XCTAssertNotNil(attempt.notice)
    XCTAssertNil(attempt.received(state(following: nil)))
    XCTAssertNil(attempt.notice)
  }

  func testARequestThatCouldNotBeSentIsNotJudged() {
    var attempt = FollowAttempt()
    attempt.sent(bass)
    attempt.notSent()
    XCTAssertNil(attempt.pending)
    XCTAssertNil(attempt.received(state(following: nil)))
    XCTAssertNil(attempt.notice)
  }
}
