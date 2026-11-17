// Ownership and threads
// ---------------------
// The model (mStore, mPalette) and the editor size (IEditorDelegate::mEditorWidth/Height) are reached from
// two kinds of non-audio thread: the main thread (OnMessage from the Swift UI, OnUIOpen, OnParentWindowResize)
// and the host's state thread (SerializeState / UnserializeState: VST3 calls them on the UI thread, AUv2
// SetState and AUv3 fullState may call them from a background thread). Every access takes mModelMutex, and
// no lock is held while calling into iPlug2 or the UI, so PushSnapshot can run inline on the main thread
// without re-entering the lock. The audio thread (ProcessBlock, HandleMidiIn) never touches the model or
// the mutex.
// Delivery to the Swift view controller (PushSnapshot, PushError, RestoreEditorSize) always happens on the
// main thread: OnMain runs the block inline when already there and dispatches it otherwise. A dispatched
// block holds the Lifeline mutex while it runs and does nothing once `alive` is false; the destructor
// flips `alive` under that mutex, so it blocks until a hop in flight has finished and every later hop is
// a no-op. That matters because destruction is not always on the main thread (AUv3's dealloc deletes the
// plug-in on whatever thread releases the audio unit, IPlugAUAudioUnit.mm). No deadlock: the destructor
// never runs inside a queued hop, the inline main-thread path takes no Lifeline lock (so a hop calling
// PushSnapshot does not re-enter it), and the lock order is always Lifeline::mutex then mModelMutex (then the
// link registry's mutex, a leaf; see "Links" below). The same Lifeline guards the republish hook other instances
// run (chordlab::guardedHook), and the destructor retires it (chordlab::retire) before it leaves the registry.
// Assumption: the host never destroys the plug-in re-entrantly from inside one of these hops (a resize
// request or a snapshot push calling back into a host that deletes the plug-in on the spot); that would
// run the destructor on the thread that already holds the non-recursive Lifeline mutex and deadlock.
// mViewController is written only on the main thread (OpenWindow / CloseWindow); other threads learn
// whether an editor is attached from the atomic mEditorOpen.
// The clipboard token (mClipboardToken, tracks design D21) belongs with the document: guarded by mModelMutex,
// renewed by UnserializeState in the same section that replaces the document, and read with the document by
// OpenWindow and by PushSnapshot's hop, which hands a changed token to the open view controller before it sends
// the snapshot read in that same section, so the editor never shows a loaded document under the old token.
// The token is also the lineage guard on edits (tracks plan Task 24a): the editor stamps every kMsgTagCommand (and
// every kMsgTagExportClip, checked the same way by HandleExportClip) with the token it held when the user's action began ("lineage", ChordLabState.send(command:), one token per action so a
// stale answer to an action's first command cannot let its later ones through). OnMessage reads that member BEFORE taking mModelMutex (chordlab::bridge::readLineage: a pure
// parse that never throws, so a 1 MB command's parse never holds the lock against a host's Serialize/Unserialize) and
// compares it with mClipboardToken under the lock, in the section that would apply the command (compareLineage, a
// string comparison that neither allocates nor throws). Without that, an AUv2/AUv3 load on the host's state thread
// could take the lock first and replace the document while a command computed for the old one (a chord id, a paste
// with its track ids, a Generate entry) waited for the lock on the main thread, ahead of the hop bringing the new
// token and snapshot. A stale command applies nothing (no palette rebuild, publish, host dirty or error); after the
// lock OnMessage calls PushSnapshot, which runs inline on the main thread and so hands the editor the current token
// and snapshot before the editor can send again. Because every snapshot the editor receives is preceded by its
// document's token (OpenWindow sets it before the view, and so the editor state, exists; PushSnapshot's hop sets a
// changed one before sending), a command is stale only when a load really replaced the document it was computed for.
// A command without lineage (tests, tools) is applied unchecked; one whose lineage is not a string, or whose lineage
// could not be read at all (an allocation failure: never "apply", the engine's own parse might then succeed), applies
// nothing and is answered with an error.
//
// Chord preview goes through the engine's Scheduler, not iPlug2's MIDI-from-UI queue (which holds 32
// messages and drops silently, so a note-off could be lost while the host had processing suspended).
// The Swift UI sends kMsgTagPreview {id, on, pitches, velocity}, and {id, extend} once a second while a
// preview is held (merged in the ring while the host is not processing): a palette cell, a piano-roll note press or
// a keyboard-column key (one held preview at a time, ChordLabState / PreviewBook), plus the roll's short one-shots
// (an insert, a velocity drag's release) that may overlap the held one, each with its own id; OnMessage (main thread)
// parses it and calls Scheduler::pushPreview, the scheduler's single other-thread entry (an SPSC ring of
// 64; a full ring is reported to the UI as an error). The audio thread owns everything else: ProcessBlock
// consumes the pending reset/bypass/panic flags, runs Scheduler::process and forwards the events with
// SendMidiMsg (HandleMidiIn queues incoming notes for thru and may consume the pending reset and bypass
// release first; see "MIDI in"). The scheduler owns the sounding notes: one physical note per pitch, Start/Stop paired by id, an Extend
// moving a held preview's deadline, and a 4 s watchdog that releases a preview whose Stop never arrives
// (editor closed mid-press: Swift's onDisappear sends the Stop, CloseWindow pushes one for an outstanding
// Start as well, and the watchdog covers the rest). The watchdog counts processed audio time, not wall
// time, from the Start or the last applied Extend: while the host has processing suspended a preview
// stays armed and is released once blocks run again.
//
// Playback snapshots: the audio thread never reads the document; it plays a compiled copy. After every
// accepted command and every state load (and once at construction, and whenever another instance runs this
// one's republish hook; see "Links" below), PublishSnapshotLocked has the link endpoint
// (LinkEndpoint::publishOwn) run chordlab::fillSlot under mModelMutex on the back slot of a
// chordlab::TripleBuffer (three preallocated chordlab::SnapshotSlots, mSnapshots, the own buffer) and publish it
// when anything changed; ProcessBlock (LinkSource::select) acquires the newest publish at the top of the block
// and calls Scheduler::adopt with a Snapshot that points into the front slot. The buffer guarantees the front
// slot is not written until the next acquire, and the scheduler never reads the previous array once the adopt
// that replaces it is entered (Snapshot in scheduler.h), so the array stays valid and unchanged for exactly as
// long as adopt requires even though acquire has already handed the old slot back to the producer. The producer
// side belongs to whoever holds mModelMutex (main thread for commands, the host's state thread for
// UnserializeState, any thread running this instance's republish hook); the consumer side belongs to ProcessBlock.
// Rapid edits coalesce: an unacquired publish is simply replaced by the next. The generation only moves
// when the compiled content moved, so a browsed-key change or a followHost or midiThru toggle does not make
// the scheduler flush and re-chase the sounding chord. followHost and midiThru travel inside the slot,
// outside the generation (publication.h): ProcessBlock ANDs the played slot's followHost into
// BlockInfo.playing and passes the own slot's midiThru as BlockInfo.thru (the same slot unless the instance
// follows: a follower keeps its own Thru switch and takes Follow Live from its leader's feed), so the
// progression and the flags that were saved with it always arrive together (a separate atomic could let a
// block chase an old progression with a newly loaded flag). The other way, ProcessBlock stores the
// block's host position and playing flag in atomics (mTransportPpq, mTransportPlaying) and OnIdle, on
// the main thread, pushes them, with the tempo, to the editor as the playhead at ~15 Hz when they change (the tempo also reaches Record, strip editing design D4).
//
// Links (per-track instruments plan I4 to I9, I11, I15): every instance has a link endpoint (mLink, engine
// chordlab::LinkEndpoint) registered with a chordlab::LinkRegistry under a persistent instance id (MakeUid, a
// random UUID; the endpoint draws document ids and lane uids from the same helper). Only the VST3 joins the
// process's shared registry, where instances find each other and a follower plays one lane of a leader; the AU
// and the app register with a registry of their own (mPrivateRegistry) that no other instance sees, so they run
// the same code and behave as before (kLinks false). Such an instance can never bind a follow, so it drops a follow
// a loaded state carries before adopting it (chordlab::bridge::adoptableState: it plays its own document, and its
// next save writes no follow; silence for ever would be the wrong failure), and it applies no link message, whose
// answer is the link state alone (chordlab::bridge::linkApplies). Two buffers feed the scheduler:
// - the own buffer (mSnapshots): written only by LinkEndpoint::publishOwn, called only from
//   PublishSnapshotLocked, so its producer is the holder of mModelMutex. A leading instance publishes there
//   every lane nobody follows, and in the same call hands the registry a feed from which the registry writes its
//   bound followers' link buffers; a following instance publishes its whole document there (not played, but its
//   Thru rides in it and an unfollow returns to it). The shell never fills or publishes a buffer itself.
// - the link buffer (mLink->linkBuffer()): written only by the registry, under the registry's mutex, from
//   whichever instance's call is holding it (a leader's publish, a follow, an unfollow, a removal); never by this
//   instance's code directly.
// The audio thread reads mLink->following() (an atomic) and its two buffers, through LinkSource::select at the top
// of ProcessBlock, and nothing else of the endpoint or the registry; HandleMidiIn, OnReset and the AU drain have no
// link role. Every other endpoint call is made under mModelMutex. Lock order: Lifeline::mutex, then mModelMutex,
// then the registry's mutex (a leaf: the registry calls nothing while holding it and never takes a model mutex),
// and no thread holds two instances' model mutexes.
// Hooks: an operation that changes who follows what (a state load's adopt or adoptWithoutBlock here; follow and
// unfollow with the link messages) returns the republish hooks of the leaders it affected. The caller runs them
// (RunHooks) after releasing every lock it holds, mModelMutex and its own Lifeline both (so never from inside an
// OnMain hop), and then republishes itself (PublishSnapshotLocked under mModelMutex again), as UnserializeState
// does: every buffer is right before the operation returns, with or without an idle timer. A hook
// (chordlab::guardedHook) holds the target's Lifeline, not the target: it takes that instance's Lifeline mutex,
// does nothing once it is retired, else takes its mModelMutex and calls PublishSnapshotLocked, and nothing else
// (no editor, no host call; editors refresh from the idle timer). An instance never receives its own hook. A hook
// may run on any non-audio thread: the main thread of another instance, a host's state thread, or a destructor's.
// Construction: the constructor takes mModelMutex BEFORE making mLink and holds it through the first publish. The
// endpoint registers (LinkRegistry::add) inside its own constructor, before mLink is assigned; neither calls a hook
// (add returns none and never runs one under the registry's mutex), and a hook of this instance that another thread
// ran in that window would wait on mModelMutex until mLink exists and the first publish is done. No such hook can
// exist today (another instance gets this one's hook only through a follow, which needs a stored feed with a
// matching lineage, i.e. after the first publish, and no follower can be waiting for a fresh uid): the lock closes
// nothing that is open now; it keeps the constructor safe whatever the registry learns to do later. Only after that
// section, with the lock released, does the constructor call iPlug2's MakeDefaultPreset, which runs SerializeState
// (mLink and mModelMutex both): it is the constructor's last call.
// Destruction, in this order: the AU editor close; chordlab::retire(*mLifeline), which waits for a hop or hook in
// flight and makes later ones no-ops, so no other instance enters this one any more; mLink.reset(), which removes
// the registration (followers are emptied and wait for this id, the registry never writes this link buffer
// again) and runs the hook of the leader this instance was bound to, holding no lock of this instance; then the
// export cleanup; mPrivateRegistry goes after mLink, with the members. State: SerializeState writes the endpoint's
// instance block (id, document id, lane uids, follow) with the document, read in one mModelMutex section;
// UnserializeState replaces the document, renews the clipboard token and adopts the loaded block (or, for a state
// without one, resets the link identity) in one mModelMutex section, then runs the hooks and republishes as above.
// Assumption, as for the hops above: a host never calls UnserializeState from inside one of this instance's OnMain
// hops (it would run other instances' hooks with this instance's Lifeline held, outside the lock order above).
// Messages (plan I12): the editor sends kMsgTagLink (18), follow a leader's lane or unfollow, handled by HandleLink
// on the main thread, inline from OnMessage (so with no Lifeline lock held): parseLinkRequest, the message's own
// "lineage" read before mModelMutex and compared under it as a command's (stale: nothing applied, PushSnapshot; not
// a string or unreadable: the command path's errors; absent, unlike a command's: nothing applied and an error of its
// own, since a link message must carry it, plan I9), and when it is current LinkEndpoint::follow or unfollow in that
// same section; then the hooks with nothing held, the own republish, MarkHostDirty when the follow the state saves
// changed, and always PushLinkState (chordlab::bridge::linkOutcome decides which). A refused follow (the leader's
// lineage stale, an unknown leader or lane) changes nothing and sends no error: the fresh link state is the answer.
// The plug-in sends kMsgTagLinkState (26), linkStateJson(mLink->view()) built under mModelMutex inside an OnMain
// hop, so only ever from the main thread: from OnUIOpen (after the snapshot), after every parsed kMsgTagLink, and,
// for an instance that links only (kLinks, the VST3), from OnIdle when the shared registry's serial() (an atomic,
// read without a lock) differs from mReportedLinkSerial. It is display only; nothing that plays waits for it. OnIdle
// never touches mLink: it reads the serial from chordlab::LinkRegistry::shared(), which is never destroyed, so an
// idle callback that overlaps a destructor on another thread (an AU host may destroy the plug-in off the main
// thread, plan V6; iPlug2 stops the idle timer only in ~IPlugAPIBase, after ~ChordLab) reads nothing that goes
// away. An AU or app instance gets its one link state on OnUIOpen and nothing from OnIdle (its registry is its own:
// nothing there ever changes for it to show). The push OnIdle makes, PushLinkState inline on the main thread, does
// read mLink under mModelMutex: that rests on the VST3's destructor running on the main thread, the thread of
// OnIdle (plan V14), so the two never interleave. A queued PushLinkState hop runs under the Lifeline, which is
// retired before mLink.reset(), so it never sees mLink gone.
//
// MIDI in (listening design D1, D2, D4, D5): (VST3 and the APP; the Audio Unit's path is described at the end
// of this note) HandleMidiIn runs on the audio thread once per incoming event (ProcessMidiMsg calls it),
// before the block's ProcessBlock (the VST3 wrapper delivers MIDI while bypassed too). It never allocates,
// locks or calls the base (iPlug2's default forwards every message to the output, unmanaged). It normalises
// the event (chordlab::heardEventOf: note-on, note-off, CC64; channels merged), stamps it with mSampleClock
// (acquire) plus its offset and pushes it into mHeardRing, a fixed SPSC ring the main thread drains (the
// ring also carries the in-band HeardEvent::Reset of a host reset; see "Reset ordering" below). The
// clock watermark: ProcessBlock advances mSampleClock by nFrames with a release store at the END of every
// block, so once a block is done every stamp of it is at or before the published clock; OnIdle reads the
// clock before draining, feeds the events (a Reset among them clears it) to mListener and calls advance(now) at most every kHeardInterval
// (the listener's time is monotonic, so a stamp after `now` cannot run it backwards). Note-ons and offs also
// go to Scheduler::pushThru, always: the Thru flag (BlockInfo::thru) is applied inside process from the slot
// acquired for that very block, not from the previous block's slot, which is all HandleMidiIn could read.
// Bypass: while GetBypassed() the wrapper runs PassThroughBuffers instead of ProcessBlock, so HandleMidiIn
// queues nothing for thru, still feeds the listener and sets mBypassSeen (VST3/APP; under AU a bypassed
// message is never queued). ApplyPendingBypassRelease consumes
// the flag after the reset (at the first non-bypassed note of a callback, before its pushThru, or the top of
// ProcessBlock) and calls Scheduler::releaseThru, which releases every thru note (an off swallowed by the
// bypass cannot leave one sounding) on the next block with frames and keeps the queue: a zero-frame block (a
// VST3 parameter flush) leaves the release pending, and the notes arriving after the bypass still pass
// through, after the release-all. The sample clock stands still while bypassed (events then are simultaneous
// to the listener). Overflow: a full heard ring refuses the event and sets a sticky flag. OnIdle takes the
// flag BEFORE draining: when set, it pops and discards everything in the ring, counts the overflow
// (mDroppedHeard, reported with the incomplete blocks in kMsgTagStatus) and clears the listener, which
// resynchronises by forgetting (a lost note-off or pedal-up can never leave a note held); otherwise it drains
// and feeds. The order matters: drain-then-take would let a burst that overflows between the two leave the
// pre-refusal events in the ring to be fed after the clear, and a note-on among them whose note-off was the
// refused event would stay held. The ring stores the flag with release after the events before the refusal
// and takeOverflow exchanges it with acquire, so the discarding pops see them all. A refused pushThru sets
// mBlockRefused; the scheduler releases every thru note and discards its ring on the next non-empty block,
// which counts once as incomplete (a zero-frame block leaves both flags for it). Reset ordering: OnReset only
// flags the reset, Scheduler::reset discards the queued incoming notes, and iPlug2 delivers a callback's MIDI
// before its ProcessBlock, so the flag is consumed by whichever comes first on the audio thread, the
// callback's first incoming message in HandleMidiIn (bypassed or not, before its heard push and its
// pushThru) or the top of ProcessBlock (ApplyPendingReset): the notes arriving in the first block after a
// reactivation are queued after the reset, not discarded by it. The listener lives on the main thread and
// cannot see the flag, so ApplyPendingReset also pushes a HeardEvent::Reset into mHeardRing right after
// Scheduler::reset, from the ring's one producer: the listener clears exactly between the events heard
// before the reset and those after it (a key held across a deactivation never sends its note-off, and would
// otherwise stay held). If the ring is full the push is refused and the sticky overflow clears the listener.
// AUv2's wrapper calls ProcessMidiMsg on the host's MIDIEvent thread (IPlugAU.cpp DoMIDIEvent), so under
// AU_API it only queues the raw bytes (mRawMidiIn, an SPSC ring, tagged with mInputEpoch) and ProcessBlock
// drains them first (DrainRawMidiIn, chordlab::drainRawMidi: bounded) on the render thread, where pushThru
// and the scheduler belong, handing each to HandleMidiIn. Under AU the epoch is the only reset signal:
// OnReset bumps mInputEpoch and never sets mResetPending (two signals could be seen in either order, and a
// fresh note pressed at the reset could be skipped or cut by it). The drain skips the messages queued before
// the last reset and applies each reset (ApplyReset: Scheduler::reset and the heard Reset) in FIFO order,
// right before the first message queued after it, even when the bump lands mid-drain; a reset with no MIDI
// after it lands at the end of the next block's drain, at the top of ProcessBlock (mAppliedEpoch records the
// last epoch applied). The ring's single-producer contract assumes the host delivers MIDIEvent from one
// thread at a time, as auval does and AU hosts are expected to (Logic unverified until Test 12); a host
// delivering from two threads concurrently would race push. iPlug2's AU render also feeds editor-sent MIDI to
// ProcessMidiMsg on the render thread (IPlugAU.cpp:1746-1753); ChordLab never calls SendMidiMsgFromUI, so the
// ring keeps its single producer: do not start.
//
// The Cocoa view controller: ARC is off for this file. OpenWindow's alloc/init returns the controller
// at +1 and CocoaEditorDelegate::CloseWindow only forgets the pointer, so CloseWindow here releases it.

