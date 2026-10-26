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
// Assumption: the host never destroys the plug-in re-entrantly from inside one of these hops (a resize
// request or a snapshot push calling back into a host that deletes the plug-in on the spot); that would
// run the destructor on the thread that already holds the non-recursive Lifeline mutex and deadlock.
// mViewController is written only on the main thread (OpenWindow / CloseWindow); other threads learn
// whether an editor is attached from the atomic mEditorOpen.
//
// Chord preview goes through the engine's Scheduler, not iPlug2's MIDI-from-UI queue (which holds 32
// messages and drops silently, so a note-off could be lost while the host had processing suspended).
// The Swift palette sends kMsgTagPreview {id, on, pitches, velocity}; OnMessage (main thread) parses it
// and calls Scheduler::pushPreview, the scheduler's single other-thread entry (an SPSC ring of 64; a full
// ring is reported to the UI as an error). ProcessBlock owns everything else: it consumes the pending
// reset/panic flags, runs Scheduler::process and forwards the events with SendMidiMsg. The scheduler
// owns the sounding notes: one physical note per pitch, Start/Stop paired by id, and a 4 s watchdog
// releases a preview whose Stop never arrives (editor closed mid-press: Swift's onDisappear sends the
// Stop, CloseWindow pushes one for an outstanding Start as well, and the watchdog covers the rest). The
// watchdog counts processed audio time, not wall time: while the host has processing suspended a
// preview stays armed and is released once blocks run again.
//
// Playback snapshots: the audio thread never reads the document; it plays a compiled copy. After every
// accepted command and every state load (and once at construction), PublishSnapshotLocked runs
// chordlab::fillSlot under mModelMutex on the back slot of a chordlab::TripleBuffer (three preallocated
// chordlab::SnapshotSlots) and publishes it when anything changed; ProcessBlock acquires the newest
// publish at the top of the block and calls Scheduler::adopt with a Snapshot that points into the front
// slot. The buffer guarantees the front slot is not written until the next acquire, and the scheduler
// never reads the previous array once the adopt that replaces it is entered (Snapshot in scheduler.h),
// so the array stays valid and unchanged for exactly as long as adopt requires even though acquire has
// already handed the old slot back to the producer. The producer side belongs to whoever holds mModelMutex (main thread for
// commands, the host's state thread for UnserializeState); the consumer side belongs to ProcessBlock.
// Rapid edits coalesce: an unacquired publish is simply replaced by the next. The generation only moves
// when the compiled content moved, so a browsed-key change or a followHost toggle does not make the
// scheduler flush and re-chase the sounding chord. followHost travels inside the slot, outside the
// generation (publication.h): ProcessBlock ANDs the acquired slot's flag into BlockInfo.playing, so the
// progression and the gate that was saved with it always arrive together (a separate atomic could let a
// block chase an old progression with a newly loaded flag). The other way, ProcessBlock stores the
// block's host position and playing flag in atomics (mTransportPpq, mTransportPlaying) and OnIdle, on
// the main thread, pushes them to the editor as the playhead at ~15 Hz when they change.
//
// The Cocoa view controller: ARC is off for this file. OpenWindow's alloc/init returns the controller
// at +1 and CocoaEditorDelegate::CloseWindow only forgets the pointer, so CloseWindow here releases it.

#include "ChordLab.h"
#include "IPlug_include_in_plug_src.h"

// Pure C++ helpers for message validation and export naming, unit-tested by ChordLab/bridge/tests (bridge_tests).
#include "bridge/export_naming.h"
#include "bridge/export_request.h"
#include "bridge/preview_request.h"

#include "chordlab/json_api.h"
#include "chordlab/smf.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <unistd.h>

#ifdef FRAMEWORK_BUILD
#import <AUv3Framework/ChordLab-Swift.h>
#import <AUv3Framework/ChordLab-Shared.h>
#else
#import <ChordLab-Swift.h>
#endif

