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
  }

  /// The serialized text itself, keys sorted: an `NSDictionary` comparison cannot tell `schema: 1` from
  /// `schema: true`, the bridge can (boundedIntField wants an integer).
  func testTheSerializedTextCarriesSchemaAsTheInteger1() throws {
    for (fields, expected) in [
      (LinkCommands.follow(leader: "leader-1", lane: "uid-7", leaderLineage: "lineage-3"),
       #"{"lane":"uid-7","leader":"leader-1","leaderLineage":"lineage-3","lineage":"own-token","op":"follow","schema":1}"#),
      (LinkCommands.unfollow(), #"{"lineage":"own-token","op":"unfollow","schema":1}"#),
    ] {
      var message = CommandLineage.stamp(fields, token: "own-token")
      message["schema"] = 1
      let data = try JSONSerialization.data(withJSONObject: message, options: [.sortedKeys])
      XCTAssertEqual(String(decoding: data, as: UTF8.self), expected)
    }
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
  private let refusedByA = "ChordLab A changed while you were choosing. Pick again."

  private func state(following leader: String?, lane: String = "uid-bass") -> LinkStateJSON {
    LinkStateJSON(
      schema: 1, instance: LinkSelfJSON(id: "me", label: "ChordLab B", copyOf: nil),
      following: leader.map {
        LinkFollowingJSON(leader: $0, leaderLabel: "ChordLab A", lane: lane, laneName: "Bass", role: "bass",
                          status: .playing, switchTo: nil)
      },
      leaders: [], routed: [])
  }

  /// One send as `ChordLabState` makes it: `sent`, then whatever the shell answers inside the send, then
  /// `sendReturned`. Returns the verdict the answers gave.
  @discardableResult
  private func send(_ attempt: FollowAttempt, _ request: FollowRequest,
                    answers: (FollowAttempt) -> FollowVerdict?) -> FollowVerdict? {
    attempt.sent(request)
    let verdict = answers(attempt)
    attempt.sendReturned()
    return verdict
  }

  func testAFollowThatTookEffectIsApplied() {
    let attempt = FollowAttempt()
    attempt.sent(bass)
    XCTAssertEqual(attempt.pending, bass)
    XCTAssertEqual(attempt.received(state(following: "a")), .applied)
    attempt.sendReturned()
    XCTAssertNil(attempt.pending)
    XCTAssertNil(attempt.notice)
  }

  func testAnAnswerThatStillFollowsNothingIsARefusal() {
    let attempt = FollowAttempt()
    XCTAssertEqual(send(attempt, bass) { $0.received(self.state(following: nil)) }, .refused)
    XCTAssertNil(attempt.pending)
    XCTAssertEqual(attempt.notice, refusedByA)
  }

  func testAnotherLaneOfTheSameLeaderIsARefusal() {
    XCTAssertEqual(send(FollowAttempt(), bass) { $0.received(self.state(following: "a", lane: "uid-lead")) }, .refused)
  }

  func testTheSameLaneUidOfAnotherLeaderIsARefusal() {
    XCTAssertEqual(send(FollowAttempt(), bass) { $0.received(self.state(following: "b")) }, .refused)
  }

  /// A stale own lineage: the shell answers with a snapshot (a new token) and the link state, nothing applied. The
  /// state passes `ownDocumentChanged`: the sentence names this ChordLab, not the other one.
  func testAStaleOwnDocumentNamesThisChordLab() {
    let attempt = FollowAttempt()
    let lead = FollowRequest(leader: "a", lane: "uid-lead", leaderLabel: "ChordLab A")
    let verdict = send(attempt, lead) { $0.received(self.state(following: "a", lane: "uid-bass"), ownDocumentChanged: true) }
    XCTAssertEqual(verdict, .refused)
    XCTAssertEqual(attempt.notice, "This ChordLab changed while you were choosing. Pick again.")
  }

  func testAnEmptyLabelNamesTheOtherChordLab() {
    let attempt = FollowAttempt()
    send(attempt, FollowRequest(leader: "a", lane: "chords", leaderLabel: "")) { $0.received(self.state(following: nil)) }
    XCTAssertEqual(attempt.notice, "The other ChordLab changed while you were choosing. Pick again.")
  }

  func testALinkStateOutsideAnySendIsNotJudged() {
    let attempt = FollowAttempt()
    XCTAssertNil(attempt.received(state(following: nil)))
    XCTAssertNil(attempt.notice)
    attempt.sent(bass)
    attempt.sendReturned()                                          // no answer inside the send
    XCTAssertNil(attempt.received(state(following: nil)))           // a later push, for another reason
    XCTAssertNil(attempt.notice)
  }

  func testNoAnswerInsideTheSendLeavesNothingPending() {
    let attempt = FollowAttempt()
    send(attempt, bass) { _ in nil }
    XCTAssertNil(attempt.pending)
    XCTAssertNil(attempt.notice)
  }

  /// A message the shell cannot parse is answered with an error and no link state: the error is the answer, and
  /// the next link state, whenever it comes, is not judged.
  func testAnErrorAloneThenALaterLinkStateGivesNoVerdictAndNoNotice() {
    let attempt = FollowAttempt()
    attempt.sent(bass)
    XCTAssertTrue(attempt.receivedError())
    attempt.sendReturned()
    XCTAssertNil(attempt.pending)
    XCTAssertNil(attempt.received(state(following: nil)))
    XCTAssertNil(attempt.notice)
  }

  /// A malformed or missing own lineage: an error and a link state in the same send. The error is the answer; no
  /// notice blames the other ChordLab.
  func testAnErrorAndALinkStateInOneSendGiveNoNotice() {
    let attempt = FollowAttempt()
    let verdict = send(attempt, bass) { a in
      XCTAssertTrue(a.receivedError())
      return a.received(self.state(following: nil))
    }
    XCTAssertNil(verdict)
    XCTAssertNil(attempt.notice)
  }

  func testAnErrorOutsideASendIsNotTheAttempts() {
    XCTAssertFalse(FollowAttempt().receivedError())
  }

  /// The shell answers inside the send; only the first link state of the send is the answer.
  func testTheAttemptIsJudgedOnceInsideTheSend() {
    let attempt = FollowAttempt()
    send(attempt, bass) { a in
      XCTAssertEqual(a.received(self.state(following: nil)), .refused)
      XCTAssertNil(a.received(self.state(following: "a")))
      return nil
    }
    XCTAssertNil(attempt.received(state(following: nil)))
  }

  func testAnAppliedAttemptLeavesNoNoticeWhenALaterStateFollowsNothing() {
    let attempt = FollowAttempt()
    XCTAssertEqual(send(attempt, bass) { $0.received(self.state(following: "a")) }, .applied)
    XCTAssertNil(attempt.received(state(following: nil)))         // stopped following later: no refusal text
    XCTAssertNil(attempt.notice)
  }

  func testTheNextChoiceClearsTheNotice() {
    let attempt = FollowAttempt()
    send(attempt, bass) { $0.received(self.state(following: nil)) }
    attempt.sent(FollowRequest(leader: "a", lane: "chords", leaderLabel: "ChordLab A"))
    XCTAssertNil(attempt.notice)
  }

  func testTheUserClearsTheNotice() {
    let attempt = FollowAttempt()
    send(attempt, bass) { $0.received(self.state(following: nil)) }
    attempt.dismiss()
    XCTAssertNil(attempt.notice)
  }

  func testStartingToFollowByAnyRouteClearsTheNotice() {
    let attempt = FollowAttempt()
    send(attempt, bass) { $0.received(self.state(following: nil)) }
    XCTAssertNil(attempt.received(state(following: "a", lane: "chords")))   // e.g. a state load made it follow
    XCTAssertNil(attempt.notice)
  }

  func testStoppingToFollowAfterARefusedChangeClearsTheNotice() {
    let attempt = FollowAttempt()
    send(attempt, FollowRequest(leader: "a", lane: "uid-lead", leaderLabel: "ChordLab A")) {
      $0.received(self.state(following: "a", lane: "uid-bass"))
    }
    XCTAssertNil(attempt.received(state(following: "a", lane: "uid-bass")))  // an idle push, nothing changed
    XCTAssertEqual(attempt.notice, refusedByA)
    XCTAssertNil(attempt.received(state(following: nil)))
    XCTAssertNil(attempt.notice)
  }
}
