import AppKit
import SwiftUI

/// The editor's render harness (per-track instruments plan M3, Task 11.6). Compiles with the editor's SwiftUI views
/// and Core (build.sh; never part of the Xcode project), feeds a `ChordLabState` the engine's snapshot and link-state
/// fixtures through the same `apply…` calls the view controller makes, renders `ContentView` in an offscreen window
/// that is never ordered in, writes one PNG per mode and size, prints the frames the views report through
/// `probeFrame` (UI/FrameProbe.swift), and checks them. Exit 0 when every check passes, 1 when one fails, 2 for a usage
/// error.
///
///     viewharness [--fixtures <dir>] <png dir> [<mode>...]
///     viewharness --list
///
/// It writes nothing but `<png dir>/<mode>-<width>x<height>.png` (the directory must exist), and keeps its preferences
/// in memory (`MemoryDefaults`, also as the views' `@AppStorage`).
@main
struct ViewHarness {
  static let sizes = [CGSize(width: 700, height: 632), CGSize(width: 900, height: 800)]
  /// The window's padding around the editor's rows (`ContentView`).
  static let padding: CGFloat = 12
  /// One full palette row (`TransportBarView`'s and `KeyTransposeRow`'s promise at the 700 x 632 minimum).
  static let minPaletteViewport: CGFloat = 76

  static func main() {
    var args = Array(CommandLine.arguments.dropFirst())
    if args == ["--list"] {
      for scenario in Scenario.all { print(scenario.name) }
      exit(0)
    }
    let source = URL(fileURLWithPath: #filePath)
    let repoChordLab = source.deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
    var fixtures = repoChordLab.deletingLastPathComponent().appendingPathComponent("engine/tests/fixtures")
    if let index = args.firstIndex(of: "--fixtures") {
      guard index + 1 < args.count else { usage("--fixtures needs a directory") }
      fixtures = URL(fileURLWithPath: args[index + 1])
      args.removeSubrange(index...(index + 1))
    }
    guard let outPath = args.first else { usage("no PNG directory given") }
    var isDirectory: ObjCBool = false
    guard FileManager.default.fileExists(atPath: outPath, isDirectory: &isDirectory), isDirectory.boolValue else {
      usage("the PNG directory '\(outPath)' does not exist")
    }
    let out = URL(fileURLWithPath: outPath, isDirectory: true)
    let names = Array(args.dropFirst())
    var scenarios = names.isEmpty ? Scenario.all : []
    for name in names {
      guard let scenario = Scenario.all.first(where: { $0.name == name }) else {
        usage("unknown mode '\(name)' (try --list)")
      }
      scenarios.append(scenario)
    }

    let app = NSApplication.shared
    app.setActivationPolicy(.prohibited)   // no Dock icon, no menu bar, never frontmost
    let failed = MainActor.assumeIsolated { () -> Int in
      let harness = Harness(fixtures: fixtures, out: out,
                            header: repoChordLab.appendingPathComponent("ChordLab-Shared.h"))
      harness.checkConstants()
      for scenario in scenarios {
        for size in sizes { harness.run(scenario, size: size) }
      }
      return harness.report()
    }
    exit(failed == 0 ? 0 : 1)
  }

  static func usage(_ problem: String) -> Never {
    FileHandle.standardError.write(Data("viewharness: \(problem)\nusage: viewharness [--fixtures <dir>] <png dir> [<mode>...] | --list\n".utf8))
    exit(2)
  }
}

// MARK: - Scenarios

/// One mode: what the state is fed, and what the frames must show.
struct Scenario {
  let name: String
  /// Feeds the state (and the shell stub) before the render.
  let setup: @MainActor (Harness, ChordLabState, ShellStub) -> Void
  /// The checks on one render.
  let checks: @MainActor (Harness, Render) -> Void

  /// The link state for a follower of ChordLab A's Bass (the follower fixture) with `status` and no copy to switch to.
  @MainActor static func follower(_ h: Harness, status: String, switchTo: Bool = false) -> Data {
    h.json("link-state-follower.json") { object in
      var following = object["following"] as! [String: Any]
      following["status"] = status
      if !switchTo { following["switchTo"] = NSNull() }
      object["following"] = following
    }
  }

