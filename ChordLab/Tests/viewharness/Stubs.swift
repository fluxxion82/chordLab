// The constants the editor's views read from ChordLab/ChordLab-Shared.h through the Xcode project's bridging header,
// which this harness does not use. Same names and values; `ViewHarness.checkConstants` compares them with the header
// at run time, so a renumbered tag fails the harness instead of passing silently.

let kNumParams = 1
let kNoCtrlTag = -1
let kMsgTagCommand = 10
let kMsgTagRequestSnapshot = 11
let kMsgTagPreview = 12
let kMsgTagPanic = 13
let kMsgTagExport = 14
let kMsgTagExportSection = 15
let kMsgTagClearHeard = 16
let kMsgTagExportClip = 17
let kMsgTagLink = 18
let kMsgTagSnapshot = 20
let kMsgTagError = 21
let kMsgTagStatus = 22
let kMsgTagTransport = 23
let kMsgTagExportReady = 24
let kMsgTagHeard = 25
let kMsgTagLinkState = 26

/// Every stub above by name, for the comparison with the header.
let stubbedConstants: [String: Int] = [
  "kNumParams": kNumParams, "kNoCtrlTag": kNoCtrlTag, "kMsgTagCommand": kMsgTagCommand,
  "kMsgTagRequestSnapshot": kMsgTagRequestSnapshot, "kMsgTagPreview": kMsgTagPreview, "kMsgTagPanic": kMsgTagPanic,
  "kMsgTagExport": kMsgTagExport, "kMsgTagExportSection": kMsgTagExportSection, "kMsgTagClearHeard": kMsgTagClearHeard,
  "kMsgTagExportClip": kMsgTagExportClip, "kMsgTagLink": kMsgTagLink, "kMsgTagSnapshot": kMsgTagSnapshot,
  "kMsgTagError": kMsgTagError, "kMsgTagStatus": kMsgTagStatus, "kMsgTagTransport": kMsgTagTransport,
  "kMsgTagExportReady": kMsgTagExportReady, "kMsgTagHeard": kMsgTagHeard, "kMsgTagLinkState": kMsgTagLinkState,
]
