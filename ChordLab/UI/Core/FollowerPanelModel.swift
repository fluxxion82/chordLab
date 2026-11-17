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

/// One of the follower panel's two actions.
enum FollowerRepair: Equatable {
  /// Open the picker (Change…).
  case change
  /// Stop following.
  case stopFollowing
}

/// An action with its button title.
struct FollowerRepairButton: Equatable {
  let repair: FollowerRepair
  let title: String
}

/// What the panel's switch button does (plan I7: a follower duplicated together with its leader).
enum FollowerSwitchAction: Equatable {
  /// Send this follow (`option.command`; `FollowRequest(option)` for `FollowAttempt`).
  case follow(FollowOption)
  /// No single lane of the copy matches: the view draws the button as a menu over the copy's entries,
  /// `FollowOptions.group(leader:in:)` with this leader id (the copy is always offered when this case is given).
  case choose(leader: String)

  /// The lane a `.follow` sends; nil for `.choose`.
  var target: FollowTarget? {
    if case .follow(let option) = self { return option.id }
    return nil
  }
}

/// How the status line reads: `fine` while the lane plays or is only muted in the other ChordLab, `attention` when
/// something must be repaired (waiting, the track deleted, another project, an unknown status): the view draws it in
/// the notice's orange.
enum FollowerStatusTone: Equatable {
  case fine
  case attention
}

/// The panel's extra line and button when the copy of the leader could be played instead (`following.switchTo`).
struct FollowerSwitchOffer: Equatable {
  let line: String
  let button: String
  let action: FollowerSwitchAction
}

/// Everything the follower's window shows (plan section 5, the read-only panel), as plain values, from the link
/// state and the instance's own snapshot: the headline, the status line, the two actions, the switch to a copy, and
/// the notes. The view draws it and sends what it says; it draws `actions` and no other Change… or Stop following.
///
/// The headline says "Plays …" while the lane plays or is only muted in the other ChordLab, and "Set to play …" while
/// something must be repaired (waiting, deleted, another project, an unknown status); `statusTone` tells the view to
/// draw those status lines in the notice's orange.
///
/// Names: the followed track's name and role come from the leader's current lanes when it lists the lane (a rename in
/// the leader shows at once), else from the follow's stored hints; an empty track name is its role's name (the lane
/// tab row's rule). The other ChordLab's label is `following.leaderLabel` (the bound leader's current label, the stored
/// one while waiting), else the listed leader's. Missing names read "a track" / "the track" and "another ChordLab"
/// (in a possessive, "the other ChordLab"). Labels are given by load order, so while waiting a stored label may now
/// name a different instance: when a listed leader carries it, the sentences say "the other ChordLab" instead.
///
/// The switch (`switchTo`, plan I7): a copy loads its original's lane table and document id, so it lists the same
/// lane uids. The button follows, in this order, the copy's lane with the followed lane's uid; else the one lane of
/// the copy with the same name and role (role ignored while unknown); else, when there is none or several,
/// `.choose` (a menu over the copy's entries). The chords lane is the copy's chords lane. A copy the picker does not
/// offer (it has published nothing yet) gives no switch offer: there is nothing to open. The button names the lane it
/// follows (bounded, `FollowLabels`), and for `.choose` no track at all ("Play a track of ChordLab C instead"): the
/// copy may have none of that name.
///
/// Nothing here mentions auditions: they are not routed to a follower yet (plan Task 9).
struct FollowerPanelModel: Equatable {
  static let changeTitle = "Change…"
  static let stopTitle = "Stop following"
  /// The Live routing reminder (plan section 2): in the recommended set-up the follower is a chain of the main
  /// ChordLab's Instrument Rack, so the instrument picks it by its chain's name.
  static let routingReminder = "On the instrument's track, set MIDI From to the track this ChordLab is on, choose this "
    + "ChordLab below it (in a rack, by its chain's name), and set Monitor to In."
  /// The instance's own document is kept while it follows (plan I1); no confirmation is asked, this line says it.
  static let keptText = "This ChordLab's own chords and tracks are kept. They come back when you stop following."

  let headline: [TextRun]
  let role: TrackRole?
  let status: LinkStatus
  let statusLine: String
  let statusTone: FollowerStatusTone
  /// Change (titled for the status) and Stop following, each exactly once, in that order, in every status.
  let actions: [FollowerRepairButton]
  /// The action the status asks for (drawn prominent): change while waiting, the track is missing, the project
  /// changed or the status is unknown; none while playing or muted.
  let emphasised: FollowerRepair?
  let switchOffer: FollowerSwitchOffer?
  /// "Edit in ChordLab A's window."; nil while waiting (it is not there to edit in).
  let editNote: String?
  /// `keptText` when this instance holds chords or tracks of its own; nil when it was empty or its snapshot has not
  /// arrived (nothing to promise).
  let keptNote: String?

  /// The headline as one plain string.
  var headlineText: String { headline.map(\.text).joined() }

