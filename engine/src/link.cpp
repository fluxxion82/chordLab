#include "chordlab/link.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <mutex>
#include <optional>
#include <set>
#include <utility>

namespace chordlab {

namespace {

bool holdsTrack(const Document& doc, int trackId) {
    return std::any_of(doc.tracks.begin(), doc.tracks.end(), [&](const Track& t) { return t.id == trackId; });
}

}  // namespace

namespace {

/// The margin of answers `LaneUids::cover` asks for one track beyond the uids its table holds (see `cover`).
constexpr std::size_t kLaneUidAttempts = 64;

}  // namespace

bool LaneUids::cover(const Document& doc, const std::vector<int>& historyTrackIds, const Generator& make) {
    assert(std::is_sorted(historyTrackIds.begin(), historyTrackIds.end()) && "LaneUids::cover: history ascending");
    bool changed = false;
    // First the entries no undo or redo can use any more: an entry stays while its track is in the document or in
    // the history. Before covering, so that an answer is checked against the entries that stay only.
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (holdsTrack(doc, it->first) ||
            std::binary_search(historyTrackIds.begin(), historyTrackIds.end(), it->first)) {
            ++it;
            continue;
        }
        it = entries_.erase(it);
        changed = true;
    }
    for (const Track& track : doc.tracks) {
        if (entries_.count(track.id) != 0) continue;
        // A generator that never returns one answer twice can collide only with the uids the table holds, each at
        // most once: within entries_.size() + 1 answers one is new. The margin is for a generator that also
        // returns an unusable answer now and then.
        const std::size_t attempts = entries_.size() + kLaneUidAttempts;
        std::optional<std::string> uid;
        for (std::size_t attempt = 0; attempt < attempts && !uid; ++attempt) {
            std::string candidate = make();
            const bool usable = !candidate.empty() && candidate != kChordsLaneUid;
            assert(usable && "a lane uid generator never returns kChordsLaneUid or an empty string");
            // The table never holds one uid twice, whatever the generator does: two entries with one uid would make
            // the saved state unloadable. Ask again. A guard only: a generator must never repeat (`Generator`).
            const bool held = std::any_of(entries_.begin(), entries_.end(),
                                          [&](const auto& entry) { return entry.second == candidate; });
            if (usable && !held) uid = std::move(candidate);
        }
        // Out of answers only with a generator that repeats itself: defined, no entry for this track in this call
        // (it is left out of the feed, and the leader plays it itself); the next cover asks again.
        if (!uid) continue;
        entries_.emplace(track.id, std::move(*uid));
        changed = true;
    }
    return changed;
}

std::string LaneUids::uidOf(int trackId) const {
    const auto it = entries_.find(trackId);
    return it == entries_.end() ? std::string() : it->second;
}

int LaneUids::trackOf(std::string_view uid) const {
    if (uid.empty() || uid == kChordsLaneUid) return 0;
    for (const auto& entry : entries_) {
        if (entry.second == uid) return entry.first;
    }
    return 0;
}

std::optional<LaneKey> LaneUids::laneOf(const Document& doc, std::string_view uid) const {
    if (uid == kChordsLaneUid) return chordsLane();
    if (uid.empty()) return std::nullopt;
    for (const auto& entry : entries_) {
        if (entry.second == uid && holdsTrack(doc, entry.first)) return trackLane(entry.first);
    }
    return std::nullopt;
}

const std::map<int, std::string>& LaneUids::entries() const noexcept {
    return entries_;
}

void LaneUids::replace(std::map<int, std::string> entries) {
#ifndef NDEBUG
    std::set<std::string_view> seen;
    for (const auto& entry : entries) {
        assert(!entry.second.empty() && entry.second != kChordsLaneUid &&
               "LaneUids::replace: a lane uid is never empty nor kChordsLaneUid (the caller validates the table)");
        assert(seen.insert(entry.second).second && "LaneUids::replace: lane uids are pairwise distinct");
    }
#endif
    entries_ = std::move(entries);
}

std::shared_ptr<const LinkFeed> buildLinkFeed(const Document& doc, std::string documentId, const LaneUids& uids,
                                              std::string lineage, const std::vector<std::string>& followed) {
    const auto isFollowed = [&](std::string_view uid) {  // an empty uid (an uncovered track) is never followed
        return !uid.empty() && std::find(followed.begin(), followed.end(), uid) != followed.end();
    };
    auto feed = std::make_shared<LinkFeed>();
    feed->documentId = std::move(documentId);
    feed->lineage = std::move(lineage);
    const Compiled chordsCompiled = compile(doc);  // once, for the frame and for every compiled lane
    feed->endBeat = chordsCompiled.endBeat;
    feed->loop = doc.progressionLoop;
    feed->followHost = doc.followHost;
    feed->lanes.reserve(1 + doc.tracks.size());

    const auto addLane = [&](LinkLane lane, const LaneKey& key) {
        lane.compiled = isFollowed(lane.uid);
        if (lane.compiled) {
            lane.events =
                compilePlaybackFor(doc, LaneSelection{LaneSelection::Mode::Only, {key}}, chordsCompiled).events;
        }
        feed->lanes.push_back(std::move(lane));
    };
    LinkLane chords;
    chords.uid = std::string(kChordsLaneUid);
    chords.chords = true;
    chords.name = "Chords";
    chords.audible = chordsAudible(doc);
    addLane(std::move(chords), chordsLane());
    for (std::size_t i = 0; i < doc.tracks.size(); ++i) {
        const Track& track = doc.tracks[i];
        LinkLane lane;
        lane.uid = uids.uidOf(track.id);
        if (lane.uid.empty()) continue;  // uncovered: not offered, so never followed (its leader plays it)
        lane.name = track.name;
        lane.role = track.role;
        lane.audible = trackAudible(doc, i);
        addLane(std::move(lane), trackLane(track.id));
    }
    return feed;
}

