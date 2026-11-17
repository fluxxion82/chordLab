#pragma once

#include "chordlab/compiler.h"
#include "chordlab/document.h"
#include "chordlab/link_state.h"
#include "chordlab/note_event.h"
#include "chordlab/publication.h"
#include "chordlab/scheduler.h"
#include "chordlab/tracks.h"
#include "chordlab/triple_buffer.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chordlab {

// Linking instances (per-track instruments plan I2, I4 to I7): a leader hands each follower the compiled notes of
// one lane, and a follower plays them from a buffer of its own. This header holds the pure, single-threaded
// pieces (how lanes are named across saves, what a leader publishes, and how a follower's slot is filled from it),
// the registry where the instances of one process meet, and the two classes an instance's shell delegates to:
// `LinkEndpoint` (its non-real-time side) and `LinkSource` (the top of its audio block). Standard library only: no
// platform code.

/// The key the chords lane has in every link.
inline constexpr std::string_view kChordsLaneUid = "chords";

/// The most entries a `LaneUids` table can hold, and so the most lanes the state's instance block accepts
/// (json_api.h): `cover` keeps exactly the entries of the tracks of the document and of its undo and redo history.
/// A document holds at most `kMaxTracks` tracks, and undo and redo together hold at most
/// `DocumentStore::kMaxUndoDepth` snapshots (`DocumentStore::historyTrackIds`), so at most
/// `kMaxTracks x (1 + kMaxUndoDepth)` distinct track ids are in reach: 4 x 201 = 804. A loaded table is held to it
/// by the loader, and the next `cover` (whose history a load has just cleared) prunes it to the document's tracks.
constexpr std::size_t kMaxInstanceLanes =
    static_cast<std::size_t>(kMaxTracks) * (1 + DocumentStore::kMaxUndoDepth);

/// One instance's table of lane uids (per-track instruments plan I2): each track number of the document it has
/// published gets a UUID once, so that a link names a lane that a reverted or replaced document cannot imitate
/// by reusing the number. A track number is `Track::id`. No entry is ever `kChordsLaneUid` or empty, and no two
/// entries share a uid (`cover` takes fresh uids from its generator, `replace` asserts it of a loaded table).
class LaneUids {
public:
    /// A fresh, never-before-returned uid (a UUID in the shell); never empty, never `kChordsLaneUid`.
    using Generator = std::function<std::string()>;

    /// Gives every track of `doc` without an entry a uid from `make`, in document order, and keeps exactly the
    /// entries of the tracks of `doc` and of `historyTrackIds` (ascending: `DocumentStore::historyTrackIds`, the
    /// tracks an undo or a redo can bring back with their number, and so with their uid); every other entry is
    /// erased, whatever its number. So the table never holds more than `kMaxInstanceLanes` entries. `kChordsLaneUid` (or an empty string) is never
    /// given to a track: `make` returning one is a programming error (asserted); without asserts such a value is
    /// skipped and `make` asked again. Nor is a uid the table already holds (an entry kept from before, or one given
    /// to an earlier track in this call): `make` is asked again, since a generator that restarted, a per-process
    /// counter against a table loaded from a set, repeats them, and two entries with one uid would make the saved
    /// state unloadable. At most 64 answers are asked for one track; after that it gets no entry in this call (a
    /// debug build asserts) and the next `cover` tries again. Returns whether the table changed (an entry added or
    /// removed).
    bool cover(const Document& doc, const std::vector<int>& historyTrackIds, const Generator& make);

    /// The uid of track `trackId`, or empty when it has none.
    std::string uidOf(int trackId) const;

    /// The lane `uid` names in `doc`: the chords lane for `kChordsLaneUid`; the track whose number has that uid
    /// when such a track exists in `doc`; nothing otherwise (an empty or unknown uid, or the uid of a track
    /// number `doc` does not hold).
    std::optional<LaneKey> laneOf(const Document& doc, std::string_view uid) const;

    /// The table as saved (track number -> uid).
    const std::map<int, std::string>& entries() const noexcept;