  /// The role's colour mark (`TrackStyle`); nil for the chords lane and an unknown role.
  var roleColor: Color? { role.map(TrackStyle.color(role:)) }

  /// The panel for `state`, with `snapshot` the instance's own and `ownLineage` its document lineage as the panel is
  /// drawn (the switch's follow carries it, `FollowOption.ownLineage`); nil when this instance does not follow.
  init?(_ state: LinkStateJSON?, snapshot: Snapshot?, ownLineage: String) {
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
    let waiting = following.status == .waiting
    let staleLabel = waiting && !following.leaderLabel.isEmpty
      && state.leaders.contains { $0.label == following.leaderLabel && $0.id != following.leader }
    let label = staleLabel ? nil : [following.leaderLabel, listedLeader?.label ?? ""].first { !$0.isEmpty }
    let leader = label ?? (staleLabel ? "the other ChordLab" : "another ChordLab")

    self.role = role
    self.status = following.status
    let leaderRun = label.map { TextRun($0, bold: true) } ?? TextRun(leader)
    // "Plays" only while it does (or is merely muted there); otherwise what it is set to play.
    let verb: String
    switch following.status {
    case .playing, .muted: verb = "Plays"
    case .waiting, .laneMissing, .projectChanged, .unknown: verb = "Set to play"
    }
    if following.isChords {
      headline = Self.merged([TextRun("\(verb) the "), TextRun("chords", bold: true), TextRun(" of "), leaderRun])
    } else if let name {
      headline = Self.merged([TextRun("\(verb) "), TextRun(name, bold: true), TextRun(" of "), leaderRun])
    } else {
      headline = Self.merged([TextRun("\(verb) a track of "), leaderRun])
    }

    let subject: (noun: String, isAre: String, wasWere: String) =
      following.isChords ? ("The chords", "are", "were") : (name.map { ($0, "is", "was") } ?? ("The track", "is", "was"))
    switch following.status {
    case .playing:
      statusLine = "Playing."
    case .muted:
      let soloed = following.isChords ? "a track" : "another track"
      statusLine = "\(subject.noun) \(subject.isAre) muted in \(leader) (or \(soloed) is soloed)."
    case .waiting:
      statusLine = "Waiting for \(leader). It is not in this set, or it now plays from another ChordLab."
    case .laneMissing:
      statusLine = "\(subject.noun) \(subject.wasWere) deleted in \(leader). Undo there brings it back."
    case .projectChanged:
      statusLine = "\(Self.capitalized(leader)) holds a different project now."
    case .unknown:
      statusLine = "Not playing."
    }
    let changeTitle = following.status == .projectChanged ? "Pick a track…" : Self.changeTitle
    actions = [FollowerRepairButton(repair: .change, title: changeTitle),
               FollowerRepairButton(repair: .stopFollowing, title: Self.stopTitle)]
    switch following.status {
    case .playing, .muted:
      emphasised = nil
      statusTone = .fine
    case .waiting, .laneMissing, .projectChanged, .unknown:
      emphasised = .change
      statusTone = .attention
    }
    editNote = waiting ? nil : "Edit in \(label ?? "the other ChordLab")'s window."
    if let snapshot, !FollowOffer.isFresh(snapshot) {
      keptNote = Self.keptText
    } else {
      keptNote = nil
    }

    if let copy = following.switchTo,
       let group = FollowOptions.group(leader: copy.leader,
                                       in: FollowOptions.groups(state, ownLineage: ownLineage)) {
      let copyName = copy.label.isEmpty ? nil : copy.label
      let line = "\(copyName ?? "Another ChordLab") was copied together with this one, but this one still plays from "
        + "\(leader)."
      let action = Self.switchAction(group: group, following: following, name: name, role: role)
      // The button names what it plays: the matched lane (its name bounded, `FollowLabels`), or, when it opens the
      // copy's lanes, no track at all (the copy may not have one of that name).
      let what: String
      switch action {
      case .follow(let option): what = following.isChords ? "the chords" : option.label
      case .choose: what = "a track"
      }
      let button = "Play \(what) of \(copyName ?? "the copy") instead"
      switchOffer = FollowerSwitchOffer(line: line, button: button, action: action)
    } else {
      switchOffer = nil
    }
  }

  /// The switch button's action: the copy's lane with the same uid, else the one with the same name and role, else
  /// a menu over the copy's entries.
  private static func switchAction(group: FollowGroup, following: LinkFollowingJSON, name: String?,
                                   role: TrackRole?) -> FollowerSwitchAction {
    if let same = group.options.first(where: { $0.lane == following.lane }) { return .follow(same) }
    guard !following.isChords, let name else { return .choose(leader: group.leader) }
    let matches = group.options.filter { $0.role != nil && $0.name == name && (role == nil || $0.role == role) }
    return matches.count == 1 ? .follow(matches[0]) : .choose(leader: group.leader)
  }

  /// Adjacent plain runs joined, so a fallback headline ends in one run.
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
