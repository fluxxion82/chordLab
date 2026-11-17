#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/compiler.h"
#include "chordlab/document.h"
#include "chordlab/feel.h"
#include "chordlab/link.h"
#include "chordlab/publication.h"
#include "chordlab/scheduler.h"
#include "chordlab/tracks.h"
#include "chordlab/triple_buffer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace chordlab;

namespace {

using Events = std::vector<NoteEvent>;

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

/// A bare document holding tracks with these ids (`cover` and `laneOf` read only the ids).
Document docWithTracks(const std::vector<int>& ids) {
    Document doc;
    for (const int id : ids) doc.tracks.push_back(Track{id, "t", TrackRole::Other});
    return doc;
}

LaneSelection only(std::vector<LaneKey> lanes) { return LaneSelection{LaneSelection::Mode::Only, std::move(lanes)}; }

LaneSelection allExcept(std::vector<LaneKey> lanes) {
    return LaneSelection{LaneSelection::Mode::AllExcept, std::move(lanes)};
}

/// The compiler's sort key: beat, then offs before ons, then ascending pitch.
bool keyLess(const NoteEvent& a, const NoteEvent& b) {
    if (a.beat != b.beat) return a.beat < b.beat;
    if (a.on != b.on) return !a.on;
    return a.pitch < b.pitch;
}

Events sortedUnion(Events a, const Events& b) {
    a.insert(a.end(), b.begin(), b.end());
    std::sort(a.begin(), a.end(), keyLess);
    return a;
}

std::uint64_t bitsOf(double value) {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

Events eventsOf(const SnapshotSlot& slot) {
    return Events(slot.events.begin(), slot.events.begin() + static_cast<std::ptrdiff_t>(slot.count));
}

/// A slot on the heap: kMaxSnapshotEvents events (~288 KB) is a lot of stack.
std::unique_ptr<SnapshotSlot> newSlot() {
    return std::make_unique<SnapshotSlot>();
}

/// Writes nonsense into every field a fill sets, to see whether a fill touched the slot.
void scribble(SnapshotSlot& slot) {
    slot.count = 99;
    slot.events[0] = NoteEvent{-1.0, true, 1, 1};
    slot.endBeat = -1.0;
    slot.loop = false;
    slot.followHost = false;
    slot.midiThru = false;
}

void checkScribbled(const SnapshotSlot& slot) {
    CHECK(slot.count == 99);
    CHECK(slot.events[0] == NoteEvent{-1.0, true, 1, 1});
    CHECK(slot.endBeat == -1.0);
    CHECK(slot.loop == false);
    CHECK(slot.followHost == false);
    CHECK(slot.midiThru == false);
}

void checkSamePublication(const Publication& a, const Publication& b) {
    CHECK(a.generation == b.generation);
    CHECK(a.compiled.events == b.compiled.events);
    CHECK(bitsOf(a.compiled.endBeat) == bitsOf(b.compiled.endBeat));
    CHECK(a.loop == b.loop);
    CHECK(a.followHost == b.followHost);
    CHECK(a.midiThru == b.midiThru);
}

/// C, Am, F, G7 of four beats (block chords, every chord pitch above 38), track A ("Low", bass: 36 on beat 0 and a
/// 60 on beat 1, which in the every-lane result cuts the C chord's 60) and track B ("High", lead: 38 on beat 2).
struct Linked {
    DocumentStore store;
    int a = 0;
    int b = 0;
    LaneUids uids;
    LaneUids::Generator make = counter();
    std::string uidA;
    std::string uidB;

    explicit Linked(bool shareAPitch = true) {
        store.appendChord(chord(0, "maj"), 4.0, 100);
        store.appendChord(chord(9, "min"), 4.0, 100);
        store.appendChord(chord(5, "maj"), 4.0, 100);
        store.appendChord(chord(7, "7"), 4.0, 100);
        std::vector<NoteSpec> aNotes{ns(36, 0, 960)};
        if (shareAPitch) aNotes.push_back(ns(60, 960, 960));
        a = addTrackWith(store, TrackRole::Bass, "Low", aNotes);
        b = addTrackWith(store, TrackRole::Lead, "High", {ns(38, 1920, 480)});
        REQUIRE(uids.cover(store.doc(), store.historyTrackIds(), make));
        uidA = uids.uidOf(a);
        uidB = uids.uidOf(b);
        for (const NoteEvent& e : compile(store.doc()).events) REQUIRE(e.pitch > 38);
    }

    std::shared_ptr<const LinkFeed> feed(const std::vector<std::string>& followed,
                                         const std::string& documentId = "doc-1") const {
        return buildLinkFeed(store.doc(), documentId, uids, "lineage-1", followed);
    }
};

}  // namespace

// --- lane uids --------------------------------------------------------------------------------------------------

TEST_CASE("link uids: cover gives every track one uid, once") {
    DocumentStore store;
    const int a = store.addTrack(TrackRole::Bass);
    const int b = store.addTrack(TrackRole::Lead);
    LaneUids uids;
    const LaneUids::Generator make = counter();
    CHECK(uids.entries().empty());
    REQUIRE(uids.cover(store.doc(), store.historyTrackIds(), make));
    const std::string uidA = uids.uidOf(a);
    const std::string uidB = uids.uidOf(b);
    CHECK(uidA == "uid-1");  // document order
    CHECK(uidB == "uid-2");
    CHECK(uids.entries() == std::map<int, std::string>{{a, uidA}, {b, uidB}});

    // Covered: nothing changes and the generator is not asked.
    CHECK_FALSE(uids.cover(store.doc(), store.historyTrackIds(), make));
    CHECK(uids.entries() == std::map<int, std::string>{{a, uidA}, {b, uidB}});

    // A new track gets a new uid; the old ones keep theirs.
    const int c = store.addTrack(TrackRole::Other);
    CHECK(uids.cover(store.doc(), store.historyTrackIds(), make));
    CHECK(uids.uidOf(c) == "uid-3");
    CHECK(uids.uidOf(a) == uidA);
    CHECK(uids.uidOf(b) == uidB);
    CHECK(uids.entries().size() == 3);

    CHECK(uids.uidOf(9999).empty());
    CHECK(uids.uidOf(kNoTrackId).empty());
    CHECK(LaneUids{}.uidOf(a).empty());
}

TEST_CASE("link uids: cover asks again when the generator returns a uid the table already holds") {
    DocumentStore store;
    const int a = store.addTrack(TrackRole::Bass);
    const int b = store.addTrack(TrackRole::Lead);
    const int c = store.addTrack(TrackRole::Other);
    // A table loaded from a set, and a per-process counter that restarted: its first answers are uids the table holds.
    LaneUids uids;
    uids.replace({{a, "uid-1"}, {b, "uid-2"}});
    int asked = 0;
    const LaneUids::Generator restarted = counter();
    const LaneUids::Generator make = [&] {
        ++asked;
        return restarted();
    };
    CHECK(uids.cover(store.doc(), store.historyTrackIds(), make));
    CHECK(asked == 3);  // uid-1 and uid-2 refused, uid-3 taken
    CHECK(uids.entries() == std::map<int, std::string>{{a, "uid-1"}, {b, "uid-2"}, {c, "uid-3"}});

    // A uid given to an earlier track in the same call is held too.
    DocumentStore two;
    const int x = two.addTrack(TrackRole::Bass);
    const int y = two.addTrack(TrackRole::Lead);
    LaneUids fresh;
    const std::vector<std::string> answers = {"same", "same", "other"};
    std::size_t next = 0;
    CHECK(fresh.cover(two.doc(), two.historyTrackIds(), [&] { return answers.at(next++); }));
    CHECK(next == 3);
    CHECK(fresh.entries() == std::map<int, std::string>{{x, "same"}, {y, "other"}});
}

TEST_CASE("link uids: a removed track keeps its entry and, restored by undo, its uid") {
    DocumentStore store;
    const int a = store.addTrack(TrackRole::Bass);
    const int b = store.addTrack(TrackRole::Lead);
    LaneUids uids;
    const LaneUids::Generator make = counter();
    REQUIRE(uids.cover(store.doc(), store.historyTrackIds(), make));
    const std::map<int, std::string> before = uids.entries();
    const std::string uidB = uids.uidOf(b);

    REQUIRE(store.removeTrack(b));
    CHECK_FALSE(uids.cover(store.doc(), store.historyTrackIds(), make));  // the entry stays: nothing changed
    CHECK(uids.entries() == before);
    CHECK(uids.uidOf(b) == uidB);
    CHECK_FALSE(uids.laneOf(store.doc(), uidB).has_value());  // but it names no lane of this document

    REQUIRE(store.undo());
    REQUIRE(store.findTrackIndex(b) >= 0);
    CHECK_FALSE(uids.cover(store.doc(), store.historyTrackIds(), make));
    CHECK(uids.uidOf(b) == uidB);
    CHECK(uids.uidOf(a) == before.at(a));
    const std::optional<LaneKey> lane = uids.laneOf(store.doc(), uidB);
    REQUIRE(lane.has_value());
    CHECK(*lane == trackLane(b));
}

TEST_CASE("link uids: laneOf names the chords lane, a track by its uid, and nothing else") {
    DocumentStore store;
    const int a = store.addTrack(TrackRole::Bass);
    const int b = store.addTrack(TrackRole::Lead);
    LaneUids uids;
    REQUIRE(uids.cover(store.doc(), store.historyTrackIds(), counter()));

    const std::optional<LaneKey> chords = uids.laneOf(store.doc(), kChordsLaneUid);
    REQUIRE(chords.has_value());
    CHECK(*chords == chordsLane());
    REQUIRE(LaneUids{}.laneOf(Document{}, "chords").has_value());  // with no table and no tracks too
    CHECK(*LaneUids{}.laneOf(Document{}, "chords") == chordsLane());

    const std::optional<LaneKey> laneA = uids.laneOf(store.doc(), uids.uidOf(a));
    REQUIRE(laneA.has_value());
    CHECK(*laneA == trackLane(a));
    const std::optional<LaneKey> laneB = uids.laneOf(store.doc(), uids.uidOf(b));
    REQUIRE(laneB.has_value());
    CHECK(*laneB == trackLane(b));

    CHECK_FALSE(uids.laneOf(store.doc(), "uid-404").has_value());
    CHECK_FALSE(uids.laneOf(store.doc(), "").has_value());
    CHECK_FALSE(uids.laneOf(store.doc(), "Chords").has_value());
    // The uid of a track that no longer exists names nothing.
    const std::string uidB = uids.uidOf(b);
    REQUIRE(store.removeTrack(b));
    CHECK_FALSE(uids.laneOf(store.doc(), uidB).has_value());
    // Nor does a uid in another document that holds no track with that number.
    CHECK_FALSE(uids.laneOf(docWithTracks({b + 100}), uids.uidOf(a)).has_value());
}

TEST_CASE("link uids: trackOf names a track by its uid, and 0 for the chords lane, an empty or an unknown uid") {
    DocumentStore store;
    const int a = store.addTrack(TrackRole::Bass);
    const int b = store.addTrack(TrackRole::Lead);
    LaneUids uids;
    REQUIRE(uids.cover(store.doc(), store.historyTrackIds(), counter()));
    REQUIRE(uids.entries().size() == 2);  // a table that holds entries: an unknown uid must not land on one
    CHECK(uids.trackOf(uids.uidOf(a)) == a);
    CHECK(uids.trackOf(uids.uidOf(b)) == b);
    CHECK(uids.trackOf("uid-404") == 0);
    CHECK(uids.trackOf("") == 0);
    CHECK(uids.trackOf(kChordsLaneUid) == 0);
    CHECK(LaneUids{}.trackOf("uid-1") == 0);
    // The entry of a track undo can bring back still answers (whether the document holds it is not asked).
    const std::string uidB = uids.uidOf(b);
    REQUIRE(store.removeTrack(b));
    REQUIRE(uids.cover(store.doc(), store.historyTrackIds(), counter()) == false);
    CHECK(uids.trackOf(uidB) == b);
    // A replaced table answers from its own entries only.
    uids.replace({{7, "uid-seven"}});
    CHECK(uids.trackOf("uid-seven") == 7);
    CHECK(uids.trackOf(uidB) == 0);
}

TEST_CASE("link uids: replace swaps the whole table") {
    DocumentStore store;
    const int a = store.addTrack(TrackRole::Bass);
    const int b = store.addTrack(TrackRole::Lead);
    LaneUids uids;
    const LaneUids::Generator make = counter();
    REQUIRE(uids.cover(store.doc(), store.historyTrackIds(), make));
    const std::string oldA = uids.uidOf(a);

    const std::map<int, std::string> loaded{{a, "x-a"}, {77, "x-77"}};
    uids.replace(loaded);
    CHECK(uids.entries() == loaded);
    CHECK(uids.uidOf(a) == "x-a");
    CHECK(uids.uidOf(b).empty());
    CHECK(uids.uidOf(77) == "x-77");
    REQUIRE(uids.laneOf(store.doc(), "x-a").has_value());
    CHECK(*uids.laneOf(store.doc(), "x-a") == trackLane(a));
    CHECK_FALSE(uids.laneOf(store.doc(), oldA).has_value());

    // The next cover gives b a fresh uid and keeps a's loaded entry; 77 is in neither the document nor the
    // history, so its entry goes.
    CHECK(uids.cover(store.doc(), store.historyTrackIds(), make));
    CHECK(uids.uidOf(b) == "uid-3");
    CHECK(uids.uidOf(a) == "x-a");
    CHECK(uids.uidOf(77).empty());
}

TEST_CASE("link uids: a reverted table never hands a reused track number its old uid") {
    // The revert case (plan I2): a link to track 2 must not find a different track 2 after the leader loads a
    // state saved before track 2 existed and then adds a track that reuses the number.
    DocumentStore store;
    const int first = store.addTrack(TrackRole::Bass);
    LaneUids uids;
    const LaneUids::Generator make = counter();
    REQUIRE(uids.cover(store.doc(), store.historyTrackIds(), make));
    const std::map<int, std::string> savedTable = uids.entries();  // saved while only `first` existed
    const Document savedDoc = store.doc();
    const int savedNextId = store.nextId();
    const int savedNextSectionId = store.nextSectionId();
    const int savedNextTrackId = store.nextTrackId();
    const int savedNextNoteId = store.nextNoteId();

    const int second = store.addTrack(TrackRole::Lead);
    REQUIRE(uids.cover(store.doc(), store.historyTrackIds(), make));
    const std::string oldUid = uids.uidOf(second);
    REQUIRE_FALSE(oldUid.empty());

    // Load the earlier state: its document and its table.
    store.replace(savedDoc, savedNextId, savedNextSectionId, savedNextTrackId, savedNextNoteId);
    uids.replace(savedTable);
    const int reused = store.addTrack(TrackRole::Other);
    REQUIRE(reused == second);  // the number comes back

    CHECK(uids.cover(store.doc(), store.historyTrackIds(), make));
    CHECK_FALSE(uids.uidOf(reused).empty());
    CHECK(uids.uidOf(reused) != oldUid);
    CHECK(uids.uidOf(first) == savedTable.at(first));
    CHECK_FALSE(uids.laneOf(store.doc(), oldUid).has_value());  // the old link finds nothing
    REQUIRE(uids.laneOf(store.doc(), uids.uidOf(reused)).has_value());
    CHECK(*uids.laneOf(store.doc(), uids.uidOf(reused)) == trackLane(reused));
}

TEST_CASE("link uids: an entry stays while its track is in the document or in the history, and only then") {
    LaneUids uids;
    const LaneUids::Generator make = counter();
    REQUIRE(uids.cover(docWithTracks({1, 2}), {}, make));
    const std::string uid1 = uids.uidOf(1);
    const std::string uid2 = uids.uidOf(2);

    CHECK_FALSE(uids.cover(docWithTracks({2}), {1}, make));  // track 1 vanished, but undo can bring it back
    CHECK(uids.uidOf(1) == uid1);
    CHECK(uids.cover(docWithTracks({2}), {}, make));  // neither the document nor the history: it goes
    CHECK(uids.uidOf(1).empty());
    CHECK(uids.uidOf(2) == uid2);
    CHECK(uids.cover(docWithTracks({2}), {2}, make) == false);  // a track of both: kept, nothing to change
    CHECK(make() == "uid-3");  // removals ask the generator nothing

    // A loaded table: entries of tracks in neither go at the next cover, whatever their number.
    std::map<int, std::string> big;
    for (int id = 1; id <= 200; ++id) big[id] = "big-" + std::to_string(id);
    uids.replace(big);
    CHECK(uids.cover(docWithTracks({5, 150}), {7, 199}, make));
    CHECK(uids.entries() ==
          std::map<int, std::string>{{5, "big-5"}, {7, "big-7"}, {150, "big-150"}, {199, "big-199"}});
}

TEST_CASE("link uids: 64 stale entries never push out a track that undo can bring back") {
    // Review finding: with a cap of 64 vanished entries, lowest numbers first, deleting the oldest live track of a
    // table that already held 64 stale entries evicted it at once, and undo brought it back with a new uid.
    LaneUids uids;
    const LaneUids::Generator make = counter();
    std::map<int, std::string> loaded{{1, "live-1"}, {2, "live-2"}};
    for (int id = 100; id < 164; ++id) loaded[id] = "stale-" + std::to_string(id);
    uids.replace(loaded);
    CHECK(uids.cover(docWithTracks({2}), {1, 2}, make));  // track 1 deleted, in the undo history; the stale go
    CHECK(uids.entries() == std::map<int, std::string>{{1, "live-1"}, {2, "live-2"}});
    CHECK_FALSE(uids.cover(docWithTracks({1, 2}), {2}, make));  // undone
    CHECK(uids.uidOf(1) == "live-1");
}

namespace {

/// A per-process counter that restarted ("uid-1", "uid-2", ...), counting how often it was asked.
struct Restarted {
    int asked = 0;
    LaneUids::Generator make() {
        return [this] { return "uid-" + std::to_string(++asked); };
    }
};

/// A table of `count` entries "uid-1" ... "uid-<count>" for tracks 1001, 1002, ...
std::map<int, std::string> heldUids(int count) {
    std::map<int, std::string> table;
    for (int i = 1; i <= count; ++i) table[1000 + i] = "uid-" + std::to_string(i);
    return table;
}

std::vector<int> heldTracks(int count) {
    std::vector<int> ids;
    for (int i = 1; i <= count; ++i) ids.push_back(1000 + i);
    return ids;
}

}  // namespace

TEST_CASE("link uids: stale entries are pruned before a track is covered, so they never use up the answers") {
    // Codex's case: 100 vanished entries uid-1..uid-100 that neither the document nor the history holds, a track
    // without an entry, and a restarted counter whose first 100 answers are exactly those uids.
    LaneUids uids;
    uids.replace(heldUids(100));
    Restarted counter;
    CHECK(uids.cover(docWithTracks({1}), {}, counter.make()));
    CHECK(uids.entries() == std::map<int, std::string>{{1, "uid-1"}});  // the stale ones went first
    CHECK(counter.asked == 1);
}

TEST_CASE("link uids: a non-repeating generator always covers a track, however many held uids it repeats first") {
    SUBCASE("100 reachable entries: the 101st answer") {
        LaneUids uids;
        uids.replace(heldUids(100));
        Restarted counter;
        CHECK(uids.cover(docWithTracks({1}), heldTracks(100), counter.make()));
        CHECK(uids.uidOf(1) == "uid-101");
        CHECK(counter.asked == 101);
        CHECK(uids.entries().size() == 101);
    }
    SUBCASE("kMaxInstanceLanes - 1 reachable entries, the most a covered table can hold beside the track") {
        const int held = static_cast<int>(kMaxInstanceLanes) - 1;
        LaneUids uids;
        uids.replace(heldUids(held));
        Restarted counter;
        CHECK(uids.cover(docWithTracks({1}), heldTracks(held), counter.make()));
        CHECK(uids.uidOf(1) == "uid-" + std::to_string(held + 1));
        CHECK(uids.entries().size() == kMaxInstanceLanes);
    }
}

TEST_CASE("link uids: a generator that repeats itself leaves the track uncovered for this call, and nothing aborts") {
    LaneUids uids;
    uids.replace({{2, "held"}});
    int asked = 0;
    const LaneUids::Generator stuck = [&] {
        ++asked;
        return std::string("held");
    };
    CHECK_FALSE(uids.cover(docWithTracks({1, 2}), {}, stuck));
    CHECK(uids.uidOf(1).empty());
    CHECK(uids.entries() == std::map<int, std::string>{{2, "held"}});
    CHECK(asked > 1);  // it was asked again, a bounded number of times
    CHECK(static_cast<std::size_t>(asked) <= uids.entries().size() + 64);
    // A later cover with a working generator covers it.
    Restarted counter;
    CHECK(uids.cover(docWithTracks({1, 2}), {}, counter.make()));
    CHECK(uids.uidOf(1) == "uid-1");
}

TEST_CASE("link feed: a track without a uid is left out of the feed, never listed with an empty uid") {
    Linked d;
    LaneUids partial;
    partial.replace({{d.a, d.uidA}});  // track b uncovered
    const auto feed = buildLinkFeed(d.store.doc(), "doc-1", partial, "lineage-1", {d.uidA});
    REQUIRE(feed->lanes.size() == 2);  // the chords and track a
    CHECK(feed->lanes[0].uid == kChordsLaneUid);
    CHECK(feed->lanes[1].uid == d.uidA);
    CHECK(feed->lanes[1].compiled);
    for (const LinkLane& lane : feed->lanes) CHECK_FALSE(lane.uid.empty());
}

TEST_CASE("link uids: a deleted track keeps its uid while 65 others come and go, then comes back by undo") {
    // Codex's case: delete track 1, create and delete 65 tracks, undo 131 times. The history holds every one of
    // them throughout (131 snapshots, under DocumentStore::kMaxUndoDepth).
    LaneUids uids;
    const LaneUids::Generator make = counter();
    REQUIRE(uids.cover(docWithTracks({1, 2}), {}, make));
    const std::string uid1 = uids.uidOf(1);
    std::vector<int> history{1, 2};
    uids.cover(docWithTracks({2}), history, make);
    for (int id = 3; id < 3 + 65; ++id) {
        uids.cover(docWithTracks({2, id}), history, make);
        history.push_back(id);
        std::sort(history.begin(), history.end());
        uids.cover(docWithTracks({2}), history, make);
        CHECK(uids.uidOf(1) == uid1);
    }
    CHECK(uids.entries().size() == 67);
    // Undone all the way: the history now holds what redo can bring back; track 1 is in the document again.
    CHECK_FALSE(uids.cover(docWithTracks({1, 2}), history, make));
    CHECK(uids.uidOf(1) == uid1);
    // A commit clears the redo history: the passing tracks are gone for good, and so are their entries.
    CHECK(uids.cover(docWithTracks({1, 2}), {1, 2}, make));
    CHECK(uids.entries().size() == 2);
    CHECK(uids.uidOf(1) == uid1);
}

// --- the feed ---------------------------------------------------------------------------------------------------

TEST_CASE("link feed: the chords lane, then the tracks in document order, with uid, name, role and audibility") {
    Linked d;
    const auto checkLanes = [&](const std::vector<int>& order) {
        const std::shared_ptr<const LinkFeed> feed = d.feed({});
        REQUIRE(feed);
        const Document& doc = d.store.doc();
        REQUIRE(feed->lanes.size() == 1 + order.size());
        const LinkLane& chords = feed->lanes[0];
        CHECK(chords.uid == kChordsLaneUid);
        CHECK(chords.chords);
        CHECK(chords.name == "Chords");
        CHECK(chords.audible == chordsAudible(doc));
        for (std::size_t i = 0; i < order.size(); ++i) {
            CAPTURE(i);
            const LinkLane& lane = feed->lanes[1 + i];
            REQUIRE(doc.tracks[i].id == order[i]);
            CHECK(lane.uid == d.uids.uidOf(order[i]));
            CHECK_FALSE(lane.chords);
            CHECK(lane.name == doc.tracks[i].name);
            CHECK(lane.role == doc.tracks[i].role);
            CHECK(lane.audible == trackAudible(doc, i));
        }
        for (const LinkLane& lane : feed->lanes) {
            CHECK_FALSE(lane.compiled);
            CHECK(lane.events.empty());
        }
        return feed;
    };
    SUBCASE("everything audible") {
        const auto feed = checkLanes({d.a, d.b});
        CHECK(feed->lanes[1].name == "Low");
        CHECK(feed->lanes[1].role == TrackRole::Bass);
        CHECK(feed->lanes[2].name == "High");
        CHECK(feed->lanes[2].role == TrackRole::Lead);
        for (const LinkLane& lane : feed->lanes) CHECK(lane.audible);
    }
    SUBCASE("a muted track") {
        REQUIRE(d.store.setTrackMute(d.a, true));
        const auto feed = checkLanes({d.a, d.b});
        CHECK(feed->lanes[0].audible);
        CHECK_FALSE(feed->lanes[1].audible);
        CHECK(feed->lanes[2].audible);
    }
    SUBCASE("muted chords") {
        REQUIRE(d.store.setChordsMute(true));
        const auto feed = checkLanes({d.a, d.b});
        CHECK_FALSE(feed->lanes[0].audible);
        CHECK(feed->lanes[1].audible);
    }
    SUBCASE("a soloed track silences the others") {
        REQUIRE(d.store.setTrackSolo(d.b, true));
        const auto feed = checkLanes({d.a, d.b});
        CHECK_FALSE(feed->lanes[0].audible);
        CHECK_FALSE(feed->lanes[1].audible);
        CHECK(feed->lanes[2].audible);
    }
    SUBCASE("soloed chords, and a muted soloed track") {
        REQUIRE(d.store.setChordsSolo(true));
        REQUIRE(d.store.setTrackMute(d.a, true));
        REQUIRE(d.store.setTrackSolo(d.a, true));
        const auto feed = checkLanes({d.a, d.b});
        CHECK(feed->lanes[0].audible);
        CHECK(feed->lanes[1].audible);
        CHECK_FALSE(feed->lanes[2].audible);
    }
    SUBCASE("moved and renamed tracks") {
        REQUIRE(d.store.moveTrack(d.b, 0));
        REQUIRE(d.store.patchTrack(d.a, std::string("Renamed"), TrackRole::Other));
        const auto feed = checkLanes({d.b, d.a});
        CHECK(feed->lanes[1].uid == d.uidB);
        CHECK(feed->lanes[2].uid == d.uidA);
        CHECK(feed->lanes[2].name == "Renamed");
        CHECK(feed->lanes[2].role == TrackRole::Other);
    }
}

TEST_CASE("link feed: a followed lane carries compilePlaybackFor of that lane alone, the others nothing") {
    const Linked d;
    const Document& doc = d.store.doc();
    // The sanity of the document: A's 60 shortens the chord's 60 in the every-lane result, so a lane's events
    // are not the every-lane events filtered by lane.
    REQUIRE(compilePlaybackFor(doc, only({chordsLane()})).events == compile(doc).events);
    REQUIRE(sortedUnion(sortedUnion(compile(doc).events, compilePlaybackFor(doc, only({trackLane(d.a)})).events),
                        compilePlaybackFor(doc, only({trackLane(d.b)})).events) != compilePlayback(doc).events);

    SUBCASE("one track") {
        const auto feed = d.feed({d.uidA});
        REQUIRE(feed->lanes.size() == 3);
        CHECK(feed->lanes[1].compiled);
        CHECK(feed->lanes[1].events == compilePlaybackFor(doc, only({trackLane(d.a)})).events);
        CHECK(feed->lanes[1].events.size() == 4);
        CHECK_FALSE(feed->lanes[0].compiled);
        CHECK(feed->lanes[0].events.empty());
        CHECK_FALSE(feed->lanes[2].compiled);
        CHECK(feed->lanes[2].events.empty());
    }
    SUBCASE("the chords lane") {
        const auto feed = d.feed({std::string(kChordsLaneUid)});
        CHECK(feed->lanes[0].compiled);
        CHECK(feed->lanes[0].events == compilePlaybackFor(doc, only({chordsLane()})).events);
        CHECK_FALSE(feed->lanes[1].compiled);
        CHECK_FALSE(feed->lanes[2].compiled);
    }
    SUBCASE("every lane, listed in another order") {
        const auto feed = d.feed({d.uidB, std::string(kChordsLaneUid), d.uidA});
        CHECK(feed->lanes[0].events == compilePlaybackFor(doc, only({chordsLane()})).events);
        CHECK(feed->lanes[1].events == compilePlaybackFor(doc, only({trackLane(d.a)})).events);
        CHECK(feed->lanes[2].events == compilePlaybackFor(doc, only({trackLane(d.b)})).events);
        for (const LinkLane& lane : feed->lanes) CHECK(lane.compiled);
    }
}

TEST_CASE("link feed: a followed lane that is not audible is compiled to nothing") {
    Linked d;
    SUBCASE("muted") {
        REQUIRE(d.store.setTrackMute(d.a, true));
        const auto feed = d.feed({d.uidA});
        CHECK(feed->lanes[1].compiled);
        CHECK(feed->lanes[1].events.empty());
        CHECK_FALSE(feed->lanes[1].audible);
    }
    SUBCASE("silenced by another lane's solo") {
        REQUIRE(d.store.setTrackSolo(d.b, true));
        const auto feed = d.feed({std::string(kChordsLaneUid), d.uidA, d.uidB});
        CHECK(feed->lanes[0].compiled);
        CHECK(feed->lanes[0].events.empty());
        CHECK(feed->lanes[1].compiled);
        CHECK(feed->lanes[1].events.empty());
        CHECK(feed->lanes[2].compiled);
        CHECK(feed->lanes[2].events == compilePlaybackFor(d.store.doc(), only({trackLane(d.b)})).events);
        CHECK(feed->lanes[2].events.size() == 2);
    }
}

TEST_CASE("link feed: a followed uid that names no lane is ignored") {
    Linked d;
    // A vanished track's uid is still in the table, but names no lane of the document.
    const int c = addTrackWith(d.store, TrackRole::Other, "Gone", {ns(40, 0, 960)});
    REQUIRE(d.uids.cover(d.store.doc(), d.store.historyTrackIds(), d.make));
    const std::string uidC = d.uids.uidOf(c);
    REQUIRE(d.store.removeTrack(c));
    const auto feed = d.feed({"uid-404", "", uidC, d.uidB, "Chords"});
    REQUIRE(feed->lanes.size() == 3);
    CHECK_FALSE(feed->lanes[0].compiled);
    CHECK_FALSE(feed->lanes[1].compiled);
    CHECK(feed->lanes[2].compiled);
    CHECK(feed->lanes[2].events == compilePlaybackFor(d.store.doc(), only({trackLane(d.b)})).events);
    for (const LinkLane& lane : feed->lanes) CHECK(lane.uid != uidC);
}

TEST_CASE("link feed: an empty uid in followed never counts as followed") {
    // Every track is covered here (so no lane has an empty uid and nothing asserts); without asserts an uncovered
    // track would carry uid "", and "" in `followed` must not mark it followed then either.
    const Linked d;
    const auto feed = d.feed({""});
    REQUIRE(feed->lanes.size() == 3);
    for (const LinkLane& lane : feed->lanes) {
        CHECK_FALSE(lane.compiled);
        CHECK(lane.events.empty());
    }
}

TEST_CASE("link feed: the frame and the caller's names are carried") {
    SUBCASE("defaults") {
        const Linked d;
        const auto feed = d.feed({});
        CHECK(bitsOf(feed->endBeat) == bitsOf(compile(d.store.doc()).endBeat));
        CHECK(feed->endBeat == 16.0);
        CHECK(feed->loop == true);
        CHECK(feed->followHost == true);
        CHECK(feed->documentId == "doc-1");
        CHECK(feed->lineage == "lineage-1");
    }
    SUBCASE("a performed non-tick-aligned document, loop and Follow Live off") {
        // Three 0.2506-beat chords played Pulse: compile's period is 722 / 960 beat, the authored sum 0.7518.
        DocumentStore store;
        for (int i = 0; i < 3; ++i) store.appendChord(chord(0, "maj"), 0.2506, 100);
        store.setFeel(Feel{Rhythm::Pulse});
        store.setProgressionLoop(false);
        store.setFollowHost(false);
        LaneUids uids;
        const auto feed = buildLinkFeed(store.doc(), "doc-7", uids, "lineage-3", {});
        CHECK(bitsOf(feed->endBeat) == bitsOf(compile(store.doc()).endBeat));
        CHECK(feed->endBeat == 722.0 / 960.0);
        CHECK(feed->endBeat != store.doc().endBeat());
        CHECK(feed->loop == false);
        CHECK(feed->followHost == false);
        CHECK(feed->documentId == "doc-7");
        CHECK(feed->lineage == "lineage-3");
        REQUIRE(feed->lanes.size() == 1);  // the chords lane only, with no tracks
        CHECK(feed->lanes[0].uid == kChordsLaneUid);
    }
}

// --- the follower's fill ----------------------------------------------------------------------------------------

TEST_CASE("link fill: the first fill publishes at kLinkGenerationBase + 1, even without a feed") {
    auto slot = newSlot();
    scribble(*slot);
    Publication last;
    SUBCASE("without a feed") {
        REQUIRE(fillSlotFromFeed(nullptr, "uid-1", "doc-1", *slot, last));
        CHECK(last.generation == kLinkGenerationBase + 1);
        CHECK(slot->count == 0);
        CHECK(slot->endBeat == 0.0);
        CHECK(slot->loop == true);
        CHECK(slot->followHost == true);
        CHECK(slot->midiThru == true);
        CHECK_FALSE(fillSlotFromFeed(nullptr, "uid-1", "doc-1", *slot, last));
        CHECK(last.generation == kLinkGenerationBase + 1);
    }
    SUBCASE("a track lane") {
        const Linked d;
        const auto feed = d.feed({d.uidA});
        REQUIRE(fillSlotFromFeed(feed.get(), d.uidA, "doc-1", *slot, last));
        CHECK(last.generation == kLinkGenerationBase + 1);
        CHECK(eventsOf(*slot) == feed->lanes[1].events);
        CHECK(eventsOf(*slot) == compilePlaybackFor(d.store.doc(), only({trackLane(d.a)})).events);
        CHECK(last.compiled.events == feed->lanes[1].events);
        CHECK(slot->endBeat == feed->endBeat);
        CHECK(slot->loop == true);
        CHECK(slot->followHost == true);
        CHECK(slot->midiThru == true);
    }
    SUBCASE("the chords lane, whole where the every-lane result cuts it") {
        const Linked d;
        const auto feed = d.feed({std::string(kChordsLaneUid)});
        REQUIRE(fillSlotFromFeed(feed.get(), kChordsLaneUid, "doc-1", *slot, last));
        CHECK(last.generation == kLinkGenerationBase + 1);
        CHECK(eventsOf(*slot) == compile(d.store.doc()).events);
    }
}

TEST_CASE("link fill: the same content again touches neither the slot nor last") {
    Linked d;
    auto slot = newSlot();
    Publication last;
    const auto feed = d.feed({d.uidA});
    REQUIRE(fillSlotFromFeed(feed.get(), d.uidA, "doc-1", *slot, last));
    const Publication before = last;

    scribble(*slot);
    CHECK_FALSE(fillSlotFromFeed(feed.get(), d.uidA, "doc-1", *slot, last));
    checkScribbled(*slot);
    checkSamePublication(last, before);

    // A new feed whose lane is unchanged, though other lanes and the lineage moved: still nothing.
    REQUIRE(d.store.setTrackMute(d.b, true));
    const auto rebuilt = buildLinkFeed(d.store.doc(), "doc-1", d.uids, "lineage-2", {d.uidA, d.uidB});
    REQUIRE(rebuilt.get() != feed.get());
    CHECK_FALSE(fillSlotFromFeed(rebuilt.get(), d.uidA, "doc-1", *slot, last));
    checkScribbled(*slot);
    checkSamePublication(last, before);

    // Without a feed: the same, once empty.
    auto other = newSlot();
    Publication empty;
    REQUIRE(fillSlotFromFeed(nullptr, d.uidA, "doc-1", *other, empty));
    const Publication emptyBefore = empty;
    scribble(*other);
    CHECK_FALSE(fillSlotFromFeed(nullptr, d.uidA, "doc-1", *other, empty));
    checkScribbled(*other);
    checkSamePublication(empty, emptyBefore);
}

TEST_CASE("link fill: content changes bump the generation, a followHost change alone keeps it") {
    Linked d;
    auto slot = newSlot();
    Publication last;
    const auto fill = [&] { return fillSlotFromFeed(d.feed({d.uidA}).get(), d.uidA, "doc-1", *slot, last); };
    REQUIRE(fill());
    REQUIRE(last.generation == kLinkGenerationBase + 1);
    const Events original = eventsOf(*slot);

    // The lane's notes: a velocity edit.
    const int noteId = d.store.doc().tracks.at(0).notes.at(0).id;
    REQUIRE(d.store.updateNotes(d.a, {NoteEdit{noteId, std::nullopt, std::nullopt, std::nullopt, 50}}));
    REQUIRE(fill());
    CHECK(last.generation == kLinkGenerationBase + 2);
    CHECK(eventsOf(*slot) != original);
    CHECK(eventsOf(*slot) == compilePlaybackFor(d.store.doc(), only({trackLane(d.a)})).events);
    CHECK_FALSE(fill());

    // An edit of another lane changes nothing here.
    REQUIRE(d.store.updateNotes(d.b, {NoteEdit{d.store.doc().tracks.at(1).notes.at(0).id, 39, std::nullopt,
                                               std::nullopt, std::nullopt}}));
    CHECK_FALSE(fill());
    CHECK(last.generation == kLinkGenerationBase + 2);

    // Follow Live alone: a fresh slot under the same generation.
    const Events edited = eventsOf(*slot);
    d.store.setFollowHost(false);
    REQUIRE(fill());
    CHECK(last.generation == kLinkGenerationBase + 2);
    CHECK(slot->followHost == false);
    CHECK(last.followHost == false);
    CHECK(eventsOf(*slot) == edited);
    CHECK_FALSE(fill());

    // The loop flag is content.
    d.store.setProgressionLoop(false);
    REQUIRE(fill());
    CHECK(last.generation == kLinkGenerationBase + 3);
    CHECK(slot->loop == false);
    CHECK(eventsOf(*slot) == edited);

    // So is endBeat: the last chord lengthened, the lane's notes (in the first chord) unchanged.
    REQUIRE(d.store.setLength(d.store.doc().events.at(3).id, 6.0));
    REQUIRE(fill());
    CHECK(last.generation == kLinkGenerationBase + 4);
    CHECK(slot->endBeat == 18.0);
    CHECK(eventsOf(*slot) == edited);
    CHECK(slot->followHost == false);
    CHECK(slot->loop == false);
}

TEST_CASE("link fill: another document, an unknown lane, an uncompiled lane or no feed give empty content") {
    Linked d;
    d.store.setProgressionLoop(false);
    d.store.setFollowHost(false);
    auto slot = newSlot();
    Publication last;
    const auto feed = d.feed({d.uidA});  // A compiled, the chords and B not
    const Events laneA = compilePlaybackFor(d.store.doc(), only({trackLane(d.a)})).events;
    REQUIRE_FALSE(laneA.empty());
    REQUIRE(fillSlotFromFeed(feed.get(), d.uidA, "doc-1", *slot, last));
    REQUIRE(eventsOf(*slot) == laneA);
    std::uint32_t generation = last.generation;

    // From content to each kind of empty: a new generation, count 0; and back to content: a new one again.
    const auto toEmptyAndBack = [&](const LinkFeed* emptyFeed, std::string_view uid, std::string_view documentId) {
        scribble(*slot);
        REQUIRE(fillSlotFromFeed(emptyFeed, uid, documentId, *slot, last));
        CHECK(last.generation == ++generation);
        CHECK(slot->count == 0);
        CHECK(last.compiled.events.empty());
        CHECK(slot->midiThru == true);
        if (emptyFeed != nullptr) {  // the feed's frame
            CHECK(slot->endBeat == feed->endBeat);
            CHECK(slot->endBeat == 16.0);
            CHECK(slot->loop == false);
            CHECK(slot->followHost == false);
        } else {  // no frame at all
            CHECK(slot->endBeat == 0.0);
            CHECK(slot->loop == true);
            CHECK(slot->followHost == true);
        }
        CHECK_FALSE(fillSlotFromFeed(emptyFeed, uid, documentId, *slot, last));
        REQUIRE(fillSlotFromFeed(feed.get(), d.uidA, "doc-1", *slot, last));
        CHECK(last.generation == ++generation);
        CHECK(eventsOf(*slot) == laneA);
        CHECK(slot->endBeat == 16.0);
    };
    SUBCASE("a feed of another document") { toEmptyAndBack(feed.get(), d.uidA, "doc-2"); }
    SUBCASE("a lane the feed does not have") { toEmptyAndBack(feed.get(), "uid-404", "doc-1"); }
    SUBCASE("a lane that was not compiled") { toEmptyAndBack(feed.get(), d.uidB, "doc-1"); }
    SUBCASE("the chords lane, not compiled") { toEmptyAndBack(feed.get(), kChordsLaneUid, "doc-1"); }
    SUBCASE("no feed") { toEmptyAndBack(nullptr, d.uidA, "doc-1"); }
    SUBCASE("one empty is the same content as another") {
        // Empty content in the same frame is the same content, whatever made it empty.
        REQUIRE(fillSlotFromFeed(feed.get(), d.uidA, "doc-2", *slot, last));
        CHECK(last.generation == ++generation);
        CHECK_FALSE(fillSlotFromFeed(feed.get(), "uid-404", "doc-1", *slot, last));
        CHECK_FALSE(fillSlotFromFeed(feed.get(), d.uidB, "doc-1", *slot, last));
        CHECK(last.generation == generation);
        // Without a feed the frame changes (endBeat 0, loop and Follow Live on): content, a new generation.
        REQUIRE(fillSlotFromFeed(nullptr, d.uidA, "doc-1", *slot, last));
        CHECK(last.generation == ++generation);
    }
}

TEST_CASE("link fill: the content follows the feed's documentId, not the follower's lane alone") {
    // The same document published under two document ids (a replaced project whose track kept its number and,
    // in this table, its uid): only the follower's own document id gets content.
    const Linked d;
    const auto mine = d.feed({d.uidA}, "doc-1");
    const auto replaced = d.feed({d.uidA}, "doc-2");
    auto slot = newSlot();
    Publication last;
    REQUIRE(fillSlotFromFeed(mine.get(), d.uidA, "doc-1", *slot, last));
    CHECK(slot->count == 4);
    REQUIRE(fillSlotFromFeed(replaced.get(), d.uidA, "doc-1", *slot, last));
    CHECK(slot->count == 0);
    CHECK(last.generation == kLinkGenerationBase + 2);
    REQUIRE(fillSlotFromFeed(replaced.get(), d.uidA, "doc-2", *slot, last));
    CHECK(slot->count == 4);
    CHECK(last.generation == kLinkGenerationBase + 3);
}

// --- together ---------------------------------------------------------------------------------------------------

TEST_CASE("link: a leader's slot and its followers' slots hold between them every lane once") {
    // No two lanes share a pitch here, so the every-lane merge has nothing to merge and the outputs partition it.
    Linked d(false);
    const auto checkPartition = [&](const std::vector<std::string>& followed) {
        CAPTURE(followed.size());
        const Document& doc = d.store.doc();
        LaneSelection own{LaneSelection::Mode::AllExcept, {}};
        for (const std::string& uid : followed) {
            const std::optional<LaneKey> lane = d.uids.laneOf(doc, uid);
            REQUIRE(lane.has_value());
            own.lanes.push_back(*lane);
        }
        auto leaderSlot = newSlot();
        Publication leader;
        REQUIRE(fillSlot(doc, own, *leaderSlot, leader));
        REQUIRE(leader.generation < kLinkGenerationBase);
        const auto feed = buildLinkFeed(doc, "doc-1", d.uids, "lineage-1", followed);
        Events all = eventsOf(*leaderSlot);
        for (const std::string& uid : followed) {
            auto followerSlot = newSlot();
            Publication follower;
            REQUIRE(fillSlotFromFeed(feed.get(), uid, "doc-1", *followerSlot, follower));
            CHECK(follower.generation > kLinkGenerationBase);
            CHECK(followerSlot->endBeat == leaderSlot->endBeat);
            CHECK(followerSlot->loop == leaderSlot->loop);
            all = sortedUnion(all, eventsOf(*followerSlot));
        }
        CHECK(all == compilePlayback(doc).events);
    };
    const std::string chords(kChordsLaneUid);
    SUBCASE("one track followed") {
        checkPartition({d.uidA});
        checkPartition({d.uidB});
    }
    SUBCASE("the chords followed") { checkPartition({chords}); }
    SUBCASE("two lanes followed by two followers") {
        checkPartition({d.uidA, d.uidB});
        checkPartition({chords, d.uidB});
    }
    SUBCASE("under mute and solo") {
        REQUIRE(d.store.setTrackMute(d.b, true));
        checkPartition({d.uidA});
        checkPartition({d.uidB});
        REQUIRE(d.store.setTrackSolo(d.a, true));
        checkPartition({d.uidA});
        checkPartition({chords});
    }
}

// --- the audio-side source --------------------------------------------------------------------------------------

namespace {

/// One instance's audio side, as the shell drives it: its own buffer and its link buffer (written here by hand, as
/// the instance's publishOwn and the registry would), a scheduler, and the LinkSource that picks between them. All
/// on the heap (a buffer is ~864 KB). What the scheduler plays is read off the scheduler itself (`sounds`): every
/// content published here is one note held over the whole four-beat loop, so after any block the content being
/// played is the one whose pitch sounds.
struct SourceRig {
    std::unique_ptr<TripleBuffer<SnapshotSlot>> own = std::make_unique<TripleBuffer<SnapshotSlot>>();
    std::unique_ptr<TripleBuffer<SnapshotSlot>> link = std::make_unique<TripleBuffer<SnapshotSlot>>();
    std::unique_ptr<Scheduler> scheduler = std::make_unique<Scheduler>();
    LinkSource source;
    std::array<MidiOut, 512> out{};
    double ppq = 0.0;
    std::uint32_t ownGeneration = 0;
    std::uint32_t linkGeneration = kLinkGenerationBase;
    LinkSource::Flags flags;
    std::uint64_t adoptedThisBlock = 0;

