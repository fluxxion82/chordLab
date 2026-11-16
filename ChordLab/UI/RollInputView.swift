import AppKit
import SwiftUI

/// What the roll's input view calls (`RollInputNSView`): every closure takes the grid's own view coordinates
/// (top-left origin) or content coordinates as named, and the view holds no edit logic of its own. All of them run on
/// the main actor (the view's event methods).
struct RollInputHandlers {
  /// A grid view point in content coordinates at the track's CURRENT zoom and scroll (read at each event).
  var contentPoint: @MainActor (CGPoint) -> CGPoint = { $0 }
  /// One pointer event, its location in content coordinates (`ChordLabState.handleRollPointer`).
  var pointer: @MainActor (RollPointerEvent) -> Void = { _ in }
  /// The cursor over a grid view point.
  var cursor: @MainActor (CGPoint) -> RollCursor = { _ in .arrow }
  /// Scroll by a content delta in points (clamped by the state).
  var scroll: @MainActor (CGSize) -> Void = { _ in }
  /// Row zoom by a factor about a view y.
  var zoomRows: @MainActor (CGFloat, CGFloat) -> Void = { _, _ in }
  /// Time zoom by a factor about a view x.
  var zoomTime: @MainActor (CGFloat, CGFloat) -> Void = { _, _ in }
  /// The context menu for a right-click or ⌃-click at a content point; nil shows none. Plan Task 20 builds it (Copy,
  /// Cut, Paste here, Duplicate, Delete); until then it is nil.
  var contextMenu: @MainActor (CGPoint) -> NSMenu? = { _ in nil }
  /// Whether any mouse button is down now (`ChordLabState.mouseButtonsDown`, which a harness replaces): a gesture
  /// with no button down lost its mouse-up.
  var mouseButtonsDown: @MainActor () -> Bool = { NSEvent.pressedMouseButtons != 0 }
}

/// The piano roll's input view (tracks design D15 "Input adapter", plan Task 18): an AppKit view over the grid region
/// only (the `ZStack` of `PianoRollView.grid`, above the canvas and the markers overlay), so its hit-testing stops at
/// the grid's frame and the toolbar, ribbon, keyboard column, lane tabs and the rest of the editor keep their clicks.
/// It turns the window's events into `RollPointerEvent`s and view-state changes for one track; the gesture machine
/// and everything it decides live in `ChordLabState.handleRollPointer` and Core.
///
/// It reads presses from its own `mouseDown`/`mouseDragged`/`mouseUp`, never from a SwiftUI gesture, and passes every
/// event on to the window's local monitors as AppKit does anyway (`MouseButton`'s root tracker, `HoverTip`'s pointer
/// tracker and `EditorKeyRouter`'s mouse-down ownership monitor see the roll's clicks before the view does, so a
/// click here takes the keyboard for the editor and ends a section rename, which commits).
struct RollInputView: NSViewRepresentable {
  let trackId: Int
  let state: ChordLabState

  func makeNSView(context: Context) -> RollInputNSView {
    let view = RollInputNSView()
    view.handlers = handlers
    return view
  }

  func updateNSView(_ view: RollInputNSView, context: Context) {
    view.handlers = handlers
  }

  static func dismantleNSView(_ view: RollInputNSView, coordinator: ()) {
    view.stopObserving()
  }

  private var handlers: RollInputHandlers {
    let trackId = trackId
    let state = state
    return RollInputHandlers(
      contentPoint: { [weak state] in state?.rollContentPoint(trackId: trackId, viewPoint: $0) ?? $0 },
      pointer: { [weak state] event in state?.handleRollPointer(event, trackId: trackId) },
      cursor: { [weak state] in state?.rollCursor(trackId: trackId, viewPoint: $0) ?? .arrow },
      scroll: { [weak state] delta in state?.scrollRoll(trackId: trackId, by: delta) },
      zoomRows: { [weak state] factor, y in state?.zoomRoll(trackId: trackId, vertically: factor, aboutViewY: y) },
      zoomTime: { [weak state] factor, x in state?.zoomRoll(trackId: trackId, horizontally: factor, aboutViewX: x) },
      contextMenu: { _ in nil },
      mouseButtonsDown: { [weak state] in state?.mouseButtonsDown() ?? false })
  }
}

