#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/compiler.h"
#include "chordlab/document.h"
#include "chordlab/feel.h"
#include "chordlab/link.h"
#include "chordlab/publication.h"
#include "chordlab/tracks.h"

#include <algorithm>
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
        REQUIRE(uids.cover(store.doc(), make));
        uidA = uids.uidOf(a);
        uidB = uids.uidOf(b);
        for (const NoteEvent& e : compile(store.doc()).events) REQUIRE(e.pitch > 38);
    }

    std::shared_ptr<const LinkFeed> feed(const std::vector<std::string>& followed,
                                         const std::string& documentId = "doc-1") const {
        return buildLinkFeed(store.doc(), documentId, uids, "lineage-1", "ChordLab A", followed);
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
    REQUIRE(uids.cover(store.doc(), make));
    const std::string uidA = uids.uidOf(a);
    const std::string uidB = uids.uidOf(b);
    CHECK(uidA == "uid-1");  // document order
    CHECK(uidB == "uid-2");
    CHECK(uids.entries() == std::map<int, std::string>{{a, uidA}, {b, uidB}});

    // Covered: nothing changes and the generator is not asked.
    CHECK_FALSE(uids.cover(store.doc(), make));
    CHECK(uids.entries() == std::map<int, std::string>{{a, uidA}, {b, uidB}});

    // A new track gets a new uid; the old ones keep theirs.
    const int c = store.addTrack(TrackRole::Other);
    CHECK(uids.cover(store.doc(), make));
    CHECK(uids.uidOf(c) == "uid-3");
    CHECK(uids.uidOf(a) == uidA);
    CHECK(uids.uidOf(b) == uidB);
    CHECK(uids.entries().size() == 3);

    CHECK(uids.uidOf(9999).empty());
    CHECK(uids.uidOf(kNoTrackId).empty());
    CHECK(LaneUids{}.uidOf(a).empty());
}

