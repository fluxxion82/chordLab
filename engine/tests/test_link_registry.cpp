#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/compiler.h"
#include "chordlab/document.h"
#include "chordlab/link.h"
#include "chordlab/publication.h"
#include "chordlab/tracks.h"
#include "chordlab/triple_buffer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace chordlab;

namespace {

using Events = std::vector<NoteEvent>;
using Hooks = LinkRegistry::Hooks;
using Names = std::vector<std::string>;

constexpr std::uint32_t kG = kLinkGenerationBase;

Chord chord(PitchClass root, const char* typeId) {
    return Chord{root, &chordType(typeId)};
}

NoteSpec ns(int pitch, std::int64_t startTick, std::int64_t lengthTicks, int velocity = 100) {
    return NoteSpec{pitch, startTick, lengthTicks, velocity};
}

/// Adds a track named `name` with `role` holding `specs` and returns its id.
int addTrackWith(DocumentStore& store, TrackRole role, std::string name, const std::vector<NoteSpec>& specs) {
    const int id = store.addTrack(role, std::move(name));
    REQUIRE(id != kNoTrackId);
    if (!specs.empty()) {
        std::string why;
        REQUIRE_MESSAGE(store.insertNotes(id, specs, &why).size() == specs.size(), why);
    }
    return id;
}

/// A deterministic generator: "uid-1", "uid-2", ... Copies share the counter.
LaneUids::Generator counter() {
    auto n = std::make_shared<int>(0);
    return [n] { return "uid-" + std::to_string(++*n); };
}

Events eventsOf(const SnapshotSlot& slot) {
    return Events(slot.events.begin(), slot.events.begin() + static_cast<std::ptrdiff_t>(slot.count));
}

/// C and Am of four beats, track "Low" (bass: 36 on beat 0) and track "High" (lead: 72 on beat 2), every lane
/// audible. Every Song's tracks get "uid-1" and "uid-2".
struct Song {
    DocumentStore store;
    int a = 0;
    int b = 0;
    LaneUids uids;
    std::string uidA;
    std::string uidB;

    Song() {
        store.appendChord(chord(0, "maj"), 4.0, 100);
        store.appendChord(chord(9, "min"), 4.0, 100);
        a = addTrackWith(store, TrackRole::Bass, "Low", {ns(36, 0, 960)});
        b = addTrackWith(store, TrackRole::Lead, "High", {ns(72, 1920, 480)});
        REQUIRE(uids.cover(store.doc(), counter()));
        uidA = uids.uidOf(a);
        uidB = uids.uidOf(b);
    }

    std::shared_ptr<const LinkFeed> feed(const Names& followed, const std::string& documentId = "doc-1") const {
        return buildLinkFeed(store.doc(), documentId, uids, "lineage-1", "feed label", followed);
    }

    /// What lane `uid` plays alone.
    Events lane(const std::string& uid) const {
        const std::optional<LaneKey> key = uids.laneOf(store.doc(), uid);
        REQUIRE(key.has_value());
        return compilePlaybackFor(store.doc(), LaneSelection{LaneSelection::Mode::Only, {*key}}).events;
    }

    Names all() const { return {std::string(kChordsLaneUid), uidA, uidB}; }
};

/// One instance as the tests drive it: a link buffer of its own (on the heap: ~864 KB) and its token.
struct Inst {
    std::string name;
    std::unique_ptr<TripleBuffer<SnapshotSlot>> buffer;
    LinkToken token = 0;
};

/// What the consumer of a link buffer sees after one `acquire`.
struct Taken {
    bool fresh = false;            // the acquire returned a slot
    std::uint32_t generation = 0;  // currentGeneration after it
    std::size_t count = 0;         // of the current slot (0 before any)
    Events events;
};

/// Acquires on `inst`'s link buffer, as its audio thread would.
Taken take(Inst& inst) {
    Taken t;
    t.fresh = inst.buffer->acquire() != nullptr;
    t.generation = inst.buffer->currentGeneration();
    if (const SnapshotSlot* slot = inst.buffer->current()) {
        t.count = slot->count;
        t.events = eventsOf(*slot);
    }
    return t;
}

/// A follow of `laneUid` of the leader `leaderId` in `documentId`, with hints that never decide anything.
LinkFollow follow(std::string leaderId, std::string laneUid, std::string documentId = "doc-1") {
    LinkFollow f;
    f.leaderId = std::move(leaderId);
    f.documentId = std::move(documentId);
    f.laneUid = std::move(laneUid);
    f.laneName = "hint name";
    f.laneRole = "hint role";
    f.leaderLabel = "hint label";
    return f;
}

/// A registry, its instances, and a log the instances' hooks write their names into.
struct World {
    LinkRegistry reg;
    std::shared_ptr<Names> log = std::make_shared<Names>();
    std::vector<std::unique_ptr<Inst>> insts;

    /// Adds an instance named `name` under the id `id` (the name when empty).
    Inst& add(const std::string& name, const std::string& id = "") {
        auto inst = std::make_unique<Inst>();
        inst->name = name;
        inst->buffer = std::make_unique<TripleBuffer<SnapshotSlot>>();
        const std::shared_ptr<Names> sink = log;
        inst->token = reg.add(id.empty() ? name : id, inst->buffer.get(), [sink, name] { sink->push_back(name); });
        REQUIRE(inst->token != 0);
        insts.push_back(std::move(inst));
        return *insts.back();
    }

    /// Runs `hooks` and returns the names of the instances they belong to, sorted (duplicates kept).
    Names run(const Hooks& hooks) {
        log->clear();
        for (const auto& hook : hooks) {
            REQUIRE(hook != nullptr);
            REQUIRE(static_cast<bool>(*hook));
            (*hook)();
        }
        Names names = *log;
        std::sort(names.begin(), names.end());
        return names;
    }

    LinkView view(const Inst& inst) const { return reg.view(inst.token); }
    /// The follow `inst`'s view shows (it must have one).
    LinkFollow shown(const Inst& inst) const {
        const LinkView v = reg.view(inst.token);
        REQUIRE(v.follow.has_value());
        return *v.follow;
    }
    /// The id `switchTo` names in `inst`'s view, or empty.
    std::string switchId(const Inst& inst) const {
        const LinkView v = reg.view(inst.token);
        return v.switchTo ? v.switchTo->id : std::string();
    }
    LinkStatus status(const Inst& inst) const { return reg.view(inst.token).status; }
    Names followed(const Inst& leader, const Song& s, const std::string& documentId = "doc-1") const {
        return reg.followedLanes(leader.token, documentId, s.all()).lanes;
    }
};

Names leaderIds(const LinkView& view) {
    Names ids;
    for (const LinkLeaderView& leader : view.leaders) ids.push_back(leader.id);
    return ids;
}

}  // namespace

// --- rule 1: leaders and followers ------------------------------------------------------------------------------

TEST_CASE("link registry: an instance with a follow never leads, one without leads") {
    Song s;
    World w;
    Inst& a = w.add("A");
    Inst& b = w.add("B");
    Inst& c = w.add("C");
    CHECK(leaderIds(w.view(a)) == Names{"B", "C"});  // every other instance, in the order of add
    CHECK(leaderIds(w.view(b)) == Names{"A", "C"});
    CHECK(w.status(a) == LinkStatus::NotFollowing);

    w.reg.setFollow(b.token, follow("A", s.uidA));
    CHECK(leaderIds(w.view(a)) == Names{"C"});
    CHECK(leaderIds(w.view(c)) == Names{"A"});
    CHECK(leaderIds(w.view(b)) == Names{"A", "C"});
    CHECK(w.status(b) != LinkStatus::NotFollowing);
    CHECK(w.status(a) == LinkStatus::NotFollowing);

    // Nothing binds to a follower, even under its id.
    Inst& d = w.add("D");
    w.reg.setFollow(d.token, follow("B", s.uidA));
    CHECK(w.status(d) == LinkStatus::Waiting);
    CHECK(w.followed(b, s).empty());

    // B leads again: D binds to it.
    w.reg.setFollow(b.token, std::nullopt);
    CHECK(w.status(b) == LinkStatus::NotFollowing);
    CHECK(w.status(d) == LinkStatus::ProjectChanged);  // bound; B has published no feed
    CHECK(w.followed(b, s) == Names{s.uidA});
    CHECK(leaderIds(w.view(a)) == Names{"B", "C"});
}

// --- rule 2: binding --------------------------------------------------------------------------------------------

