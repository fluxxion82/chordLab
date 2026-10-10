#pragma once

#include "IPlug_include_in_plug_hdr.h"

using namespace iplug;

class ChordLab final : public Plugin
{
public:
  ChordLab(const InstanceInfo& info);

  void* OpenWindow(void* pParent) override;
  
  void OnParentWindowResize(int width, int height) override;
  bool OnHostRequestingSupportedViewConfiguration(int width, int height) override;

  void OnIdle() override;
  void ProcessBlock(sample** inputs, sample** outputs, int nFrames) override;
  bool OnMessage(int msgTag, int ctrlTag, int dataSize, const void* pData) override;
};