    /// Replaces the whole table by a loaded one: entries not in `entries` are forgotten, so a track number the
    /// loaded table lacks gets a new uid at the next `cover`, never the one it had before. The caller passes a
    /// validated table (Task 6's loader validates a loaded one): no value empty or `kChordsLaneUid`, the values
    /// pairwise distinct (asserted). Nothing is dropped or repaired here.
    void replace(std::map<int, std::string> entries);

private:
    std::map<int, std::string> entries_;
};

/// One lane as a leader publishes it.
struct LinkLane {
    std::string uid;          // kChordsLaneUid, or the track's lane uid
    bool chords = false;
    std::string name;         // "Chords" for the chords lane, else the track's name
    TrackRole role = TrackRole::Other;   // meaningless for the chords lane
    bool audible = false;     // chordsAudible / trackAudible
    bool compiled = false;    // events were compiled for it (it is followed)
    std::vector<NoteEvent> events;   // compilePlaybackFor(doc, Only{this lane}).events when `compiled`; empty otherwise
};

/// What a leader publishes for its followers: immutable once built.
struct LinkFeed {
    std::string documentId;
    std::string lineage;
    std::string label;
    double endBeat = 0.0;     // compile(doc).endBeat
    bool loop = true;         // doc.progressionLoop
    bool followHost = true;   // doc.followHost
    std::vector<LinkLane> lanes;   // the chords lane first, then the tracks in document order
};

/// Builds the feed of `doc`. `uids` must cover `doc` (every track has a uid; asserted). The lanes whose uid is in
/// `followed` are compiled (`compiled` true, their events possibly empty: an inaudible lane compiles to nothing);
/// the others carry no events. A uid in `followed` that names no lane of `doc` is ignored. Allocates: never on
/// the audio thread.
std::shared_ptr<const LinkFeed> buildLinkFeed(const Document& doc, std::string documentId, const LaneUids& uids,
                                              std::string lineage, std::string label,
                                              const std::vector<std::string>& followed);

/// Generations of a link buffer start above this, own-buffer generations (publication.h, counting up from 1) stay
/// below it: a scheduler that switches between the two buffers can never see one generation for two contents.
constexpr std::uint32_t kLinkGenerationBase = 0x80000000u;

/// Fills a follower's slot from its leader's feed, with `fillSlot`'s contract (publication.h): returns true, having
/// filled `slot` and recorded it in `last`, when anything the slot carries differs from `last` or nothing was
/// published yet; false, touching neither, otherwise. The content is the events of the lane `laneUid` when `feed`
/// is not null, `feed->documentId == documentId`, the lane is in the feed and was compiled; in every other case
/// the content is empty (count 0). `endBeat`, `loop` and `followHost` are the feed's (0, true, true without a
/// feed). `slot.midiThru` is always true and means nothing: a follower takes Thru from its own buffer. The
/// generation is `kLinkGenerationBase + n`, n counting up from 1 with every change of content (events, count,
/// endBeat, loop), not with a change of `followHost` alone. A lane with more events than a slot holds is
/// published as empty (and asserts), as `fillSlot` does. `last` must be fresh (generation 0) or have been
/// filled only by this function (asserted). "Same generation, same content" holds only while one `Publication`
/// lives exactly as long as its link buffer: a caller must never reset or recreate a link buffer's `Publication`,
/// or a later fill would restart at `kLinkGenerationBase + 1` and a scheduler could take new content for
/// unchanged. Reaching `UINT32_MAX` would take 2^31 - 1 content changes (asserted before every increment).
/// Allocates: never on the audio thread.
bool fillSlotFromFeed(const LinkFeed* feed, std::string_view laneUid, std::string_view documentId,
                      SnapshotSlot& slot, Publication& last);

// --- the registry (per-track instruments plan I4 to I7) -------------------------------------------------------

/// An instance's registration with a `LinkRegistry`. 0 is none; a registry never hands out one token twice.
using LinkToken = std::uint64_t;

// `LinkFollow` (what a follower follows) and `InstanceState` (what an instance saves) are plain data in link_state.h.

/// Where an instance stands as a follower (`LinkRegistry` rule 7). `NotFollowing` for a leader.
enum class LinkStatus { NotFollowing, Waiting, ProjectChanged, LaneMissing, Muted, Playing };

/// The outcome of `LinkRegistry::follow`: applied, or refused (and nothing changed) because no live leading
/// instance other than the caller has that id, its lineage is not the one asked for (or it has published no feed),
/// or its feed has no such lane.
enum class LinkFollowResult { Applied, StaleLeader, UnknownLeader, UnknownLane };

