#pragma once

// The editor side of vst3host (editor.mm, Objective-C++ under ARC): a parent view for an instance's editor, and the
// editor's own way of sending a message to the plug-in. A host has no message path into a single-component VST3, so
// the host opens the editor (IPlugView::attached, which runs ChordLab::OpenWindow) and calls the method the Swift
// editor calls for every message it sends (IPlugCocoaViewController's sendArbitraryMsgFromUI:::, which reaches
// ChordLab::OnMessage through iPlug2's editor delegate) on the editor's real view controller. What the plug-in sends
// back to the editor (its onMessage:::) is recorded on the way, then delivered as usual. Main thread only.

#include <functional>
#include <string>
#include <vector>

namespace editor {

/// The bridge's message tags (ChordLab/ChordLab-Shared.h), read from the header the editor and the plug-in share.
extern const int kPreview;
extern const int kPanic;
extern const int kError;

/// Runs `body` inside an autorelease pool (the editor's objects are autoreleased; a command-line host has no pool).
void withPool(const std::function<void()>& body);

/// Makes the application object AppKit's views expect, once, with no Dock icon or menu bar, and never runs it.
void startApplication();

/// A new parent view (an NSView, retained, in no window) of `width` x `height`, for IPlugView::attached.
void* makeParent(int width, int height);
/// Releases a parent view made by makeParent.
void releaseParent(void* parent);

/// Whether an editor's view controller sits in `parent` (the view OpenWindow added, whose next responder is its
/// controller) and takes the editor's messages.
bool hasController(void* parent);

/// Sends `json` (empty: no payload) under `tag` the way the editor does: sendArbitraryMsgFromUI with no control tag
/// on the controller of the editor in `parent`. False when there is none.
bool send(void* parent, int tag, const std::string& json);

/// The payloads of the kError messages the plug-in sent to an editor since the last call, oldest first.
std::vector<std::string> takeErrors();

}  // namespace editor
