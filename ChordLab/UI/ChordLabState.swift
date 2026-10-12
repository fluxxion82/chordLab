import SwiftUI

/// UI-side state. Logic lives in C++; this object only mirrors what the plugin
/// pushes (snapshot / error / status JSON) and forwards user intent (commands, previews) back.
class ChordLabState: NSObject, ObservableObject {
  var params: [Param] = []
  @Published var bundleID = String("")
  /// The last snapshot the plugin pushed; nil until the first one arrives.
  @Published var snapshot: Snapshot?
  /// The last command the plugin rejected, or a decode failure; cleared by the next good snapshot.
  @Published var lastError: String?
  /// Audio blocks the scheduler could not complete (debug aid from `kMsgTagStatus`).
  @Published var incompleteBlocks: Int = 0
  /// Audio blocks that allocated (Debug plug-in builds only; 0 otherwise).
  @Published var allocatingBlocks: Int = 0
  /// Id of the palette cell whose voicing is sounding (pressed), or nil.
  @Published private(set) var heldCellId: String?
  /// Preview id of the held cell: a per-press counter, so that Start and Stop pair up in the scheduler.
  private var heldPreviewId: Int?
  private var previewCounter = 0

  let beginEdit: @MainActor (Int) -> Void
  let doEdit: @MainActor (Int, Double) -> Void
  let endEdit: @MainActor (Int) -> Void
  let sendCommand: @MainActor (Int, Data?) -> Void

  public override init() {
    self.beginEdit = { _ in }
    self.doEdit = { _, _ in }
    self.endEdit = { _ in }
    self.sendCommand = { _, _ in }
    super.init()
  }

  public init(beginEdit: @MainActor @escaping (Int) -> Void = { _ in },
              doEdit: @MainActor @escaping (Int, Double) -> Void = { _, _ in },
              endEdit: @MainActor @escaping (Int) -> Void = { _ in },
              sendCommand: @MainActor @escaping (Int, Data?) -> Void = { _, _ in }) {
    self.beginEdit = beginEdit
    self.doEdit = doEdit
    self.endEdit = endEdit
    self.sendCommand = sendCommand
    super.init()
  }

  convenience init(numParams: Int) {
    self.init()
    for paramIdx in 0..<numParams {
      params.append(Param(id: paramIdx))
    }
  }

  // MARK: - Preview (through the engine scheduler: kMsgTagPreview Start/Stop paired by id)

  /// Sounds `cell`'s voicing. At most one cell is held: pressing another first releases the held one,
  /// so every Start this object sends is matched by exactly one Stop with the same id (and the
  /// scheduler's 4 s watchdog ends a preview whose Stop is lost).
  @MainActor func pressCell(_ cell: CellJSON) {
    releaseHeld()
    previewCounter += 1
    let id = previewCounter
    heldPreviewId = id
    heldCellId = cell.id
    let pitches = Array(cell.voicing.filter { (0...127).contains($0) }.prefix(8))
    send(["id": id, "on": true, "velocity": 100, "pitches": pitches], tag: kMsgTagPreview)
  }

  /// Releases `cell` if it is the held one; a cell that was already released (by a press elsewhere,
  /// a cancelled gesture or disappearance) is ignored, never released twice.
  @MainActor func releaseCell(_ cell: CellJSON) {
    guard heldCellId == cell.id else { return }
    releaseHeld()
  }

  /// Releases whatever is held (view disappearance, window close).
  @MainActor func releaseHeld() {
    guard let id = heldPreviewId else { return }
    heldPreviewId = nil
    heldCellId = nil
    send(["id": id, "on": false], tag: kMsgTagPreview)
  }

  /// Silences everything the scheduler is sounding (next audio block).
  @MainActor func panic() {
    heldPreviewId = nil
    heldCellId = nil
    sendCommand(kMsgTagPanic, Data())
  }

  // MARK: - Commands (engine wire format, json_api.h)

  /// Sends one command, e.g. `["cmd": "appendChord", "root": 0, "type": "maj"]`; `"schema": 1` is added
  /// here. The plugin answers with a snapshot (`kMsgTagSnapshot`) or an error (`kMsgTagError`).
  @MainActor func send(command: [String: Any]) {
    send(command, tag: kMsgTagCommand)
  }

  /// JSON-encodes `fields` plus `"schema": 1` and sends it under `tag`.
  @MainActor private func send(_ fields: [String: Any], tag: Int) {
    var message = fields
    message["schema"] = 1
    guard JSONSerialization.isValidJSONObject(message),
          let data = try? JSONSerialization.data(withJSONObject: message) else {
      lastError = "message is not valid JSON: \(fields)"
      return
    }
    sendCommand(tag, data)
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

  /// `kMsgTagStatus` payload.
  func applyStatus(json: Data) {
    if let status = try? JSONDecoder().decode(StatusJSON.self, from: json) {
      incompleteBlocks = status.incompleteBlocks
      allocatingBlocks = status.allocatingBlocks ?? 0
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
