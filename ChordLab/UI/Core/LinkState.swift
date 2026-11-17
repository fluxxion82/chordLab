import Foundation

/// Where a following instance stands (`kMsgTagLinkState`'s `following.status`, `LinkRegistry` rule 7 in
/// engine/include/chordlab/link.h): `playing` (the lane is in the leader's current document and audible), `muted`
/// (in the document, not audible: muted, or another lane soloed), `waiting` (no live leading instance holds the
/// leader's id: deleted, not loaded yet, or following itself), `projectChanged` (bound, but the leader holds another
/// document or has published none yet), `laneMissing` (same document, the lane is gone: deleted, undo brings it back).
///
/// Decoding is tolerant: a status this UI does not know (a newer plug-in's) is `.unknown`, so a new status never
/// loses the whole link state. Encoding writes the wire name (`unknown` for `.unknown`).
enum LinkStatus: String, Codable, Equatable, CaseIterable {
  case playing, muted, waiting, projectChanged, laneMissing, unknown

  init(from decoder: Decoder) throws {
    let text = try decoder.singleValueContainer().decode(String.self)
    self = LinkStatus(rawValue: text) ?? .unknown
  }

  func encode(to encoder: Encoder) throws {
    var container = encoder.singleValueContainer()
    try container.encode(rawValue)
  }
}

/// Another instance named for display: `self.copyOf` (the instance this one is a copy of) and
/// `following.switchTo` (a copy of the leader this one follows). `leader` is its persistent id, `label` its display
/// label ("ChordLab A"; display only, never identity).
struct LinkInstanceJSON: Codable, Equatable {
  let leader: String
  let label: String
}

/// The link state's `self`: this instance's id and label, and `copyOf`, the instance this one is a copy of while
/// this one is not following and the original lives and leads (`LinkView::copyOf`, registry rule 11); nil (JSON
/// null, or absent from a build before Task 10a) otherwise.
struct LinkSelfJSON: Codable, Equatable {
  let id: String
  let label: String
  let copyOf: LinkInstanceJSON?
}

/// The link state's `following`: what this instance follows. `leader` is the bound leader's current id (the stored
/// id while waiting) and `lane` is `"chords"` or a lane uid of that leader. `leaderLabel`, `laneName` and `role` are
/// the bound leader's label and the follow's stored hints: any of them may be empty, and names shown to the user
/// come from the leader's current lanes where it lists them (`leaders`), these only as the fallback. `switchTo`
/// names a copy of the leader that this instance could follow instead (registry rule 8); nil (null or absent)
/// otherwise.
struct LinkFollowingJSON: Codable, Equatable {
  let leader: String
  let leaderLabel: String
  let lane: String
  let laneName: String
  let role: String
  let status: LinkStatus
  let switchTo: LinkInstanceJSON?

  /// Whether the followed lane is the chords lane.
  var isChords: Bool { lane == LinkStateJSON.chordsLane }

  /// The followed track's role from the stored hint: nil for the chords lane and for an empty hint; a role this UI
  /// does not know is `.other`, as `TrackRole` decodes one.
  var trackRole: TrackRole? {
    guard !isChords, !role.isEmpty else { return nil }
    return TrackRole(rawValue: role) ?? .other
  }
}

/// One lane of another instance as the link state lists it (`leaders[].lanes[]`): `lane` is `"chords"` or the
/// lane's uid (its identity), `name` the track's name (`"Chords"` for the chords lane; a track's may be empty),
/// `role` the role's wire name (empty for the chords lane), `audible` the leader's mute and solo outcome and
/// `followers` how many instances follow it now.
struct LinkLaneJSON: Codable, Equatable {
  let lane: String
  let name: String
  let role: String
  let chords: Bool
  let audible: Bool
  let followers: Int

  /// The track's role: nil for the chords lane; a role this UI does not know (or an empty one) is `.other`.
  var trackRole: TrackRole? {
    chords ? nil : (TrackRole(rawValue: role) ?? .other)
  }
}

