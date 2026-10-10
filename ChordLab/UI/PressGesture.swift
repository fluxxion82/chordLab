import SwiftUI

/// Press/release semantics for a view (SwiftUI's Button only reports taps).
/// `onRelease` is also fired if the gesture is cancelled or the view disappears
/// mid-press, so a MIDI note-on is never left without its note-off.
struct PressAction: ViewModifier {
  let onPress: () -> Void
  let onRelease: () -> Void
  @State private var pressed = false

  func body(content: Content) -> some View {
    content
      .simultaneousGesture(
        DragGesture(minimumDistance: 0)
          .onChanged { _ in
            if !pressed { pressed = true; onPress() }
          }
          .onEnded { _ in
            if pressed { pressed = false; onRelease() }
          })
      .onDisappear {
        if pressed { pressed = false; onRelease() }
      }
  }
}

extension View {
  func pressAction(onPress: @escaping () -> Void, onRelease: @escaping () -> Void) -> some View {
    modifier(PressAction(onPress: onPress, onRelease: onRelease))
  }
}
