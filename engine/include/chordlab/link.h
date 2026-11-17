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
#include <climits>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
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
    /// A uid that no call has returned before, in this process or an earlier one, for this instance or another (a
    /// random UUID in the shell); never empty, never `kChordsLaneUid`. That uniqueness is the generator's duty and
    /// nothing here can stand in for it: the table forgets a track's uid once no undo can bring the track back
    /// (`cover`), so a generator that returned a forgotten uid again would hand the lane a follower still names to
    /// another track, and no bounded table could remember every retired uid. `cover` only guarantees that the
    /// table never holds one uid twice.
    using Generator = std::function<std::string()>;

    /// First erases every entry whose track is neither in `doc` nor in `historyTrackIds` (ascending:
    /// `DocumentStore::historyTrackIds`, the tracks an undo or a redo can bring back with their number, and so with
    /// their uid), whatever its number; so the table never holds more than `kMaxInstanceLanes` entries. Then gives
    /// every track of `doc` without an entry a uid from `make`, in document order. `kChordsLaneUid` (or an empty
    /// string) is never given to a track: `make` returning one is a programming error (asserted); without asserts
    /// such a value is skipped and `make` asked again. Nor is a uid the table already holds (an entry kept, or one
    /// given to an earlier track in this call): `make` is asked again, because two entries with one uid would make
    /// the saved state unloadable. That is a guard for the table, not leave for a generator to repeat itself (see
    /// `Generator`). Pruning comes first, so an answer is checked against the entries that stay only. For one
    /// track `make` is asked at most as many times as the table then holds entries, plus 64: a generator that never
    /// returns one answer twice can collide with each held uid at most once, so it always covers the track. Only a
    /// generator that repeats itself can run out; then, as defined behaviour (no assertion), the track gets no
    /// entry in this call, the next `cover` asks again, and meanwhile the track is uncovered: `buildLinkFeed` leaves
    /// it out of the feed (not offered, never followed) and its leader plays it itself. Returns whether the table
    /// changed (an entry added or removed).
    bool cover(const Document& doc, const std::vector<int>& historyTrackIds, const Generator& make);

    /// The uid of track `trackId`, or empty when it has none.
    std::string uidOf(int trackId) const;

    /// The track number whose entry holds `uid`, or 0 when none does (`kChordsLaneUid`, an empty or unknown uid).
    /// 0 is never a track number (`Track::id` counts from 1). Whether the document still holds that track is not
    /// asked: an entry stays while undo or redo can bring its track back (`cover`).
    int trackOf(std::string_view uid) const;

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
    double endBeat = 0.0;     // compile(doc).endBeat
    bool loop = true;         // doc.progressionLoop
    bool followHost = true;   // doc.followHost
    std::vector<LinkLane> lanes;   // the chords lane first, then the tracks in document order
};

/// Builds the feed of `doc`. A track `uids` does not cover (no uid: `LaneUids::cover` ran out of answers) is left out
/// of the feed's lanes: never listed with an empty uid, so it is not offered and cannot be followed, while every
/// other lane is published as usual (its leader plays the uncovered one itself). The lanes whose uid is in
/// `followed` are compiled (`compiled` true, their events possibly empty: an inaudible lane compiles to nothing);
/// the others carry no events. A uid in `followed` that names no lane of `doc` is ignored. Allocates: never on
/// the audio thread.
std::shared_ptr<const LinkFeed> buildLinkFeed(const Document& doc, std::string documentId, const LaneUids& uids,
                                              std::string lineage, const std::vector<std::string>& followed);

/// How close in time two state loads that made copies must be to count as one paste of a group (`LinkRegistry`
/// rule 8), in milliseconds, inclusive. A host duplicates or pastes the members of a group within milliseconds; a
/// person needs far longer than two seconds between two duplications of their own.
constexpr std::int64_t kPasteWindowMs = 2000;

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