TEST_CASE("link registry: a follower added after its leader binds to it at once") {
    Song s;
    World w;
    Inst& l = w.add("L");
    w.reg.publish(l.token, s.feed({}));
    Inst& f = w.add("F");
    CHECK(w.status(f) == LinkStatus::NotFollowing);
    w.reg.setFollow(f.token, follow("L", s.uidA));
    CHECK(w.status(f) == LinkStatus::Playing);
    CHECK(w.followed(l, s) == Names{s.uidA});
}

TEST_CASE("link registry: a follower added before its leader waits, and binds when the leader takes the id") {
    Song s;
    World w;
    Inst& f = w.add("F");
    const LinkRegistry::Adopted adopted = w.reg.adoptState(f.token, "F", "F-fresh", follow("L", s.uidA));
    CHECK(adopted.id == "F");
    CHECK_FALSE(adopted.copy);
    CHECK(w.status(f) == LinkStatus::Waiting);

    SUBCASE("the leader's state load gives it the id") {
        Inst& l = w.add("L-new", "ctor-l");  // a constructor's fresh id
        CHECK(w.status(f) == LinkStatus::Waiting);
        CHECK(w.followed(l, s).empty());
        const LinkRegistry::Adopted la = w.reg.adoptState(l.token, "L", "L-fresh", std::nullopt);
        CHECK(la.id == "L");
        CHECK_FALSE(la.copy);
        CHECK(w.status(f) == LinkStatus::ProjectChanged);  // bound; no feed yet
        CHECK(w.followed(l, s) == Names{s.uidA});
    }
    SUBCASE("the leader is constructed with the id") {
        Inst& l = w.add("L");
        CHECK(w.status(f) == LinkStatus::ProjectChanged);
        CHECK(w.followed(l, s) == Names{s.uidA});
    }
}

TEST_CASE("link registry: a removed leader empties its followers, which wait with their link kept and rebind to "
          "its successor") {
    Song s;
    World w;
    Inst& l = w.add("L");
    Inst& f = w.add("F");
    const LinkFollow link = follow("L", s.uidA);
    w.reg.setFollow(f.token, link);
    Taken t = take(f);
    CHECK(t.fresh);  // emptied when the follow was set
    CHECK(t.count == 0);
    CHECK(t.generation == kG + 1);

    w.reg.publish(l.token, s.feed({s.uidA}));
    t = take(f);
    REQUIRE_FALSE(s.lane(s.uidA).empty());
    CHECK(t.fresh);
    CHECK(t.events == s.lane(s.uidA));
    CHECK(t.generation == kG + 2);

    CHECK(w.run(w.reg.remove(l.token)).empty());
    t = take(f);
    CHECK(t.fresh);
    CHECK(t.count == 0);
    CHECK(t.generation == kG + 3);
    const LinkView v = w.view(f);
    CHECK(v.status == LinkStatus::Waiting);
    REQUIRE(v.follow.has_value());
    CHECK(*v.follow == link);  // kept as stored
    CHECK(w.reg.view(l.token).id.empty());

    // A new instance adopting the leader's state (the undo of a delete) takes its id; the follower binds to it and
    // gets content at its first publish, not before.
    Inst& l2 = w.add("L2", "ctor-l2");
    const LinkRegistry::Adopted a = w.reg.adoptState(l2.token, "L", "L-fresh", std::nullopt);
    CHECK(a.id == "L");
    CHECK_FALSE(a.copy);
    CHECK(w.status(f) == LinkStatus::ProjectChanged);
    CHECK(w.followed(l2, s) == Names{s.uidA});
    t = take(f);
    CHECK_FALSE(t.fresh);
    CHECK(t.count == 0);
    w.reg.publish(l2.token, s.feed({s.uidA}));
    t = take(f);
    CHECK(t.fresh);
    CHECK(t.events == s.lane(s.uidA));
    CHECK(t.generation == kG + 4);  // the buffer's one Publication counts on
}

TEST_CASE("link registry: a bound follower stays with its leader instance whatever happens to ids") {
    Song s;
    World w;
    Inst& l = w.add("L");
    Inst& f = w.add("F");
    w.reg.setFollow(f.token, follow("L", s.uidA));
    w.reg.publish(l.token, s.feed({s.uidA}));
    CHECK(take(f).events == s.lane(s.uidA));

    // The leader loads a state with another id: it keeps its id (rule 3) and its follower.
    CHECK(w.reg.adoptState(l.token, "X", "L-fresh", std::nullopt).id == "L");
    CHECK(w.status(f) == LinkStatus::Playing);
    CHECK(w.followed(l, s) == Names{s.uidA});

    // Another instance loads the leader's state: a copy under a fresh id, and the follower stays with the original.
    Inst& c = w.add("C", "ctor-c");
    const LinkRegistry::Adopted ac = w.reg.adoptState(c.token, "L", "L-copy", std::nullopt);
    CHECK(ac.copy);
    CHECK(ac.id == "L-copy");
    CHECK(w.followed(c, s).empty());
    CHECK(w.followed(l, s) == Names{s.uidA});
    w.reg.publish(c.token, s.feed({}));  // would empty f if f were bound to c
    CHECK_FALSE(take(f).fresh);
    CHECK(w.shown(f).leaderId == "L");
    CHECK(w.shown(f).leaderLabel == w.view(l).label);
}

// --- rule 3: ids on a state load --------------------------------------------------------------------------------

TEST_CASE("link registry: a state loaded while its source lives makes a copy with the fresh id; nothing binds to "
          "it") {
    Song s;
    World w;
    Inst& src = w.add("S");
    Inst& f = w.add("F");
    w.reg.setFollow(f.token, follow("S", s.uidA));
    Inst& c = w.add("C", "ctor-c");
    const LinkRegistry::Adopted a = w.reg.adoptState(c.token, "S", "S-2", std::nullopt);
    CHECK(a.id == "S-2");
    CHECK(a.copy);
    CHECK(a.hooks.empty());
    CHECK(w.view(c).id == "S-2");
    CHECK(w.view(src).id == "S");
    CHECK(w.followed(c, s).empty());
    CHECK(w.followed(src, s) == Names{s.uidA});

    // A follower that arrives now binds to the source as well, not to the copy.
    Inst& g = w.add("G");
    w.reg.setFollow(g.token, follow("S", s.uidB));
    CHECK(w.followed(src, s) == (Names{s.uidA, s.uidB}));
    CHECK(w.followed(c, s).empty());
}

TEST_CASE("link registry: a state loaded after its source was removed takes the id (the undo of a delete)") {
    World w;
    Inst& src = w.add("S");
    w.reg.remove(src.token);
    Inst& n = w.add("N", "ctor-n");
    const LinkRegistry::Adopted a = w.reg.adoptState(n.token, "S", "S-2", std::nullopt);
    CHECK(a.id == "S");
    CHECK_FALSE(a.copy);
    CHECK(w.view(n).id == "S");
}

TEST_CASE("link registry: a leader with bound followers keeps its id on a state load, even when the loaded id is "
          "another's") {
    Song s;
    World w;
    Inst& l = w.add("L");
    Inst& m = w.add("M");
    Inst& f = w.add("F");
    w.reg.setFollow(f.token, follow("L", s.uidA));

    LinkRegistry::Adopted a = w.reg.adoptState(l.token, "M", "L-2", std::nullopt);
    CHECK(a.id == "L");
    CHECK_FALSE(a.copy);
    CHECK(w.view(l).id == "L");
    CHECK(w.view(m).id == "M");
    CHECK(w.shown(f).leaderId == "L");

    a = w.reg.adoptState(l.token, "Z", "L-3", std::nullopt);
    CHECK(a.id == "L");
    CHECK_FALSE(a.copy);
    CHECK(w.shown(f).leaderId == "L");

    // Without bound followers it takes the loaded id.
    w.reg.setFollow(f.token, std::nullopt);
    a = w.reg.adoptState(l.token, "Z", "L-4", std::nullopt);
    CHECK(a.id == "Z");
    CHECK_FALSE(a.copy);
}

TEST_CASE("link registry: an instance's own id is not another's: loading it again is no copy") {
    World w;
    Inst& l = w.add("L");
    const LinkRegistry::Adopted a = w.reg.adoptState(l.token, "L", "L-2", std::nullopt);
    CHECK(a.id == "L");
    CHECK_FALSE(a.copy);
}

// --- rule 4: writes into a link buffer --------------------------------------------------------------------------

