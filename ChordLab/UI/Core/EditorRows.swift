import Foundation

/// Which of the editor's optional rows show (`ContentView`): the Follow banner, the key transpose row and the palette's
/// Diatonic / Applied switch row, and whether the banner's refusal sentence takes the place of its sentence on its one
/// line. The palette viewport is the row that gives up height, and at the 700 x 632 minimum it must keep one full row
/// (76 pt; measured for every combination by the render harness, ChordLab/Tests/viewharness):
/// - in a window shorter than `FollowOffer.compactHeight`, at most one of the banner and the key row shows, and the key
///   row wins: it is short-lived (the hint until it is answered or another key is browsed, the moved note four
///   seconds), and the banner returns when it goes;
/// - the switch row shows unless the key row shows in a short window, and always while the listener is active (it
///   holds the Heard group), as before the banner existed;
/// - in a short window the banner's refusal sentence replaces its sentence instead of adding a line under it.
///
/// Depends on nothing but Foundation; unit-tested by the `ChordLabUICore` package
/// (Tests/ChordLabUICoreTests/EditorRowsTests.swift).
struct EditorRows: Equatable {
  let banner: Bool
  let keyRow: Bool
  let paletteSwitchRow: Bool
  /// The banner draws the refusal sentence in place of its sentence (one line); false: under it, or there is none.
  let noticeInline: Bool

  /// `offer`: the banner has something to show (`FollowOffer.make` gave one); `keyRow`: the key transpose row has;
  /// `heardActive`: the listener is active; `compact`: the window is short (`FollowOffer.isCompact`); `notice`: a
  /// refusal sentence is waiting (`FollowAttempt.notice`).
  static func visible(offer: Bool, keyRow: Bool, heardActive: Bool, compact: Bool, notice: Bool) -> EditorRows {
    let banner = offer && !(compact && keyRow)
    return EditorRows(banner: banner, keyRow: keyRow, paletteSwitchRow: !keyRow || !compact || heardActive,
                      noticeInline: banner && compact && notice)
  }

  /// Whether the status lines (incomplete blocks, the last error) have anything to show; the follower's panel gives
  /// them a row only then.
  static func showsStatusLines(incompleteBlocks: Int, lastError: String?) -> Bool {
    incompleteBlocks > 0 || lastError != nil
  }
}
