#pragma once

#include "IPlug_include_in_plug_hdr.h"

#include <atomic>
#include <climits>
#include <string>

using namespace iplug;

class ChordLab final : public Plugin
{
public:
  ChordLab(const InstanceInfo& info);

  void* OpenWindow(void* pParent) override;
  
  void OnParentWindowResize(int width, int height) override;
  bool OnHostRequestingSupportedViewConfiguration(int width, int height) override;

  void OnIdle() override;
  void OnUIOpen() override;
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;

  // M0 spike 4: versioned state chunk (version byte + UTF-8 JSON)
  bool SerializeState(IByteChunk& chunk) const override;
  int UnserializeState(const IByteChunk& chunk, int startPos) override;

private:
  void PushStateToUI();
  std::string StateJson() const;

  // M0 spike 2: a one-note arpeggio driven purely by host ppq (temporary; removed after M0).
  void RunSpikeArp(int nFrames);

  static constexpr uint8_t kStateVersion = 1;
  int mTestValue = 42;

  std::atomic<bool> mSpikeArp{false};
  int mArpSounding = -1;          // audio thread only
  double mArpOffPpq = 0.0;        // audio thread only
  double mArpNextBeat = 0.0;      // audio thread only: next un-emitted integer beat
  double mArpLastPpqEnd = -1.0;   // audio thread only
  int mIdleCounter = 0;

  // host time info mirrored for the UI (written on the audio thread, read in OnIdle)
  std::atomic<double> mDbgPpq{0.0}, mDbgTempo{0.0}, mDbgCycleStart{0.0}, mDbgCycleEnd{0.0};
  std::atomic<int> mDbgPlaying{0}, mDbgLoop{0};
};
