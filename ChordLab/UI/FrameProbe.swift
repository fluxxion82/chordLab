import SwiftUI

/// Named frames for the render harness (ChordLab/Tests/viewharness): a view marked `.probeFrame("name")` reports its
/// frame, in the window's coordinates (top-left origin), to the `FrameProbe` the harness puts in the environment. In
/// the plug-in there is none, and the modifier adds nothing to the view.
final class FrameProbe {
  /// The last frame each name reported; only views that were laid out report.
  private(set) var frames: [String: CGRect] = [:]

  func record(_ name: String, _ frame: CGRect) {
    frames[name] = frame
  }

  func reset() {
    frames = [:]
  }
}

private struct FrameProbeKey: EnvironmentKey {
  static let defaultValue: FrameProbe? = nil
}

extension EnvironmentValues {
  /// The harness's frame probe; nil in the plug-in.
  var frameProbe: FrameProbe? {
    get { self[FrameProbeKey.self] }
    set { self[FrameProbeKey.self] = newValue }
  }
}

private struct ProbeFrame: ViewModifier {
  @Environment(\.frameProbe) private var probe
  let name: String

  func body(content: Content) -> some View {
    if let probe {
      content.onGeometryChange(for: CGRect.self) { $0.frame(in: .global) } action: { probe.record(name, $0) }
    } else {
      content
    }
  }
}

extension View {
  /// Reports this view's frame under `name` to the harness's `FrameProbe`; nothing without one.
  func probeFrame(_ name: String) -> some View {
    modifier(ProbeFrame(name: name))
  }
}
