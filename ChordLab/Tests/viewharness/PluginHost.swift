import AppKit
import SwiftUI

// MARK: - The plug-in's hosting

/// Stands in for `ChordLabViewController`, which cannot be compiled here (its base class is iPlug2's
/// `IPlugCocoaViewController`): the same `loadView` (a plain `NSView`) and the same `viewDidLoad`, minus what needs
/// iPlug2 (the parameters come from the plug-in, here the one `kNumParams` parameter by hand; `BundleLocator` is not
/// initialised: there is no plug-in bundle). The editor's root goes through the controller's own hosting code,
/// `EditorHosting.install`.
@MainActor final class EditorControllerReplica: NSViewController {
  let state: ChordLabState
  let rootView: AnyView

  init(state: ChordLabState, rootView: AnyView) {
    self.state = state
    self.rootView = rootView
    super.init(nibName: nil, bundle: nil)
  }

  required init?(coder: NSCoder) { fatalError("not used") }

  override func loadView() {
    self.view = NSView()
  }

  override func viewDidLoad() {
    super.viewDidLoad()
    for idx in 0..<kNumParams { state.params.append(Param(id: idx, name: "Param \(idx)")) }
    EditorHosting.install(rootView, in: self)
  }
}

/// What one plug-in instance holds for its editor: the document's snapshot and the link state the shell pushes.
struct HostedInstance {
  var snapshot: Data
  var link: Data?
}

/// The editor of one instance in a host window, opened and closed the way `ChordLab::OpenWindow` and
/// `ChordLab::CloseWindow` (VST3) do it: a new controller and state per open; `vc.view.frame` set to the editor size
/// before anything else (it loads the view, so `viewDidLoad` hosts the editor before the frame is set), the view added
/// to the host's parent view, then `OnUIOpen` pushes the snapshot, what was heard and the link state, inline on the
/// main thread. The editor asks for a snapshot when it appears (`ContentView.onAppear`) and the shell answers inside
/// the send with the snapshot, the heard state and the link state again (`OnMessage`, kMsgTagRequestSnapshot). Later
/// link states arrive as `OnIdle` pushes (`push`). Closing releases the controller and leaves its view in the parent,
/// as iPlug2's VST3 close does; the host then takes its window down. The window is offscreen and never ordered in.
@MainActor final class PluginWindow {
  let size: CGSize
  let state: ChordLabState
  let shell = ShellStub()
  let probe = FrameProbe()
  let defaults = MemoryDefaults()
  /// Every proposal the editor's root was measured or placed with, in order.
  let proposals = ProposalLog()
  private(set) var instance: HostedInstance
  private let window: NSWindow
  private var controller: EditorControllerReplica?

  /// The host's parent view (what the plug-in's view is added to).
  var parent: NSView { window.contentView! }
  /// The hosting view inside the controller's view.
  var hostingView: NSView? { controller?.view.subviews.first }
  /// Whether the hosting view's controller is alive as the one child of the editor's controller.
  var hostingIsContained: Bool {
    guard let controller, let hosting = hostingView else { return false }
    return controller.children.count == 1 && controller.children[0].view === hosting
  }

  init(_ h: Harness, size: CGSize, instance: HostedInstance) {
    self.size = size
    self.instance = instance
    window = NSWindow(contentRect: CGRect(x: -30000, y: -30000, width: size.width, height: size.height),
                      styleMask: [.borderless], backing: .buffered, defer: false)
    window.isReleasedWhenClosed = false
    window.isRestorable = false
    window.appearance = NSAppearance(named: .darkAqua)
    window.contentView = NSView(frame: CGRect(origin: .zero, size: size))
    let shell = self.shell
    state = ChordLabState(sendCommand: { tag, data in shell.receive(tag: tag, data: data) },
                          mouseButtonsDown: { false }, clipboardToken: "doc-1", defaults: defaults,
                          followDismissals: FollowDismissals())
    shell.onRequestSnapshot = { [unowned self] in self.pushAll() }
    let root = PassThroughLayout(log: proposals) {
      ContentView()
        .environmentObject(self.state)
        .environment(\.frameProbe, self.probe)
        .defaultAppStorage(self.defaults)
    }
    let vc = EditorControllerReplica(state: state, rootView: AnyView(root))
    controller = vc
    vc.view.frame = CGRect(origin: .zero, size: size)
    parent.addSubview(vc.view)
    pushAll()                                                         // OnUIOpen
    settle()
  }