TEST_CASE("link registry: a follower gets content only at its leader's publish, never when it binds") {
    Song s;
    World w;
    REQUIRE_FALSE(s.lane(s.uidA).empty());

    SUBCASE("the leader published before the follow") {
        Inst& l = w.add("L");
        w.reg.publish(l.token, s.feed({s.uidA, s.uidB}));  // the lane is compiled already
        Inst& f = w.add("F");
        w.reg.setFollow(f.token, follow("L", s.uidA));
        CHECK(w.status(f) == LinkStatus::Playing);
        Taken t = take(f);
        CHECK(t.fresh);
        CHECK(t.count == 0);
        CHECK(t.generation == kG + 1);

        w.reg.publish(l.token, s.feed({s.uidA, s.uidB}));
        t = take(f);
        CHECK(t.fresh);
        CHECK(t.events == s.lane(s.uidA));
        CHECK(t.generation == kG + 2);
    }
    SUBCASE("the leader takes the id after the follow, with a feed stored") {
        Inst& f = w.add("F");
        w.reg.adoptState(f.token, "F", "F-2", follow("L", s.uidA));
        Taken t = take(f);
        CHECK(t.fresh);
        CHECK(t.count == 0);
        CHECK(t.generation == kG + 1);

        Inst& l = w.add("L-new", "ctor-l");
        w.reg.publish(l.token, s.feed({s.uidA}));  // stored, nobody bound
        w.reg.adoptState(l.token, "L", "L-2", std::nullopt);
        CHECK(w.status(f) == LinkStatus::Playing);
        t = take(f);
        CHECK(t.count == 0);
        CHECK(t.events.empty());

        w.reg.publish(l.token, s.feed({s.uidA}));
        t = take(f);
        CHECK(t.fresh);
        CHECK(t.events == s.lane(s.uidA));
        CHECK(t.generation == kG + 2);
    }
    SUBCASE("the leader stops following and so becomes one, with followers waiting for it") {
        Inst& l = w.add("L");
        Inst& c = w.add("C");
        w.reg.publish(l.token, s.feed({s.uidA}));
        w.reg.setFollow(l.token, follow("C", s.uidA));  // the feed is dropped
        Inst& f = w.add("F");
        w.reg.setFollow(f.token, follow("L", s.uidA));
        CHECK(take(f).generation == kG + 1);
        w.reg.setFollow(l.token, std::nullopt);
        Taken t = take(f);
        CHECK(t.count == 0);
        CHECK_FALSE(t.fresh);
        w.reg.publish(l.token, s.feed({s.uidA}));
        CHECK(take(f).events == s.lane(s.uidA));
        (void)c;
    }
}

TEST_CASE("link registry: every change of a follow empties the buffer, under one Publication for its whole life") {
    Song s;
    World w;
    Inst& l = w.add("L");
    Inst& f = w.add("F");
    const auto feed = s.feed({s.uidA, s.uidB});
    w.reg.publish(l.token, feed);
    REQUIRE(s.lane(s.uidA) != s.lane(s.uidB));

    w.reg.setFollow(f.token, follow("L", s.uidA));
    Taken t = take(f);
    CHECK(t.count == 0);
    CHECK(t.generation == kG + 1);
    w.reg.publish(l.token, feed);
    t = take(f);
    CHECK(t.events == s.lane(s.uidA));
    CHECK(t.generation == kG + 2);

    // Another lane: emptied at once, even though the stored feed has the new lane.
    w.reg.setFollow(f.token, follow("L", s.uidB));
    t = take(f);
    CHECK(t.fresh);
    CHECK(t.count == 0);
    CHECK(t.generation == kG + 3);
    w.reg.publish(l.token, feed);
    t = take(f);
    CHECK(t.events == s.lane(s.uidB));
    CHECK(t.generation == kG + 4);

    // Cleared.
    w.reg.setFollow(f.token, std::nullopt);
    t = take(f);
    CHECK(t.fresh);
    CHECK(t.count == 0);
    CHECK(t.generation == kG + 5);

    // Set by a state load: empty already, so nothing new is published; a fresh Publication would publish kG + 1.
    w.reg.adoptState(f.token, "F", "F-2", follow("L", s.uidA));
    t = take(f);
    CHECK_FALSE(t.fresh);
    CHECK(t.count == 0);
    CHECK(t.generation == kG + 5);
    w.reg.publish(l.token, feed);
    t = take(f);
    CHECK(t.events == s.lane(s.uidA));
    CHECK(t.generation == kG + 6);

    // The same follow again (a reload of the follower's own state, new hints alone): no write.
    LinkFollow again = follow("L", s.uidA);
    again.laneName = "renamed";
    again.leaderLabel = "relabelled";
    w.reg.adoptState(f.token, "F", "F-2", again);
    w.reg.setFollow(f.token, again);
    t = take(f);
    CHECK_FALSE(t.fresh);
    CHECK(t.events == s.lane(s.uidA));
    CHECK(w.shown(f).laneName == "renamed");  // the hints are stored

    // Another document id is another follow.
    w.reg.setFollow(f.token, follow("L", s.uidA, "doc-other"));
    t = take(f);
    CHECK(t.fresh);
    CHECK(t.count == 0);
    CHECK(t.generation == kG + 7);
    w.reg.publish(l.token, feed);
    t = take(f);  // the feed is of doc-1: no events, but the feed's endBeat (fillSlotFromFeed's contract)
    CHECK(t.fresh);
    CHECK(t.count == 0);
    CHECK(t.generation == kG + 8);
    w.reg.publish(l.token, feed);
    CHECK_FALSE(take(f).fresh);
    CHECK(w.status(f) == LinkStatus::ProjectChanged);
}

TEST_CASE("link registry: publish writes the bound followers' buffers and nothing else") {
    Song s;
    World w;
    Inst& l = w.add("L");
    Inst& m = w.add("M");
    Inst& f1 = w.add("F1");
    Inst& f2 = w.add("F2");
    Inst& f3 = w.add("F3");
    Inst& f4 = w.add("F4");
    w.reg.setFollow(f1.token, follow("L", s.uidA));
    w.reg.setFollow(f2.token, follow("M", s.uidA));
    w.reg.setFollow(f3.token, follow("nobody", s.uidA));  // waits
    w.reg.setFollow(f4.token, follow("L", s.uidA, "doc-old"));
    for (Inst* inst : {&l, &m, &f1, &f2, &f3, &f4}) take(*inst);

    w.reg.publish(l.token, s.feed({s.uidA}));
    const Taken t = take(f1);
    CHECK(t.fresh);
    CHECK(t.events == s.lane(s.uidA));
    const Taken t4 = take(f4);  // bound, its link names another document: no events, the feed's endBeat
    CHECK(t4.fresh);
    CHECK(t4.count == 0);
    CHECK_FALSE(take(f2).fresh);
    CHECK_FALSE(take(f3).fresh);
    CHECK_FALSE(take(l).fresh);
    CHECK_FALSE(take(m).fresh);
    // A leader's own link buffer is never written while it leads.
    CHECK(l.buffer->current() == nullptr);
    CHECK(m.buffer->current() == nullptr);

    // The same content again writes nothing; new content writes once.
    w.reg.publish(l.token, s.feed({s.uidA}));
    CHECK_FALSE(take(f1).fresh);
    CHECK_FALSE(take(f4).fresh);
    s.store.setTrackMute(s.a, true);
    w.reg.publish(l.token, s.feed({s.uidA}));
    const Taken muted = take(f1);
    CHECK(muted.fresh);
    CHECK(muted.count == 0);
    CHECK(muted.generation == t.generation + 1);
}

// --- rule 5: an instance that starts or stops following ---------------------------------------------------------

