import SwiftUI

/// One tab of the lane tab row (tracks design D13): the Chords lane or one track, as the snapshot has it. The
/// row shows `label` (a track tab with its role's colour mark, `TrackStyle`), M and S toggles showing `muted` and
/// `soloed` when `hasMuteSolo`, and the whole tab dimmed while it is not `audible` (the engine's mute/solo
/// outcome, never derived here). `isOpen` marks the lane the editor shows.
struct LaneTab: Equatable, Identifiable {
  let lane: Lane
  let label: String
  /// The track's role; nil for the Chords tab.
  let role: TrackRole?
  let muted: Bool
  let soloed: Bool
  let audible: Bool
  /// Whether the tab has its M and S toggles: always for a track; for Chords only while the snapshot carries
  /// `chordsMuted` and `chordsSolo` (an older engine has neither and would refuse the commands).
  let hasMuteSolo: Bool
  let isOpen: Bool

  var id: Lane { lane }
  /// Drawn dimmed: not audible.
  var dimmed: Bool { !audible }
  /// The track's id; nil for the Chords tab.
  var trackId: Int? {
    if case .track(let id) = lane { return id }
    return nil
  }
}

/// A track tab's context menu (design D13): Rename, Role ▸ with the current role checked, Move left / Move right
/// (each nil, so disabled, at its end of the row), Remove. The Chords tab has none.
struct TrackTabMenu: Equatable {
  let trackId: Int
  /// The stored name (the rename field starts from it).
  let name: String
  /// The current role, checked in the Role submenu.
  let role: TrackRole
  /// The `moveTrack` index of Move left, nil for the first track.
  let moveLeftIndex: Int?
  /// The `moveTrack` index of Move right, nil for the last track.
  let moveRightIndex: Int?

  /// Whether `candidate` is checked in the Role submenu.
  func isChecked(_ candidate: TrackRole) -> Bool { candidate == role }
}

/// One entry of the + menu (design D13): a role, its title, and whether it is enabled.
struct AddTrackEntry: Equatable, Identifiable {
  let role: TrackRole
  let enabled: Bool
  var title: String { role.displayName }
  var id: TrackRole { role }
}

/// The lane tab row's rules (tracks design D13), so the views only read and call this: the tabs a snapshot gives
/// (Chords first, then the tracks in snapshot order), the + menu, the track menu, every command the row sends,
/// the rename rule (the section rename's), and the lane a snapshot leaves open (`laneAfterSnapshot`, `LaneOpener`).
/// The open lane itself is UI state in `ChordLabState.openLane`.
///
/// A snapshot without `tracks` (an engine from before tracks) gives the Chords tab alone, without M/S, and no +:
/// the row keeps its place and height, so the layout does not depend on the engine's age.
enum LaneTabs {
  /// The track limit when the snapshot carries no `trackLimits` (the engine's `kMaxTracks`).
  static let defaultMaxTracks = 4
  /// The engine's bound on a track name (`kMaxTrackNameBytes`, engine/include/chordlab/tracks.h): the rename
  /// field keeps its draft within it, as the section rename does with `kMaxSectionNameBytes`.
  static let maxTrackNameBytes = 64
  /// The Chords tab's label.
  static let chordsLabel = "Chords"

  /// Whether the snapshot knows tracks at all (it carries a `tracks` key, even an empty one).
  static func showsTracks(_ snapshot: Snapshot?) -> Bool { snapshot?.tracks != nil }

  /// The tabs in row order: Chords, then each track in snapshot order. `open` is resolved against the snapshot's
  /// tracks first (`Lane.resolved`), so a lane the snapshot lacks shows Chords open. A track with an empty name
  /// is labelled with its role's name, so its tab never draws blank.
  static func tabs(in snapshot: Snapshot?, open: Lane) -> [LaneTab] {
    let resolved = open.resolved(in: snapshot?.tracks)
    let chordsFlags = snapshot.flatMap { s in s.chordsMuted.flatMap { m in s.chordsSolo.map { (m, $0) } } }
    var tabs = [LaneTab(lane: .chords, label: chordsLabel, role: nil,
                        muted: chordsFlags?.0 ?? false, soloed: chordsFlags?.1 ?? false,
                        audible: snapshot?.chordsAudible ?? true, hasMuteSolo: chordsFlags != nil,
                        isOpen: resolved == .chords)]
    for track in snapshot?.tracks ?? [] {
      tabs.append(LaneTab(lane: .track(track.id), label: label(for: track),
                          role: track.role, muted: track.mute, soloed: track.solo, audible: track.audible,
                          hasMuteSolo: true, isOpen: resolved == .track(track.id)))
    }
    return tabs
  }

