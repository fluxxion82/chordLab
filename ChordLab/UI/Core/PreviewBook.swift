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

/// A hold a view starts itself (`PreviewBook.hold`): a palette cell or a keyboard key. A roll hold comes only from the
/// roll's effects (`PreviewBook.perform`), so no caller can start one behind its gesture machine.
enum PreviewHoldRequest: Equatable {
  case cell(String)
  case key(pitch: Int)

  var owner: PreviewOwner {
    switch self {
    case .cell(let id): return .cell(id)
    case .key(let pitch): return .key(pitch: pitch)
    }
  }
}

/// What the caller does for the book (`ChordLabState` routes these and nothing else): send a command, send a preview
/// message, schedule a one-shot's end (`PreviewBook.oneShotEnded(id:)` after `PreviewBook.oneShotSeconds`), or cancel a
/// track's roll gesture (`ChordLabState.cancelRollGesture`) whose held preview the book has ALREADY stopped (the stop
/// precedes this output), so the stop that machine hands back finds no hold and sends nothing.
enum PreviewOutput: Equatable {
  case command(RollCommand)
  case preview(PreviewMessage)
  case scheduleOneShotEnd(id: Int)
  case cancelRoll(trackId: Int)
}

/// The bookkeeping of the editor's note previews through the engine's scheduler (tracks design D15 "Preview": note
/// presses, inserts and key clicks use the existing preview path, one pitch, paired Start/Stop with keep-alive): one
/// id counter, AT MOST ONE held preview whoever holds it (a palette cell, the roll, a keyboard key; `ChordLabState`
/// keeps it alive with `extend`s), and the short one-shot previews (an insert, a velocity drag's release), each
/// ended by its own timer. Every decision lives here; `ChordLabState` only routes the `PreviewOutput`s.
///
/// The invariants: every `start` is followed by exactly one `stop` with the same id, and a `stop` goes out only for an
/// id that is sounding; and a track's gesture machine (`RollEditLogic`: a held `previewStart` is always followed by
/// exactly one `previewStop`) believes it holds a pitch exactly when the book holds for that track. So whenever the
/// book ends a roll hold for any other reason than that machine's own stop (another owner takes the hold, a press on
/// another track, the keep-alive's lost release, the editor closing, panic), it stops the id AND emits `.cancelRoll`
/// for the track in the same outputs; the cancelled machine's stop then finds no hold. A new hold always ends the old
/// one explicitly that way; nothing is stopped silently.
///
/// Depends on nothing but Foundation and Core's `RollEffect` / `RollCommand`; unit-tested by the `ChordLabUICore`
/// package (Tests/ChordLabUICoreTests/PreviewBookTests.swift, with the real gesture machine).
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

  /// Stops the held preview; a roll hold's gesture is cancelled too unless `keepingGesture` (that machine's own stop).
  private mutating func stopHeld(keepingGesture: Bool = false) -> [PreviewOutput] {
    guard let held else { return [] }
    self.held = nil
    var outputs: [PreviewOutput] = [.preview(.stop(id: held.id))]
    if !keepingGesture, case .roll(let trackId) = held.owner { outputs.append(.cancelRoll(trackId: trackId)) }
    return outputs
  }

  private mutating func start(_ owner: PreviewOwner, pitches: [Int], velocity: Int) -> (id: Int, output: PreviewOutput) {
    let id = nextId()
    held = PreviewHold(id: id, owner: owner)
    return (id, .preview(.start(id: id, pitches: pitches, velocity: velocity)))
  }

  /// A cell or key press holds `pitches`: the held preview ends first (a roll hold with its gesture's cancel), then the
  /// new id's start. Returns the id, which only this press's end may stop (`endHold(id:)`).
  mutating func hold(_ request: PreviewHoldRequest, pitches: [Int], velocity: Int) -> (id: Int, outputs: [PreviewOutput]) {
    var outputs = stopHeld()
    let started = start(request.owner, pitches: pitches, velocity: velocity)
    outputs.append(started.output)
    return (started.id, outputs)
  }

  /// Ends whatever is held (a cell's release, the keep-alive's lost release, the editor closing, panic): its stop, and
  /// for a roll hold its gesture's cancel. Nothing when nothing is held.
  mutating func endHold() -> [PreviewOutput] {
    stopHeld()
  }

  /// Ends the hold `id` (a key press's own up or cancel); nothing when another hold, or none, is there now.
  mutating func endHold(id: Int) -> [PreviewOutput] {
    held?.id == id ? stopHeld() : []
  }

  /// A press went down in track `trackId`'s roll (before the machine sees it): a cell's or a key's hold, or another
  /// track's roll hold, ends now, so a press that starts no preview of its own (an empty click, a rubber band, a
  /// Velocity-mode press) never leaves one sounding behind it. This track's own hold is its machine's to stop.
  mutating func rollPressed(trackId: Int) -> [PreviewOutput] {
    held?.owner == .roll(trackId: trackId) ? [] : stopHeld()
  }

  /// A one-shot's timer fired: its stop, once.
  mutating func oneShotEnded(id: Int) -> [PreviewOutput] {
    oneShots.remove(id) == nil ? [] : [.preview(.stop(id: id))]
  }

  /// The roll's effects of track `trackId`, in order (the seam, `ChordLabState.performRollEffects`): a `send` is the
  /// command; a `previewStart` ends the held preview (another owner's with `.cancelRoll` for a roll; this track's own,
  /// which its machine always stops first, without) and holds as `.roll(trackId)`; a `previewStop` stops this track's
  /// roll hold and nothing else (never a second stop); a `previewOneShot` starts under a new id and schedules its end.
  /// Without `startsAllowed` (`LinkGate.allowsHold`: this instance follows another ChordLab) a `previewStart` or a
  /// `previewOneShot` changes nothing and sends nothing; commands and stops go on as ever.
  mutating func perform(_ effects: [RollEffect], trackId: Int, startsAllowed: Bool = true) -> [PreviewOutput] {
    var outputs: [PreviewOutput] = []
    for effect in effects {
      switch effect {
      case .send(let command):
        outputs.append(.command(command))
      case .previewStart where !startsAllowed, .previewOneShot where !startsAllowed:
        continue
      case .previewStart(let pitch, let velocity):
        outputs += stopHeld(keepingGesture: held?.owner == .roll(trackId: trackId))
        outputs.append(start(.roll(trackId: trackId), pitches: [pitch], velocity: velocity).output)
      case .previewStop:
        if held?.owner == .roll(trackId: trackId) { outputs += stopHeld(keepingGesture: true) }
      case .previewOneShot(let pitch, let velocity):
        let id = nextId()
        oneShots.insert(id)
        outputs += [.preview(.start(id: id, pitches: [pitch], velocity: velocity)), .scheduleOneShotEnd(id: id)]
      }
    }
    return outputs
  }
}
