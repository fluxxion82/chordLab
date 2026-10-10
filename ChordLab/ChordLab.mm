#include "ChordLab.h"
#include "IPlug_include_in_plug_src.h"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cstdio>

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
}

void ChordLab::OnIdle()
{
}

void* ChordLab::OpenWindow(void* pParent)
{
  PLATFORM_VIEW* platformParent = (PLATFORM_VIEW*) pParent;
  auto* vc = [[ChordLabViewController alloc] initWithEditorDelegateAndBundleID: this : GetBundleID()];
  mViewController = vc;
  vc.view.frame = MAKERECT(0.f, 0.f, (float) PLUG_WIDTH, (float) PLUG_HEIGHT);
  [platformParent addSubview:vc.view];
  
  OnUIOpen();
  
  return vc.view;
}

void ChordLab::OnParentWindowResize(int width, int height)
{
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
  // MIDI-only plugin: Live requires an audio bus, so keep the outputs silent.
  // Not zeroing them leaves stale host buffers playing (iPlug2 Discourse, 2024).
  const int nOut = NOutChansConnected();
  for (int c = 0; c < nOut; ++c)
    std::fill(outputs[c], outputs[c] + nFrames, 0.0);
}

void ChordLab::OnUIOpen()
{
  Plugin::OnUIOpen();
  PushStateToUI();
}

bool ChordLab::OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData)
{
  if (msgTag == kMsgTagSetTestValue && dataSize == (int) sizeof(int32_t) && pData)
  {
    int32_t v = 0;
    memcpy(&v, pData, sizeof(v));
    mTestValue = v;
    PushStateToUI();
    return true;
  }

  return CocoaEditorDelegate::OnMessage(msgTag, ctrlTag, dataSize, pData);
}

std::string ChordLab::StateJson() const
{
  // Built by hand for M0; the engine's json_api replaces this in M2.
  char buf[128];
  snprintf(buf, sizeof(buf), "{\"v\":%d,\"key\":\"C major\",\"test\":%d}", (int) kStateVersion, mTestValue);
  return std::string(buf);
}

void ChordLab::PushStateToUI()
{
  const std::string json = StateJson();
  SendArbitraryMsgFromDelegate((int) kMsgTagStateToUI, (int) json.size(), json.data());
}

bool ChordLab::SerializeState(IByteChunk& chunk) const
{
  chunk.Put(&kStateVersion);
  const std::string json = StateJson();
  const int32_t len = (int32_t) json.size();
  chunk.Put(&len);
  chunk.PutBytes(json.data(), len);
  return true;
}

int ChordLab::UnserializeState(const IByteChunk& chunk, int startPos)
{
  uint8_t version = 0;
  startPos = chunk.Get(&version, startPos);
  if (version != kStateVersion)
    return startPos;   // unknown version: keep defaults

  int32_t len = 0;
  startPos = chunk.Get(&len, startPos);
  if (len < 0 || len > 1 << 20 || startPos + len > chunk.Size())
    return startPos;

  std::string json((const char*) chunk.GetData() + startPos, (size_t) len);
  startPos += len;

  // Minimal parse for the spike: find "test":<int>.
  const auto pos = json.find("\"test\":");
  if (pos != std::string::npos)
    mTestValue = atoi(json.c_str() + pos + 7);

  PushStateToUI();   // no-op if the editor is closed (nothing is listening)
  return startPos;
}