TEST_CASE("link registry: A leads B, then A follows C, then A stops following") {
    Song s;
    World w;
    Inst& a = w.add("A");
    Inst& b = w.add("B");
    Inst& c = w.add("C");
    w.reg.setFollow(b.token, follow("A", s.uidA));
    take(b);
    w.reg.publish(a.token, s.feed({s.uidA}));
    Taken t = take(b);
    CHECK(t.events == s.lane(s.uidA));
    CHECK(t.generation == kG + 2);
    w.reg.publish(c.token, s.feed({}, "doc-c"));

    bool byLoad = false;
    SUBCASE("by command") { byLoad = false; }
    SUBCASE("by state loads") { byLoad = true; }
    const auto setA = [&](std::optional<LinkFollow> link) {
        if (!byLoad) return w.run(w.reg.setFollow(a.token, std::move(link)));
        const LinkRegistry::Adopted adopted = w.reg.adoptState(a.token, "A", "A-2", std::move(link));
        CHECK(adopted.id == "A");
        CHECK_FALSE(adopted.copy);
        return w.run(adopted.hooks);
    };

    CHECK(setA(follow("C", s.uidB, "doc-c")) == Names{"C"});
    t = take(b);
    CHECK(t.fresh);
    CHECK(t.count == 0);
    CHECK(t.generation == kG + 3);
    CHECK(w.status(b) == LinkStatus::Waiting);
    CHECK(w.shown(b).leaderId == "A");
    CHECK(w.followed(a, s).empty());
    CHECK(w.view(a).routed.empty());
    CHECK(w.view(c).leaders.empty());  // A follows, B follows
    CHECK(w.reg.followedLanes(c.token, "doc-c", {s.uidB}).lanes == Names{s.uidB});

    CHECK(setA(std::nullopt) == Names{"C"});
    CHECK(w.status(b) == LinkStatus::ProjectChanged);  // bound to A again, whose feed was dropped
    t = take(b);
    CHECK_FALSE(t.fresh);
    CHECK(t.count == 0);
    REQUIRE(w.view(c).leaders.size() == 1);
    CHECK(w.view(c).leaders[0].id == "A");
    CHECK(w.view(c).leaders[0].lanes.empty());
    CHECK(w.view(c).leaders[0].lineage.empty());
    CHECK(w.followed(a, s) == Names{s.uidA});

    w.reg.publish(a.token, s.feed({s.uidA}));
    t = take(b);
    CHECK(t.fresh);
    CHECK(t.events == s.lane(s.uidA));
    CHECK(t.generation == kG + 4);
    CHECK(w.status(b) == LinkStatus::Playing);
}

// --- rule 6: hooks ----------------------------------------------------------------------------------------------

TEST_CASE("link registry: hooks name the follower's old and new leader, never the caller, each once") {
    Song s;
    World w;
    Inst& l = w.add("L");
    Inst& m = w.add("M");
    Inst& f = w.add("F");

    // setFollow
    CHECK(w.run(w.reg.setFollow(f.token, follow("nobody", s.uidA))).empty());  // waiting: no leader
    CHECK(w.run(w.reg.setFollow(f.token, follow("L", s.uidA))) == Names{"L"});
    CHECK(w.run(w.reg.setFollow(f.token, follow("L", s.uidB))) == Names{"L"});  // old and new: once
    CHECK(w.run(w.reg.setFollow(f.token, follow("L", s.uidB))).empty());       // unchanged
    CHECK(w.run(w.reg.setFollow(f.token, follow("M", s.uidA))) == (Names{"L", "M"}));
    CHECK(w.run(w.reg.setFollow(f.token, follow("nobody", s.uidA))) == Names{"M"});
    CHECK(w.run(w.reg.setFollow(f.token, follow("M", s.uidA))) == Names{"M"});
    CHECK(w.run(w.reg.setFollow(f.token, std::nullopt)) == Names{"M"});
    CHECK(w.run(w.reg.setFollow(f.token, std::nullopt)).empty());

    // adoptState
    CHECK(w.run(w.reg.adoptState(f.token, "F", "F-2", follow("L", s.uidA)).hooks) == Names{"L"});
    CHECK(w.run(w.reg.adoptState(f.token, "F", "F-2", follow("L", s.uidA)).hooks).empty());
    CHECK(w.run(w.reg.adoptState(f.token, "F", "F-2", follow("M", s.uidA)).hooks) == (Names{"L", "M"}));
    CHECK(w.run(w.reg.adoptState(f.token, "F", "F-2", std::nullopt).hooks) == Names{"M"});
    CHECK(w.run(w.reg.adoptState(l.token, "L", "L-2", std::nullopt).hooks).empty());  // a leader's own load

    // A leader whose load binds waiting followers to it does not get its own hook.
    Inst& g = w.add("G");
    w.reg.setFollow(g.token, follow("N", s.uidA));
    Inst& n = w.add("N-inst", "ctor-n");
    CHECK(w.run(w.reg.adoptState(n.token, "N", "N-2", std::nullopt).hooks).empty());
    CHECK(w.followed(n, s) == Names{s.uidA});
    // Nor does a leader that starts following get its followers' hooks or its own.
    Inst& c = w.add("C");
    CHECK(w.run(w.reg.setFollow(n.token, follow("C", s.uidA))) == Names{"C"});
    CHECK(w.run(w.reg.setFollow(n.token, std::nullopt)) == Names{"C"});

    // remove
    CHECK(w.run(w.reg.remove(g.token)) == Names{"N-inst"});
    Inst& h = w.add("H");
    w.reg.setFollow(h.token, follow("nobody", s.uidA));
    CHECK(w.run(w.reg.remove(h.token)).empty());
    w.reg.setFollow(f.token, follow("L", s.uidA));
    CHECK(w.run(w.reg.remove(l.token)).empty());  // a leader: not its followers' hooks, not its own
    CHECK(w.run(w.reg.remove(f.token)).empty());  // waiting since its leader went
    CHECK(w.run(w.reg.remove(l.token)).empty());  // unknown token
    (void)m;
    (void)c;
}

// --- rule 7: status ---------------------------------------------------------------------------------------------

TEST_CASE("link registry: a follower's status") {
    Song s;
    World w;
    Inst& l = w.add("L");
    Inst& f = w.add("F");
    CHECK(w.status(f) == LinkStatus::NotFollowing);
    CHECK(w.status(l) == LinkStatus::NotFollowing);

    w.reg.setFollow(f.token, follow("nobody", s.uidA));
    CHECK(w.status(f) == LinkStatus::Waiting);
    w.reg.setFollow(f.token, follow("L", s.uidA));
    CHECK(w.status(f) == LinkStatus::ProjectChanged);  // no feed yet
    w.reg.publish(l.token, s.feed({}, "doc-2"));
    CHECK(w.status(f) == LinkStatus::ProjectChanged);  // a feed of another document
    w.reg.publish(l.token, s.feed({}));
    CHECK(w.status(f) == LinkStatus::Playing);
    w.reg.setFollow(f.token, follow("L", "uid-404"));
    CHECK(w.status(f) == LinkStatus::LaneMissing);
    w.reg.setFollow(f.token, follow("L", std::string(kChordsLaneUid)));
    CHECK(w.status(f) == LinkStatus::Playing);

    s.store.setTrackMute(s.a, true);
    w.reg.publish(l.token, s.feed({}));
    w.reg.setFollow(f.token, follow("L", s.uidA));
    CHECK(w.status(f) == LinkStatus::Muted);
    w.reg.setFollow(f.token, follow("L", std::string(kChordsLaneUid)));
    CHECK(w.status(f) == LinkStatus::Playing);
    s.store.setChordsMute(true);
    w.reg.publish(l.token, s.feed({}));
    CHECK(w.status(f) == LinkStatus::Muted);

    w.reg.remove(l.token);
    CHECK(w.status(f) == LinkStatus::Waiting);
}

TEST_CASE("link registry: a feed of another document is ProjectChanged even when it lacks the lane") {
    Song s;
    World w;
    Inst& l = w.add("L");
    Inst& f = w.add("F");
    w.reg.setFollow(f.token, follow("L", s.uidA, "doc-1"));
    // The leader's document is now another one, with no track at all: the lane uid is in neither its feed nor it.
    DocumentStore other;
    other.appendChord(chord(2, "min"), 4.0, 100);
    const LaneUids none;
    const auto feed = buildLinkFeed(other.doc(), "doc-2", none, "lineage-2", "feed label", {});
    REQUIRE(std::none_of(feed->lanes.begin(), feed->lanes.end(),
                         [&](const LinkLane& lane) { return lane.uid == s.uidA; }));
    w.reg.publish(l.token, feed);
    CHECK(w.status(f) == LinkStatus::ProjectChanged);
}

// --- rule 8: copies ---------------------------------------------------------------------------------------------

