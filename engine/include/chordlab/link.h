#pragma once

#include "chordlab/compiler.h"
#include "chordlab/document.h"
#include "chordlab/note_event.h"
#include "chordlab/publication.h"
#include "chordlab/tracks.h"
#include "chordlab/triple_buffer.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chordlab {

// Linking instances (per-track instruments plan I2, I4 to I7): a leader hands each follower the compiled notes of
// one lane, and a follower plays them from a buffer of its own. This header holds the pure, single-threaded
// pieces (how lanes are named across saves, what a leader publishes, and how a follower's slot is filled from it)
// and the registry where the instances of one process meet. Standard library only: no platform code.

/// The key the chords lane has in every link.
inline constexpr std::string_view kChordsLaneUid = "chords";

/// The most entries of vanished tracks a `LaneUids` table keeps (`LaneUids::cover`).
constexpr std::size_t kMaxVanishedLaneUids = 64;

/// One instance's table of lane uids (per-track instruments plan I2): each track number of the document it has
/// published gets a UUID once, so that a link names a lane that a reverted or replaced document cannot imitate
/// by reusing the number. A track number is `Track::id`. No entry is ever `kChordsLaneUid` or empty, and no two
/// entries share a uid (`cover` takes fresh uids from its generator, `replace` asserts it of a loaded table).
class LaneUids {
public:
    /// A fresh, never-before-returned uid (a UUID in the shell); never empty, never `kChordsLaneUid`.
    using Generator = std::function<std::string()>;

    /// Gives every track of `doc` without an entry a uid from `make`, in document order. Entries of tracks that
    /// no longer exist are kept (undo brings a track back with its number, and it must keep its uid), except that
    /// at most `kMaxVanishedLaneUids` entries of vanished tracks stay: beyond that the lowest track numbers go
    /// first. The entry of a track of `doc` is never removed. `kChordsLaneUid` (or an empty string) is never
    /// given to a track: `make` returning one is a programming error (asserted); without asserts such a value is
    /// skipped and `make` asked again. Returns whether the table changed (an entry added or removed).
    bool cover(const Document& doc, const Generator& make);

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

/// What a follower follows: persisted with the follower (plan I11). `leaderId`, `documentId` and `laneUid` name
/// the link; the other three are hints for messages only and never decide anything.
struct LinkFollow {
    std::string leaderId;      // the leader instance's persistent id
    std::string documentId;    // the document the link was made in
    std::string laneUid;       // kChordsLaneUid or a lane uid of that document
    std::string laneName;      // hints for messages only
    std::string laneRole;
    std::string leaderLabel;
};

/// All six fields equal.
bool operator==(const LinkFollow& a, const LinkFollow& b) noexcept;
bool operator!=(const LinkFollow& a, const LinkFollow& b) noexcept;

/// Where an instance stands as a follower (`LinkRegistry` rule 7). `NotFollowing` for a leader.
enum class LinkStatus { NotFollowing, Waiting, ProjectChanged, LaneMissing, Muted, Playing };

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
///    it was copied from; else it takes `loadedId`. The constructor's id (`add`) is assumed unique.
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
///    yet or its feed has another `documentId`); `LaneMissing` (the lane uid is not in the feed); `Muted` (the lane
///    is in the feed and not audible); `Playing` otherwise.
/// 8. *Copies.* For a follower that is itself a copy and whose bound leader has a live copy that leads,
///    `switchTo` names that copy (the newest, if several: the one whose state load made it a copy last).
/// 9. *Labels.* "ChordLab A", "ChordLab B", ... by order of `add`, one per instance for its lifetime (after Z:
///    "ChordLab 27", "ChordLab 28", ...). Display only.
/// 10. *Serial.* Moves on every call that changes what some `view` would return, and on `publish`; does not move
///    on `followedLanes` or `view`.
///
/// A follow is changed (rules 4 and 6) when its `leaderId`, `documentId` or `laneUid` differ, or when it is set
/// or cleared; new hints alone are stored and change nothing else. Unknown tokens (0, or removed instances) are
/// ignored by every method: nothing changes and the result is empty.
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
    /// `freshId` is used when the instance turns out to be a copy. Being a copy (here and for rule 8) is the
    /// outcome of the latest load alone: a later load that is no copy, including one that keeps the id because
    /// followers are bound, ends it. The id is decided before the follow is applied, so a bound leader that loads
    /// a follower's state keeps its id and then loses its dependents (rule 5).
    Adopted adoptState(LinkToken token, std::string loadedId, std::string freshId, std::optional<LinkFollow> follow);

    /// A follow or unfollow by command. Returns the hooks to run.
    Hooks setFollow(LinkToken token, std::optional<LinkFollow> follow);

    /// Which of `laneUids` (kChordsLaneUid may be among them) are followed, in document `documentId`, by followers
    /// bound to `leader`, in the order of `laneUids` and each once. A follower whose link names another document
    /// does not count, nor does a waiting one.
    std::vector<std::string> followedLanes(LinkToken leader, std::string_view documentId,
                                           const std::vector<std::string>& laneUids) const;

    /// A leader's publish: stores `feed` and writes every bound follower's link buffer from it (rule 4). Ignored
    /// (and asserts) for an instance that is following.
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

}  // namespace chordlab