  /// What names `track` in the row and the roll area: its name, or its role's name while the name is empty, so a
  /// track never shows blank.
  static func label(for track: TrackJSON) -> String {
    track.name.isEmpty ? track.role.displayName : track.name
  }

  // MARK: - The + menu

  /// The snapshot's `trackLimits.maxTracks`, else `defaultMaxTracks`.
  static func maxTracks(_ snapshot: Snapshot?) -> Int { snapshot?.trackLimits?.maxTracks ?? defaultMaxTracks }

  /// Whether + can add a track: the snapshot knows tracks and holds fewer than the limit.
  static func canAddTrack(_ snapshot: Snapshot?) -> Bool {
    guard let tracks = snapshot?.tracks else { return false }
    return tracks.count < maxTracks(snapshot)
  }

  /// Bass, Lead, Other, all enabled below the limit and all disabled at it.
  static func addEntries(_ snapshot: Snapshot?) -> [AddTrackEntry] {
    let enabled = canAddTrack(snapshot)
    return TrackRole.allCases.map { AddTrackEntry(role: $0, enabled: enabled) }
  }

  /// `addTrack {role}`: no name, so the engine gives the role's default (design D2: Bass, Lead, Track, with " 2"
  /// and so on for a taken one).
  static func addCommand(role: TrackRole) -> [String: Any] {
    ["cmd": "addTrack", "role": role.rawValue]
  }

  // MARK: - The track menu

  /// The context menu of `lane`'s tab: nil for Chords and for a track the snapshot does not have.
  static func menu(for lane: Lane, in snapshot: Snapshot?) -> TrackTabMenu? {
    guard case .track(let id) = lane, let tracks = snapshot?.tracks,
          let index = tracks.firstIndex(where: { $0.id == id }) else { return nil }
    let track = tracks[index]
    return TrackTabMenu(trackId: id, name: track.name, role: track.role,
                        moveLeftIndex: index > 0 ? index - 1 : nil,
                        moveRightIndex: index < tracks.count - 1 ? index + 1 : nil)
  }

  /// `moveTrack {id, index}`: `index` is the track's place in the order after the move (the engine's rule).
  static func moveCommand(trackId: Int, index: Int) -> [String: Any] {
    ["cmd": "moveTrack", "id": trackId, "index": index]
  }

  /// `removeTrack {id}`: no confirmation; one undo step brings it back (design D13).
  static func removeCommand(trackId: Int) -> [String: Any] {
    ["cmd": "removeTrack", "id": trackId]
  }

  /// `patchTrack {id, role}` for a role other than `current`; nil for the checked one (nothing to change).
  static func roleCommand(trackId: Int, role: TrackRole, current: TrackRole) -> [String: Any]? {
    role == current ? nil : ["cmd": "patchTrack", "id": trackId, "role": role.rawValue]
  }

  // MARK: - Rename

  /// `name` cut to `maxTrackNameBytes` of UTF-8 at a character boundary (`HarmonyText.boundedSectionName`, the
  /// section rename's cut), as the engine itself cuts a long name.
  static func boundedName(_ name: String) -> String {
    HarmonyText.boundedSectionName(name, maxBytes: maxTrackNameBytes)
  }

  /// The section rename's rule (`SectionsHeaderView.commitRename`): the draft trimmed of whitespace and newlines
  /// and bounded; `patchTrack {id, name}` when that is non-empty and differs from `current`, else nil (a blank or
  /// unchanged draft is a cancel).
  static func renameCommand(trackId: Int, draft: String, current: String) -> [String: Any]? {
    let name = boundedName(draft.trimmingCharacters(in: .whitespacesAndNewlines))
    guard !name.isEmpty, name != current else { return nil }
    return ["cmd": "patchTrack", "id": trackId, "name": name]
  }

