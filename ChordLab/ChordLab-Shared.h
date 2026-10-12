#ifndef ChordLab_Shared_h
#define ChordLab_Shared_h

// Shared between C++ (ObjC++) and Swift via the bridging header.

const int kNumPresets = 1;

const NSInteger kParamGain = 0;   // placeholder parameter from the scaffold; removed in M2
const NSInteger kNumParams = 1;

const NSInteger kNoCtrlTag = -1;

// Message tags of the UI <-> plugin bridge. Every payload is UTF-8 JSON in the engine's wire format
// (engine/include/chordlab/json_api.h): commands and snapshots carry "schema":1.
// UI -> plugin
const NSInteger kMsgTagCommand = 10;          // payload: one command, {"schema":1,"cmd":"...",...}
const NSInteger kMsgTagRequestSnapshot = 11;  // no payload: asks for a fresh snapshot
// plugin -> UI
const NSInteger kMsgTagSnapshot = 20;         // payload: snapshotJson (document + palette)
const NSInteger kMsgTagError = 21;            // payload: {"schema":1,"error":"..."} for a rejected command

#endif /* ChordLab_Shared_h */
