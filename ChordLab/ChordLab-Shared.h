#ifndef ChordLab_Shared_h
#define ChordLab_Shared_h

// Shared between C++ (ObjC++) and Swift via the bridging header.
// `static`: a plain `const` C global has external linkage, and Swift emits a definition of a bridged
// constant in every Swift object file that uses it, so two Swift files referencing the same constant
// fail to link with a duplicate symbol. `static const` gives each translation unit its own copy.

static const int kNumPresets = 1;

static const NSInteger kParamGain = 0;   // placeholder parameter from the scaffold; unused but still declared and saved
static const NSInteger kNumParams = 1;

static const NSInteger kNoCtrlTag = -1;

// Message tags of the UI <-> plugin bridge. Every payload is UTF-8 JSON in the engine's wire format
// (engine/include/chordlab/json_api.h): commands and snapshots carry "schema":1.
// UI -> plugin
static const NSInteger kMsgTagCommand = 10;          // payload: one command, {"schema":1,"cmd":"...",...}
static const NSInteger kMsgTagRequestSnapshot = 11;  // no payload: asks for a fresh snapshot
static const NSInteger kMsgTagPreview = 12;          // payload: {"schema":1,"id":n,"on":true|false,"velocity":100,"pitches":[..]} (<= 8) or {"schema":1,"id":n,"extend":true}
static const NSInteger kMsgTagPanic = 13;            // no payload: silence everything on the next audio block
static const NSInteger kMsgTagExport = 14;           // no payload: write the progression as a .mid file; answered by kMsgTagExportReady or kMsgTagError
static const NSInteger kMsgTagExportSection = 15;    // payload: {"schema":1,"id":<sectionId>}: write that section alone as a .mid file; same answers
// plugin -> UI
static const NSInteger kMsgTagSnapshot = 20;         // payload: snapshotJson (document + palette)
static const NSInteger kMsgTagError = 21;            // payload: {"schema":1,"error":"..."} for a rejected command or preview, or a failed export
static const NSInteger kMsgTagStatus = 22;           // payload: {"schema":1,"incompleteBlocks":n} (scheduler debug aid)
static const NSInteger kMsgTagTransport = 23;        // payload: {"schema":1,"ppq":<beats>,"playing":true|false}, ~15 Hz while it changes
static const NSInteger kMsgTagExportReady = 24;      // payload: {"schema":1,"path":"/.../ChordLab/ChordLab-<label>-<stamp>-<pid>-<counter>-<uuid8>.mid"} (see ChordLab.mm, "MIDI export files")

#endif /* ChordLab_Shared_h */