/// What `LinkRegistry::followedLanes` returns: the followed lanes, and the leader's binding counter read in the same
/// critical section.
struct LinkFollowed {
    std::vector<std::string> lanes;
    /// Moves (never back) whenever a follower binds to or unbinds from the leader, including a bound follower whose
    /// link changes (an unbind, then a bind if it binds again) and a bound follower's removal. It does not move on
    /// reads, a refused `follow`, a publish or new hints alone. 0 for an unknown token.
    std::uint64_t bindings = 0;
};

/// One lane of a leader as `LinkView` lists it: from the leader's feed, plus how many followers bound to that
/// leader follow it in the feed's document.
struct LinkLaneView {
    std::string uid;
    std::string name;
    TrackRole role = TrackRole::Other;   // meaningless for the chords lane
    bool chords = false;
    bool audible = false;
    int followers = 0;
};

/// A leader as `LinkView` lists it. `documentId`, `lineage` and `lanes` come from its stored feed and are empty
/// while it has none (it has not published since it was added or since it last stopped following).
struct LinkLeaderView {
    std::string id;
    std::string label;
    std::string documentId;
    std::string lineage;
    std::vector<LinkLaneView> lanes;   // the feed's order: the chords lane first, then the tracks
};

/// An instance named for display: its persistent id and its label.
struct LinkInstanceRef {
    std::string id;
    std::string label;
};

/// What one instance sees of the registry (plain data, for the editor and for saving), as of one moment.
struct LinkView {
    std::string id;                    // the instance's own id and label; both empty for an unknown token
    std::string label;
    LinkStatus status = LinkStatus::NotFollowing;
    /// When following: its follow, with `leaderId` and `leaderLabel` replaced by the bound leader's current id
    /// and label while bound (as stored while waiting).
    std::optional<LinkFollow> follow;
    /// For a follower that is itself a copy and whose bound leader has a live copy that leads: that copy (the one
    /// that became a copy last, if several). Empty otherwise.
    std::optional<LinkInstanceRef> switchTo;
    /// Every other live instance that is not following, in the order they were added.
    std::vector<LinkLeaderView> leaders;
    /// For a leader with a feed: the lanes of its feed that bound followers follow in the feed's document, with
    /// their counts (`followers` > 0), in the feed's order. Empty for a follower and for a leader without a feed.
    std::vector<LinkLaneView> routed;
};

