import SwiftUI

/// UI-side state. Logic lives in C++; this object only mirrors what the plugin
/// pushes (snapshot, error, status, transport, export-ready and heard JSON) and forwards user intent
/// (commands, previews, export requests, forgetting what was heard) back.
class ChordLabState: NSObject, ObservableObject {
  var params: [Param] = []
  @Published var bundleID = String("")
  /// The last snapshot the plugin pushed; nil until the first one arrives.
  @Published var snapshot: Snapshot? {
    didSet { rollTiming = snapshot.map(RollTiming.init(snapshot:)) }
  }
  /// The roll's timing of `snapshot` (`RollTiming`), computed once per snapshot: its `contentEndTick` walks every
  /// track's notes, and the roll's setters and canvases read it at display rate.
  private(set) var rollTiming: RollTiming?
  /// The last command the plugin rejected, or a decode failure; cleared by the next good snapshot.
  @Published var lastError: String?
  /// Counts every error reported into `lastError`, so a watcher can tell a repeated identical error
  /// (the same refusal twice) from no answer at all; `lastError` alone cannot.
  @Published var errorSerial = 0
  /// Audio blocks the scheduler could not complete (debug aid from `kMsgTagStatus`).
  @Published var incompleteBlocks: Int = 0
  /// The playhead (`kMsgTagTransport`); nil until the plug-in pushes one.
  @Published var transport: TransportJSON?
  /// The host tempo from the last transport push (`TransportJSON.tempo`), 120 until the first one (design D4):
  /// what Record turns a held time into beats with.
  var bpm: Double { transport?.tempo ?? 120 }
  /// The MIDI file of the last export request (`kMsgTagExportReady`), or nil while none is ready.
  @Published var exportPath: String?
  /// The drag-outs' round trip (`ClipExportExchange`): each request's outcome, and which errors answer an export.
  private let exports = ClipExportExchange()
  /// What the plug-in hears on its MIDI input (`kMsgTagHeard`, design D14); nil until the first push. The
  /// Heard group (`HeardView`) shows only while `active` is true.
  @Published var heard: HeardJSON?
  /// Whether a text field (a section rename) is being edited: the key routes (`EditorKeyRouter`) stand
  /// down meanwhile, so typing a name never appends or removes chords.
  @Published var editingText = false
  /// What the keyboard edits (tracks design D15 "Keyboard", `KeyTarget`): the chords, or one track's notes; the key
  /// routes ask the map with it (`EditorKeyRouter`) and `perform` dispatches by it (`KeyCommand.route`). Set by the
  /// last press: the roll's mouse-down and its context-menu click target that track's notes (`handleRollPointer`,
  /// `rollContextMenu`); a press on the strip, a palette or home-row cell or a suggestion chip targets the chords
  /// (`targetChords`). Every applied snapshot resolves it (`KeyTarget.resolved`: a removed track's notes fall back to
  /// the chords), and every open-lane change resets it (`KeyTarget.afterOpenLaneChange`, in `openLane`'s observer).
  @Published var keyTarget: KeyTarget = .chords
  /// The lane the editor shows (tracks design D13): Chords (suggestions, palette, home row) or one track's roll.
  /// UI state, never saved; every applied snapshot resolves it (`Lane.resolved`: a removed track falls back to
  /// Chords). Changing it resets the key target, so a key never acts on a lane that has just been hidden.
  @Published var openLane: Lane = .chords {
    didSet {
      guard openLane != oldValue else { return }
      // The roll being left drops its gesture now (its held preview stops), not at its next event. Lane changes
      // come from the main thread (the editor's views and the snapshot's fallback).
      if case .track(let left) = oldValue { MainActor.assumeIsolated { cancelRollGesture(trackId: left) } }
      let target = keyTarget.afterOpenLaneChange(from: oldValue, to: openLane)
      if target != keyTarget { keyTarget = target }
    }
  }
  /// The pending `addTrack`, until its answer: the next snapshot opens the one new track (`LaneOpener`,
  /// `LaneTabs.laneAfterSnapshot`); a refusal (`applyError`) forgets it.
  private var laneOpener = LaneOpener()
  /// The lineage of the action in progress (`action`, `ActionLineage`): every command and export request it sends
  /// carries the token read once at its start, so a stale answer to its first command (which renews the token inline)
  /// cannot let its later commands, computed from the old document, through.
  private var actionLineage = ActionLineage()
  /// Every track's piano roll view state (scroll, zoom, grid, snap, mode, other tracks, the gesture state) and the
  /// roll's viewport, by track id (tracks design D14; UI-only, never saved). A separate observable object, not a
  /// `@Published` property here: the roll scrolls at display rate, and a change published by this object would
  /// re-run the body of every view that observes it (the key bar, the transport row, the strip, the tabs). Only the
  /// roll observes `rolls`; it changes through the `Roll` methods below.
  let rolls = RollViewStore()
  /// What the rolls' harmonic guides draw (the roll toolbar's View menu, `GuideDisplay`): an editor-wide preference,
  /// the same on every track, read from `defaults` when the state is made and written back by `setGuideDisplay`;
  /// another editor of this process changing it (a second plug-in instance) updates it too. Never sent to the engine.
  @Published private(set) var guideDisplay: GuideDisplay
  /// Where editor-wide preferences are remembered (`guideDisplay`): `UserDefaults.standard`, the store `@AppStorage`
  /// uses for the palette's label mode; a harness passes a store of its own so it never writes the user's.
  let defaults: UserDefaults
  /// Sees `defaults` change (`UserDefaults.didChangeNotification`), so another editor's View menu choice shows here.
  private var defaultsObserver: NSObjectProtocol?
  /// The row under the key bar (`KeyTransposeRow`): the one-time "chords kept their notes" hint after a tonic
  /// change with Transpose with key off, or the four-second "moved" note after one with it on (D8, D9).
  /// Either closes as soon as a snapshot browses a key other than the project key.
  enum KeyRow: Equatable {
    /// Switch off: offer to move the chords from `fromPc`; `basis` is what the key change's own snapshot
    /// showed, and the hint lives only while snapshots still match it (`KeyTransposeHint.stillValid`).
    case hint(fromPc: Int, basis: KeyTransposeHint.Basis)
    /// Switch on: `count` chords moved to `tonic` (display form).
    case moved(count: Int, tonic: String)
  }
  @Published private(set) var keyRow: KeyRow?
  /// A key change sent from the Key pickers whose snapshot (the first with its new tonic and the project key
  /// browsed) has not arrived yet.
  private var pendingKeyChange: (fromPc: Int, toPc: Int, showHint: Bool, transposing: Bool, count: Int)?
  /// How long the moved note shows.
  static let movedNoteDuration: TimeInterval = 4.0
  /// Counts moved notes, so a note's timer clears that note only, never a later one.
  private var movedNoteSerial = 0

