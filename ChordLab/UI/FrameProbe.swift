import SwiftUI

/// Named frames for the render harness (ChordLab/Tests/viewharness): a view marked `.probeFrame("name")` reports its
/// frame, in the window's coordinates (top-left origin), to the `FrameProbe` the harness puts in the environment. In
/// the plug-in there is none, and the modifier adds nothing to the view.
final class FrameProbe {
  /// The last frame each name reported; only views that are laid out report, and one that leaves the layout is
  /// dropped.
  private(set) var frames: [String: CGRect] = [:]

  /// Which view reported each name last (one view's leaving never drops another's frame of the same name).
  private var owners: [String: UUID] = [:]

  func record(_ name: String, _ frame: CGRect, owner: UUID) {
    frames[name] = frame
    owners[name] = owner
  }

  func reset() {
    frames = [:]
    owners = [:]
  }

  /// The view `owner` named `name` left the layout (another `ViewThatFits` candidate was chosen): its frame no longer
  /// counts, unless another view of that name has reported since.
  func remove(_ name: String, owner: UUID) {
    guard owners[name] == owner else { return }
    frames[name] = nil
    owners[name] = nil
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
  @State private var owner = UUID()
  let name: String

  func body(content: Content) -> some View {
    if let probe {
      content
        .onGeometryChange(for: CGRect.self) { $0.frame(in: .global) } action: { probe.record(name, $0, owner: owner) }
        .onDisappear { probe.remove(name, owner: owner) }
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