TEST_CASE("link registry: a follower that is a copy is offered its leader's newest live leading copy") {
    Song s;
    World w;
    Inst& l = w.add("L");
    Inst& f = w.add("F");
    w.reg.setFollow(f.token, follow("L", s.uidA));
    CHECK_FALSE(w.view(f).switchTo.has_value());

    // Both tracks duplicated: each copy is a new instance given its source's state while the source lives.
    Inst& l2 = w.add("L2", "ctor-l2");
    REQUIRE(w.reg.adoptState(l2.token, "L", "L-copy", std::nullopt).copy);
    CHECK_FALSE(w.view(f).switchTo.has_value());  // f is no copy
    Inst& f2 = w.add("F2", "ctor-f2");
    REQUIRE(w.reg.adoptState(f2.token, "F", "F-copy", follow("L", s.uidA)).copy);
    CHECK(w.status(f2) == LinkStatus::ProjectChanged);  // bound to L, not to its copy
    LinkView v = w.view(f2);
    REQUIRE(v.switchTo.has_value());
    CHECK(v.switchTo->id == "L-copy");
    CHECK(v.switchTo->label == w.view(l2).label);
    CHECK_FALSE(w.view(f).switchTo.has_value());

    // A newer copy of the leader is named instead.
    Inst& l3 = w.add("L3", "ctor-l3");
    REQUIRE(w.reg.adoptState(l3.token, "L", "L-copy-3", std::nullopt).copy);
    CHECK(w.switchId(f2) == "L-copy-3");
    // A copy that follows does not lead: the newest leading one is named.
    w.reg.setFollow(l3.token, follow("nobody", s.uidA));
    CHECK(w.switchId(f2) == "L-copy");
    // Newest is the copy made last, whatever the order of add.
    Inst& l4 = w.add("L4", "ctor-l4");
    Inst& l5 = w.add("L5", "ctor-l5");
    REQUIRE(w.reg.adoptState(l5.token, "L", "L-copy-5", std::nullopt).copy);
    REQUIRE(w.reg.adoptState(l4.token, "L", "L-copy-4", std::nullopt).copy);
    CHECK(w.switchId(f2) == "L-copy-4");
    w.reg.remove(l4.token);
    CHECK(w.switchId(f2) == "L-copy-5");
    w.reg.remove(l5.token);
    w.reg.remove(l2.token);
    CHECK_FALSE(w.view(f2).switchTo.has_value());

    // Only a bound copy is offered anything.
    Inst& l6 = w.add("L6", "ctor-l6");
    REQUIRE(w.reg.adoptState(l6.token, "L", "L-copy-6", std::nullopt).copy);
    CHECK(w.switchId(f2) == "L-copy-6");
    w.reg.setFollow(f2.token, follow("nobody", s.uidA));
    CHECK_FALSE(w.view(f2).switchTo.has_value());
    // A later load that is no copy ends being one.
    w.reg.setFollow(f2.token, follow("L", s.uidA));
    CHECK(w.view(f2).switchTo.has_value());
    CHECK_FALSE(w.reg.adoptState(f2.token, "F-copy", "F-3", follow("L", s.uidA)).copy);
    CHECK_FALSE(w.view(f2).switchTo.has_value());
}

TEST_CASE("link registry: a follower that is a copy is offered no copy of an unrelated leader") {
    Song s;
    World w;
    Inst& l = w.add("L");
    w.add("M");
    Inst& f = w.add("F");
    w.reg.setFollow(f.token, follow("L", s.uidA));
    // M is duplicated (its copy leads); L is not.
    Inst& m2 = w.add("M2", "ctor-m2");
    REQUIRE(w.reg.adoptState(m2.token, "M", "M-copy", std::nullopt).copy);
    Inst& f2 = w.add("F2", "ctor-f2");
    REQUIRE(w.reg.adoptState(f2.token, "F", "F-copy", follow("L", s.uidA)).copy);
    REQUIRE(w.status(f2) == LinkStatus::ProjectChanged);  // bound to L
    CHECK_FALSE(w.view(f2).switchTo.has_value());
    (void)l;
}

TEST_CASE("link registry: a copy that keeps its id on a load because followers are bound is no copy any more") {
    Song s;
    World w;
    w.add("L");
    Inst& f = w.add("F");
    w.reg.setFollow(f.token, follow("L", s.uidA));
    Inst& f2 = w.add("F2", "ctor-f2");
    REQUIRE(w.reg.adoptState(f2.token, "F", "F-copy", follow("L", s.uidA)).copy);
    Inst& d = w.add("D", "ctor-d");
    REQUIRE(w.reg.adoptState(d.token, "L", "L-copy", std::nullopt).copy);
    REQUIRE(w.switchId(f2) == "L-copy");

    // D gets a follower of its own, then reloads a state: it keeps its id (rule 3) and is no copy after that load.
    Inst& g = w.add("G");
    w.reg.setFollow(g.token, follow("L-copy", s.uidA));
    REQUIRE(w.followed(d, s) == Names{s.uidA});
    const LinkRegistry::Adopted a = w.reg.adoptState(d.token, "L", "L-copy-2", std::nullopt);
    CHECK(a.id == "L-copy");
    CHECK_FALSE(a.copy);
    CHECK(w.view(d).id == "L-copy");
    CHECK_FALSE(w.view(f2).switchTo.has_value());
}

// --- rule 9: labels ---------------------------------------------------------------------------------------------

TEST_CASE("link registry: labels go A to Z by order of add, then numbers, one per instance for its lifetime") {
    World w;
    std::vector<Inst*> insts;
    for (int i = 0; i < 28; ++i) insts.push_back(&w.add("I" + std::to_string(i)));
    CHECK(w.view(*insts[0]).label == "ChordLab A");
    CHECK(w.view(*insts[1]).label == "ChordLab B");
    CHECK(w.view(*insts[25]).label == "ChordLab Z");
    CHECK(w.view(*insts[26]).label == "ChordLab 27");
    CHECK(w.view(*insts[27]).label == "ChordLab 28");
    REQUIRE_FALSE(w.view(*insts[2]).leaders.empty());
    CHECK(w.view(*insts[2]).leaders[0].label == "ChordLab A");

    w.reg.adoptState(insts[0]->token, "other", "fresh", std::nullopt);
    CHECK(w.view(*insts[0]).label == "ChordLab A");
    w.reg.remove(insts[1]->token);
    CHECK(w.view(w.add("next")).label == "ChordLab 29");
}

// --- rule 10: serial --------------------------------------------------------------------------------------------

TEST_CASE("link registry: the serial moves when a view changes and on every publish, never on no-ops or reads") {
    Song s;
    World w;
    const auto moved = [&](const std::function<void()>& call) {
        const std::uint64_t before = w.reg.serial();
        call();
        return w.reg.serial() != before;
    };
    Inst* l = nullptr;
    Inst* f = nullptr;
    CHECK(moved([&] { l = &w.add("L"); }));
    CHECK(moved([&] { f = &w.add("F"); }));

    // setFollow: the link and the binding move it; the identical follow does not; hints alone do.
    CHECK(moved([&] { w.reg.setFollow(f->token, follow("L", s.uidA)); }));
    CHECK_FALSE(moved([&] { w.reg.setFollow(f->token, follow("L", s.uidA)); }));
    LinkFollow hinted = follow("L", s.uidA);
    hinted.laneName = "new name";
    CHECK(moved([&] { w.reg.setFollow(f->token, hinted); }));
    CHECK_FALSE(moved([&] { w.reg.setFollow(f->token, hinted); }));

    // publish: every call (a new feed).
    const auto feed = s.feed({s.uidA});
    CHECK(moved([&] { w.reg.publish(l->token, feed); }));
    CHECK(moved([&] { w.reg.publish(l->token, feed); }));

    // adoptState that changes nothing: a leader reloading its own id, a bound leader keeping its id, a follower
    // reloading its own state.
    CHECK_FALSE(moved([&] { w.reg.adoptState(l->token, "L", "L-2", std::nullopt); }));
    CHECK_FALSE(moved([&] { w.reg.adoptState(l->token, "X", "L-2", std::nullopt); }));  // keeps "L": f is bound
    CHECK_FALSE(moved([&] { w.reg.adoptState(f->token, "F", "F-2", hinted); }));
    // adoptState that changes the follow, the id, or being a copy.
    CHECK(moved([&] { w.reg.adoptState(f->token, "F", "F-2", std::nullopt); }));
    CHECK(moved([&] { w.reg.adoptState(l->token, "L2", "L3", std::nullopt); }));  // no followers now: takes L2
    Inst* c = nullptr;
    CHECK(moved([&] { c = &w.add("C", "ctor-c"); }));
    CHECK(moved([&] { REQUIRE(w.reg.adoptState(c->token, "L2", "C-copy", std::nullopt).copy); }));
    CHECK(moved([&] { REQUIRE_FALSE(w.reg.adoptState(c->token, "C-copy", "C-2", std::nullopt).copy); }));  // id kept
    CHECK_FALSE(moved([&] { w.reg.adoptState(c->token, "C-copy", "C-2", std::nullopt); }));

    // Reads and unknown tokens.
    CHECK_FALSE(moved([&] {
        (void)w.reg.view(f->token);
        (void)w.reg.view(l->token);
        (void)w.reg.followedLanes(l->token, "doc-1", s.all());
        (void)w.reg.view(0);
        w.reg.setFollow(0, follow("L2", s.uidA));
        w.reg.adoptState(0, "Q", "Q-2", std::nullopt);
        w.reg.publish(0, feed);
        w.reg.remove(0);
    }));
    CHECK(moved([&] { w.reg.remove(f->token); }));
}