#include "ChordLab.h"
#include "IPlug_include_in_plug_src.h"

// Pure C++ helpers for message validation and export naming, unit-tested by ChordLab/bridge/tests (bridge_tests).
#include "bridge/command_lineage.h"
#include "bridge/export_naming.h"
#include "bridge/export_request.h"
#include "bridge/link_outcome.h"
#include "bridge/link_request.h"
#include "bridge/preview_request.h"

#include "chordlab/export.h"
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

  mClipboardToken = MakeUid();

  // The instance joins the registry (see "Links" at the top): the VST3 the process's shared one, every other
  // format a registry of its own that no other instance sees. A fresh id, so no follower waits for it yet. The
  // hook is what other instances run to make this one republish: it holds mLifeline, not `this` alone, and does
  // nothing once the destructor retired it; it only republishes (no editor, no host call).
  if (!kLinks)
    mPrivateRegistry = std::make_unique<chordlab::LinkRegistry>();
  chordlab::LinkRegistry& registry = kLinks ? chordlab::LinkRegistry::shared() : *mPrivateRegistry;
  // mModelMutex from before the registration to the first publish: the endpoint registers inside its constructor,
  // before mLink is assigned, and a hook that reached this instance in that window would publish through a null
  // mLink. The hook takes mModelMutex, so it waits until the instance is whole (order: model, then the registry's,
  // which LinkRegistry::add takes; add and the endpoint's constructor call no hook, they return none).
  {
    std::lock_guard<std::mutex> lock(mModelMutex);
    mLink = std::make_unique<chordlab::LinkEndpoint>(registry, MakeUid(), &ChordLab::MakeUid,
                                                     chordlab::guardedHook(mLifeline, [this] {
                                                       std::lock_guard<std::mutex> lock(mModelMutex);
                                                       PublishSnapshotLocked();
                                                     }));
    RebuildPaletteLocked();
    PublishSnapshotLocked();                                     // the empty progression, generation 1
  }

  // Last, and outside the section above: iPlug2's MakeDefaultPreset calls the virtual SerializeState, which reads
  // the instance block from mLink (so mLink must exist: called first, as it used to be, it crashed every format
  // while the host constructed the plug-in) and takes mModelMutex itself (so the constructor's lock must be released:
  // the mutex is not recursive). The default preset is the state as constructed: the empty document and this
  // instance's block. The host test (ChordLab/Tests/vst3host) constructs the plug-in and fails if this regresses.
  MakeDefaultPreset();
}