/// Where the instances of the plug-in in one host process find each other (per-track instruments plan I4 to I7):
/// leaders publish feeds here, and a leader's `publish` writes its followers' link buffers directly, with no idle
/// callback in between. One mutex, taken by every public method and only by non-real-time threads; nothing is
/// called while it is held except the link-buffer fills of rule 4 (`fillSlotFromFeed` and the buffer's
/// `publish`). Hooks are returned to the caller, never called here.
///
/// The rules:
/// 1. *Leaders and followers.* An instance with a follow is a follower: it is never listed in `leaders`, and
///    nothing can bind to it. An instance without one is a leader.
/// 2. *Binding.* A follower is bound to a leader *instance* (its token) when a live leader holds
///    `follow.leaderId`; otherwise it is `Waiting`. Binding is re-resolved whenever an instance is added, removed,
///    adopts an id, or starts or stops following. Once bound, a follower stays bound to that instance while it
///    lives and leads, whatever happens to ids.
/// 3. *Ids on a state load* (`adoptState`), in this order: an instance with bound followers keeps its id; else if
///    another live instance holds `loadedId`, this one is a copy: it takes `freshId` and remembers which instance
///    it was copied from; else it takes `loadedId`. The constructor's id (`add`) is assumed unique. A load whose
///    state has no instance block is no `adoptState`: the instance keeps its id, and only its follow is cleared
///    (`setFollow`, `LinkEndpoint::adoptWithoutBlock`).
/// 4. *Writes into a link buffer* happen under the mutex and only here: in `publish` (each bound follower's lane
///    from the feed, `fillSlotFromFeed(feed, follow.laneUid, follow.documentId, ...)`, then the buffer's
///    `publish` when that returned true), and "empty" (`fillSlotFromFeed(nullptr, ...)`, published when it
///    returned true) when the instance's follow is set, changed or cleared, when it binds, when its leader is
///    removed and when its leader starts following. One `Publication` per link buffer, created at `add` and never
///    reset or recreated until `remove` (not on a follow, an unfollow, a rebind or a state load), so that "same
///    generation, same content" holds for the buffer's whole life. A follower gets content only from its
///    leader's `publish`, never at bind time.
/// 5. *An instance that starts following* loses its dependents: every follower bound to it is unbound, emptied
///    and waits; its stored feed is dropped. *An instance that stops following* is a leader again: followers
///    waiting for its id bind to it (they get content when it next publishes, which its caller does at once).
/// 6. *Hooks.* `setFollow`, `adoptState` and `remove` return the hooks of every other instance whose set of
///    followed lanes may have changed (the follower's old bound leader and its new one), never the caller's own,
///    each at most once.
/// 7. *Status* of a follower: `Waiting` (unbound); `ProjectChanged` (bound, and the leader has published no feed
///    yet or its feed has another `documentId`, whether or not that feed has the lane); `LaneMissing` (the lane
///    uid is not in the feed); `Muted` (the lane is in the feed and not audible); `Playing` otherwise. Right after
///    binding a follower is `Playing` although its buffer is still empty: content arrives with the leader's next
///    publish, which the hook returned to the caller causes at once.
/// 8. *Copies.* For a follower that is itself a copy and whose bound leader has a live copy that leads,
///    `switchTo` names that copy (the newest, if several: the one whose state load made it a copy last).
/// 9. *Labels.* "ChordLab A", "ChordLab B", ... by order of `add`, one per instance for its lifetime (after Z:
///    "ChordLab 27", "ChordLab 28", ...). Display only.
/// 10. *Serial.* Moves on every `add`, every `remove` and `publish` of a live instance, and on a `setFollow`,
///    `follow` or `adoptState` that changes something a view shows (a follow's link or hints, an id, being a copy,
///    a binding); does not move on a call that changes nothing (an identical follow, a load that leaves all of that
///    as it was, a refused `follow`, an unknown token), nor on `followedLanes`, `view` or `serial`.
///
/// A follow is changed (rules 4 and 6) when its `leaderId`, `documentId` or `laneUid` differ, or when it is set
/// or cleared; new hints alone are stored and change nothing else. A follow whose `leaderId` is the instance's own
/// id is accepted by `setFollow` and `adoptState` and waits forever (an instance never binds to itself, and it is
/// no leader while it follows); `follow` refuses it (`UnknownLeader`). Unknown tokens (0, or removed instances)
/// are ignored by every method: nothing changes and the result is empty (`UnknownLeader` for `follow`).
class LinkRegistry {
public:
    /// Called by another instance, on its thread, after it released every lock, to make this instance publish
    /// again. Must only republish; must be safe to call after the instance is gone (the shell guards it).
    using Hook = std::function<void()>;
    using Hooks = std::vector<std::shared_ptr<const Hook>>;

    LinkRegistry();
    ~LinkRegistry();
    LinkRegistry(const LinkRegistry&) = delete;
    LinkRegistry& operator=(const LinkRegistry&) = delete;

    /// The process's registry: heap-allocated on first use and never destroyed, so instances torn down during
    /// static destruction still find it.
    static LinkRegistry& shared();

    /// A new instance: a leader, following nothing, with id `id` (assumed unique among live instances) and the
    /// next label. `linkBuffer` (not null) is its link buffer, which only this registry writes, under its mutex,
    /// until `remove`; it must outlive the registration, and its consumer is the instance's audio thread. Followers
    /// waiting for `id` bind to it. An empty `hook` is never returned.
    LinkToken add(std::string id, TripleBuffer<SnapshotSlot>* linkBuffer, Hook hook);

    /// The instance is going away: its followers are emptied and wait (their links are kept). Returns the hooks
    /// to run: its leader's, if it was bound to one.
    Hooks remove(LinkToken token);