bool fillSlotFromFeed(const LinkFeed* feed, std::string_view laneUid, std::string_view documentId,
                      SnapshotSlot& slot, Publication& last) {
    assert((last.generation == 0 || last.generation > kLinkGenerationBase) &&
           "fillSlotFromFeed: `last` belongs to a link buffer");
    static const std::vector<NoteEvent> kNoEvents;
    const std::vector<NoteEvent>* events = &kNoEvents;
    if (feed != nullptr && feed->documentId == documentId) {
        const auto lane = std::find_if(feed->lanes.begin(), feed->lanes.end(),
                                       [&](const LinkLane& l) { return l.uid == laneUid; });
        if (lane != feed->lanes.end() && lane->compiled) events = &lane->events;
    }
    const double endBeat = feed != nullptr ? feed->endBeat : 0.0;
    const bool loop = feed != nullptr ? feed->loop : true;
    const bool followHost = feed != nullptr ? feed->followHost : true;

    const bool first = last.generation == 0;
    const bool contentChanged =
        first || *events != last.compiled.events || endBeat != last.compiled.endBeat || loop != last.loop;
    const bool followChanged = first || followHost != last.followHost;
    if (!contentChanged && !followChanged) {
        return false;
    }
    const bool fits = events->size() <= slot.events.size();
    assert(fits && "a lane of a leader's document fits one snapshot slot (publication.h)");
    slot.count = fits ? events->size() : 0;
    std::copy_n(events->begin(), slot.count, slot.events.begin());
    slot.endBeat = fits ? endBeat : 0.0;
    slot.loop = loop;
    slot.followHost = followHost;
    slot.midiThru = true;
    last.compiled.events = *events;
    last.compiled.endBeat = endBeat;
    last.compiled.feelLimited = false;
    last.loop = loop;
    last.followHost = followHost;
    last.midiThru = true;
    if (contentChanged) {
        // Reaching UINT32_MAX takes 2^31 - 1 content changes of one link buffer.
        assert(last.generation != UINT32_MAX && "fillSlotFromFeed: the link generation would wrap");
        last.generation = first ? kLinkGenerationBase + 1 : last.generation + 1;
    }
    return true;
}

// --- the registry ---------------------------------------------------------------------------------------------

namespace {

/// Rule 9: the label of number `n` (from 1).
std::string labelFor(std::uint64_t n) {
    if (n >= 1 && n <= 26) return std::string("ChordLab ") + static_cast<char>('A' + (n - 1));
    return "ChordLab " + std::to_string(n);
}

/// Whether two follows name the same link: both absent, or the same leader id, document and lane (the hints do
/// not count).
bool sameLink(const std::optional<LinkFollow>& a, const std::optional<LinkFollow>& b) {
    if (a.has_value() != b.has_value()) return false;
    if (!a) return true;
    return a->leaderId == b->leaderId && a->documentId == b->documentId && a->laneUid == b->laneUid;
}

/// Whether two follows show the same in a view: both absent, or the same link and the same `laneName` and
/// `laneRole` hints. The `leaderLabel` hint is stored but no view shows it (rule 9: a view's label comes from the
/// registry's table), so it does not count (rule 10).
bool sameShownFollow(const std::optional<LinkFollow>& a, const std::optional<LinkFollow>& b) {
    return sameLink(a, b) && (!a || (a->laneName == b->laneName && a->laneRole == b->laneRole));
}

}  // namespace

/// One registered instance. Lives in `State::entries` (a map: its address is stable while it is registered).
struct LinkRegistry::Entry {
    std::string id;
    TripleBuffer<SnapshotSlot>* buffer = nullptr;  // the link buffer: written only by `State::write`
    std::shared_ptr<const Hook> hook;              // null for an empty hook
    Publication publication;                       // the link buffer's, from `add` to `remove` (rule 4)
    std::optional<LinkFollow> follow;              // set: a follower (rule 1)
    LinkToken boundTo = 0;                         // a follower's leader instance, 0 while waiting (rule 2)
    LinkToken copiedFrom = 0;                      // the instance the last state load found it a copy of (rule 3)
    std::uint64_t copyOrder = 0;                   // when it became that copy, for "the newest" (rule 8)
    bool copyCarriedFollow = false;                // that load carried a follow (rule 8); meaningful with copiedFrom
    std::int64_t copyMadeAtMs = 0;                 // the clock when that load made it a copy (rule 8)
    std::shared_ptr<const LinkFeed> feed;          // a leader's last feed; null before one and once it follows
    std::uint64_t bindings = 0;                    // moves on every bind to and unbind from this instance
    LinkPreviewSink previews;                      // rule 12: valid from add to remove; no scheduler: no forwards
    int nextPreviewId = kForwardedPreviewIdFirst;  // rule 12: this follower's cursor in the reserved range
    std::uint64_t linkEpoch = 0;                   // rule 12: moves when it unbinds or loads a state
};

/// Everything behind the mutex. The helpers expect the caller to hold `mutex`.
struct LinkRegistry::State {
    mutable std::mutex mutex;
    std::map<LinkToken, Entry> entries;  // by token, so in the order of add
    LinkToken lastToken = 0;
    std::map<std::string, std::uint64_t> labels;  // rule 9: the number of every id in use
    std::uint64_t copiesMade = 0;
    /// Rule 12: one follower a forwarded Start reached, the id its scheduler knows it by, and its `linkEpoch` then.
    struct Forwarded {
        LinkToken follower = 0;
        int id = 0;
        std::uint64_t epoch = 0;
    };
    /// Rule 12, the record table: (originating instance, the Start's id) -> the followers it reached. Never an
    /// empty list. Every entry names a live originator and followers that are live, bound to it and of the epoch
    /// recorded: `dropBrokenForwards` restores that after every call that can break it.
    std::map<std::pair<LinkToken, int>, std::vector<Forwarded>> forwards;
    std::atomic<std::uint64_t> serial{0};
    Clock clock;  // set by the constructor, never empty

    Entry* find(LinkToken token) {
        const auto it = entries.find(token);
        return it == entries.end() ? nullptr : &it->second;
    }
    const Entry* find(LinkToken token) const {
        const auto it = entries.find(token);
        return it == entries.end() ? nullptr : &it->second;
    }

    /// A live instance that is not following.
    bool leads(LinkToken token) const {
        const Entry* e = find(token);
        return e != nullptr && !e->follow;
    }