/// The preview ids a `LinkRegistry` gives the commands it forwards to a follower's scheduler (rule 12), inclusive:
/// the top 2^20 + 1 ids of the range an editor's ids come from (0..INT_MAX). Editors count up from small numbers,
/// and the bridge refuses an editor's id inside this range, so a forwarded id never meets an editor's in one
/// scheduler.
constexpr int kForwardedPreviewIdFirst = INT_MAX - (1 << 20);
constexpr int kForwardedPreviewIdLast = INT_MAX;

/// Rule 12: the next id for one follower's forwarded preview. Tries the ids from `cursor` on, in order, cycling
/// inside [`first`, `last`] (after `last` comes `first`), and returns the first that `held` (the ids still
/// recorded for that follower) does not contain; `cursor` is then the id after it. Tries at most `held.size()` + 1
/// ids, never more than the range holds: when every id of the range is held it returns nothing (and `cursor` has
/// moved). `cursor` must lie in the range (asserted; outside it, it starts at `first`). The registry calls it
/// with the default range; the range is a parameter so that tests can reach the wrap and the exhaustion.
std::optional<int> nextForwardedPreviewId(int& cursor, const std::set<int>& held, int first = kForwardedPreviewIdFirst,
                                          int last = kForwardedPreviewIdLast);

/// Where a registered instance takes forwarded previews (rule 12): its scheduler, into which the registry calls
/// `Scheduler::pushPreview`, and the flag its audio thread turns into `Scheduler::panic` at the top of its next
/// block (the shell's `mPanicPending`), which the registry sets. Both are valid from `LinkRegistry::add` to
/// `LinkRegistry::remove`, like the link buffer, and the registry touches neither after `remove` returned. A null
/// `scheduler` (the default) means the instance takes no forwards: it is never sent a preview, and its `panic` is
/// never set. With a scheduler, `panic` must not be null (asserted).
struct LinkPreviewSink {
    Scheduler* scheduler = nullptr;
    std::atomic<bool>* panic = nullptr;
};

/// What `LinkRegistry::forwardPreview` did with a preview command (rule 12).
enum class LinkForward {
    /// Nothing was forwarded: the caller gives the command to its own scheduler, as without links.
    NotForwarded,
    /// Pushed to at least one follower's scheduler: the caller does not play it itself.
    Forwarded,
    /// The command was for followers (a Start for a lane followers play here, or an Extend or Stop of a forwarded
    /// Start), and every one of their preview rings was full: nothing was pushed. The caller does not play it
    /// either and reports a full queue, as for its own ring.
    QueueFull,
};

// `LinkFollow` (what a follower follows) and `InstanceState` (what an instance saves) are plain data in link_state.h.

/// Where an instance stands as a follower (`LinkRegistry` rule 7). `NotFollowing` for a leader.
enum class LinkStatus { NotFollowing, Waiting, ProjectChanged, LaneMissing, Muted, Playing };

// `LinkFollowResult` (the outcome of `LinkRegistry::follow`) is in link_state.h, so that the shell's header-only helpers
// can name it without this header.

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
    /// The number (`Track::id`) of the track this lane is, in the viewing leader's own lane uid table: set only in
    /// `LinkView::routed` and only by `LinkEndpoint::view` (`LaneUids::trackOf` of `uid`). 0 for the chords lane,
    /// for a uid the table does not hold, everywhere in `LinkView::leaders`, and in every `LinkRegistry::view`
    /// (the registry knows no track numbers).
    int trackId = 0;
};

/// A leader as `LinkView` lists it. `label` is its id's label at that moment (`LinkRegistry` rule 9).
/// `documentId`, `lineage` and `lanes` come from its stored feed and are empty while it has none (it has not
/// published since it was added or since it last stopped following).
struct LinkLeaderView {
    std::string id;
    std::string label;
    std::string documentId;
    std::string lineage;
    std::vector<LinkLaneView> lanes;   // the feed's order: the chords lane first, then the tracks
};

/// An instance named for display: its persistent id and that id's label at that moment (`LinkRegistry` rule 9).
struct LinkInstanceRef {
    std::string id;
    std::string label;
};