    struct Adopted {
        std::string id;     // the instance's id after the load
        bool copy = false;  // it turned out to be a copy (it took `freshId`)
        Hooks hooks;
    };
    /// A state load: the instance takes `loadedId` unless rule 3 says otherwise, and its follow becomes `follow`.
    /// `freshId` is used when the instance turns out to be a copy. A follow of the instance's own (resulting) id
    /// waits forever. Being a copy (here and for rule 8) is the
    /// outcome of the latest load alone: a later load that is no copy, including one that keeps the id because
    /// followers are bound, ends it. The id is decided before the follow is applied, so a bound leader that loads
    /// a follower's state keeps its id and then loses its dependents (rule 5).
    Adopted adoptState(LinkToken token, std::string loadedId, std::string freshId, std::optional<LinkFollow> follow);

    /// A follow or unfollow as given, for an unfollow and for state loads; a follow by command goes through
    /// `follow`, which checks the leader first. A follow of the instance's own id waits forever. Returns the hooks
    /// to run.
    Hooks setFollow(LinkToken token, std::optional<LinkFollow> follow);

    /// A follow by command, under one lock: finds the live leading instance `leaderId` (not `token` itself;
    /// UnknownLeader), compares its stored feed's lineage with `leaderLineage` (StaleLeader when it differs or the
    /// leader has published no feed), finds the lane `laneUid` in that feed (UnknownLane), and then sets `token`'s
    /// follow to that leader, the feed's documentId and the lane, with the lane's name, its role
    /// (`trackRoleName`; empty for the chords lane) and the leader's label as hints, exactly as `setFollow` would.
    /// `hooks` is replaced by what `setFollow` would return; on any other result nothing changes (not the serial
    /// either) and it is empty. An unknown `token` gives UnknownLeader.
    LinkFollowResult follow(LinkToken token, std::string_view leaderId, std::string_view laneUid,
                            std::string_view leaderLineage, Hooks& hooks);

    /// Which of `laneUids` (kChordsLaneUid may be among them) are followed, in document `documentId`, by followers
    /// bound to `leader`, in the order of `laneUids` and each once (`lanes`; empty for an instance that follows).
    /// A follower whose link names another document does not count, nor does a waiting one. With them, from the same
    /// critical section, `leader`'s binding counter (`LinkFollowed::bindings`): a follower that takes over a lane
    /// its old follower just left changes no lane, but it moves the counter.
    LinkFollowed followedLanes(LinkToken leader, std::string_view documentId,
                               const std::vector<std::string>& laneUids) const;

    /// A leader's publish: stores `feed` and writes every bound follower's link buffer from it (rule 4). Ignored
    /// (and asserts) for an instance that is following, and for a null `feed`: a leader with nothing to publish
    /// does not call it.
    void publish(LinkToken leader, std::shared_ptr<const LinkFeed> feed);

    /// Atomic: moves on every change of what some `view` would return (rule 10). Readable without the mutex.
    std::uint64_t serial() const noexcept;

    /// What `token` sees (see `LinkView`). Allocates.
    LinkView view(LinkToken token) const;

private:
    struct Entry;
    struct State;
    std::unique_ptr<State> state_;
};

// --- the two halves of an instance (per-track instruments plan I5, I6) -----------------------------------------

/// The top of an instance's audio block when it may follow: which of its two buffers the scheduler plays, the
/// instance's own (`own`, its document's compiled lanes, written by its `publishOwn`) or its link buffer (`link`,
/// a leader's lane, written only by the registry). One per instance, used only by its audio thread. Nothing here
/// allocates, locks, throws or logs.
class LinkSource {
public:
    /// The flags of one block: `followHost` gates the progression on the transport (`BlockInfo::playing` is the
    /// host's flag AND this), `thru` is `BlockInfo::thru`.
    struct Flags {
        bool followHost = true;
        bool thru = true;
    };
    /// Which buffer the scheduler is playing: none before the first `select`.
    enum class Source : std::uint8_t { None, Own, Link };