    /// Binds follower `e` to `leader`, moving the leader's binding counter.
    void bind(Entry& e, LinkToken leader) {
        e.boundTo = leader;
        if (Entry* l = find(leader)) ++l->bindings;
    }
    /// Unbinds follower `e`, moving its old leader's binding counter (when that one still lives) and its own link
    /// epoch (rule 12: what was forwarded to it under that binding no longer holds, even if it binds again to the
    /// same leader in the same call).
    void unbind(Entry& e) {
        if (e.boundTo == 0) return;
        if (Entry* l = find(e.boundTo)) ++l->bindings;
        e.boundTo = 0;
        ++e.linkEpoch;
    }

    /// Rule 12: sets `e`'s panic flag; its audio thread panics its scheduler at the top of its next block.
    static void raisePanic(Entry& e) {
        if (e.previews.scheduler != nullptr && e.previews.panic != nullptr) {
            e.previews.panic->store(true, std::memory_order_release);
        }
    }

    /// Rule 12: drops every recorded follower that no longer holds (removed, unbound from the originator, or bound
    /// again since, or it loaded a state: its `linkEpoch` moved), setting the panic flag of each such follower that
    /// still lives; drops the records left empty and those of an originator that is gone. Pushes nothing (any
    /// thread may run it). Called at the end of every method that can unbind or load: `remove`, `adoptState`,
    /// `adoptWithoutBlock`, `setFollow`, `follow`.
    void dropBrokenForwards() {
        for (auto it = forwards.begin(); it != forwards.end();) {
            const LinkToken origin = it->first.first;
            const bool originLives = find(origin) != nullptr;
            std::vector<Forwarded>& reached = it->second;
            reached.erase(std::remove_if(reached.begin(), reached.end(),
                                         [&](const Forwarded& r) {
                                             Entry* f = find(r.follower);
                                             const bool holds = originLives && f != nullptr && f->boundTo == origin &&
                                                                f->linkEpoch == r.epoch;
                                             if (!holds && f != nullptr) raisePanic(*f);
                                             return !holds;
                                         }),
                          reached.end());
            it = reached.empty() ? forwards.erase(it) : std::next(it);
        }
    }

    /// Rule 12: pushes `cmd` under each recorded follower's id. Returns whether at least one push succeeded.
    bool pushRecorded(const std::vector<Forwarded>& reached, const PreviewCmd& cmd) {
        bool any = false;
        for (const Forwarded& r : reached) {
            Entry* f = find(r.follower);
            if (f == nullptr || f->previews.scheduler == nullptr) continue;  // never: dropBrokenForwards
            PreviewCmd mapped = cmd;
            mapped.id = r.id;
            if (f->previews.scheduler->pushPreview(mapped)) any = true;
        }
        return any;
    }

    /// Rule 12: the ids recorded for follower `follower`, over every originator.
    std::set<int> heldIds(LinkToken follower) const {
        std::set<int> held;
        for (const auto& record : forwards) {
            for (const Forwarded& r : record.second) {
                if (r.follower == follower) held.insert(r.id);
            }
        }
        return held;
    }

    /// Rule 4, the only writes into a link buffer: `e`'s lane from `feed`, or empty for a null feed.
    static void write(Entry& e, const LinkFeed* feed) {
        const std::string_view laneUid = e.follow ? std::string_view(e.follow->laneUid) : std::string_view();
        const std::string_view documentId = e.follow ? std::string_view(e.follow->documentId) : std::string_view();
        if (fillSlotFromFeed(feed, laneUid, documentId, e.buffer->beginWrite(), e.publication)) {
            e.buffer->publish(e.publication.generation);
        }
    }

    /// The id entry `e` keeps in use (rule 9): its own while it is not following, its follow's leader's otherwise.
    static const std::string& idInUse(const Entry& e) { return e.follow ? e.follow->leaderId : e.id; }

    /// Rule 9, after every add, remove, id change and change of a follow's link (`resolve` calls it): drops the
    /// rows of the ids no longer in use, then gives every id in use without a row the lowest number no row holds,
    /// visiting the entries in token order.
    void relabel() {
        std::set<std::string_view> inUse;
        for (const auto& entry : entries) inUse.insert(idInUse(entry.second));
        std::set<std::uint64_t> held;
        for (auto it = labels.begin(); it != labels.end();) {
            if (inUse.count(it->first) == 0) {
                it = labels.erase(it);
            } else {
                held.insert(it->second);
                ++it;
            }
        }
        for (const auto& entry : entries) {
            const std::string& id = idInUse(entry.second);
            if (labels.count(id) != 0) continue;
            std::uint64_t n = 1;
            while (held.count(n) != 0) ++n;
            labels.emplace(id, n);
            held.insert(n);
        }
    }

    /// Rule 9: the label of `id`, which must be in use (every id a view names is: a leader's own, a follow's
    /// leader's). Empty, and asserts, for an id without a row.
    std::string labelOf(const std::string& id) const {
        const auto it = labels.find(id);
        assert(it != labels.end() && "LinkRegistry: every id a view names has a label (rule 9)");
        return it == labels.end() ? std::string() : labelFor(it->second);
    }

    /// Rule 2, after every add, remove, id change and start or stop of following. A follower bound to an instance
    /// that still lives and leads stays bound. One whose leader was removed or started following is unbound and
    /// emptied (rules 4 and 5). Then every unbound follower binds to the live leader holding its
    /// `follow.leaderId`, if there is one, and is written empty (always empty already: emptied when its follow
    /// changed or when it was unbound). Content comes only with the leader's next publish. Then the labels follow
    /// (rule 9, `relabel`). Returns whether any binding changed.
    bool resolve() {
        bool changed = false;
        for (auto& entry : entries) {
            Entry& e = entry.second;
            const LinkToken before = e.boundTo;
            if (!e.follow) {
                unbind(e);
            } else if (e.boundTo == 0 || !leads(e.boundTo)) {
                if (e.boundTo != 0) {
                    unbind(e);
                    write(e, nullptr);
                }
                for (const auto& candidate : entries) {
                    if (candidate.first != entry.first && !candidate.second.follow &&
                        candidate.second.id == e.follow->leaderId) {
                        bind(e, candidate.first);
                        write(e, nullptr);
                        break;
                    }
                }
            }
            changed = changed || e.boundTo != before;
        }
        relabel();
        return changed;
    }

