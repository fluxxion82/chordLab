import AppKit
import SwiftUI

/// The piano roll's keyboard column (tracks design D14): one key per pitch row, row-aligned with the grid at the same
/// vertical scroll and row height, drawn in one `Canvas` for the visible rows only. White keys are light rows; a
/// black key is a dark bar over the left `blackKeyFraction` of its row, the rest of the row light like the white key
/// it sits in; a thin line marks the two places white keys meet (B|C and E|F). Each C carries its label ("C4" for
/// MIDI 60, middle C; `RollGeometry.cLabel`) at the right. Clicking a key previews its pitch while the button is
/// down (`PianoKeyInputView` over the column); the column is hidden from accessibility (the preview is a pointer aid).
struct PianoKeyboardView: View {
  let geometry: RollGeometry
  /// The grid's vertical scroll (content y at the top), shared with the canvas.
  let scrollY: CGFloat

  /// How much of a black key's row its dark bar covers.
  static let blackKeyFraction: CGFloat = 0.62

  var body: some View {
    Canvas { context, size in
      let scroll = CGPoint(x: 0, y: scrollY)
      let pitches = geometry.visiblePitches(viewport: size, scroll: scroll)
      context.fill(Path(CGRect(origin: .zero, size: size)), with: .color(RollColors.whiteKey))
      var black = Path()
      var seams = Path()
      for pitch in pitches {
        let y = geometry.y(pitch: pitch) - scrollY
        if RollGeometry.isBlackKey(pitch: pitch) {
          black.addRoundedRect(in: CGRect(x: 0, y: y + 0.5, width: size.width * Self.blackKeyFraction,
                                          height: geometry.rowHeight - 1),
                               cornerSize: CGSize(width: 1.5, height: 1.5))
        } else if pitch % 12 == 0 || pitch % 12 == 5 {
          // Under C and F a white key meets the white key below it (B, E).
          let line = (y + geometry.rowHeight).rounded(.down) + 0.5
          seams.move(to: CGPoint(x: 0, y: line))
          seams.addLine(to: CGPoint(x: size.width, y: line))
        }
      }
      context.stroke(seams, with: .color(RollColors.keySeam), lineWidth: 1)
      context.fill(black, with: .color(RollColors.blackKey))
      for pitch in pitches {
        guard let label = RollGeometry.cLabel(pitch: pitch) else { continue }
        let y = geometry.y(pitch: pitch) - scrollY + geometry.rowHeight / 2
        context.draw(Text(label).font(RollColors.keyLabelFont).foregroundColor(RollColors.keyLabel),
                     at: CGPoint(x: size.width - 3, y: y), anchor: .trailing)
      }
      // The column's right edge against the grid.
      context.fill(Path(CGRect(x: size.width - 1, y: 0, width: 1, height: size.height)), with: .color(RollColors.keyEdge))
    }
    .accessibilityHidden(true)
  }
}

/// The keyboard column's press input (tracks design D14: clicking a key previews it; plan Task 19): an AppKit view over
/// the column only. A left press on a key holds that pitch through the editor's preview path (`ChordLabState.pressKey`,
/// one held preview with keep-alive) and its up stops it (`releaseKey`); a drag does not change the key. The press also
/// ends on the window resigning key, the application resigning active and the view leaving its window; a lost up is
/// ended by the keep-alive (no button down releases the hold). A ⌃-click or right-click does nothing.
struct PianoKeyInputView: NSViewRepresentable {
  let trackId: Int
  let state: ChordLabState

  func makeNSView(context: Context) -> PianoKeyInputNSView {
    let view = PianoKeyInputNSView()
    configure(view)
    return view
  }

  func updateNSView(_ view: PianoKeyInputNSView, context: Context) {
    configure(view)
  }

  static func dismantleNSView(_ view: PianoKeyInputNSView, coordinator: ()) {
    view.stopObserving()
  }

  private func configure(_ view: PianoKeyInputNSView) {
    let trackId = trackId
    let state = state
    view.pitchAt = { [weak state] point in
      guard let state else { return nil }
      let roll = state.rolls.states[trackId]
      return PianoKeys.pitch(atViewY: point.y, geometry: roll.geometry(ticksPerBeat: state.rollTiming?.ticksPerBeat ?? 960),
                             scrollY: roll.scroll.y)
    }
    view.press = { [weak state] pitch in state?.pressKey(pitch: pitch) }
    view.release = { [weak state] in state?.releaseKey() }
  }
}

/// The AppKit half of `PianoKeyInputView`: flipped (the column's own top-left coordinates), first mouse accepted (a
/// plug-in window is often not key), never first responder, never moves the window.
final class PianoKeyInputNSView: NSView {
  var pitchAt: @MainActor (CGPoint) -> Int? = { _ in nil }
  var press: @MainActor (Int) -> Void = { _ in }
  var release: @MainActor () -> Void = {}
  /// Between this view's own down on a key and its up (or a cancel).
  private(set) var pressing = false
  private var observers: [NSObjectProtocol] = []

  override var isFlipped: Bool { true }
  override var acceptsFirstResponder: Bool { false }
  override var mouseDownCanMoveWindow: Bool { false }
  override func acceptsFirstMouse(for event: NSEvent?) -> Bool { true }

  override func mouseDown(with event: NSEvent) {
    guard !event.modifierFlags.contains(.control) else { return }
    if pressing { end() }
    guard let pitch = pitchAt(convert(event.locationInWindow, from: nil)) else { return }
    pressing = true
    press(pitch)
  }

  override func mouseUp(with event: NSEvent) {
    end()
  }

  override func rightMouseDown(with event: NSEvent) {}

  override func viewDidMoveToWindow() {
    super.viewDidMoveToWindow()
    stopObserving()
    guard let window else {
      end()
      return
    }
    let center = NotificationCenter.default
    let cancel: (Notification) -> Void = { [weak self] _ in MainActor.assumeIsolated { self?.end() } }
    observers = [
      center.addObserver(forName: NSWindow.didResignKeyNotification, object: window, queue: .main, using: cancel),
      center.addObserver(forName: NSApplication.didResignActiveNotification, object: nil, queue: .main, using: cancel),
    ]
  }

  func stopObserving() {
    observers.forEach { NotificationCenter.default.removeObserver($0) }
    observers = []
  }

  /// Ends this view's press (its up or a cancel): the key's preview stops once.
  func end() {
    guard pressing else { return }
    pressing = false
    release()
  }
}
