import SwiftUI

public typealias MIDINoteNumber = UInt8
public typealias MIDIVelocity = UInt8

@objc class ChordLabViewController: IPlugCocoaViewController {
  /// The clipboard token of the plug-in instance's document lineage (tracks design D21, `ClipboardToken`):
  /// `ChordLab::OpenWindow` sets it right after creating this controller and before touching its view, so the lazy
  /// `state` below is created with it. One per lineage, not per window: an editor closed and reopened pastes its own
  /// chord copies with their track ids. The shell sets it again on the main thread when loaded state replaced the
  /// document (`ChordLab::PushSnapshot`, before the loaded snapshot); a later value goes on to the state once it
  /// exists.
  @objc var clipboardToken: String? {
    didSet { if stateCreated { state.clipboardToken = clipboardToken ?? "" } }
  }

  /// Whether the lazy `state` exists yet: a token set before it does is read by its initializer instead.
  private var stateCreated = false

  // All callbacks capture `self` weakly: the controller owns the state, and the
  // state owns these closures, so strong captures would form a retain cycle.
  lazy var state: ChordLabState = {
    stateCreated = true
    return ChordLabState(
      beginEdit: { [weak self] idx in self?.beginInformHostOfParamChangeFromUI(paramIdx: idx) },
      doEdit: { [weak self] idx, value in self?.sendParameterValueFromUI(paramIdx: idx, normalizedValue: value) },
      endEdit: { [weak self] idx in self?.endInformHostOfParamChangeFromUI(paramIdx: idx) },
      sendCommand: { [weak self] tag, data in
        self?.sendArbitraryMsgFromUI(msgTag: tag, ctrlTag: kNoCtrlTag, msg: data)
      },
      clipboardToken: clipboardToken)
  }()

  /// The keyboard routes (`EditorKeyRouter`): installed while the view is on screen.
  private lazy var keyRouter = EditorKeyRouter(view: view, state: state)

  override init(nibName nibNameOrNil: PlatformNibName?, bundle nibBundleOrNil: Bundle?) {
    super.init(nibName: nibNameOrNil, bundle: nibBundleOrNil)
  }

  required init?(coder: NSCoder) {
    super.init(coder: coder)
  }

  override init!(editorDelegateAndBundleID editorDelegate: UnsafeMutableRawPointer!, _ bundleID: UnsafePointer<CChar>!) {
    super.init(editorDelegateAndBundleID: editorDelegate, bundleID)
  }

  #if os(macOS)
  override func loadView() {
    self.view = NSView()
  }
  #endif

  override func viewDidLoad() {
    super.viewDidLoad()

    for idx in 0..<parameterCount() {
      state.params.append(Param(id: idx,
                                name: getParameterName(idx),
                                defaultValue: getParameterDefault(idx),
                                minValue: getParameterMin(idx),
                                maxValue: getParameterMax(idx),
                                step: getParameterStep(idx),
                                label: getParameterLabel(idx),
                                group: getParameterGroup(idx)))
    }

    let contentView = ContentView().environmentObject(state)
    let hostingController = PlatformHostingController(rootView: contentView)
    state.bundleID = getBundleID()
    BundleLocator.shared.initialize(with: state.bundleID)

    view.addSubview(hostingController.view)
    hostingController.view.pinToSuperviewEdges()
  }

  override func viewDidAppear() {
    super.viewDidAppear()
    keyRouter.install()
  }

  override func viewDidDisappear() {
    super.viewDidDisappear()
    keyRouter.remove()
  }

  /// Route B: the host delivered the key through the plug-in API (VST3 `IPlugView::onKeyDown` →
  /// iPlug2 `IEditorDelegate::OnKeyDown` → `ChordLab::OnKeyDown`). Same router, same map.
  @objc func handleKey(characters: String, virtualKey: Int, shift: Bool, command: Bool, option: Bool) -> Bool {
    keyRouter.handle(characters: characters, virtualKey: virtualKey, shift: shift, command: command, option: option)
  }

  /// Route B's key-up (VST3 `IPlugView::onKeyUp` → `ChordLab::OnKeyUp`): ends the key's hold in the router's
  /// repeat filter; true for a key the editor would consume, so the host sees the release go where the
  /// press went.
  @objc func handleKeyUp(characters: String, virtualKey: Int, shift: Bool, command: Bool, option: Bool) -> Bool {
    keyRouter.keyUp(characters: characters, virtualKey: virtualKey, shift: shift, command: command, option: option)
  }

  /// Messages from C++ (SendArbitraryMsgFromDelegate): snapshots, errors, scheduler status, the playhead,
  /// the export file, what the listener heard.
  override func onMessage(_ msgTag: Int, _ ctrlTag: Int, _ msg: Data!) -> Bool {
    guard let msg = msg else { return false }
    switch msgTag {
    case kMsgTagSnapshot:
      state.applySnapshot(json: msg)
      return true
    case kMsgTagError:
      state.applyError(json: msg)
      return true
    case kMsgTagStatus:
      state.applyStatus(json: msg)
      return true
    case kMsgTagTransport:
      state.applyTransport(json: msg)
      return true
    case kMsgTagExportReady:
      state.applyExportReady(json: msg)
      return true
    case kMsgTagHeard:
      state.applyHeard(json: msg)
      return true
    default:
      return false
    }
  }

  override func sendControlMsgFromDelegate(ctrlTag: Int, msgTag: Int, msg: Data!) {
  }

  override func onMidiMsgUI(_ status: UInt8, _ data1: UInt8, _ data2: UInt8, _ offset: Int) {
  }

  override func onSysexMsgUI(_ msg: Data!, _ offset: Int) {
  }

  override func onParamChangeUI(_ paramIdx: Int, _ value: Double) {
    if paramIdx < state.params.count { state.params[Int(paramIdx)].value = value }
  }
}
