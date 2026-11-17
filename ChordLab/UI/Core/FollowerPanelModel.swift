import SwiftUI

/// A piece of a line, bold or not: the panel's headline names the track and the other ChordLab in bold. Built as runs
/// rather than Markdown, so a track called "*Bass*" is drawn as typed.
struct TextRun: Equatable {
  let text: String
  let bold: Bool

  init(_ text: String, bold: Bool = false) {
    self.text = text
    self.bold = bold
  }
}

/// A repair the follower's panel offers next to its status.
enum FollowerRepair: Equatable {
  /// Open the picker (Change…).
  case change
  /// Stop following.
  case stopFollowing
}

/// A repair with its button title.
struct FollowerRepairButton: Equatable {
  let repair: FollowerRepair
  let title: String
}

/// What the panel's switch button does (plan I7: a follower that is itself a copy, whose leader has a copy too).
enum FollowerSwitchAction: Equatable {
  /// Send this follow (`option.command`; `FollowRequest(option)` for `FollowAttempt`).
  case follow(FollowOption)
  /// Open the picker on the leader with this id: no single lane of the copy matches.
  case choose(leader: String)

  /// The lane a `.follow` sends; nil for `.choose`.
  var target: FollowTarget? {
    if case .follow(let option) = self { return option.id }
    return nil
  }
}

/// The panel's extra line and button when the copy of the leader could be played instead (`following.switchTo`).
struct FollowerSwitchOffer: Equatable {
  let line: String
  let button: String
  let action: FollowerSwitchAction
}

/// Everything the follower's window shows (plan section 5, the read-only panel), as plain values, from the link
/// state: the headline, the status line and its repairs, the switch to a copy, and the fixed notes. A pure function
/// of `following` (and `leaders`, for the current names and labels); the view draws it and sends what it says.
///
/// Names: the followed track's name and role come from the leader's current lanes when it lists the lane (a rename in
/// the leader shows at once), else from the follow's stored hints; an empty track name is its role's name (the lane
/// tab row's rule). The other ChordLab's label is `following.leaderLabel`, else the listed leader's. Any of them may be
/// missing: "a track" / "the track", and "another ChordLab" (in a possessive, "the other ChordLab").
///
/// The switch (`switchTo`, plan I7): a copy loads its original's lane table and document id, so it lists the same
/// lane uids. The button follows, in this order, the copy's lane with the followed lane's uid; else the one lane of
/// the copy with the same name and role (role ignored while unknown); else, when there is none or several, it opens
/// the picker on the copy. The chords lane is the copy's chords lane. A copy that is not offered (it has published
/// nothing yet, `FollowOptions`) opens the picker too.
///
/// Nothing here mentions auditions: they are not routed to a follower yet (plan Task 9).
struct FollowerPanelModel: Equatable {
  static let changeTitle = "Change…"
  static let stopTitle = "Stop following"
  /// The Live routing reminder (plan section 2).
  static let routingReminder =
    "On the instrument's track choose MIDI From: this track, then this ChordLab, and set Monitor to In."
  /// The instance's own document is kept while it follows (plan I1); no confirmation is asked, this line says it.
  static let keptNote = "This ChordLab's own chords and tracks are kept. They come back when you stop following."

  let headline: [TextRun]
  let role: TrackRole?
  let status: LinkStatus
  let statusLine: String
  let repairs: [FollowerRepairButton]
  let switchOffer: FollowerSwitchOffer?
  /// "Edit in ChordLab A's window."
  let editNote: String

  /// The headline as one plain string.
  var headlineText: String { headline.map(\.text).joined() }

  /// The role's colour mark (`TrackStyle`); nil for the chords lane and an unknown role.
  var roleColor: Color? { role.map(TrackStyle.color(role:)) }

