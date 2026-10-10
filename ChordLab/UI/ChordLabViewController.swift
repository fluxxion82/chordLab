import SwiftUI

public typealias MIDINoteNumber = UInt8
public typealias MIDIVelocity = UInt8

@objc class ChordLabViewController: IPlugCocoaViewController {
  lazy var state = ChordLabState(
    beginEdit: self.beginInformHostOfParamChangeFromUI,
    doEdit: self.sendParameterValueFromUI,
    endEdit: self.endInformHostOfParamChangeFromUI,
    sendMidi: { status, data1, data2 in
      self.sendMidiMsgFromUI(status: status, data1: data1, data2: data2, offset: 0)
    },
    sendCommand: { tag, data in
      self.sendArbitraryMsgFromUI(msgTag: tag, ctrlTag: kNoCtrlTag, msg: data)
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

  /// Messages from C++ (sendArbitraryMsgFromDelegate).
  override func onMessage(_ msgTag: Int, _ ctrlTag: Int, _ msg: Data!) -> Bool {
    if msgTag == kMsgTagStateToUI, let msg = msg {
      state.applyState(json: msg)
      return true
    }
    return false
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