/// What one instance sees of the registry (plain data, for the editor and for saving), as of one moment.
struct LinkView {
    /// The instance's own id; empty for an unknown token.
    std::string id;
    /// The label of its own id (`LinkRegistry` rule 9) while it is not following; empty while it follows (a
    /// follower has no label of its own) and for an unknown token.
    std::string label;
    LinkStatus status = LinkStatus::NotFollowing;
    /// When following: its follow, with `leaderId` replaced by the bound leader's current id while bound (as stored
    /// while waiting), and `leaderLabel` replaced, bound or waiting, by the label of that `leaderId` (rule 9), never
    /// the stored hint.
    std::optional<LinkFollow> follow;
    /// For a follower of a duplicated or pasted *group* (`LinkRegistry` rule 8): it became a copy by a state load
    /// that carried a follow, it is bound to a leader O, and a copy of O that is not following was made a copy
    /// within `kPasteWindowMs` of it; that copy (the newest such, if several). Empty otherwise.
    std::optional<LinkInstanceRef> switchTo;
    /// For an instance that is not following and is a copy (`LinkRegistry` rule 3: its latest state load found its
    /// id held by another live instance, the *original*): the original, by its current id and label, while the
    /// original lives and is not following, no follower is bound to this instance, and no follower's `switchTo`
    /// names this instance (rule 11). Empty otherwise, and so always for a follower (a follower of a pasted group
    /// has `switchTo`, rule 8). The relation follows the original instance's token: it names whatever id the
    /// original takes and survives whatever the original loads, and, unlike a binding, it does not re-form when a
    /// new instance takes the original's id back (after a delete and its undo the copy has no `copyOf`).
    std::optional<LinkInstanceRef> copyOf;
    /// Every other live instance that is not following, in the order they were added.
    std::vector<LinkLeaderView> leaders;
    /// For a leader with a feed: the lanes of its feed that bound followers follow in the feed's document, with
    /// their counts (`followers` > 0), in the feed's order. Empty for a follower and for a leader without a feed.
    /// `LinkEndpoint::view` fills each one's `trackId`; `LinkRegistry::view` leaves it 0.
    std::vector<LinkLaneView> routed;
};