  /// PushSnapshot, PushHeard, PushLinkState, in the shell's order.
  private func pushAll() {
    state.applySnapshot(json: instance.snapshot)
    state.applyHeard(json: PluginWindow.heardInactive)
    if let link = instance.link { state.applyLinkState(json: link) }
  }

  /// An OnIdle push of a new link state.
  func push(link: Data) {
    instance.link = link
    state.applyLinkState(json: link)
    settle()
  }

  /// Lets layout run until the reported frames stop changing (as `Harness.render`).
  func settle() {
    var last: [String: CGRect] = [:]
    var stable = 0
    for _ in 0..<60 {
      parent.layoutSubtreeIfNeeded()
      RunLoop.main.run(until: Date().addingTimeInterval(0.03))
      if probe.frames == last { stable += 1 } else { stable = 0 }
      last = probe.frames
      if stable >= 4 { break }
    }
  }

  var isVisible: Bool { window.isVisible }

  /// ChordLab::CloseWindow (VST3): the controller goes, its view stays in the parent; then the host's window goes.
  func close() {
    controller = nil
    window.contentView = nil
    window.close()
  }

  static let heardInactive = Data(#"{"schema":1,"active":false,"held":[],"heldNames":[],"chord":null,"chordHeld":false,"key":null,"onsets":0}"#.utf8)
}

/// The proposals one view was measured or placed with.
final class ProposalLog {
  private(set) var entries: [ProposedViewSize] = []
  func record(_ proposal: ProposedViewSize) { entries.append(proposal) }
  func clear() { entries = [] }
}

/// Layout-neutral: measures and places its one subview with exactly the proposal it gets, and records it.
struct PassThroughLayout: Layout {
  let log: ProposalLog

  func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
    log.record(proposal)
    return subviews.first?.sizeThatFits(proposal) ?? .zero
  }

  func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
    log.record(proposal)
    subviews.first?.place(at: bounds.origin, proposal: proposal)
  }
}

/// Measures and places its one subview, centred, with a fixed proposal whatever it is offered: a container that
/// proposes `width` x `height` (nil: unspecified).
struct FixedProposalLayout: Layout {
  let width: CGFloat?
  let height: CGFloat?

  func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
    proposal.replacingUnspecifiedDimensions()
  }

  func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
    subviews.first?.place(at: CGPoint(x: bounds.midX, y: bounds.midY), anchor: .center,
                          proposal: ProposedViewSize(width: width, height: height))
  }
}

// MARK: - Modes