// MIDI export files and their cleanup
// -----------------------------------
// Every export (the whole progression or one section) is a new file under NSTemporaryDirectory()/ChordLab
// with a unique name, ChordLab-<label>-<yyyyMMdd-HHmmss>-<pid>-<counter>-<uuid8>.mid, checked for non-existence before it is
// written and never rewritten or truncated afterwards: an NSItemProvider handed to the host copies the
// file lazily, after the drop callback has returned, so a file may back a drag for a while. Nothing is
// deleted at export time. Cleanup is deferred and age-based only: each export purges files in that folder
// named ChordLab-*.mid whose modification time is older than kExportMaxAgeSeconds (ten minutes, any
// instance's), and the destructor purges this instance's own files older than the same age. A file younger
// than ten minutes is never touched, so any drag or drop in flight is safe, and the OS purges the
// temporary folder eventually (macOS clears it on reboot and after days without access), so what the
// purges miss does not accumulate forever.
namespace
{
constexpr double kExportMaxAgeSeconds = 600.0;

// Deletes `path` if its modification time is older than kExportMaxAgeSeconds; a missing file is nothing.
void RemoveIfStale(NSString* path)
{
  NSDictionary* attributes = [[NSFileManager defaultManager] attributesOfItemAtPath:path error:nil];
  NSDate* modified = attributes ? attributes[NSFileModificationDate] : nil;
  if (modified && -[modified timeIntervalSinceNow] > kExportMaxAgeSeconds)
    [[NSFileManager defaultManager] removeItemAtPath:path error:nil];
}

// Purges every stale export in `dir`, whichever instance or process wrote it. What counts as an export
// is the name rule ChordLab-*.mid (chordlab::bridge::isChordLabExportName).
void PurgeStaleExports(NSString* dir)
{
  NSArray<NSString*>* names = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:dir error:nil];
  for (NSString* name in names)
  {
    const char* utf8 = [name UTF8String];
    if (utf8 && chordlab::bridge::isChordLabExportName(utf8))
      RemoveIfStale([dir stringByAppendingPathComponent:name]);
  }
}
}  // namespace

ChordLab::ChordLab(const InstanceInfo& info)
: Plugin(info, MakeConfig(kNumParams, kNumPresets))
{
  GetParam(kParamGain)->InitDouble("Gain", 100., 0., 100.0, 0.01, "%");
  
  MakeDefaultPreset();

  std::lock_guard<std::mutex> lock(mModelMutex);
  RebuildPaletteLocked();
  PublishSnapshotLocked();                                       // the empty progression, generation 1
}

ChordLab::~ChordLab()
{
  {
    std::lock_guard<std::mutex> guard(mLifeline->mutex);       // waits for a hop in flight; later hops do nothing
    mLifeline->alive = false;
  }
  // This instance's exports that are old enough to be certainly unused; younger ones stay for the next
  // purge or the OS (see the cleanup note above). May run off the main thread (AUv3), hence the pool.
  @autoreleasepool
  {
    for (const std::string& path : mExportPaths)
      RemoveIfStale([NSString stringWithUTF8String:path.c_str()]);
  }
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
  
  mEditorOpen.store(true);
  OnUIOpen();
  
  return vc.view;
}

