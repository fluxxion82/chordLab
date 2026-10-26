import SwiftUI

/// What a hovered view publishes: its bounds anchor, which side it prefers, the tip to draw, and how to
/// take the tip down again (the root overlay calls `dismiss` when it finds the item stale).
struct TipItem: Identifiable {
  let id: String
  let anchor: Anchor<CGRect>
  let preferAbove: Bool
  let content: AnyView
  let dismiss: () -> Void
}

/// Empty except while a tip shows (the hovered view's).
struct TipPreferenceKey: PreferenceKey {
  static var defaultValue: [TipItem] = []
  static func reduce(value: inout [TipItem], nextValue: () -> [TipItem]) { value += nextValue() }
}

extension EnvironmentValues {
  /// Whether a mouse button is down right now, which a tip checks before it shows: a drag that began on
  /// another view never disarmed the view it passes over, so the cached hover state alone cannot tell.
  /// The default is the hardware state (`NSEvent.pressedMouseButtons`); whoever hosts the editor with
  /// synthesized events, which never reach that state, sets its own at the root.
  var tipButtonsDown: () -> Bool {
    get { self[TipButtonsDownKey.self] }
    set { self[TipButtonsDownKey.self] = newValue }
  }
}

private struct TipButtonsDownKey: EnvironmentKey {
  static let defaultValue: () -> Bool = { NSEvent.pressedMouseButtons != 0 }
}

/// A tip that opens after the mouse rests on the view for `delay` and closes on hover exit, drawn by
/// `TipOverlay` at the editor's root (`tipHost()`) — inside the window, never a popover, never
/// hit-testable, so it cannot take a click or key status. It never opens while a mouse button is down
/// (on this view or anywhere: the live button state is checked when the delay ends), closes on mouse-down,
/// stays closed after a click until the mouse leaves and comes back (so a press, then a drag of the handle,
/// is never covered), closes the moment `suppressed` turns true and is scheduled again when `suppressed`
/// turns false with the mouse still there (entering a block over its handle, then moving onto the block).
/// Every request carries a generation: an exit, a press, a change of `suppressed` or `id`, a dismissal or
/// disappearance starts a new one, so a delayed callback from before it shows nothing. The root overlay
/// drops a tip whose view is not under the pointer (a missed hover exit) and every tip when the pointer
/// leaves the editor, the application deactivates or the editor's window resigns key. The palette cells,
/// the sequence blocks and the suggestion chips share this one mechanism.
struct HoverTip<Tip: View>: ViewModifier {
  static var defaultDelay: TimeInterval { 0.6 }

  let id: String
  let delay: TimeInterval
  let suppressed: Bool
  let preferAbove: Bool
  @ViewBuilder let tip: () -> Tip

  @State private var hovering = false
  @State private var showing = false
  @State private var armed = true          // false from a mouse-down until the next hover exit
  @State private var generation = 0
  @Environment(\.tipButtonsDown) private var buttonsDown

  func body(content: Content) -> some View {
    content
      .onHover { over in
        hovering = over
        if over {
          schedule()
        } else {
          invalidate()
          armed = true
        }
      }
      .simultaneousGesture(
        DragGesture(minimumDistance: 0)
          .onChanged { _ in
            armed = false
            invalidate()
          })
      .onChange(of: suppressed) { _, now in
        invalidate()
        if !now { schedule() }
      }
      .onChange(of: id) { _, _ in invalidate() }
      .onDisappear { invalidate() }
      .anchorPreference(key: TipPreferenceKey.self, value: .bounds) { anchor in
        guard showing else { return [] }
        return [TipItem(id: id, anchor: anchor, preferAbove: preferAbove, content: AnyView(tip()),
                        dismiss: { hovering = false; invalidate() })]
      }
  }

  /// Hides the tip and makes every pending request obsolete.
  private func invalidate() {
    generation &+= 1
    showing = false
  }

  private func schedule() {
    guard hovering, armed, !suppressed else { return }
    generation &+= 1
    let request = generation
    let buttonsDown = self.buttonsDown
    DispatchQueue.main.asyncAfter(deadline: .now() + delay) {
      guard request == generation, hovering, armed, !buttonsDown() else { return }
      showing = true
    }
  }
}

extension View {
  /// See `HoverTip`. `id` must be unique among the views that can show a tip at once (a cell id, an
  /// event id, a chip index).
  func hoverTip<Tip: View>(id: String, suppressed: Bool = false, preferAbove: Bool = true,
                           delay: TimeInterval = HoverTip<Tip>.defaultDelay,
                           @ViewBuilder _ tip: @escaping () -> Tip) -> some View {
    modifier(HoverTip(id: id, delay: delay, suppressed: suppressed, preferAbove: preferAbove, tip: tip))
  }

  /// Draws the hover tips of this view tree (`TipHost`); applied once, at the editor's root.
  func tipHost() -> some View { modifier(TipHost()) }
}

/// The editor's root: tracks the pointer in the root's space (`PointerTracker`) and draws the published
/// tips over everything with `TipOverlay`. The pointer is forgotten when it leaves the editor, when the
/// application resigns active and when the editor's window resigns key, which takes every tip down. Kept
/// in a modifier of its own so that the pointer's state, which changes on every mouse move, re-renders only
/// the overlay.
struct TipHost: ViewModifier {
  @State private var pointer: CGPoint?