    /// Gives `e` the follow `follow`. Returns whether the link changed (rule 4: set, changed or cleared); then `e`
    /// is unbound and emptied, and the caller must `resolve` (which also unbinds the followers of an instance that
    /// started following, rule 5). New hints alone are stored and change nothing else. An instance that starts
    /// following drops its feed (rule 5) into `dropped`, for the caller to release after the mutex.
    bool applyFollow(Entry& e, std::optional<LinkFollow> follow, std::shared_ptr<const LinkFeed>& dropped) {
        if (sameLink(e.follow, follow)) {
            e.follow = std::move(follow);
            return false;
        }
        if (!e.follow) dropped = std::move(e.feed);
        e.follow = std::move(follow);
        unbind(e);  // the old leader's counter moves
        write(e, nullptr);
        return true;
    }

    /// `setFollow`'s work, shared with `follow`: gives `e` (token `token`) the follow `follow`, re-resolves when the
    /// link changed (which relabels: an unchanged link leaves every id in use as it was), and adds the hooks of
    /// rule 6 to `hooks`. Returns whether something a view shows changed (the follow's link or shown hints, or a
    /// binding; labels change only with a link).
    bool setFollowLocked(LinkToken token, Entry& e, std::optional<LinkFollow> follow,
                         std::shared_ptr<const LinkFeed>& dropped, Hooks& hooks) {
        const bool sameFollow = sameShownFollow(e.follow, follow);
        const LinkToken before = e.boundTo;
        const bool changed = applyFollow(e, std::move(follow), dropped);
        const bool rebound = changed && resolve();
        if (changed || e.boundTo != before) collectHooks(token, {before, e.boundTo}, hooks);
        return !sameFollow || rebound;
    }

    /// Rule 6: adds the hooks of `leaders` (0 for none) to `out`, never `self`'s, each once.
    void collectHooks(LinkToken self, std::initializer_list<LinkToken> leaders, Hooks& out) const {
        for (const LinkToken leader : leaders) {
            if (leader == 0 || leader == self) continue;
            const Entry* e = find(leader);
            if (e == nullptr || e->hook == nullptr) continue;
            if (std::find(out.begin(), out.end(), e->hook) != out.end()) continue;
            out.push_back(e->hook);
        }
    }

    /// Followers bound to `leader` whose link names lane `laneUid` of document `documentId`.
    int followersOf(LinkToken leader, std::string_view documentId, std::string_view laneUid) const {
        int n = 0;
        for (const auto& entry : entries) {
            const Entry& e = entry.second;
            if (e.boundTo == leader && e.follow->documentId == documentId && e.follow->laneUid == laneUid) ++n;
        }
        return n;
    }

    LinkLeaderView leaderView(LinkToken token, const Entry& e) const {
        LinkLeaderView v;
        v.id = e.id;
        v.label = labelOf(e.id);
        if (e.feed) {
            v.documentId = e.feed->documentId;
            v.lineage = e.feed->lineage;
            v.lanes.reserve(e.feed->lanes.size());
            for (const LinkLane& lane : e.feed->lanes) {
                v.lanes.push_back(LinkLaneView{lane.uid, lane.name, lane.role, lane.chords, lane.audible,
                                               followersOf(token, e.feed->documentId, lane.uid)});
            }
        }
        return v;
    }

    /// Rule 8: the instance `f`'s `switchTo` names, or null. `f` follows, is bound, and became a copy by a load
    /// that carried a follow; the answer is the newest copy of `f`'s leader that is not following and was made a
    /// copy within kPasteWindowMs of `f`, in either order.
    const Entry* switchTarget(const Entry& f) const {
        if (!f.follow || f.boundTo == 0 || f.copiedFrom == 0 || !f.copyCarriedFollow) return nullptr;
        const Entry* newest = nullptr;
        for (const auto& entry : entries) {
            const Entry& c = entry.second;
            if (c.copiedFrom != f.boundTo || c.follow) continue;
            const std::int64_t apart =
                c.copyMadeAtMs > f.copyMadeAtMs ? c.copyMadeAtMs - f.copyMadeAtMs : f.copyMadeAtMs - c.copyMadeAtMs;
            if (apart > kPasteWindowMs) continue;
            if (newest == nullptr || c.copyOrder > newest->copyOrder) newest = &c;
        }
        return newest;
    }

    /// Rule 11: the instance `e` (token `token`) is a leader in its own right: a follower is bound to it, or some
    /// follower's `switchTo` names it.
    bool leadsOthers(LinkToken token, const Entry& e) const {
        for (const auto& entry : entries) {
            if (entry.second.boundTo == token || switchTarget(entry.second) == &e) return true;
        }
        return false;
    }

    /// Rule 7, for a bound follower.
    static LinkStatus boundStatus(const LinkFollow& follow, const LinkFeed* feed) {
        if (feed == nullptr || feed->documentId != follow.documentId) return LinkStatus::ProjectChanged;
        const auto lane = std::find_if(feed->lanes.begin(), feed->lanes.end(),
                                       [&](const LinkLane& l) { return l.uid == follow.laneUid; });
        if (lane == feed->lanes.end()) return LinkStatus::LaneMissing;
        return lane->audible ? LinkStatus::Playing : LinkStatus::Muted;
    }

    void moved() { serial.fetch_add(1, std::memory_order_acq_rel); }
};

LinkRegistry::LinkRegistry() : LinkRegistry(Clock{}) {}

LinkRegistry::LinkRegistry(Clock clock) : state_(std::make_unique<State>()) {
    if (!clock) {
        clock = [] {
            return static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                 std::chrono::steady_clock::now().time_since_epoch())
                                                 .count());
        };
    }
    state_->clock = std::move(clock);
}

LinkRegistry::~LinkRegistry() = default;

LinkRegistry& LinkRegistry::shared() {
    static LinkRegistry* const registry = new LinkRegistry();  // never destroyed
    return *registry;
}