/// Where the instances of the plug-in in one host process find each other (per-track instruments plan I4 to I7):
/// leaders publish feeds here, and a leader's `publish` writes its followers' link buffers directly, with no idle
/// callback in between. One mutex, taken by every public method and only by non-real-time threads; nothing is
/// called while it is held except the link-buffer fills of rule 4 (`fillSlotFromFeed` and the buffer's
/// `publish`) and, rule 12, a follower's `Scheduler::pushPreview` (a bounded wait-free ring push) and the store
/// into its panic flag. Hooks are returned to the caller, never called here.
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
///    it was copied from (that instance, by its token, not its id), whether this load carried a follow, and when
///    (the registry's clock, `Clock`); else it takes `loadedId`. Being a copy is the
///    outcome of the latest load alone: a load that is no copy (the first case, the third) ends it. The
///    constructor's id (`add`) is assumed unique. A load whose state has no instance block is no `adoptState`:
///    the instance keeps its id, its follow is cleared, and it is no copy any more, since the document it now
///    holds is unrelated to the original's as far as links know (`adoptWithoutBlock`, which
///    `LinkEndpoint::adoptWithoutBlock` calls). Nothing else ends or starts being a copy: not a `setFollow` or
///    `follow` of the copy, nor anything the original does (rules 8 and 11 look at the original when asked).
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
/// 6. *Hooks.* `setFollow`, `adoptState`, `adoptWithoutBlock` and `remove` return the hooks of every other
///    instance whose set of followed lanes may have changed (the follower's old bound leader and its new one),
///    never the caller's own, each at most once.
/// 7. *Status* of a follower: `Waiting` (unbound); `ProjectChanged` (bound, and the leader has published no feed
///    yet or its feed has another `documentId`, whether or not that feed has the lane); `LaneMissing` (the lane
///    uid is not in the feed); `Muted` (the lane is in the feed and not audible); `Playing` otherwise. Right after
///    binding a follower is `Playing` although its buffer is still empty: content arrives with the leader's next
///    publish, which the hook returned to the caller causes at once.
/// 8. *Switch to a pasted leader.* `switchTo` of a follower f names the newest live instance c (the one whose
///    state load made it a copy last) such that: f became a copy by a load that carried a follow (it was copied
///    as a follower); f is bound to a leader O; c is a copy of O and is not following; and c and f were made
///    copies within `kPasteWindowMs` of each other, in either order. Otherwise it is empty. This is the one case
///    the hint is for: a group (a leader and its followers) duplicated or pasted in one go (plan I7), whose copied
///    followers still follow the original leader. A host makes the members of such a group within milliseconds of
///    each other, while a person needs far longer than `kPasteWindowMs` between two duplications. So a follower
///    made from the leader's chain (a copy carrying no follow, which then follows by command), or a follower's
///    chain duplicated now and the leader's chain later, is never offered a copy that is merely the next follower
///    to be.
/// 9. *Labels.* A label belongs to an instance id, not to a registration: the registry keeps a table from ids to
///    numbers, and number n is labelled "ChordLab A" to "ChordLab Z" for 1 to 26, then "ChordLab 27",
///    "ChordLab 28", and so on. An id is in use while a live instance that is not following holds it, or while a
///    live follower's follow names it as its leader, whether that follower is bound or waiting. Whenever an
///    instance is added or removed, adopts an id, or has its follow's link set, changed or cleared, the table
///    first drops the row of every id no longer in use, and then gives every id in use without a row the lowest
///    number from 1 that no row holds, visiting the instances in the order they were added (each one stands for
///    its own id when it is not following, for its follow's leader's otherwise). An id in use therefore keeps its
///    number: a deleted leader's label waits for its id while a follower names it, and comes back with the
///    instance that adopts that id again (an undo). The number goes with the id, not with the instance: a leader
///    whose state load gives it another id (rule 3) is labelled anew, with that id's row if it has one and else
///    with the lowest free number, which is its old one unless a lower one was free; and the number of a deleted
///    leader that no follower names is free for the next id that needs one, also before an undo brings that
///    leader back. A follower has no label of its own. Every label a view shows
///    is the table's for that id at that moment: an instance's own, a listed leader's, `switchTo`'s and
///    `copyOf`'s, and a follow's `leaderLabel`. The `leaderLabel` hint stored with a follow (written by `follow`,
///    loaded with a state) is never shown. Display only.
/// 10. *Serial.* Moves on every `add`, every `remove` and `publish` of a live instance, and on a `setFollow`,
///    `follow`, `adoptState` or `adoptWithoutBlock` that changes something a view shows (a follow's link or shown hints,
///    an id, being a copy, a binding); does not move on a call that changes nothing (an identical follow, a load
///    that leaves all of that as it was, a refused `follow`, an unknown token), nor on `followedLanes`, `view` or
///    `serial`. So it moves whenever some view's `copyOf` or `switchTo` changes: those change only with a
///    removal, a follow's link, a binding, an id or being a copy (the times a copy was made are fixed with it;
///    the clock moving on changes no view). Likewise for labels: an id in use never changes its number (rule 9),
///    so a label changes in some view only with an `add`, a `remove`, an id or a follow's link. A new
///    `leaderLabel` hint alone is stored but changes no view, and does not move it.
/// 11. *Copy of.* For an instance that is not following and is a copy (rule 3), `copyOf` names the instance it
///    was copied from while that instance lives and is not following, no follower is bound to this instance, and
///    no follower's `switchTo` (rule 8) names this instance: a copy that others play from, or the new leader of a
///    pasted group, is a leader in its own right and is never invited to follow (it would leave its followers
///    waiting); it gets `copyOf` back when that ends. It is empty while the original follows and
///    names it again when it stops (the copy still holds a copy of its document: following changes no document,
///    plan I1), and the same holds for the copy's own following (empty while it follows, back when it stops). The
///    original's state loads do not end it (whatever id or document it takes, it is still the instance the copy
///    was made from, and its current lanes are what the copy can follow); its removal ends it for good (tokens
///    are never handed out twice, so, unlike a binding, it does not re-form when a successor takes the original's
///    id back). A copy of
///    a copy names the instance its own load collided with, never that one's original, also once that instance is
///    gone. Two copies of one original each name it.
/// 12. *Previews.* An instance takes forwarded previews when `add` gave it a sink with a scheduler
///    (`LinkPreviewSink`). `forwardPreview` pushes a preview Start of the caller's lane `laneUid` in its document
///    `documentId` into the scheduler of every follower that takes forwards, is bound to the caller, and whose
///    follow names that document and that lane (the test `followedLanes` counts by), each under an id of its own
///    from the reserved range (`kForwardedPreviewIdFirst` to `kForwardedPreviewIdLast`, `nextForwardedPreviewId`
///    with a cursor per follower, skipping the ids still recorded for it), and records, for the caller and the
///    Start's id, every follower whose push succeeded, with its id. A follower whose ring is full is left out; if
///    every one was, nothing is recorded and the answer is `QueueFull`. An Extend or a Stop of a recorded id goes
///    to exactly the recorded followers under their ids, whoever is bound on the lane now (a follower that joined
///    the lane since gets neither); a Stop ends the record; an Extend or a Stop with no record is not forwarded. A
///    Start of an id the caller has recorded first ends that record (a Stop to each recorded follower), whether or
///    not the new Start is forwarded, as a scheduler releases a preview whose id starts again. A follower stays
///    recorded only while it is bound to the caller without a break and loads no state: when it stops being bound
///    (its follow's link changed, also to another lane of the same leader, or cleared; its leader removed or
///    starting to follow), when it loads a state (`adoptState`, even one with its follow as it was, and
///    `adoptWithoutBlock`) and when it is removed, the call that did it drops the follower from every record and,
///    unless the follower was removed, sets its panic flag. A leader whose feed takes another document id or loses
///    the lane unbinds no one (rules 2 and 7): its records stay, and a forwarded preview then sounds until its
///    Stop, which still reaches the follower, or the scheduler's watchdog. `panicFollowers` sets the panic flag of
///    every follower bound to the caller that takes forwards, whatever its lane or document, and drops every record
///    of the caller. A panic releases everything the follower sounds, its own notes too (its lane's notes come
///    back with their next events, `Scheduler::panic`), and discards the preview commands queued before it, so a
///    Start forwarded to it after the cleanup and before its next audio block never sounds. `stopForwards` (the
///    caller's editor closing) ends every record of the caller: a Stop under the recorded id to each recorded
///    follower, and the record dropped; no panic flag is set. Forwarding, `panicFollowers` and `stopForwards`
///    change no view: the serial does not move (rule 10). What the registry does to a follower is only those
///    pushes and stores, under the mutex. A scheduler's preview ring has one producer, so every `forwardPreview`
///    and `stopForwards` must be called on the thread that makes the followers' own pushes (the plug-in's main
///    thread): the caller's obligation. The panic flag is an atomic, set from whichever thread runs the call.
///
/// A follow is changed (rules 4 and 6) when its `leaderId`, `documentId` or `laneUid` differ, or when it is set
/// or cleared; new hints alone are stored and change nothing else (new `laneName` or `laneRole` hints show in the
/// follower's view, a new `leaderLabel` hint shows nowhere, rule 9). A follow whose `leaderId` is the instance's own
/// id is accepted by `setFollow` and `adoptState` and waits forever (an instance never binds to itself, and it is
/// no leader while it follows); `follow` refuses it (`UnknownLeader`). Unknown tokens (0, or removed instances)
/// are ignored by every method: nothing changes and the result is empty (`UnknownLeader` for `follow`).
class LinkRegistry {
public:
    /// Called by another instance, on its thread, after it released every lock, to make this instance publish
    /// again. Must only republish; must be safe to call after the instance is gone (the shell guards it).
    using Hook = std::function<void()>;
    using Hooks = std::vector<std::shared_ptr<const Hook>>;
    /// A monotonic time in milliseconds (rule 3: when a state load made an instance a copy). Called under the
    /// registry's mutex, only from `adoptState`: it must not call into the registry, and must not go backwards.
    using Clock = std::function<std::int64_t()>;