ChordLab::~ChordLab()
{
#if defined(AU_API)
  if (mEditorOpen.load())                                        // a host that destroys the plug-in before releasing
  {                                                              // the view: close the editor and disarm the container
#ifndef NDEBUG
    // NSLog rather than DBGMSG (a printf to the host's stdout), so Test 12's Console filter shows it.
    if (![NSThread isMainThread])
      NSLog(@"ChordLab: AU disposed off the main thread with the editor open");
#endif
    CloseWindow();
  }
#endif
  // Waits for a hop or another instance's republish hook in flight; later ones do nothing. From here on no other
  // instance enters this one.
  chordlab::retire(*mLifeline);
  // Leaves the registry: this instance's followers are emptied and wait for its id, its link buffer is never
  // written again, and the hook of the leader it followed (if bound) runs here, on this thread, with no lock of
  // this instance held. mPrivateRegistry, if any, goes after it, with the members.
  mLink.reset();
  // This instance's exports that are old enough to be certainly unused; younger ones stay for the next
  // purge or the OS (see the cleanup note above). May run off the main thread (AUv3), hence the pool.
  @autoreleasepool
  {
    for (const std::string& path : mExportPaths)
      RemoveIfStale([NSString stringWithUTF8String:path.c_str()]);
  }
}

#if defined(AU_API)
// The AUv2 view factory (IPlugAU_view_factory.mm) returns OpenWindow's view to the host and never calls
// CloseWindow; the host takes the view out of its window and releases it. This container, returned in place
// of the controller's view (which it holds as its one subview, autoresized with it), turns either event into
// CloseWindow. A host that took the view out and put the same view back would get an empty container: the
// editor closed on the removal.
@interface ChordLabAUHostView : NSView
@property (nonatomic, assign) ChordLab* plug;   // cleared by CloseWindow (also when ~ChordLab closes the editor)
@end

@implementation ChordLabAUHostView
- (void) viewDidMoveToWindow
{
  [super viewDidMoveToWindow];
  if (self.window == nil && self.plug)                           // taken out of the host's window: the editor is closing
    self.plug->CloseWindow();                                    // clears self.plug and takes the controller's view out
}
- (void) dealloc
{
  if (ChordLab* plug = self.plug)                                // released without ever having been in a window
  {
    self.plug = nullptr;                                         // tells CloseWindow we are going (see there)
    plug->CloseWindow();
  }
  [super dealloc];
}
@end
#endif

void* ChordLab::OpenWindow(void* pParent)
{
#if defined(AU_API)
  if (mViewController)                                           // the host asked for a new view before releasing the old one
    CloseWindow();                                               // (Logic's Editor/Controls toggle): disarm and empty the old container first
#endif
  PLATFORM_VIEW* platformParent = (PLATFORM_VIEW*) pParent;
  auto* vc = [[ChordLabViewController alloc] initWithEditorDelegateAndBundleID: this : GetBundleID()];
  mViewController = vc;
  int width = PLUG_WIDTH;
  int height = PLUG_HEIGHT;
  std::string token;
  {
    std::lock_guard<std::mutex> lock(mModelMutex);
    width = GetEditorWidth();      // PLUG_WIDTH x PLUG_HEIGHT until a host resize or a state load changes it
    height = GetEditorHeight();
    token = mClipboardToken;
  }
  // Before anything touches vc.view: the view's load creates the editor state, which reads the token (D21).
  vc.clipboardToken = [NSString stringWithUTF8String: token.c_str()];   // autoreleased; the property copies it
  vc.view.frame = MAKERECT(0.f, 0.f, (float) width, (float) height);
  [platformParent addSubview:vc.view];
  
  mEditorOpen.store(true);
  OnUIOpen();
  
#if defined(AU_API)
  if (pParent == nullptr)                                        // the AUv2 view factory; VST3 and the APP pass a parent
  {
    ChordLabAUHostView* host = [[[ChordLabAUHostView alloc] initWithFrame: vc.view.frame] autorelease];
    host.autoresizesSubviews = YES;
    vc.view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [host addSubview: vc.view];
    host.plug = this;
    mAUHostView = host;                                          // weak: the host owns it
    return host;
  }
#endif
  return vc.view;
}