  /// Id of the palette cell whose voicing is sounding (pressed), or nil (`PreviewBook.heldCellId`, published here so
  /// the cell draws pressed).
  @Published private(set) var heldCellId: String?
  /// The note previews' bookkeeping (`PreviewBook`): the one held preview (a palette cell, the roll's note press, a
  /// keyboard key), the roll's one-shots, the id counter. Start and Stop pair up by id in the scheduler.
  private var previews = PreviewBook()
  /// The transport row's Record switch (strip editing design D9): while on, releasing a palette or home-row cell
  /// inserts its chord after the anchor (else at the end), as long as the cell was held. UI state: off whenever
  /// the window opens, never saved.
  @Published var recording = false
  /// While a preview is held (a cell, the roll's note press, a keyboard key), an `extend` for it every
  /// `keepAliveInterval` from a main-run-loop timer in the common modes (so it fires during mouse tracking). The
  /// scheduler ends a preview four seconds of processed audio after its Start or its last applied Extend, so it ends a
  /// held one only when the extends stop reaching it: a main thread stalled for several seconds (the cell then shows
  /// pressed until released and pressed again). It also ends a preview whose release never arrived;
  /// while the host processes no audio nothing expires. Each tick also stands in for a release the view
  /// never reported (`PreviewKeepAlive`): a hold that began with a mouse button down is released once no
  /// button is down; one that began without (touch input is outside the button mask) is renewed for at
  /// most `PreviewKeepAlive.maxTicksWithoutButton` ticks; a roll hold is released by cancelling its gesture
  /// (`releaseHeld`). Invalidated on release, panic and deinit, always on the main thread.
  static let keepAliveInterval: TimeInterval = 1.0
  private var keepAlive: Timer?
  /// Whether a mouse button was down when the held preview began, and the keep-alive ticks since.
  private var holdHasButton = false
  private var keepAliveTicks = 0
  /// The physical button state (`NSEvent.pressedMouseButtons`); a harness that synthesizes events
  /// passes its own. Read by the preview keep-alive and by the roll's input view (`RollInputHandlers.mouseButtonsDown`:
  /// a gesture with no button down lost its mouse-up).
  let mouseButtonsDown: () -> Bool

  let beginEdit: @MainActor (Int) -> Void
  let doEdit: @MainActor (Int, Double) -> Void
  let endEdit: @MainActor (Int) -> Void
  let sendCommand: @MainActor (Int, Data?) -> Void

  public override init() {
    self.beginEdit = { _ in }
    self.doEdit = { _, _ in }
    self.endEdit = { _ in }
    self.sendCommand = { _, _ in }
    self.mouseButtonsDown = { NSEvent.pressedMouseButtons != 0 }
    self.resolvedClipboardToken = ClipboardToken.resolve(nil)
    self.defaults = .standard
    self.guideDisplay = GuideDisplay(rawValue: UserDefaults.standard.string(forKey: GuideDisplay.storageKey))
    super.init()
    observeDefaults()
  }

  /// `clipboardToken` is the plug-in instance's (`ClipboardToken`: the view controller passes the one the C++ shell
  /// made); nil makes one for this state alone. `defaults` is where editor-wide preferences are remembered
  /// (`UserDefaults.standard` unless a harness passes its own).
  public init(beginEdit: @MainActor @escaping (Int) -> Void = { _ in },
              doEdit: @MainActor @escaping (Int, Double) -> Void = { _, _ in },
              endEdit: @MainActor @escaping (Int) -> Void = { _ in },
              sendCommand: @MainActor @escaping (Int, Data?) -> Void = { _, _ in },
              mouseButtonsDown: @escaping () -> Bool = { NSEvent.pressedMouseButtons != 0 },
              clipboardToken: String? = nil,
              defaults: UserDefaults = .standard) {
    self.beginEdit = beginEdit
    self.doEdit = doEdit
    self.endEdit = endEdit
    self.sendCommand = sendCommand
    self.mouseButtonsDown = mouseButtonsDown
    self.resolvedClipboardToken = ClipboardToken.resolve(clipboardToken)
    self.defaults = defaults
    self.guideDisplay = GuideDisplay(rawValue: defaults.string(forKey: GuideDisplay.storageKey))
    super.init()
    observeDefaults()
  }

  deinit {
    if let defaultsObserver { NotificationCenter.default.removeObserver(defaultsObserver) }
    // The timer holds `self` weakly (`KeepAliveTarget`), so the last release can come from any thread; a run-loop timer must
    // be invalidated on the thread it was scheduled on, the main thread here.
    guard let timer = keepAlive else { return }
    if Thread.isMainThread { timer.invalidate() } else { DispatchQueue.main.async { timer.invalidate() } }
  }

  convenience init(numParams: Int) {
    self.init()
    for paramIdx in 0..<numParams {
      params.append(Param(id: paramIdx))
    }
  }

  // MARK: - Preview (through the engine scheduler: kMsgTagPreview Start/Stop paired by id, Extend while held)

  /// Sounds `cell`'s voicing. At most one preview is held (`PreviewBook`): pressing a cell first ends the held one
  /// (a roll's with its gesture's cancel), so every Start this object sends is matched by exactly one Stop with the
  /// same id (and the scheduler's 4 s watchdog ends a preview whose Stop never arrived; `extend`s every second keep a
  /// held one alive).
  @MainActor func pressCell(_ cell: CellJSON) {
    targetChords()
    route(previews.hold(.cell(cell.id), pitches: Array(cell.voicing.filter { (0...127).contains($0) }.prefix(8)),
                        velocity: 100).outputs)
  }

  /// Releases `cell` if it is the held one; a cell that was already released (by a press elsewhere, a cancelled
  /// gesture or disappearance) is ignored, never released twice. `release` says why the press ended and how long
  /// it was held, from the window's event timestamps (`PressRelease`); with Record on, a press that ended with its
  /// own button-up also inserts its chord for that long (`RecordLength`), and a press that became a drag (the drop
  /// appends by itself), was superseded or disappeared records nothing.
  @MainActor func releaseCell(_ cell: CellJSON, _ release: PressRelease) {
    guard heldCellId == cell.id else { return }
    action {
      releaseHeld()
      record(cell, release)
    }
  }

  /// Record's insert for a released cell (D9), or nothing (`RecordLength.recordedBeats`): the chord's root and
  /// type, the held time at the host tempo (`bpm`), after the anchor or at the end; the insert selects it, so
  /// presses chain.
  @MainActor private func record(_ cell: CellJSON, _ release: PressRelease) {
    guard let snapshot,
          let beats = RecordLength.recordedBeats(
            recording: recording, release: release, bpm: bpm,
            defaultBeats: LengthOptions.newChordBeats(snapshot.newChordBeats, meter: snapshot.meter))
    else { return }
    let index = SelectionLogic.insertionIndex(ids: snapshot.events.map(\.id), anchor: snapshot.selectedEventId)
    send(command: RecordLength.command(root: cell.root, type: cell.type, beats: beats, index: index))
  }

  /// A press on the roll's keyboard column (tracks design D14: clicking a key previews it): holds `pitch` until
  /// `releaseKey(id:)` with the id returned (or a lost release, which the keep-alive ends), ending any other hold
  /// first. Nil for a pitch outside 0...127.
  @MainActor func pressKey(pitch: Int) -> Int? {
    guard (0...127).contains(pitch) else { return nil }
    let held = previews.hold(.key(pitch: pitch), pitches: [pitch], velocity: PianoKeys.velocity)
    route(held.outputs)
    return held.id
  }

  /// The keyboard press `id` ended (its up, a cancel): stops that hold only; nothing when it no longer holds (another
  /// press, the keep-alive or panic already ended it), so a key never stops twice or stops another press.
  @MainActor func releaseKey(id: Int) {
    route(previews.endHold(id: id))
  }

