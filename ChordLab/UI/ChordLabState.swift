import SwiftUI

/// UI-side state. Logic lives in C++; this object only mirrors what the plugin
/// pushes (JSON) and forwards user intent (commands, MIDI) back.
class ChordLabState: NSObject, ObservableObject {
  var params: [Param] = []
  @Published var bundleID = String("")
  @Published var testValue: Int = 0        // M0 spike 4: round-trips through the state chunk
  @Published var stateJson: String = ""

  let beginEdit: @MainActor (Int) -> Void
  let doEdit: @MainActor (Int, Double) -> Void
  let endEdit: @MainActor (Int) -> Void
  let sendMidi: @MainActor (UInt8, UInt8, UInt8) -> Void
  let sendCommand: @MainActor (Int, Data?) -> Void

  public override init() {
    self.beginEdit = { _ in }
    self.doEdit = { _, _ in }
    self.endEdit = { _ in }
    self.sendMidi = { _, _, _ in }
    self.sendCommand = { _, _ in }
    super.init()
  }

  public init(beginEdit: @MainActor @escaping (Int) -> Void = { _ in },
              doEdit: @MainActor @escaping (Int, Double) -> Void = { _, _ in },
              endEdit: @MainActor @escaping (Int) -> Void = { _ in },
              sendMidi: @MainActor @escaping (UInt8, UInt8, UInt8) -> Void = { _, _, _ in },
              sendCommand: @MainActor @escaping (Int, Data?) -> Void = { _, _ in }) {
    self.beginEdit = beginEdit
    self.doEdit = doEdit
    self.endEdit = endEdit
    self.sendMidi = sendMidi
    self.sendCommand = sendCommand
    super.init()
  }

  convenience init(numParams: Int) {
    self.init()
    for paramIdx in 0..<numParams {
      params.append(Param(id: paramIdx))
    }
  }

  @MainActor func sendChord(_ pitches: [UInt8], on: Bool, velocity: UInt8 = 100) {
    for p in pitches {
      sendMidi(on ? 0x90 : 0x80, p, on ? velocity : 0)
    }
  }

  @MainActor func setTestValue(_ v: Int) {
    testValue = v
    var value = Int32(v)
    sendCommand(kMsgTagSetTestValue, Data(bytes: &value, count: MemoryLayout<Int32>.size))
  }

  /// Called with the JSON the plugin pushes on open and after every change.
  func applyState(json: Data) {
    stateJson = String(decoding: json, as: UTF8.self)
    if let obj = try? JSONSerialization.jsonObject(with: json) as? [String: Any],
       let v = obj["test"] as? Int {
      testValue = v
    }
  }
}
