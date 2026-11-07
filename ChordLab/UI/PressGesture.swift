import SwiftUI

/// Press/release semantics for a view (SwiftUI's Button only reports taps): `onPress` when the left
/// mouse button goes down inside the view, `onRelease` when it comes up, when the press turns into a drag
/// (the native drag session of a `.draggable` view takes over from there), and on disappearance. The
/// button comes from the window's own events (`MouseButton`, published by `TipHost` through the
/// environment) and the decisions are `PressLogic`'s, which owns a press by where the down fell against
/// this view's `PressRegion` (its frame in the root's space, cut to the enclosing scroll view's viewport
/// so a scrolled-out view never claims a click, and the inscribed circle when `circular`) and reconciles
/// each change from the whole value (a down and its up delivered in one update still press and release;
/// a press whose up was missed does not block the next). No gesture: a zero-distance `DragGesture` here,
/// even a simultaneous one, stopped `.draggable` on the same view from ever starting a drag. A release
/// the window never reports (the mouse-up went to a drag session, the window resigned mid-press) is
/// caught by `ChordLabState`'s keep-alive, which releases a held cell once no button is down.
struct PressAction: ViewModifier {
  let onPress: () -> Void
  let onRelease: () -> Void
  let circular: Bool
  @Environment(\.mouseButton) private var button
  @State private var logic = PressLogic()
  @State private var region = PressRegion.nowhere

  func body(content: Content) -> some View {
    content
      .onGeometryChange(for: PressRegion.self) { proxy in
        let frame = proxy.frame(in: .named(MouseButton.coordinateSpace))
        let local = CGRect(origin: .zero, size: proxy.size)
        let shown = local.intersection(proxy.bounds(of: .scrollView) ?? local)
        let visible = shown.isNull ? CGRect.zero : shown.offsetBy(dx: frame.minX, dy: frame.minY)
        return PressRegion(frame: frame, visible: visible, circular: circular)
      } action: { region = $0 }
      .onChange(of: button) { old, new in
        apply(logic.update(from: old, to: new, region: region))
      }
      .onDisappear { apply(logic.disappear()) }
  }

  private func apply(_ events: [PressLogic.Event]) {
    for event in events {
      switch event {
      case .press: onPress()
      case .release: onRelease()
      }
    }
  }
}

extension View {
  /// `circular`: the view's hit shape is the circle inscribed in its frame (a palette cell), not the frame.
  func pressAction(circular: Bool = false, onPress: @escaping () -> Void, onRelease: @escaping () -> Void) -> some View {
    modifier(PressAction(onPress: onPress, onRelease: onRelease, circular: circular))
  }
}
