import SwiftUI

public typealias MIDINoteNumber = UInt8
public typealias MIDIVelocity = UInt8

@objc class ChordLabViewController: IPlugCocoaViewController {
  // All callbacks capture `self` weakly: the controller owns the state, and the
  // state owns these closures, so strong captures would form a retain cycle.
  lazy var state = ChordLabState(
    beginEdit: { [weak self] idx in self?.beginInformHostOfParamChangeFromUI(paramIdx: idx) },
    doEdit: { [weak self] idx, value in self?.sendParameterValueFromUI(paramIdx: idx, normalizedValue: value) },
    endEdit: { [weak self] idx in self?.endInformHostOfParamChangeFromUI(paramIdx: idx) },
    sendCommand: { [weak self] tag, data in
      self?.sendArbitraryMsgFromUI(msgTag: tag, ctrlTag: kNoCtrlTag, msg: data)
    })

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

  /// Messages from C++ (SendArbitraryMsgFromDelegate): snapshots, errors, scheduler status, the playhead,
  /// the export file.
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