/// Another instance that leads (`leaders[]`): its id (identity), its label (display only), the lineage of the
/// document its lanes come from (a follow request carries it; empty while it has published nothing) and its lanes,
/// the chords lane first, then its tracks in its own order (empty while it has published nothing).
struct LinkLeaderJSON: Codable, Equatable {
  let id: String
  let label: String
  let lineage: String
  let lanes: [LinkLaneJSON]
}

/// One of this instance's own lanes that other instances follow (`routed[]`): the lane (`"chords"` or its uid), how
/// many follow it, and `trackId`, the number of this instance's track that the lane is (0 for the chords lane and
/// for a uid the instance's table does not hold; absent from a build before Task 10a).
struct LinkRoutedJSON: Codable, Equatable {
  let lane: String
  let followers: Int
  let trackId: Int?
}

/// `kMsgTagLinkState` payload (per-track instruments plan I12, `linkStateJson` in engine/include/chordlab/json_api.h):
/// who this instance is (`self`, decoded as `instance`), what it follows (`following`, nil when nothing), the other
/// instances it could follow (`leaders`: every other live instance that is not following, in the order they were
/// added; empty in an AU, the app, or a set with one ChordLab, which is not an error) and which of its own lanes
/// others follow (`routed`). Identity is always an instance id and a lane uid (or `"chords"`), never a label, a name
/// or a position. A key a newer build adds is ignored; one an older build lacks (`copyOf`, `switchTo`, `trackId`)
/// decodes as nil.
struct LinkStateJSON: Codable, Equatable {
  /// The lane key of the chords lane, in `following.lane`, `lanes[].lane` and `routed[].lane`.
  static let chordsLane = "chords"

  let schema: Int
  let instance: LinkSelfJSON
  let following: LinkFollowingJSON?
  let leaders: [LinkLeaderJSON]
  let routed: [LinkRoutedJSON]

  enum CodingKeys: String, CodingKey {
    case schema
    case instance = "self"
    case following, leaders, routed
  }

  /// Whether this instance follows a lane (its window is the follower's panel).
  var isFollowing: Bool { following != nil }

  /// The leader with this id, nil when it is not listed.
  func leader(id: String) -> LinkLeaderJSON? { leaders.first { $0.id == id } }
}

/// The mark on a followed lane's tab (plan section 5, "In the leader"): how many other tracks play the lane, and the
/// tab's tooltip.
struct RoutedMark: Equatable {
  let followers: Int
  let tooltip: String
}

/// Which lane tabs carry the mark of a lane that plays on its own track (`routed`), so `LaneTabsView` only draws it.
/// The chords lane is found by its key (`"chords"`), a track by `routed[].trackId` (the snapshot's tracks carry their
/// numbers, not their lane uids); an entry without `trackId` (an older build), with `trackId` 0 on a track lane (a uid
/// the instance's table does not hold) or with no tab of that number marks nothing, and nothing is ever matched by a
/// name or a position. A following instance marks nothing (its window is the panel).
enum RoutedMarks {
  /// The tooltip for `followers` tracks: "Plays on its own track", "Plays on 2 tracks of its own".
  static func tooltip(followers: Int) -> String {
    followers == 1 ? "Plays on its own track" : "Plays on \(followers) tracks of its own"
  }

  /// The marked tabs of `tabs`, by lane.
  static func marks(_ state: LinkStateJSON?, tabs: [LaneTab]) -> [Lane: RoutedMark] {
    guard let state, !state.isFollowing else { return [:] }
    let lanes = Set(tabs.map(\.lane))
    var marks: [Lane: RoutedMark] = [:]
    for entry in state.routed where entry.followers > 0 {
      let lane: Lane
      if entry.lane == LinkStateJSON.chordsLane {
        lane = .chords
      } else if let trackId = entry.trackId, trackId > 0 {
        lane = .track(trackId)
      } else {
        continue
      }
      guard lanes.contains(lane), marks[lane] == nil else { continue }
      marks[lane] = RoutedMark(followers: entry.followers, tooltip: tooltip(followers: entry.followers))
    }
    return marks
  }
}
