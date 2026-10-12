import SwiftUI

/// UI-side state. Logic lives in C++; this object only mirrors what the plugin
/// pushes (snapshot / error JSON) and forwards user intent (commands, MIDI) back.
class ChordLabState: NSObject, ObservableObject {
  var params: [Param] = []
  @Published var bundleID = String("")
  /// The last snapshot the plugin pushed; nil until the first one arrives.
  @Published var snapshot: Snapshot?
  /// The last command the plugin rejected, or a decode failure; cleared by the next good snapshot.
  @Published var lastError: String?
  /// Id of the palette cell whose voicing is sounding (pressed), or nil.
  @Published private(set) var heldCellId: String?
  private var heldPitches: [UInt8] = []

  let beginEdit: @MainActor (Int) -> Void
  let doEdit: @MainActor (Int, Double) -> Void
  let endEdit: @MainActor (Int) -> Void
  let sendMidi: @MainActor (UInt8, UInt8, UInt8) -> Void
  let sendCommand: @MainActor (Int, Data?) -> Void

  public override init() {
    self.beginEdit = { _ in }
    self.doEdit = { _, _ in }
    self.endEdit = { _ in }
    self.sendMidi = { _, _, _ in }
    self.sendCommand = { _, _ in }
    super.init()
  }

  public init(beginEdit: @MainActor @escaping (Int) -> Void = { _ in },
              doEdit: @MainActor @escaping (Int, Double) -> Void = { _, _ in },
              endEdit: @MainActor @escaping (Int) -> Void = { _ in },
              sendMidi: @MainActor @escaping (UInt8, UInt8, UInt8) -> Void = { _, _, _ in },
              sendCommand: @MainActor @escaping (Int, Data?) -> Void = { _, _ in }) {
    self.beginEdit = beginEdit
    self.doEdit = doEdit
    self.endEdit = endEdit
    self.sendMidi = sendMidi
    self.sendCommand = sendCommand
    super.init()
  }

  convenience init(numParams: Int) {
    self.init()
    for paramIdx in 0..<numParams {
      params.append(Param(id: paramIdx))
    }
  }

  // MARK: - Preview (M2: straight through the UI MIDI path; M3 moves it to the scheduler)

  @MainActor func sendChord(_ pitches: [UInt8], on: Bool, velocity: UInt8 = 100) {
    for p in pitches {
      sendMidi(on ? 0x90 : 0x80, p, on ? velocity : 0)
    }
  }

  /// Sounds `cell`'s voicing. At most one cell is held: pressing another first releases the held one,
  /// so every note-on this object sends is matched by exactly one note-off.
  @MainActor func pressCell(_ cell: CellJSON) {
    releaseHeld()
    let pitches = cell.voicing.compactMap { UInt8(exactly: $0) }.filter { $0 <= 127 }
    heldPitches = pitches
    heldCellId = cell.id
    sendChord(pitches, on: true)
  }

  /// Releases `cell` if it is the held one; a cell that was already released (by a press elsewhere,
  /// a cancelled gesture or disappearance) is ignored, never released twice.
  @MainActor func releaseCell(_ cell: CellJSON) {
    guard heldCellId == cell.id else { return }
    releaseHeld()
  }

  /// Releases whatever is held (view disappearance, window close).
  @MainActor func releaseHeld() {
    guard heldCellId != nil else { return }
    sendChord(heldPitches, on: false)
    heldPitches = []
    heldCellId = nil
  }

  // MARK: - Commands (engine wire format, json_api.h)

  /// Sends one command, e.g. `["cmd": "appendChord", "root": 0, "type": "maj"]`; `"schema": 1` is added
  /// here. The plugin answers with a snapshot (`kMsgTagSnapshot`) or an error (`kMsgTagError`).
  @MainActor func send(command: [String: Any]) {
    var message = command
    message["schema"] = 1
    guard JSONSerialization.isValidJSONObject(message),
          let data = try? JSONSerialization.data(withJSONObject: message) else {
      lastError = "command is not valid JSON: \(command)"
      return
    }
    sendCommand(kMsgTagCommand, data)
  }

  /// Asks the plugin to push its current snapshot.
  @MainActor func requestSnapshot() {
    sendCommand(kMsgTagRequestSnapshot, Data())
  }

  // MARK: - Messages from the plugin

  /// `kMsgTagSnapshot` payload.
  func applySnapshot(json: Data) {
    do {
      snapshot = try JSONDecoder().decode(Snapshot.self, from: json)
      lastError = nil
    } catch {
      lastError = "snapshot decode failed: \(error)"
    }
  }

  /// `kMsgTagError` payload.
  func applyError(json: Data) {
    if let message = try? JSONDecoder().decode(ErrorJSON.self, from: json) {
      lastError = message.error
    } else {
      lastError = String(decoding: json, as: UTF8.self)
    }
  }
}
