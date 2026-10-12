#pragma once

#include "IPlug_include_in_plug_hdr.h"

#include "chordlab/document.h"
#include "chordlab/palette.h"
#include "chordlab/publication.h"
#include "chordlab/scheduler.h"
#include "chordlab/triple_buffer.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

using namespace iplug;

class ChordLab final : public Plugin
{
public:
  ChordLab(const InstanceInfo& info);
  ~ChordLab() override;

  void* OpenWindow(void* pParent) override;
  void CloseWindow() override;
  
  void OnParentWindowResize(int width, int height) override;
  bool OnHostRequestingSupportedViewConfiguration(int width, int height) override;

  void OnUIOpen() override;
  void OnIdle() override;
  void OnReset() override;
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;

  // Versioned state chunk: u8 version, i32 payload length, stateJson payload, then iPlug2's params.
  bool SerializeState(IByteChunk& chunk) const override;
  int UnserializeState(const IByteChunk& chunk, int startPos) override;

private:
  // Palette of the browsed key annotated against the project key; call after anything that may have
  // changed either key (commands, undo/redo, a state load). The caller holds mModelMutex.
  void RebuildPaletteLocked();
  // Compiles the document and, when anything a slot carries changed (content or followHost), publishes
  // it to the audio thread (see "Playback snapshots" in ChordLab.mm). Call after every accepted command
  // and state load. The caller holds mModelMutex, which is what makes it a single producer.
  void PublishSnapshotLocked();
  // Pushes snapshotJson(doc, palette) to the editor, on the main thread; a no-op when no editor is open.
  void PushSnapshot();
  // Pushes {"schema":1,"error":message} to the editor, on the main thread.
  void PushError(const std::string& message);
  // Runs `fn` on the main thread: inline when already there, else queued; dropped once the plugin is gone.
  void OnMain(std::function<void()> fn);
  // Main thread: adopts a restored editor size, asking the host to resize an open editor.
  void RestoreEditorSize(int width, int height);
  // Main thread: parses a kMsgTagPreview payload and queues it for the scheduler.
  void HandlePreview(std::string_view json);
  // Main thread: pushes {"schema":1,"incompleteBlocks":n} to the editor.
  void PushStatus(uint32_t incompleteBlocks);
  // Main thread: pushes {"schema":1,"ppq":ppq,"playing":playing} (the playhead) to the editor.
  void PushTransport(double ppq, bool playing);
  // Main thread: compiles the document, writes it as a format 0 MIDI file in the temporary directory at
  // the host's tempo and pushes kMsgTagExportReady with the path, or kMsgTagError. The previous export is
  // deleted then, never the one just written (the host reads a dropped file after the drop returns).
  void HandleExport();
  // Main thread: pushes {"schema":1,"path":path} to the editor.
  void PushExportReady(const std::string& path);

  static constexpr uint8_t kStateVersion = 2;   // 1 was the M0 spike payload; it is not migrated

  // The model and the editor size (iPlug2's IEditorDelegate fields) are read and written under mModelMutex
  // from the main thread and from the hosts' state threads; see the ownership note in ChordLab.mm.
  // ProcessBlock never touches any of them.
  mutable std::mutex mModelMutex;
  chordlab::DocumentStore mStore;
  chordlab::Palette mPalette;

  // Playback snapshots. PublishSnapshotLocked (the producer, under mModelMutex) runs chordlab::fillSlot
  // on the back slot of mSnapshots and publishes it when anything changed; ProcessBlock (the consumer)
  // acquires the newest slot at the top of every block and hands the scheduler a Snapshot pointing into
  // it, which the triple buffer keeps unmodified until the next acquire: exactly the lifetime adopt
  // requires. Every slot is preallocated, so the handoff never allocates, locks or blocks either thread.
  // The generation (mPublication.generation) moves only with the content, so the scheduler's rule "same
  // generation, same content" holds; followHost travels in the slot outside the generation, so a block
  // always gates the progression it is playing with the flag that was saved with it, and a follow-only
  // change costs the scheduler nothing (publication.h). mPublication belongs to the producer.
  chordlab::TripleBuffer<chordlab::SnapshotSlot> mSnapshots;
  chordlab::Publication mPublication;

  // The scheduler belongs to the audio thread (process, reset, panic); pushPreview is its one other-thread
  // entry and is called from the main thread only (OnMessage, CloseWindow). OnReset and panic requests
  // are flagged here and consumed at the top of ProcessBlock.
  chordlab::Scheduler mScheduler;
  static constexpr std::size_t kMidiOutCapacity = 256;
  static_assert(kMidiOutCapacity >= chordlab::Scheduler::kCapacityForImmediateRelease,
                "every owed release must fit the block that owes it");
  std::array<chordlab::MidiOut, kMidiOutCapacity> mMidiOut{};
  static_assert(std::atomic<bool>::is_always_lock_free && std::atomic<double>::is_always_lock_free &&
                std::atomic<uint32_t>::is_always_lock_free, "the audio thread must never lock on these");
  std::atomic<bool> mResetPending{false};
  std::atomic<double> mResetSampleRate{44100.0};
  std::atomic<bool> mPanicPending{false};
  std::atomic<uint32_t> mIncompleteBlocks{0};                  // blocks whose process() returned false
  uint32_t mReportedIncompleteBlocks = 0;                      // main thread: last value pushed to the UI

  // The playhead. ProcessBlock stores the block's host position and whether the progression plays; OnIdle
  // pushes them as kMsgTagTransport at most every kTransportPushInterval, and only when they changed.
  std::atomic<double> mTransportPpq{0.0};
  std::atomic<bool> mTransportPlaying{false};
  // The host tempo as ProcessBlock last saw it, for the MIDI export; 120 until the first block.
  std::atomic<double> mHostBpm{120.0};
  std::string mLastExportPath;                                 // main thread: deleted by the next export
  static constexpr std::chrono::milliseconds kTransportPushInterval{67};   // at most ~15 Hz
  std::chrono::steady_clock::time_point mLastTransportPush{};               // main thread
  double mPushedPpq = std::numeric_limits<double>::quiet_NaN();              // main thread; NaN: nothing pushed yet
  bool mPushedPlaying = false;                                              // main thread
  int mOpenPreviewId = -1;                                     // main thread: Start without a Stop yet

  // Whether an editor is attached: set at the end of OpenWindow, cleared in CloseWindow (both main
  // thread). Any thread may read it; mViewController itself is only touched on the main thread.
  std::atomic<bool> mEditorOpen{false};

  // Shared with the main-thread hops OnMain queues. A hop runs under `mutex` and skips `fn` once `alive`
  // is false; the destructor flips `alive` under the same mutex, so it waits for a hop that is running
  // and makes later ones no-ops. Lock order: Lifeline::mutex, then mModelMutex (see ChordLab.mm).
  struct Lifeline
  {
    std::mutex mutex;
    bool alive = true;
  };
  std::shared_ptr<Lifeline> mLifeline = std::make_shared<Lifeline>();
};