TEST_CASE("link uids: a removed track keeps its entry and, restored by undo, its uid") {
    DocumentStore store;
    const int a = store.addTrack(TrackRole::Bass);
    const int b = store.addTrack(TrackRole::Lead);
    LaneUids uids;
    const LaneUids::Generator make = counter();
    REQUIRE(uids.cover(store.doc(), make));
    const std::map<int, std::string> before = uids.entries();
    const std::string uidB = uids.uidOf(b);

    REQUIRE(store.removeTrack(b));
    CHECK_FALSE(uids.cover(store.doc(), make));  // the entry stays: nothing changed
    CHECK(uids.entries() == before);
    CHECK(uids.uidOf(b) == uidB);
    CHECK_FALSE(uids.laneOf(store.doc(), uidB).has_value());  // but it names no lane of this document

    REQUIRE(store.undo());
    REQUIRE(store.findTrackIndex(b) >= 0);
    CHECK_FALSE(uids.cover(store.doc(), make));
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
    REQUIRE(uids.cover(store.doc(), counter()));

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

TEST_CASE("link uids: replace swaps the whole table") {
    DocumentStore store;
    const int a = store.addTrack(TrackRole::Bass);
    const int b = store.addTrack(TrackRole::Lead);
    LaneUids uids;
    const LaneUids::Generator make = counter();
    REQUIRE(uids.cover(store.doc(), make));
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

    // The next cover gives b a fresh uid and keeps the loaded entries (77 vanished, kept).
    CHECK(uids.cover(store.doc(), make));
    CHECK(uids.uidOf(b) == "uid-3");
    CHECK(uids.uidOf(a) == "x-a");
    CHECK(uids.uidOf(77) == "x-77");
}

TEST_CASE("link uids: a reverted table never hands a reused track number its old uid") {
    // The revert case (plan I2): a link to track 2 must not find a different track 2 after the leader loads a
    // state saved before track 2 existed and then adds a track that reuses the number.
    DocumentStore store;
    const int first = store.addTrack(TrackRole::Bass);
    LaneUids uids;
    const LaneUids::Generator make = counter();
    REQUIRE(uids.cover(store.doc(), make));
    const std::map<int, std::string> savedTable = uids.entries();  // saved while only `first` existed
    const Document savedDoc = store.doc();
    const int savedNextId = store.nextId();
    const int savedNextSectionId = store.nextSectionId();
    const int savedNextTrackId = store.nextTrackId();
    const int savedNextNoteId = store.nextNoteId();

    const int second = store.addTrack(TrackRole::Lead);
    REQUIRE(uids.cover(store.doc(), make));
    const std::string oldUid = uids.uidOf(second);
    REQUIRE_FALSE(oldUid.empty());

    // Load the earlier state: its document and its table.
    store.replace(savedDoc, savedNextId, savedNextSectionId, savedNextTrackId, savedNextNoteId);
    uids.replace(savedTable);
    const int reused = store.addTrack(TrackRole::Other);
    REQUIRE(reused == second);  // the number comes back

    CHECK(uids.cover(store.doc(), make));
    CHECK_FALSE(uids.uidOf(reused).empty());
    CHECK(uids.uidOf(reused) != oldUid);
    CHECK(uids.uidOf(first) == savedTable.at(first));
    CHECK_FALSE(uids.laneOf(store.doc(), oldUid).has_value());  // the old link finds nothing
    REQUIRE(uids.laneOf(store.doc(), uids.uidOf(reused)).has_value());
    CHECK(*uids.laneOf(store.doc(), uids.uidOf(reused)) == trackLane(reused));
}

TEST_CASE("link uids: at most kMaxVanishedLaneUids vanished entries stay, the lowest numbers going first") {
    // Tracks 1 and 2 stay throughout; tracks 3, 4, ... appear one at a time beside them and vanish.
    constexpr int kExtra = 6;
    const int lastVanished = 2 + static_cast<int>(kMaxVanishedLaneUids) + kExtra;
    LaneUids uids;
    const LaneUids::Generator make = counter();
    std::map<int, std::string> given;
    const auto vanishedCount = [&](const std::vector<int>& existing) {
        std::size_t n = 0;
        for (const auto& entry : uids.entries()) {
            if (std::find(existing.begin(), existing.end(), entry.first) == existing.end()) ++n;
        }
        return n;
    };
    for (int id = 3; id <= lastVanished; ++id) {
        REQUIRE(uids.cover(docWithTracks({1, 2, id}), make));
        given[id] = uids.uidOf(id);
        CHECK(vanishedCount({1, 2, id}) <= kMaxVanishedLaneUids);
    }
    const std::string uid1 = uids.uidOf(1);
    const std::string uid2 = uids.uidOf(2);
    CHECK(uid1 == "uid-1");
    CHECK(uid2 == "uid-2");

    // The last one vanishes too: a removal alone is a change, and asks the generator nothing.
    CHECK(uids.cover(docWithTracks({1, 2}), make));
    CHECK(make() == "uid-" + std::to_string(lastVanished + 1));  // nothing was taken since
    CHECK(uids.entries().size() == 2 + kMaxVanishedLaneUids);
    CHECK(uids.uidOf(1) == uid1);  // the existing tracks keep theirs, though their numbers are the lowest
    CHECK(uids.uidOf(2) == uid2);
    for (int id = 3; id <= lastVanished; ++id) {
        CAPTURE(id);
        if (id < 3 + kExtra) {
            CHECK(uids.uidOf(id).empty());
        } else {
            CHECK(uids.uidOf(id) == given.at(id));
        }
    }
    CHECK_FALSE(uids.cover(docWithTracks({1, 2}), make));

    // A loaded table beyond the cap is trimmed by the next cover, existing tracks untouched.
    std::map<int, std::string> big;
    for (int id = 1; id <= 200; ++id) big[id] = "big-" + std::to_string(id);
    uids.replace(big);
    CHECK(uids.cover(docWithTracks({5, 150}), make));
    CHECK(uids.entries().size() == 2 + kMaxVanishedLaneUids);
    CHECK(uids.uidOf(5) == "big-5");
    CHECK(uids.uidOf(150) == "big-150");
    CHECK(uids.uidOf(200 - static_cast<int>(kMaxVanishedLaneUids)) == "big-" +
                                                                         std::to_string(200 - kMaxVanishedLaneUids));
    CHECK(uids.uidOf(200 - static_cast<int>(kMaxVanishedLaneUids) - 1).empty());
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
    REQUIRE(d.uids.cover(d.store.doc(), d.make));
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
        CHECK(feed->label == "ChordLab A");
    }
    SUBCASE("a performed non-tick-aligned document, loop and Follow Live off") {
        // Three 0.2506-beat chords played Pulse: compile's period is 722 / 960 beat, the authored sum 0.7518.
        DocumentStore store;
        for (int i = 0; i < 3; ++i) store.appendChord(chord(0, "maj"), 0.2506, 100);
        store.setFeel(Feel{Rhythm::Pulse});
        store.setProgressionLoop(false);
        store.setFollowHost(false);
        LaneUids uids;
        const auto feed = buildLinkFeed(store.doc(), "doc-7", uids, "lineage-3", "ChordLab B", {});
        CHECK(bitsOf(feed->endBeat) == bitsOf(compile(store.doc()).endBeat));
        CHECK(feed->endBeat == 722.0 / 960.0);
        CHECK(feed->endBeat != store.doc().endBeat());
        CHECK(feed->loop == false);
        CHECK(feed->followHost == false);
        CHECK(feed->documentId == "doc-7");
        CHECK(feed->lineage == "lineage-3");
        CHECK(feed->label == "ChordLab B");
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

    // A new feed whose lane is unchanged, though other lanes, the label and the lineage moved: still nothing.
    REQUIRE(d.store.setTrackMute(d.b, true));
    const auto rebuilt = buildLinkFeed(d.store.doc(), "doc-1", d.uids, "lineage-2", "ChordLab Z", {d.uidA, d.uidB});
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
        const auto feed = buildLinkFeed(doc, "doc-1", d.uids, "lineage-1", "ChordLab A", followed);
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