    /// Top of a block. Acquires `own` always and `link` when `following` (and never touches `link` otherwise),
    /// then makes the scheduler adopt at most once: the selected buffer (`link` when `following`, else `own`) is
    /// adopted when it delivered a fresh slot, and also, fresh or not, whenever it differs from the buffer being
    /// played (follow, play, unfollow with nothing published must return to the own content); then its current
    /// slot is adopted with its current generation. A selected buffer that has never been published is adopted as
    /// an empty snapshot: under generation `kLinkGenerationBase` for `link` (no publication of a link buffer has
    /// it: they start at `kLinkGenerationBase + 1`), under generation 0 for `own` (own publications start at 1).
    /// Every acquire comes before that adopt, so the scheduler never keeps reading an array whose buffer was
    /// acquired again (`Snapshot`'s lifetime rule, scheduler.h; triple_buffer.h): the buffer it leaves is free to
    /// be acquired from the next call on, and the one it plays is never acquired without adopting. Own and link
    /// generations come from disjoint ranges (`kLinkGenerationBase`), so "same generation, same content" holds
    /// across a switch, and a switch always flushes and chases. Returns `followHost` from the slot being played
    /// (true while it has none) and `thru` always from `own`'s current slot (true before any): a follower keeps
    /// its own Thru switch.
    Flags select(TripleBuffer<SnapshotSlot>& own, TripleBuffer<SnapshotSlot>& link, bool following,
                 Scheduler& scheduler) noexcept;

    /// The buffer the scheduler plays since the last `select` (`None` before the first). Audio thread.
    Source playing() const noexcept { return playing_; }
    /// The generation of the snapshot last adopted (0 before any). Audio thread.
    std::uint32_t generation() const noexcept { return generation_; }
    /// How many times `select` has called `Scheduler::adopt` (at most once a call). Audio thread; diagnostics.
    std::uint64_t adoptions() const noexcept { return adoptions_; }

private:
    Source playing_ = Source::None;
    std::uint32_t generation_ = 0;
    std::uint64_t adoptions_ = 0;
};

/// What an instance does for linking on its non-real-time threads (per-track instruments plan I5 to I7, I9): its
/// registration with a `LinkRegistry`, its document id and lane uids, its link buffer, its feed, and the switch
/// between leading and following. The caller serialises every call with its model mutex (the shell's
/// `mModelMutex`), as it does for its own buffer's producer side; `following`, `linkBuffer`'s consumer side and
/// `serial` are the exceptions noted on them. Lock order: the caller's model mutex, then the registry's (a leaf).
/// The endpoint never holds two instances' locks and never runs a hook itself, except in its destructor after
/// `remove`: `follow`, `unfollow` and `adopt` hand the hooks to the caller, which runs them after releasing its
/// own lock and then calls `publishOwn` for itself (its own content may have changed role). Allocates: never on
/// the audio thread.
class LinkEndpoint {
public:
    /// Makes uids: instance ids for copies, document ids and lane uids. Each call returns a string never returned
    /// before (a UUID in the shell), never empty and never `kChordsLaneUid`.
    using UidGenerator = std::function<std::string()>;

    /// `LinkFollowResult`, under the name the endpoint's callers use.
    using LinkResult = LinkFollowResult;

    /// What an instance saves and loads of its link (plan I11): `InstanceState` (link_state.h), the state's
    /// `instance` block (`stateJson` / `loadStateJson`, json_api.h).
    using State = InstanceState;

    /// Registers a new instance with `registry` under `instanceId` (assumed unique), a leader following nothing,
    /// with a fresh document id from `makeUid` and an empty lane uid table. Allocates its link buffer on the heap
    /// (about 864 KB) and hands it to the registry. `hook` is what other instances call to make this one
    /// republish (`LinkRegistry::Hook`: it must only lock the caller's model mutex and call `publishOwn`, and be
    /// safe to call after the instance is gone). Followers waiting for `instanceId` bind to it; they get content
    /// from its first `publishOwn`, which the caller makes at once (the shell's constructor publishes).
    LinkEndpoint(LinkRegistry& registry, std::string instanceId, UidGenerator makeUid, LinkRegistry::Hook hook);
    /// Removes the registration (its followers are emptied and wait, the link buffer is never written again) and
    /// then runs the hooks `remove` returned (its leader's, when it was bound to one). The caller holds no lock.
    ~LinkEndpoint();
    LinkEndpoint(const LinkEndpoint&) = delete;
    LinkEndpoint& operator=(const LinkEndpoint&) = delete;

