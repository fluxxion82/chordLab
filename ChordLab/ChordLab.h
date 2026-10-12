#pragma once

#include "IPlug_include_in_plug_hdr.h"

#include "chordlab/document.h"
#include "chordlab/palette.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

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
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;

  // Versioned state chunk: u8 version, i32 payload length, stateJson payload, then iPlug2's params.
  bool SerializeState(IByteChunk& chunk) const override;
  int UnserializeState(const IByteChunk& chunk, int startPos) override;

private:
  // Palette of the browsed key annotated against the project key; call after anything that may have
  // changed either key (commands, undo/redo, a state load). The caller holds mModelMutex.
  void RebuildPaletteLocked();
  // Pushes snapshotJson(doc, palette) to the editor, on the main thread; a no-op when no editor is open.
  void PushSnapshot();
  // Pushes {"schema":1,"error":message} to the editor, on the main thread.
  void PushError(const std::string& message);
  // Runs `fn` on the main thread: inline when already there, else queued; dropped once the plugin is gone.
  void OnMain(std::function<void()> fn);
  // Main thread: adopts a restored editor size, asking the host to resize an open editor.
  void RestoreEditorSize(int width, int height);

  static constexpr uint8_t kStateVersion = 2;   // 1 was the M0 spike payload; it is not migrated

  // The model and the editor size (iPlug2's IEditorDelegate fields) are read and written under mModelMutex
  // from the main thread and from the hosts' state threads; see the ownership note in ChordLab.mm.
  // ProcessBlock never touches any of them.
  mutable std::mutex mModelMutex;
  chordlab::DocumentStore mStore;
  chordlab::Palette mPalette;

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