/// The AppKit half of `RollInputView` (see there). Decisions:
///
/// - **Flipped**, so a view point is the grid's own top-left coordinates, like SwiftUI's; every location goes
///   view → content through `handlers.contentPoint` at the moment of the event.
/// - **First mouse accepted:** Live's plug-in window is often not key when the user clicks it, and the editor's
///   SwiftUI controls take that first click too; a roll that ignored it would need a second click. A press never
///   moves the window (`mouseDownCanMoveWindow` false).
/// - **Never first responder:** the keyboard stays with `EditorKeyRouter`'s routes (a click here does not take focus
///   from anything; the router's mouse-down monitor ends a section rename on the same click).
/// - **⌃-click** is a context-menu click, like a right-click, never a `down`; a gesture still marked in progress
///   (its up was lost) is cancelled first, and the button's later drags and up are ignored. A right-click while a
///   left drag is in progress shows nothing.
/// - **Cancel:** on its window resigning key, the application resigning active and the view leaving its window, a
///   `cancel` event is sent whether or not a gesture is in progress (the machine leaves an idle state as it is, with
///   no effects), and the rest of the interrupted button's drags and up are ignored.
/// - **A lost mouse-up** (the up went to another window, a host's modal loop swallowed it): a `mouseMoved` during a
///   gesture proves it (AppKit sends drags, not moves, while a button is down), so it cancels the gesture whatever
///   the buttons say; `mouseEntered`, `cursorUpdate`, `scrollWheel`, `magnify` and a context-menu click cancel it when
///   `handlers.mouseButtonsDown()` says no button is down, before doing their own work. The next `down` would cancel
///   the stale gesture too (the machine does), but a held preview must not sound until then. Plan Task 19: the
///   preview keep-alive's own "no button down" release of a roll preview goes through
///   `ChordLabState.cancelRollGesture(trackId:)`, never `releaseHeld`, so the machine and the seam's Start/Stop stay
///   paired.
/// - **A drag outside the window** keeps arriving here (AppKit sends a press's drags and up to the view that took
///   the down, wherever the pointer is), so a release outside the window still ends the gesture; a release outside
///   the view sets the arrow (a resize cursor would otherwise stay until something else sets one).
/// - **Scroll wheel and pinch** (`RollWheel`): ⌥ zooms rows about the pointer, ⇧⌥ time about the pointer, else both
///   axes scroll; a pinch zooms time by `1 + magnification` about the pointer. During a drag, a scroll or zoom is
///   followed by a drag at the pointer's last location, so the preview follows the content under the pointer.
/// - **Cursor:** a tracking area (`.cursorUpdate`, `.mouseMoved`, `.mouseEnteredAndExited`, `.activeInActiveApp`,
///   `.inVisibleRect`): active whenever the host application is, because a plug-in window is usually not the key
///   window; the resize cursor over a note's edge in Notes mode, the arrow elsewhere (`RollCursor`); unchanged during
///   a drag.
final class RollInputNSView: NSView {
  var handlers = RollInputHandlers()
  /// Between this view's own `down` and its `up` (or a cancel).
  private(set) var gestureActive = false
  /// The last mouse event of the gesture in progress, for the drag that follows a scroll or zoom.
  private var lastLocationInWindow: CGPoint?
  private var lastModifiers = RollModifiers()
  private var lastClickCount = 1
  private var observers: [NSObjectProtocol] = []
  private var trackingArea: NSTrackingArea?

  override var isFlipped: Bool { true }
  override var acceptsFirstResponder: Bool { false }
  override var mouseDownCanMoveWindow: Bool { false }
  override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

  static func modifiers(_ flags: NSEvent.ModifierFlags) -> RollModifiers {
    let flags = flags.intersection(.deviceIndependentFlagsMask)
    return RollModifiers(shift: flags.contains(.shift), option: flags.contains(.option),
                         command: flags.contains(.command), control: flags.contains(.control))
  }

  // MARK: - Buttons

  override func mouseDown(with event: NSEvent) {
    if Self.modifiers(event.modifierFlags).control {
      // This button is down, so a gesture still marked in progress lost its up: it ends here, whatever the buttons say.
      if gestureActive { cancelGesture() }
      showContextMenu(for: event)
      return
    }
    gestureActive = true
    send(.down, event)
  }

  override func mouseDragged(with event: NSEvent) {
    guard gestureActive else { return }
    send(.drag, event)
  }

  override func mouseUp(with event: NSEvent) {
    guard gestureActive else { return }
    gestureActive = false
    send(.up, event)
    lastLocationInWindow = nil
    if bounds.contains(convert(event.locationInWindow, from: nil)) {
      updateCursor(at: event.locationInWindow)
    } else {
      NSCursor.arrow.set()
    }
  }

  override func rightMouseDown(with event: NSEvent) {
    // The right button itself counts as down here, so this rarely finds a lost left up; the move that brought the
    // pointer here usually has (`mouseMoved`).
    cancelIfButtonLost()
    guard !gestureActive else { return }
    showContextMenu(for: event)
  }

  /// A gesture in progress with no mouse button down lost its up: cancel it (see the type's "A lost mouse-up").
  private func cancelIfButtonLost() {
    if gestureActive && !handlers.mouseButtonsDown() { cancelGesture() }
  }

