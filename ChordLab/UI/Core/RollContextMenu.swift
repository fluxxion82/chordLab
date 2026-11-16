import CoreGraphics
import Foundation

/// The roll's context menu (plan Task 20): Copy, Cut, Paste here, Duplicate, Delete, for a right-click or ⌃-click at a
/// content point of one track's grid. The view builds the `NSMenu` from this model and runs a chosen item through
/// `commands(for:snapshot:timing:pasteboard:)`; everything decided here is pure and tested.
///
/// Decisions (the design names the items only):
/// - **What a right-click selects** (common practice: Finder, Logic, Live): on a note that is not selected, that note
///   alone becomes the selection first (`select`, sent when the menu is built, before it opens) and the items act on
///   it; on a selected note, or on empty space, the selection stays and the items act on it. Modifiers are ignored.
///   Inactive (dashed) notes count like any other.
/// - **The targets are fixed when the menu opens** (`targetIds`), and each item reads the snapshot current when it is
///   chosen (its revision, which of the targets still exist), so a selection echo that has not arrived yet, or a
///   commit in between, never makes an item act on other notes.
/// - **Paste here** lands at the clicked tick floored to the track's grid (bar-relative, as an insert), and is
///   enabled only with ChordLab notes on the pasteboard and the click inside the progression (before its end; the
///   hatched area after it owns no tick). It carries no `baseRevision`, like ⌘V, and moves no insert marker.
/// - Copy, Cut, Duplicate and Delete are enabled with at least one target; they act as ⌘C, ⌘X (the removal only after
///   a checked write), ⌘D and Delete do (`NoteKeys`).
///
/// Depends on nothing but Foundation, CoreGraphics and Core's snapshot, geometry, grid and note operations;
/// unit-tested by the `ChordLabUICore` package (Tests/ChordLabUICoreTests/RollContextMenuTests.swift).
struct RollContextMenu: Equatable {
  enum Item: CaseIterable {
    case copy, cut, pasteHere, duplicate, delete

    /// The menu title.
    var title: String {
      switch self {
      case .copy: return "Copy"
      case .cut: return "Cut"
      case .pasteHere: return "Paste here"
      case .duplicate: return "Duplicate"
      case .delete: return "Delete"
      }
    }
  }

  let trackId: Int
  /// The notes Copy, Cut, Duplicate and Delete act on, in note order (empty: those items are disabled).
  let targetIds: [Int]
  /// Sent when the menu opens: the right-clicked unselected note becomes the selection; nil otherwise.
  let select: RollCommand?
  /// Where Paste here lands; nil when the click is at or after the progression's end.
  let pasteTick: Int?
  /// Whether the pasteboard held ChordLab notes when the menu was built.
  let notesOnPasteboard: Bool

  /// Whether `item` is enabled.
  func isEnabled(_ item: Item) -> Bool {
    switch item {
    case .copy, .cut, .duplicate, .delete: return !targetIds.isEmpty
    case .pasteHere: return notesOnPasteboard && pasteTick != nil
    }
  }

  /// The menu for a right-click at content `point` on track `trackId` (geometry and grid the track's), nil for a track
  /// the snapshot lacks.
  static func make(at point: CGPoint, trackId: Int, snapshot: Snapshot, timing: RollTiming, geometry: RollGeometry,
                   grid: RollGrid, notesOnPasteboard: Bool) -> RollContextMenu? {
    guard let track = snapshot.track(id: trackId) else { return nil }
    let selected = NoteKeys.selectedIds(trackId: trackId, snapshot: snapshot)
    var targets = selected
    var select: RollCommand?
    if let hit = RollHit.hit(at: point, notes: track.notes, geometry: geometry), !selected.contains(hit.id) {
      targets = [hit.id]
      select = .selectNotes(trackId: trackId, ids: [hit.id])
    }
    let tick = geometry.tick(atX: point.x)
    let pasteTick = tick < timing.endTick
      ? grid.snapDown(tick, barTicks: timing.barTicks, ticksPerBeat: timing.ticksPerBeat) : nil
    return RollContextMenu(trackId: trackId, targetIds: targets, select: select, pasteTick: pasteTick,
                           notesOnPasteboard: notesOnPasteboard)
  }

  /// The track's edit state once the menu has opened: a menu that selects the right-clicked note clears the insert
  /// marker, as a note press does (M2 "As built"), so a ⌘V afterwards pastes after that note rather than at an old
  /// marker; a menu that keeps the selection keeps the marker.
  func editState(afterOpening state: RollEditState) -> RollEditState {
    guard select != nil else { return state }
    var next = state
    next.insertMarker = nil
    return next
  }

  /// What choosing `item` sends against the CURRENT `snapshot` (zero or one command); Copy writes `pasteboard` and
  /// sends nothing. A disabled Paste here sends nothing.
  func commands(for item: Item, snapshot: Snapshot, timing: RollTiming, pasteboard: ChordPasteboard) -> [RollCommand] {
    let result: RollCommand?
    switch item {
    case .copy:
      NoteKeys.copy(ids: targetIds, trackId: trackId, snapshot: snapshot, pasteboard: pasteboard)
      result = nil
    case .cut:
      result = NoteKeys.cut(ids: targetIds, trackId: trackId, snapshot: snapshot, pasteboard: pasteboard)
    case .pasteHere:
      guard let pasteTick else { return [] }
      result = NoteKeys.paste(at: pasteTick, trackId: trackId, snapshot: snapshot, timing: timing, pasteboard: pasteboard)
    case .duplicate:
      result = NoteKeys.duplicate(ids: targetIds, trackId: trackId, snapshot: snapshot, timing: timing)
    case .delete:
      result = NoteKeys.delete(ids: targetIds, trackId: trackId, snapshot: snapshot)
    }
    return result.map { [$0] } ?? []
  }
}

/// One context-menu click as `RollInputNSView` knows it (Task 20 review): its event type, timestamp and event number,
/// so the view asks for the menu once per click whether AppKit asks (`menu(for:)`) and then delivers the same click to
/// `rightMouseDown` or a ⌃-`mouseDown`, possibly as another `NSEvent` object, or does not ask at all.
struct RollMenuClick: Equatable {
  let type: UInt
  let timestamp: TimeInterval
  let eventNumber: Int

  /// Whether `other` is this click.
  func isSame(as other: RollMenuClick?) -> Bool { other == self }
}