    /// The instance's publish of `store`'s document (`doc` below, at the store's `revision`), in place of a bare
    /// `fillSlot` and the own buffer's publish. It takes the store, not the document, so that the lane uid table
    /// always learns which vanished tracks undo or redo can still bring back (`LaneUids::cover` with
    /// `DocumentStore::historyTrackIds`). In the caller's
    /// model-mutex section: the own buffer's single producer is whoever holds that mutex, and this is its only
    /// writer. It may run in the same section as a store change (a command, a state load right after `adopt`) or
    /// later, from this instance's hook or after the caller ran the hooks of an operation. Returns whether it
    /// published the own buffer at least once.
    ///
    /// Always: gives every track of `doc` a lane uid and drops the entries of tracks neither `doc` nor the history
    /// holds (`LaneUids::cover`).
    ///
    /// Not following: asks the registry which of its
    /// lanes (`kChordsLaneUid` and each track's uid) are followed in THIS document, `documentId()` (a follower
    /// whose link names another document does not count: it is about to fall silent, so this instance keeps the
    /// lane). Then, so that the side that gives a lane up always lets go before the side that takes it is written
    /// (a gap is preferred to a doubled lane, both ways):
    ///  1. it fills and publishes the own buffer (`fillSlot(doc, AllExcept{...}, ...)`, published when that
    ///     returns true) with every lane excluded that is followed now OR was excluded by the last own publication
    ///     and is still a lane of `doc`: nothing comes back yet, newly followed lanes leave. The last exclusions
    ///     are kept by lane key (the chords lane, a track number), not by uid, so they carry over a document
    ///     replacement whether or not the new lane table gives the number its old uid (`adopt` with another
    ///     document, `adoptWithoutBlock`): the old follower is emptied in step 2 before the track comes back;
    ///  2. it publishes a feed (`buildLinkFeed`, the followed lanes compiled) when anything a feed carries changed
    ///     since the last one, which writes every bound follower's link buffer: newly bound followers get their
    ///     lane, followers whose lane no longer resolves (another document, a deleted track) are emptied;
    ///  3. only when some lane excluded by step 1 is not followed now, it fills and publishes the own buffer again
    ///     with exactly the followed lanes excluded: the lane comes back after its old follower was emptied (by the
    ///     registry on an unfollow, or by step 2). Without such a lane step 3 does not run (no second compile).
    /// A feed is published when there was none since the instance was created, adopted a state or stopped
    /// following, and otherwise when one of these differs from the last feed's: `revision` (the store's content
    /// revision, which every undoable change moves), the document id, the lane uids in order, `progressionLoop`,
    /// `followHost`, any lane's audibility (mute and solo), the followed set, `lineage` (the label is the
    /// registry's for the registration's life and never changes). The followed set includes who follows: the
    /// registry's binding counter (`LinkFollowed::bindings`), since a follower that binds to a lane already
    /// followed changes no lane of the feed but gets its content only from its leader's next publish
    /// (`LinkRegistry` rule 4), the one its hook causes. A selection change, a browsed key, Thru, or a refused or
    /// no-op command publishes no feed.
    ///
    /// Following: fills the own buffer with the whole own document (`fillSlot(doc, LaneSelection{}, ...)`: it is
    /// not played, but Thru rides in it and an unfollow returns to it), publishes it when that returns true, and
    /// publishes no feed. That publication excludes nothing, so a later leading publish brings nothing back.
    bool publishOwn(const DocumentStore& store, const std::string& lineage, TripleBuffer<SnapshotSlot>& ownBuffer,
                    Publication& ownPublication);

    /// A test seam: when set, called inside a leading `publishOwn` between its publications, after step 1 and
    /// after step 2 (twice per call, whether or not a step published anything), with the caller's model mutex
    /// held. The engine's simulation runs audio blocks there to test the order of the publications; the shell
    /// never sets it.
    void setPublicationProbe(std::function<void()> probe);

    /// Follow `laneUid` of the leader `leaderId` if `leaderLineage` is that leader's current lineage: one
    /// `LinkRegistry::follow` call (its checks and results). On `Applied` the instance follows (`following()`
    /// turns true after the registry wrote the link buffer empty) and `hooks` holds what the caller must run after
    /// releasing its lock; on any other result nothing changes and `hooks` is empty.
    LinkResult follow(std::string_view leaderId, std::string_view laneUid, std::string_view leaderLineage,
                      LinkRegistry::Hooks& hooks);
    /// Stop following (nothing changes for an instance that does not follow). The registry empties the link
    /// buffer, then `following()` turns false; `hooks` receives the old leader's hook, to run after the lock.
    void unfollow(LinkRegistry::Hooks& hooks);

