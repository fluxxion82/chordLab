import Foundation

/// The fields of a `kMsgTagLink` request (per-track instruments plan I12), exactly as `parseLinkRequest`
/// (ChordLab/bridge/link_request.h) reads them. `ChordLabState` stamps the instance's own document lineage
/// (`CommandLineage.stamp`, mandatory for this message: the shell refuses a link message without one) and its generic
/// sender adds `schema`. Identity is always the leader's id and the lane's key (`"chords"` or a lane uid), never a
/// label, a name or a position.
///
/// Depends on nothing but Foundation; unit-tested by the `ChordLabUICore` package
/// (Tests/ChordLabUICoreTests/LinkCommandsTests.swift).
enum LinkCommands {
  /// The bridge's bound on each of a follow's strings (`chordlab::kMaxLinkIdBytes`, engine/include/chordlab/link_state.h).
  static let maxIdBytes = 64

  /// Follow `lane` of the leader `leader`, as listed in the link state whose `leaderLineage` it carries (the shell
  /// refuses the follow, silently, when that leader's document has changed since).
  static func follow(leader: String, lane: String, leaderLineage: String) -> [String: Any] {
    ["op": "follow", "leader": leader, "lane": lane, "leaderLineage": leaderLineage]
  }

  /// Stop following.
  static func unfollow() -> [String: Any] {
    ["op": "unfollow"]
  }

  /// Whether `value` is a follow string the bridge accepts: 1 to `maxIdBytes` bytes of UTF-8. A request with any
  /// other would be answered with an error rather than a link state, so nothing outside this is ever offered.
  static func isSendable(_ value: String) -> Bool {
    (1...maxIdBytes).contains(value.utf8.count)
  }
}

/// One follow request as `FollowAttempt` remembers it: the leader's id and the lane's key (identity), and the
/// leader's label at the time of the click, for the text.
struct FollowRequest: Equatable {
  let leader: String
  let lane: String
  let leaderLabel: String
}

/// What the answer to a follow request says.
enum FollowVerdict: Equatable {
  /// The link state names the requested leader and lane in `following`.
  case applied
  /// It does not: the shell refused the request (the leader's document changed since the list was drawn, the leader
  /// or the lane is gone, or this instance's own document lineage was stale) and answered with a fresh link state
  /// and no error.
  case refused
}

/// A follow request and the sentence its refusal leaves for the user (plan I9, M2 as built: a refused follow is not
/// an error; the shell answers it with a fresh link state and nothing else). The shell answers a link message inline,
/// inside the send (`ChordLab::OnMessage` runs synchronously on the main thread), so, as `ClipExportExchange` does for
/// exports, only an answer that arrives while the send is in flight belongs to the request; a link state outside a send
/// (an idle push, another instance's change) is never judged. A class, because the answer re-enters during the send:
/// the state calls `received` / `receivedError` from inside the call that sends.
///
/// The sequence `ChordLabState.follow` follows: `sent(request)`; the send; `sendReturned()`. Inside the send, its
/// `applyLinkState` calls `received(_:ownDocumentChanged:)` (true when a snapshot with a new lineage token arrived
/// earlier in the same send: the shell's answer to a stale own lineage) and its `applyError` calls `receivedError()`.
/// Outside a send it calls `received` too (verdict nil), which only clears a notice that no longer applies.
///
/// - The first link state inside the send is the answer: **applied** when `following` names the requested leader and
///   lane, otherwise **refused** with `notice` set. Later link states of the same send are not judged.
/// - An error inside the send is the answer (a message the shell could not parse is answered with an error and no
///   link state; a malformed or missing own lineage with an error and then a link state): no verdict, no notice, and a
///   link state that follows in the same send is not judged. The state shows the error as it shows any.
/// - No answer inside the send: `sendReturned` forgets the request; no verdict, no notice.
/// - The notice stays (in the picker or the banner) until the next choice (`sent`), until the user clears it
///   (`dismiss`), or until a later link state shows that what this instance follows has changed since the refusal
///   (it started or stopped following, or follows another lane). A refused Change… in the follower's panel leaves the
///   previous follow in place, so the answer itself still follows something and still shows the sentence.
final class FollowAttempt {
  /// The request of the send in flight that has not been answered yet; nil otherwise.
  private(set) var pending: FollowRequest?
  /// The refusal text, until cleared; nil when there is none.
  private(set) var notice: String?
  /// True between `sent` and `sendReturned`.
  private var inFlight = false
  /// What this instance followed (leader id and lane key) in the answer that refused, nil for nothing.
  private var followedAtRefusal: Followed?

  /// A followed lane by identity: the leader's id and the lane's key.
  private struct Followed: Equatable {
    let leader: String
    let lane: String
  }

  /// The sentence a refusal shows: "ChordLab A changed while you were choosing. Pick again."; with no label, "The
  /// other ChordLab"; when this instance's own document changed (`ownDocumentChanged`), "This ChordLab".
  static func refusalText(leaderLabel: String, ownDocumentChanged: Bool = false) -> String {
    let who = ownDocumentChanged ? "This ChordLab" : (leaderLabel.isEmpty ? "The other ChordLab" : leaderLabel)
    return "\(who) changed while you were choosing. Pick again."
  }

  /// Right before the follow request is sent: it is the one an answer inside the send judges, and the old notice goes.
  func sent(_ request: FollowRequest) {
    pending = request
    inFlight = true
    notice = nil
  }

  /// Right after the send returned, whether or not anything answered (or the message could not be sent at all): an
  /// unanswered request is forgotten without a notice.
  func sendReturned() {
    pending = nil
    inFlight = false
  }

  /// The user cleared the notice.
  func dismiss() {
    notice = nil
  }

  /// `kMsgTagError`: true when it answers the send in flight (the error is the request's answer, no notice follows);
  /// false outside a send.
  @discardableResult func receivedError() -> Bool {
    guard inFlight else { return false }
    pending = nil
    return true
  }

  /// A link state arrived. Inside a send with an unanswered request: the verdict. Otherwise nil (not an answer), and
  /// the notice is cleared when what this instance follows differs from the refusing answer's.
  @discardableResult func received(_ state: LinkStateJSON, ownDocumentChanged: Bool = false) -> FollowVerdict? {
    let followed = state.following.map { Followed(leader: $0.leader, lane: $0.lane) }
    guard inFlight, let request = pending else {
      if notice != nil, followed != followedAtRefusal { notice = nil }
      return nil
    }
    pending = nil
    if followed == Followed(leader: request.leader, lane: request.lane) {
      notice = nil
      return .applied
    }
    notice = Self.refusalText(leaderLabel: request.leaderLabel, ownDocumentChanged: ownDocumentChanged)
    followedAtRefusal = followed
    return .refused
  }
}
