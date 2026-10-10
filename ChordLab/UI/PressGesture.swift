import SwiftUI

/// Press/release semantics for a view (SwiftUI's Button only reports taps).
/// `@GestureState` is reset by SwiftUI when the gesture ends *or is cancelled*
/// (e.g. the host window loses focus mid-press), so the release path also runs
/// then, and again on disappearance. `release()` is idempotent, so a MIDI
/// note-on is never left without exactly one note-off.
struct PressAction: ViewModifier {
  let onPress: () -> Void
  let onRelease: () -> Void
  @GestureState private var isPressing = false
  @State private var pressed = false

  func body(content: Content) -> some View {
    content
      .simultaneousGesture(
        DragGesture(minimumDistance: 0)
          .updating($isPressing) { _, state, _ in state = true })
      .onChange(of: isPressing) { _, now in
        if now { press() } else { release() }
      }
      .onDisappear { release() }
  }

  private func press() {
    guard !pressed else { return }
    pressed = true
    onPress()
  }

  private func release() {
    guard pressed else { return }
    pressed = false
    onRelease()
  }
}

extension View {
  func pressAction(onPress: @escaping () -> Void, onRelease: @escaping () -> Void) -> some View {
    modifier(PressAction(onPress: onPress, onRelease: onRelease))
  }
}