// --- follow: the lineage checked under the lock ---------------------------------------------------------------

TEST_CASE("link registry: follow applies only to a live leading instance with that lineage and lane") {
    Song s;
    World w;
    Inst& l = w.add("L");
    Inst& f = w.add("F");
    Inst& x = w.add("X");
    Hooks hooks;

    // No feed yet: stale, whatever lineage is asked for.
    CHECK(w.reg.follow(f.token, "L", s.uidA, "lineage-1", hooks) == LinkFollowResult::StaleLeader);
    CHECK(hooks.empty());
    w.reg.publish(l.token, s.feed({}, "doc-7"));

    // Applied: the feed's document id, the lane's name and role and the leader's label as hints.
    CHECK(w.reg.follow(f.token, "L", s.uidA, "lineage-1", hooks) == LinkFollowResult::Applied);
    CHECK(w.run(hooks) == Names{"L"});
    LinkFollow expected;
    expected.leaderId = "L";
    expected.documentId = "doc-7";
    expected.laneUid = s.uidA;
    expected.laneName = "Low";
    expected.laneRole = "bass";
    expected.leaderLabel = "ChordLab A";
    CHECK(w.shown(f) == expected);
    CHECK(w.status(f) == LinkStatus::Playing);
    CHECK(w.reg.followedLanes(l.token, "doc-7", s.all()).lanes == Names{s.uidA});
    Taken t = take(f);
    CHECK(t.fresh);  // emptied, as by setFollow
    CHECK(t.count == 0);
    CHECK(t.generation == kG + 1);

    // The same link again: applied, nothing changes.
    hooks.push_back(nullptr);  // a result always replaces what the caller passed
    const std::uint64_t serial = w.reg.serial();
    CHECK(w.reg.follow(f.token, "L", s.uidA, "lineage-1", hooks) == LinkFollowResult::Applied);
    CHECK(hooks.empty());
    CHECK(w.reg.serial() == serial);

    // The chords lane: its name, no role.
    Inst& g = w.add("G");
    CHECK(w.reg.follow(g.token, "L", std::string(kChordsLaneUid), "lineage-1", hooks) == LinkFollowResult::Applied);
    CHECK(w.shown(g).laneName == "Chords");
    CHECK(w.shown(g).laneRole.empty());
    CHECK(w.shown(g).documentId == "doc-7");

    // The stored follow keeps the hints: seen as stored once the leader is gone.
    w.reg.setFollow(x.token, follow("nobody", s.uidA));  // X follows: it is no leader
    w.reg.remove(l.token);
    REQUIRE(w.status(f) == LinkStatus::Waiting);
    CHECK(w.shown(f) == expected);
    (void)x;
}

TEST_CASE("link registry: a refused follow changes nothing") {
    Song s;
    World w;
    Inst& l = w.add("L");
    Inst& f = w.add("F");
    Inst& x = w.add("X");
    w.reg.publish(l.token, s.feed({s.uidB}));
    Hooks hooks;
    REQUIRE(w.reg.follow(f.token, "L", s.uidB, "lineage-1", hooks) == LinkFollowResult::Applied);
    w.reg.publish(l.token, s.feed({s.uidB}));
    REQUIRE(take(f).events == s.lane(s.uidB));
    w.reg.setFollow(x.token, follow("nobody", s.uidA));  // X follows: its id names no leading instance

    const LinkView before = w.view(f);
    const std::uint32_t followerGeneration = f.buffer->currentGeneration();
    const std::uint64_t serial = w.reg.serial();
    const auto unchanged = [&] {
        CHECK(hooks.empty());
        CHECK(w.reg.serial() == serial);
        const LinkView now = w.view(f);
        REQUIRE(now.follow.has_value());
        CHECK(*now.follow == *before.follow);
        CHECK(now.status == before.status);
        CHECK(now.id == before.id);
        const Taken t = take(f);
        CHECK_FALSE(t.fresh);
        CHECK(t.generation == followerGeneration);
        CHECK(t.events == s.lane(s.uidB));
        CHECK_FALSE(take(l).fresh);
        CHECK(l.buffer->current() == nullptr);
        CHECK(w.followed(l, s) == Names{s.uidB});
    };

    hooks.push_back(nullptr);
    CHECK(w.reg.follow(f.token, "nobody", s.uidA, "lineage-1", hooks) == LinkFollowResult::UnknownLeader);
    unchanged();
    CHECK(w.reg.follow(f.token, "X", s.uidA, "lineage-1", hooks) == LinkFollowResult::UnknownLeader);  // follows
    unchanged();
    CHECK(w.reg.follow(f.token, "F", s.uidA, "lineage-1", hooks) == LinkFollowResult::UnknownLeader);  // itself
    unchanged();
    CHECK(w.reg.follow(f.token, "L", s.uidA, "lineage-0", hooks) == LinkFollowResult::StaleLeader);
    unchanged();
    CHECK(w.reg.follow(f.token, "L", "uid-404", "lineage-1", hooks) == LinkFollowResult::UnknownLane);
    unchanged();
    CHECK(w.reg.follow(0, "L", s.uidA, "lineage-1", hooks) == LinkFollowResult::UnknownLeader);  // unknown caller
    unchanged();
}

TEST_CASE("link registry: following one's own id is refused by follow and waits forever by setFollow") {
    Song s;
    World w;
    Inst& l = w.add("L");
    w.reg.publish(l.token, s.feed({}));
    Hooks hooks;
    CHECK(w.reg.follow(l.token, "L", s.uidA, "lineage-1", hooks) == LinkFollowResult::UnknownLeader);
    CHECK(w.status(l) == LinkStatus::NotFollowing);
    CHECK(w.run(w.reg.setFollow(l.token, follow("L", s.uidA))).empty());
    CHECK(w.status(l) == LinkStatus::Waiting);
    w.add("M");
    CHECK(w.status(l) == LinkStatus::Waiting);
}

// --- the view ---------------------------------------------------------------------------------------------------

