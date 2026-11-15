import SwiftUI

/// UI-side state. Logic lives in C++; this object only mirrors what the plugin
/// pushes (snapshot, error, status, transport, export-ready and heard JSON) and forwards user intent
/// (commands, previews, export requests, forgetting what was heard) back.
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
  /// The host tempo from the last transport push (`TransportJSON.tempo`), 120 until the first one (design D4):
  /// what Record turns a held time into beats with.
  var bpm: Double { transport?.tempo ?? 120 }
  /// The MIDI file of the last export request (`kMsgTagExportReady`), or nil while none is ready.
  @Published var exportPath: String?
  /// What the plug-in hears on its MIDI input (`kMsgTagHeard`, design D14); nil until the first push. The
  /// Heard group (`HeardView`) shows only while `active` is true.
  @Published var heard: HeardJSON?
  /// Whether a text field (a section rename) is being edited: the key routes (`EditorKeyRouter`) stand
  /// down meanwhile, so typing a name never appends or removes chords.
  @Published var editingText = false
  /// What the keyboard edits (tracks design D15 "Keyboard", `KeyTarget`): the chords, or one track's notes; the key
  /// routes ask the map with it (`EditorKeyRouter`). Every applied snapshot resolves it (`KeyTarget.resolved`: a
  /// removed track's notes fall back to the chords), and every open-lane change resets it
  /// (`KeyTarget.afterOpenLaneChange`, in `openLane`'s observer). The roll's mouse-down will set `.notes` (plan
  /// Tasks 18, 20); until then it stays `.chords`.
  @Published var keyTarget: KeyTarget = .chords
  /// The lane the editor shows (tracks design D13): Chords (suggestions, palette, home row) or one track's roll.
  /// UI state, never saved; every applied snapshot resolves it (`Lane.resolved`: a removed track falls back to
  /// Chords). Changing it resets the key target, so a key never acts on a lane that has just been hidden.
  @Published var openLane: Lane = .chords {
    didSet {
      guard openLane != oldValue else { return }
      let target = keyTarget.afterOpenLaneChange(from: oldValue, to: openLane)
      if target != keyTarget { keyTarget = target }
    }
  }
  /// The track ids when an `addTrack` was sent, until its answer: the next snapshot opens the one new track
  /// (`LaneTabs.newTrackId`); a refusal (`applyError`) forgets it.
  private var pendingAddTrackIds: [Int]?
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

  /// Id of the palette cell whose voicing is sounding (pressed), or nil.
  @Published private(set) var heldCellId: String?
  /// Preview id of the held cell: a per-press counter, so that Start and Stop pair up in the scheduler.
  private var heldPreviewId: Int?
  /// The transport row's Record switch (strip editing design D9): while on, releasing a palette or home-row cell
  /// inserts its chord after the anchor (else at the end), as long as the cell was held. UI state: off whenever
  /// the window opens, never saved.
  @Published var recording = false
  private var previewCounter = 0
  /// While a cell is held, an `extend` for its preview every `keepAliveInterval` from a main-run-loop
  /// timer in the common modes (so it fires during mouse tracking). The scheduler ends a preview four
  /// seconds of processed audio after its Start or its last applied Extend, so it ends a held one only
  /// when the extends stop reaching it: a main thread stalled for several seconds (the cell then shows
  /// pressed until released and pressed again). It also ends a preview whose release never arrived;
  /// while the host processes no audio nothing expires. Each tick also stands in for a release the view
  /// never reported (`PreviewKeepAlive`): a hold that began with a mouse button down is released once no
  /// button is down; one that began without (touch input is outside the button mask) is renewed for at
  /// most `PreviewKeepAlive.maxTicksWithoutButton` ticks. Invalidated on release, panic and deinit,
  /// always on the main thread.
  static let keepAliveInterval: TimeInterval = 1.0
  private var keepAlive: Timer?
  /// Whether a mouse button was down when the held cell was pressed, and the keep-alive ticks since.
  private var holdHasButton = false
  private var keepAliveTicks = 0
  /// The physical button state (`NSEvent.pressedMouseButtons`); a harness that synthesizes events
  /// passes its own.
  private let mouseButtonsDown: () -> Bool

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
    super.init()
  }

  public init(beginEdit: @MainActor @escaping (Int) -> Void = { _ in },
              doEdit: @MainActor @escaping (Int, Double) -> Void = { _, _ in },
              endEdit: @MainActor @escaping (Int) -> Void = { _ in },
              sendCommand: @MainActor @escaping (Int, Data?) -> Void = { _, _ in },
              mouseButtonsDown: @escaping () -> Bool = { NSEvent.pressedMouseButtons != 0 }) {
    self.beginEdit = beginEdit
    self.doEdit = doEdit
    self.endEdit = endEdit
    self.sendCommand = sendCommand
    self.mouseButtonsDown = mouseButtonsDown
    super.init()
  }

  deinit {
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

  /// Sounds `cell`'s voicing. At most one cell is held: pressing another first releases the held one,
  /// so every Start this object sends is matched by exactly one Stop with the same id (and the
  /// scheduler's 4 s watchdog ends a preview whose Stop never arrived; `extend`s every second keep a
  /// held one alive).
  @MainActor func pressCell(_ cell: CellJSON) {
    releaseHeld()
    previewCounter += 1
    let id = previewCounter
    heldPreviewId = id
    heldCellId = cell.id
    let pitches = Array(cell.voicing.filter { (0...127).contains($0) }.prefix(8))
    send(["id": id, "on": true, "velocity": 100, "pitches": pitches], tag: kMsgTagPreview)
    holdHasButton = mouseButtonsDown()
    startKeepAlive(id: id)
  }

  /// Releases `cell` if it is the held one; a cell that was already released (by a press elsewhere, a cancelled
  /// gesture or disappearance) is ignored, never released twice. `release` says why the press ended and how long
  /// it was held, from the window's event timestamps (`PressRelease`); with Record on, a press that ended with its
  /// own button-up also inserts its chord for that long (`RecordLength`), and a press that became a drag (the drop
  /// appends by itself), was superseded or disappeared records nothing.
  @MainActor func releaseCell(_ cell: CellJSON, _ release: PressRelease) {
    guard heldCellId == cell.id else { return }
    releaseHeld()
    record(cell, release)
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

  /// Releases whatever is held (view disappearance, window close).
  @MainActor func releaseHeld() {
    stopKeepAlive()
    guard let id = heldPreviewId else { return }
    heldPreviewId = nil
    heldCellId = nil
    send(["id": id, "on": false], tag: kMsgTagPreview)
  }

  /// Silences everything the scheduler is sounding (next audio block).
  @MainActor func panic() {
    stopKeepAlive()
    heldPreviewId = nil
    heldCellId = nil
    sendCommand(kMsgTagPanic, Data())
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

  @MainActor private func startKeepAlive(id: Int) {
    stopKeepAlive()
    keepAliveTicks = 0
    let timer = Timer(timeInterval: Self.keepAliveInterval, target: KeepAliveTarget(state: self, id: id),
                      selector: #selector(KeepAliveTarget.tick(_:)), userInfo: nil, repeats: true)
    RunLoop.main.add(timer, forMode: .common)
    keepAlive = timer
  }

  @MainActor private func stopKeepAlive() {
    keepAlive?.invalidate()
    keepAlive = nil
  }

  /// One tick: the timer is done when `id` is no longer held; otherwise `PreviewKeepAlive` decides
  /// between an `extend` and a release.
  @MainActor private func keepAliveTick(id: Int) {
    guard heldPreviewId == id else { stopKeepAlive(); return }
    keepAliveTicks += 1
    switch PreviewKeepAlive.action(tick: keepAliveTicks, holdHasButton: holdHasButton, buttonDown: mouseButtonsDown()) {
    case .extend: send(["id": id, "extend": true], tag: kMsgTagPreview)
    case .release: releaseHeld()
    }
  }

  // MARK: - Commands (engine wire format, json_api.h)

  /// Sends one command, e.g. `["cmd": "appendChord", "root": 0, "type": "maj"]`; `"schema": 1` is added
  /// here. The plugin answers with a snapshot (`kMsgTagSnapshot`) or an error (`kMsgTagError`).
  @MainActor func send(command: [String: Any]) {
    send(command, tag: kMsgTagCommand)
  }

  /// Runs a keyboard command. Digits append the suggestion chip of that number (nothing when there is no such
  /// chip); Delete removes every selected chord in one step; ⌘C copies the selection, ⌘X cuts it, ⌘V pastes
  /// after the anchor (else at the end), ⌘D duplicates the selection after its last block, ⌘A selects every
  /// block (strip editing design D5, D6); undo and redo are the key bar's commands; an arrow nudge (mapped only
  /// with a track's notes targeted, tracks design D15) does nothing yet (plan Task 20); with notes targeted only
  /// undo and redo run (`KeyCommand.runsBeforeNoteDispatch`). Each does nothing when it has nothing to act on,
  /// and the router consumes every mapped key whether or not it did anything, so the host never acts on it (see
  /// `KeyCommandMap`, `EditorKeyRouter`).
  @MainActor func perform(_ command: KeyCommand) {
    // Everything below acts on the chords. With a track's notes targeted only the shared undo history runs, so
    // a key meant for the notes never edits the chords the open track hides; plan Task 20 replaces this guard
    // with the dispatch by target.
    guard command.runsBeforeNoteDispatch(target: keyTarget) else { return }
    let selection = snapshot?.selection ?? []
    switch command {
    case .appendSuggestion(let n):
      guard let suggestions = snapshot?.suggestions, suggestions.indices.contains(n - 1) else { return }
      let suggestion = suggestions[n - 1]
      send(command: ["cmd": "appendChord", "root": suggestion.root, "type": suggestion.type])
    case .deleteSelection:
      guard !selection.isEmpty else { return }
      send(command: ClipboardChords.removeCommand(selection))
    case .undo:
      send(command: ["cmd": "undo"])
    case .redo:
      send(command: ["cmd": "redo"])
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
    case .nudge:
      break   // mapped only with notes targeted, which needs the roll (M3); plan Task 20 sends the nudge
    }
  }

  // MARK: - Strip editing (the clipboard and the selection, strip editing design D5–D7)

  /// Where the clipboard is written and read (`ChordPasteboard`): the general pasteboard; a harness may put a fake
  /// here.
  var pasteboard: any ChordPasteboard = NSPasteboard.general

  /// The ids of the strip's blocks in order; empty before the first snapshot.
  private var eventIds: [Int] { snapshot?.events.map(\.id) ?? [] }

  /// ⌘C and the block menu's Copy: the chords `ids` (in strip order, whatever the order given) on the pasteboard
  /// as one item in both forms (`ClipboardChords.copy`). Returns whether the pasteboard took it; false, the
  /// pasteboard untouched, when none of them is on the strip.
  @MainActor @discardableResult func copyChords(_ ids: [Int]) -> Bool {
    guard let events = snapshot?.events else { return false }
    return ClipboardChords.copy(ids, from: events, to: pasteboard)
  }

  /// ⌘X and the block menu's Cut: the copy, then `removeChords` (one undo step), sent only once the copy is
  /// published (`ClipboardChords.cut`); a failed copy removes nothing.
  @MainActor func cutChords(_ ids: [Int]) {
    guard let events = snapshot?.events, let removal = ClipboardChords.cut(ids, from: events, to: pasteboard) else { return }
    send(command: removal)
  }

  /// Whether the pasteboard holds ChordLab's chords (Paste after is disabled otherwise).
  @MainActor var canPaste: Bool {
    ClipboardChords.paste(from: pasteboard) != nil
  }

  /// ⌘V and Paste after: inserts the pasteboard's chords at `index` (one undo step; they become the selection).
  /// A pasteboard without ChordLab's chords (text only, or nothing) pastes nothing; a paste past 256 chords is
  /// the engine's refusal, shown like any other (`lastError`), and the pasteboard keeps its content.
  @MainActor func pasteChords(at index: Int) {
    guard let chords = ClipboardChords.paste(from: pasteboard) else { return }
    send(command: ClipboardChords.insertCommand(chords, at: index))
  }

  /// ⌘D and the block menu's Duplicate: the chords `ids` again, right after the last of them, without touching
  /// the pasteboard.
  @MainActor func duplicateChords(_ ids: [Int]) {
    guard let events = snapshot?.events else { return }
    let chords = ClipboardChords.chords(of: events, ids: ids)
    guard !chords.isEmpty else { return }
    send(command: ClipboardChords.insertCommand(chords, at: SelectionLogic.index(after: ids, ids: eventIds)))
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
    pendingAddTrackIds = tracks.map(\.id)
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

  /// After a snapshot is decoded: opens the track a pending add created (the next snapshot after the add decides,
  /// `LaneTabs.newTrackId`), else keeps the open lane while its track exists and falls back to Chords otherwise.
  private func updateOpenLane(for snapshot: Snapshot) {
    if let before = pendingAddTrackIds {
      pendingAddTrackIds = nil
      if let id = LaneTabs.newTrackId(before: before, after: snapshot.tracks?.map(\.id) ?? before) {
        openLane = .track(id)
        return
      }
    }
    let lane = openLane.resolved(in: snapshot.tracks)
    if lane != openLane { openLane = lane }
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
    send(command: ["cmd": "setKey", "letter": tonic.letter, "alter": tonic.alter, "mode": mode])
    send(command: ["cmd": "browseKey", "home": true])
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
    if always { send(command: ["cmd": "setTransposeWithKey", "on": true]) }
    send(command: ["cmd": "transposeFromKey", "fromTonicPc": fromPc])
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
        DispatchQueue.main.async { [weak self] in
          self?.send(command: ["cmd": "setKeyHintSeen", "on": true])
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
      let decoded = try JSONDecoder().decode(Snapshot.self, from: json)
      snapshot = decoded
      lastError = nil
      let target = keyTarget.resolved(in: decoded.tracks)
      if target != keyTarget { keyTarget = target }
      updateOpenLane(for: decoded)
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

  /// `kMsgTagExportReady` payload.
  func applyExportReady(json: Data) {
    if let ready = try? JSONDecoder().decode(ExportReadyJSON.self, from: json) {
      exportPath = ready.path
    }
  }

  /// `kMsgTagError` payload. A key change still waiting for its snapshot was most likely the refused
  /// command: it is dropped, so a later snapshot with that tonic (an undo, a redo) shows nothing. So is a pending
  /// track add (a refused add opens nothing).
  func applyError(json: Data) {
    pendingKeyChange = nil
    pendingAddTrackIds = nil
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