    SourceRig() { scheduler->reset(48000.0); }

    /// Writes one held note of `pitch` (none for a negative pitch) with the flags into `buffer`'s back slot.
    static void write(TripleBuffer<SnapshotSlot>& buffer, int pitch, bool followHost, bool thru) {
        SnapshotSlot& slot = buffer.beginWrite();
        slot.count = 0;
        if (pitch >= 0) {
            slot.events[0] = NoteEvent{0.0, true, pitch, 100};
            slot.events[1] = NoteEvent{4.0, false, pitch, 0};
            slot.count = 2;
        }
        slot.endBeat = 4.0;
        slot.loop = true;
        slot.followHost = followHost;
        slot.midiThru = thru;
    }
    /// A new own content (the next own generation).
    void publishOwn(int pitch, bool followHost = true, bool thru = true) {
        write(*own, pitch, followHost, thru);
        own->publish(++ownGeneration);
    }
    /// A new link content (the next link generation).
    void publishLink(int pitch, bool followHost = true, bool thru = true) {
        write(*link, pitch, followHost, thru);
        link->publish(++linkGeneration);
    }

    /// The top of a block and the block (0.2 beat at 120 bpm, 48 kHz), the transport running. Every call adopts at
    /// most once.
    void block(bool following) {
        const std::uint64_t before = source.adoptions();
        flags = source.select(*own, *link, following, *scheduler);
        adoptedThisBlock = source.adoptions() - before;
        REQUIRE(adoptedThisBlock <= 1);
        const BlockInfo info{ppq, 4800, 48000.0, 120.0, flags.followHost, flags.thru};
        std::size_t written = 0;
        REQUIRE(scheduler->process(info, out.data(), out.size(), written));
        ppq += 0.2;
    }
    bool sounds(int pitch) const { return scheduler->sounding(pitch); }
    /// Whether exactly `pitch` (or nothing, for a negative pitch) of the given set sounds.
    bool soundsOnly(int pitch, const std::vector<int>& candidates) const {
        for (const int p : candidates) {
            if (sounds(p) != (p == pitch)) return false;
        }
        return true;
    }
};

}  // namespace

TEST_CASE("link source: a fresh own publication is adopted; nothing is adopted without one") {
    SourceRig rig;
    rig.publishOwn(60);
    rig.block(false);
    CHECK(rig.adoptedThisBlock == 1);
    CHECK(rig.sounds(60));
    CHECK(rig.source.playing() == LinkSource::Source::Own);
    CHECK(rig.source.generation() == 1);
    rig.block(false);
    CHECK(rig.adoptedThisBlock == 0);
    CHECK(rig.sounds(60));
    rig.publishOwn(62);
    rig.block(false);
    CHECK(rig.adoptedThisBlock == 1);
    CHECK(rig.soundsOnly(62, {60, 62}));
    CHECK(rig.source.generation() == 2);
}

TEST_CASE("link source: a switch to the link adopts its current slot, the second time without a fresh one") {
    SourceRig rig;
    rig.publishOwn(60);
    rig.block(false);
    rig.publishLink(40);
    rig.block(true);  // a fresh link slot
    CHECK(rig.adoptedThisBlock == 1);
    CHECK(rig.soundsOnly(40, {40, 60}));
    CHECK(rig.source.playing() == LinkSource::Source::Link);
    CHECK(rig.source.generation() == kLinkGenerationBase + 1);
    rig.block(false);
    CHECK(rig.soundsOnly(60, {40, 60}));
    rig.block(true);  // nothing new in the link buffer: its current slot all the same
    CHECK(rig.adoptedThisBlock == 1);
    CHECK(rig.soundsOnly(40, {40, 60}));
    CHECK(rig.source.generation() == kLinkGenerationBase + 1);
    rig.block(true);
    CHECK(rig.adoptedThisBlock == 0);
    CHECK(rig.soundsOnly(40, {40, 60}));
}

TEST_CASE("link source: follow, play, unfollow with nothing edited returns to the own content") {
    SourceRig rig;
    rig.publishOwn(60);
    rig.block(false);
    rig.publishLink(40);
    for (int i = 0; i < 3; ++i) rig.block(true);
    REQUIRE(rig.soundsOnly(40, {40, 60}));
    rig.block(false);  // no own publication since the first: the own buffer's current slot
    CHECK(rig.adoptedThisBlock == 1);
    CHECK(rig.soundsOnly(60, {40, 60}));
    CHECK(rig.source.playing() == LinkSource::Source::Own);
    CHECK(rig.source.generation() == 1);
    rig.block(false);
    CHECK(rig.adoptedThisBlock == 0);
    CHECK(rig.soundsOnly(60, {40, 60}));
}

TEST_CASE("link source: own publications while following are acquired, not adopted, and play after the switch back") {
    SUBCASE("acquired in a block while following") {
        SourceRig rig;
        rig.publishOwn(60);
        rig.block(false);
        rig.publishLink(40);
        rig.block(true);
        rig.publishOwn(62);
        rig.block(true);
        CHECK(rig.adoptedThisBlock == 0);
        CHECK(rig.soundsOnly(40, {40, 60, 62}));
        CHECK(rig.own->currentGeneration() == 2);  // the consumer took it
        CHECK(rig.source.generation() == kLinkGenerationBase + 1);
        rig.block(false);
        CHECK(rig.soundsOnly(62, {40, 60, 62}));
        CHECK(rig.source.generation() == 2);
    }
    SUBCASE("published between the last block while following and the switch back") {
        // The fresh own slot and the switch arrive in one call: acquired first, then adopted once. An acquire after
        // the adopt would leave the scheduler on the slot it had just handed back to the producer (60).
        SourceRig rig;
        rig.publishOwn(60);
        rig.block(false);
        rig.publishLink(40);
        rig.block(true);
        rig.publishOwn(62);
        rig.block(false);
        CHECK(rig.adoptedThisBlock == 1);
        CHECK(rig.soundsOnly(62, {40, 60, 62}));
        CHECK(rig.source.generation() == 2);
        rig.publishOwn(64);  // the producer reuses the slot the scheduler left
        rig.block(false);
        CHECK(rig.soundsOnly(64, {40, 60, 62, 64}));
    }
    SUBCASE("published with the switch to the link") {
        SourceRig rig;
        rig.publishOwn(60);
        rig.block(false);
        rig.publishLink(40);
        rig.publishOwn(62);
        rig.block(true);
        CHECK(rig.adoptedThisBlock == 1);
        CHECK(rig.soundsOnly(40, {40, 60, 62}));
        CHECK(rig.own->currentGeneration() == 2);
        rig.block(false);
        CHECK(rig.soundsOnly(62, {40, 60, 62}));
    }
}

TEST_CASE("link source: thru always comes from the own buffer") {
    SourceRig rig;
    rig.publishLink(40, true, false);
    rig.block(true);  // no own publication yet
    CHECK(rig.flags.thru);
    rig.publishOwn(60, true, false);
    rig.block(true);
    CHECK_FALSE(rig.flags.thru);  // the link slot's midiThru is false too here, but means nothing
    rig.publishLink(41, true, true);
    rig.block(true);
    CHECK_FALSE(rig.flags.thru);
    rig.block(false);
    CHECK_FALSE(rig.flags.thru);
    rig.publishOwn(60, true, true);
    rig.publishLink(42, true, false);
    rig.block(true);
    CHECK(rig.flags.thru);
}

TEST_CASE("link source: followHost comes from the slot being played") {
    SourceRig rig;
    rig.publishOwn(60, false);
    rig.block(false);
    CHECK_FALSE(rig.flags.followHost);
    rig.block(true);  // the link buffer has never been published: its empty snapshot has no gate
    CHECK(rig.flags.followHost);
    rig.publishLink(40, true);
    rig.block(true);
    CHECK(rig.flags.followHost);
    rig.block(false);
    CHECK_FALSE(rig.flags.followHost);
    rig.publishOwn(60, true);
    rig.publishLink(40, false);  // a followHost change alone: same content, a fresh slot
    rig.block(true);
    CHECK_FALSE(rig.flags.followHost);
    rig.block(false);
    CHECK(rig.flags.followHost);
}

TEST_CASE("link source: an unpublished link buffer plays empty; the link buffer is never touched while not following") {
    SourceRig rig;
    rig.publishOwn(60);
    rig.block(false);
    REQUIRE(rig.sounds(60));
    rig.block(true);
    CHECK(rig.adoptedThisBlock == 1);
    CHECK_FALSE(rig.sounds(60));
    CHECK(rig.source.playing() == LinkSource::Source::Link);
    CHECK(rig.source.generation() == kLinkGenerationBase);
    rig.block(false);
    CHECK(rig.sounds(60));

    rig.publishLink(40);
    for (int i = 0; i < 3; ++i) rig.block(false);
    CHECK(rig.link->currentGeneration() == 0);  // never acquired: the publication still waits
    rig.block(true);
    CHECK(rig.soundsOnly(40, {40, 60}));
    CHECK(rig.link->currentGeneration() == kLinkGenerationBase + 1);
}

TEST_CASE("link source: an own buffer that was never published is adopted as empty under generation 0") {
    SUBCASE("on a switch back from the link") {
        SourceRig rig;
        rig.publishLink(40);
        rig.block(true);
        REQUIRE(rig.sounds(40));
        rig.block(false);
        CHECK(rig.adoptedThisBlock == 1);
        CHECK_FALSE(rig.sounds(40));
        CHECK(rig.source.playing() == LinkSource::Source::Own);
        CHECK(rig.source.generation() == 0);
        CHECK(rig.flags.thru);
        CHECK(rig.flags.followHost);
        rig.block(false);
        CHECK(rig.adoptedThisBlock == 0);
        rig.publishOwn(60);  // its first publication, generation 1, is a change
        rig.block(false);
        CHECK(rig.adoptedThisBlock == 1);
        CHECK(rig.soundsOnly(60, {40, 60}));
    }
    SUBCASE("on the very first block") {
        SourceRig rig;
        rig.block(false);
        CHECK(rig.adoptedThisBlock == 1);
        CHECK(rig.source.playing() == LinkSource::Source::Own);
        CHECK(rig.source.generation() == 0);
        CHECK(rig.scheduler->sounding(60) == false);
    }
}

TEST_CASE("link source: rapid toggles and publications, each block playing the selected buffer's latest") {
    SourceRig rig;
    std::uint32_t state = 12345u;
    const auto next = [&state] {
        state = state * 1664525u + 1013904223u;
        return state >> 16;
    };
    int ownPitch = -1;   // the latest published pitch of each buffer (none yet)
    int linkPitch = -1;
    std::vector<int> candidates;
    for (int p = 36; p < 48; ++p) candidates.push_back(p);
    for (int p = 60; p < 72; ++p) candidates.push_back(p);
    bool wasFollowing = false;
    bool first = true;
    for (int i = 0; i < 400; ++i) {
        CAPTURE(i);
        const unsigned r = next();
        bool ownFresh = false;
        bool linkFresh = false;
        if (r % 5 == 0) {
            ownPitch = 60 + static_cast<int>(next() % 12);
            rig.publishOwn(ownPitch);
            ownFresh = true;
        }
        if (r % 7 == 0) {
            linkPitch = 36 + static_cast<int>(next() % 12);
            rig.publishLink(linkPitch);
            linkFresh = true;
        }
        const bool following = (next() % 3) != 0 ? !wasFollowing : wasFollowing;  // mostly toggling
        rig.block(following);
        const bool switched = first || following != wasFollowing;
        CHECK(rig.adoptedThisBlock == ((switched || (following ? linkFresh : ownFresh)) ? 1u : 0u));
        CHECK(rig.soundsOnly(following ? linkPitch : ownPitch, candidates));
        CHECK(rig.source.playing() == (following ? LinkSource::Source::Link : LinkSource::Source::Own));
        wasFollowing = following;
        first = false;
    }
}

// --- the endpoint's previews (LinkRegistry rule 12) -------------------------------------------------------------

namespace {

/// One instance as the shell holds it, for previews: an endpoint, a scheduler and a panic flag (its preview sink),
/// a document store and its own buffer. Everything big on the heap.
struct PreviewInst {
    std::unique_ptr<Scheduler> scheduler = std::make_unique<Scheduler>();
    std::unique_ptr<std::atomic<bool>> panic = std::make_unique<std::atomic<bool>>(false);
    DocumentStore store;
    std::unique_ptr<TripleBuffer<SnapshotSlot>> own = std::make_unique<TripleBuffer<SnapshotSlot>>();
    Publication ownPublication;
    std::unique_ptr<LinkEndpoint> endpoint;