    /// A registry on `std::chrono::steady_clock` (milliseconds).
    LinkRegistry();
    /// A registry on `clock` (tests drive their own; an empty `clock` means `steady_clock`).
    explicit LinkRegistry(Clock clock);
    ~LinkRegistry();
    LinkRegistry(const LinkRegistry&) = delete;
    LinkRegistry& operator=(const LinkRegistry&) = delete;

    /// The process's registry, on `std::chrono::steady_clock`: heap-allocated on first use and never destroyed,
    /// so instances torn down during static destruction still find it.
    static LinkRegistry& shared();

    /// A new instance: a leader, following nothing, with id `id` (assumed unique among live instances), labelled
    /// by rule 9 (the label its id already holds, or the lowest free one). `linkBuffer` (not null) is its link
    /// buffer, which only this registry writes, under its mutex, until `remove`; it must outlive the registration,
    /// and its consumer is the instance's audio thread. Followers waiting for `id` bind to it. An empty `hook` is
    /// never returned. `previews` is where it takes forwarded previews (rule 12), valid until `remove` as the link
    /// buffer is; the default (no scheduler) takes none.
    LinkToken add(std::string id, TripleBuffer<SnapshotSlot>* linkBuffer, Hook hook, LinkPreviewSink previews = {});

    /// The instance is going away: its followers are emptied and wait (their links are kept). Returns the hooks
    /// to run: its leader's, if it was bound to one.
    Hooks remove(LinkToken token);

