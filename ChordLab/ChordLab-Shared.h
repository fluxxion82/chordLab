#ifndef ChordLab_Shared_h
#define ChordLab_Shared_h

// Shared between C++ (ObjC++) and Swift via the bridging header.

const int kNumPresets = 1;

const NSInteger kParamGain = 0;   // placeholder parameter from the scaffold; removed in M2
const NSInteger kNumParams = 1;

const NSInteger kNoCtrlTag = -1;

// UI -> plugin
const NSInteger kMsgTagSetTestValue = 3;   // payload: int32 (M0 spike 4)
// plugin -> UI
const NSInteger kMsgTagStateToUI = 4;      // payload: UTF-8 JSON of the plugin state

#endif /* ChordLab_Shared_h */
