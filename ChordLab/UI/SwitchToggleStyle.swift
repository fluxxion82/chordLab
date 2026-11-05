import SwiftUI

/// The switch style of Follow Live, Loop, Auto voice-lead and Transpose with key: a capsule track with a
/// knob that slides to the trailing end when on, the track in the accent colour when on and translucent
/// white when off, all drawn by SwiftUI from the toggle's value alone.
///
/// The system `.switch` style is an `NSSwitch`, and AppKit draws a switch's on state grey while its window
/// is not the key window. A plug-in window in Live is not key until the first click inside it, so on the
/// first paint after the window opened every switch that was on read as off, while the status line beside
/// them said on, until that click (Live run of 2026-11-05, finding 3). This style's colours do not depend on
/// the window, so the first paint is right. The label keeps whatever style the caller gave it.
struct AccentSwitchToggleStyle: ToggleStyle {
  /// The track: the size of a small `NSSwitch`, which is what the text-sized system switch draws.
  static let trackSize = CGSize(width: 26, height: 15)

  func makeBody(configuration: Configuration) -> some View {
    Button {
      configuration.isOn.toggle()
    } label: {
      HStack(spacing: 8) {
        configuration.label
        Capsule()
          .fill(configuration.isOn ? Color.accentColor : Color.white.opacity(0.25))
          .frame(width: Self.trackSize.width, height: Self.trackSize.height)
          .overlay(alignment: configuration.isOn ? .trailing : .leading) {
            Circle()
              .fill(Color.white)
              .shadow(color: .black.opacity(0.3), radius: 0.5, y: 0.5)
              .padding(1)
          }
          .animation(.easeInOut(duration: 0.15), value: configuration.isOn)
          .accessibilityHidden(true)
      }
    }
    .buttonStyle(.plain)
    .accessibilityAddTraits(.isToggle)
    .accessibilityValue(configuration.isOn ? "On" : "Off")
  }
}

extension ToggleStyle where Self == AccentSwitchToggleStyle {
  /// `.toggleStyle(.accentSwitch)`: the switch whose on state stays visible in a window that is not key.
  static var accentSwitch: AccentSwitchToggleStyle { AccentSwitchToggleStyle() }
}