LinkToken LinkRegistry::add(std::string id, TripleBuffer<SnapshotSlot>* linkBuffer, Hook hook,
                           LinkPreviewSink previews) {
    assert(linkBuffer != nullptr && "LinkRegistry::add: every instance has a link buffer");
    assert((previews.scheduler == nullptr || previews.panic != nullptr) &&
           "LinkRegistry::add: a preview sink with a scheduler has a panic flag");
    if (previews.panic == nullptr) previews.scheduler = nullptr;  // without asserts: takes no forwards
    std::shared_ptr<const Hook> shared = hook ? std::make_shared<const Hook>(std::move(hook)) : nullptr;
    State& s = *state_;
    const std::lock_guard<std::mutex> lock(s.mutex);
    const LinkToken token = ++s.lastToken;
    Entry e;
    e.id = std::move(id);
    e.buffer = linkBuffer;
    e.hook = std::move(shared);
    e.previews = previews;
    s.entries.emplace(token, std::move(e));
    s.resolve();  // binds the followers waiting for its id, and labels it (rule 9)
    s.moved();
    return token;
}

LinkRegistry::Hooks LinkRegistry::remove(LinkToken token) {
    std::optional<Entry> dead;  // its hook and feed are released after the mutex
    Hooks hooks;
    State& s = *state_;
    const std::lock_guard<std::mutex> lock(s.mutex);
    const auto it = s.entries.find(token);
    if (it == s.entries.end()) return hooks;
    s.collectHooks(token, {it->second.boundTo}, hooks);
    s.unbind(it->second);  // its leader's counter moves
    dead.emplace(std::move(it->second));
    s.entries.erase(it);
    s.resolve();  // its followers are unbound and emptied
    s.dropBrokenForwards();  // rule 12: its records, and it from others' (its own sink is gone: never touched)
    s.moved();
    return hooks;
}

LinkRegistry::Adopted LinkRegistry::adoptState(LinkToken token, std::string loadedId, std::string freshId,
                                               std::optional<LinkFollow> follow) {
    std::shared_ptr<const LinkFeed> dropped;  // released after the mutex
    Adopted out;
    State& s = *state_;
    const std::lock_guard<std::mutex> lock(s.mutex);
    Entry* e = s.find(token);
    if (e == nullptr) return out;

    const std::string idBefore = e->id;
    const LinkToken copiedFromBefore = e->copiedFrom;
    const std::uint64_t copyOrderBefore = e->copyOrder;
    const bool sameFollow = sameShownFollow(e->follow, follow);

    // Rule 3, in order. Whether it is a copy is the outcome of this load alone.
    const bool hasDependents = std::any_of(s.entries.begin(), s.entries.end(),
                                           [&](const auto& entry) { return entry.second.boundTo == token; });
    const auto holder = std::find_if(s.entries.begin(), s.entries.end(), [&](const auto& entry) {
        return entry.first != token && entry.second.id == loadedId;
    });
    if (hasDependents) {
        e->copiedFrom = 0;
    } else if (holder != s.entries.end()) {
        e->id = std::move(freshId);
        e->copiedFrom = holder->first;
        e->copyOrder = ++s.copiesMade;
        e->copyCarriedFollow = follow.has_value();  // rule 8: copied as a follower
        e->copyMadeAtMs = s.clock();
        out.copy = true;
    } else {
        e->id = std::move(loadedId);
        e->copiedFrom = 0;
    }

    const LinkToken before = e->boundTo;
    ++e->linkEpoch;  // rule 12: a follower that loads a state is released, even with its follow as it was
    const bool changed = s.applyFollow(*e, std::move(follow), dropped);
    const bool rebound = s.resolve();  // always: the id may have changed (which may relabel, rule 9)
    if (changed || e->boundTo != before) s.collectHooks(token, {before, e->boundTo}, out.hooks);
    s.dropBrokenForwards();
    out.id = e->id;
    if (e->id != idBefore || e->copiedFrom != copiedFromBefore || e->copyOrder != copyOrderBefore || !sameFollow ||
        rebound) {
        s.moved();
    }
    return out;
}

LinkRegistry::Hooks LinkRegistry::adoptWithoutBlock(LinkToken token) {
    std::shared_ptr<const LinkFeed> dropped;  // released after the mutex
    Hooks hooks;
    State& s = *state_;
    const std::lock_guard<std::mutex> lock(s.mutex);
    Entry* e = s.find(token);
    if (e == nullptr) return hooks;
    const bool wasCopy = e->copiedFrom != 0;
    e->copiedFrom = 0;  // rule 3: its document is no copy of the original's now
    ++e->linkEpoch;     // rule 12: a state load
    const bool followChanged = s.setFollowLocked(token, *e, std::nullopt, dropped, hooks);
    s.dropBrokenForwards();
    if (wasCopy || followChanged) s.moved();
    return hooks;
}

LinkRegistry::Hooks LinkRegistry::setFollow(LinkToken token, std::optional<LinkFollow> follow) {
    std::shared_ptr<const LinkFeed> dropped;  // released after the mutex
    Hooks hooks;
    State& s = *state_;
    const std::lock_guard<std::mutex> lock(s.mutex);
    Entry* e = s.find(token);
    if (e == nullptr) return hooks;
    if (s.setFollowLocked(token, *e, std::move(follow), dropped, hooks)) s.moved();
    s.dropBrokenForwards();
    return hooks;
}

