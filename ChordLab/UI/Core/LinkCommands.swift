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
/// an error; the shell answers it with a fresh link state and nothing else, inside the send). `ChordLabState` keeps
/// one: `sent` before the request goes out, `received` with every decoded link state. The attempt is judged once, on
/// the first link state after the request (the shell answers inside the send, so that one is the answer); later link
/// states are not judged again. A refusal's `notice` stays (in the picker or the banner) until the next choice
/// (`sent`), until the user clears it (`dismiss`), or until a later link state shows that what this instance follows
/// has changed since the refusal (it started or stopped following, or follows another lane: the sentence is about a
/// choice that no longer stands). A refused Change… in the follower's panel leaves the previous follow in place, so
/// the answer itself still follows something and still shows the sentence.
struct FollowAttempt: Equatable {
  /// The request that has not been judged yet; nil when there is none.
  private(set) var pending: FollowRequest?
  /// The refusal text, until cleared; nil when there is none.
  private(set) var notice: String?
  /// What this instance followed (leader id and lane key) in the answer that refused, nil for nothing.
  private var followedAtRefusal: Followed?

  /// A followed lane by identity: the leader's id and the lane's key.
  private struct Followed: Equatable {
    let leader: String
    let lane: String
  }

  /// The one sentence a refusal shows: "ChordLab A changed while you were choosing. Pick again."; with no label,
  /// "The other ChordLab".
  static func refusalText(leaderLabel: String) -> String {
    "\(leaderLabel.isEmpty ? "The other ChordLab" : leaderLabel) changed while you were choosing. Pick again."
  }

  /// A follow request is about to be sent: it is the one the next link state judges, and the old notice goes.
  mutating func sent(_ request: FollowRequest) {
    pending = request
    notice = nil
  }

  /// The request could not be sent (the message was not valid JSON): nothing will answer it.
  mutating func notSent() {
    pending = nil
  }

  /// The user cleared the notice.
  mutating func dismiss() {
    notice = nil
    pending = nil
  }

  /// A link state arrived: the verdict on the pending request, or nil when none was pending (it is then not an
  /// answer, and it clears the notice when what this instance follows differs from the refusing answer's).
  @discardableResult mutating func received(_ state: LinkStateJSON) -> FollowVerdict? {
    let followed = state.following.map { Followed(leader: $0.leader, lane: $0.lane) }
    guard let request = pending else {
      if notice != nil, followed != followedAtRefusal { notice = nil }
      return nil
    }
    pending = nil
    if followed == Followed(leader: request.leader, lane: request.lane) {
      notice = nil
      return .applied
    }
    notice = Self.refusalText(leaderLabel: request.leaderLabel)
    followedAtRefusal = followed
    return .refused
  }
}