void ChordLab::CloseWindow()
{
  // Main thread. Idempotent: VST3 and the APP call it from their close callbacks; AUv2's view factory has
  // none in iPlug2, so there the container OpenWindow returned (ChordLabAUHostView) calls it when the host
  // takes the view out of its window or releases it, and ~ChordLab calls it for an editor still open.
  if (!mViewController)
    return;
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
  mEditorOpen.store(false);                                      // OnIdle and the pushes stop here
#ifndef NDEBUG
  NSLog(@"ChordLab: editor closed");                             // Test 12 counts these in Console
#endif
#if defined(AU_API)
  if (mAUHostView)
  {
    auto* host = (ChordLabAUHostView*) mAUHostView;
    mAUHostView = nullptr;
    // The container belongs to the host and outlives this call. Only one container is armed at a time
    // (OpenWindow closes the previous editor first), so its plug is null here only when the call comes from
    // its own dealloc, where [super dealloc] releases the subview. Otherwise disarm it and take the
    // controller's view out of it, so the view goes with the controller below: NSViewController puts itself
    // in its view's responder chain, and a view left in a container the host might show again would forward
    // events to the freed controller.
    if (host.plug)
    {
      host.plug = nullptr;
      [((NSViewController*) controller).view removeFromSuperview];
    }
  }
#endif
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
#if defined(AU_API)
  // A host resize sizes the container and the autoresizing mask follows; a plug-in-initiated one (iPlug2's
  // AUv2 has no caller today) must size the container too, keeping the origin the host placed it at. First,
  // so the frame set below is the subview's final one rather than autoresized on top of it.
  if (mAUHostView)
    [(ChordLabAUHostView*) mAUHostView setFrameSize: NSMakeSize((CGFloat) width, (CGFloat) height)];
#endif
  auto* vc = (ChordLabViewController*) mViewController;
  vc.view.frame = MAKERECT(0.f, 0.f, (float) width, (float) height);
}

bool ChordLab::OnKeyDown(const IKeyPress& key)
{
  // Main thread: the host calls IPlugView::onKeyDown on the UI thread, between OpenWindow and
  // CloseWindow, so mViewController is valid or null. iPlug2's IKeyPress: utf8 text, a Windows virtual
  // key (kVK_BACK 0x08, kVK_DELETE 0x2E, digits 0x30..0x39, arrows kVK_LEFT 0x25 / kVK_UP 0x26 /
  // kVK_RIGHT 0x27 / kVK_DOWN 0x28, normally with empty text; the VK decides), S = shift, C = command
  // on macOS (IPlugVST3_View.h TranslateKeyMessage reads kCommandKey into C), A = alt; Control is not
  // carried.
  // Every key goes to the router unfiltered; it reads arrows from the VK and consumes a key only when
  // the map has a command for it with the current key target (arrows: only with notes targeted).
  // Live 12 never calls this route (it delivers keys to the plug-in window, route A; tracks design S0);
  // other hosts may.
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
  // May run off the audio thread; the scheduler's reset is marshalled to the audio thread.
  mResetSampleRate.store(GetSampleRate(), std::memory_order_relaxed);
#if defined(AU_API)
  // One signal, applied in FIFO order by the drain (see "MIDI in"): raw input queued before this bump is
  // forgotten, and the reset lands before the first message queued after it. No mResetPending here.
  // The wrapper calls OnReset right after SetBypassed on both bypass edges, on the same thread, so this read
  // is not a race; the release bump below publishes the snapshot to ProcessMidiMsg.
  mAUBypassed.store(GetBypassed(), std::memory_order_relaxed);
  mInputEpoch.fetch_add(1, std::memory_order_release);
#else
  mResetPending.store(true, std::memory_order_release);
#endif
}

void ChordLab::ApplyPendingReset() noexcept
{
  if (!mResetPending.exchange(false, std::memory_order_acq_rel))
    return;
  ApplyReset();
}

void ChordLab::ApplyReset() noexcept
{
  mScheduler.reset(mResetSampleRate.load(std::memory_order_relaxed));
  // In-band, from the ring's one producer: the listener forgets exactly between the events heard before the
  // reset and after it (keys held across a deactivation never send their note-offs). A refused push means
  // the ring is full; its sticky overflow flag makes OnIdle discard everything and clear the listener anyway.
  mHeardRing.push({chordlab::HeardEvent::Reset, 0, 0, mSampleClock.load(std::memory_order_relaxed)});
}

void ChordLab::ApplyPendingBypassRelease() noexcept
{
  if (mBypassSeen.exchange(false, std::memory_order_acq_rel))
    mScheduler.releaseThru();                                    // pending until a block with frames; the queue stays
}

// Limits of the wrappers (outside the engine's real-time proof, which covers the scheduler and the rings):
//  - The standalone APP pops its MIDI input in the audio callback and calls this with offset 0 for every
//    message (IPlug/APP/IPlugAPP.cpp:138), and its SendMidiMsg allocates a std::vector per message and
//    ignores the sample offset (IPlugAPP.cpp:79): the APP's thru output is block-quantised and not
//    allocation-free. The APP delivers CC64 in order with the notes.
//  - VST3 delivers CC64 as a parameter: the wrapper processes parameter changes before the block's note
//    events and keeps only the last point of a parameter's queue per block (IPlugVST3_ProcessorBase.cpp:291,
//    :334), so a pedal transition is ordered before that block's notes, stamped at the parameter point's
//    offset: the earlier notes of that block are taken as simultaneous with it (less than a block of
//    shift), and several transitions within one block collapse to the last (design D5; the user guide's
//    limits). The pedal arrives only on MIDI channel 1: VST3_NUM_CC_CHANS defaults to 1
//    (IPlugVST3_Defs.h:12), and the host drops the other channels' CCs.
//  - AUv2 calls ProcessMidiMsg on the host's MIDIEvent thread (IPlugAU.cpp DoMIDIEvent), not the render
//    thread, so under AU_API it only pushes the raw message into mRawMidiIn and DrainRawMidiIn runs the
//    normal path at the top of ProcessBlock (offsets are relative to the next render, so they are block
//    offsets already (a host delivering concurrently with a render may land an event one block early)).
//    OnReset only bumps mInputEpoch (no mResetPending under AU): the drain applies each reset in FIFO order
//    with the queued messages, and a reset with no following MIDI lands at the top of the next block. The
//    wrapper runs OnReset on both edges of a bypass (kAudioUnitProperty_BypassEffect) and does not call
//    ProcessBlock while bypassed (PassThroughBuffers), so a bypass forgets: nothing is queued or drained
//    meanwhile and the display holds its last state; the reset waits in the epoch and lands on the first block
//    after the bypass ends (Heard clears, the progression chases back in; the user guide's Logic limits).
//  - A bypass end releases the thru notes through Scheduler::releaseThru, on the next block with frames,
//    and keeps the queued notes (see "MIDI in").
void ChordLab::ProcessMidiMsg(const IMidiMsg& msg)
{
#if defined(AU_API)
  // The host's MIDIEvent thread: only the ring (its one producer). A refusal is sticky; the drain resynchronises.

  // The bypass is read from mAUBypassed, the snapshot OnReset publishes with the epoch, never from iPlug2's
  // plain bool (written on the property thread). The epoch is loaded first, with acquire, so the snapshot
  // read after it is at least the one stored before that epoch's bump. A message that races a bypass edge is
  // either tagged with the pre-reset epoch and skipped by the drain, or handled and then released by the
  // reset at the bypass end; just after a bypass end it at most drops a fresh message.
  const uint32_t epoch = mInputEpoch.load(std::memory_order_acquire);
  if (mAUBypassed.load(std::memory_order_relaxed))               // a bypass is a reset in the AU wrapper: nothing to keep
    return;
  mRawMidiIn.push(chordlab::RawMidi{msg.mStatus, msg.mData1, msg.mData2, epoch, msg.mOffset});
#else
  HandleMidiIn(msg);
#endif
}