  func body(content: Content) -> some View {
    content
      .background(PointerTracker { pointer = $0 })
      .overlayPreferenceValue(TipPreferenceKey.self) { items in TipOverlay(items: items, pointer: pointer) }
  }
}

/// Reports the pointer in its own top-left coordinates (the editor root's, as it sits behind the root)
/// from the mouse events the application dispatches to its window, through a local event monitor, so a
/// move counts however deep the view under the mouse is: SwiftUI's own hover tracking at the root
/// (`onContinuousHover`) missed moves, and every move over the palette's and the strip's scroll views,
/// in the harness. Nil when the pointer leaves, when the application resigns active and when the window
/// resigns key. It takes no clicks.
private struct PointerTracker: NSViewRepresentable {
  let onPointer: (CGPoint?) -> Void

  func makeNSView(context: Context) -> PointerTrackingView {
    let view = PointerTrackingView()
    view.onPointer = onPointer
    return view
  }

  func updateNSView(_ view: PointerTrackingView, context: Context) { view.onPointer = onPointer }

  static func dismantleNSView(_ view: PointerTrackingView, coordinator: ()) { view.stop() }
}

private final class PointerTrackingView: NSView {
  var onPointer: ((CGPoint?) -> Void)?
  private var monitor: Any?
  private var observers: [NSObjectProtocol] = []

  override var isFlipped: Bool { true }
  override func hitTest(_ point: NSPoint) -> NSView? { nil }

  override func viewDidMoveToWindow() {
    super.viewDidMoveToWindow()
    stop()
    guard let window else { return }
    let events: NSEvent.EventTypeMask = [.mouseMoved, .mouseEntered, .mouseExited,
                                         .leftMouseDragged, .rightMouseDragged, .otherMouseDragged]
    monitor = NSEvent.addLocalMonitorForEvents(matching: events) { [weak self] event in
      MainActor.assumeIsolated { self?.track(event) }
      return event
    }
    let center = NotificationCenter.default
    let forget: (Notification) -> Void = { [weak self] _ in MainActor.assumeIsolated { self?.onPointer?(nil) } }
    observers = [center.addObserver(forName: NSApplication.didResignActiveNotification, object: nil, queue: .main, using: forget),
                 center.addObserver(forName: NSWindow.didResignKeyNotification, object: window, queue: .main, using: forget)]
  }

  func stop() {
    if let monitor { NSEvent.removeMonitor(monitor) }
    monitor = nil
    observers.forEach { NotificationCenter.default.removeObserver($0) }
    observers = []
  }

  private func track(_ event: NSEvent) {
    guard let window, event.window === window else { return }
    let point = convert(event.locationInWindow, from: nil)
    onPointer?(bounds.contains(point) ? point : nil)
  }
}

/// The root overlay: shows the published tip whose view is under the pointer, resolved in the editor's
/// space and handed to a `TipCard` keyed by the tip's id, so every tip starts with fresh measurement
/// state. Any other published tip is stale (its view missed a hover exit, or the pointer has left the
/// editor) and is dismissed. Not hit-testable.
struct TipOverlay: View {
  let items: [TipItem]
  let pointer: CGPoint?

  var body: some View {
    GeometryReader { proxy in
      let live = pointer.flatMap { point in
        items.first { proxy[$0.anchor].insetBy(dx: -1, dy: -1).contains(point) }
      }
      let stale = items.filter { $0.id != live?.id }
      ZStack {
        if let item = live {
          TipCard(item: item, anchor: proxy[item.anchor], container: CGRect(origin: .zero, size: proxy.size))
            .id(item.id)
        }
        // Dismissed after this update, never during it.
        Color.clear.task(id: stale.map(\.id)) { for item in stale { item.dismiss() } }
      }
    }
    .allowsHitTesting(false)
    .accessibilityHidden(true)               // the hovered views carry their own labels
  }
}

/// One tip: measures itself on its first layout pass and is invisible until then, so it never flashes
/// at the wrong place or at a previous tip's size; then sits at `TipGeometry.frame`. The size comes from
/// `onGeometryChange`: a size preference read with `onPreferenceChange` inside the root overlay never
/// reported (seen in the offscreen harness), which left every tip invisible.
private struct TipCard: View {
  static let gap: CGFloat = 6
  let item: TipItem
  let anchor: CGRect
  let container: CGRect
  @State private var size = CGSize.zero

  var body: some View {
    let frame = TipGeometry.frame(anchor: anchor, tipSize: size, in: container,
                                  preferAbove: item.preferAbove, gap: Self.gap)
    item.content
      .modifier(TipChrome())
      .fixedSize()
      .onGeometryChange(for: CGSize.self) { $0.size } action: { size = $0 }
      .position(x: frame.midX, y: frame.midY)
      .opacity(size == .zero ? 0 : 1)
  }
}

/// The look the popovers had: a dark card with a hairline and a shadow.
private struct TipChrome: ViewModifier {
  func body(content: Content) -> some View {
    content
      .padding(12)
      .background(RoundedRectangle(cornerRadius: 8).fill(Color(red: 0.10, green: 0.12, blue: 0.24)))
      .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(Color.white.opacity(0.18), lineWidth: 1))
      .shadow(color: .black.opacity(0.45), radius: 10, y: 4)
  }
}