    PreviewInst(LinkRegistry& registry, const std::string& id, const LinkEndpoint::UidGenerator& uids,
                bool takesForwards = true) {
        scheduler->reset(48000.0);
        endpoint = std::make_unique<LinkEndpoint>(
            registry, id, uids, [] {},
            takesForwards ? LinkPreviewSink{scheduler.get(), panic.get()} : LinkPreviewSink{});
    }
    void publish() { endpoint->publishOwn(store, "lineage-" + endpoint->view().id, *own, ownPublication); }
    /// One block with the transport stopped: the panic flag first, as the shell's ProcessBlock.
    void block() {
        if (panic->exchange(false, std::memory_order_acq_rel)) scheduler->panic();
        std::array<MidiOut, 512> out{};
        std::size_t written = 0;
        scheduler->process(BlockInfo{0.0, 64, 48000.0, 120.0, false, true}, out.data(), out.size(), written);
    }
    std::vector<int> sounds() const {
        std::vector<int> out;
        for (int p = 0; p < 128; ++p) {
            if (scheduler->sounding(p)) out.push_back(p);
        }
        return out;
    }
    void follow(const PreviewInst& leader, const std::string& laneUid) {
        LinkRegistry::Hooks hooks;
        const std::string leaderId = leader.endpoint->view().id;
        REQUIRE(endpoint->follow(leaderId, laneUid, "lineage-" + leaderId, hooks) == LinkFollowResult::Applied);
    }
};

PreviewCmd previewStart(int id, int pitch) {
    PreviewCmd cmd{};
    cmd.kind = PreviewCmd::Start;
    cmd.id = id;
    cmd.velocity = 100;
    cmd.count = 1;
    cmd.pitches[0] = static_cast<std::uint8_t>(pitch);
    return cmd;
}

PreviewCmd previewStop(int id) {
    PreviewCmd cmd{};
    cmd.kind = PreviewCmd::Stop;
    cmd.id = id;
    return cmd;
}

}  // namespace

TEST_CASE("link endpoint: a preview Start names its lane by the track's uid, or the chords lane without a track") {
    LinkRegistry registry;
    const LinkEndpoint::UidGenerator uids = counter();
    PreviewInst leader(registry, "L", uids);
    leader.store.appendChord(chord(0, "maj"), 4.0, 100);
    const int bass = addTrackWith(leader.store, TrackRole::Bass, "Bass", {ns(36, 0, 960)});
    const int lead = addTrackWith(leader.store, TrackRole::Lead, "Lead", {ns(72, 0, 960)});
    leader.publish();
    const std::string bassUid = leader.endpoint->state().lanes.at(bass);
    PreviewInst onBass(registry, "B", uids);
    PreviewInst onChords(registry, "C", uids);
    PreviewInst silent(registry, "S", uids, false);  // follows the lead lane, takes no forwards
    onBass.follow(leader, bassUid);
    onChords.follow(leader, std::string(kChordsLaneUid));
    silent.follow(leader, leader.endpoint->state().lanes.at(lead));
    leader.publish();
    const auto blocks = [&] {
        for (PreviewInst* inst : {&leader, &onBass, &onChords, &silent}) inst->block();
    };

    CHECK(leader.endpoint->forwardPreview(bass, previewStart(1, 50)) == LinkForward::Forwarded);
    CHECK(leader.endpoint->forwardPreview(std::nullopt, previewStart(2, 52)) == LinkForward::Forwarded);
    CHECK(leader.endpoint->forwardPreview(lead, previewStart(3, 54)) == LinkForward::NotForwarded);  // no sink
    CHECK(leader.endpoint->forwardPreview(99, previewStart(4, 56)) == LinkForward::NotForwarded);    // no uid
    CHECK(leader.endpoint->forwardPreview(0, previewStart(5, 58)) == LinkForward::NotForwarded);
    CHECK(leader.endpoint->forwardPreview(-1, previewStart(6, 59)) == LinkForward::NotForwarded);
    blocks();
    CHECK(onBass.sounds() == std::vector<int>{50});
    CHECK(onChords.sounds() == std::vector<int>{52});
    CHECK(leader.sounds().empty());  // the endpoint pushes nothing into its own scheduler: that is the caller's
    CHECK(silent.sounds().empty());

    // An Extend or a Stop goes where its Start went, whatever track it is given.
    CHECK(leader.endpoint->forwardPreview(lead, previewStop(1)) == LinkForward::Forwarded);
    CHECK(leader.endpoint->forwardPreview(std::nullopt, previewStop(3)) == LinkForward::NotForwarded);
    blocks();
    CHECK(onBass.sounds().empty());
    CHECK(onChords.sounds() == std::vector<int>{52});

    // A follower's endpoint forwards nothing (nothing is bound to it).
    CHECK(onBass.endpoint->forwardPreview(std::nullopt, previewStart(7, 60)) == LinkForward::NotForwarded);

    // Panic: every bound follower that takes forwards is flagged, and the records are gone.
    CHECK(leader.endpoint->panicFollowers() == 2);
    CHECK(onBass.panic->load());
    CHECK(onChords.panic->load());
    CHECK_FALSE(leader.panic->load());
    CHECK(leader.endpoint->forwardPreview(std::nullopt, previewStop(2)) == LinkForward::NotForwarded);
    blocks();
    CHECK(onChords.sounds().empty());
    CHECK(onBass.endpoint->panicFollowers() == 0);
}

TEST_CASE("link endpoint: a follower's endpoint destroyed takes its records with it") {
    LinkRegistry registry;
    const LinkEndpoint::UidGenerator uids = counter();
    PreviewInst leader(registry, "L", uids);
    leader.publish();
    auto follower = std::make_unique<PreviewInst>(registry, "F", uids);
    follower->follow(leader, std::string(kChordsLaneUid));
    REQUIRE(leader.endpoint->forwardPreview(std::nullopt, previewStart(1, 60)) == LinkForward::Forwarded);
    follower.reset();  // the endpoint first (the member declared last), then its scheduler and flag
    CHECK(leader.endpoint->forwardPreview(std::nullopt, previewStop(1)) == LinkForward::NotForwarded);
    CHECK(leader.endpoint->panicFollowers() == 0);
}
