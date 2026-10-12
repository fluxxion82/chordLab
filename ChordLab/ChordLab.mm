// Ownership and threads
// ---------------------
// The model (mStore, mPalette) and the editor size (IEditorDelegate::mEditorWidth/Height) are reached from
// two kinds of non-audio thread: the main thread (OnMessage from the Swift UI, OnUIOpen, OnParentWindowResize)
// and the host's state thread (SerializeState / UnserializeState: VST3 calls them on the UI thread, AUv2
// SetState and AUv3 fullState may call them from a background thread). Every access takes mModelMutex, and
// no lock is held while calling into iPlug2 or the UI, so PushSnapshot can run inline on the main thread
// without re-entering the lock. The audio thread (ProcessBlock) never touches the model or the mutex.
// Delivery to the Swift view controller (PushSnapshot, PushError, RestoreEditorSize) always happens on the
// main thread: OnMain runs the block inline when already there and dispatches it otherwise. A dispatched
// block holds the Lifeline mutex while it runs and does nothing once `alive` is false; the destructor
// flips `alive` under that mutex, so it blocks until a hop in flight has finished and every later hop is
// a no-op. That matters because destruction is not always on the main thread (AUv3's dealloc deletes the
// plug-in on whatever thread releases the audio unit, IPlugAUAudioUnit.mm). No deadlock: the destructor
// never runs inside a queued hop, the inline main-thread path takes no Lifeline lock (so a hop calling
// PushSnapshot does not re-enter it), and the lock order is always Lifeline::mutex then mModelMutex.
//
// Chord preview (M2, Task 2.4) does not go through the model: the Swift palette sends note-ons/offs with
// IPlugCocoaViewController::sendMidiMsgFromUI -> SendMidiMsgFromUI, which iPlug2 queues for the next
// ProcessBlock's MIDI output; the UI guarantees one note-off per note-on. M3 replaces this with
// Scheduler::pushPreview, which owns the sounding notes and adds a watchdog.

#include "ChordLab.h"
#include "IPlug_include_in_plug_src.h"

#include "chordlab/json_api.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#ifdef FRAMEWORK_BUILD
#import <AUv3Framework/ChordLab-Swift.h>
#import <AUv3Framework/ChordLab-Shared.h>
#else
#import <ChordLab-Swift.h>
#endif

ChordLab::ChordLab(const InstanceInfo& info)
: Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  GetParam(kParamGain)->InitDouble("Gain", 100., 0., 100.0, 0.01, "%");
  
  MakeDefaultPreset();

  std::lock_guard<std::mutex> lock(mModelMutex);
  RebuildPaletteLocked();
}

ChordLab::~ChordLab()
{
  std::lock_guard<std::mutex> guard(mLifeline->mutex);         // waits for a hop in flight; later hops do nothing
  mLifeline->alive = false;
}

void* ChordLab::OpenWindow(void* pParent)
{
  PLATFORM_VIEW* platformParent = (PLATFORM_VIEW*) pParent;
  auto* vc = [[ChordLabViewController alloc] initWithEditorDelegateAndBundleID: this : GetBundleID()];
  mViewController = vc;
  int width = PLUG_WIDTH;
  int height = PLUG_HEIGHT;
  {
    std::lock_guard<std::mutex> lock(mModelMutex);
    width = GetEditorWidth();      // PLUG_WIDTH x PLUG_HEIGHT until a host resize or a state load changes it
    height = GetEditorHeight();
  }
  vc.view.frame = MAKERECT(0.f, 0.f, (float) width, (float) height);
  [platformParent addSubview:vc.view];
  
  OnUIOpen();
  
  return vc.view;
}

void ChordLab::OnParentWindowResize(int width, int height)
{
  // Logic/GB probe with 0x0 (see OnHostRequestingSupportedViewConfiguration); loadStateJson rejects a
  // non-positive editor size, so only a real size is remembered.
  if (width > 0 && height > 0)
  {
    std::lock_guard<std::mutex> lock(mModelMutex);
    SetEditorSize(width, height);
  }
  auto* vc = (ChordLabViewController*) mViewController;
  vc.view.frame = MAKERECT(0.f, 0.f, (float) width, (float) height);
}

// AUv3 only
bool ChordLab::OnHostRequestingSupportedViewConfiguration(int width, int height)
{
#ifdef OS_MAC
  // Logic/GB offer one option with 0w, 0h, and if we allow that, our AUv3 has "our" size as its 100% setting
  return ((width + height) == 0);
#else
  return true;
#endif
}

void ChordLab::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  // Audio thread: nothing here may touch mStore, mPalette or mModelMutex (see the note at the top).
  // MIDI-only plugin: Live requires an audio bus, so keep the outputs silent.
  // Not zeroing them leaves stale host buffers playing (iPlug2 Discourse, 2024).
  const int nOut = NOutChansConnected();
  for (int c = 0; c < nOut; ++c)
    std::fill(outputs[c], outputs[c] + nFrames, 0.0);
}

void ChordLab::OnUIOpen()
{
  Plugin::OnUIOpen();
  PushSnapshot();
}

bool ChordLab::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
  if (msgTag == kMsgTagCommand)
  {
    const std::string_view json = (pData && dataSize > 0) ? std::string_view((const char*) pData, (size_t) dataSize)
                                                          : std::string_view();
    std::string error;
    bool accepted = false;
    {
      std::lock_guard<std::mutex> lock(mModelMutex);
      accepted = chordlab::applyCommandJson(mStore, json, error);
      // Always rebuild: besides setKey/browseKey, undo/redo can change the project key, and the
      // palette is cheap (35 cells).
      if (accepted)
        RebuildPaletteLocked();
    }
    if (accepted)
      PushSnapshot();
    else
    {
      DBGMSG("ChordLab: command rejected: %s\n", error.c_str());
      PushError(error);
    }
    return true;
  }

  if (msgTag == kMsgTagRequestSnapshot)
  {
    PushSnapshot();
    return true;
  }

  return CocoaEditorDelegate::OnMessage(msgTag, ctrlTag, dataSize, pData);
}

