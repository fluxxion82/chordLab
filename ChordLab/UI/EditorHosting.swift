import SwiftUI

/// How the plug-in hosts the editor's SwiftUI root in its view controller (`ChordLabViewController.viewDidLoad`; the
/// render harness hosts it through here too, ChordLab/Tests/viewharness/PluginHost.swift): a hosting controller, a
/// child of the editor's controller, its view pinned to the controller's view, whose frame the shell sets
/// (`ChordLab::OpenWindow`, `OnParentWindowResize`).
///
/// The host decides the editor's size, never its content, so the hosting controller makes no size constraints from the
/// content (`sizingOptions` empty). With the default options it measured the root for a zero, an infinite and an
/// unspecified proposal on every constraint pass, to turn them into minimum, maximum and intrinsic size constraints
/// that only contradicted the frame (the panel's minimum height came to 4133 pt, the editor's to 1201 pt, at a priority
/// just under the frame's); those measurements were the only place an unspecified proposal reached the editor's views.
enum EditorHosting {
  /// Hosts `root` in `owner`'s view. The hosting controller becomes `owner`'s child, so it lives as long as the editor's
  /// controller does (a hosting view whose controller is gone is not a set-up either framework describes).
  static func install<Root: View>(_ root: Root, in owner: PlatformViewController) {
    let hostingController = PlatformHostingController(rootView: root)
    hostingController.sizingOptions = []
    owner.addChild(hostingController)
    owner.view.addSubview(hostingController.view)
    hostingController.view.pinToSuperviewEdges()
    #if os(iOS)
    hostingController.didMove(toParent: owner)
    #endif
  }
}