  private func send(_ phase: RollPointerEvent.Phase, _ event: NSEvent) {
    lastLocationInWindow = event.locationInWindow
    lastModifiers = Self.modifiers(event.modifierFlags)
    if phase == .down { lastClickCount = event.clickCount }
    let viewPoint = convert(event.locationInWindow, from: nil)
    handlers.pointer(RollPointerEvent(phase: phase, location: handlers.contentPoint(viewPoint),
                                      modifiers: lastModifiers, clickCount: event.clickCount,
                                      timestamp: event.timestamp))
  }

  private func showContextMenu(for event: NSEvent) {
    let point = handlers.contentPoint(convert(event.locationInWindow, from: nil))
    if let menu = handlers.contextMenu(point) {
      NSMenu.popUpContextMenu(menu, with: event, for: self)
    }
  }

  // MARK: - Wheel and pinch

  override func scrollWheel(with event: NSEvent) {
    cancelIfButtonLost()
    let action = RollWheel.action(deltaX: event.scrollingDeltaX, deltaY: event.scrollingDeltaY,
                                  precise: event.hasPreciseScrollingDeltas,
                                  invertedFromDevice: event.isDirectionInvertedFromDevice,
                                  momentum: event.momentumPhase != [],
                                  modifiers: Self.modifiers(event.modifierFlags))
    let point = convert(event.locationInWindow, from: nil)
    switch action {
    case .none: return
    case .scroll(let delta): handlers.scroll(delta)
    case .zoomRows(let factor): handlers.zoomRows(factor, point.y)
    case .zoomTime(let factor): handlers.zoomTime(factor, point.x)
    }
    followContent()
  }

  override func magnify(with event: NSEvent) {
    cancelIfButtonLost()
    let factor = max(0.1, 1 + event.magnification)
    guard factor != 1, factor.isFinite else { return }
    handlers.zoomTime(factor, convert(event.locationInWindow, from: nil).x)
    followContent()
  }

  /// After a scroll or zoom during a drag: a drag at the pointer's last location, now over other content.
  private func followContent() {
    guard gestureActive, let location = lastLocationInWindow else { return }
    let viewPoint = convert(location, from: nil)
    handlers.pointer(RollPointerEvent(phase: .drag, location: handlers.contentPoint(viewPoint),
                                      modifiers: lastModifiers, clickCount: lastClickCount,
                                      timestamp: ProcessInfo.processInfo.systemUptime))
  }

  // MARK: - Cursor

  override func updateTrackingAreas() {
    super.updateTrackingAreas()
    if let trackingArea { removeTrackingArea(trackingArea) }
    let area = NSTrackingArea(rect: .zero, options: [.cursorUpdate, .mouseMoved, .mouseEnteredAndExited,
                                                     .activeInActiveApp, .inVisibleRect],
                              owner: self, userInfo: nil)
    addTrackingArea(area)
    trackingArea = area
  }

  override func cursorUpdate(with event: NSEvent) {
    cancelIfButtonLost()
    updateCursor(at: event.locationInWindow)
  }

  override func mouseEntered(with event: NSEvent) {
    cancelIfButtonLost()
    updateCursor(at: event.locationInWindow)
  }

  /// No button is down when AppKit sends a move (a press sends drags), so a gesture still in progress lost its up.
  override func mouseMoved(with event: NSEvent) {
    if gestureActive { cancelGesture() }
    updateCursor(at: event.locationInWindow)
  }
  override func mouseExited(with event: NSEvent) {
    if !gestureActive { NSCursor.arrow.set() }
  }

  /// The cursor for the point under the pointer; left as it is during a gesture (a resize keeps its cursor).
  private func updateCursor(at locationInWindow: CGPoint) {
    guard !gestureActive else { return }
    let point = convert(locationInWindow, from: nil)
    guard bounds.contains(point) else { return }
    switch handlers.cursor(point) {
    case .arrow: NSCursor.arrow.set()
    case .resizeLeftRight: NSCursor.resizeLeftRight.set()
    }
  }

  // MARK: - Cancel

  override func viewDidMoveToWindow() {
    super.viewDidMoveToWindow()
    stopObserving()
    guard let window else {
      cancelGesture()
      return
    }
    let center = NotificationCenter.default
    // Notifications delivered on the main queue, but the closure type is not main-actor isolated.
    let cancel: (Notification) -> Void = { [weak self] _ in MainActor.assumeIsolated { self?.cancelGesture() } }
    observers = [
      center.addObserver(forName: NSWindow.didResignKeyNotification, object: window, queue: .main, using: cancel),
      center.addObserver(forName: NSApplication.didResignActiveNotification, object: nil, queue: .main, using: cancel),
    ]
  }

  func stopObserving() {
    observers.forEach { NotificationCenter.default.removeObserver($0) }
    observers = []
  }

  /// Ends the gesture without committing: the rest of the button's events are ignored and the machine gets `cancel`.
  func cancelGesture() {
    gestureActive = false
    lastLocationInWindow = nil
    handlers.pointer(RollPointerEvent(phase: .cancel, location: .zero,
                                      timestamp: ProcessInfo.processInfo.systemUptime))
  }
}
