#include "ChordLab.h"
#include "IPlug_include_in_plug_src.h"
#include "chordlab/json_api.h"
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

  // Task 2.0 smoke: the engine links and runs. Replaced by the real model in Task 2.1.
  {
    const chordlab::DocumentStore store;
    const chordlab::Palette palette = chordlab::buildPalette(store.doc().browsedKey, store.doc().projectKey);
    const std::string snapshot = chordlab::snapshotJson(store.doc(), palette);
    DBGMSG("ChordLab: engine linked; default snapshot is %zu bytes, %zu palette cells\n",
           snapshot.size(), palette.cells.size());
  }
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

  // Sample offset of a ppq position relative to this block; may lie outside [0, nFrames).
  auto offsetFor = [&](double ppq) { return (int) std::lround((ppq - ppqStart) / ppqPerSample); };

  // Boundary ownership is decided by sample offset, not by ppq interval membership, so a beat
  // sitting on a block edge is emitted exactly once (float noise in ppqEnd caused duplicates).
  const bool contiguous = mArpLastPpqEnd >= 0.0 && std::fabs(ppqStart - mArpLastPpqEnd) <= 1.0 / 1024.0;
  if (!contiguous)
  {
    if (mArpSounding >= 0) noteOff(0);                           // start / seek / host loop wrap: never hang a note
    mArpNextBeat = std::ceil(ppqStart - 0.5 * ppqPerSample);     // a beat within half a sample before the start is ours
  }

  if (mArpSounding >= 0)                                          // note still sounding from an earlier block
  {
    const int off = offsetFor(mArpOffPpq);
    if (off < nFrames) noteOff(std::max(off, 0));
  }

  for (;;)
  {
    const double b = mArpNextBeat;
    const int onOffset = offsetFor(b);
    if (onOffset >= nFrames) break;                                // belongs to a later block
    if (b >= 0.0)
    {
      const int on = std::max(onOffset, 0);
      if (mArpSounding >= 0) noteOff(on);                          // off before on at the same offset
      const int pitch = 60 + ((int) b % 4);
      IMidiMsg msg; msg.MakeNoteOnMsg(pitch, 100, on); SendMidiMsg(msg);
      mArpSounding = pitch;
      mArpOffPpq = b + 0.125;                                      // 1/8 beat long
      const int off = offsetFor(mArpOffPpq);
      if (off < nFrames) noteOff(std::max(off, on));               // ends inside this block: schedule it now
    }
    mArpNextBeat += 1.0;
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
  // Envelope, identical for every version: u8 version, i32 payloadLen, payload, then iPlug2 params.
  chunk.Put(&kStateVersion);
  const std::string json = StateJson();
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
    const std::string json((const char*) chunk.GetData() + pos, (size_t) len);
    const auto at = json.find("\"test\":");                     // minimal parse for the spike
    if (at != std::string::npos)
      mTestValue = atoi(json.c_str() + at + 7);
  }
  // Unknown payload version: keep defaults but skip the payload so the trailer still lines up.
  pos += len;

  pos = UnserializeParams(chunk, pos);
  if (pos < 0)
    pos = fail;
  PushStateToUI();                                               // no listener when the editor is closed
  return pos;
}
