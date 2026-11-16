import Foundation

/// The keyboard with a track's notes targeted (tracks design D15 "Keyboard", plan Task 20), and the note operations
/// the roll's context menu shares (`RollContextMenu`): Delete / Forward Delete remove the selected notes, ⌘C copies
/// them, ⌘X cuts them (the removal only after a checked pasteboard write), ⌘V pastes, ⌘D duplicates, ⌘A selects every
/// note of the track, ↑/↓ move a semitone (⇧ an octave), ←/→ one grid step (`RollNudge`, the group clamp). Each is at
/// most ONE command (one undo step), and nothing when there is nothing to act on: no selection on the track, no
/// ChordLab notes on the pasteboard, a clamp that leaves nothing to move, everything already selected.
///
/// Decisions:
/// - Delete, ⌘X, ⌘D and the arrows carry the snapshot's revision as `baseRevision` (they act on the notes as shown,
///   like a gesture's commit); a paste carries none (M2 "As built": it names no existing notes).
/// - ⌘V lands at the insert marker, else right after the selection's end, else at tick 0 (`NoteClipboard`).
/// - ⌘D places the copy right after the selection's end (the latest end of the selected notes), the spacing kept:
///   the same place a ⌘C ⌘V lands without a marker, so the two agree; the insert marker never moves a duplicate, and
///   the pasteboard is not touched. A copy that starts at or after the progression's end is the engine's refusal
///   ("no chord owns"), shown like any other.
/// - ⌘A sends `selectNotes` with every note of the track (inactive ones included), nothing when it already is the
///   selection or the track has no notes.
///
/// Depends on nothing but Foundation and Core's snapshot, clipboard and nudge; unit-tested by the `ChordLabUICore`
/// package (Tests/ChordLabUICoreTests/NoteKeysTests.swift).
enum NoteKeys {
  /// The commands `command` sends with track `trackId`'s notes targeted (zero or one). `grid` is the track's grid
  /// (the horizontal nudge step), `insertMarker` the roll's marker (`RollEditState.insertMarker`). Undo, redo and the
  /// suggestion digits are not note commands (`KeyCommand.route`): none.
  static func commands(for command: KeyCommand, trackId: Int, snapshot: Snapshot, timing: RollTiming, grid: RollGrid,
                       insertMarker: Int?, pasteboard: ChordPasteboard) -> [RollCommand] {
    let selected = selectedIds(trackId: trackId, snapshot: snapshot)
    let result: RollCommand?
    switch command {
    case .deleteSelection:
      result = delete(ids: selected, trackId: trackId, snapshot: snapshot)
    case .copy:
      _ = copy(ids: selected, trackId: trackId, snapshot: snapshot, pasteboard: pasteboard)
      result = nil
    case .cut:
      result = cut(ids: selected, trackId: trackId, snapshot: snapshot, pasteboard: pasteboard)
    case .paste:
      guard let notes = snapshot.track(id: trackId)?.notes else { return [] }
      let at = NoteClipboard.pastePosition(marker: insertMarker, notes: notes, selectedIds: Set(selected))
      result = paste(at: at, trackId: trackId, snapshot: snapshot, timing: timing, pasteboard: pasteboard)
    case .duplicate:
      result = duplicate(ids: selected, trackId: trackId, snapshot: snapshot, timing: timing)
    case .selectAll:
      result = selectAll(trackId: trackId, snapshot: snapshot)
    case .nudge(let direction, let octave):
      result = RollNudge.command(direction, octave: octave, trackId: trackId, snapshot: snapshot, timing: timing,
                                 grid: grid)
    case .undo, .redo, .appendSuggestion:
      result = nil
    }
    return result.map { [$0] } ?? []
  }

  /// Track `trackId`'s selected notes' ids in note order (empty when the selection names another track).
  static func selectedIds(trackId: Int, snapshot: Snapshot) -> [Int] {
    RollNudge.selectedNotes(trackId: trackId, snapshot: snapshot).map(\.id)
  }

  /// The ids of `ids` that name a note of the track now, in note order.
  private static func existing(_ ids: [Int], trackId: Int, snapshot: Snapshot) -> [Int] {
    guard let notes = snapshot.track(id: trackId)?.notes else { return [] }
    return notes.indices(ofIds: Set(ids)).map { notes.id[$0] }
  }

  /// `removeNotes` of the notes `ids` names that still exist, with the snapshot's revision; nil for none.
  static func delete(ids: [Int], trackId: Int, snapshot: Snapshot) -> RollCommand? {
    let ids = existing(ids, trackId: trackId, snapshot: snapshot)
    guard !ids.isEmpty else { return nil }
    return .removeNotes(trackId: trackId, ids: ids, baseRevision: snapshot.revision ?? 0)
  }

  /// Writes the notes `ids` names to `pasteboard` (`NoteClipboard.copy`); false, the pasteboard untouched, for none.
  @discardableResult
  static func copy(ids: [Int], trackId: Int, snapshot: Snapshot, pasteboard: ChordPasteboard) -> Bool {
    guard let notes = snapshot.track(id: trackId)?.notes else { return false }
    return NoteClipboard.copy(Set(ids), from: notes, to: pasteboard)
  }

  /// The copy, then their `removeNotes` with the snapshot's revision, only when the pasteboard took the copy.
  static func cut(ids: [Int], trackId: Int, snapshot: Snapshot, pasteboard: ChordPasteboard) -> RollCommand? {
    guard let notes = snapshot.track(id: trackId)?.notes else { return nil }
    return NoteClipboard.cut(Set(ids), trackId: trackId, from: notes, baseRevision: snapshot.revision ?? 0,
                             to: pasteboard)
  }

  /// The `insertNotes` of the pasteboard's notes at `tick` (no `baseRevision`); nil without ChordLab notes on it or
  /// for a track the snapshot lacks.
  static func paste(at tick: Int, trackId: Int, snapshot: Snapshot, timing: RollTiming,
                    pasteboard: ChordPasteboard) -> RollCommand? {
    guard snapshot.track(id: trackId) != nil, let clip = NoteClipboard.paste(from: pasteboard) else { return nil }
    return NoteClipboard.pasteCommand(clip, trackId: trackId, at: tick, limits: RollLimits(timing: timing))
  }

  /// The `insertNotes` of a copy of the notes `ids` names, right after their latest end, the spacing kept, with the
  /// snapshot's revision; nil for none.
  static func duplicate(ids: [Int], trackId: Int, snapshot: Snapshot, timing: RollTiming) -> RollCommand? {
    guard let notes = snapshot.track(id: trackId)?.notes else { return nil }
    let picked = Set(ids)
    let clip = NoteClipboard.clipNotes(of: notes, ids: picked)
    guard !clip.isEmpty else { return nil }
    let end = NoteClipboard.pastePosition(marker: nil, notes: notes, selectedIds: picked)
    return NoteClipboard.pasteCommand(clip, trackId: trackId, at: end, limits: RollLimits(timing: timing),
                                      baseRevision: snapshot.revision ?? 0)
  }

  /// `selectNotes` of every note of the track, nil when the track has none or they already are the selection.
  static func selectAll(trackId: Int, snapshot: Snapshot) -> RollCommand? {
    guard let notes = snapshot.track(id: trackId)?.notes, !notes.isEmpty else { return nil }
    let all = Array(notes.id.prefix(notes.count))
    guard selectedIds(trackId: trackId, snapshot: snapshot) != all else { return nil }
    return .selectNotes(trackId: trackId, ids: all)
  }
}