  static let all: [Scenario] = [
    Scenario(name: "editor", setup: { h, state, _ in
      state.applySnapshot(json: h.data("snapshot-tracks.json"))
    }, checks: { h, r in
      h.checkEditor(r, banner: false, followMenu: false)
      h.checkKeyGate(r, following: false)
    }),
    Scenario(name: "banner-fresh", setup: { h, state, _ in
      // A fresh instance (no chords, no tracks) that sees ChordLab A and is no copy.
      state.applySnapshot(json: h.data("snapshot-home-empty.json"))
      state.applyLinkState(json: h.json("link-state-copy.json") { object in
        var me = object["self"] as! [String: Any]
        me["copyOf"] = NSNull()
        object["self"] = me
      })
    }, checks: { h, r in
      h.checkEditor(r, banner: true, followMenu: true)
      h.checkBanner(r)
    }),
    Scenario(name: "banner-copy", setup: { h, state, _ in
      // A copy of ChordLab A that holds A's progression (not fresh): the copy's wording.
      state.applySnapshot(json: h.data("snapshot-home-progression.json"))
      state.applyLinkState(json: h.data("link-state-copy.json"))
    }, checks: { h, r in
      h.checkEditor(r, banner: true, followMenu: true)
      h.checkBanner(r)
    }),
    Scenario(name: "banner-compact", setup: { h, state, shell in
      // The fullest banner: five lanes of ChordLab A, More… (a second ChordLab), and the sentence a refused click
      // leaves under it (the stub shell answers the follow inline with the link state unchanged, as the plug-in
      // answers a stale leader lineage).
      state.applySnapshot(json: h.data("snapshot-home-empty.json"))
      let link = h.json("link-state-copy.json") { object in
        var me = object["self"] as! [String: Any]
        me["copyOf"] = NSNull()
        object["self"] = me
        var leaders = object["leaders"] as! [[String: Any]]
        var lanes = leaders[0]["lanes"] as! [[String: Any]]
        lanes.append(["audible": true, "chords": false, "followers": 0, "lane": "uid-4", "name": "Keys", "role": "other"])
        lanes.append(["audible": true, "chords": false, "followers": 0, "lane": "uid-5", "name": "Arp", "role": "lead"])
        leaders[0]["lanes"] = lanes
        leaders.append(["id": "id-second", "label": "ChordLab C", "lineage": "lineage-second",
                        "lanes": [["audible": true, "chords": true, "followers": 0, "lane": "chords", "name": "Chords",
                                   "role": ""]]])
        object["leaders"] = leaders
      }
      state.applyLinkState(json: link)
      shell.answerLink = link
      h.clickFirstBannerButton(state)
    }, checks: { h, r in
      h.checkEditor(r, banner: true, followMenu: true)
      h.checkBanner(r)
      h.check(r.frames["banner.notice"] != nil, "the refusal sentence shows under the banner")
      h.check(r.state.followNotice == FollowAttempt.refusalText(leaderLabel: "ChordLab A"),
              "the refusal sentence is FollowAttempt's for ChordLab A")
      h.checkFollowRequest(r)
    }),
    Scenario(name: "menu", setup: { h, state, _ in
      // A leader whose chords and Lead others play (routed): no banner, the Follow menu with its warning.
      state.applySnapshot(json: h.data("snapshot-tracks.json"))
      state.applyLinkState(json: h.data("link-state-leader.json"))
    }, checks: { h, r in
      h.checkEditor(r, banner: false, followMenu: true)
      h.checkTransportLayouts(r)
      h.check(FollowOptions.menuNotes(r.state.linkState, notice: r.state.followNotice)
                == [FollowOptions.warning(r.state.linkState)!],
              "the menu's first, disabled entry is the warning")
    }),
    Scenario(name: "panel-playing", setup: { h, state, _ in
      state.applySnapshot(json: h.data("snapshot-tracks.json"))
      state.applyLinkState(json: follower(h, status: "playing"))
    }, checks: { h, r in
      h.checkPanel(r, rows: ["status", "actions", "controls", "notes"], absent: ["switch", "notice"])
      h.checkKeyGate(r, following: true)
    }),
    Scenario(name: "panel-muted", setup: { h, state, _ in
      state.applySnapshot(json: h.data("snapshot-home-empty.json"))
      state.applyLinkState(json: follower(h, status: "muted"))
    }, checks: { h, r in
      h.checkPanel(r, rows: ["status", "actions", "controls", "notes"], absent: ["switch", "notice"])
      h.check(r.frames["panel.note.kept"] == nil, "an instance that was empty promises nothing kept")
    }),
    Scenario(name: "panel-waiting", setup: { h, state, _ in
      state.applySnapshot(json: h.data("snapshot-home-progression.json"))
      state.applyLinkState(json: h.data("link-state-waiting.json"))
    }, checks: { h, r in
      h.checkPanel(r, rows: ["status", "actions", "controls", "notes"], absent: ["switch"])
      h.check(r.frames["panel.note.edit"] == nil, "no edit note while waiting")
    }),
    Scenario(name: "panel-lane-missing", setup: { h, state, _ in
      state.applySnapshot(json: h.data("snapshot-tracks.json"))
      state.applyLinkState(json: follower(h, status: "laneMissing"))
    }, checks: { h, r in
      h.checkPanel(r, rows: ["status", "actions", "controls", "notes"], absent: ["switch"])
    }),
    Scenario(name: "panel-project-changed", setup: { h, state, _ in
      state.applySnapshot(json: h.data("snapshot-tracks.json"))
      state.applyLinkState(json: follower(h, status: "projectChanged"))
    }, checks: { h, r in
      h.checkPanel(r, rows: ["status", "actions", "controls", "notes"], absent: ["switch"])
    }),
    Scenario(name: "panel-switch-to", setup: { h, state, _ in
      state.applySnapshot(json: h.data("snapshot-tracks.json"))
      state.applyLinkState(json: follower(h, status: "playing", switchTo: true))
    }, checks: { h, r in
      h.checkPanel(r, rows: ["status", "switch", "actions", "controls", "notes"], absent: [])
    }),
    Scenario(name: "panel-crowded", setup: { h, state, shell in
      // Everything the panel can hold at once: the switch as a menu (the copy names its lanes differently), the
      // refusal sentence of a Change… the stub shell refused, the incomplete blocks and an error line, the kept note.
      state.applySnapshot(json: h.data("snapshot-tracks.json"))
      let link = h.json("link-state-follower.json") { object in
        var leaders = object["leaders"] as! [[String: Any]]
        leaders[1]["lanes"] = [["audible": true, "chords": true, "followers": 0, "lane": "chords", "name": "Chords",
                                "role": ""],
                               ["audible": true, "chords": false, "followers": 0, "lane": "uid-9", "name": "Bass 2",
                                "role": "bass"]]
        object["leaders"] = leaders
      }
      state.applyLinkState(json: link)
      shell.answerLink = link
      if let option = FollowOptions.groups(state.linkState).first?.options.first(where: { !$0.isCurrent }) {
        state.follow(option)
      }
      state.applyStatus(json: Data(#"{"schema":1,"incompleteBlocks":3}"#.utf8))
      state.applyError(json: Data(#"{"schema":1,"error":"a refused command's text, long enough to need a second line in the narrowest panel"}"#.utf8))
    }, checks: { h, r in
      h.checkPanel(r, rows: ["status", "switch", "actions", "notice", "controls", "statusLines", "notes"], absent: [])
      if case .choose? = FollowerPanelModel(r.state.linkState, snapshot: r.state.snapshot)?.switchOffer?.action {
        h.check(true, "the switch is a menu over the copy's lanes")
      } else {
        h.check(false, "the switch is a menu over the copy's lanes")
      }
    }),
    Scenario(name: "tab-mark", setup: { h, state, _ in
      // A leader whose chords and Lead (track 2) others play.
      state.applySnapshot(json: h.data("snapshot-tracks.json"))
      state.applyLinkState(json: h.data("link-state-leader.json"))
    }, checks: { h, r in
      h.checkEditor(r, banner: false, followMenu: true)
      h.checkTabMarks(r, marked: [.chords, .track(2)], unmarked: [.track(1)])
    }),
  ]
}

// MARK: - The stub plug-in connection

/// Stands in for the plug-in: records what the editor sends, and answers a link message inline (as the shell does,
/// inside the send) with `answerLink` when set, so a follow it does not apply comes back refused.
final class ShellStub {
  struct Message {
    let tag: Int
    let json: [String: Any]?
  }

  weak var state: ChordLabState?
  private(set) var sent: [Message] = []
  var answerLink: Data?

  @MainActor func receive(tag: Int, data: Data?) {
    let object = data.flatMap { try? JSONSerialization.jsonObject(with: $0) as? [String: Any] }
    sent.append(Message(tag: tag, json: object))
    if tag == kMsgTagLink, let answer = answerLink { state?.applyLinkState(json: answer) }
  }

  func clear() { sent = [] }
}

/// Preferences in memory only: the harness never writes the user's defaults (`ChordLabState.defaults` and the views'
/// `@AppStorage`).
final class MemoryDefaults: UserDefaults {
  private var values: [String: Any] = [:]

  override func object(forKey defaultName: String) -> Any? { values[defaultName] }
  override func set(_ value: Any?, forKey defaultName: String) { values[defaultName] = value }
  override func removeObject(forKey defaultName: String) { values[defaultName] = nil }
  override func string(forKey defaultName: String) -> String? { values[defaultName] as? String }
  override func bool(forKey defaultName: String) -> Bool { values[defaultName] as? Bool ?? false }
  override func integer(forKey defaultName: String) -> Int { values[defaultName] as? Int ?? 0 }
  override func double(forKey defaultName: String) -> Double { values[defaultName] as? Double ?? 0 }
  override func dictionaryRepresentation() -> [String: Any] { values }
}

// MARK: - Rendering

/// One render: the state it drew, the frames the views reported, the window's size.
struct Render {
  let scenario: String
  let size: CGSize
  let state: ChordLabState
  let shell: ShellStub
  let frames: [String: CGRect]
  let host: NSView
  var bounds: CGRect { CGRect(origin: .zero, size: size) }
}

@MainActor final class Harness {
  let fixtures: URL
  let out: URL
  let header: URL
  private var passed = 0
  private var failures: [String] = []
  private var current = ""

  init(fixtures: URL, out: URL, header: URL) {
    self.fixtures = fixtures
    self.out = out
    self.header = header
  }

  // MARK: Fixtures

  func data(_ name: String) -> Data {
    guard let data = try? Data(contentsOf: fixtures.appendingPathComponent(name)) else {
      FileHandle.standardError.write(Data("viewharness: cannot read the fixture \(name) in \(fixtures.path)\n".utf8))
      exit(2)
    }
    return data
  }

  /// The fixture `name` as a JSON object, changed by `edit`, back as data.
  func json(_ name: String, edit: (inout [String: Any]) -> Void) -> Data {
    guard var object = try? JSONSerialization.jsonObject(with: data(name)) as? [String: Any] else {
      FileHandle.standardError.write(Data("viewharness: the fixture \(name) is not a JSON object\n".utf8))
      exit(2)
    }
    edit(&object)
    return try! JSONSerialization.data(withJSONObject: object)
  }

  // MARK: Running

  func run(_ scenario: Scenario, size: CGSize) {
    current = "\(scenario.name) \(Int(size.width))x\(Int(size.height))"
    print("== \(current)")
    let defaults = MemoryDefaults()
    let shell = ShellStub()
    let state = ChordLabState(sendCommand: { tag, data in shell.receive(tag: tag, data: data) },
                              mouseButtonsDown: { false }, clipboardToken: "doc-1", defaults: defaults)
    shell.state = state
    scenario.setup(self, state, shell)
    let probe = FrameProbe()
    let root = ContentView()
      .environmentObject(state)
      .environment(\.frameProbe, probe)
      .defaultAppStorage(defaults)
      .frame(width: size.width, height: size.height)
    let (host, window) = render(root, size: size, probe: probe)
    let frames = probe.frames
    for name in frames.keys.sorted() {
      let f = frames[name]!
      print("  frame " + name.padding(toLength: 30, withPad: " ", startingAt: 0)
            + String(format: "x %7.1f  y %7.1f  w %7.1f  h %7.1f", f.minX, f.minY, f.width, f.height))
    }
    let file = out.appendingPathComponent("\(scenario.name)-\(Int(size.width))x\(Int(size.height)).png")
    let render = Render(scenario: scenario.name, size: size, state: state, shell: shell, frames: frames, host: host)
    writePNG(host, to: file)
    check(window.isVisible == false, "the window was never on screen")
    scenario.checks(self, render)
    window.contentView = nil
    window.close()
  }

  /// Hosts `root` in an offscreen window that is never ordered in, and lets layout settle until the reported frames
  /// stop changing.
  func render<V: View>(_ root: V, size: CGSize, probe: FrameProbe) -> (NSView, NSWindow) {
    let host = NSHostingView(rootView: root)
    host.frame = CGRect(origin: .zero, size: size)
    let window = NSWindow(contentRect: CGRect(x: -30000, y: -30000, width: size.width, height: size.height),
                          styleMask: [.borderless], backing: .buffered, defer: false)
    window.isReleasedWhenClosed = false
    window.isRestorable = false
    window.appearance = NSAppearance(named: .darkAqua)
    window.contentView = host
    var last: [String: CGRect] = [:]
    var stable = 0
    for _ in 0..<60 {
      host.layoutSubtreeIfNeeded()
      RunLoop.main.run(until: Date().addingTimeInterval(0.03))
      if probe.frames == last { stable += 1 } else { stable = 0 }
      last = probe.frames
      if stable >= 4 { break }
    }
    return (host, window)
  }

  /// The view's pixels as a PNG; a render with a single colour (the display asleep: run under `caffeinate -d -u`)
  /// fails.
  func writePNG(_ view: NSView, to url: URL) {
    guard let rep = view.bitmapImageRepForCachingDisplay(in: view.bounds) else {
      check(false, "the view could be drawn into a bitmap")
      return
    }
    view.cacheDisplay(in: view.bounds, to: rep)
    var colours = Set<UInt32>()
    let step = max(1, rep.pixelsWide / 64)
    for y in stride(from: 0, to: rep.pixelsHigh, by: step) {
      for x in stride(from: 0, to: rep.pixelsWide, by: step) {
        guard let c = rep.colorAt(x: x, y: y)?.usingColorSpace(.deviceRGB) else { continue }
        colours.insert(UInt32(c.redComponent * 255) << 16 | UInt32(c.greenComponent * 255) << 8
                       | UInt32(c.blueComponent * 255))
      }
    }
    check(colours.count > 8, "the render is not empty (\(colours.count) colours sampled; an empty render means the display slept: run under caffeinate -d -u)")
    guard let png = rep.representation(using: .png, properties: [:]) else {
      check(false, "the render could be encoded as PNG")
      return
    }
    do {
      try png.write(to: url)
      print("  wrote \(url.path)")
    } catch {
      check(false, "wrote \(url.path): \(error)")
    }
  }

  // MARK: Checks

  func check(_ ok: Bool, _ what: String) {
    if ok {
      passed += 1
      print("  PASS  \(what)")
    } else {
      failures.append("\(current): \(what)")
      print("  FAIL  \(what)")
    }
  }

  func report() -> Int {
    print("== summary: \(passed) passed, \(failures.count) failed")
    for failure in failures { print("  FAIL  \(failure)") }
    return failures.count
  }

  /// The stubs (Stubs.swift) carry the header's values.
  func checkConstants() {
    current = "constants"
    print("== constants")
    guard let text = try? String(contentsOf: header, encoding: .utf8) else {
      check(false, "read \(header.path)")
      return
    }
    let pattern = try! NSRegularExpression(pattern: #"static const (?:NSInteger|int) (\w+) = (-?\d+);"#)
    var declared: [String: Int] = [:]
    for match in pattern.matches(in: text, range: NSRange(text.startIndex..., in: text)) {
      declared[String(text[Range(match.range(at: 1), in: text)!])] = Int(text[Range(match.range(at: 2), in: text)!])
    }
    for (name, value) in stubbedConstants.sorted(by: { $0.key < $1.key }) {
      check(declared[name] == value, "\(name) = \(value) as in ChordLab-Shared.h")
    }
  }

  private func inside(_ inner: CGRect, _ outer: CGRect, tolerance: CGFloat = 0.5) -> Bool {
    inner.minX >= outer.minX - tolerance && inner.minY >= outer.minY - tolerance
      && inner.maxX <= outer.maxX + tolerance && inner.maxY <= outer.maxY + tolerance
  }

  private func overlap(_ a: CGRect, _ b: CGRect) -> Bool {
    let i = a.intersection(b)
    return !i.isNull && i.width > 0.5 && i.height > 0.5
  }

  private func describe(_ r: CGRect?) -> String {
    guard let r else { return "absent" }
    return String(format: "(%.1f, %.1f, %.1f x %.1f)", r.minX, r.minY, r.width, r.height)
  }

  /// The editor (no panel): the transport bar inside the window, the banner and the Follow menu as expected, and
  /// the palette viewport printed.
  func checkEditor(_ r: Render, banner: Bool, followMenu: Bool) {
    check(r.frames["panel.content"] == nil, "the editor shows, not the panel")
    let content = r.bounds.insetBy(dx: ViewHarness.padding, dy: ViewHarness.padding)
    if let bar = r.frames["transportBar"] {
      check(inside(bar, content), "the transport bar \(describe(bar)) lies inside the window's padding")
    } else {
      check(false, "the transport bar was laid out")
    }
    for group in ["transport.playback", "transport.voicing", "transport.strip"] {
      guard let frame = r.frames[group] else { continue }
      check(frame.height <= 26, String(format: "%@ is one line (%.1f pt high)", group, frame.height))
    }
    check((r.frames["banner"] != nil) == banner, banner ? "the banner shows" : "no banner")
    check((r.frames["transport.follow"] != nil) == followMenu, followMenu ? "the Follow menu shows" : "no Follow menu")
    if let follow = r.frames["transport.follow"] {
      check(inside(follow, content), "the Follow menu \(describe(follow)) lies inside the window's padding")
    }
    if let viewport = r.frames["palette.viewport"] {
      print(String(format: "  palette viewport height %.1f", viewport.height))
      if r.size.height < FollowOffer.compactHeight {
        check(viewport.height >= ViewHarness.minPaletteViewport,
              String(format: "the palette viewport (%.1f pt) keeps one full row (%.0f pt)", viewport.height,
                     ViewHarness.minPaletteViewport))
      }
    } else {
      check(false, "the palette viewport was laid out")
    }
  }

  /// The banner: under the key bar, above the transport bar, inside the window, one line in its compact form.
  func checkBanner(_ r: Render) {
    guard let banner = r.frames["banner"], let keyBar = r.frames["keyBar"], let bar = r.frames["transportBar"] else {
      check(false, "the banner, the key bar and the transport bar were laid out")
      return
    }
    let content = r.bounds.insetBy(dx: ViewHarness.padding, dy: ViewHarness.padding)
    check(inside(banner, content), "the banner \(describe(banner)) lies inside the window's padding")
    check(banner.minY >= keyBar.maxY + 4, String(format: "the banner keeps clear of the key bar (gap %.1f pt)",
                                                 banner.minY - keyBar.maxY))
    check(bar.minY >= banner.maxY + 4, String(format: "the transport bar keeps clear of the banner (gap %.1f pt)",
                                              bar.minY - banner.maxY))
    let compact = FollowOffer.isCompact(height: r.size.height)
    print(String(format: "  banner height %.1f (%@)", banner.height, compact ? "compact" : "full"))
    if let sentence = r.frames["banner.sentence"], let buttons = r.frames["banner.buttons"] {
      check(abs(sentence.midY - buttons.midY) < 2, "the sentence and the buttons share one line")
      check(buttons.height <= (compact ? 24 : 28), String(format: "the buttons are one line (%.1f pt)", buttons.height))
    } else {
      check(false, "the banner's sentence and buttons were laid out")
    }
  }

  /// Clicks the banner's first lane as the user would, through the state.
  func clickFirstBannerButton(_ state: ChordLabState) {
    guard let option = state.followOffer?.buttons.first else { return }
    state.follow(option)
  }

  /// The follow request the click sent: tag 18, schema 1, the option's identity and lineage, the own lineage.
  func checkFollowRequest(_ r: Render) {
    let links = r.shell.sent.filter { $0.tag == kMsgTagLink }
    check(links.count == 1, "one link message was sent (\(links.count))")
    guard let message = links.first?.json else { return }
    check(message["schema"] as? Int == 1 && message["op"] as? String == "follow",
          "it is a follow with schema 1")
    check(message["leader"] as? String == "id-original" && message["lane"] as? String == "chords"
            && message["leaderLineage"] as? String == "lineage-id-original",
          "it names ChordLab A's chords lane by id and key, with A's lineage")
    check(message["lineage"] as? String == "doc-1", "it carries this ChordLab's own lineage")
  }

  /// The transport bar's three layouts at this width with the Follow menu present: each one's width from the rows'
  /// measured widths, which one `ViewThatFits` chose, and that it fits.
  func checkTransportLayouts(_ r: Render) {
    let switchHere = r.size.width < ContentView.keyBarSwitchMinWidth
    let probe = FrameProbe()
    let alone = TransportBarView(showsTransposeSwitch: switchHere)
      .environmentObject(r.state)
      .environment(\.frameProbe, probe)
      .frame(width: 4000, height: 120, alignment: .topLeading)
    let (_, window) = render(alone, size: CGSize(width: 4000, height: 120), probe: probe)
    defer { window.close() }
    guard let playback = probe.frames["transport.playback"]?.width, let voicing = probe.frames["transport.voicing"]?.width,
          let strip = probe.frames["transport.strip"]?.width else {
      check(false, "the transport rows were measured alone")
      return
    }
    let spacing: CGFloat = 16
    let widths = [playback + spacing + voicing + spacing + strip, max(playback, voicing + spacing + strip),
                  max(playback, max(voicing, strip))]
    let available = r.size.width - 2 * ViewHarness.padding
    print(String(format: "  transport rows: playback %.1f, voicing %.1f, strip (with Follow) %.1f", playback, voicing,
                 strip))
    for (index, width) in widths.enumerated() {
      print(String(format: "  transport layout %d: %.1f pt wide (%.1f available)", index + 1, width, available))
    }
    guard let p = r.frames["transport.playback"], let v = r.frames["transport.voicing"],
          let s = r.frames["transport.strip"] else {
      check(false, "the transport rows were laid out in the window")
      return
    }
    let chosen = abs(v.midY - p.midY) < 1 ? 1 : (abs(s.midY - v.midY) < 1 ? 2 : 3)
    let expected = (widths.firstIndex { $0 <= available } ?? 2) + 1
    print("  transport layout chosen: \(chosen)")
    check(chosen == expected, "ViewThatFits chose layout \(chosen), the first that fits (\(expected))")
    check(widths[chosen - 1] <= available, "the chosen layout fits the window")
  }

  /// The follower's panel: every reported part inside the window and the content, the rows apart, the content
  /// centred and capped, the buttons on one line, and the rows expected for the status present.
  func checkPanel(_ r: Render, rows: [String], absent: [String]) {
    check(r.frames["keyBar"] == nil && r.frames["transportBar"] == nil, "the panel replaces the editor")
    guard let content = r.frames["panel.content"] else {
      check(false, "the panel was laid out")
      return
    }
    check(inside(content, r.bounds.insetBy(dx: ViewHarness.padding, dy: ViewHarness.padding)),
          "the panel \(describe(content)) lies inside the window's padding")
    check(abs(content.midX - r.size.width / 2) <= 1 && abs(content.midY - r.size.height / 2) <= 1,
          String(format: "the panel is centred (centre %.1f, %.1f)", content.midX, content.midY))
    check(content.width <= FollowerPanelView.maxContentWidth + 0.5,
          String(format: "the panel is at most %.0f pt wide (%.1f)", FollowerPanelView.maxContentWidth, content.width))
    let parts = r.frames.filter { $0.key.hasPrefix("panel.") && $0.key != "panel.content" }
    let outside = parts.filter { !inside($0.value, content) }.map(\.key).sorted()
    check(outside.isEmpty, "every part lies inside the panel (outside: \(outside))")
    let rowFrames = parts.filter { $0.key.hasPrefix("panel.row.") }.sorted { $0.value.minY < $1.value.minY }
    var overlapping: [String] = []
    for i in rowFrames.indices {
      for j in rowFrames.indices where j > i && overlap(rowFrames[i].value, rowFrames[j].value) {
        overlapping.append("\(rowFrames[i].key)/\(rowFrames[j].key)")
      }
    }
    check(overlapping.isEmpty, "no two rows overlap (\(overlapping))")
    for row in rows { check(r.frames["panel.row.\(row)"] != nil, "the \(row) row shows") }
    for row in absent { check(r.frames["panel.row.\(row)"] == nil, "no \(row) row") }
    for button in ["panel.action.change", "panel.action.stop", "panel.switch.button"] {
      guard let frame = r.frames[button] else { continue }
      check(frame.height <= 30, String(format: "%@ is one line (%.1f pt high)", button, frame.height))
    }
    check(r.frames["panel.action.change"] != nil && r.frames["panel.action.stop"] != nil,
          "Change… and Stop following show")
    if let change = r.frames["panel.action.change"], let stop = r.frames["panel.action.stop"] {
      check(change.maxX <= stop.minX, "Change… comes before Stop following")
    }
  }

  /// While following, no key command reaches the hidden editor on route B (key-down and its key-up) or through
  /// `perform`; in the editor ⌘Z goes through.
  func checkKeyGate(_ r: Render, following: Bool) {
    let router = EditorKeyRouter(view: r.host, state: r.state)
    r.shell.clear()
    let down = router.handle(characters: "z", virtualKey: 0, shift: false, command: true, option: false)
    let up = router.keyUp(characters: "z", virtualKey: 0, shift: false, command: true, option: false)
    r.state.perform(.deleteSelection)
    r.state.perform(.undo)
    let commands = r.shell.sent.filter { $0.tag == kMsgTagCommand }.count
    if following {
      check(!down && !up, "route B leaves ⌘Z and its release to the host while following")
      check(commands == 0, "no command reaches the hidden document while following (\(commands) sent)")
    } else {
      check(down && up, "route B takes ⌘Z and its release in the editor")
      check(commands > 0, "⌘Z sends undo in the editor")
    }
  }

  /// The arrow after the name on the lanes others play, apart from the name and inside the tab; none elsewhere.
  func checkTabMarks(_ r: Render, marked: [Lane], unmarked: [Lane]) {
    if let row = r.frames["laneTabs"] {
      check(inside(row, r.bounds.insetBy(dx: ViewHarness.padding, dy: 0)), "the lane tab row lies inside the window")
    }
    for lane in marked {
      let name = LaneTabsView.probeName(lane)
      guard let mark = r.frames[name + ".mark"], let label = r.frames[name + ".name"], let tab = r.frames[name] else {
        check(false, "\(name) carries the mark")
        continue
      }
      check(mark.minX - label.maxX >= 2, String(format: "%@'s mark keeps %.1f pt from its name", name,
                                                 mark.minX - label.maxX))
      check(inside(mark, tab), "\(name)'s mark lies inside its tab")
    }
    for lane in unmarked {
      check(r.frames[LaneTabsView.probeName(lane) + ".mark"] == nil, "\(LaneTabsView.probeName(lane)) has no mark")
    }
  }
}
