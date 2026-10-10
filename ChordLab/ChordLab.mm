#include "ChordLab.h"
#include "IPlug_include_in_plug_src.h"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>

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
  // Spike 2 telemetry: refresh the UI's view of host time info a few times a second.
  if (++mIdleCounter % 15 == 0 && mSpikeArp.load(std::memory_order_relaxed))
    PushStateToUI();
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

  mDbgPpq.store(GetPPQPos(), std::memory_order_relaxed);
  mDbgTempo.store(GetTempo(), std::memory_order_relaxed);
  mDbgPlaying.store(GetTransportIsRunning() ? 1 : 0, std::memory_order_relaxed);
  mDbgLoop.store(mTimeInfo.mTransportLoopEnabled ? 1 : 0, std::memory_order_relaxed);
  mDbgCycleStart.store(mTimeInfo.mCycleStart, std::memory_order_relaxed);
  mDbgCycleEnd.store(mTimeInfo.mCycleEnd, std::memory_order_relaxed);

  if (mSpikeArp.load(std::memory_order_relaxed))
    RunSpikeArp(nFrames);
  else if (mArpSounding >= 0)
  {
    IMidiMsg off; off.MakeNoteOffMsg(mArpSounding, 0); SendMidiMsg(off);
    mArpSounding = -1;
  }
}

void ChordLab::RunSpikeArp(int nFrames)
{
  const double sr = GetSampleRate();
  const double bpm = GetTempo();
  const double ppqStart = GetPPQPos();

  auto noteOff = [&](int offset) {
    IMidiMsg off; off.MakeNoteOffMsg(mArpSounding, offset); SendMidiMsg(off);
    mArpSounding = -1;
  };

  if (!GetTransportIsRunning() || bpm <= 0.0 || sr <= 0.0)
  {
    if (mArpSounding >= 0) noteOff(0);
    mArpLastPpqEnd = -1.0;
    return;
  }

  const double ppqPerSample = bpm / (60.0 * sr);
  const double ppqEnd = ppqStart + nFrames * ppqPerSample;

  // Discontinuity (host loop wrap or seek): never leave a note hanging.
  const bool jumped = mArpLastPpqEnd >= 0.0 && std::fabs(ppqStart - mArpLastPpqEnd) > 1.0 / 1024.0;
  if (mArpSounding >= 0 && (jumped || mArpOffPpq < ppqStart))
    noteOff(0);

  auto offsetFor = [&](double ppq) {
    return std::clamp((int) std::lround((ppq - ppqStart) / ppqPerSample), 0, nFrames - 1);
  };

  if (mArpSounding >= 0 && mArpOffPpq < ppqEnd)
    noteOff(offsetFor(mArpOffPpq));

  for (double b = std::ceil(ppqStart - 1e-9); b < ppqEnd; b += 1.0)
  {
    if (b < 0.0) continue;
    const int offset = offsetFor(b);
    if (mArpSounding >= 0) noteOff(offset);                 // off before on at the same offset
    const int pitch = 60 + ((int) b % 4);
    IMidiMsg on; on.MakeNoteOnMsg(pitch, 100, offset); SendMidiMsg(on);
    mArpSounding = pitch;
    mArpOffPpq = b + 0.125;                                  // 1/8 beat long
  }

  mArpLastPpqEnd = ppqEnd;
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

  if (msgTag == kMsgTagSpikeArp && dataSize == (int) sizeof(int32_t) && pData)
  {
    int32_t v = 0;
    memcpy(&v, pData, sizeof(v));
    mSpikeArp.store(v != 0, std::memory_order_relaxed);
    PushStateToUI();
    return true;
  }

  return CocoaEditorDelegate::OnMessage(msgTag, ctrlTag, dataSize, pData);
}

std::string ChordLab::StateJson() const
{
  // Built by hand for M0; the engine's json_api replaces this in M2.
  char buf[256];
  snprintf(buf, sizeof(buf),
           "{\"v\":%d,\"key\":\"C major\",\"test\":%d,\"arp\":%d,"
           "\"ppq\":%.3f,\"tempo\":%.2f,\"playing\":%d,\"loop\":%d,\"cycle\":[%.2f,%.2f]}",
           (int) kStateVersion, mTestValue, mSpikeArp.load(std::memory_order_relaxed) ? 1 : 0,
           mDbgPpq.load(std::memory_order_relaxed), mDbgTempo.load(std::memory_order_relaxed),
           mDbgPlaying.load(std::memory_order_relaxed), mDbgLoop.load(std::memory_order_relaxed),
           mDbgCycleStart.load(std::memory_order_relaxed), mDbgCycleEnd.load(std::memory_order_relaxed));
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