TEST_CASE("link registry: the view lists the leaders with their feeds' lanes and counts, and a leader's routes") {
    Song s;
    World w;
    Inst& l = w.add("L");
    Inst& m = w.add("M");
    Inst& f1 = w.add("F1");
    Inst& f2 = w.add("F2");
    Inst& f3 = w.add("F3");
    Inst& f4 = w.add("F4");
    Inst& f5 = w.add("F5");
    w.reg.setFollow(f1.token, follow("L", s.uidA));
    w.reg.setFollow(f2.token, follow("L", s.uidA));
    w.reg.setFollow(f3.token, follow("L", std::string(kChordsLaneUid)));
    w.reg.setFollow(f4.token, follow("L", s.uidB, "doc-old"));
    w.reg.setFollow(f5.token, follow("nobody", s.uidB));
    s.store.setTrackMute(s.b, true);
    w.reg.publish(l.token, s.feed({s.uidA, std::string(kChordsLaneUid)}));

    const LinkView vm = w.view(m);
    CHECK(vm.id == "M");
    CHECK(vm.status == LinkStatus::NotFollowing);
    CHECK_FALSE(vm.follow.has_value());
    REQUIRE(vm.leaders.size() == 1);  // M itself is not listed, followers never are
    const LinkLeaderView& leader = vm.leaders[0];
    CHECK(leader.id == "L");
    CHECK(leader.label == w.view(l).label);
    CHECK(leader.documentId == "doc-1");
    CHECK(leader.lineage == "lineage-1");
    REQUIRE(leader.lanes.size() == 3);
    CHECK(leader.lanes[0].uid == kChordsLaneUid);
    CHECK(leader.lanes[0].chords);
    CHECK(leader.lanes[0].name == "Chords");
    CHECK(leader.lanes[0].audible);
    CHECK(leader.lanes[0].followers == 1);
    CHECK(leader.lanes[1].uid == s.uidA);
    CHECK_FALSE(leader.lanes[1].chords);
    CHECK(leader.lanes[1].name == "Low");
    CHECK(leader.lanes[1].role == TrackRole::Bass);
    CHECK(leader.lanes[1].audible);
    CHECK(leader.lanes[1].followers == 2);
    CHECK(leader.lanes[2].uid == s.uidB);
    CHECK(leader.lanes[2].name == "High");
    CHECK(leader.lanes[2].role == TrackRole::Lead);
    CHECK_FALSE(leader.lanes[2].audible);
    CHECK(leader.lanes[2].followers == 0);  // f4's link names another document
    CHECK(vm.routed.empty());               // M has no feed

    const LinkView vl = w.view(l);
    CHECK(leaderIds(vl) == Names{"M"});
    REQUIRE(vl.routed.size() == 2);
    CHECK(vl.routed[0].uid == kChordsLaneUid);
    CHECK(vl.routed[0].followers == 1);
    CHECK(vl.routed[1].uid == s.uidA);
    CHECK(vl.routed[1].name == "Low");
    CHECK(vl.routed[1].followers == 2);

    const LinkView v1 = w.view(f1);
    REQUIRE(leaderIds(v1) == (Names{"L", "M"}));
    CHECK(v1.leaders[1].lanes.empty());
    CHECK(v1.leaders[1].documentId.empty());
    CHECK(v1.routed.empty());
    REQUIRE(v1.follow.has_value());
    CHECK(v1.follow->leaderId == "L");
    CHECK(v1.follow->leaderLabel == vl.label);  // the bound leader's
    CHECK(v1.follow->laneName == "hint name");
    CHECK(v1.follow->laneUid == s.uidA);
    CHECK(v1.follow->documentId == "doc-1");
    CHECK(w.shown(f5).leaderLabel == "hint label");  // waiting: as stored

    const LinkView unknown = w.reg.view(0);
    CHECK(unknown.id.empty());
    CHECK(unknown.label.empty());
    CHECK(unknown.status == LinkStatus::NotFollowing);
    CHECK(unknown.leaders.empty());
}

TEST_CASE("link registry: the view's follow names the bound leader by its current id and label") {
    Song s;
    World w;
    Inst& l = w.add("L-inst", "ctor-l");
    Inst& f = w.add("F");
    w.reg.setFollow(f.token, follow("L", s.uidA));
    LinkView v = w.view(f);
    REQUIRE(v.follow.has_value());
    CHECK(v.follow->leaderId == "L");
    CHECK(v.follow->leaderLabel == "hint label");
    // The leader's id changes while the follower is unbound, which binds it.
    CHECK(w.reg.adoptState(l.token, "L", "L-2", std::nullopt).id == "L");
    v = w.view(f);
    REQUIRE(v.follow.has_value());
    CHECK(v.follow->leaderId == "L");
    CHECK(v.follow->leaderLabel == "ChordLab A");
    CHECK(v.status == LinkStatus::ProjectChanged);
}

// --- followedLanes ----------------------------------------------------------------------------------------------

TEST_CASE("link registry: followedLanes counts bound followers in the given document, each lane once") {
    Song s;
    World w;
    Inst& l = w.add("L");
    Inst& m = w.add("M");
    Inst& f1 = w.add("F1");
    Inst& f2 = w.add("F2");
    Inst& f3 = w.add("F3");
    Inst& f4 = w.add("F4");
    Inst& f5 = w.add("F5");
    w.reg.setFollow(f1.token, follow("L", s.uidA));
    w.reg.setFollow(f2.token, follow("L", s.uidA));  // the same lane
    w.reg.setFollow(f3.token, follow("L", std::string(kChordsLaneUid), "doc-old"));
    w.reg.setFollow(f4.token, follow("L-other", s.uidB));  // waits
    w.reg.setFollow(f5.token, follow("M", s.uidB));

    CHECK(w.followed(l, s) == Names{s.uidA});
    CHECK(w.followed(l, s, "doc-old") == Names{std::string(kChordsLaneUid)});
    CHECK(w.reg.followedLanes(l.token, "doc-1", {s.uidB, s.uidA, s.uidA}).lanes == Names{s.uidA});
    CHECK(w.reg.followedLanes(l.token, "doc-1", {s.uidB}).lanes.empty());
    CHECK(w.reg.followedLanes(l.token, "doc-1", {}).lanes.empty());
    CHECK(w.followed(m, s) == Names{s.uidB});
    CHECK(w.followed(f1, s).empty());  // a follower has no followers
    CHECK(w.reg.followedLanes(0, "doc-1", s.all()).lanes.empty());

    Inst& lo = w.add("L-other");
    CHECK(w.followed(lo, s) == Names{s.uidB});
}

TEST_CASE("link registry: a leader's binding counter moves with every bind and unbind, and comes with its lanes") {
    // followedLanes returns the counter from the same critical section as the lanes: a follower that takes over a
    // lane its old follower just left changes no lane, but it moves the counter (a leader's publish decision).
    Song s;
    World w;
    Inst& l = w.add("L");
    Inst& m = w.add("M");
    Inst& f1 = w.add("F1");
    Inst& f2 = w.add("F2");
    const auto bindings = [&](const Inst& leader) {
        return w.reg.followedLanes(leader.token, "doc-1", s.all()).bindings;
    };
    w.reg.publish(l.token, s.feed(s.all()));
    w.reg.publish(m.token, s.feed(s.all()));

    std::uint64_t before = bindings(l);
    w.reg.setFollow(f1.token, follow("L", s.uidA));  // binds
    CHECK(bindings(l) > before);
    before = bindings(l);
    const std::uint64_t mBefore = bindings(m);
    Hooks hooks;
    REQUIRE(w.reg.follow(f1.token, "L", s.uidB, "lineage-1", hooks) == LinkFollowResult::Applied);  // another lane
    CHECK(bindings(l) > before);
    CHECK(bindings(m) == mBefore);  // nothing of M's changed

    SUBCASE("an unbind") {
        before = bindings(l);
        w.reg.setFollow(f1.token, std::nullopt);
        CHECK(bindings(l) > before);
    }
    SUBCASE("a bound follower moving to another leader moves both counters") {
        before = bindings(l);
        w.reg.setFollow(f1.token, follow("M", s.uidA));
        CHECK(bindings(l) > before);
        CHECK(bindings(m) > mBefore);
    }
    SUBCASE("a bound follower removed") {
        before = bindings(l);
        (void)w.reg.remove(f1.token);
        CHECK(bindings(l) > before);
    }
    SUBCASE("a follower takes over the lane its old follower left: the lanes stay, the counter moves") {
        const Names lanes = w.followed(l, s);
        before = bindings(l);
        w.reg.setFollow(f1.token, std::nullopt);
        w.reg.setFollow(f2.token, follow("L", s.uidB));
        CHECK(w.followed(l, s) == lanes);
        CHECK(bindings(l) > before);
    }
    SUBCASE("reads, a refused follow, a publish and new hints alone leave it") {
        before = bindings(l);
        (void)w.reg.view(l.token);
        (void)w.reg.view(f1.token);
        (void)w.reg.followedLanes(l.token, "doc-1", s.all());
        CHECK(w.reg.follow(f2.token, "L", s.uidA, "stale-lineage", hooks) == LinkFollowResult::StaleLeader);
        CHECK(w.reg.follow(f2.token, "L", "no-such-lane", "lineage-1", hooks) == LinkFollowResult::UnknownLane);
        w.reg.publish(l.token, s.feed(s.all()));
        LinkFollow hinted = w.shown(f1);
        hinted.laneName = "other hint";
        w.reg.setFollow(f1.token, hinted);
        CHECK(bindings(l) == before);
    }
}

TEST_CASE("link registry: shared is one registry for the process") {
    LinkRegistry& a = LinkRegistry::shared();
    LinkRegistry& b = LinkRegistry::shared();
    CHECK(&a == &b);
}

// --- threads ----------------------------------------------------------------------------------------------------

