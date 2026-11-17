import CoreGraphics
import Foundation

/// The Follow banner (plan section 2 and section 5, "Picking"): when it shows and what it says, so `FollowBannerView`
/// only draws it. It shows only when all of these hold:
/// - this instance is not following;
/// - no other track plays from this instance (`routed` is empty): following would silence those tracks, so the banner
///   never asks (the Follow menu stays, as the user's explicit choice);
/// - the picker has options (`FollowOptions`; none in an AU, the app or a lone instance, which is not an error);
/// - the instance is a **copy** of a leader the picker offers (`self.copyOf`; a duplicated rack chain, plan I13), or it
///   is **fresh**: its snapshot has arrived and holds no chords in the sequence and no tracks;
/// - the user has not dismissed it for this document (`dismissedLineage`, the document lineage the user dismissed it
///   in; a new document may offer again).
///
/// A copy is named first and gets its own wording; a `copyOf` that names a leader the picker does not offer (it has
/// published nothing yet) is ignored, and the fresh rule may still apply. The buttons are the first group's entries in
/// the picker's order, at most `maxButtons`; "More…" (the picker) shows when there are more lanes or more leaders.
struct FollowOffer: Equatable {
  static let maxButtons = 5
  static let moreTitle = "More…"
  static let dismissTitle = "Not now"
  /// The editor's short-window threshold (`ContentView.compactHeight`): below it the banner takes its one-line form.
  static let compactHeight: CGFloat = 700

  /// The leader whose lanes the buttons offer (the first group).
  let leader: String
  /// Whether the banner speaks of a copy.
  let isCopy: Bool
  let buttons: [FollowOption]
  let showsMore: Bool
  /// The leader's label as the sentence uses it ("ChordLab A", or "another ChordLab").
  private let leaderName: String

  /// The sentence before the buttons, in full or in the one-line compact form.
  func text(compact: Bool) -> String {
    switch (isCopy, compact) {
    case (false, false): return "Play a track of \(leaderName) on this track:"
    case (false, true): return "Play a track of \(leaderName) here:"
    case (true, false): return "This ChordLab is a copy of \(leaderName). Play one of its tracks on this track:"
    case (true, true): return "A copy of \(leaderName). Play one of its tracks here:"
    }
  }

  /// Whether a window of this height takes the compact form.
  static func isCompact(height: CGFloat) -> Bool { height < compactHeight }

  /// Fresh: the snapshot has arrived and holds no chords in the sequence and no tracks. No snapshot yet is not fresh.
  static func isFresh(_ snapshot: Snapshot?) -> Bool {
    guard let snapshot else { return false }
    return snapshot.events.isEmpty && (snapshot.tracks ?? []).isEmpty
  }

  /// The banner for this link state and snapshot in the document of lineage `lineage`, nil when it does not show.
  static func make(link: LinkStateJSON?, snapshot: Snapshot?, lineage: String, dismissedLineage: String?) -> FollowOffer? {
    guard let link, !link.isFollowing, link.routed.isEmpty, dismissedLineage != lineage else { return nil }
    let groups = FollowOptions.groups(link)
    guard let first = groups.first else { return nil }
    let isCopy = link.instance.copyOf.map { $0.leader == first.leader } ?? false   // groups put an offered original first
    guard isCopy || isFresh(snapshot) else { return nil }
    let label = first.options.first?.leaderLabel ?? ""
    return FollowOffer(leader: first.leader, isCopy: isCopy, buttons: Array(first.options.prefix(maxButtons)),
                       showsMore: groups.count > 1 || first.options.count > maxButtons,
                       leaderName: label.isEmpty ? "another ChordLab" : label)
  }
}
