import Foundation

/// A selection of the strip's blocks (strip editing design D2): event ids in strip order and the anchor, one of
/// them (nil exactly when `ids` is empty). The anchor is what the suggestions and the palette relations follow.
struct StripSelection: Equatable {
  var ids: [Int]
  var anchor: Int?

  static let none = StripSelection(ids: [], anchor: nil)
}

/// The modifier keys of a click on the strip.
struct ClickModifiers: Equatable {
  var shift = false
  var command = false
}

/// What a click on the strip selects, and the commands and positions the strip's editing composes from a
/// selection (D5, D7). Pure: the view reads the modifiers and the snapshot and sends the result.
enum SelectionLogic {
  /// The selection after a click (D7). `click` is the clicked block's id, nil for the empty strip; `ids` the
  /// strip's event ids in order; `current` and `anchor` the selection before the click (the snapshot's).
  /// - The empty strip: nothing selected.
  /// - A plain click: that block alone, as the anchor.
  /// - ⇧-click, with or without ⌘: every block from the anchor's index to the clicked one's, either way, the
  ///   anchor unchanged; with no anchor on the strip, a plain click.
  /// - ⌘-click: toggles the block. Added, it becomes the anchor. Removed while it was the anchor, the anchor
  ///   moves to the nearest selected block after it in strip order, else before it, else none.
  /// A click on an id the strip does not hold (a stale view) changes nothing. The result keeps strip order and
  /// only ids the strip holds.
  static func resolve(click: Int?, modifiers: ClickModifiers, ids: [Int], current: [Int], anchor: Int?) -> StripSelection {
    let kept = ids.filter(current.contains)
    let keptAnchor = anchor.flatMap { kept.contains($0) ? $0 : nil }
    guard let click else { return .none }
    guard let clickIndex = ids.firstIndex(of: click) else {
      return StripSelection(ids: kept, anchor: keptAnchor ?? kept.last)
    }
    if modifiers.shift, let keptAnchor, let anchorIndex = ids.firstIndex(of: keptAnchor) {
      let range = min(anchorIndex, clickIndex)...max(anchorIndex, clickIndex)
      return StripSelection(ids: Array(ids[range]), anchor: keptAnchor)
    }
    if modifiers.command && !modifiers.shift {
      guard kept.contains(click) else {
        return StripSelection(ids: ids.filter { kept.contains($0) || $0 == click }, anchor: click)
      }
      let remaining = kept.filter { $0 != click }
      if let keptAnchor, keptAnchor != click {
        return StripSelection(ids: remaining, anchor: keptAnchor)
      }
      let position = { (id: Int) in ids.firstIndex(of: id) ?? 0 }
      let after = remaining.first { position($0) > clickIndex }
      let before = remaining.last { position($0) < clickIndex }
      return StripSelection(ids: remaining, anchor: after ?? before)
    }
    return StripSelection(ids: [click], anchor: click)
  }

  /// ⌘A: every block, the last the anchor; nothing on an empty strip.
  static func all(_ ids: [Int]) -> StripSelection {
    StripSelection(ids: ids, anchor: ids.last)
  }

  /// The command that makes `selection` the store's: `selectEvent {id}` for one block (`{id: null}` for none),
  /// else `selectEvents {ids, anchor}`.
  static func command(_ selection: StripSelection) -> [String: Any] {
    switch selection.ids.count {
    case 0:
      return ["cmd": "selectEvent", "id": NSNull()]
    case 1:
      return ["cmd": "selectEvent", "id": selection.ids[0]]
    default:
      var command: [String: Any] = ["cmd": "selectEvents", "ids": selection.ids]
      if let anchor = selection.anchor { command["anchor"] = anchor }
      return command
    }
  }

  /// The blocks a block's context-menu edit acts on (D7): the whole selection when the block is part of it, else
  /// the block alone.
  static func targets(block: Int, selection: [Int]) -> [Int] {
    selection.contains(block) ? selection : [block]
  }

  /// Where a paste or a recorded chord goes (D5, D9): right after the anchor, else at the end.
  static func insertionIndex(ids: [Int], anchor: Int?) -> Int {
    guard let anchor, let index = ids.firstIndex(of: anchor) else { return ids.count }
    return index + 1
  }

  /// Where ⌘D puts the copies (D5): right after the last of `targets` in strip order; the end when none of them
  /// is on the strip.
  static func index(after targets: [Int], ids: [Int]) -> Int {
    guard let last = ids.lastIndex(where: targets.contains) else { return ids.count }
    return last + 1
  }
}
