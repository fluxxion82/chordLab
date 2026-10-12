#pragma once

#include "IPlug_include_in_plug_hdr.h"

#include "chordlab/document.h"
#include "chordlab/palette.h"

#include <cstdint>
#include <string>

using namespace iplug;

class ChordLab final : public Plugin
{
public:
  ChordLab(const InstanceInfo& info);

  void* OpenWindow(void* pParent) override;
  
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
  // changed either key (commands, undo/redo, a state load).
  void RebuildPalette();
  // Pushes snapshotJson(doc, palette) to the editor; a no-op when no editor is open.
  void PushSnapshot();
  // Pushes {"schema":1,"error":message} to the editor.
  void PushError(const std::string& message);

  static constexpr uint8_t kStateVersion = 2;   // 1 was the M0 spike payload; it is not migrated

  // The model. Main thread only: OnMessage, OnUIOpen and the state chunk touch it; ProcessBlock never does.
  chordlab::DocumentStore mStore;
  chordlab::Palette mPalette;

  // Last editor size the host reported, persisted with the document.
  int mEditorWidth = PLUG_WIDTH;
  int mEditorHeight = PLUG_HEIGHT;
};