  /// The panel for `state`; nil when this instance does not follow.
  init?(_ state: LinkStateJSON?) {
    guard let state, let following = state.following else { return nil }
    let listedLeader = state.leader(id: following.leader)
    let current = listedLeader?.lanes.first { $0.lane == following.lane }
    let role = following.isChords ? nil : (current?.trackRole ?? following.trackRole)
    let name: String? = {
      if following.isChords { return nil }
      if let current { return FollowOptions.displayName(current) }
      if !following.laneName.isEmpty { return following.laneName }
      return role?.displayName
    }()
    let label = [following.leaderLabel, listedLeader?.label ?? ""].first { !$0.isEmpty }

    self.role = role
    self.status = following.status
    let leaderRun = label.map { TextRun($0, bold: true) } ?? TextRun("another ChordLab")
    if following.isChords {
      headline = Self.merged([TextRun("Plays the "), TextRun("chords", bold: true), TextRun(" of "), leaderRun])
    } else if let name {
      headline = Self.merged([TextRun("Plays "), TextRun(name, bold: true), TextRun(" of "), leaderRun])
    } else {
      headline = Self.merged([TextRun("Plays a track of "), leaderRun])
    }

    let leader = label ?? "another ChordLab"
    let subject: (noun: String, isAre: String, wasWere: String) =
      following.isChords ? ("The chords", "are", "were") : (name.map { ($0, "is", "was") } ?? ("The track", "is", "was"))
    switch following.status {
    case .playing:
      statusLine = "Playing."
      repairs = []
    case .muted:
      let soloed = following.isChords ? "a track" : "another track"
      statusLine = "\(subject.noun) \(subject.isAre) muted in \(leader) (or \(soloed) is soloed)."
      repairs = []
    case .waiting:
      statusLine = "Waiting for \(leader). It is not in this set right now."
      repairs = [FollowerRepairButton(repair: .change, title: Self.changeTitle),
                 FollowerRepairButton(repair: .stopFollowing, title: Self.stopTitle)]
    case .laneMissing:
      statusLine = "\(subject.noun) \(subject.wasWere) deleted in \(leader). Undo there brings it back."
      repairs = [FollowerRepairButton(repair: .change, title: Self.changeTitle)]
    case .projectChanged:
      statusLine = "\(Self.capitalized(leader)) holds a different project now."
      repairs = [FollowerRepairButton(repair: .change, title: "Pick a track…")]
    case .unknown:
      statusLine = "Not playing."
      repairs = [FollowerRepairButton(repair: .change, title: Self.changeTitle)]
    }
    editNote = "Edit in \(label ?? "the other ChordLab")'s window."

    if let copy = following.switchTo {
      let copyName = copy.label.isEmpty ? nil : copy.label
      let line = copyName.map { "\($0) is a copy of \(leader)." } ?? "There is a copy of \(leader) in this set."
      let what = following.isChords ? "the chords" : (name ?? "the same track")
      let button = "Play \(what) of \(copyName ?? "the copy") instead"
      let action = Self.switchAction(state: state, following: following, copy: copy.leader, name: name, role: role)
      switchOffer = FollowerSwitchOffer(line: line, button: button, action: action)
    } else {
      switchOffer = nil
    }
  }

  /// The switch button's action: the copy's lane with the same uid, else the one with the same name and role, else
  /// the picker on the copy.
  private static func switchAction(state: LinkStateJSON, following: LinkFollowingJSON, copy: String, name: String?,
                                   role: TrackRole?) -> FollowerSwitchAction {
    guard let group = FollowOptions.group(leader: copy, in: FollowOptions.groups(state)) else {
      return .choose(leader: copy)
    }
    if let same = group.options.first(where: { $0.lane == following.lane }) { return .follow(same) }
    guard !following.isChords, let name else { return .choose(leader: copy) }
    let matches = group.options.filter { $0.role != nil && $0.name == name && (role == nil || $0.role == role) }
    return matches.count == 1 ? .follow(matches[0]) : .choose(leader: copy)
  }

  /// Adjacent plain runs joined, so a fallback headline is one run.
  private static func merged(_ runs: [TextRun]) -> [TextRun] {
    var out: [TextRun] = []
    for run in runs {
      if let last = out.last, !last.bold, !run.bold {
        out[out.count - 1] = TextRun(last.text + run.text)
      } else {
        out.append(run)
      }
    }
    return out
  }

  /// `text` with its first letter upper-cased ("another ChordLab" at the start of a sentence).
  private static func capitalized(_ text: String) -> String {
    text.prefix(1).uppercased() + text.dropFirst()
  }
}