// Under AU_API the ApplyPendingReset() below is a no-op: OnReset never sets the flag there, and the drain
// applies resets by epoch before handing a message here.
void ChordLab::HandleMidiIn(const IMidiMsg& msg) noexcept
{
  // Audio thread: no allocation, no locks, no logging, never the base class (see "MIDI in" at the top).
  ApplyPendingReset();                                           // first: its heard Reset precedes this event, and reset() empties the thru queue
  const uint64_t stamp = mSampleClock.load(std::memory_order_acquire) + (uint64_t) std::max(0, msg.mOffset);
  if (const std::optional<chordlab::HeardEvent> heard = chordlab::heardEventOf(msg.mStatus, msg.mData1, msg.mData2, stamp))
    mHeardRing.push(*heard);                                     // a refusal is sticky in the ring; OnIdle resynchronises
  if (GetBypassed())
  {
    mBypassSeen.store(true, std::memory_order_relaxed);         // no thru while bypassed; the next block releases it
    return;
  }
  if (const std::optional<chordlab::ThruEvent> thru = chordlab::thruEventOf(msg.mStatus, msg.mData1, msg.mData2, msg.mOffset))
  {
    ApplyPendingBypassRelease();                                 // after the reset above, which would forget it
    if (!mScheduler.pushThru(*thru))                             // queued whatever the flag: the block's own slot decides
      mBlockRefused.store(true, std::memory_order_relaxed);
  }
}

#if defined(AU_API)
void ChordLab::DrainRawMidiIn() noexcept
{
  // The policy (bounded pops, the flag before the drain, stale epochs skipped, resets applied in FIFO order
  // by epoch) is the engine's, tested there.
  const uint32_t before = mAppliedEpoch;
  const uint32_t current = mInputEpoch.load(std::memory_order_acquire);
  const chordlab::RawDrain drained = chordlab::drainRawMidi(
      mRawMidiIn, mAppliedEpoch, current, [this] { ApplyReset(); },
      [this](const chordlab::RawMidi& raw) {
        IMidiMsg msg;
        msg.mStatus = raw.status;
        msg.mData1 = raw.data1;
        msg.mData2 = raw.data2;
        msg.mOffset = raw.sampleOffset;
        HandleMidiIn(msg);
      });
  if (drained == chordlab::RawDrain::Forgot)
  {
    mDroppedRaw.fetch_add(1, std::memory_order_relaxed);
    if (chordlab::newerEpoch(current, before))
      ApplyReset();                                              // a host reset was queued behind the lost messages: the full forget
    else
      mHeardRing.push({chordlab::HeardEvent::Reset, 0, 0, mSampleClock.load(std::memory_order_relaxed)});   // the listener forgets
    mScheduler.dropThru();                                       // the queued thru notes are stale too; a lost note-off never sticks a note
  }
}
#endif

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

#if defined(AU_API)
  DrainRawMidiIn();                                              // the host's MIDIEvent thread only queued (see "MIDI in")
#endif
  ApplyPendingReset();                                           // unless this callback's MIDI already ran it
  ApplyPendingBypassRelease();                                   // likewise
  // panic() before process(): panic records the preview ring's tail as it sees it, and the drain inside
  // process discards every command queued before that tail (Scheduler::panic), so a Start the UI pushed
  // before pressing Panic never sounds, while commands pushed after the panic are served as usual.
  if (mPanicPending.exchange(false, std::memory_order_acq_rel))
    mScheduler.panic();
  // The newest published progression of the buffer this instance plays: its own (mSnapshots) or, while it
  // follows, its link buffer (a leader's lane, written by the registry), adopted when it delivered a fresh slot
  // or when the source changed (see "Links" at the top). The pointer stays valid and unchanged until the next
  // acquire of that buffer, after which the scheduler no longer reads it (Scheduler::adopt); a changed
  // generation makes the scheduler release and re-chase. following() is an atomic and linkBuffer() a plain
  // reference: the only things of the endpoint this thread touches.
  //
  // Follow Live gates the progression only: previews are independent of the transport, and the scheduler
  // releases the progression on the playing -> stopped edge, so turning it off mid-chord is clean. The
  // flag comes from the slot the scheduler is playing (the default before the first publication), never from
  // somewhere the progression could outrun. Thru always comes from the own buffer's slot (a follower keeps its
  // own Thru switch); a bypass end is a releaseThru above, not a block with thru off (see "MIDI in" at the top).
  const chordlab::LinkSource::Flags flags =
    mLinkSource.select(mSnapshots, mLink->linkBuffer(), mLink->following(), mScheduler);
  const double ppq = GetPPQPos();
  const double bpm = GetTempo();
  const bool playing = GetTransportIsRunning() && flags.followHost;
  const chordlab::BlockInfo block{ppq, nFrames, GetSampleRate(), bpm, playing, flags.thru};
  mTransportPpq.store(ppq, std::memory_order_relaxed);             // the playhead, read by OnIdle
  mTransportPlaying.store(playing, std::memory_order_relaxed);
  mHostBpm.store(bpm, std::memory_order_relaxed);                  // the export's tempo
  std::size_t written = 0;
  const bool complete = mScheduler.process(block, mMidiOut.data(), mMidiOut.size(), written);

  // Output capacity (plan M3.3, listening design D16): iPlug2's VST3 output event queue holds
  // maxSamplesPerBlock rounded up to a 4 KiB page of 8-byte IMidiMsg, at least 512 entries (IPlugMidi.h's
  // DEFAULT_BLOCK_SIZE macro, 512, shadows IPlugConstants.h's 1024 there; setupProcessing resizes it), so a
  // burst of at most kMidiOutCapacity (512) events fits without a realloc. Thru notes are part of that
  // burst: ProcessMidiMsg replaces iPlug2's default, which forwarded incoming MIDI into the same queue
  // outside this budget. The queue still retains its events across blocks while the host passes no
  // pOutputEvents, and the standalone APP's SendMidiMsg allocates a std::vector per message
  // (IPlug/APP/IPlugAPP.cpp:79; see the limits above ProcessMidiMsg).
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

  // The watermark: every event stamped during this callback is now at or before the published clock.
  mSampleClock.fetch_add((uint64_t) std::max(0, nFrames), std::memory_order_release);

  // One count per block: a refused thru push also makes process() report the block (it releases the thru
  // notes and discards the ring), so the two verdicts fold into one increment. A zero-frame block is a
  // no-op in process() (its only early-out) and leaves the refusal for the next block, which handles it.
  const bool refused = nFrames != 0 && mBlockRefused.exchange(false, std::memory_order_relaxed);
  if (!complete || refused)
    mIncompleteBlocks.fetch_add(1, std::memory_order_relaxed);   // no logging on the audio thread; OnIdle reports it
}

void ChordLab::OnUIOpen()
{
  Plugin::OnUIOpen();
  PushSnapshot();
  PushHeard();                                                   // replays an active listener; an inactive one hides the group
  mReportedIncompleteBlocks = 0;                                 // make OnIdle resend the current counts
  mReportedDroppedHeard = 0;
  mPushedPpq = std::numeric_limits<double>::quiet_NaN();         // ...and the playhead
  mPushedBpm = std::numeric_limits<double>::quiet_NaN();         // ...and the tempo
  // ...and the link state, after the snapshot: the serial first, so the state built inline below is at least that
  // recent and OnIdle pushes again only when something moved after it.
  mReportedLinkSerial = LinkSerial();
  PushLinkState();
}