    struct Adopted {
        std::string id;     // the instance's id after the load
        bool copy = false;  // it turned out to be a copy (it took `freshId`)
        Hooks hooks;
    };
    /// A state load: the instance takes `loadedId` unless rule 3 says otherwise, and its follow becomes `follow`.
    /// `freshId` is used when the instance turns out to be a copy; then the registry also records whether `follow`
    /// is set (the load carried a follow) and the clock's time, for rule 8. A follow of the instance's own
    /// (resulting) id waits forever. Being a copy (here and for rules 8 and 11) is the outcome of the latest load
    /// alone: a later load that is no copy, including one that keeps the id because
    /// followers are bound, ends it, and a later load that is a copy again (of the same original or another) makes
    /// it the copy of the instance that load collided with. The id is decided before the follow is applied, so a
    /// bound leader that loads a follower's state keeps its id and then loses its dependents (rule 5).
    Adopted adoptState(LinkToken token, std::string loadedId, std::string freshId, std::optional<LinkFollow> follow);

    /// A state load whose state has no instance block (rule 3): under one lock, the instance is no copy any more
    /// and its follow is cleared exactly as `setFollow(token, std::nullopt)` clears it (the link buffer emptied,
    /// the old leader unbound); its id is kept. Returns the hooks to run (its old leader's, when it was bound).
    Hooks adoptWithoutBlock(LinkToken token);

    /// A follow or unfollow as given, for an unfollow and for state loads; a follow by command goes through
    /// `follow`, which checks the leader first. A follow of the instance's own id waits forever. Returns the hooks
    /// to run.
    Hooks setFollow(LinkToken token, std::optional<LinkFollow> follow);

    /// A follow by command, under one lock: finds the live leading instance `leaderId` (not `token` itself;
    /// UnknownLeader), compares its stored feed's lineage with `leaderLineage` (StaleLeader when it differs or the
    /// leader has published no feed), finds the lane `laneUid` in that feed (UnknownLane), and then sets `token`'s
    /// follow to that leader, the feed's documentId and the lane, with the lane's name, its role
    /// (`trackRoleName`; empty for the chords lane) and the leader's label at that moment (rule 9) as hints, exactly
    /// as `setFollow` would.
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

