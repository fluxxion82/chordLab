import Foundation

/// A lane of another instance by identity: the leader's id and the lane's key (`"chords"` or a lane uid). Never a
/// label, a name or a position: two leaders may hold the same uid (a copy keeps its original's lane table), and two
/// tracks may share a name.
struct FollowTarget: Hashable {
  let leader: String
  let lane: String
}

/// One entry of the Follow picker: a lane of a leader, with what a request needs (the leader's id, the lane's key,
/// the leader's lineage as this list was drawn from it) and what the entry shows (`name`, the role's colour,
/// whether others already play it, whether it is the lane this instance follows now).
struct FollowOption: Equatable, Identifiable {
  let leader: String
  let lane: String
  let leaderLineage: String
  /// The leader's label as listed (display only; may be empty), for `FollowAttempt`'s sentence.
  let leaderLabel: String
  /// "Chords" for the chords lane, the track's name, or its role's name when the name is empty (`LaneTabs.label`).
  let name: String
  /// The track's role (its colour, `TrackStyle`); nil for the chords lane.
  let role: TrackRole?
  /// How many instances follow this lane now, this one included when `isCurrent`.
  let followers: Int
  /// Whether this instance follows this lane now.
  let isCurrent: Bool

  var id: FollowTarget { FollowTarget(leader: leader, lane: lane) }

  /// How many other tracks play this lane already: `followers` without this instance.
  var otherTracks: Int { max(0, followers - (isCurrent ? 1 : 0)) }

  /// "(on 1 other track)", "(on 2 other tracks)"; nil when no other track plays it.
  var detail: String? {
    switch otherTracks {
    case 0: return nil
    case 1: return "(on 1 other track)"
    default: return "(on \(otherTracks) other tracks)"
    }
  }

  /// The entry as the Follow menu shows it: the name, "  ✓" on the current lane (as the Length menu checks its
  /// entry), then the detail.
  var menuTitle: String {
    name + (isCurrent ? "  ✓" : "") + (detail.map { "  " + $0 } ?? "")
  }

  /// The request's fields (`LinkCommands.follow`); `ChordLabState` stamps the own lineage.
  var command: [String: Any] {
    LinkCommands.follow(leader: leader, lane: lane, leaderLineage: leaderLineage)
  }
}

extension FollowRequest {
  /// The request `FollowAttempt` remembers for a click on `option`.
  init(_ option: FollowOption) {
    self.init(leader: option.leader, lane: option.lane, leaderLabel: option.leaderLabel)
  }
}

/// One leader's entries in the picker: a section titled with its label.
struct FollowGroup: Equatable, Identifiable {
  let leader: String
  let title: String
  let options: [FollowOption]

  var id: String { leader }
}

/// What the Follow picker (the transport bar's menu, the banner, the follower panel's Change…) offers, from the link
/// state (plan section 5, "Picking"): one group per leader, by id, in the order the link state lists them, except
/// that the leader this instance is a copy of (`self.copyOf`) comes first. Inside a group, the lanes nobody follows
/// first, then the others, each in the leader's own order (chords first, then its tracks).
///
/// A leader whose `lineage` or `lanes` are empty has not published yet and is not offered (a request would carry an
/// empty lineage, which the bridge refuses with an error); nor is an entry whose leader id, lane key or lineage the
/// bridge would refuse (`LinkCommands.isSendable`). No leaders, which is what an AU, the app or a lone instance sees,
/// is no options: the menu and the banner do not appear, and that is not an error.
enum FollowOptions {
  /// The transport bar menu's title.
  static let menuTitle = "Follow"
  /// A group's title when the leader's label is empty.
  static let untitledLeader = "Another ChordLab"

  static func groups(_ state: LinkStateJSON?) -> [FollowGroup] {
    guard let state else { return [] }
    let current = state.following.map { FollowTarget(leader: $0.leader, lane: $0.lane) }
    var groups: [FollowGroup] = []
    for leader in state.leaders {
      guard LinkCommands.isSendable(leader.id), LinkCommands.isSendable(leader.lineage), !leader.lanes.isEmpty else {
        continue
      }
      let options = leader.lanes.filter { LinkCommands.isSendable($0.lane) }.map { lane in
        FollowOption(leader: leader.id, lane: lane.lane, leaderLineage: leader.lineage, leaderLabel: leader.label,
                     name: displayName(lane), role: lane.trackRole, followers: lane.followers,
                     isCurrent: current == FollowTarget(leader: leader.id, lane: lane.lane))
      }
      guard !options.isEmpty else { continue }
      let ordered = options.filter { $0.followers == 0 } + options.filter { $0.followers != 0 }
      groups.append(FollowGroup(leader: leader.id, title: leader.label.isEmpty ? untitledLeader : leader.label,
                                options: ordered))
    }
    if let original = state.instance.copyOf?.leader, let index = groups.firstIndex(where: { $0.leader == original }) {
      groups.insert(groups.remove(at: index), at: 0)
    }
    return groups
  }

  /// The group of the leader with id `leader`, nil when it is not offered.
  static func group(leader: String, in groups: [FollowGroup]) -> FollowGroup? {
    groups.first { $0.leader == leader }
  }

  /// A lane's name as the picker shows it: "Chords" for the chords lane (`LaneTabs.chordsLabel`), else the track's
  /// name, or its role's name while the name is empty (the lane tab row's rule, `LaneTabs.label`).
  static func displayName(_ lane: LinkLaneJSON) -> String {
    if lane.chords { return LaneTabs.chordsLabel }
    return lane.name.isEmpty ? (lane.trackRole ?? .other).displayName : lane.name
  }
}
