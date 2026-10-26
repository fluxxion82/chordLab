import SwiftUI

/// UI-side state. Logic lives in C++; this object only mirrors what the plugin
/// pushes (snapshot, error, status, transport and export-ready JSON) and forwards user intent
/// (commands, previews, export requests) back.
class ChordLabState: NSObject, ObservableObject {
  var params: [Param] = []
  @Published var bundleID = String("")
  /// The last snapshot the plugin pushed; nil until the first one arrives.
  @Published var snapshot: Snapshot?
  /// The last command the plugin rejected, or a decode failure; cleared by the next good snapshot.
  @Published var lastError: String?
  /// Counts every error reported into `lastError`, so a watcher can tell a repeated identical error
  /// (the same refusal twice) from no answer at all; `lastError` alone cannot.
  @Published var errorSerial = 0
  /// Audio blocks the scheduler could not complete (debug aid from `kMsgTagStatus`).
  @Published var incompleteBlocks: Int = 0
  /// The playhead (`kMsgTagTransport`); nil until the plug-in pushes one.
  @Published var transport: TransportJSON?
  /// The MIDI file of the last export request (`kMsgTagExportReady`), or nil while none is ready.
  @Published var exportPath: String?
  /// Whether a text field (a section rename) is being edited: the key routes (`EditorKeyRouter`) stand
  /// down meanwhile, so typing a name never appends or removes chords.
  @Published var editingText = false
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

  /// Runs a keyboard command. Digits append the suggestion chip of that number (nothing when there is
  /// no such chip); Delete removes the selected chord (nothing without a selection); undo and redo are
  /// the key bar's commands. The router consumes every mapped key whether or not it did anything, so
  /// the host never acts on it (see `KeyCommandMap`, `EditorKeyRouter`).
  @MainActor func perform(_ command: KeyCommand) {
    switch command {
    case .appendSuggestion(let n):
      guard let suggestions = snapshot?.suggestions, suggestions.indices.contains(n - 1) else { return }
      let suggestion = suggestions[n - 1]
      send(command: ["cmd": "appendChord", "root": suggestion.root, "type": suggestion.type])
    case .deleteSelection:
      guard let id = snapshot?.selectedEventId else { return }
      send(command: ["cmd": "removeChord", "id": id])
    case .undo:
      send(command: ["cmd": "undo"])
    case .redo:
      send(command: ["cmd": "redo"])
    }
  }

  /// JSON-encodes `fields` plus `"schema": 1` and sends it under `tag`.
  @MainActor private func send(_ fields: [String: Any], tag: Int) {
    var message = fields
    message["schema"] = 1
    guard JSONSerialization.isValidJSONObject(message),
          let data = try? JSONSerialization.data(withJSONObject: message) else {
      reportError("message is not valid JSON: \(fields)")
      return
    }
    sendCommand(tag, data)
  }

  /// Asks the plugin to push its current snapshot.
  @MainActor func requestSnapshot() {
    sendCommand(kMsgTagRequestSnapshot, Data())
  }

  /// Asks the plugin to write the progression as a MIDI file. The round trip is synchronous on the main
  /// thread (`OnMessage` runs inline and answers through the delegate at once), so `exportPath` holds the
  /// new file, or nil after a failure, when this returns.
  @MainActor func requestExport() {
    exportPath = nil
    sendCommand(kMsgTagExport, Data())
  }

  /// The same for one section (`kMsgTagExportSection {id}`): the section's chords alone, re-based to
  /// beat 0; an unknown id comes back as `kMsgTagError`.
  @MainActor func requestExportSection(id: Int) {
    exportPath = nil
    send(["id": id], tag: kMsgTagExportSection)
  }

  // MARK: - Messages from the plugin

  /// `kMsgTagSnapshot` payload.
  func applySnapshot(json: Data) {
    do {
      snapshot = try JSONDecoder().decode(Snapshot.self, from: json)
      lastError = nil
    } catch {
      reportError("snapshot decode failed: \(error)")
    }
  }

  /// `kMsgTagStatus` payload.
  func applyStatus(json: Data) {
    if let status = try? JSONDecoder().decode(StatusJSON.self, from: json) {
      incompleteBlocks = status.incompleteBlocks
    }
  }

  /// `kMsgTagTransport` payload.
  func applyTransport(json: Data) {
    if let transport = try? JSONDecoder().decode(TransportJSON.self, from: json) {
      self.transport = transport
    }
  }

  /// `kMsgTagExportReady` payload.
  func applyExportReady(json: Data) {
    if let ready = try? JSONDecoder().decode(ExportReadyJSON.self, from: json) {
      exportPath = ready.path
    }
  }

  /// `kMsgTagError` payload.
  func applyError(json: Data) {
    if let message = try? JSONDecoder().decode(ErrorJSON.self, from: json) {
      reportError(message.error)
    } else {
      reportError(String(decoding: json, as: UTF8.self))
    }
  }

  private func reportError(_ message: String) {
    lastError = message
    errorSerial += 1
  }
}