    /// Rule 12: offers the preview command `cmd` of instance `token` to its followers. A Start is forwarded to the
    /// followers that take forwards and are bound to `token` with a follow naming `documentId` (the caller's current
    /// document id, as for `followedLanes`) and `laneUid` (the lane the Start belongs to: `kChordsLaneUid`, or a
    /// track's lane uid; an empty one matches no follower). An Extend or a Stop goes to the followers recorded for
    /// `cmd.id` by an earlier Start of `token`; `documentId` and `laneUid` are not read. Returns `Forwarded` when at
    /// least one follower's scheduler took it (the caller does not play it), `QueueFull` when it was for followers
    /// but every one of their rings was full (nothing pushed; for a Start nothing recorded), and `NotForwarded`
    /// otherwise: no follower takes forwards on that lane, an Extend or a Stop of an id not recorded, an instance
    /// that follows, an unknown token. The caller plays a `NotForwarded` command itself, as without links. Never
    /// moves the serial. Caller's obligation: called only on the thread that makes every other `pushPreview` into
    /// the followers' schedulers (the plug-in's main thread, where their editors push), since a scheduler's preview
    /// ring has a single producer. Allocates (the record table).
    LinkForward forwardPreview(LinkToken token, std::string_view documentId, std::string_view laneUid,
                               const PreviewCmd& cmd);

    /// Rule 12: the leader `token`'s Panic reaches its followers. Sets the panic flag of every follower bound to
    /// `token` that takes forwards, whatever lane or document its follow names, and drops every preview record of
    /// `token`. Returns how many flags it set (0 for an unknown token or one with no such follower). Pushes nothing,
    /// so any thread may call it. Never moves the serial.
    int panicFollowers(LinkToken token);

    /// Rule 12: everything instance `token` forwarded ends, for its editor closing. For every record of `token`,
    /// pushes a Stop under the recorded id to each recorded follower (each still lives and takes forwards: the
    /// records hold no other) and drops the record, also when a push found a full ring (that follower's watchdog
    /// then ends the preview). Returns how many records it ended (0 for an unknown token or one with none). Sets no
    /// panic flag and touches no other instance's records. Never moves the serial. It pushes into the followers'
    /// schedulers, so it has `forwardPreview`'s obligation: called only on the thread that makes every other
    /// `pushPreview` into them (the plug-in's main thread). Allocates nothing.
    int stopForwards(LinkToken token);

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
    /// before, in this process or an earlier one, for this instance or another (a random UUID in the shell, and
    /// nothing weaker: `LaneUids::Generator` says what a repeated uid would do), never empty and never
    /// `kChordsLaneUid`.
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
    /// from its first `publishOwn`, which the caller makes at once (the shell's constructor publishes). `previews`
    /// is handed to the registry (`LinkRegistry::add`, rule 12): the instance's scheduler and panic flag, which must
    /// outlive the endpoint (the destructor removes the registration first, after which the registry touches
    /// neither); the default takes no forwarded previews.
    LinkEndpoint(LinkRegistry& registry, std::string instanceId, UidGenerator makeUid, LinkRegistry::Hook hook,
                 LinkPreviewSink previews = {});
    /// Removes the registration (its followers are emptied and wait, the link buffer and the preview sink are never
    /// touched again) and then runs the hooks `remove` returned (its leader's, when it was bound to one). The
    /// caller holds no lock.
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
    /// Always: drops the entries of tracks neither `doc` nor the history holds, and gives every track of `doc` a
    /// lane uid (`LaneUids::cover`). A track the generator could not cover is published all the same, in the own
    /// buffer (it is never followed) and left out of the feed; the feed and every other follower are not held
    /// back by it, and a later publish covers and offers it.
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
    /// `followHost`, any lane's audibility (mute and solo), the followed set, `lineage` (a feed carries no label:
    /// labels are the registry's, rule 9). The followed set includes who follows: the
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