void ChordLab::RebuildPaletteLocked()
{
  const chordlab::Document& doc = mStore.doc();
  mPalette = chordlab::buildPalette(doc.browsedKey, doc.projectKey);
}

void ChordLab::OnMain(std::function<void()> fn)
{
  if ([NSThread isMainThread])
  {
    fn();
    return;
  }
  const std::shared_ptr<Lifeline> lifeline = mLifeline;
  dispatch_async(dispatch_get_main_queue(), ^{
    std::lock_guard<std::mutex> guard(lifeline->mutex);
    if (lifeline->alive)
      fn();
  });
}

void ChordLab::PushSnapshot()
{
  OnMain([this] {
    if (!mViewController)                                        // no listener: nothing to serialize
      return;
    std::string json;
    {
      std::lock_guard<std::mutex> lock(mModelMutex);
      json = chordlab::snapshotJson(mStore.doc(), mPalette);
    }
    SendArbitraryMsgFromDelegate((int) kMsgTagSnapshot, (int) json.size(), json.data());
  });
}

void ChordLab::PushError(const std::string& message)
{
  OnMain([this, message] {
    if (!mViewController)
      return;
    // The only JSON the plugin writes itself; nlohmann escapes the message (engine error strings quote
    // user input, e.g. unknown cmd "x"), replacing any invalid UTF-8 rather than throwing.
    const std::string json = nlohmann::json{{"schema", chordlab::kJsonSchema}, {"error", message}}
                               .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    SendArbitraryMsgFromDelegate((int) kMsgTagError, (int) json.size(), json.data());
  });
}

void ChordLab::RestoreEditorSize(int width, int height)
{
  // Main thread, for a size restored while an editor is open (UnserializeState stores it directly when
  // none is). The stored size must stay the actual size of the open editor: it changes only if the host
  // resizes. VST3 and APP implement EditorResize (host window resize, true, and they record the size
  // themselves after comparing with GetEditorWidth/Height, so it must run before any SetEditorSize);
  // AUv2 and AUv3 inherit the default, which resizes nothing and returns false.
  if (!mViewController)
  {
    std::lock_guard<std::mutex> lock(mModelMutex);
    SetEditorSize(width, height);                                // closed since the load: next open uses it
    return;
  }
  bool differs = false;
  {
    std::lock_guard<std::mutex> lock(mModelMutex);
    differs = width != GetEditorWidth() || height != GetEditorHeight();
  }
  if (!differs)
    return;
  if (EditorResizeFromUI(width, height, true))                   // host-driven: ends in OnParentWindowResize
  {
    std::lock_guard<std::mutex> lock(mModelMutex);
    SetEditorSize(width, height);
  }
  else
    DBGMSG("ChordLab: the host cannot resize the open editor; keeping %dx%d\n", GetEditorWidth(), GetEditorHeight());
}

bool ChordLab::SerializeState(IByteChunk& chunk) const
{
  // Envelope, identical for every version: u8 version, i32 payloadLen, payload, then iPlug2 params.
  chunk.Put(&kStateVersion);
  std::string json;
  {
    std::lock_guard<std::mutex> lock(mModelMutex);
    json = chordlab::stateJson(mStore, GetEditorWidth(), GetEditorHeight());
  }
  const int32_t len = (int32_t) json.size();
  chunk.Put(&len);
  chunk.PutBytes(json.data(), len);
  return SerializeParams(chunk);
}

int ChordLab::UnserializeState(const IByteChunk& chunk, int startPos)
{
  // On malformed input return chunk.Size(), never -1: iPlug2's VST3 SetState seeks to the
  // returned position unchecked (IPlugVST3_Common.h) and a negative seek reads out of bounds.
  // Seeking to the end makes its bypass read fail cleanly instead.
  const int fail = chunk.Size();

  uint8_t version = 0;
  int pos = chunk.Get(&version, startPos);
  if (pos < 0)
    return fail;

  int32_t len = 0;
  pos = chunk.Get(&len, pos);
  if (pos < 0 || len < 0 || len > chunk.Size() - pos)
    return fail;

  if (version == kStateVersion)
  {
    const std::string_view json((const char*) chunk.GetData() + pos, (size_t) len);
    std::string error;
    int width = 0;
    int height = 0;
    bool loaded = false;
    bool editorOpen = false;
    {
      std::lock_guard<std::mutex> lock(mModelMutex);
      loaded = chordlab::loadStateJson(mStore, json, error, &width, &height);   // atomic: a failure changes nothing
      if (loaded)
      {
        RebuildPaletteLocked();
        editorOpen = mViewController != nullptr;
        if (!editorOpen)
          SetEditorSize(width, height);                          // synchronous: a save right after this load sees it
      }
    }
    if (loaded && editorOpen)
      OnMain([this, width, height] { RestoreEditorSize(width, height); });
    else if (!loaded)
      DBGMSG("ChordLab: state payload rejected, keeping the current document: %s\n", error.c_str());
  }
  else
    DBGMSG("ChordLab: state version %d is not %d, keeping the current document\n", (int) version, (int) kStateVersion);
  // A rejected or unknown payload is skipped so the parameter trailer still lines up.
  pos += len;

  pos = UnserializeParams(chunk, pos);
  if (pos < 0)
    pos = fail;
  PushSnapshot();                                                // queued after the resize; no-op without an editor
  return pos;
}