extension Scenario {
  /// The plug-in-hosted modes: the editor in the controller's hosting, opened and closed in the orders seen in Live.
  static let hosted: [Scenario] = [
    .custom(name: "host-open-panel") { h, size in
      // A first open of an instance that already follows: the window opens straight into the panel.
      let w = PluginWindow(h, size: size, instance: HostedInstance(snapshot: h.data("snapshot-tracks.json"),
                                                                   link: follower(h, status: "playing")))
      h.checkHosted(w, step: "open", shows: .panel)
      w.close()
    },
    .custom(name: "host-editor-then-panel") { h, size in
      // The editor first, then the instance follows (the shell's push after a banner click or a follow made
      // elsewhere): the same open window turns into the panel; then Change… picks another track.
      let w = PluginWindow(h, size: size, instance: HostedInstance(snapshot: h.data("snapshot-tracks.json"),
                                                                   link: nil))
      h.checkHosted(w, step: "editor", shows: .editor(banner: false, followMenu: false))
      w.proposals.clear()
      w.push(link: follower(h, status: "playing"))
      h.checkHosted(w, step: "panel", shows: .panel)
      w.proposals.clear()
      w.push(link: h.json("link-state-follower.json") { object in
        var following = object["following"] as! [String: Any]
        following["lane"] = "uid-3"
        following["laneName"] = "Lead"
        following["role"] = "lead"
        following["switchTo"] = NSNull()
        object["following"] = following
      })
      h.checkHosted(w, step: "changed", shows: .panel)
      w.close()
    },
    .custom(name: "host-reopen") { h, size in
      // One process, in order: A opens with the editor and turns into the panel; closed; B opens straight into
      // the panel; closed; C, whose window was never opened, opens straight into its panel (muted); B again.
      let a = PluginWindow(h, size: size, instance: HostedInstance(snapshot: h.data("snapshot-tracks.json"),
                                                                   link: nil))
      a.push(link: follower(h, status: "playing"))
      h.checkHosted(a, step: "a", shows: .panel)
      a.close()
      let bInstance = HostedInstance(snapshot: h.data("snapshot-tracks.json"), link: follower(h, status: "playing"))
      let b = PluginWindow(h, size: size, instance: bInstance)
      h.checkHosted(b, step: "b", shows: .panel)
      b.close()
      let c = PluginWindow(h, size: size, instance: HostedInstance(snapshot: h.data("snapshot-home-empty.json"),
                                                                   link: follower(h, status: "muted")))
      h.checkHosted(c, step: "c", shows: .panel)
      c.close()
      let again = PluginWindow(h, size: size, instance: bInstance)
      h.checkHosted(again, step: "b-again", shows: .panel)
      again.close()
    },
    .custom(name: "host-reopen-banner") { h, size in
      // A fresh instance that sees ChordLab A opens with the Follow banner; closed and opened again, it still does.
      let fresh = HostedInstance(snapshot: h.data("snapshot-home-empty.json"), link: h.freshLink())
      let first = PluginWindow(h, size: size, instance: fresh)
      h.checkHosted(first, step: "first", shows: .editor(banner: true, followMenu: true))
      first.close()
      let again = PluginWindow(h, size: size, instance: fresh)
      h.checkHosted(again, step: "again", shows: .editor(banner: true, followMenu: true))
      again.close()
    },
    .custom(name: "panel-unspecified") { h, size in
      // The panel in a container that proposes no width (nor height), and in one that proposes far more than the
      // window: never wider than its cap or the window, its texts wrapped, its card holding every row.
      let defaults = MemoryDefaults()
      let state = ChordLabState(sendCommand: { _, _ in }, mouseButtonsDown: { false }, clipboardToken: "doc-1",
                                defaults: defaults, followDismissals: FollowDismissals())
      state.applySnapshot(json: h.data("snapshot-tracks.json"))
      state.applyLinkState(json: follower(h, status: "playing"))
      guard let model = FollowerPanelModel(state.linkState, snapshot: state.snapshot,
                                           ownLineage: state.clipboardToken) else {
        h.check(false, "the follower fixture makes a panel")
        return
      }
      for (label, width) in [("unspecified", nil), ("2000 pt", CGFloat(2000))] as [(String, CGFloat?)] {
        let probe = FrameProbe()
        let panel = FollowerPanelView(model: model)
          .environmentObject(state)
          .environment(\.frameProbe, probe)
          .defaultAppStorage(defaults)
        // What the panel reports for no proposal, measured apart (no probe), as a hosting view asks for it.
        let ideal = NSHostingView(rootView: FollowerPanelView(model: model).environmentObject(state)
          .defaultAppStorage(defaults)).fittingSize
        let root = FixedProposalLayout(width: width, height: nil) { panel }.background(PaletteView.background)
        let (host, window) = h.render(root, size: size, probe: probe)
        let shell = ShellStub()
        let r = Render(scenario: "panel-unspecified", size: size, state: state, shell: shell, frames: probe.frames,
                       host: host)
        print("  -- \(label)")
        h.printFrames(r.frames)
        h.writePNG(host, to: h.out.appendingPathComponent("panel-unspecified-\(width == nil ? "nil" : "2000")-\(Int(size.width))x\(Int(size.height)).png"))
        h.checkPanelCard(r, label: label)
        if width == nil, let notes = r.frames["panel.row.notes"], let status = r.frames["panel.row.status"] {
          let drawn = notes.maxY - status.minY + 2 * 20 + 2 * 12
          h.check(ideal.height >= drawn - 0.5,
                  String(format: "the panel's ideal size (%.0f x %.0f) holds the card it draws at that size (%.0f pt high)",
                         ideal.width, ideal.height, drawn))
          h.check(ideal.width <= FollowerPanelView.maxContentWidth + 2 * 12 + 0.5,
                  String(format: "the panel's ideal width (%.0f) is its cap and margins at most", ideal.width))
        }
        window.close()
      }
    },
  ]
}

// MARK: - Checks

extension Harness {
  /// One render of a plug-in-hosted editor: the PNG `<mode>-<step>-<width>x<height>.png`, the hosting view filling the
  /// editor, the root laid out at the window's size only, and the panel's or the editor's checks.
  /// What a plug-in-hosted render shows.
  enum Shows {
    case panel
    case editor(banner: Bool, followMenu: Bool)
  }