    /// What to save: the instance's id, its document id and lane uids, and its follow as its view shows it
    /// (`LinkRegistry::view`): `leaderId` replaced by the bound leader's current id while bound (as stored while
    /// waiting), and the `leaderLabel` hint replaced, bound or waiting, by the label of that id at that moment
    /// (rule 9).
    State state() const;
    /// A state load whose state has the instance block (`loadStateJson` returned one; for a state without one the
    /// shell calls `adoptWithoutBlock`). Call it in the same model-mutex section as the store's replace, after the
    /// replace: otherwise another instance's hook could run `publishOwn` with the new document and the old document
    /// id and lane table, or the old document and the new ones. `publishOwn` may follow in that same section, as
    /// well as after the hooks.
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
    /// (the registry empties the link buffer, then `following()` turns false), and the instance is no copy any more
    /// (`LinkRegistry::adoptWithoutBlock`: its document is no copy of the original's now, so its view's `copyOf`
    /// ends and no follower is offered it as `switchTo`); the instance keeps its id (it is the same instance; no
    /// `adoptState`, so the id rules do not apply). `hooks` receives the old leader's hook, to run
    /// after the lock. Same calling contract as `adopt`: in the model-mutex section of the store's replace, after
    /// the replace. The next `publishOwn` publishes a feed, under the new document id, so every bound follower
    /// stops resolving (`ProjectChanged`, its link buffer empty) while this instance plays every lane itself.
    void adoptWithoutBlock(LinkRegistry::Hooks& hooks);

    /// Offers a preview command of this instance's editor to the followers that play its lane
    /// (`LinkRegistry::forwardPreview`, rule 12), and returns what the registry did. A Start belongs to the lane of
    /// track `trackId` (`Track::id`) when it is given, to the chords lane when it is not; the lane is named by its
    /// uid in this instance's lane uid table and the document by `documentId()`. A track id without a uid in the
    /// table (none was given yet, or a track number below 1) names no lane: such a Start is offered with an empty
    /// uid, so it is not forwarded, but it still ends what an earlier Start of its id forwarded. An Extend or a
    /// Stop goes where the Start of its id went, and `trackId` is not read. On `NotForwarded` the caller pushes the
    /// command into its own scheduler; on `QueueFull` it reports a full queue. In the caller's model-mutex section
    /// (it reads the lane table and the document id), and on the thread that makes every other `pushPreview` of
    /// the process's instances (the main thread): `LinkRegistry::forwardPreview`'s obligation.
    LinkForward forwardPreview(std::optional<int> trackId, const PreviewCmd& cmd);
    /// The instance's Panic, for its followers (`LinkRegistry::panicFollowers`): sets the panic flag of every
    /// follower bound to it and drops what it forwarded. Returns how many flags were set. The caller still panics
    /// its own scheduler.
    int panicFollowers();
    /// The instance's editor closing, for its followers (`LinkRegistry::stopForwards`): a Stop to every follower
    /// for everything this instance forwarded, and the records dropped. Returns how many records were ended. What
    /// it plays itself is the caller's to stop. In the caller's model-mutex section, as every endpoint call, and on
    /// the thread that makes every other `pushPreview` of the process's instances (the main thread):
    /// `LinkRegistry::stopForwards`' obligation.
    int stopForwards();

    /// Whether the instance follows. Atomic: the audio thread reads it at the top of every block
    /// (`LinkSource::select`). It turns true only after the link buffer holds a publication.
    bool following() const noexcept { return following_.load(std::memory_order_acquire); }
    /// The link buffer: the registry is its only producer; its consumer side is the audio thread's.
    TripleBuffer<SnapshotSlot>& linkBuffer() noexcept { return *linkBuffer_; }
    /// The registry's serial (any thread).
    std::uint64_t serial() const noexcept;
    /// What this instance sees of the registry (`LinkRegistry::view`), with each `routed` lane's `trackId` filled
    /// from this instance's lane uid table (`LaneUids::trackOf`): the track number the uid names now, 0 for the
    /// chords lane and for a uid the table does not hold (`routed` comes from the last feed published, the table
    /// from the last `publishOwn` or load: between a load and the next `publishOwn` an old feed's uid may be
    /// unknown to the loaded table). `leaders` is left as the registry gives it.
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
    std::string documentId_;
    LaneUids uids_;
    std::atomic<bool> following_{false};
    std::optional<FeedKey> lastFeed_;  // empty: the next leading publishOwn publishes a feed
    std::vector<LaneKey> ownExcluded_;  // the lanes (by key) the last own publication excluded
    std::function<void()> probe_;
};

}  // namespace chordlab
