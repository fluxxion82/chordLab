import CoreGraphics
import Foundation

/// Who holds the editor's one held preview (`PreviewBook`): a pressed palette cell (its id), the roll's gesture
/// machine of one track (a note press, design D15 "Preview"), or a pressed key of the roll's keyboard column (D14).
enum PreviewOwner: Equatable {
  case cell(String)
  case roll(trackId: Int)
  case key(pitch: Int)
}

/// The held preview: its scheduler id and its owner.
struct PreviewHold: Equatable {
  let id: Int
  let owner: PreviewOwner
}

/// One `kMsgTagPreview` message to the engine's scheduler: `start` sounds `pitches` under `id` until the `stop` with
/// the same id (or the scheduler's four-second watchdog), `extend` renews a held one's watchdog. `json` is the payload
/// `ChordLabState` sends (it adds `schema`).
enum PreviewMessage: Equatable {
  case start(id: Int, pitches: [Int], velocity: Int)
  case stop(id: Int)
  case extend(id: Int)

  var json: [String: Any] {
    switch self {
    case .start(let id, let pitches, let velocity): return ["id": id, "on": true, "velocity": velocity, "pitches": pitches]
    case .stop(let id): return ["id": id, "on": false]
    case .extend(let id): return ["id": id, "extend": true]
    }
  }
}

/// How the held preview must end (`PreviewBook.release(before:)`): nothing is held; the book stops it itself
/// (`endHold`, a cell's or a key's hold); or the roll's gesture of that track is cancelled, whose own stop comes back
/// through the seam (`ChordLabState.cancelRollGesture`), so the machine never believes it holds a pitch that was
/// stopped behind its back.
enum PreviewRelease: Equatable {
  case none
  case stop
  case cancelRoll(trackId: Int)
}

/// What the roll's effects seam does for one batch of `RollEffect`s (`PreviewBook.perform`): send a command, send a
/// preview message, or schedule a one-shot's end (`PreviewBook.oneShotEnded(id:)` after `PreviewBook.oneShotSeconds`).
enum RollSeamOutput: Equatable {
  case command(RollCommand)
  case preview(PreviewMessage)
  case scheduleOneShotEnd(id: Int)
}

/// The bookkeeping of the editor's note previews through the engine's scheduler (tracks design D15 "Preview": note
/// presses, inserts and key clicks use the existing preview path, one pitch, paired Start/Stop with keep-alive): one
/// id counter, AT MOST ONE held preview whoever holds it (a palette cell, the roll, a keyboard key; `ChordLabState`
/// keeps it alive with `extend`s), and the short one-shot previews (an insert, a velocity drag's release), each
/// ended by its own timer.
///
/// The invariant: every `start` the book emits is followed by exactly one `stop` with the same id, and a `stop` is
/// emitted only for an id that is sounding. A hold over a hold stops the old one first; an owner's end stops only
/// its own hold; a one-shot's end stops it once.
///
/// The roll's hold belongs to that track's gesture machine (`RollEditLogic`: a held `previewStart` is always followed
/// by exactly one `previewStop`): another owner taking the hold, the keep-alive's lost-release, the editor closing and
/// panic end it by cancelling the gesture (`release(before:)` says `.cancelRoll`), so the machine's stop is the one
/// that reaches the scheduler and the two never disagree.
///
/// Depends on nothing but Foundation and Core's `RollEffect` / `RollCommand`; unit-tested by the `ChordLabUICore`
/// package (Tests/ChordLabUICoreTests/PreviewBookTests.swift).
struct PreviewBook: Equatable {
  /// How long a one-shot sounds before its stop (an insert's or a velocity drag's preview).
  static let oneShotSeconds: TimeInterval = 0.3

  private(set) var held: PreviewHold?
  /// The one-shots started whose stop is still due.
  private(set) var oneShots: Set<Int> = []
  /// The last id given out; ids start at 1 and are never reused.
  private(set) var lastId = 0