void ChordLab::OnIdle()
{
  // Main thread, with or without an editor (iPlug2 drives it from IPlugAPIBase::OnTimer). What was heard:
  // the clock first, so it is a watermark for every event drained after it (see "MIDI in" at the top).
  {
    const uint64_t now = mSampleClock.load(std::memory_order_acquire);
    const double bpm = HostBpm();
    const double sampleRate = GetSampleRate();                   // the listener moves no time while it is unusable
    // The overflow flag before the drain: taken after it, a burst overflowing in between would leave the
    // events pushed before the refusal in the ring to be fed after the clear, and a note-on among them
    // whose note-off was the refused event would stay held. The flag's release store and the exchange's
    // acquire make every event pushed before the refusal visible to the discarding pops.
    chordlab::HeardEvent e{};
    if (mHeardRing.takeOverflow())
    {
      while (mHeardRing.pop(e))
        ;                                                        // the lost epoch, discarded whole
      ++mDroppedHeard;                                           // resynchronise by forgetting
      mListener.clear();
    }
    else
      while (mHeardRing.pop(e))
        mListener.feed(e, bpm, sampleRate);                      // a host reset's in-band Reset clears it in order
    const auto tick = std::chrono::steady_clock::now();
    if (tick - mLastHeardTick >= kHeardInterval)
    {
      mLastHeardTick = tick;
      mListener.advance(now, bpm, sampleRate);                   // the only key evaluation
      if (mListener.changed() && mEditorOpen.load())
        PushHeard();                                             // acknowledges; a closed editor keeps `changed` for OnUIOpen
    }
  }

#if defined(AU_API)
  mDroppedHeard += mDroppedRaw.exchange(0, std::memory_order_relaxed);   // AU input overflows read as forgotten epochs too
#endif
  const uint32_t incomplete = mIncompleteBlocks.load(std::memory_order_relaxed);
  if ((incomplete != mReportedIncompleteBlocks || mDroppedHeard != mReportedDroppedHeard) && mEditorOpen.load())
  {
    mReportedIncompleteBlocks = incomplete;
    mReportedDroppedHeard = mDroppedHeard;
    PushStatus(incomplete, mDroppedHeard);
  }

  // The playhead and the tempo: at most every kTransportPushInterval, only to an open editor, only when the
  // position moved more than a rounding error, the transport started or stopped, or the tempo moved by 0.01 bpm
  // or more (so a tempo change while stopped reaches Record). mPushedPpq and mPushedBpm are NaN before the first
  // push (or after an editor opened), and a NaN comparison is false, so that first push always happens.
  if (mEditorOpen.load())
  {
    const auto now = std::chrono::steady_clock::now();
    if (now - mLastTransportPush >= kTransportPushInterval)
    {
      double ppq = mTransportPpq.load(std::memory_order_relaxed);
      bool playing = mTransportPlaying.load(std::memory_order_relaxed);
      const double bpm = HostBpm();                              // finite and positive: 120 before the first block
      if (!std::isfinite(ppq))
      {
        ppq = 0.0;                                               // nlohmann would write null for NaN or inf...
        playing = false;                                         // ...and an unusable position hides the playhead
      }
      if (!(std::fabs(ppq - mPushedPpq) <= 1e-6) || playing != mPushedPlaying || !(std::fabs(bpm - mPushedBpm) < 0.01))
      {
        mLastTransportPush = now;
        mPushedPpq = ppq;
        mPushedPlaying = playing;
        mPushedBpm = bpm;
        PushTransport(ppq, playing, bpm);
      }
    }
  }

  // The link state, to an open editor of an instance that links (the VST3), whenever the shared registry's serial
  // moved (a link, a load, an instance added or removed anywhere in the process, a leader's publish). Never mLink:
  // the serial comes from the registry that is never destroyed (LinkSerial), so this reads nothing a destructor on
  // another thread could take away; the push below relies on the VST3's destructor running on this, the main, thread
  // (see "Links" at the top). An AU or app instance pushes its link state on OnUIOpen only.
  if (kLinks && mEditorOpen.load())
  {
    const uint64_t serial = LinkSerial();
    if (serial != mReportedLinkSerial)
    {
      mReportedLinkSerial = serial;                              // before the build: a later move is pushed next time
      PushLinkState();
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
    bool documentChanged = false;
    // The document the editor computed this command for (tracks plan Task 24a). Read before the lock (a large command's
    // parse must not hold mModelMutex against a host's Serialize/Unserialize); compared under it, in the same section
    // that would apply the command, so a state load cannot slip in between. Never throws (readLineage catches).
    const chordlab::bridge::LineageField field = chordlab::bridge::readLineage(json);
    chordlab::bridge::CommandLineage lineage = chordlab::bridge::CommandLineage::Absent;
    {
      std::lock_guard<std::mutex> lock(mModelMutex);
      lineage = chordlab::bridge::compareLineage(field, mClipboardToken);
      if (chordlab::bridge::lineageApplies(lineage))             // absent or current; stale, malformed, failed: nothing
        accepted = chordlab::applyCommandJson(mStore, json, error, &documentChanged);
      // Always rebuild: besides setKey/browseKey, undo/redo can change the project key, and the
      // palette is cheap (35 cells). Likewise always recompile: PublishSnapshotLocked publishes only
      // when the playback content actually changed.
      if (accepted)
      {
        RebuildPaletteLocked();
        PublishSnapshotLocked();
      }
    }
    if (lineage == chordlab::bridge::CommandLineage::Stale)
    {
      // Computed for a document a state load has since replaced (an AU host may load off the main thread while the
      // editor still shows the old one). The editor gets the current token and snapshot now, inline on this main
      // thread, ahead of the load's queued push. Not an error: a click that raced a load is not the user's mistake.
      DBGMSG("ChordLab: command of a replaced document not applied (stale lineage)\n");
      PushSnapshot();
      return true;
    }
    if (lineage == chordlab::bridge::CommandLineage::Malformed)
      error = chordlab::bridge::kMalformedLineageError;
    else if (lineage == chordlab::bridge::CommandLineage::Failed)
      error = chordlab::bridge::kLineageFailedError;
    if (accepted)
    {
      if (documentChanged)                                       // not for a selection or an accepted no-op
        MarkHostDirty();                                         // outside the lock: it calls into the host
      PushSnapshot();
      if (mListener.active())
        PushHeard();                                             // names, symbol and numeral follow the project key
    }
    else
    {
      DBGMSG("ChordLab: command rejected: %s\n", error.c_str());
      PushError(error);
    }
    return true;
  }

  if (msgTag == kMsgTagClearHeard)
  {
    // Everything still in the ring arrived before the press: discard it (overflow flag first, as in OnIdle),
    // or the next idle would feed it and bring the cleared chord back.
    mHeardRing.takeOverflow();
    for (chordlab::HeardEvent e{}; mHeardRing.pop(e);)
      ;
    mListener.clear();                                           // inactive again: the push hides the group
    PushHeard();                                                 // acknowledges
    return true;
  }

  if (msgTag == kMsgTagRequestSnapshot)
  {
    PushSnapshot();
    PushHeard();                                                 // as OnUIOpen: replays, or hides the group
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

  if (msgTag == kMsgTagExportClip)
  {
    HandleExportClip((pData && dataSize > 0) ? std::string_view((const char*) pData, (size_t) dataSize) : std::string_view());
    return true;
  }

  if (msgTag == kMsgTagLink)
  {
    HandleLink((pData && dataSize > 0) ? std::string_view((const char*) pData, (size_t) dataSize) : std::string_view());
    return true;
  }

  return CocoaEditorDelegate::OnMessage(msgTag, ctrlTag, dataSize, pData);
}

void ChordLab::HandleLink(std::string_view json)
{
  // Main thread, inline from OnMessage only: never inside an OnMain hop, so no Lifeline lock is held here and the
  // hooks below may run (they enter other instances through their Lifelines). Mirrors the engine simulation's
  // FakeInstance::follow / unfollow: the step under mModelMutex, the hooks with nothing held, the own republish.
  std::string problem;
  const std::optional<chordlab::bridge::LinkRequest> request = chordlab::bridge::parseLinkRequest(json, &problem);
  if (!request)                                                  // it also refuses texts beyond the message limits, so
  {                                                              // the lineage below is never left unread
    DBGMSG("ChordLab: link message rejected: %s\n", problem.c_str());
    PushError(problem);
    return;
  }
  // The follower's own document lineage (plan I9), exactly as for a command: read before the lock, compared under it
  // in the section that would apply the request, so a state load cannot slip in between. The leader's lineage
  // (request->leaderLineage) is the registry's to check, inside LinkEndpoint::follow.
  const chordlab::bridge::LineageField field = chordlab::bridge::readLineage(json);
  chordlab::bridge::CommandLineage lineage = chordlab::bridge::CommandLineage::Absent;
  chordlab::LinkFollowResult result = chordlab::LinkFollowResult::Applied;   // an unfollow has none
  bool savedFollowChanged = false;
  chordlab::LinkRegistry::Hooks hooks;
  {
    std::lock_guard<std::mutex> lock(mModelMutex);
    lineage = chordlab::bridge::compareLineage(field, mClipboardToken);
    if (chordlab::bridge::linkApplies(lineage, kLinks))       // never for an instance that does not link (I15)
    {
      // What SerializeState would save of the follow, before and after: the host is dirtied only when it changed
      // (an applied follow of the very link already saved, hints included, changes nothing).
      const std::optional<chordlab::LinkFollow> before = mLink->state().follow;
      if (request->follow)
        result = mLink->follow(request->leaderId, request->laneUid, request->leaderLineage, hooks);
      else
        mLink->unfollow(hooks);
      savedFollowChanged = mLink->state().follow != before;
    }
  }
  const chordlab::bridge::LinkOutcome outcome =
    chordlab::bridge::linkOutcome(kLinks, lineage, request->follow, result, savedFollowChanged);
  if (chordlab::bridge::linkApplies(lineage, kLinks))
  {
    RunHooks(hooks);                                             // holding nothing: the old and the new leader republish
    std::lock_guard<std::mutex> lock(mModelMutex);
    PublishSnapshotLocked();                                     // then this instance: its role may have changed
  }
  if (outcome.markDirty)
    MarkHostDirty();                                             // outside every lock: it calls into the host
  if (outcome.pushSnapshot)
  {
    DBGMSG("ChordLab: link message of a replaced document not applied (stale lineage)\n");
    PushSnapshot();                                              // the current token and snapshot, inline, first
  }
  if (outcome.error)
    PushError(outcome.error);
  PushLinkState();                                               // always: for a refused follow it is the answer
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
#ifndef NDEBUG
  // NSLog rather than DBGMSG (a printf to the host's stdout): the Live test reads these in Console.app.
  NSLog(@"ChordLab: preview %s id %d",
        cmd.kind == chordlab::PreviewCmd::Start ? "start" : cmd.kind == chordlab::PreviewCmd::Stop ? "stop" : "extend",
        cmd.id);
#endif
  if (!mScheduler.pushPreview(cmd))
  {
    // Extends for one id merge in the ring, so they cannot fill it on their own. The watchdog (4 s of
    // processed audio after the Start or the last applied Extend) still ends what sounds.
    PushError("preview queue full");
    return;
  }
  switch (cmd.kind)
  {
    // Only the LAST Start is remembered: a roll one-shot started over a held preview replaces it here, so
    // CloseWindow's fallback Stop can miss the held one. Accepted: the UI's onDisappear stops the held preview
    // itself, and the scheduler's 4 s watchdog ends anything whose Stop never arrives.
    case chordlab::PreviewCmd::Start:  mOpenPreviewId = cmd.id; break;
    case chordlab::PreviewCmd::Stop:   if (cmd.id == mOpenPreviewId) mOpenPreviewId = -1; break;
    case chordlab::PreviewCmd::Extend: break;                    // the open Start stays open
  }
}

// The host's tempo as ProcessBlock last saw it, 120 before the first block: the export's and the listener's.
double ChordLab::HostBpm() const
{
  const double bpm = mHostBpm.load(std::memory_order_relaxed);
  return (std::isfinite(bpm) && bpm > 0.0) ? bpm : 120.0;
}

void ChordLab::HandleExport()
{
  // Main thread, never real-time: compile and the SMF writer allocate. The export is exactly the playback
  // content, so a dropped clip is as long as the progression, trailing silence included (D10). Live names
  // the dropped clip after the file's track name, which exportMidi sets from the chords or the section
  // (chordlab::exportClipName); the file name carries the key (WriteExport).
  @autoreleasepool
  {
  const double bpm = HostBpm();
  std::vector<uint8_t> bytes;
  std::string label;
  {
    // The document and its meter are read together under the lock, so the clip's time signature is the one it was compiled with.
    std::lock_guard<std::mutex> lock(mModelMutex);
    bytes = chordlab::exportMidi(mStore.doc(), -1, bpm);
    label = chordlab::keyName(mStore.doc().projectKey);
  }
  WriteExport(bytes, label);
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
  const double bpm = HostBpm();
  std::vector<uint8_t> bytes;
  std::string label;
  bool found = false;
  {
    std::lock_guard<std::mutex> lock(mModelMutex);
    const int index = mStore.findSectionIndex(id);
    if (index >= 0)
    {
      const chordlab::Document& doc = mStore.doc();
      bytes = chordlab::exportMidi(doc, index, bpm);
      label = chordlab::keyName(doc.projectKey) + "-" + doc.sections[(size_t) index].name;
      found = true;
    }
  }
  if (!found)
  {
    PushError("no such section");                                // outside the lock, like every call into the UI
    return;
  }
  WriteExport(bytes, label);
  }
}

void ChordLab::HandleExportClip(std::string_view text)
{
  // Main thread. {"schema":1,"lanes":...,"sectionId":...}: lanes are independent clips of their own notes
  // (tracks design D12). exportClip answers an unknown track or section with empty bytes ("nothing to
  // export"), so resolveClipExport (bridge/export_request.h, unit-tested) resolves the ids under the same lock
  // as the export, for an error that says which is wrong (the section first, then the first unknown track).
  @autoreleasepool
  {
  const std::optional<chordlab::bridge::ClipExportRequest> request = chordlab::bridge::parseClipExportRequest(text);
  if (!request)
  {
    PushError("clip export message needs schema 1, lanes and a section id or null");
    return;
  }
  // The document the request's lanes and section id were picked in (Task 24a): read before the lock, compared under it
  // before the export reads the document, as for a command (see "Ownership and threads").
  const chordlab::bridge::LineageField field = chordlab::bridge::readLineage(text);
  chordlab::bridge::CommandLineage lineage = chordlab::bridge::CommandLineage::Absent;
  const double bpm = HostBpm();
  chordlab::bridge::ResolvedClipExport resolved;
  std::vector<uint8_t> bytes;
  std::string label;
  {
    std::lock_guard<std::mutex> lock(mModelMutex);
    lineage = chordlab::bridge::compareLineage(field, mClipboardToken);
    if (chordlab::bridge::lineageApplies(lineage))
    {
      const chordlab::Document& doc = mStore.doc();
      resolved = chordlab::bridge::resolveClipExport(doc, *request);
      if (!resolved.problem)
      {
        bytes = chordlab::exportClip(doc, resolved.spec, bpm);
        label = chordlab::bridge::clipExportLabel(chordlab::keyName(doc.projectKey), resolved.laneNames, request->allLanes,
                                                  resolved.sectionName);
      }
    }
  }
  if (lineage == chordlab::bridge::CommandLineage::Stale)
  {
    // Picked in a document a state load has since replaced: no file. The current token and snapshot first (a snapshot
    // clears the editor's error line), then the error, which answers the drag's exchange so the drag offers nothing.
    DBGMSG("ChordLab: export of a replaced document not written (stale lineage)\n");
    PushSnapshot();
    PushError(chordlab::bridge::kStaleExportError);
    return;
  }
  if (!chordlab::bridge::lineageApplies(lineage))              // malformed or unreadable: nothing exported
  {
    PushError(lineage == chordlab::bridge::CommandLineage::Malformed ? chordlab::bridge::kMalformedLineageError
                                                                    : chordlab::bridge::kLineageFailedError);
    return;
  }
  if (resolved.problem)
  {
    PushError(resolved.problem);                                 // outside the lock, like every call into the UI
    return;
  }
  WriteExport(bytes, label);
  }
}

void ChordLab::WriteExport(const std::vector<uint8_t>& bytes, const std::string& label)
{
  // Main thread, never real-time. `bytes` come from chordlab::exportMidi or chordlab::exportClip (empty when there is
  // nothing to write: no events, or the writer refused).
  @autoreleasepool
  {
  if (bytes.empty())
  {
    PushError("nothing to export");                              // no events, or the writer refused (a tempo it cannot encode)
    return;
  }

  // NSTemporaryDirectory()/ChordLab/ChordLab-<label>-<yyyyMMdd-HHmmss>-<pid>-<counter>-<uuid8>.mid, the
  // label (the key; plus the section name for a section; for a clip, the lane and then the section) spelled
  // with 's' for a sharp and '-' for anything but an ASCII letter or digit so the name is safe anywhere
  // ("C-Ionian", "Fs-Dorian", "C-Ionian-Verse", "C-Ionian-Bass-Verse"); pid, counter and uuid make it unique
  // across instances and processes, and the name is checked to be unused before anything is written (see the
  // cleanup note above).
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
  // Caller holds mModelMutex: that serializes the producer side of mSnapshots across the main thread, the
  // hosts' state threads and the threads that run this instance's republish hook (the mutex hand-off orders
  // one publish before the next). Never the audio thread: it compiles, which allocates. The endpoint is the
  // own buffer's only writer and owns the order of the publications (the own buffer, the feed to the followers,
  // the own buffer again when a lane comes back; LinkEndpoint::publishOwn); fillSlot inside it decides what
  // counts as a change and whether the generation moves (content yes, followHost no), so an unchanged document
  // publishes nothing. The lineage is the document's clipboard token, which a follow by command names.
  mLink->publishOwn(mStore, mClipboardToken, mSnapshots, mPublication);
}

void ChordLab::RunHooks(const chordlab::LinkRegistry::Hooks& hooks)
{
  // The caller holds nothing: not mModelMutex, not this instance's Lifeline mutex (never inside an OnMain hop).
  // A hook takes its instance's Lifeline mutex, then that instance's mModelMutex (then the registry's, inside
  // publishOwn), and an instance never receives its own hook, so this cannot deadlock against itself or against
  // another instance doing the same.
  for (const auto& hook : hooks)
  {
    if (hook && *hook)
      (*hook)();
  }
}

std::string ChordLab::MakeUid()
{
  std::string uid;
  @autoreleasepool                                               // ARC is off, and this may run off the main thread
  {
    uid = [[[NSUUID UUID] UUIDString] UTF8String];               // 36 characters: hex digits and '-'
  }
  return uid;
}

uint64_t ChordLab::LinkSerial()
{
  // The shared registry is heap-allocated on first use and never destroyed: safe from any thread at any time, and
  // the VST3's registry is that one (mLink->serial() would read the same counter, through an instance member).
  return kLinks ? chordlab::LinkRegistry::shared().serial() : 0;
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
    std::string token;
    {
      std::lock_guard<std::mutex> lock(mModelMutex);
      // The note selection and the revision live in the store, not the document: read them in this same section.
      const chordlab::SnapshotExtras extras{mStore.noteSelection(), mStore.revision()};
      json = chordlab::snapshotJson(mStore.doc(), mPalette, mStore.selectedEventId(), mStore.selectedEventIds(),
                                    extras);  // the live selection: the anchor drives suggestions
      token = mClipboardToken;                                   // the lineage of exactly this document (D21)
    }
    // A state load renewed the token: the editor takes it before the loaded document's snapshot, so a paste of a
    // copy made before the load already pastes as foreign (track ids dropped).
    auto* vc = (ChordLabViewController*) mViewController;
    NSString* fresh = [NSString stringWithUTF8String: token.c_str()];   // autoreleased; the property copies it
    if (vc && ![vc.clipboardToken isEqualToString: fresh])
      vc.clipboardToken = fresh;
    SendArbitraryMsgFromDelegate((int) kMsgTagSnapshot, (int) json.size(), json.data());
  });
}

void ChordLab::PushHeard()
{
  // The listener is main-thread only, so everything, the build included, happens inside the hop.
  OnMain([this] {
    if (!mEditorOpen.load())                                     // `changed` stays set; OnUIOpen replays
      return;
    std::string json;
    {
      std::lock_guard<std::mutex> lock(mModelMutex);             // the document only read: the project key spells
      json = chordlab::heardJson(mListener, mStore.doc().projectKey);
    }
#ifndef NDEBUG
    NSLog(@"ChordLab: heard %s", json.c_str());                  // once per push, like the preview log
#endif
    SendArbitraryMsgFromDelegate((int) kMsgTagHeard, (int) json.size(), json.data());
    mListener.acknowledge();                                     // the editor now shows these outputs
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

void ChordLab::MarkHostDirty()
{
  // Main thread (OnMessage), the lock released. Without this Live never knew that a set had changed when
  // only ChordLab was edited: ⌘S wrote nothing, ⌘N asked nothing, and the edits were lost on reopen (Live
  // Test 14, finding 1). The window size is left out on purpose: a host-driven resize right after a state
  // load would otherwise mark a freshly opened set modified.
#if defined(VST3_API)
  // The SDK's EditController helper: IComponentHandler2::setDirty(true) when the host offers that interface,
  // kNotImplemented otherwise. Cheap and idempotent, so one call per changed command is enough.
  setDirty(true);
#endif
  // AU_API: AUv2 has no dirty call, and Logic ignores both a kAudioUnitProperty_ClassInfo property-change
  // notification and a dummy parameter (Apple developer forum thread 814070, 2026); see "Limits and
  // troubleshooting" in docs/user-guide.md. APP_API: the standalone app saves its own document.
}

void ChordLab::PushStatus(uint32_t incompleteBlocks, uint32_t droppedHeard)
{
  OnMain([this, incompleteBlocks, droppedHeard] {
    if (!mEditorOpen.load())
      return;
    const std::string json = nlohmann::json{{"schema", chordlab::kJsonSchema}, {"incompleteBlocks", incompleteBlocks},
                                            {"droppedHeard", droppedHeard}}.dump();
    SendArbitraryMsgFromDelegate((int) kMsgTagStatus, (int) json.size(), json.data());
  });
}

void ChordLab::PushTransport(double ppq, bool playing, double bpm)
{
  OnMain([this, ppq, playing, bpm] {
    if (!mEditorOpen.load())
      return;
    const std::string json =
      nlohmann::json{{"schema", chordlab::kJsonSchema}, {"ppq", ppq}, {"playing", playing}, {"bpm", bpm}}.dump();
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

void ChordLab::PushLinkState()
{
  OnMain([this] {
    if (!mEditorOpen.load())                                     // no listener: nothing to build
      return;
    std::string json;
    {
      // Order: this hop's Lifeline (when queued), mModelMutex, then the registry's mutex inside view(). mLink is
      // valid here: a queued hop runs under the Lifeline and the destructor retires it before mLink.reset(), so a
      // hop either finishes first or does nothing; the inline path runs on the main thread from OnUIOpen, OnIdle (an
      // instance that links only) and HandleLink, and the VST3's destructor runs on the main thread too (plan V14),
      // so never during it. The check is belt and braces.
      std::lock_guard<std::mutex> lock(mModelMutex);
      if (!mLink)
        return;
      json = chordlab::linkStateJson(mLink->view());
    }
    SendArbitraryMsgFromDelegate((int) kMsgTagLinkState, (int) json.size(), json.data());
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
    // The instance block (plan I11): the instance id, the document id and lane uids, and the follow with the
    // bound leader's current id, read in the same section as the document they belong to.
    const chordlab::InstanceState instance = mLink->state();
    json = chordlab::stateJson(mStore, GetEditorWidth(), GetEditorHeight(), &instance);
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
  bool loaded = false;

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
    bool editorOpen = false;
    std::string renewedToken = MakeUid();                        // made outside the lock; used only if the load succeeds
    chordlab::LinkRegistry::Hooks hooks;                         // what the link change asks of other instances
    {
      std::lock_guard<std::mutex> lock(mModelMutex);
      std::optional<chordlab::InstanceState> instance;
      loaded = chordlab::loadStateJson(mStore, json, error, &width, &height, &instance);   // atomic: a failure changes nothing
      if (loaded)
      {
        // A new document lineage (D21): its track ids are not the old document's, so a chord copy made before
        // this load must paste as foreign. PushSnapshot below hands the token to an open editor with the snapshot.
        mClipboardToken = std::move(renewedToken);
        // The link identity goes with the document, in this same section (plan I11; "Links" at the top): the
        // loaded instance block, or, for a state without one, a fresh document id, no lane uids and no follow.
        // One of the two on every successful load, never neither; a failed load or another version changes
        // nothing of the link.
        if (instance)
          mLink->adopt(chordlab::bridge::adoptableState(*instance, kLinks), hooks);   // a non-linking one drops a follow
        else
          mLink->adoptWithoutBlock(hooks);
        ConstrainEditorResize(width, height);                    // the state stores the size verbatim: a set saved under
                                                                 // an older, smaller minimum (700x480) opens at today's
        RebuildPaletteLocked();
        PublishSnapshotLocked();                                 // the audio thread plays the loaded set, editor or not
        editorOpen = mEditorOpen.load();
        if (!editorOpen)
          SetEditorSize(width, height);                          // synchronous: a save right after this load sees it
      }
    }
    if (loaded)
    {
      // Holding nothing (no mModelMutex, no Lifeline: this is never an OnMain hop), possibly off the main thread:
      // the old and the new leader republish, then this instance once more, since its role may have changed
      // through what the hooks did (a leader it now follows has written its link buffer; followers that wait for
      // its id are bound). Every buffer is right before this function returns.
      RunHooks(hooks);
      std::lock_guard<std::mutex> lock(mModelMutex);
      PublishSnapshotLocked();
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
  if (loaded)                                                    // the project key may have changed: respell what was heard
    OnMain([this] {
      if (mListener.active())                                    // the listener is read on the main thread only
        PushHeard();
    });
  return pos;
}