LinkFollowResult LinkRegistry::follow(LinkToken token, std::string_view leaderId, std::string_view laneUid,
                                      std::string_view leaderLineage, Hooks& hooks) {
    hooks.clear();
    std::shared_ptr<const LinkFeed> dropped;  // released after the mutex
    State& s = *state_;
    const std::lock_guard<std::mutex> lock(s.mutex);
    Entry* e = s.find(token);
    if (e == nullptr) return LinkFollowResult::UnknownLeader;
    const auto leader = std::find_if(s.entries.begin(), s.entries.end(), [&](const auto& entry) {
        return entry.first != token && !entry.second.follow && entry.second.id == leaderId;
    });
    if (leader == s.entries.end()) return LinkFollowResult::UnknownLeader;
    const LinkFeed* feed = leader->second.feed.get();
    if (feed == nullptr || feed->lineage != leaderLineage) return LinkFollowResult::StaleLeader;
    const auto lane = std::find_if(feed->lanes.begin(), feed->lanes.end(),
                                   [&](const LinkLane& l) { return l.uid == laneUid; });
    if (lane == feed->lanes.end()) return LinkFollowResult::UnknownLane;

    LinkFollow link;
    link.leaderId = leader->second.id;
    link.documentId = feed->documentId;
    link.laneUid = lane->uid;
    link.laneName = lane->name;
    link.laneRole = lane->chords ? std::string() : std::string(trackRoleName(lane->role));
    link.leaderLabel = s.labelOf(leader->second.id);
    if (s.setFollowLocked(token, *e, std::move(link), dropped, hooks)) s.moved();
    s.dropBrokenForwards();
    return LinkFollowResult::Applied;
}

LinkFollowed LinkRegistry::followedLanes(LinkToken leader, std::string_view documentId,
                                        const std::vector<std::string>& laneUids) const {
    LinkFollowed out;
    const State& s = *state_;
    const std::lock_guard<std::mutex> lock(s.mutex);
    const Entry* e = s.find(leader);
    if (e == nullptr) return out;
    out.bindings = e->bindings;
    if (e->follow) return out;
    for (const std::string& uid : laneUids) {
        if (std::find(out.lanes.begin(), out.lanes.end(), uid) != out.lanes.end()) continue;
        if (s.followersOf(leader, documentId, uid) > 0) out.lanes.push_back(uid);
    }
    return out;
}

void LinkRegistry::publish(LinkToken leader, std::shared_ptr<const LinkFeed> feed) {
    std::shared_ptr<const LinkFeed> previous;  // released after the mutex
    State& s = *state_;
    const std::lock_guard<std::mutex> lock(s.mutex);
    Entry* e = s.find(leader);
    if (e == nullptr) return;
    assert(!e->follow && "LinkRegistry::publish: a follower publishes no feed");
    assert(feed != nullptr && "LinkRegistry::publish: a leader with nothing to publish does not call it");
    if (e->follow || feed == nullptr) return;
    previous = std::move(e->feed);
    e->feed = std::move(feed);
    for (auto& entry : s.entries) {
        if (entry.second.boundTo == leader) State::write(entry.second, e->feed.get());
    }
    s.moved();
}

LinkForward LinkRegistry::forwardPreview(LinkToken token, std::string_view documentId, std::string_view laneUid,
                                         const PreviewCmd& cmd) {
    State& s = *state_;
    const std::lock_guard<std::mutex> lock(s.mutex);
    const Entry* e = s.find(token);
    if (e == nullptr) return LinkForward::NotForwarded;
    const std::pair<LinkToken, int> key{token, cmd.id};
    const auto recorded = s.forwards.find(key);

    if (cmd.kind != PreviewCmd::Start) {
        // An Extend or a Stop follows its Start: exactly the recorded followers, under their ids.
        if (recorded == s.forwards.end()) return LinkForward::NotForwarded;
        const bool pushed = s.pushRecorded(recorded->second, cmd);
        if (cmd.kind == PreviewCmd::Stop) s.forwards.erase(recorded);  // a full ring: the watchdog ends it there
        return pushed ? LinkForward::Forwarded : LinkForward::QueueFull;
    }

    // A Start of an id already recorded first ends that record, as a scheduler releases a preview whose id
    // starts again, whether or not this Start is forwarded.
    if (recorded != s.forwards.end()) {
        PreviewCmd stop{};
        stop.kind = PreviewCmd::Stop;
        s.pushRecorded(recorded->second, stop);
        s.forwards.erase(recorded);
    }
    if (e->follow || laneUid.empty()) return LinkForward::NotForwarded;

    // The followers on that lane: bound to `token`, the follow naming `documentId` and `laneUid` (followersOf's
    // test), and taking forwards.
    std::vector<State::Forwarded> reached;
    bool offered = false;
    for (auto& entry : s.entries) {
        Entry& f = entry.second;
        if (f.boundTo != token || f.previews.scheduler == nullptr || f.follow->documentId != documentId ||
            f.follow->laneUid != laneUid) {
            continue;
        }
        offered = true;
        const std::optional<int> id = nextForwardedPreviewId(f.nextPreviewId, s.heldIds(entry.first));
        if (!id) continue;  // every reserved id still recorded for it: as a full ring
        PreviewCmd mapped = cmd;
        mapped.id = *id;
        if (f.previews.scheduler->pushPreview(mapped)) {
            reached.push_back(State::Forwarded{entry.first, *id, f.linkEpoch});
        }
    }
    if (!offered) return LinkForward::NotForwarded;
    if (reached.empty()) return LinkForward::QueueFull;
    s.forwards.emplace(key, std::move(reached));
    return LinkForward::Forwarded;
}

int LinkRegistry::panicFollowers(LinkToken token) {
    State& s = *state_;
    const std::lock_guard<std::mutex> lock(s.mutex);
    if (s.find(token) == nullptr) return 0;
    int flagged = 0;
    for (auto& entry : s.entries) {
        Entry& f = entry.second;
        if (f.boundTo != token || f.previews.scheduler == nullptr) continue;
        State::raisePanic(f);
        ++flagged;
    }
    for (auto it = s.forwards.begin(); it != s.forwards.end();) {
        it = it->first.first == token ? s.forwards.erase(it) : std::next(it);
    }
    return flagged;
}

std::optional<int> nextForwardedPreviewId(int& cursor, const std::set<int>& held, int first, int last) {
    assert(first <= last && "nextForwardedPreviewId: a range");
    assert(cursor >= first && cursor <= last && "nextForwardedPreviewId: the cursor lies in the range");
    if (cursor < first || cursor > last) cursor = first;
    // Ids from `cursor` on are distinct until the range is exhausted; among held.size() + 1 distinct ids one is
    // free. The range's size, as an unsigned count, bounds the tries as well (no overflow for INT_MIN..INT_MAX).
    const std::uint64_t rangeSize =
        static_cast<std::uint64_t>(static_cast<std::int64_t>(last) - static_cast<std::int64_t>(first)) + 1;
    const std::uint64_t tries = std::min<std::uint64_t>(rangeSize, static_cast<std::uint64_t>(held.size()) + 1);
    for (std::uint64_t n = 0; n < tries; ++n) {
        const int id = cursor;
        cursor = id == last ? first : id + 1;
        if (held.count(id) == 0) return id;
    }
    return std::nullopt;
}