  /// Ends whatever is held (the editor closing, the keep-alive's lost release, panic): its Stop, and for a roll hold
  /// that gesture's cancel (`PreviewBook.endHold`), so the gesture machine never believes it holds a stopped pitch.
  @MainActor func releaseHeld() {
    route(previews.endHold())
  }

  /// Ends every sounding preview with its own Stop (the held one as `releaseHeld` does; a one-shot keeps its pending
  /// stop), then silences everything the scheduler is sounding (next audio block).
  @MainActor func panic() {
    releaseHeld()
    sendCommand(kMsgTagPanic, Data())
  }

  /// Carries out the book's outputs (`PreviewOutput`), the only thing this object does for the previews: commands and
  /// preview messages are sent, a one-shot's end is scheduled, a `.cancelRoll` cancels that track's gesture (its held
  /// preview is already stopped, so the stop it hands back sends nothing); then the keep-alive follows the hold.
  @MainActor private func route(_ outputs: [PreviewOutput]) {
    action {
      for output in outputs {
        switch output {
        case .command(let command):
          send(command: command.json)
        case .preview(let message):
          send(message.json, tag: kMsgTagPreview)
        case .scheduleOneShotEnd(let id):
          let lineage = actionLineage.lineage(current: clipboardToken)   // deferred: the lineage it was queued under
          DispatchQueue.main.asyncAfter(deadline: .now() + PreviewBook.oneShotSeconds) { [weak self] in
            guard let self else { return }
            self.action(lineage: lineage) { self.route(self.previews.oneShotEnded(id: id)) }
          }
        case .cancelRoll(let trackId):
          cancelRollGesture(trackId: trackId)
        }
      }
    }
    syncHold()
  }

  /// After any change to the book: publishes the held cell, and runs the keep-alive for the held id (a new hold
  /// samples the mouse button now) or stops it when nothing is held.
  @MainActor private func syncHold() {
    let cell = previews.heldCellId
    if cell != heldCellId { heldCellId = cell }
    guard let held = previews.held else {
      stopKeepAlive()
      return
    }
    guard keepAliveId != held.id else { return }
    holdHasButton = mouseButtonsDown()
    startKeepAlive(id: held.id)
  }

  /// The keep-alive timer's target. The timer retains it and it holds the state weakly, so a running
  /// timer never keeps the state alive. (A target rather than a block: the SDK's block is `@Sendable`,
  /// and this class is not.)
  private final class KeepAliveTarget: NSObject {
    weak var state: ChordLabState?
    let id: Int
    init(state: ChordLabState, id: Int) {
      self.state = state
      self.id = id
    }
    @objc func tick(_ timer: Timer) {
      MainActor.assumeIsolated { state?.keepAliveTick(id: id) }   // scheduled on the main run loop
    }
  }

  /// The held id the keep-alive timer renews, nil while it is stopped.
  private var keepAliveId: Int?