TEST_CASE("link registry: threads publishing, toggling follows, leaders coming, going and following, consuming") {
    // Two contents per lane: the second song has a third chord and a second bass note (uids coincide).
    Song s1;
    Song s2;
    s2.store.appendChord(chord(5, "maj"), 4.0, 100);
    {
        std::string why;
        REQUIRE_MESSAGE(s2.store.insertNotes(s2.a, {ns(48, 1920, 480)}, &why).size() == 1, why);
    }
    REQUIRE(s1.uidA == s2.uidA);
    const auto feed1 = s1.feed(s1.all());
    const auto feed2 = s2.feed(s2.all());
    const std::vector<Events> allowedA{Events{}, s1.lane(s1.uidA), s2.lane(s2.uidA)};
    const std::vector<Events> allowedChords{Events{}, s1.lane(std::string(kChordsLaneUid)),
                                            s2.lane(std::string(kChordsLaneUid))};
    const std::vector<Events> allowedB{Events{}, s1.lane(s1.uidB)};
    REQUIRE(allowedA[1] != allowedA[2]);
    REQUIRE(allowedChords[1] != allowedChords[2]);
    REQUIRE_FALSE(allowedB[1].empty());

    LinkRegistry reg;
    std::atomic<int> hookCalls{0};
    const LinkRegistry::Hook hook = [&hookCalls] { hookCalls.fetch_add(1, std::memory_order_relaxed); };
    const auto runHooks = [](const Hooks& hooks) {
        for (const auto& h : hooks) {
            if (h && *h) (*h)();
        }
    };
    auto leaderBuffer = std::make_unique<TripleBuffer<SnapshotSlot>>();
    auto buffer1 = std::make_unique<TripleBuffer<SnapshotSlot>>();
    auto buffer2 = std::make_unique<TripleBuffer<SnapshotSlot>>();
    auto buffer3 = std::make_unique<TripleBuffer<SnapshotSlot>>();
    const LinkToken leader = reg.add("L", leaderBuffer.get(), hook);
    const LinkToken follower1 = reg.add("F1", buffer1.get(), hook);
    const LinkToken follower2 = reg.add("F2", buffer2.get(), hook);
    const LinkToken follower3 = reg.add("F3", buffer3.get(), hook);
    const LinkFollow follow1 = follow("L", s1.uidA);
    const LinkFollow follow2 = follow("L", std::string(kChordsLaneUid));
    runHooks(reg.setFollow(follower3, follow("M", s1.uidB)));  // waits for leaders that come and go
    const Names lanes = s1.all();

    // L's model mutex, as the shell has one per instance: L publishes no feed while it follows (LinkEndpoint does
    // the same), and its publishes and its follow changes are serialised by it.
    std::mutex leaderModel;
    bool leaderFollowing = false;  // guarded by leaderModel

    constexpr int kPublishes = 20000;
    constexpr int kToggles = 10000;
    constexpr int kCycles = 300;

    // Per consumer: written by its thread only, read after the join.
    struct Seen {
        int acquires = 0;
        int foreign = 0;    // content that is neither empty nor one of the lane's two contents
        int backwards = 0;  // a generation not above the previous one, or not above kLinkGenerationBase
        Events last;
        std::uint32_t lastGeneration = 0;
    };
    std::atomic<bool> stop{false};
    const auto consume = [&stop](TripleBuffer<SnapshotSlot>* buffer, const std::vector<Events>* allowed,
                                 Seen* seen) {
        return std::thread([&stop, buffer, allowed, seen] {
            std::uint32_t previous = 0;
            for (;;) {
                // Read before the acquire: once it reads true, that acquire sees every publish made before it.
                const bool done = stop.load(std::memory_order_acquire);
                if (const SnapshotSlot* slot = buffer->acquire()) {
                    ++seen->acquires;
                    const std::uint32_t generation = buffer->currentGeneration();
                    if (generation <= previous || generation <= kLinkGenerationBase) ++seen->backwards;
                    previous = generation;
                    if (std::find(allowed->begin(), allowed->end(), eventsOf(*slot)) == allowed->end()) {
                        ++seen->foreign;
                    }
                }
                if (done) break;
                std::this_thread::yield();
            }
            if (const SnapshotSlot* slot = buffer->current()) seen->last = eventsOf(*slot);
            seen->lastGeneration = buffer->currentGeneration();
        });
    };

    const auto started = std::chrono::steady_clock::now();
    Seen seen1;
    Seen seen2;
    Seen seen3;
    std::thread consumer1 = consume(buffer1.get(), &allowedA, &seen1);
    std::thread consumer2 = consume(buffer2.get(), &allowedChords, &seen2);
    std::thread consumer3 = consume(buffer3.get(), &allowedB, &seen3);

    std::thread publisher([&] {
        for (int i = 0; i < kPublishes; ++i) {
            const std::lock_guard<std::mutex> model(leaderModel);
            if (leaderFollowing) continue;
            (void)reg.followedLanes(leader, "doc-1", lanes);
            reg.publish(leader, i % 2 == 0 ? feed1 : feed2);
        }
    });
    const auto toggle = [&](LinkToken token, const LinkFollow* link) {
        return std::thread([&reg, &runHooks, token, link, leader, &lanes] {
            for (int i = 0; i < kToggles; ++i) {
                runHooks(reg.setFollow(token, i % 2 == 0 ? std::optional<LinkFollow>(*link) : std::nullopt));
                (void)reg.view(token);
                (void)reg.followedLanes(leader, "doc-1", lanes);
            }
        });
    };
    std::thread toggler1 = toggle(follower1, &follow1);
    std::thread toggler2 = toggle(follower2, &follow2);
    std::thread churn([&] {
        for (int i = 0; i < kCycles; ++i) {
            if (i % 2 == 0) {
                // A leader comes under the id F3 waits for, publishes (F3 gets its lane) and goes (F3 is emptied).
                // A fresh link buffer per registration: it outlives the registration; it is never written, as
                // this instance only leads.
                auto buffer = std::make_unique<TripleBuffer<SnapshotSlot>>();
                const LinkToken third = reg.add("M-ctor-" + std::to_string(i), buffer.get(), hook);
                runHooks(reg.adoptState(third, "M", "M-fresh-" + std::to_string(i), std::nullopt).hooks);
                (void)reg.followedLanes(third, "doc-1", lanes);
                reg.publish(third, feed1);
                (void)reg.view(third);
                runHooks(reg.remove(third));
            } else {
                // L starts following (F1 and F2, when bound, are unbound and emptied), then leads again.
                Hooks hooks;
                {
                    const std::lock_guard<std::mutex> model(leaderModel);
                    hooks = reg.setFollow(leader, follow("nobody", s1.uidA));
                    leaderFollowing = true;
                }
                runHooks(hooks);
                (void)reg.view(follower1);
                {
                    const std::lock_guard<std::mutex> model(leaderModel);
                    hooks = reg.setFollow(leader, std::nullopt);
                    leaderFollowing = false;
                }
                runHooks(hooks);
            }
        }
    });

    publisher.join();
    toggler1.join();
    toggler2.join();
    churn.join();

    // The end state: L leads, F1 and F2 follow it, and a final publish writes their lanes; F3 waits, emptied.
    REQUIRE(reg.view(leader).status == LinkStatus::NotFollowing);
    runHooks(reg.setFollow(follower1, follow1));
    runHooks(reg.setFollow(follower2, follow2));
    reg.publish(leader, feed1);
    stop.store(true, std::memory_order_release);
    consumer1.join();
    consumer2.join();
    consumer3.join();
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    MESSAGE("threads: " << seconds << " s, acquires " << seen1.acquires << ", " << seen2.acquires << " and "
                        << seen3.acquires << ", hooks run " << hookCalls.load());

    CHECK(seen1.foreign == 0);
    CHECK(seen2.foreign == 0);
    CHECK(seen1.backwards == 0);
    CHECK(seen2.backwards == 0);
    CHECK(seen3.foreign == 0);
    CHECK(seen3.backwards == 0);
    CHECK(seen1.acquires > 0);
    CHECK(seen2.acquires > 0);
    CHECK(seen3.acquires > 0);
    CHECK(seen1.last == s1.lane(s1.uidA));
    CHECK(seen2.last == s1.lane(std::string(kChordsLaneUid)));
    CHECK(seen3.last.empty());
    CHECK(reg.view(follower1).status == LinkStatus::Playing);
    CHECK(reg.view(follower2).status == LinkStatus::Playing);
    CHECK(reg.view(follower3).status == LinkStatus::Waiting);
    // L's link buffer was written only while it followed "nobody": always empty. Its consumer is this thread.
    leaderBuffer->acquire();
    REQUIRE(leaderBuffer->current() != nullptr);
    CHECK(leaderBuffer->current()->count == 0);
    reg.remove(follower1);
    reg.remove(follower2);
    reg.remove(follower3);
    reg.remove(leader);
}