std::uint64_t LinkRegistry::serial() const noexcept {
    return state_->serial.load(std::memory_order_acquire);
}

LinkView LinkRegistry::view(LinkToken token) const {
    LinkView v;
    const State& s = *state_;
    const std::lock_guard<std::mutex> lock(s.mutex);
    const Entry* e = s.find(token);
    if (e == nullptr) return v;
    v.id = e->id;
    if (!e->follow) v.label = s.labelOf(e->id);  // rule 9: a follower has no label of its own
    if (e->follow) {
        v.follow = *e->follow;
        const Entry* leader = s.find(e->boundTo);
        if (e->boundTo == 0 || leader == nullptr) {
            v.status = LinkStatus::Waiting;
        } else {
            v.follow->leaderId = leader->id;
            v.status = State::boundStatus(*e->follow, leader->feed.get());
            if (const Entry* target = s.switchTarget(*e)) {
                v.switchTo = LinkInstanceRef{target->id, s.labelOf(target->id)};
            }
        }
        v.follow->leaderLabel = s.labelOf(v.follow->leaderId);  // bound or waiting: the table's (rule 9)
    }
    if (!e->follow && e->copiedFrom != 0) {  // rule 11
        const Entry* original = s.find(e->copiedFrom);
        if (original != nullptr && !original->follow && !s.leadsOthers(token, *e)) {
            v.copyOf = LinkInstanceRef{original->id, s.labelOf(original->id)};
        }
    }
    for (const auto& entry : s.entries) {
        if (entry.first != token && !entry.second.follow) v.leaders.push_back(s.leaderView(entry.first, entry.second));
    }
    if (!e->follow && e->feed) {
        for (LinkLaneView& lane : s.leaderView(token, *e).lanes) {
            if (lane.followers > 0) v.routed.push_back(std::move(lane));
        }
    }
    return v;
}

// --- the source -----------------------------------------------------------------------------------------------

LinkSource::Flags LinkSource::select(TripleBuffer<SnapshotSlot>& own, TripleBuffer<SnapshotSlot>& link,
                                     bool following, Scheduler& scheduler) noexcept {
    // Every acquire before the one adopt (Snapshot's lifetime rule): the link buffer only while following.
    const bool ownFresh = own.acquire() != nullptr;
    const bool linkFresh = following && link.acquire() != nullptr;

    const Source selected = following ? Source::Link : Source::Own;
    if ((following ? linkFresh : ownFresh) || selected != playing_) {
        TripleBuffer<SnapshotSlot>& buffer = following ? link : own;
        Snapshot snapshot{nullptr, 0, 0.0, true, following ? kLinkGenerationBase : 0u};  // never published
        if (const SnapshotSlot* slot = buffer.current()) {
            snapshot =
                Snapshot{slot->events.data(), slot->count, slot->endBeat, slot->loop, buffer.currentGeneration()};
        }
        scheduler.adopt(snapshot);
        playing_ = selected;
        generation_ = snapshot.generation;
        ++adoptions_;
    }

    // `playing_` is `selected` now, so `link` is read only while following.
    const SnapshotSlot* played = playing_ == Source::Link ? link.current() : own.current();
    const SnapshotSlot* ownSlot = own.current();
    Flags flags;
    flags.followHost = played != nullptr ? played->followHost : true;
    flags.thru = ownSlot != nullptr ? ownSlot->midiThru : true;
    return flags;
}

// --- the endpoint ---------------------------------------------------------------------------------------------

LinkEndpoint::LinkEndpoint(LinkRegistry& registry, std::string instanceId, UidGenerator makeUid,
                           LinkRegistry::Hook hook, LinkPreviewSink previews)
    : registry_(registry), makeUid_(std::move(makeUid)), linkBuffer_(std::make_unique<TripleBuffer<SnapshotSlot>>()) {
    documentId_ = makeUid_();
    token_ = registry_.add(std::move(instanceId), linkBuffer_.get(), std::move(hook), previews);
}

LinkEndpoint::~LinkEndpoint() {
    const LinkRegistry::Hooks hooks = registry_.remove(token_);  // first: nothing writes the link buffer after it
    for (const auto& hook : hooks) {
        if (hook != nullptr && *hook) (*hook)();
    }
}

namespace {

/// `fillSlot` into the buffer's back slot and its publish when that returned true. Returns whether it published.
bool fillAndPublish(const Document& doc, const LaneSelection& selection, TripleBuffer<SnapshotSlot>& buffer,
                    Publication& last) {
    if (!fillSlot(doc, selection, buffer.beginWrite(), last)) return false;
    buffer.publish(last.generation);
    return true;
}

}  // namespace