void ChordLab::CloseWindow()
{
  // Main thread. AUv2's view factory path has no close callback in iPlug2, so that lifecycle still
  // leaks the controller (known; Phase 1 targets VST3).
  id controller = (id) mViewController;                         // +1 from OpenWindow's alloc/init
  if (mOpenPreviewId >= 0)                                       // belt and braces: the UI's onDisappear sends this too
  {
    chordlab::PreviewCmd stop{};
    stop.kind = chordlab::PreviewCmd::Stop;
    stop.id = mOpenPreviewId;
    mScheduler.pushPreview(stop);                                // a full ring is covered by the watchdog
    mOpenPreviewId = -1;
  }
  CocoaEditorDelegate::CloseWindow();                            // forgets mViewController
  mEditorOpen.store(false);
  [controller release];
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

bool ChordLab::OnKeyDown(const IKeyPress& key)
{
  // Main thread: the host calls IPlugView::onKeyDown on the UI thread, between OpenWindow and
  // CloseWindow, so mViewController is valid or null. iPlug2's IKeyPress: utf8 text, a Windows virtual
  // key (kVK_BACK 0x08, kVK_DELETE 0x2E, digits 0x30..0x39), S = shift, C = command on macOS
  // (IPlugVST3_View.h TranslateKeyMessage reads kCommandKey into C), A = alt; Control is not carried.
  auto* vc = (ChordLabViewController*) mViewController;
  if (!vc)
    return false;
  @autoreleasepool   // ARC is off here: the bridged string is autoreleased
  {
    NSString* characters = [NSString stringWithUTF8String:key.utf8] ?: @"";   // nil only for invalid UTF-8
    return [vc handleKeyWithCharacters:characters
                            virtualKey:key.VK shift:key.S command:key.C option:key.A];
  }
}

bool ChordLab::OnKeyUp(const IKeyPress& key)
{
  // Main thread, like OnKeyDown, with the same translation; the router ends the key's hold.
  auto* vc = (ChordLabViewController*) mViewController;
  if (!vc)
    return false;
  @autoreleasepool
  {
    NSString* characters = [NSString stringWithUTF8String:key.utf8] ?: @"";
    return [vc handleKeyUpWithCharacters:characters
                              virtualKey:key.VK shift:key.S command:key.C option:key.A];
  }
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

void ChordLab::OnReset()
{
  // May run off the audio thread; the scheduler's reset is marshalled to the next ProcessBlock.
  mResetSampleRate.store(GetSampleRate(), std::memory_order_relaxed);
  mResetPending.store(true, std::memory_order_release);
}

void ChordLab::ProcessBlock(sample** inputs, sample** outputs, int nFrames)
{
  // Audio thread: nothing here may touch mStore, mPalette or mModelMutex (see the note at the top).
  // That it allocates nothing is checked engine-side (engine_rt_alloc_test covers the scheduler and the
  // triple buffer); the iPlug2 forwarding below is outside that proof (see the capacity note).
  // MIDI-only plugin: Live requires an audio bus, so keep the outputs silent.
  // Not zeroing them leaves stale host buffers playing (iPlug2 Discourse, 2024).
  const int nOut = NOutChansConnected();
  for (int c = 0; c < nOut; ++c)
    std::fill(outputs[c], outputs[c] + nFrames, 0.0);

  if (mResetPending.exchange(false, std::memory_order_acq_rel))
    mScheduler.reset(mResetSampleRate.load(std::memory_order_relaxed));
  // panic() before process(): panic records the preview ring's tail as it sees it, and the drain inside
  // process discards every command queued before that tail (Scheduler::panic), so a Start the UI pushed
  // before pressing Panic never sounds, while commands pushed after the panic are served as usual.
  if (mPanicPending.exchange(false, std::memory_order_acq_rel))
    mScheduler.panic();
  // The newest published progression, if any arrived since the last block. The pointer stays valid and
  // unchanged until the next acquire, which is the next block's, after which the scheduler no longer
  // reads it (Scheduler::adopt); a changed generation makes the scheduler release and re-chase.
  if (const chordlab::SnapshotSlot* s = mSnapshots.acquire())
    mScheduler.adopt(chordlab::Snapshot{s->events.data(), s->count, s->endBeat, s->loop, mSnapshots.currentGeneration()});

  // Follow Live gates the progression only: previews are independent of the transport, and the scheduler
  // releases the progression on the playing -> stopped edge, so turning it off mid-chord is clean. The
  // flag comes from the slot the scheduler is playing (the front slot; the document's default before the
  // first acquire), never from somewhere the progression could outrun.
  const chordlab::SnapshotSlot* front = mSnapshots.current();
  const bool followHost = front ? front->followHost : true;
  const double ppq = GetPPQPos();
  const double bpm = GetTempo();
  const bool playing = GetTransportIsRunning() && followHost;
  const chordlab::BlockInfo block{ppq, nFrames, GetSampleRate(), bpm, playing};
  mTransportPpq.store(ppq, std::memory_order_relaxed);             // the playhead, read by OnIdle
  mTransportPlaying.store(playing, std::memory_order_relaxed);
  mHostBpm.store(bpm, std::memory_order_relaxed);                  // the export's tempo
  std::size_t written = 0;
  const bool complete = mScheduler.process(block, mMidiOut.data(), mMidiOut.size(), written);

  // Output capacity (plan M3.3): iPlug2's VST3 output event queue is page-rounded (at least 512 entries),
  // so a burst of at most kMidiOutCapacity events fits without a realloc; but incoming MIDI shares that
  // queue, its events are retained across blocks while the host passes no pOutputEvents, and the
  // standalone APP's SendMidiMsg allocates a std::vector per message (IPlug/APP/IPlugAPP.cpp:79).
  for (std::size_t i = 0; i < written; ++i)
  {
    const chordlab::MidiOut& e = mMidiOut[i];
    IMidiMsg msg;
    if ((e.status & 0xF0) == 0x90)
      msg.MakeNoteOnMsg(e.data1, e.data2, e.sampleOffset);
    else
      msg.MakeNoteOffMsg(e.data1, e.sampleOffset);
    SendMidiMsg(msg);
  }

  if (!complete)
    mIncompleteBlocks.fetch_add(1, std::memory_order_relaxed);   // no logging on the audio thread; OnIdle reports it
}

void ChordLab::OnUIOpen()
{
  Plugin::OnUIOpen();
  PushSnapshot();
  mReportedIncompleteBlocks = 0;                                 // make OnIdle resend the current count
  mPushedPpq = std::numeric_limits<double>::quiet_NaN();         // ...and the playhead
}

void ChordLab::OnIdle()
{
  const uint32_t incomplete = mIncompleteBlocks.load(std::memory_order_relaxed);
  if (incomplete != mReportedIncompleteBlocks && mEditorOpen.load())
  {
    mReportedIncompleteBlocks = incomplete;
    PushStatus(incomplete);
  }

  // The playhead: at most every kTransportPushInterval, only to an open editor, only when it moved more
  // than a rounding error or started/stopped. mPushedPpq is NaN before the first push (or after an editor
  // opened), and a NaN comparison is false, so that first push always happens.
  if (mEditorOpen.load())
  {
    const auto now = std::chrono::steady_clock::now();
    if (now - mLastTransportPush >= kTransportPushInterval)
    {
      double ppq = mTransportPpq.load(std::memory_order_relaxed);
      bool playing = mTransportPlaying.load(std::memory_order_relaxed);
      if (!std::isfinite(ppq))
      {
        ppq = 0.0;                                               // nlohmann would write null for NaN or inf...
        playing = false;                                         // ...and an unusable position hides the playhead
      }
      if (!(std::fabs(ppq - mPushedPpq) <= 1e-6) || playing != mPushedPlaying)
      {
        mLastTransportPush = now;
        mPushedPpq = ppq;
        mPushedPlaying = playing;
        PushTransport(ppq, playing);
      }
    }
  }
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
      // palette is cheap (35 cells). Likewise always recompile: PublishSnapshotLocked publishes only
      // when the playback content actually changed.
      if (accepted)
      {
        RebuildPaletteLocked();
        PublishSnapshotLocked();
      }
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

  if (msgTag == kMsgTagPreview)
  {
    HandlePreview((pData && dataSize > 0) ? std::string_view((const char*) pData, (size_t) dataSize) : std::string_view());
    return true;
  }

  if (msgTag == kMsgTagPanic)
  {
    mPanicPending.store(true, std::memory_order_release);        // panic must run on the audio thread
    mOpenPreviewId = -1;                                         // the scheduler forgets every preview
    return true;
  }

  if (msgTag == kMsgTagExport)
  {
    HandleExport();
    return true;
  }

  if (msgTag == kMsgTagExportSection)
  {
    HandleExportSection((pData && dataSize > 0) ? std::string_view((const char*) pData, (size_t) dataSize) : std::string_view());
    return true;
  }

  return CocoaEditorDelegate::OnMessage(msgTag, ctrlTag, dataSize, pData);
}

void ChordLab::HandlePreview(std::string_view text)
{
  // Main thread, never real-time: nlohmann parses here (parsePreviewRequest, which owns the validation
  // rules and the error texts), the scheduler only ever sees plain PreviewCmd.
  std::string problem;
  const std::optional<chordlab::PreviewCmd> parsed = chordlab::bridge::parsePreviewRequest(text, &problem);
  if (!parsed)
  {
    DBGMSG("ChordLab: %s\n", problem.c_str());
    PushError(problem);
    return;
  }
  const chordlab::PreviewCmd& cmd = *parsed;
  if (!mScheduler.pushPreview(cmd))
  {
    PushError("preview queue full");                             // the watchdog (4 s of processed audio) still ends what sounds
    return;
  }
  mOpenPreviewId = cmd.kind == chordlab::PreviewCmd::Start ? cmd.id : (cmd.id == mOpenPreviewId ? -1 : mOpenPreviewId);
}

void ChordLab::HandleExport()
{
  // Main thread, never real-time: compile and the SMF writer allocate. The export is exactly the playback
  // content, so a dropped clip is as long as the progression, trailing silence included (D10).
  @autoreleasepool
  {
  chordlab::Compiled compiled;
  std::string label;
  {
    std::lock_guard<std::mutex> lock(mModelMutex);
    compiled = chordlab::compile(mStore.doc());
    label = chordlab::keyName(mStore.doc().projectKey);
  }
  WriteExport(compiled, label);
  }
}

void ChordLab::HandleExportSection(std::string_view text)
{
  // Main thread. {"schema":1,"id":n}: the section is compiled as it sounds in the whole progression
  // (compileSection slices the whole document's voicings) and re-based to beat 0, so the clip starts at
  // once and is as long as the section.
  @autoreleasepool
  {
  // parseSectionExportRequest reads schema and id like HandlePreview does (boundedIntField: the full
  // 64-bit value is range-checked BEFORE narrowing), so an oversized schema (4294967297, which narrows to
  // 1) or id (2^31, -2^40) is refused with tag 21 rather than read as something in range.
  const std::optional<chordlab::bridge::SectionExportRequest> request = chordlab::bridge::parseSectionExportRequest(text);
  if (!request)
  {
    PushError("section export message needs schema 1 and a section id");
    return;
  }
  const int id = request->sectionId;
  chordlab::Compiled compiled;
  std::string label;
  bool found = false;
  {
    std::lock_guard<std::mutex> lock(mModelMutex);
    const int index = mStore.findSectionIndex(id);
    if (index >= 0)
    {
      const chordlab::Document& doc = mStore.doc();
      compiled = chordlab::compileSection(doc, index);
      label = chordlab::keyName(doc.projectKey) + "-" + doc.sections[(size_t) index].name;
      found = true;
    }
  }
  if (!found)
  {
    PushError("no such section");                                // outside the lock, like every call into the UI
    return;
  }
  WriteExport(compiled, label);
  }
}

void ChordLab::WriteExport(const chordlab::Compiled& compiled, const std::string& label)
{
  // Main thread, never real-time. The tempo is the host's as ProcessBlock last saw it (120 before the
  // first block).
  @autoreleasepool
  {
  double bpm = mHostBpm.load(std::memory_order_relaxed);
  if (!std::isfinite(bpm) || bpm <= 0.0)
    bpm = 120.0;
  const std::vector<uint8_t> bytes = compiled.events.empty() ? std::vector<uint8_t>{}
                                                             : chordlab::writeSmf0(compiled, bpm, 4, 4, 480);
  if (bytes.empty())
  {
    PushError("nothing to export");                              // no chords, or the writer refused (a tempo it cannot encode)
    return;
  }

  // NSTemporaryDirectory()/ChordLab/ChordLab-<label>-<yyyyMMdd-HHmmss>-<pid>-<counter>-<uuid8>.mid, the
  // label (the key, plus the section name for a section) spelled with 's' for a sharp and '-' for
  // anything but an ASCII letter or digit so the name is safe anywhere ("C-Ionian", "Fs-Dorian",
  // "C-Ionian-Verse"); pid, counter and uuid make it unique across instances and processes, and the name
  // is checked to be unused before anything is written (see the cleanup note above).
  const std::string safeLabel = chordlab::bridge::safeExportLabel(label);
  char stamp[32] = {0};
  const std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_r(&now, &local);
  std::strftime(stamp, sizeof stamp, "%Y%m%d-%H%M%S", &local);
  NSString* dir = [NSTemporaryDirectory() stringByAppendingPathComponent:@"ChordLab"];
  if (![[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil])
  {
    PushError("could not create the export folder");
    return;
  }
  std::string path;
  for (int attempt = 0; attempt < 8 && path.empty(); ++attempt)
  {
    NSString* uuid = [[[NSUUID UUID] UUIDString] substringToIndex:8];
    std::string candidate = std::string([dir UTF8String]) + "/" +
                            chordlab::bridge::exportFileName(safeLabel, stamp, (long) getpid(), ++mExportCounter, [uuid UTF8String]);
    if (![[NSFileManager defaultManager] fileExistsAtPath:[NSString stringWithUTF8String:candidate.c_str()]])
      path = std::move(candidate);                             // never an existing file: nothing is ever rewritten
  }
  if (path.empty())
  {
    PushError("could not name the MIDI file");
    return;
  }
  if (!chordlab::writeFile(path, bytes))
  {
    PushError("could not write the MIDI file");
    return;
  }
  mExportPaths.push_back(path);
  mLastExportPath = path;                                        // for the UI only: nothing is deleted on its account
  PurgeStaleExports(dir);                                        // deferred cleanup: ten-minute-old files of any instance
  PushExportReady(path);
  }
}

void ChordLab::RebuildPaletteLocked()
{
  const chordlab::Document& doc = mStore.doc();
  mPalette = chordlab::buildPalette(doc.browsedKey, doc.projectKey);
}

void ChordLab::PublishSnapshotLocked()
{
  // Caller holds mModelMutex: that serializes the producer side of mSnapshots across the main thread and
  // the hosts' state threads (the mutex hand-off orders one publish before the next). Never the audio
  // thread: fillSlot compiles, which allocates. fillSlot decides what counts as a change and whether the
  // generation moves (content yes, followHost no); an unchanged document publishes nothing.
  if (chordlab::fillSlot(mStore.doc(), mSnapshots.beginWrite(), mPublication))
    mSnapshots.publish(mPublication.generation);
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
    if (!mEditorOpen.load())                                     // no listener: nothing to serialize
      return;
    std::string json;
    {
      std::lock_guard<std::mutex> lock(mModelMutex);
      json = chordlab::snapshotJson(mStore.doc(), mPalette, mStore.selectedEventId());  // the live selection drives suggestions
    }
    SendArbitraryMsgFromDelegate((int) kMsgTagSnapshot, (int) json.size(), json.data());
  });
}

void ChordLab::PushError(const std::string& message)
{
  OnMain([this, message] {
    if (!mEditorOpen.load())
      return;
    // The only JSON the plugin writes itself; nlohmann escapes the message (engine error strings quote
    // user input, e.g. unknown cmd "x"), replacing any invalid UTF-8 rather than throwing.
    const std::string json = nlohmann::json{{"schema", chordlab::kJsonSchema}, {"error", message}}
                               .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    SendArbitraryMsgFromDelegate((int) kMsgTagError, (int) json.size(), json.data());
  });
}

void ChordLab::PushStatus(uint32_t incompleteBlocks)
{
  OnMain([this, incompleteBlocks] {
    if (!mEditorOpen.load())
      return;
    const std::string json = nlohmann::json{{"schema", chordlab::kJsonSchema}, {"incompleteBlocks", incompleteBlocks}}.dump();
    SendArbitraryMsgFromDelegate((int) kMsgTagStatus, (int) json.size(), json.data());
  });
}

void ChordLab::PushTransport(double ppq, bool playing)
{
  OnMain([this, ppq, playing] {
    if (!mEditorOpen.load())
      return;
    const std::string json = nlohmann::json{{"schema", chordlab::kJsonSchema}, {"ppq", ppq}, {"playing", playing}}.dump();
    SendArbitraryMsgFromDelegate((int) kMsgTagTransport, (int) json.size(), json.data());
  });
}

void ChordLab::PushExportReady(const std::string& path)
{
  OnMain([this, path] {
    if (!mEditorOpen.load())
      return;
    const std::string json = nlohmann::json{{"schema", chordlab::kJsonSchema}, {"path", path}}
                               .dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    SendArbitraryMsgFromDelegate((int) kMsgTagExportReady, (int) json.size(), json.data());
  });
}

void ChordLab::RestoreEditorSize(int width, int height)
{
  // Main thread, for a size restored while an editor is open (UnserializeState stores it directly when
  // none is). The stored size must stay the actual size of the open editor: it changes only if the host
  // resizes. VST3 and APP implement EditorResize (host window resize, true, and they record the size
  // themselves after comparing with GetEditorWidth/Height, so it must run before any SetEditorSize);
  // AUv2 and AUv3 inherit the default, which resizes nothing and returns false.
  if (!mEditorOpen.load())
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
        PublishSnapshotLocked();                                 // the audio thread plays the loaded set, editor or not
        editorOpen = mEditorOpen.load();
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