  // MARK: - Mute and solo

  /// The M toggle's command: `setChordsMute {on}` for Chords, `setTrackMute {id, on}` for a track.
  static func muteCommand(lane: Lane, on: Bool) -> [String: Any] {
    switch lane {
    case .chords: return ["cmd": "setChordsMute", "on": on]
    case .track(let id): return ["cmd": "setTrackMute", "id": id, "on": on]
    }
  }

  /// The S toggle's command: `setChordsSolo {on}` for Chords, `setTrackSolo {id, on}` for a track.
  static func soloCommand(lane: Lane, on: Bool) -> [String: Any] {
    switch lane {
    case .chords: return ["cmd": "setChordsSolo", "on": on]
    case .track(let id): return ["cmd": "setTrackSolo", "id": id, "on": on]
    }
  }

  // MARK: - After an add

  /// The track to open once an `addTrack` is answered: the one id in `after` (the next snapshot's track ids) that
  /// is not in `before` (the ids when the add was sent); nil when there is none (the add was refused, or this
  /// snapshot is not its answer) or more than one (ambiguous: open nothing).
  static func newTrackId(before: [Int], after: [Int]) -> Int? {
    let old = Set(before)
    let new = after.filter { !old.contains($0) }
    return new.count == 1 ? new[0] : nil
  }

  /// The lane to show once a snapshot with `tracks` is applied, `open` being the lane shown before it and
  /// `pendingAdd` the track ids when an unanswered `addTrack` was sent (nil when none is): the one new track when
  /// this snapshot answers the add with exactly one (`newTrackId`), else `open` resolved against `tracks`
  /// (`Lane.resolved`: an open track the snapshot lacks, or any track lane without `tracks`, falls back to Chords).
  static func laneAfterSnapshot(open: Lane, pendingAdd: [Int]?, tracks: [TrackJSON]?) -> Lane {
    if let before = pendingAdd, let after = tracks?.map(\.id), let id = newTrackId(before: before, after: after) {
      return .track(id)
    }
    return open.resolved(in: tracks)
  }
}

/// Which track an add opens, over time (tracks design D13): `addSent` remembers the track ids when an `addTrack`
/// goes out; the next snapshot decides the lane (`LaneTabs.laneAfterSnapshot`) and forgets the add, answered or
/// not; a refusal (`kMsgTagError`) forgets it at once, so a track that arrives later is never opened as the add's.
/// `ChordLabState` keeps one and sets its `openLane` from what it returns.
struct LaneOpener: Equatable {
  /// The track ids when the unanswered add was sent; nil when there is none.
  private(set) var pendingAdd: [Int]?

  mutating func addSent(trackIds: [Int]) { pendingAdd = trackIds }

  /// The add (or whatever command was pending) was refused: the next snapshot opens nothing new.
  mutating func refused() { pendingAdd = nil }

  /// The lane once a snapshot with `tracks` is applied while `open` is shown; the pending add, if any, is spent.
  mutating func lane(afterSnapshotWith tracks: [TrackJSON]?, open: Lane) -> Lane {
    defer { pendingAdd = nil }
    return LaneTabs.laneAfterSnapshot(open: open, pendingAdd: pendingAdd, tracks: tracks)
  }
}

/// The track colours by role (tracks design D2: the role sets the colour): the tab's colour mark now, the roll's
/// notes later (D14). Chosen to read on the dark editor background (`PaletteView.background` #0B1240) at full
/// strength and as velocity-scaled fills, and to stay apart from the guide bands (cool #6E8FD8, warm #D9904A,
/// `GuideStyle`) and from the section and function palettes (`SectionStyle`, `FunctionStyle`): a light lime for
/// Bass, a rose for Lead, a lavender for Other.
enum TrackStyle {
  static let bass = Color(red: 0.561, green: 0.820, blue: 0.310)    // #8FD14F lime
  static let lead = Color(red: 0.941, green: 0.482, blue: 0.682)    // #F07BAE rose
  static let other = Color(red: 0.725, green: 0.639, blue: 0.941)   // #B9A3F0 lavender

  static func color(role: TrackRole) -> Color {
    switch role {
    case .bass: return bass
    case .lead: return lead
    case .other: return other
    }
  }
}
