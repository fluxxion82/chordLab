import AppKit

/// Both keyboard routes for one editor view, ending in `ChordLabState.perform`.
///
/// Route A (`install`): a local key-down monitor sees every key the application dispatches, before any
/// window does. It acts only when the event is for the editor's own window, the editor owns the keyboard
/// in that window, no text is being edited, and the key maps. Ownership: true when the editor appears,
/// then follows the last mouse-down in that window's content view — inside the editor's view it is ours,
/// elsewhere in the content view it is the host's (a host that puts its own controls, or a second editor,
/// in the same window keeps its keys); a mouse-down outside the content view (a title-bar drag, a
/// traffic-light click, a resize-edge grab) chooses neither and changes nothing. Route B (`handle(characters:…)`): the host delivered the key through the plug-in API
/// (VST3 `onKeyDown` → `ChordLab::OnKeyDown`); the host already chose the plug-in, so only the text gate
/// applies; its key-ups (`keyUp(characters:…)`, VST3 `onKeyUp` → `ChordLab::OnKeyUp`) end a held key,
/// since that route has no repeat flag. `KeyRepeatFilter` makes one physical press one command on either
/// route, or on both when a host uses both, and keeps a held key from repeating. A mapped key is consumed
/// either way; everything else goes on to the host.
final class EditorKeyRouter {
  private unowned let view: NSView
  private unowned let state: ChordLabState
  private var keyMonitor: Any?
  private var mouseMonitor: Any?
  private var focusObservers: [NSObjectProtocol] = []
  private(set) var ownsKeyboard = false
  private var filter = KeyRepeatFilter()

  init(view: NSView, state: ChordLabState) {
    self.view = view
    self.state = state
  }

  deinit { remove() }

  func install() {
    guard keyMonitor == nil else { return }
    ownsKeyboard = true
    filter = KeyRepeatFilter()
    keyMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { [weak self] event in
      guard let self else { return event }
      return MainActor.assumeIsolated { self.handle(event) } ? nil : event
    }
    mouseMonitor = NSEvent.addLocalMonitorForEvents(matching: [.leftMouseDown, .rightMouseDown, .otherMouseDown]) { [weak self] event in
      self?.noteMouseDown(event)
      return event
    }
    // Focus loss: the releases of keys held now may go to another window or app, so their holds and
    // recorded key-down answers go (click ownership stays as it is).
    let center = NotificationCenter.default
    focusObservers = [
      center.addObserver(forName: NSWindow.didResignKeyNotification, object: nil, queue: .main) { [weak self] note in
        MainActor.assumeIsolated { self?.windowDidResignKey(note.object as? NSWindow) }
      },
      center.addObserver(forName: NSApplication.didResignActiveNotification, object: nil, queue: .main) { [weak self] _ in
        self?.filter.clearHolds()
      },
    ]
  }

  func remove() {
    if let monitor = keyMonitor { NSEvent.removeMonitor(monitor) }
    if let monitor = mouseMonitor { NSEvent.removeMonitor(monitor) }
    keyMonitor = nil
    mouseMonitor = nil
    focusObservers.forEach { NotificationCenter.default.removeObserver($0) }
    focusObservers = []
    ownsKeyboard = false
    filter = KeyRepeatFilter()   // no hold or duplicate history survives the editor going away
  }

  /// The editor's own window lost key status; another window's resignation changes nothing.
  @MainActor private func windowDidResignKey(_ window: NSWindow?) {
    guard let window, window === view.window else { return }
    filter.clearHolds()
  }

  /// A click in the editor's window decides who has the keyboard there; clicks in other windows, and on
  /// the window's frame outside its content view, change nothing.
  func noteMouseDown(_ event: NSEvent) {
    guard let window = view.window, event.window === window, let content = window.contentView else { return }
    guard content.bounds.contains(content.convert(event.locationInWindow, from: nil)) else { return }
    let owns = view.bounds.contains(view.convert(event.locationInWindow, from: nil))
    if ownsKeyboard && !owns { filter.clearHolds() }   // the releases of keys held now may go elsewhere
    ownsKeyboard = owns
  }

  /// Route A. Whether the event was consumed.
  @MainActor func handle(_ event: NSEvent) -> Bool {
    guard let window = view.window, event.window === window, ownsKeyboard else { return false }
    return deliver(KeyInput(event: event), source: .appKit(isRepeat: event.isARepeat))
  }

  /// Route B. Whether the key was consumed.
  @MainActor func handle(characters: String, virtualKey: Int, shift: Bool, command: Bool, option: Bool) -> Bool {
    let input = Self.hostInput(characters: characters, virtualKey: virtualKey, shift: shift, command: command,
                               option: option)
    let consumed = deliver(input, source: .host)
    filter.noteHostKeyDown(input, consumed: consumed)
    return consumed
  }

  /// Route B's key-up: the key is no longer held (`KeyRepeatFilter.keyUp`). Answers what the key's last
  /// route-B key-down answered, recorded then — not recomputed, since a modifier may have been released
  /// or the text gate changed since — so the host sees the press and the release go to the same place.
  @MainActor func keyUp(characters: String, virtualKey: Int, shift: Bool, command: Bool, option: Bool) -> Bool {
    let input = Self.hostInput(characters: characters, virtualKey: virtualKey, shift: shift, command: command,
                               option: option)
    return filter.keyUp(input, at: ProcessInfo.processInfo.systemUptime)
  }

  /// Route B's key: Control is read from the live modifier flags because iPlug2's translation does not
  /// carry it.
  private static func hostInput(characters: String, virtualKey: Int, shift: Bool, command: Bool,
                                option: Bool) -> KeyInput {
    let control = NSEvent.modifierFlags.intersection(.deviceIndependentFlagsMask).contains(.control)
    return KeyInput(characters: characters, virtualKey: virtualKey, shift: shift, command: command,
                    option: option, control: control)
  }

  /// A section name, or any other text, is being typed: no key is ours.
  @MainActor private var editingText: Bool {
    state.editingText || view.window?.firstResponder is NSTextView
  }

  @MainActor private func deliver(_ input: KeyInput, source: KeySource) -> Bool {
    guard !editingText else { return false }
    guard let command = KeyCommandMap.command(for: input) else { return false }
    if filter.shouldAct(input, at: ProcessInfo.processInfo.systemUptime, source: source) {
      state.perform(command)
    }
    return true
  }
}

extension KeyInput {
  /// Route A: macOS hardware key codes for the delete keys (51 = delete, 117 = forward delete), the
  /// unmodified character for the rest; modifiers masked to the device-independent flags.
  init(event: NSEvent) {
    let flags = event.modifierFlags.intersection(.deviceIndependentFlagsMask)
    let key: Key
    switch event.keyCode {
    case 51: key = .delete
    case 117: key = .forwardDelete
    default:
      let text = event.charactersIgnoringModifiers ?? ""
      key = text.count == 1 ? .character(text.first!) : .other
    }
    self.init(key: key, command: flags.contains(.command), shift: flags.contains(.shift),
              option: flags.contains(.option), control: flags.contains(.control))
  }
}
