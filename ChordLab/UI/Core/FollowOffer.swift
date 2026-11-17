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
/// - the user has not dismissed it for this document (`FollowDismissals`, by document lineage, for the life of the
///   process; a new document may offer again). Stopping following counts as a dismissal
///   (`dismissedLineage(afterStopFollowing:)`), so the banner does not come back at once.
///
/// The wording never says "on this track": in the recommended set-up the instance is a chain of the main ChordLab's
/// Instrument Rack, not a Live track of its own, and a copy plays everything until told to play one track.
///
/// A copy is named first and gets its own wording; a `copyOf` that names a leader the picker does not offer (it has
/// published nothing yet) is ignored, and the fresh rule may still apply. The buttons are the first group's entries in
/// the picker's order, at most `maxButtons`, each labelled with its bounded name (`FollowOption.label`); the view shows
/// as many of them, in order, as fit on the banner's line, and "More…" (the picker) shows when there are more lanes or
/// more leaders than it shows (`showsMore(shown:)`).
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
    case (false, false): return "Make this ChordLab play one track of \(leaderName):"
    case (false, true): return "Play from \(leaderName):"
    case (true, false): return "This ChordLab is a copy of \(leaderName). Make it play only one of its tracks:"
    case (true, true): return "A copy of \(leaderName). Play only:"
    }
  }

  /// The dismissal the state records when the user stops following, in the document of lineage `lineage`: the
  /// instance is no longer following and may be fresh, and the banner must not come back at once. It is the same
  /// per-document dismissal the banner's own button records (`FollowDismissals`, which `make` consults); a new document may offer
  /// again.
  static func dismissedLineage(afterStopFollowing lineage: String) -> String { lineage }

  /// Whether a window of this height takes the compact form.
  static func isCompact(height: CGFloat) -> Bool { height < compactHeight }

  /// Fresh: the snapshot has arrived and holds no chords in the sequence and no tracks. No snapshot yet is not fresh.
  static func isFresh(_ snapshot: Snapshot?) -> Bool {
    guard let snapshot else { return false }
    return snapshot.events.isEmpty && (snapshot.tracks ?? []).isEmpty
  }

  /// The banner for this link state and snapshot in the document of lineage `lineage` (the buttons carry it as their
  /// own lineage), nil when it does not show or the user dismissed it in that document (`dismissals`).
  static func make(link: LinkStateJSON?, snapshot: Snapshot?, lineage: String, dismissals: FollowDismissals) -> FollowOffer? {
    guard let link, !link.isFollowing, link.routed.isEmpty, !dismissals.isDismissed(lineage) else { return nil }
    let groups = FollowOptions.groups(link, ownLineage: lineage)
    guard let first = groups.first else { return nil }
    let isCopy = link.instance.copyOf.map { $0.leader == first.leader } ?? false   // groups put an offered original first
    guard isCopy || isFresh(snapshot) else { return nil }
    let label = first.options.first?.leaderLabel ?? ""
    let buttons = Array(first.options.prefix(maxButtons))
    return FollowOffer(leader: first.leader, isCopy: isCopy, buttons: buttons,
                       showsMore: groups.count > 1 || first.options.count > buttons.count,
                       leaderName: label.isEmpty ? "another ChordLab" : label)
  }

  /// Whether More… shows when the view shows the first `shown` buttons (those that fit on its line): when the picker
  /// holds more than the banner offers, or the view leaves some of them out.
  func showsMore(shown: Int) -> Bool {
    showsMore || shown < buttons.count
  }
}