    /// What to save: the instance's id, its document id and lane uids, and its follow with `leaderId` (and the
    /// `leaderLabel` hint) replaced by the bound leader's current ones while bound (as stored while waiting).
    State state() const;
    /// A state load whose state has the instance block (`loadStateJson` returned one; for a state without one the
    /// shell calls `adoptWithoutBlock`). Call it in the same model-mutex section as the store's replace, after the replace: otherwise another
    /// instance's hook could run `publishOwn` with the new document and the old document id and lane table, or the
    /// old document and the new ones. `publishOwn` may follow in that same section, as well as after the hooks.
    /// The registry decides the id (`LinkRegistry::adoptState` rule 3, a fresh uid from `makeUid` for a copy) and
    /// replaces the follow atomically under its mutex; the document id and the lane uid table become the loaded
    /// ones; `following()` follows the loaded follow. `hooks` receives what the caller must run after releasing
    /// its lock (the old and new leader's). The next `publishOwn` publishes a feed.
    void adopt(const State& loaded, LinkRegistry::Hooks& hooks);
    /// A state load whose state has no instance block (`loadStateJson` returned none: a state saved before linking,
    /// or by a caller that gave none). The shell calls this whenever that happens, never "keeps what it has": the
    /// loaded document is unrelated to the old one as far as links know, so a bound leader must not hand its
    /// track 1 to the followers of the old document's track 1, and a follower must not keep a link its state says
    /// nothing of. So: the document id becomes a fresh `makeUid()`; the lane uid table is emptied (the next
    /// `cover` gives every track a fresh uid); the follow is cleared through the registry in `unfollow`'s order
    /// (the registry empties the link buffer, then `following()` turns false); the instance keeps its id (it is the
    /// same instance; no `adoptState`, so the id rules do not apply). `hooks` receives the old leader's hook, to run
    /// after the lock. Same calling contract as `adopt`: in the model-mutex section of the store's replace, after
    /// the replace. The next `publishOwn` publishes a feed, under the new document id, so every bound follower
    /// stops resolving (`ProjectChanged`, its link buffer empty) while this instance plays every lane itself.
    void adoptWithoutBlock(LinkRegistry::Hooks& hooks);

    /// Whether the instance follows. Atomic: the audio thread reads it at the top of every block
    /// (`LinkSource::select`). It turns true only after the link buffer holds a publication.
    bool following() const noexcept { return following_.load(std::memory_order_acquire); }
    /// The link buffer: the registry is its only producer; its consumer side is the audio thread's.
    TripleBuffer<SnapshotSlot>& linkBuffer() noexcept { return *linkBuffer_; }
    /// The registry's serial (any thread).
    std::uint64_t serial() const noexcept;
    /// What this instance sees of the registry.
    LinkView view() const;
    /// The id of the document the instance holds.
    const std::string& documentId() const noexcept { return documentId_; }

private:
    /// What the last published feed was built from, for `publishOwn`'s "anything a feed carries changed".
    struct FeedKey {
        std::uint64_t revision = 0;
        std::string documentId;
        std::string lineage;
        bool loop = true;
        bool followHost = true;
        std::vector<std::string> laneUids;  // kChordsLaneUid, then the tracks' uids in document order
        std::vector<bool> audible;          // in the same order
        std::vector<std::string> followed;
        std::uint64_t bindings = 0;  // the registry's binding counter for this instance
    };

    LinkRegistry& registry_;
    UidGenerator makeUid_;
    std::unique_ptr<TripleBuffer<SnapshotSlot>> linkBuffer_;
    LinkToken token_ = 0;
    std::string label_;
    std::string documentId_;
    LaneUids uids_;
    std::atomic<bool> following_{false};
    std::optional<FeedKey> lastFeed_;  // empty: the next leading publishOwn publishes a feed
    std::vector<LaneKey> ownExcluded_;  // the lanes (by key) the last own publication excluded
    std::function<void()> probe_;
};

}  // namespace chordlab
