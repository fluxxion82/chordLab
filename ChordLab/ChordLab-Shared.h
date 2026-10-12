#ifndef ChordLab_Shared_h
#define ChordLab_Shared_h

// Shared between C++ (ObjC++) and Swift via the bridging header.
// `static`: a plain `const` C global has external linkage, and Swift emits a definition of a bridged
// constant in every Swift object file that uses it, so two Swift files referencing the same constant
// fail to link with a duplicate symbol. `static const` gives each translation unit its own copy.

static const int kNumPresets = 1;

static const NSInteger kParamGain = 0;   // placeholder parameter from the scaffold; removed in M2
static const NSInteger kNumParams = 1;

static const NSInteger kNoCtrlTag = -1;

// Message tags of the UI <-> plugin bridge. Every payload is UTF-8 JSON in the engine's wire format
// (engine/include/chordlab/json_api.h): commands and snapshots carry "schema":1.
// UI -> plugin
static const NSInteger kMsgTagCommand = 10;          // payload: one command, {"schema":1,"cmd":"...",...}
static const NSInteger kMsgTagRequestSnapshot = 11;  // no payload: asks for a fresh snapshot
static const NSInteger kMsgTagPreview = 12;          // payload: {"schema":1,"id":n,"on":true|false,"velocity":100,"pitches":[..]} (<= 8)
static const NSInteger kMsgTagPanic = 13;            // no payload: silence everything on the next audio block
// plugin -> UI
static const NSInteger kMsgTagSnapshot = 20;         // payload: snapshotJson (document + palette)
static const NSInteger kMsgTagError = 21;            // payload: {"schema":1,"error":"..."} for a rejected command or preview
static const NSInteger kMsgTagStatus = 22;           // payload: {"schema":1,"incompleteBlocks":n} (scheduler debug aid)

#endif /* ChordLab_Shared_h */