  /// The held palette cell's id (the cell draws pressed), nil when no cell holds.
  var heldCellId: String? {
    if case .cell(let id)? = held?.owner { return id }
    return nil
  }

  private mutating func nextId() -> Int {
    lastId += 1
    return lastId
  }

  /// How the current hold must end before `owner` holds (nil: it just ends — the editor closing, the keep-alive's
  /// lost release, panic). A roll hold of ANOTHER owner (or for nil) is `.cancelRoll`; the same track's roll is
  /// `.stop` (its machine always stops before it starts again, so this is only a guard).
  func release(before owner: PreviewOwner?) -> PreviewRelease {
    guard let held else { return .none }
    if case .roll(let trackId) = held.owner, owner != held.owner { return .cancelRoll(trackId: trackId) }
    return .stop
  }

  /// `owner` holds `pitches`: a new id's start, after the stop of any hold still there (the caller has ended a roll
  /// hold through its gesture first, per `release(before:)`).
  mutating func startHold(_ owner: PreviewOwner, pitches: [Int], velocity: Int) -> [PreviewMessage] {
    var messages = endHold()
    let id = nextId()
    held = PreviewHold(id: id, owner: owner)
    messages.append(.start(id: id, pitches: pitches, velocity: velocity))
    return messages
  }

  /// Stops whatever is held; nothing when nothing is.
  mutating func endHold() -> [PreviewMessage] {
    guard let held else { return [] }
    self.held = nil
    return [.stop(id: held.id)]
  }

  /// Stops the hold when `owner` says it is its own; nothing otherwise.
  mutating func endHold(if owner: (PreviewOwner) -> Bool) -> [PreviewMessage] {
    guard let held, owner(held.owner) else { return [] }
    return endHold()
  }

  /// A one-shot's timer fired: its stop, once.
  mutating func oneShotEnded(id: Int) -> [PreviewMessage] {
    oneShots.remove(id) == nil ? [] : [.stop(id: id)]
  }

  /// The roll's effects of track `trackId`, in order (the seam, `ChordLabState.performRollEffects`): a `send` is the
  /// command; a `previewStart` holds as `.roll(trackId)` (the caller ends another owner's hold first, per
  /// `release(before:)`); a `previewStop` stops that track's roll hold and nothing else (never a second stop); a
  /// `previewOneShot` starts under a new id and schedules its end.
  mutating func perform(_ effects: [RollEffect], trackId: Int) -> [RollSeamOutput] {
    var outputs: [RollSeamOutput] = []
    for effect in effects {
      switch effect {
      case .send(let command):
        outputs.append(.command(command))
      case .previewStart(let pitch, let velocity):
        outputs += startHold(.roll(trackId: trackId), pitches: [pitch], velocity: velocity).map(RollSeamOutput.preview)
      case .previewStop:
        outputs += endHold(if: { $0 == .roll(trackId: trackId) }).map(RollSeamOutput.preview)
      case .previewOneShot(let pitch, let velocity):
        let id = nextId()
        oneShots.insert(id)
        outputs += [.preview(.start(id: id, pitches: [pitch], velocity: velocity)), .scheduleOneShotEnd(id: id)]
      }
    }
    return outputs
  }
}

/// The roll's keyboard column (tracks design D14: clicking a key previews it): which key a point is on, and the
/// velocity a key preview plays at.
enum PianoKeys {
  /// A key click's velocity (an insert's, design D15).
  static let velocity = 100

  /// The pitch of the key at the column's view y (top-left origin) with the grid scrolled to content y `scrollY`;
  /// nil above pitch 127's row, below pitch 0's, or for a y that is not a number.
  static func pitch(atViewY y: CGFloat, geometry: RollGeometry, scrollY: CGFloat) -> Int? {
    let content = y + scrollY
    guard content.isFinite, content >= 0, content < CGFloat(RollGeometry.pitchCount) * geometry.rowHeight else { return nil }
    return geometry.pitch(atY: content)
  }
}