  @MainActor private func startKeepAlive(id: Int) {
    stopKeepAlive()
    keepAliveTicks = 0
    let timer = Timer(timeInterval: Self.keepAliveInterval, target: KeepAliveTarget(state: self, id: id),
                      selector: #selector(KeepAliveTarget.tick(_:)), userInfo: nil, repeats: true)
    RunLoop.main.add(timer, forMode: .common)
    keepAlive = timer
    keepAliveId = id
  }

  @MainActor private func stopKeepAlive() {
    keepAlive?.invalidate()
    keepAlive = nil
    keepAliveId = nil
  }

  /// One tick: the timer is done when `id` is no longer held; otherwise `PreviewKeepAlive` decides
  /// between an `extend` and a release (`releaseHeld`: a roll hold through its gesture's cancel).
  @MainActor private func keepAliveTick(id: Int) {
    guard previews.held?.id == id else { stopKeepAlive(); return }
    keepAliveTicks += 1
    switch PreviewKeepAlive.action(tick: keepAliveTicks, holdHasButton: holdHasButton, buttonDown: mouseButtonsDown()) {
    case .extend: send(PreviewMessage.extend(id: id).json, tag: kMsgTagPreview)
    case .release: releaseHeld()
    }
  }

  // MARK: - Commands (engine wire format, json_api.h)

  /// Sends one command, e.g. `["cmd": "appendChord", "root": 0, "type": "maj"]`; `"schema": 1` is added
  /// here, and `"lineage"`: the document lineage it was computed for (`clipboardToken`, `CommandLineage`, plan Task
  /// 24a). The plugin answers with a snapshot (`kMsgTagSnapshot`) or an error (`kMsgTagError`); a command whose
  /// lineage a state load has since replaced applies nothing and is answered with the loaded document's snapshot.
  /// Every `kMsgTagCommand` goes through here; export requests carry the lineage too (`exportDrag`); previews, panic,
  /// snapshot requests and the heard group's clear carry none.
  @MainActor func send(command: [String: Any]) {
    send(command: command, lineage: actionLineage.lineage(current: clipboardToken))
  }

  /// `send(command:)` with an explicit lineage: deferred work sends with the lineage captured when it was queued.
  @MainActor func send(command: [String: Any], lineage: String) {
    send(CommandLineage.stamp(command, token: lineage), tag: kMsgTagCommand)
  }

  /// Runs one user action (`ActionLineage`): every command and export request it sends, through any helper, carries
  /// the token current at its start (or `lineage`, for deferred work); inside another action it keeps the outer one's.
  @MainActor @discardableResult private func action<T>(lineage: String? = nil, _ body: () -> T) -> T {
    let opened = actionLineage.begin(token: lineage ?? clipboardToken)
    defer { if opened { actionLineage.end() } }
    return body()
  }

  /// Runs a keyboard command, dispatched by the key target (`KeyCommand.route`, tracks design D15 "Keyboard"): undo and
  /// redo are the key bar's commands (one history for chords and notes); with the chords targeted the rest acts on
  /// them (`performOnChords`); with a track's notes targeted on those notes (`NoteKeys`: Delete, ⌘C ⌘X ⌘V ⌘D ⌘A and
  /// the arrows, each at most one command, one undo step). Each does nothing when it has nothing to act on, and the
  /// router consumes every mapped key whether or not it did anything, so the host never acts on it (see
  /// `KeyCommandMap`, `EditorKeyRouter`).
  @MainActor func perform(_ command: KeyCommand) {
    action { performAction(command) }
  }

  @MainActor private func performAction(_ command: KeyCommand) {
    switch command.route(target: keyTarget, openLane: openLane) {
    case .shared:
      send(command: ["cmd": command == .redo ? "redo" : "undo"])
    case .chords:
      performOnChords(command)
    case .notes(let trackId):
      guard let snapshot, let timing = rollTiming else { return }
      let view = rolls.states[trackId]
      for note in NoteKeys.commands(for: command, trackId: trackId, snapshot: snapshot, timing: timing, grid: view.grid,
                                    insertMarker: view.editState.insertMarker, pasteboard: pasteboard) {
        send(command: note.json)
      }
    case .ignored:
      return   // nothing acts on a hidden track, or on the suggestion chips a track's roll hides
    }
  }

  /// A key with the chords targeted: digits append the suggestion chip of that number (nothing when there is no such
  /// chip); Delete removes every selected chord in one step; ⌘C copies the selection, ⌘X cuts it, ⌘V pastes after the
  /// anchor (else at the end), ⌘D duplicates the selection after its last block, ⌘A selects every block (strip editing
  /// design D5, D6; the chords carry their notes, tracks design D21); an arrow is never mapped with the chords
  /// targeted (the host keeps it).
  @MainActor private func performOnChords(_ command: KeyCommand) {
    let selection = snapshot?.selection ?? []
    switch command {
    case .appendSuggestion(let n):
      guard let suggestions = snapshot?.suggestions, suggestions.indices.contains(n - 1) else { return }
      let suggestion = suggestions[n - 1]
      send(command: ["cmd": "appendChord", "root": suggestion.root, "type": suggestion.type])
    case .deleteSelection:
      guard !selection.isEmpty else { return }
      send(command: ClipboardChords.removeCommand(selection))
    case .copy:
      copyChords(selection)
    case .cut:
      cutChords(selection)
    case .paste:
      pasteChords(at: SelectionLogic.insertionIndex(ids: eventIds, anchor: snapshot?.selectedEventId))
    case .duplicate:
      duplicateChords(selection)
    case .selectAll:
      select(SelectionLogic.all(eventIds))
    case .undo, .redo, .nudge:
      break   // undo and redo are routed as shared; arrows are mapped only with notes targeted
    }
  }

  /// A press on the strip, a palette or home-row cell or a suggestion chip (tracks design D15 "Keyboard"): the keys
  /// act on the chords from now on.
  @MainActor func targetChords() {
    if keyTarget != .chords { keyTarget = .chords }
  }

  // MARK: - Strip editing (the clipboard and the selection, strip editing design D5–D7)

  /// Where the clipboard is written and read (`ChordPasteboard`): the general pasteboard; a harness may put a fake
  /// here.
  var pasteboard: any ChordPasteboard = NSPasteboard.general

  /// The clipboard token of the plug-in instance's document lineage (tracks design D21, `ClipboardToken`): made by
  /// the C++ shell and passed in by the view controller, so it outlives this state (an editor window closed and
  /// reopened keeps it), and set again by the view controller when loaded state replaced the document (the shell
  /// renews it then, before the loaded snapshot arrives). A chord copy carries it, and a paste whose payload carries
  /// another (another instance, or this one before the load: track ids that mean nothing here) drops the notes'
  /// track ids so the engine matches lanes by name and role or creates tracks. A blank value resolves to a fresh one
  /// (`ClipboardToken.resolve`).
  ///
  /// It is also the document lineage every command and export request carries (`CommandLineage`, plan Task 24a). A
  /// change means another document (the shell renews it before the loaded snapshot): what was waiting for a reply to
  /// a command sent for the old one is dropped (`lineageChanged`).
  var clipboardToken: String {
    get { resolvedClipboardToken }
    set {
      let change = ClipboardToken.change(from: resolvedClipboardToken, to: newValue)
      resolvedClipboardToken = change.token
      if change.changed { lineageChanged() }
    }
  }
  private var resolvedClipboardToken: String

  /// The document was replaced (Task 24a review): a stale command's answer is the loaded document's snapshot, not an
  /// error, so nothing else would forget what its command left waiting. The pending track add (the loaded document's
  /// tracks are not its answer), the pending key change (a stale `setKey`; a loaded document already in the target key
  /// would otherwise show a hint built from the old tonic and write `setKeyHintSeen` into it) and the row under the key
  /// bar (the hint and the moved note belong to the old document) go now, before the loaded snapshot is applied.
  private func lineageChanged() {
    laneOpener.refused()
    pendingKeyChange = nil
    if keyRow != nil { keyRow = nil }
  }

  /// The ids of the strip's blocks in order; empty before the first snapshot.
  private var eventIds: [Int] { snapshot?.events.map(\.id) ?? [] }

  /// ⌘C and the block menu's Copy: the chords `ids` (in strip order, whatever the order given) on the pasteboard
  /// as one item in both forms (`ClipboardChords.copy`). Returns whether the pasteboard took it; false, the
  /// pasteboard untouched, when none of them is on the strip.
  @MainActor @discardableResult func copyChords(_ ids: [Int]) -> Bool {
    guard let events = snapshot?.events else { return false }
    return ClipboardChords.copy(ids, from: events, tracks: snapshot?.tracks, token: clipboardToken, to: pasteboard)
  }

  /// ⌘X and the block menu's Cut: the copy, then `removeChords` (one undo step), sent only once the copy is
  /// published (`ClipboardChords.cut`); a failed copy removes nothing.
  @MainActor func cutChords(_ ids: [Int]) {
    guard let events = snapshot?.events,
          let removal = ClipboardChords.cut(ids, from: events, tracks: snapshot?.tracks, token: clipboardToken,
                                            to: pasteboard) else { return }
    send(command: removal)
  }

  /// Whether the pasteboard holds ChordLab's chords (Paste after is disabled otherwise).
  @MainActor var canPaste: Bool {
    ClipboardChords.paste(from: pasteboard) != nil
  }

  /// ⌘V and Paste after: inserts the pasteboard's chords with their notes at `index` (one undo step; they become the
  /// selection). A payload another instance copied loses its notes' track ids (`ClipboardChords.paste(from:ownToken:)`,
  /// D21). A pasteboard without ChordLab's chords (text only, or nothing) pastes nothing; a paste past 256 chords, an
  /// ambiguous lane or no room for a new track is the engine's refusal, shown like any other (`lastError`), and the
  /// pasteboard keeps its content.
  @MainActor func pasteChords(at index: Int) {
    guard let chords = ClipboardChords.paste(from: pasteboard, ownToken: clipboardToken) else { return }
    send(command: ClipboardChords.insertCommand(chords, at: index))
  }

  /// ⌘D and the block menu's Duplicate: the chords `ids` again with their notes (each to its own track, D21), right
  /// after the last of them, without touching the pasteboard.
  @MainActor func duplicateChords(_ ids: [Int]) {
    guard let events = snapshot?.events,
          let command = ClipboardChords.duplicateCommand(ids, events: events, tracks: snapshot?.tracks) else { return }
    send(command: command)
  }

  /// Makes `selection` the store's (one `selectEvent` or `selectEvents`), unless it already is.
  @MainActor func select(_ selection: StripSelection) {
    guard selection.ids != snapshot?.selection || selection.anchor != snapshot?.selectedEventId else { return }
    send(command: SelectionLogic.command(selection))
  }

  // MARK: - Lanes and tracks (the lane tab row, tracks design D13)

  /// A tab click: shows `lane`, resolved against the snapshot's tracks (a lane the snapshot lacks opens Chords).
  @MainActor func open(_ lane: Lane) {
    let resolved = lane.resolved(in: snapshot?.tracks)
    if resolved != openLane { openLane = resolved }
  }

  /// The + menu: `addTrack {role}` (the engine names the track, D2); its answering snapshot opens the new track.
  /// Nothing at the track limit or for an engine without tracks.
  @MainActor func addTrack(role: TrackRole) {
    guard LaneTabs.canAddTrack(snapshot), let tracks = snapshot?.tracks else { return }
    laneOpener.addSent(trackIds: tracks.map(\.id))
    send(command: LaneTabs.addCommand(role: role))
  }

  /// The track menu's Remove: `removeTrack {id}`, no confirmation (undoable); an open track falls back to Chords
  /// with the answering snapshot.
  @MainActor func removeTrack(id: Int) {
    send(command: LaneTabs.removeCommand(trackId: id))
  }

  /// The inline rename's commit: `patchTrack {id, name}` under the section rename's rule
  /// (`LaneTabs.renameCommand`); a blank or unchanged draft sends nothing.
  @MainActor func renameTrack(id: Int, draft: String) {
    guard let current = snapshot?.track(id: id),
          let command = LaneTabs.renameCommand(trackId: id, draft: draft, current: current.name) else { return }
    send(command: command)
  }

  /// The Role submenu: `patchTrack {id, role}`; the checked role sends nothing.
  @MainActor func setTrackRole(id: Int, role: TrackRole) {
    guard let current = snapshot?.track(id: id),
          let command = LaneTabs.roleCommand(trackId: id, role: role, current: current.role) else { return }
    send(command: command)
  }

  /// Move left / Move right: `moveTrack {id, index}`, `index` the track's place after the move.
  @MainActor func moveTrack(id: Int, to index: Int) {
    send(command: LaneTabs.moveCommand(trackId: id, index: index))
  }

  /// A tab's M toggle: `setChordsMute {on}` or `setTrackMute {id, on}`; the answering snapshot flips it.
  @MainActor func setMute(_ lane: Lane, on: Bool) {
    send(command: LaneTabs.muteCommand(lane: lane, on: on))
  }

  /// A tab's S toggle: `setChordsSolo {on}` or `setTrackSolo {id, on}`; the answering snapshot flips it.
  @MainActor func setSolo(_ lane: Lane, on: Bool) {
    send(command: LaneTabs.soloCommand(lane: lane, on: on))
  }

  /// After a snapshot is decoded: the lane `LaneOpener` gives (the track a pending add created, else the open lane
  /// while its track exists, else Chords); a change resets the key target in `openLane`'s observer.
  private func updateOpenLane(for snapshot: Snapshot) {
    let lane = laneOpener.lane(afterSnapshotWith: snapshot.tracks, open: openLane)
    if lane != openLane { openLane = lane }
  }

  // MARK: - Piano roll view state (tracks design D14; UI-only, nothing is sent)

  /// Track `trackId`'s roll view state; the defaults before its roll first appears.
  func rollState(trackId: Int) -> RollViewState { rolls.states[trackId] }

  /// The roll of track `trackId` measured its grid (`PianoRollView`'s `onGeometryChange`): stores the viewport and
  /// fits the track's scroll to it; the first measurement with a real size is the track's first open, which
  /// scrolls to bar 1 and the track's active notes, or its role's register when it has none (`RollViewState.fit`).
  /// Nothing for a track the snapshot lacks.
  @MainActor func setRollViewport(trackId: Int, _ size: CGSize) {
    guard let snapshot, let timing = rollTiming, let track = snapshot.track(id: trackId) else { return }
    var states = rolls.states
    states.setViewport(size, trackId: trackId, role: track.role, timing: timing, notes: RollNoteSpan(notes: track.notes))
    publishRolls(states)
  }

  /// Changes track `trackId`'s roll view state with the current viewport and the snapshot's timing; the one path
  /// every roll setter takes (and the input view's, plan Task 18). Nothing before the first snapshot, and nothing
  /// for a track the snapshot lacks (no entry is made for it).
  @MainActor func editRoll(trackId: Int, _ change: (inout RollViewState, CGSize, RollTiming) -> Void) {
    guard let snapshot, let timing = rollTiming, snapshot.track(id: trackId) != nil else { return }
    var states = rolls.states
    states.edit(trackId: trackId, timing: timing, change)
    publishRolls(states)
  }

  /// Scrolls track `trackId`'s roll to `scroll` (content coordinates of the grid's top-left), clamped.
  @MainActor func setRollScroll(trackId: Int, _ scroll: CGPoint) {
    editRoll(trackId: trackId) { $0.setScroll(scroll, viewport: $1, timing: $2) }
  }

  /// Scrolls track `trackId`'s roll by `delta` points, clamped (the scroll wheel, plan Task 18).
  @MainActor func scrollRoll(trackId: Int, by delta: CGSize) {
    editRoll(trackId: trackId) { $0.scroll(by: delta, viewport: $1, timing: $2) }
  }

  /// Time zoom by `factor` about the grid's view x `aboutViewX` (⇧⌥-scroll, pinch; plan Task 18).
  @MainActor func zoomRoll(trackId: Int, horizontally factor: CGFloat, aboutViewX: CGFloat) {
    editRoll(trackId: trackId) { $0.zoom(horizontally: factor, aboutViewX: aboutViewX, viewport: $1, timing: $2) }
  }

  /// Row zoom by `factor` about the grid's view y `aboutViewY` (⌥-scroll; plan Task 18).
  @MainActor func zoomRoll(trackId: Int, vertically factor: CGFloat, aboutViewY: CGFloat) {
    editRoll(trackId: trackId) { $0.zoom(vertically: factor, aboutViewY: aboutViewY, viewport: $1, timing: $2) }
  }

  /// The toolbar's zoom + and −: time ×2 or ÷2 about the grid's centre.
  @MainActor func zoomRollIn(trackId: Int) {
    editRoll(trackId: trackId) { $0.zoomIn(viewport: $1, timing: $2) }
  }

  @MainActor func zoomRollOut(trackId: Int) {
    editRoll(trackId: trackId) { $0.zoomOut(viewport: $1, timing: $2) }
  }

  /// The toolbar's Grid, Snap pitch, Mode and Other tracks.
  @MainActor func setRollGrid(trackId: Int, _ grid: RollGrid) {
    editRoll(trackId: trackId) { state, _, _ in state.grid = grid }
  }

  @MainActor func setRollSnap(trackId: Int, _ snap: PitchSnap) {
    editRoll(trackId: trackId) { state, _, _ in state.snap = snap }
  }

  @MainActor func setRollMode(trackId: Int, _ mode: RollMode) {
    editRoll(trackId: trackId) { state, _, _ in state.mode = mode }
  }

  @MainActor func setRollShowsOtherTracks(trackId: Int, _ on: Bool) {
    editRoll(trackId: trackId) { state, _, _ in state.showsOtherTracks = on }
  }

  /// The View menu's guide choice (`GuideDisplay`): stores it in `defaults` under `GuideDisplay.storageKey` and
  /// publishes it, so every roll draws it; nothing is sent.
  @MainActor func setGuideDisplay(_ display: GuideDisplay) {
    defaults.set(display.rawValue, forKey: GuideDisplay.storageKey)
    if display != guideDisplay { guideDisplay = display }
  }

  /// Re-reads `guideDisplay` whenever `defaults` changes (another editor's choice); publishes only a change.
  private func observeDefaults() {
    defaultsObserver = NotificationCenter.default.addObserver(forName: UserDefaults.didChangeNotification,
                                                              object: defaults, queue: .main) { [weak self] _ in
      guard let self else { return }
      let stored = GuideDisplay(rawValue: self.defaults.string(forKey: GuideDisplay.storageKey))
      if stored != self.guideDisplay { self.guideDisplay = stored }
    }
  }

  /// Publishes `states` only when it changed, so an unchanged fit redraws nothing.
  private func publishRolls(_ states: RollViewStates) {
    if states != rolls.states { rolls.states = states }
  }

  // MARK: - Piano roll input (plan Task 18: RollInputView → the gesture machine → one effects seam)

  /// Track `trackId`'s grid view point (the input view's coordinates, top-left origin) in content coordinates, at
  /// the track's current zoom and scroll.
  func rollContentPoint(trackId: Int, viewPoint: CGPoint) -> CGPoint {
    rolls.states.contentPoint(viewPoint: viewPoint, trackId: trackId, ticksPerBeat: rollTiming?.ticksPerBeat ?? 960)
  }

  /// The cursor over track `trackId`'s grid view point (`RollCursor`: resize over a note's edge in Notes mode).
  func rollCursor(trackId: Int, viewPoint: CGPoint) -> RollCursor {
    guard let snapshot, let timing = rollTiming else { return .arrow }
    return rolls.states.cursor(atView: viewPoint, trackId: trackId, snapshot: snapshot, timing: timing)
  }

  /// One pointer event of track `trackId`'s roll (`RollInputView`, location already in content coordinates): runs
  /// the gesture machine with the current snapshot and the track's view state (`RollViewStates.handle`), stores the
  /// next edit state through the roll store (so the drag preview, hidden notes, rubber band and insert marker draw)
  /// and hands the effects to `performRollEffects`, the one place they go.
  @MainActor func handleRollPointer(_ event: RollPointerEvent, trackId: Int) {
    guard let snapshot, let timing = rollTiming else { return }
    // Design D15 "Keyboard": the roll's mouse-down targets its notes.
    if event.phase == .down, keyTarget != .notes(trackId: trackId) { keyTarget = .notes(trackId: trackId) }
    // A press here ends a key's or a cell's held preview (and another track's roll hold) before the machine runs, so
    // a press that starts no preview of its own never leaves one sounding (`PreviewBook.rollPressed`).
    action {
      if event.phase == .down { route(previews.rollPressed(trackId: trackId)) }
      var states = rolls.states
      let effects = states.handle(event, trackId: trackId, snapshot: snapshot, timing: timing)
      publishRolls(states)
      performRollEffects(effects, trackId: trackId)
    }
  }

  /// Cancels track `trackId`'s gesture: the open lane left it, or the preview book ended its hold (`.cancelRoll`:
  /// another owner took the held preview, the keep-alive found no button down, the editor closed, panic; the book has
  /// already stopped that hold, so the stop handed back sends nothing). Its drag preview drops and a held preview's
  /// stop goes through `performRollEffects`, the one place that stops it. Nothing when it is idle.
  @MainActor func cancelRollGesture(trackId: Int) {
    var states = rolls.states
    let effects = states.cancelGesture(trackId: trackId)
    publishRolls(states)
    performRollEffects(effects, trackId: trackId)
  }

  /// THE seam every effect of the roll's gesture machine goes through (`RollEffect`: from `handleRollPointer`, a
  /// cancel and a snapshot's revision cancel), and the only one (tracks design D15 "Commit" and "Preview"): `send`
  /// effects go out as `send(command:)` (each content command carries its `baseRevision`); `previewStart` holds the
  /// note's pitch through the existing preview path (`kMsgTagPreview`, one pitch, paired Start and Stop, keep-alive)
  /// after ending any other owner's hold; `previewStop` stops this track's hold, never a second time;
  /// `previewOneShot` sounds for `PreviewBook.oneShotSeconds` under its own id. Every decision is `PreviewBook.perform`'s;
  /// this only routes its outputs.
  ///
  /// The plug-in answers a command inline, inside `send` (the next snapshot is applied before `send` returns), so a
  /// selection echo or a commit's new revision reaches the roll here, after the effects' state was published.
  ///
  /// `rollEffectsObserver` (nil in production) sees every call first, so a test or the harness can check what reaches
  /// the seam.
  @MainActor func performRollEffects(_ effects: [RollEffect], trackId: Int) {
    rollEffectsObserver?(effects, trackId)
    route(previews.perform(effects, trackId: trackId))
  }

  /// The toolbar's nudge ◀ ▶ ▲ ▼ for track `trackId` (`RollNudge.command`): one `updateNotes` of the selected notes
  /// by one grid step or semitone with the snapshot's revision (one undo step); nothing without a selection on that
  /// track or when the clamp leaves nothing to move (the button is disabled then).
  @MainActor func nudgeRoll(trackId: Int, _ direction: ArrowDirection) {
    guard let snapshot, let timing = rollTiming,
          let command = RollNudge.command(direction, trackId: trackId, snapshot: snapshot, timing: timing,
                                          grid: rolls.states[trackId].grid) else { return }
    send(command: command.json)
  }

  /// The toolbar's Generate menu for track `trackId` (`GenerateOptions`): the chosen entry's one `generateBass` (its
  /// pattern, and the chord ids it was offered for, or none for every chord): one undo step, no confirmation. Nothing
  /// when the snapshot and document lineage (`clipboardToken`) current now no longer offer the entry
  /// (`GenerateOptions.command(for:trackId:in:lineage:)`: the menu was drawn for another selection, or for the
  /// document a state load has since replaced). A refusal ("Add chords first", the track limits) comes back as any
  /// refused command does (`applyError`, `lastError`).
  ///
  /// The engine answers inline, inside the send (its new snapshot, or an error), so afterwards the roll shows what was
  /// written: the track's active notes in the entry's chords (every chord's for a whole-progression entry), from the
  /// snapshot current now, are revealed (`RollViewState.reveal`). Nothing scrolls when nothing was sent, when the
  /// answer was an error or no snapshot, when the document lineage changed meanwhile (a stale command is answered with
  /// another document's snapshot), or when those chords hold no active note.
  @MainActor func generateBass(trackId: Int, _ entry: GenerateEntry) {
    guard let snapshot,
          let command = GenerateOptions.command(for: entry, trackId: trackId, in: snapshot,
                                                lineage: clipboardToken) else { return }
    let errors = errorSerial
    let snapshots = snapshotSerial
    let lineage = clipboardToken
    send(command: command)
    guard errorSerial == errors, snapshotSerial != snapshots, clipboardToken == lineage,
          let written = self.snapshot?.track(id: trackId),
          let span = RollNoteSpan(notes: written.notes, anchors: entry.scope.chordIds) else { return }
    editRoll(trackId: trackId) { $0.reveal(span, viewport: $1, timing: $2) }
  }

  /// Whether the pasteboard holds ChordLab notes (the roll menu's Paste here is disabled otherwise).
  @MainActor var canPasteNotes: Bool {
    NoteClipboard.paste(from: pasteboard) != nil
  }

  /// The roll's context menu for a right-click or ⌃-click at content `point` of track `trackId` (`RollContextMenu`):
  /// targets that track's notes (like the roll's mouse-down), sends the right-clicked note's selection when it was
  /// not selected (clearing the insert marker, as a note press does: `RollContextMenu.editState(afterOpening:)`), and
  /// returns the model the view turns into an `NSMenu`; nil for a track the snapshot lacks.
  @MainActor func rollContextMenu(trackId: Int, at point: CGPoint) -> RollContextMenu? {
    guard let snapshot, let timing = rollTiming else { return nil }
    let view = rolls.states[trackId]
    guard let menu = RollContextMenu.make(at: point, trackId: trackId, snapshot: snapshot, timing: timing,
                                          geometry: view.geometry(ticksPerBeat: timing.ticksPerBeat), grid: view.grid,
                                          notesOnPasteboard: canPasteNotes) else { return nil }
    if keyTarget != .notes(trackId: trackId) { keyTarget = .notes(trackId: trackId) }
    editRoll(trackId: trackId) { state, _, _ in state.editState = menu.editState(afterOpening: state.editState) }
    if let select = menu.select { send(command: select.json) }
    return menu
  }

  /// A roll context-menu item chosen: what it sends against the snapshot current now (`RollContextMenu.commands`).
  @MainActor func performRollMenu(_ item: RollContextMenu.Item, _ menu: RollContextMenu) {
    guard let snapshot, let timing = rollTiming else { return }
    action {
      for command in menu.commands(for: item, snapshot: snapshot, timing: timing, pasteboard: pasteboard) {
        send(command: command.json)
      }
    }
  }

  /// Sees every call of `performRollEffects` (the effects, in order, and the track); nil in production. Tests and the
  /// harness set it; it never sends anything itself.
  var rollEffectsObserver: (@MainActor ([RollEffect], Int) -> Void)?

  /// A snapshot arrived: every track's stored gesture checks its revision (`RollViewStates.snapshotChanged`: a
  /// different revision cancels, a selection echo does not), the open roll's and every parked one's.
  @MainActor private func cancelStaleRollGestures(revision: UInt64) {
    var states = rolls.states
    let cancelled = states.snapshotChanged(revision: revision)
    publishRolls(states)
    for (trackId, effects) in cancelled { performRollEffects(effects, trackId: trackId) }
  }

  /// After a snapshot: drops removed tracks' roll states and fits the open track's to the new content.
  private func updateRolls(for snapshot: Snapshot) {
    var openTrackId: Int?
    if case .track(let id) = openLane { openTrackId = id }
    var states = rolls.states
    states.snapshotApplied(tracks: snapshot.tracks ?? [], timing: rollTiming ?? RollTiming(snapshot: snapshot),
                           openTrackId: openTrackId)
    publishRolls(states)
  }

  // MARK: - Project key (the Key pickers) and the transpose row

  /// Picking a tonic or a mode: sets the project key, spelled as the snapshot's table spells that pitch
  /// class in that mode, and brings the browsed key home, so the palette follows (the plug-in keeps a home
  /// browse home by itself, document.h; the `browseKey {home}` is for a palette browsing elsewhere). Any
  /// shown row closes.
  /// The hint (or the moved note) is decided now, from the snapshot before the change, and shown once the
  /// change's own snapshot arrives (`applySnapshot`: the first with the new tonic that is home, i.e. the
  /// `setKey` reply when the palette was home, else the `browseKey {home}` reply), never here: the plug-in
  /// answers each command inline, inside `send`, but this works as well when a reply comes later.
  @MainActor func changeProjectKey(pc: Int, mode: String) {
    guard let snapshot, let tonic = snapshot.tonics(for: mode)?[pc] else { return }
    let fromPc = KeyNames.pitchClass(of: snapshot.projectKey.tonic)
    let switchOn = snapshot.transposeWithKey ?? false
    let count = snapshot.events.count
    keyRow = nil
    pendingKeyChange = (fromPc: fromPc, toPc: pc,
                        showHint: KeyTransposeHint.shows(fromPc: fromPc, toPc: pc, transposeWithKey: switchOn,
                                                         eventCount: count, hintSeen: snapshot.keyHintSeen ?? false),
                        transposing: switchOn && fromPc != pc, count: count)
    action {   // one lineage: a stale setKey's answer must not let the browseKey through into the loaded document
      send(command: ["cmd": "setKey", "letter": tonic.letter, "alter": tonic.alter, "mode": mode])
      send(command: ["cmd": "browseKey", "home": true])
    }
  }

  // MARK: - What the plug-in heard (the Heard group, design D11–D13)

  /// The group's Use: makes the heard key the project key through the Key pickers' own entry
  /// (`changeProjectKey`), so the browse comes home and Transpose with key, its hint and the moved note
  /// behave exactly as after a pick. Nothing without a heard key.
  @MainActor func useHeardKey() {
    guard let key = HeardCommands.use(heard) else { return }
    changeProjectKey(pc: key.pc, mode: key.mode)
  }

  /// The group's Add: appends the heard chord as it was played (`appendChord` with its inversion, which the
  /// engine marks manual, D11), held or already released (D10; `HeardCommands.add`). Nothing without a chord.
  @MainActor func addHeardChord() {
    guard let append = HeardCommands.add(heard) else { return }
    send(command: append.command)
  }

  /// The group's ×: the listener forgets everything (`kMsgTagClearHeard`, no payload, D13); the plug-in
  /// answers with an inactive heard push, which hides the group.
  @MainActor func clearHeard() {
    sendCommand(kMsgTagClearHeard, Data())
  }

  /// The transport row's Thru switch: the snapshot's `midiThru` (off when an older plug-in sends none; the
  /// switch is hidden then), set with `setMidiThru`. The answering snapshot is what flips it.
  @MainActor var thru: Binding<Bool> {
    Binding(get: { self.snapshot?.midiThru ?? false },
            set: { self.send(command: ["cmd": "setMidiThru", "on": $0]) })
  }

  /// The hint's Move once (`always` false) and Move and always transpose (`always` true): moves the chords
  /// from the hint's old tonic, provided the sequence is still the one the hint was shown for and the
  /// project key is still the one browsed (`KeyTransposeHint.stillValid`).
  @MainActor func moveChordsWithKey(always: Bool) {
    guard case .hint(let fromPc, let basis)? = keyRow else { return }
    guard let snapshot, KeyTransposeHint.stillValid(basis, in: snapshot) else {
      keyRow = nil
      return
    }
    keyRow = nil
    action {   // one lineage: fromPc is the old document's tonic
      if always { send(command: ["cmd": "setTransposeWithKey", "on": true]) }
      send(command: ["cmd": "transposeFromKey", "fromTonicPc": fromPc])
    }
  }

  /// The hint's Keep chords: closes it; the chords stay as they are.
  @MainActor func keepChords() {
    keyRow = nil
  }

  /// After a snapshot is decoded: resolves a pending key change by its own snapshot (the first whose
  /// project tonic is the new one and whose browsed key is the project key: the plug-in's `setKey` reply
  /// when the palette was home, else the `browseKey {home}` reply that follows, so a row is never published
  /// only to be dropped by the next snapshot), else drops a hint the snapshot no
  /// longer matches. The row belongs to the moment of the key change: any snapshot browsing another key
  /// closes it, the hint (`KeyTransposeHint.stillValid`) and the moved note alike, and the moved note also
  /// closes when the project tonic is no longer the one it names (⌘Z of the key change, after which the
  /// plug-in keeps the browse home: document.h).
  private func updateKeyRow(for snapshot: Snapshot) {
    if let pending = pendingKeyChange, KeyNames.pitchClass(of: snapshot.projectKey.tonic) == pending.toPc,
       KeyTransposeHint.isHome(snapshot) {
      pendingKeyChange = nil
      if pending.showHint {
        keyRow = .hint(fromPc: pending.fromPc, basis: KeyTransposeHint.basis(of: snapshot))
        // Once per project, even if closed at once. On a later main-queue turn: this runs inside a
        // command's reply (setKey, or the browseKey {home} after it), and the plug-in would answer
        // setKeyHintSeen inline, re-entering here.
        // The lineage of the snapshot that resolved the key change (its token always arrives first).
        let lineage = clipboardToken
        DispatchQueue.main.async { [weak self] in
          self?.send(command: ["cmd": "setKeyHintSeen", "on": true], lineage: lineage)
        }
      } else if pending.transposing && pending.count > 0 {
        showMovedNote(count: pending.count, tonic: KeyNames.tonicName(snapshot.projectKey.tonic))
      }
      return
    }
    switch keyRow {
    case .hint(_, let basis)? where !KeyTransposeHint.stillValid(basis, in: snapshot):
      keyRow = nil
    case .moved(_, let tonic)? where !KeyTransposeHint.isHome(snapshot)
      || KeyNames.tonicName(snapshot.projectKey.tonic) != tonic:
      keyRow = nil
    default:
      break
    }
  }

  /// Shows the moved note for `movedNoteDuration`; its timer clears that note only.
  private func showMovedNote(count: Int, tonic: String) {
    movedNoteSerial += 1
    let serial = movedNoteSerial
    let note = KeyRow.moved(count: count, tonic: tonic)
    keyRow = note
    DispatchQueue.main.asyncAfter(deadline: .now() + Self.movedNoteDuration) { [weak self] in
      guard let self, self.movedNoteSerial == serial, self.keyRow == note else { return }
      self.keyRow = nil
    }
  }

  /// JSON-encodes `fields` plus `"schema": 1` and sends it under `tag`; false (and an error shown) when it is not valid
  /// JSON.
  @MainActor @discardableResult private func send(_ fields: [String: Any], tag: Int) -> Bool {
    var message = fields
    message["schema"] = 1
    guard JSONSerialization.isValidJSONObject(message),
          let data = try? JSONSerialization.data(withJSONObject: message) else {
      reportError("message is not valid JSON: \(fields)")
      return false
    }
    sendCommand(tag, data)
    return true
  }

  /// Asks the plugin to push its current snapshot.
  @MainActor func requestSnapshot() {
    sendCommand(kMsgTagRequestSnapshot, Data())
  }

  /// Asks the plugin to write a clip (`kMsgTagExportClip`, tracks design D12): `request`'s lanes over the whole
  /// progression or its section, and returns what it came to (`ClipExportExchange`): the shell answers inline inside
  /// the send (`OnMessage` runs synchronously on the main thread), so the outcome is `.ready` with the new file or
  /// `.failed` after a `kMsgTagError`, and `.pending` only if no answer arrived. `exportPath` is cleared first and holds
  /// the file once it arrives. The legacy `kMsgTagExport` / `kMsgTagExportSection` messages stay in the shell (D12) but
  /// the UI no longer sends them: the chords lane through this path is byte-identical to them.
  @MainActor @discardableResult func requestExportClip(_ request: ClipExportRequest) -> ExportOutcome {
    exportDrag(request).outcome
  }

  /// The body of every drag-out's `.onDrag` (the lane grips, All, the bottom drag-out, the section handles): the
  /// request `grip` makes against the current snapshot and open lane (`ClipExport.request`), its outcome and the item
  /// provider for the file this request produced (`ClipExportExchange.drag`). With nothing to export nothing is sent;
  /// on a failure or with no answer the provider is empty. The round trip and its decisions are Core's, tested there
  /// (ClipExportExchangeTests); this only wires the sender.
  @MainActor func dragExport(_ grip: ExportGrip) -> ExportDrag {
    exportDrag(ClipExport.request(for: grip, in: snapshot, open: openLane))
  }

  @MainActor private func exportDrag(_ request: ClipExportRequest?) -> ExportDrag {
    exports.drag(request) { request in
      exportPath = nil
      // The export reads the document: it carries the lineage too, and the shell writes nothing for a stale one (it
      // answers with the current snapshot and then an error, which fails this exchange: no file is offered).
      return send(CommandLineage.stamp(request.fields, token: actionLineage.lineage(current: clipboardToken)),
                  tag: kMsgTagExportClip)
    }
  }

  // MARK: - Messages from the plugin

  /// Counts the snapshots applied, so an action can tell whether its command was answered with one (`generateBass`).
  private var snapshotSerial = 0

  /// `kMsgTagSnapshot` payload.
  func applySnapshot(json: Data) {
    do {
      let decoded = try JSONDecoder().decode(Snapshot.self, from: json)
      snapshotSerial += 1
      snapshot = decoded
      lastError = nil
      // Snapshots are applied on the main thread (the controller's message handler, the harness).
      MainActor.assumeIsolated { cancelStaleRollGestures(revision: decoded.revision ?? 0) }
      let target = keyTarget.resolved(in: decoded.tracks)
      if target != keyTarget { keyTarget = target }
      updateOpenLane(for: decoded)
      updateRolls(for: decoded)
      updateKeyRow(for: decoded)
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

  /// `kMsgTagHeard` payload. A payload that does not decode is reported and leaves the last one shown.
  func applyHeard(json: Data) {
    do {
      // Only a changed value is published: an equal push is not republished and the views do not redraw.
      let decoded = try JSONDecoder().decode(HeardJSON.self, from: json)
      if decoded != heard { heard = decoded }
    } catch {
      reportError("heard decode failed: \(error)")
    }
  }

  /// `kMsgTagExportReady` payload: the export request in flight gets it as its answer (`ClipExportExchange`).
  func applyExportReady(json: Data) {
    if let ready = try? JSONDecoder().decode(ExportReadyJSON.self, from: json) {
      exportPath = ready.path
      exports.receivedPath(ready.path)
    }
  }

  /// `kMsgTagError` payload. An error answering the export request in flight is that export's failure
  /// (`ClipExportExchange.receivedError`): only its text shows. Otherwise it is a refused command: a key change still
  /// waiting for its snapshot was most likely it, so it is dropped (a later snapshot with that tonic, an undo or a
  /// redo, shows nothing), and so is a pending track add (a refused add opens nothing).
  func applyError(json: Data) {
    let message = (try? JSONDecoder().decode(ErrorJSON.self, from: json))?.error ?? String(decoding: json, as: UTF8.self)
    if !exports.receivedError(message) {
      pendingKeyChange = nil
      laneOpener.refused()
    }
    reportError(message)
  }

  private func reportError(_ message: String) {
    lastError = message
    errorSerial += 1
  }
}

/// The piano rolls' UI state (`ChordLabState.rolls`, tracks design D14): every track's `RollViewState` and the roll's
/// viewport, published on their own so a scroll redraws the roll alone, plus the guide band layout cache the roll
/// reads in its body (rebuilt per snapshot revision, zoom and track role; never published).
final class RollViewStore: ObservableObject {
  @Published var states = RollViewStates()
  let bands = GuideBandsCache()
}