  func checkHosted(_ w: PluginWindow, step: String, shows: Shows) {
    print("  -- \(step)")
    let frames = w.probe.frames
    printFrames(frames)
    let r = Render(scenario: currentMode, size: w.size, state: w.state, shell: w.shell, frames: frames,
                   host: w.hostingView ?? w.parent)
    writePNG(w.parent, to: out.appendingPathComponent("\(currentMode)-\(step)-\(Int(w.size.width))x\(Int(w.size.height)).png"))
    check(!w.isVisible, "the window was never on screen")
    if let hosting = w.hostingView {
      check(hosting.frame == CGRect(origin: .zero, size: w.size),
            String(format: "the hosting view fills the editor (%.0f x %.0f)", hosting.frame.width, hosting.frame.height))
    } else {
      check(false, "the controller's view holds the hosting view")
    }
    check(w.hostingIsContained, "the hosting controller is the one child of the editor's controller")
    checkHostProposals(w)
    switch shows {
    case .panel:
      checkPanel(r, rows: ["status", "actions", "controls", "notes"], absent: [])
      checkPanelCard(r, label: step)
      checkNoBand(w.parent, r)
    case .editor(let banner, let followMenu):
      checkEditor(r, banner: banner, followMenu: followMenu)
      if banner { checkBanner(r) }
    }
  }

  /// The editor's root is measured and placed with the window's size only: never an unspecified, zero or infinite
  /// proposal (the hosting view asks for those to make size constraints from the content).
  func checkHostProposals(_ w: PluginWindow) {
    func text(_ p: ProposedViewSize) -> String {
      let f = { (v: CGFloat?) in v.map { String(format: "%.0f", $0) } ?? "nil" }
      return "\(f(p.width)) x \(f(p.height))"
    }
    let seen = w.proposals.entries.map(text)
    var distinct: [String] = []
    for p in seen where !distinct.contains(p) { distinct.append(p) }
    let expected = text(ProposedViewSize(w.size))
    check(!seen.isEmpty && distinct == [expected],
          "the editor's root is laid out at the window's size only (\(seen.count) proposals: \(distinct.joined(separator: ", ")))")
  }

  /// The card (`panel.content`): at most its cap, inside the window, holding every row with its 20-pt padding above
  /// and below (the size it reports is the size it draws), the routing reminder wrapped to it.
  func checkPanelCard(_ r: Render, label: String) {
    guard let content = r.frames["panel.content"] else {
      check(false, "\(label): the panel was laid out")
      return
    }
    let bounds = r.bounds
    check(content.width <= FollowerPanelView.maxContentWidth + 0.5,
          String(format: "%@: the card is at most %.0f pt wide (%.1f)", label, FollowerPanelView.maxContentWidth, content.width))
    check(content.minX >= bounds.minX - 0.5 && content.maxX <= bounds.maxX + 0.5,
          String(format: "%@: the card lies across the window, not past it (x %.1f to %.1f)", label, content.minX, content.maxX))
    let rows = r.frames.filter { $0.key.hasPrefix("panel.row.") }.map(\.value)
    if let top = rows.map(\.minY).min(), let bottom = rows.map(\.maxY).max() {
      check(top - content.minY >= 19.5 && content.maxY - bottom >= 19.5,
            String(format: "%@: the card holds its rows with its padding (%.1f pt above, %.1f below)", label,
                   top - content.minY, content.maxY - bottom))
    }
    if let reminder = r.frames["panel.note.reminder"] {
      check(reminder.width <= content.width - 40 + 0.5,
            String(format: "%@: the routing reminder wraps to the card (%.1f pt wide)", label, reminder.width))
    }
  }

  /// Nothing of the card reaches the window's left or right edge: at the card's middle height the edges show the
  /// window's background, as its corners do.
  func checkNoBand(_ view: NSView, _ r: Render) {
    guard let content = r.frames["panel.content"],
          let rep = view.bitmapImageRepForCachingDisplay(in: view.bounds) else { return }
    view.cacheDisplay(in: view.bounds, to: rep)
    let scale = CGFloat(rep.pixelsWide) / view.bounds.width
    func colour(_ x: CGFloat, _ y: CGFloat) -> String {
      guard let c = rep.colorAt(x: Int(x * scale), y: Int(y * scale))?.usingColorSpace(.deviceRGB) else { return "?" }
      return String(format: "%02x%02x%02x", Int(c.redComponent * 255), Int(c.greenComponent * 255),
                    Int(c.blueComponent * 255))
    }
    let background = colour(3, 3)
    let left = colour(3, content.midY)
    let right = colour(r.size.width - 4, content.midY)
    check(left == background && right == background,
          "the window's edges beside the card show the background (\(left), \(right); corner \(background))")
  }
}