bool LinkEndpoint::publishOwn(const DocumentStore& store, const std::string& lineage,
                              TripleBuffer<SnapshotSlot>& ownBuffer, Publication& ownPublication) {
    const Document& doc = store.doc();
    const std::uint64_t revision = store.revision();
    uids_.cover(doc, store.historyTrackIds(), makeUid_);
    if (following()) {
        ownExcluded_.clear();  // the whole own document: nothing excluded
        return fillAndPublish(doc, LaneSelection{}, ownBuffer, ownPublication);
    }

    FeedKey key;
    key.revision = revision;
    key.documentId = documentId_;
    key.lineage = lineage;
    key.loop = doc.progressionLoop;
    key.followHost = doc.followHost;
    key.laneUids.reserve(1 + doc.tracks.size());
    key.audible.reserve(1 + doc.tracks.size());
    key.laneUids.emplace_back(kChordsLaneUid);
    key.audible.push_back(chordsAudible(doc));
    for (std::size_t i = 0; i < doc.tracks.size(); ++i) {
        std::string uid = uids_.uidOf(doc.tracks[i].id);
        if (uid.empty()) continue;  // uncovered (LaneUids::cover): not in the feed, never followed
        key.laneUids.push_back(std::move(uid));
        key.audible.push_back(trackAudible(doc, i));
    }
    // Followed in the document being published: a follower linked to another document does not count. With the
    // binding counter from the same critical section: a follower newly bound to a lane already followed changes no
    // lane, but needs this publish to get its content (rule 4).
    LinkFollowed followed = registry_.followedLanes(token_, documentId_, key.laneUids);
    key.followed = std::move(followed.lanes);
    key.bindings = followed.bindings;

    // The lanes as keys of `doc`: what an own publication excludes. Kept by key between publishes, so that a lane
    // let go stays let go across a document replacement until its follower was emptied, whether or not the new
    // document's lane table gives that track number the old uid.
    std::vector<LaneKey> followedKeys;
    for (const std::string& uid : key.followed) {
        if (const std::optional<LaneKey> lane = uids_.laneOf(doc, uid)) followedKeys.push_back(*lane);
    }
    const auto isFollowed = [&](const LaneKey& lane) {
        return std::find(followedKeys.begin(), followedKeys.end(), lane) != followedKeys.end();
    };

    // 1. Let go first: the followed lanes, and every lane the last own publication excluded that is still a lane.
    std::vector<LaneKey> letGo = followedKeys;
    for (const LaneKey& lane : ownExcluded_) {
        if (!isFollowed(lane) && (lane.chords || holdsTrack(doc, lane.trackId))) letGo.push_back(lane);
    }
    bool published =
        fillAndPublish(doc, LaneSelection{LaneSelection::Mode::AllExcept, letGo}, ownBuffer, ownPublication);
    ownExcluded_ = letGo;
    if (probe_) probe_();

    // 2. The feed: serves newly bound followers, empties those whose lane no longer resolves.
    const bool feedChanged = !lastFeed_ || key.revision != lastFeed_->revision ||
                             key.documentId != lastFeed_->documentId || key.lineage != lastFeed_->lineage ||
                             key.loop != lastFeed_->loop || key.followHost != lastFeed_->followHost ||
                             key.laneUids != lastFeed_->laneUids || key.audible != lastFeed_->audible ||
                             key.followed != lastFeed_->followed || key.bindings != lastFeed_->bindings;
    if (feedChanged) {
        registry_.publish(token_, buildLinkFeed(doc, documentId_, uids_, lineage, key.followed));
        lastFeed_ = key;
    }
    if (probe_) probe_();

    // 3. Take back what nobody follows any more, now that its old follower is silent.
    const bool comesBack = std::any_of(ownExcluded_.begin(), ownExcluded_.end(),
                                       [&](const LaneKey& lane) { return !isFollowed(lane); });
    if (comesBack) {
        published =
            fillAndPublish(doc, LaneSelection{LaneSelection::Mode::AllExcept, followedKeys}, ownBuffer,
                           ownPublication) ||
            published;
        ownExcluded_ = followedKeys;
    }
    return published;
}

void LinkEndpoint::setPublicationProbe(std::function<void()> probe) {
    probe_ = std::move(probe);
}

LinkEndpoint::LinkResult LinkEndpoint::follow(std::string_view leaderId, std::string_view laneUid,
                                              std::string_view leaderLineage, LinkRegistry::Hooks& hooks) {
    const LinkResult result = registry_.follow(token_, leaderId, laneUid, leaderLineage, hooks);
    if (result == LinkResult::Applied) {
        following_.store(true, std::memory_order_release);  // after the registry wrote the link buffer
        lastFeed_.reset();                                    // the registry dropped the feed (rule 5)
    }
    return result;
}

void LinkEndpoint::unfollow(LinkRegistry::Hooks& hooks) {
    hooks = registry_.setFollow(token_, std::nullopt);
    if (following()) {
        following_.store(false, std::memory_order_release);
        lastFeed_.reset();  // a leader again, with no feed stored: the next publishOwn publishes one
    }
}

LinkEndpoint::State LinkEndpoint::state() const {
    LinkView view = registry_.view(token_);
    State s;
    s.id = std::move(view.id);
    s.documentId = documentId_;
    s.lanes = uids_.entries();
    s.follow = std::move(view.follow);
    return s;
}

void LinkEndpoint::adopt(const State& loaded, LinkRegistry::Hooks& hooks) {
    LinkRegistry::Adopted adopted = registry_.adoptState(token_, loaded.id, makeUid_(), loaded.follow);
    documentId_ = loaded.documentId;
    uids_.replace(loaded.lanes);
    following_.store(loaded.follow.has_value(), std::memory_order_release);  // the registry wrote it first
    lastFeed_.reset();
    hooks = std::move(adopted.hooks);
}

void LinkEndpoint::adoptWithoutBlock(LinkRegistry::Hooks& hooks) {
    hooks = registry_.adoptWithoutBlock(token_);             // empties the link buffer if it followed
    following_.store(false, std::memory_order_release);      // after the registry wrote it
    documentId_ = makeUid_();
    uids_.replace({});
    lastFeed_.reset();
}

LinkForward LinkEndpoint::forwardPreview(std::optional<int> trackId, const PreviewCmd& cmd) {
    std::string laneUid;  // empty: no lane (an Extend or a Stop does not need one)
    if (cmd.kind == PreviewCmd::Start) {
        if (!trackId) {
            laneUid = std::string(kChordsLaneUid);
        } else if (*trackId >= 1) {
            laneUid = uids_.uidOf(*trackId);  // empty when the track has no uid
        }
    }
    return registry_.forwardPreview(token_, documentId_, laneUid, cmd);
}

int LinkEndpoint::panicFollowers() {
    return registry_.panicFollowers(token_);
}

std::uint64_t LinkEndpoint::serial() const noexcept {
    return registry_.serial();
}

LinkView LinkEndpoint::view() const {
    LinkView v = registry_.view(token_);
    for (LinkLaneView& lane : v.routed) lane.trackId = uids_.trackOf(lane.uid);
    return v;
}

}  // namespace chordlab
