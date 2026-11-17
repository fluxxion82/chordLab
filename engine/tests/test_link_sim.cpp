#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/compiler.h"
#include "chordlab/document.h"
#include "chordlab/link.h"
#include "chordlab/publication.h"
#include "chordlab/scheduler.h"
#include "chordlab/tracks.h"
#include "chordlab/triple_buffer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// The two-instance simulation (per-track instruments plan, Task 5): whole instances, each a document store, a
// LinkEndpoint, its own and its link buffer, a scheduler and a LinkSource, driven through scripted orders of
// main-thread work and audio blocks exactly as the shell will drive them. What each instance emits is compared,
// message for message and offset for offset, with a lone reference instance that plays the expected content as a
// document of its own (only the lane in question audible, or the lane muted), never with anything computed by the
// code under test. There is no idle step: everything must be right when the call that caused it returns.

using namespace chordlab;

namespace {

constexpr double kSr = 48000.0;
constexpr double kBpm = 120.0;
constexpr double kSamplesPerBeat = 24000.0;  // 120 bpm at 48 kHz
constexpr int kFrames = 2999;                 // about 0.125 beat, so that offsets vary
constexpr std::size_t kOutCapacity = 512;     // the shell's output buffer

using Bits = std::bitset<128>;
using Hooks = LinkRegistry::Hooks;
using Result = LinkEndpoint::LinkResult;

Chord chord(PitchClass root, const char* typeId) {
    return Chord{root, &chordType(typeId)};
}

NoteSpec ns(int pitch, std::int64_t startTick, std::int64_t lengthTicks, int velocity = 100) {
    return NoteSpec{pitch, startTick, lengthTicks, velocity};
}

int addTrackWith(DocumentStore& store, TrackRole role, std::string name, const std::vector<NoteSpec>& specs) {
    const int id = store.addTrack(role, std::move(name));
    REQUIRE(id != kNoTrackId);
    if (!specs.empty()) {
        std::string why;
        REQUIRE_MESSAGE(store.insertNotes(id, specs, &why).size() == specs.size(), why);
    }
    return id;
}

/// One MIDI message as an instance writes it.
struct Msg {
    int offset;
    int status;
    int pitch;
    int velocity;
};

bool operator==(const Msg& a, const Msg& b) {
    return a.offset == b.offset && a.status == b.status && a.pitch == b.pitch && a.velocity == b.velocity;
}

using Midi = std::vector<Msg>;

std::string describe(const Midi& midi) {
    std::ostringstream os;
    os << "[";
    for (std::size_t i = 0; i < midi.size(); ++i) {
        os << (i ? " " : "") << (midi[i].status == 0x90 ? "on" : "off") << midi[i].pitch << "@" << midi[i].offset;
    }
    os << "]";
    return os.str();
}

/// The messages after the block's first sample.
Midi tail(const Midi& midi) {
    Midi out;
    for (const Msg& m : midi) {
        if (m.offset > 0) out.push_back(m);
    }
    return out;
}

/// A uid source shared by every instance of a test: "u1", "u2", ...
LinkEndpoint::UidGenerator uidSource() {
    auto n = std::make_shared<int>(0);
    return [n] { return "u" + std::to_string(++*n); };
}

/// A document with its id counters, as a state load restores it.
struct DocState {
    Document doc;
    int nextId = 1;
    int nextSectionId = 0;
    int nextTrackId = 0;
    int nextNoteId = 0;
};

DocState docStateOf(const DocumentStore& store) {
    return DocState{store.doc(), store.nextId(), store.nextSectionId(), store.nextTrackId(), store.nextNoteId()};
}

/// An instance's saved state: its document and its link block.
struct Saved {
    DocState doc;
    LinkEndpoint::State link;
};

class FakeInstance;

/// The shell's lifeline: a hook reaches its instance only while it lives.
struct Lifeline {
    std::mutex mutex;
    FakeInstance* instance = nullptr;
};

/// One whole instance. Main-thread methods take `model_` (the shell's mModelMutex) as the shell will; `block` is
/// the audio thread's ProcessBlock (select, then process) and takes no lock. Everything big is on the heap.
class FakeInstance {
public:
    FakeInstance(LinkRegistry& registry, std::string id, LinkEndpoint::UidGenerator uids)
        : uids_(std::move(uids)),
          own_(std::make_unique<TripleBuffer<SnapshotSlot>>()),
          scheduler_(std::make_unique<Scheduler>()),
          lifeline_(std::make_shared<Lifeline>()) {
        scheduler_->reset(kSr);
        lineage_ = "lineage-" + uids_();
        lifeline_->instance = this;
        const std::shared_ptr<Lifeline> life = lifeline_;
        endpoint_ = std::make_unique<LinkEndpoint>(registry, std::move(id), uids_, [life] {
            const std::lock_guard<std::mutex> guard(life->mutex);
            if (life->instance != nullptr) life->instance->republish();
        });
        republish();  // the shell's constructor publishes its (empty) document
    }

    ~FakeInstance() {
        {
            const std::lock_guard<std::mutex> guard(lifeline_->mutex);
            lifeline_->instance = nullptr;
        }
        endpoint_.reset();  // removes the registration, then runs the returned hooks, holding nothing
    }

    FakeInstance(const FakeInstance&) = delete;
    FakeInstance& operator=(const FakeInstance&) = delete;

    // --- main thread ---------------------------------------------------------------------------------------

    /// A command: applies `fn` to the store and publishes, in one model-mutex section.
    void edit(const std::function<void(DocumentStore&)>& fn) {
        const std::lock_guard<std::mutex> lock(model_);
        fn(store_);
        publishLocked();
    }

    /// Replaces the document alone (a reference instance's setup), and publishes.
    void loadDocument(const DocState& state) {
        const std::lock_guard<std::mutex> lock(model_);
        store_.replace(state.doc, state.nextId, state.nextSectionId, state.nextTrackId, state.nextNoteId);
        publishLocked();
    }

    Saved save() const {
        const std::lock_guard<std::mutex> lock(model_);
        return Saved{docStateOf(store_), endpoint_->state()};
    }

    // An operation is its step under the model mutex, then the returned hooks with no lock held, then the
    // instance's own republish. The steps are public so that a test can put audio blocks between them.
    Result followStep(const std::string& leaderId, const std::string& laneUid, const std::string& leaderLineage,
                      Hooks& hooks) {
        const std::lock_guard<std::mutex> lock(model_);
        return endpoint_->follow(leaderId, laneUid, leaderLineage, hooks);
    }
    Hooks unfollowStep() {
        const std::lock_guard<std::mutex> lock(model_);
        Hooks hooks;
        endpoint_->unfollow(hooks);
        return hooks;
    }
    Hooks loadStep(const Saved& saved) {
        const std::lock_guard<std::mutex> lock(model_);
        store_.replace(saved.doc.doc, saved.doc.nextId, saved.doc.nextSectionId, saved.doc.nextTrackId,
                       saved.doc.nextNoteId);
        lineage_ = "lineage-" + uids_();  // a loaded document is a new lineage (tracks design D21)
        Hooks hooks;
        endpoint_->adopt(saved.link, hooks);
        return hooks;
    }
    static void run(const Hooks& hooks) {
        for (const auto& hook : hooks) {
            REQUIRE(hook != nullptr);
            REQUIRE(static_cast<bool>(*hook));
            (*hook)();
        }
    }

    Result follow(const std::string& leaderId, const std::string& laneUid, const std::string& leaderLineage) {
        Hooks hooks;
        const Result result = followStep(leaderId, laneUid, leaderLineage, hooks);
        run(hooks);
        republish();
        return result;
    }
    void unfollow() {
        run(unfollowStep());
        republish();
    }
    void load(const Saved& saved) {
        run(loadStep(saved));
        republish();
    }

    /// What its hook does: publishOwn and the own buffer's publish under the model mutex.
    void republish() {
        const std::lock_guard<std::mutex> lock(model_);
        publishLocked();
    }

    /// Run between publishOwn's return (the feed, and with it the followers' buffers, already published) and the
    /// own buffer's publish, if set: where a test puts audio blocks inside a republish.
    std::function<void()> betweenFillAndPublish;

    DocumentStore& store() { return store_; }
    LinkView view() const { return endpoint_->view(); }
    std::string id() const { return endpoint_->view().id; }
    std::uint64_t serial() const { return endpoint_->serial(); }
    /// The lane uid this instance gave track `trackId` (empty if none).
    std::string laneUid(int trackId) const {
        const auto lanes = endpoint_->state().lanes;
        const auto it = lanes.find(trackId);
        return it == lanes.end() ? std::string() : it->second;
    }

    // --- audio thread -------------------------------------------------------------------------------------

    /// ProcessBlock: LinkSource::select, then Scheduler::process with the flags it returned.
    Midi block(double ppq, int frames, double bpm = kBpm, bool playing = true) {
        const LinkSource::Flags flags =
            source_.select(*own_, endpoint_->linkBuffer(), endpoint_->following(), *scheduler_);
        const BlockInfo info{ppq, frames, kSr, bpm, playing && flags.followHost, flags.thru};
        std::size_t written = 0;
        complete_ = scheduler_->process(info, out_.data(), out_.size(), written);
        if (quiet) {
            faults += complete_ ? 0 : 1;
        } else {
            CHECK(complete_);
        }
        Midi midi;
        for (std::size_t i = 0; i < written; ++i) {
            const MidiOut& m = out_[i];
            const bool on = m.status == 0x90;
            REQUIRE((on || m.status == 0x80));
            // Every note-on from silence, every note-off of a sounding note, on this instance.
            if (quiet) {
                faults += (on == on_[m.data1]) ? 1 : 0;
            } else if (on) {
                CHECK_MESSAGE(!on_[m.data1], "a note-on of a sounding pitch ", int(m.data1));
            } else {
                CHECK_MESSAGE(on_[m.data1], "a note-off of a silent pitch ", int(m.data1));
            }
            on_[m.data1] = on;
            midi.push_back(Msg{m.sampleOffset, m.status, m.data1, m.data2});
        }
        return midi;
    }
    /// The pitches the scheduler sounds now.
    Bits sounding() const {
        Bits bits;
        for (int p = 0; p < 128; ++p) bits[static_cast<std::size_t>(p)] = scheduler_->sounding(p);
        return bits;
    }
    /// The pitches of the snapshot the scheduler plays.
    Bits played() {
        const SnapshotSlot* slot = source_.playing() == LinkSource::Source::Link ? endpoint_->linkBuffer().current()
                                   : source_.playing() == LinkSource::Source::Own ? own_->current()
                                                                                  : nullptr;
        Bits bits;
        if (slot != nullptr) {
            for (std::size_t i = 0; i < slot->count; ++i) bits[static_cast<std::size_t>(slot->events[i].pitch)] = true;
        }
        return bits;
    }
    std::uint32_t generation() const { return source_.generation(); }
    /// For a block on a thread of its own: no assertion inside `block`, faults counted in `faults` instead.
    bool quiet = false;
    int faults = 0;
    /// Nothing sounds, by the scheduler and by the messages written.
    bool silent() const { return sounding().none() && std::none_of(on_.begin(), on_.end(), [](bool b) { return b; }); }

private:
    void publishLocked() {
        const bool filled =
            endpoint_->publishOwn(store_.doc(), store_.revision(), lineage_, own_->beginWrite(), ownPublication_);
        if (betweenFillAndPublish) betweenFillAndPublish();
        if (filled) own_->publish(ownPublication_.generation);
    }

    LinkEndpoint::UidGenerator uids_;
    mutable std::mutex model_;
    DocumentStore store_;
    std::string lineage_;
    std::unique_ptr<TripleBuffer<SnapshotSlot>> own_;
    Publication ownPublication_;
    std::unique_ptr<Scheduler> scheduler_;
    LinkSource source_;
    std::shared_ptr<Lifeline> lifeline_;
    std::unique_ptr<LinkEndpoint> endpoint_;
    std::array<MidiOut, kOutCapacity> out_{};
    std::array<bool, 128> on_{};
    bool complete_ = true;
};

/// A registry and the uid source of one test. Instances are declared after it, so they go first.
struct World {
    LinkRegistry registry;
    LinkEndpoint::UidGenerator uids = uidSource();
    std::unique_ptr<FakeInstance> make(const std::string& id) {
        return std::make_unique<FakeInstance>(registry, id, uids);
    }
};

/// The lineage `viewer` sees for the leader `leaderId` (what its editor would send with a follow).
std::string lineageOf(const FakeInstance& viewer, const std::string& leaderId) {
    for (const LinkLeaderView& leader : viewer.view().leaders) {
        if (leader.id == leaderId) return leader.lineage;
    }
    FAIL("no such leader: " << leaderId);
    return {};
}

// --- documents ------------------------------------------------------------------------------------------------

constexpr int kBass1 = 36;  // the bass lane's pitches
constexpr int kBass2 = 41;
constexpr int kLead1 = 79;  // the lead lane's
constexpr int kLead2 = 81;

Bits pitches(std::initializer_list<int> list) {
    Bits bits;
    for (const int p : list) bits[static_cast<std::size_t>(p)] = true;
    return bits;
}

Bits chordPitches(const Document& doc) {
    Bits bits;
    for (const NoteEvent& e : compile(doc).events) bits[static_cast<std::size_t>(e.pitch)] = true;
    return bits;
}

/// C then Am, four beats each (endBeat 8, looped); track 1 "Bass" (36 on beat 0, 41 from beat 5 for two beats) and
/// track 2 "Lead" (79 on beat 3, 81 on beat 7). No two lanes share a pitch.
struct Song {
    DocState state;
    int bass = 0;
    int lead = 0;
};

Song song() {
    DocumentStore s;
    s.appendChord(chord(0, "maj"), 4.0, 100);
    s.appendChord(chord(9, "min"), 4.0, 100);
    Song out;
    out.bass = addTrackWith(s, TrackRole::Bass, "Bass", {ns(kBass1, 0, 960), ns(kBass2, 4800, 1920)});
    out.lead = addTrackWith(s, TrackRole::Lead, "Lead", {ns(kLead1, 2880, 480), ns(kLead2, 6720, 480, 90)});
    REQUIRE((chordPitches(s.doc()) & pitches({kBass1, kBass2, kLead1, kLead2})).none());
    out.state = docStateOf(s);
    return out;
}

/// Another project: E minor then G, three beats each, its track 1 "Bass" (48 on beat 1) and track 2 "Lead" (84 on
/// beat 4). Its track numbers are the song's.
Song otherSong() {
    DocumentStore s;
    s.appendChord(chord(4, "min"), 3.0, 100);
    s.appendChord(chord(7, "maj"), 3.0, 100);
    Song out;
    out.bass = addTrackWith(s, TrackRole::Bass, "Bass", {ns(48, 960, 960)});
    out.lead = addTrackWith(s, TrackRole::Lead, "Lead", {ns(84, 3840, 480)});
    REQUIRE((chordPitches(s.doc()) & pitches({kBass1, kBass2, kLead1, kLead2, 48, 84})).none());
    out.state = docStateOf(s);
    return out;
}

/// A follower's own document: one D major chord of four beats.
DocState followerOwn() {
    DocumentStore s;
    s.appendChord(chord(2, "maj"), 4.0, 100);
    REQUIRE((chordPitches(s.doc()) & pitches({kBass1, kBass2, kLead1, kLead2})).none());
    return docStateOf(s);
}

// --- transport and players --------------------------------------------------------------------------------------

/// One audio block as the host describes it: its first sample on the host's timeline, its length, its position
/// and its tempo.
struct Span {
    long start;
    int frames;
    double ppq;
    double bpm;
};

/// Blocks of the given sizes (cycled) from sample 0 to `total` at 120 bpm, ppq derived from the sample position as
/// a host does; `jitter` beats added and subtracted alternately from the second block on.
std::vector<Span> spansOf(long total, const std::vector<int>& sizes = {kFrames}, double jitter = 0.0) {
    std::vector<Span> spans;
    long position = 0;
    for (std::size_t n = 0; position < total; ++n) {
        const int frames = static_cast<int>(std::min<long>(sizes[n % sizes.size()], total - position));
        double ppq = static_cast<double>(position) / kSamplesPerBeat;
        if (n > 0) ppq += (n % 2 == 0) ? jitter : -jitter;
        spans.push_back(Span{position, frames, ppq, kBpm});
        position += frames;
    }
    return spans;
}

/// `count` blocks of kFrames at 120 bpm.
std::vector<Span> steady(std::size_t count) {
    return spansOf(static_cast<long>(count) * kFrames);
}

/// Blocks of the given sizes under a tempo map: 120 bpm until sample `change`, `bpmAfter` from there. A block's tempo
/// is the one at its first sample; its ppq is the map's exact position there.
std::vector<Span> spansWithTempoChange(long total, const std::vector<int>& sizes, long change, double bpmAfter) {
    std::vector<Span> spans;
    long position = 0;
    for (std::size_t n = 0; position < total; ++n) {
        const int frames = static_cast<int>(std::min<long>(sizes[n % sizes.size()], total - position));
        const double ppq = position <= change ? static_cast<double>(position) / kSamplesPerBeat
                                              : static_cast<double>(change) / kSamplesPerBeat +
                                                    static_cast<double>(position - change) * bpmAfter / (60.0 * kSr);
        spans.push_back(Span{position, frames, ppq, position < change ? kBpm : bpmAfter});
        position += frames;
    }
    return spans;
}

/// Runs one instance over a transport, span by span, and records what it did in each.
struct Player {
    FakeInstance* inst = nullptr;
    std::vector<Span> spans;
    Bits lane;  // the pitches of the lane under watch
    std::size_t next = 0;
    std::size_t first = 0;  // the first span it ran
    std::vector<Midi> out;
    std::vector<Bits> sounding;
    std::vector<bool> plays;  // the snapshot played in that block holds a pitch of `lane`
    std::vector<std::uint32_t> generation;
    std::vector<bool> during;  // the block began while an operation was under way

    Player(FakeInstance& instance, std::vector<Span> transport, Bits watched = {}, std::size_t from = 0)
        : inst(&instance), spans(std::move(transport)), lane(watched), next(from), first(from),
          out(spans.size()), sounding(spans.size()), plays(spans.size()), generation(spans.size()),
          during(spans.size()) {}

    void step(bool duringOp = false) {
        REQUIRE(next < spans.size());
        const Span& s = spans[next];
        out[next] = inst->block(s.ppq, s.frames, s.bpm);
        sounding[next] = inst->sounding();
        plays[next] = (inst->played() & lane).any();
        generation[next] = inst->generation();
        during[next] = duringOp;
        ++next;
    }
    void stepTo(std::size_t n) {
        while (next < n) step();
    }
    /// The block of span `i` adopted a new generation (or was the first: a start).
    bool adoptedAt(std::size_t i) const { return i == first || generation[i] != generation[i - 1]; }
    /// Every message of the run from span `from`, at its sample on the host's timeline (the offset into its
    /// block, which depends on the partition, set to 0).
    std::vector<std::pair<long, Msg>> absolute(std::size_t from = 0) const {
        std::vector<std::pair<long, Msg>> all;
        for (std::size_t i = std::max(from, first); i < next; ++i) {
            for (const Msg& m : out[i]) {
                all.emplace_back(spans[i].start + m.offset, Msg{0, m.status, m.pitch, m.velocity});
            }
        }
        return all;
    }
};

/// A lone instance playing `doc` (after `tweak`, a mute or a solo) over `spans`, in a registry of its own: the
/// expected output.
struct Reference {
    LinkRegistry registry;
    std::unique_ptr<FakeInstance> inst;
    std::unique_ptr<Player> player;
};

std::unique_ptr<Reference> reference(const DocState& doc, const std::function<void(DocumentStore&)>& tweak,
                                     const std::vector<Span>& spans) {
    auto ref = std::make_unique<Reference>();
    ref->inst = std::make_unique<FakeInstance>(ref->registry, "reference", uidSource());
    ref->inst->loadDocument(doc);
    if (tweak) ref->inst->edit(tweak);
    ref->player = std::make_unique<Player>(*ref->inst, spans);
    ref->player->stepTo(spans.size());
    return ref;
}

/// A reference that plays nothing.
std::unique_ptr<Reference> silence(const std::vector<Span>& spans) {
    return reference(DocState{}, nullptr, spans);
}

/// Spans [from, to) of `p` equal `ref`'s message for message, offset for offset.
void checkExact(const Player& p, const Player& ref, std::size_t from, std::size_t to, const std::string& what) {
    for (std::size_t i = from; i < to; ++i) {
        if (!(p.out[i] == ref.out[i])) {
            FAIL_CHECK(what << ": span " << i << ": " << describe(p.out[i]) << " expected " << describe(ref.out[i]));
            return;
        }
    }
}

/// From span `from` (the first block after an operation) to `to`: when that first block adopted a new generation
/// it is the switch's block, where both sides release and chase at offset 0 (scheduler.h): after it the same
/// pitches sound as in `ref`, and its messages after offset 0 are `ref`'s; every later block is exact and adopts
/// nothing new. Without an adopt in the first block, every block is exact.
void checkSettles(const Player& p, const Player& ref, std::size_t from, std::size_t to, const std::string& what) {
    REQUIRE(from < to);
    std::size_t exactFrom = from;
    if (p.adoptedAt(from)) {
        CHECK_MESSAGE(tail(p.out[from]) == tail(ref.out[from]),
                      what << ": switch block " << from << ": " << describe(p.out[from]) << " expected "
                           << describe(ref.out[from]) << " after offset 0");
        CHECK_MESSAGE(p.sounding[from] == ref.sounding[from], what << ": switch block " << from << " sounds "
                                                                    << p.sounding[from].count() << " pitches, "
                                                                    << ref.sounding[from].count() << " expected");
        exactFrom = from + 1;
    }
    for (std::size_t i = from + 1; i < to; ++i) {
        CHECK_MESSAGE(!p.adoptedAt(i), what << ": adopted again at span " << i);
    }
    checkExact(p, ref, exactFrom, to, what);
}

/// Stops the transport for `inst` and checks that nothing is left sounding.
void checkStopsClean(FakeInstance& inst, double ppq) {
    inst.block(ppq, kFrames, kBpm, false);
    inst.block(ppq, kFrames, kBpm, false);
    CHECK(inst.silent());
}

/// Runs both players in rounds up to span `n`.
void rounds(Player& a, Player& b, std::size_t n) {
    while (a.next < n || b.next < n) {
        if (a.next < n) a.step();
        if (b.next < n) b.step();
    }
}

}  // namespace

// --- 2. steady state ---------------------------------------------------------------------------------------------

TEST_CASE("link sim: a follower plays its lane exactly as a lone instance with only that lane, the leader the rest") {
    const Song s = song();
    const std::vector<Span> spans = steady(140);  // two loops and a bit
    const auto soloBass = reference(s.state, [&](DocumentStore& d) { d.setTrackSolo(s.bass, true); }, spans);
    const auto mutedBass = reference(s.state, [&](DocumentStore& d) { d.setTrackMute(s.bass, true); }, spans);

    SUBCASE("one follower on the bass") {
        World w;
        auto leader = w.make("L");
        leader->loadDocument(s.state);
        auto follower = w.make("F");
        follower->loadDocument(followerOwn());
        REQUIRE(follower->follow("L", leader->laneUid(s.bass), lineageOf(*follower, "L")) == Result::Applied);
        Player pl(*leader, spans);
        Player pf(*follower, spans);
        rounds(pl, pf, spans.size());
        checkExact(pf, *soloBass->player, 0, spans.size(), "follower");
        checkExact(pl, *mutedBass->player, 0, spans.size(), "leader");
        CHECK(follower->view().status == LinkStatus::Playing);
        checkStopsClean(*leader, 0.0);
        checkStopsClean(*follower, 0.0);
    }
    SUBCASE("two followers on one lane") {
        World w;
        auto leader = w.make("L");
        leader->loadDocument(s.state);
        auto f1 = w.make("F1");
        auto f2 = w.make("F2");
        REQUIRE(f1->follow("L", leader->laneUid(s.bass), lineageOf(*f1, "L")) == Result::Applied);
        REQUIRE(f2->follow("L", leader->laneUid(s.bass), lineageOf(*f2, "L")) == Result::Applied);
        Player pl(*leader, spans);
        Player p1(*f1, spans);
        Player p2(*f2, spans);
        for (std::size_t i = 0; i < spans.size(); ++i) {
            pl.step();
            p1.step();
            p2.step();
        }
        checkExact(p1, *soloBass->player, 0, spans.size(), "first follower");
        checkExact(p2, *soloBass->player, 0, spans.size(), "second follower");
        checkExact(pl, *mutedBass->player, 0, spans.size(), "leader");
    }
    SUBCASE("the chords lane followed") {
        const auto soloChords = reference(s.state, [](DocumentStore& d) { d.setChordsSolo(true); }, spans);
        const auto mutedChords = reference(s.state, [](DocumentStore& d) { d.setChordsMute(true); }, spans);
        World w;
        auto leader = w.make("L");
        leader->loadDocument(s.state);
        auto follower = w.make("F");
        REQUIRE(follower->follow("L", std::string(kChordsLaneUid), lineageOf(*follower, "L")) == Result::Applied);
        Player pl(*leader, spans);
        Player pf(*follower, spans);
        rounds(pl, pf, spans.size());
        checkExact(pf, *soloChords->player, 0, spans.size(), "follower");
        checkExact(pl, *mutedChords->player, 0, spans.size(), "leader");
    }
}

TEST_CASE("link sim: mute, solo, deleting the followed track and its undo reach both outputs with the call") {
    const Song s = song();
    const std::vector<Span> spans = steady(200);
    const auto soloBass = reference(s.state, [&](DocumentStore& d) { d.setTrackSolo(s.bass, true); }, spans);
    const auto mutedBass = reference(s.state, [&](DocumentStore& d) { d.setTrackMute(s.bass, true); }, spans);
    const auto nothing = silence(spans);

    World w;
    auto leader = w.make("L");
    leader->loadDocument(s.state);
    auto follower = w.make("F");
    follower->loadDocument(followerOwn());
    REQUIRE(follower->follow("L", leader->laneUid(s.bass), lineageOf(*follower, "L")) == Result::Applied);
    Player pl(*leader, spans);
    Player pf(*follower, spans);
    rounds(pl, pf, 20);
    checkExact(pf, *soloBass->player, 0, 20, "follower before");
    checkExact(pl, *mutedBass->player, 0, 20, "leader before");

    SUBCASE("the followed lane muted, then unmuted") {
        leader->edit([&](DocumentStore& d) { REQUIRE(d.setTrackMute(s.bass, true)); });
        CHECK(follower->view().status == LinkStatus::Muted);
        rounds(pl, pf, 60);
        checkSettles(pf, *nothing->player, 20, 60, "follower muted");
        checkExact(pl, *mutedBass->player, 20, 60, "leader with the followed lane muted");  // its content is unchanged
        leader->edit([&](DocumentStore& d) { REQUIRE(d.setTrackMute(s.bass, false)); });
        CHECK(follower->view().status == LinkStatus::Playing);
        rounds(pl, pf, 200);
        checkSettles(pf, *soloBass->player, 60, 200, "follower unmuted");
        checkExact(pl, *mutedBass->player, 60, 200, "leader unmuted");
    }
    SUBCASE("another lane soloed") {
        const auto chordsOnly = reference(s.state, [](DocumentStore& d) { d.setChordsSolo(true); }, spans);
        leader->edit([](DocumentStore& d) { REQUIRE(d.setChordsSolo(true)); });
        CHECK(follower->view().status == LinkStatus::Muted);
        rounds(pl, pf, 200);
        checkSettles(pf, *nothing->player, 20, 200, "follower");
        checkSettles(pl, *chordsOnly->player, 20, 200, "leader");
    }
    SUBCASE("the followed track deleted, then restored by undo with its uid") {
        const std::string uid = leader->laneUid(s.bass);
        leader->edit([&](DocumentStore& d) { REQUIRE(d.removeTrack(s.bass)); });
        CHECK(follower->view().status == LinkStatus::LaneMissing);
        rounds(pl, pf, 60);
        checkSettles(pf, *nothing->player, 20, 60, "follower, track deleted");
        checkExact(pl, *mutedBass->player, 20, 60, "leader, track deleted");
        leader->edit([](DocumentStore& d) { REQUIRE(d.undo()); });
        CHECK(leader->laneUid(s.bass) == uid);
        CHECK(follower->view().status == LinkStatus::Playing);
        rounds(pl, pf, 200);
        checkSettles(pf, *soloBass->player, 60, 200, "follower, track restored");
        checkExact(pl, *mutedBass->player, 60, 200, "leader, track restored");
    }
    SUBCASE("an edit of the followed lane reaches the follower with the edit call") {
        DocumentStore edited;
        edited.replace(s.state.doc, s.state.nextId, s.state.nextSectionId, s.state.nextTrackId, s.state.nextNoteId);
        REQUIRE(edited.insertNotes(s.bass, {ns(38, 1920, 960)}).size() == 1);
        const auto editedSolo =
            reference(docStateOf(edited), [&](DocumentStore& d) { d.setTrackSolo(s.bass, true); }, spans);
        leader->edit([&](DocumentStore& d) { REQUIRE(d.insertNotes(s.bass, {ns(38, 1920, 960)}).size() == 1); });
        rounds(pl, pf, 200);  // no step in between: the next blocks
        checkSettles(pf, *editedSolo->player, 20, 200, "follower after the edit");
        checkExact(pl, *mutedBass->player, 20, 200, "leader after the edit");
    }
    checkStopsClean(*leader, 0.0);
    checkStopsClean(*follower, 0.0);
}

// --- 3. cold restore ---------------------------------------------------------------------------------------------

TEST_CASE("link sim: a cold restore in either order is right from the very first block") {
    const Song s = song();
    const std::vector<Span> spans = steady(80);
    const auto soloBass = reference(s.state, [&](DocumentStore& d) { d.setTrackSolo(s.bass, true); }, spans);
    const auto mutedBass = reference(s.state, [&](DocumentStore& d) { d.setTrackMute(s.bass, true); }, spans);

    World w;
    Saved leaderState;
    Saved followerState;
    std::string leaderId;
    {
        auto leader = w.make("L");
        leader->loadDocument(s.state);
        auto follower = w.make("F");
        follower->loadDocument(followerOwn());
        REQUIRE(follower->follow("L", leader->laneUid(s.bass), lineageOf(*follower, "L")) == Result::Applied);
        leaderState = leader->save();
        followerState = follower->save();
        leaderId = leader->id();
        REQUIRE(followerState.link.follow.has_value());
        CHECK(followerState.link.follow->leaderId == leaderId);
    }  // the set closes: both destroyed

    const auto restore = [&](bool followerFirst) {
        CAPTURE(followerFirst);
        std::unique_ptr<FakeInstance> leader;
        std::unique_ptr<FakeInstance> follower;
        if (followerFirst) {
            follower = w.make("fresh-F");
            follower->load(followerState);
            CHECK(follower->view().status == LinkStatus::Waiting);
            leader = w.make("fresh-L");
            leader->load(leaderState);
        } else {
            leader = w.make("fresh-L");
            leader->load(leaderState);
            follower = w.make("fresh-F");
            follower->load(followerState);
        }
        CHECK(leader->id() == leaderId);
        CHECK(follower->view().status == LinkStatus::Playing);
        Player pl(*leader, spans);
        Player pf(*follower, spans);
        rounds(pl, pf, spans.size());
        checkExact(pf, *soloBass->player, 0, spans.size(), "follower");
        checkExact(pl, *mutedBass->player, 0, spans.size(), "leader");
        checkStopsClean(*leader, 0.0);
        checkStopsClean(*follower, 0.0);
    };
    SUBCASE("follower first") { restore(true); }
    SUBCASE("leader first") { restore(false); }
}

// --- 4. orderings --------------------------------------------------------------------------------------------

namespace {

/// What one interleaving showed: the spans in which the lane was on both outputs or on neither.
struct Overlap {
    int both = 0;
    int neither = 0;
};

enum class Op { Link, Unlink };

/// One link or unlink of the bass between leader L and follower F, with audio blocks put between its internal
/// steps: `g1` after the follower's step (the registry empties its link buffer and the flag turns), `g2` inside
/// the leader's hook between its publishOwn (whose feed writes the follower's buffer) and its own buffer's
/// publish, `g3` after the hook and before the follower's own republish. Each letter is one block of that
/// instance ('L' or 'F'), in order.
Overlap runOrdering(Op op, const std::string& g1, const std::string& g2, const std::string& g3, const Song& s,
                    const std::vector<Span>& spans, std::size_t pre, const Player& leaderBefore,
                    const Player& leaderAfter, const Player& followerBefore, const Player& followerAfter) {
    CAPTURE(g1);
    CAPTURE(g2);
    CAPTURE(g3);
    World w;
    auto leader = w.make("L");
    leader->loadDocument(s.state);
    auto follower = w.make("F");
    follower->loadDocument(followerOwn());
    const std::string lane = leader->laneUid(s.bass);
    if (op == Op::Unlink) REQUIRE(follower->follow("L", lane, lineageOf(*follower, "L")) == Result::Applied);

    const Bits bass = pitches({kBass1, kBass2});
    Player pl(*leader, spans, bass);
    Player pf(*follower, spans, bass);
    rounds(pl, pf, pre);
    checkExact(pl, leaderBefore, 0, pre, "leader before");
    checkExact(pf, followerBefore, 0, pre, "follower before");

    const auto gap = [&](const std::string& pattern) {
        for (const char c : pattern) (c == 'L' ? pl : pf).step(true);
    };
    Hooks hooks;
    if (op == Op::Link) {
        REQUIRE(follower->followStep("L", lane, lineageOf(*follower, "L"), hooks) == Result::Applied);
    } else {
        hooks = follower->unfollowStep();
    }
    REQUIRE(hooks.size() == 1);  // the leader's
    gap(g1);
    leader->betweenFillAndPublish = [&] { gap(g2); };
    FakeInstance::run(hooks);
    leader->betweenFillAndPublish = nullptr;
    gap(g3);
    follower->republish();

    // Everything after the whole operation returned.
    const std::size_t leaderFrom = pl.next;
    const std::size_t followerFrom = pf.next;
    rounds(pl, pf, spans.size());
    checkSettles(pl, leaderAfter, leaderFrom, spans.size(), "leader after");
    checkSettles(pf, followerAfter, followerFrom, spans.size(), "follower after");

    Overlap overlap;
    for (std::size_t i = 0; i < spans.size(); ++i) {
        const bool both = pl.plays[i] && pf.plays[i];
        const bool neither = !pl.plays[i] && !pf.plays[i];
        if (both || neither) {
            CHECK_MESSAGE((pl.during[i] || pf.during[i]),
                          "span " << i << ": the lane on " << (both ? "both outputs" : "neither output")
                                  << " in blocks that began after the operation");
        }
        overlap.both += both ? 1 : 0;
        overlap.neither += neither ? 1 : 0;
    }
    checkStopsClean(*leader, 0.0);
    checkStopsClean(*follower, 0.0);
    return overlap;
}

}  // namespace

TEST_CASE("link sim: link and unlink with audio blocks between every internal step") {
    const Song s = song();
    const std::size_t pre = 8;
    const std::vector<Span> spans = steady(pre + 80);  // after the operation, more than one loop (64 blocks)
    const auto all = reference(s.state, nullptr, spans);
    const auto soloBass = reference(s.state, [&](DocumentStore& d) { d.setTrackSolo(s.bass, true); }, spans);
    const auto mutedBass = reference(s.state, [&](DocumentStore& d) { d.setTrackMute(s.bass, true); }, spans);
    const auto own = reference(followerOwn(), nullptr, spans);

    const std::vector<std::string> patterns{"", "L", "F", "LF", "FL"};
    for (const Op op : {Op::Link, Op::Unlink}) {
        const std::string opName = op == Op::Link ? "link" : "unlink";
        CAPTURE(opName);
        Overlap most;
        int interleavings = 0;
        for (const std::string& g1 : patterns) {
            for (const std::string& g2 : patterns) {
                for (const std::string& g3 : patterns) {
                    const Overlap o =
                        op == Op::Link
                            ? runOrdering(op, g1, g2, g3, s, spans, pre, *all->player, *mutedBass->player,
                                          *own->player, *soloBass->player)
                            : runOrdering(op, g1, g2, g3, s, spans, pre, *mutedBass->player, *all->player,
                                          *soloBass->player, *own->player);
                    most.both = std::max(most.both, o.both);
                    most.neither = std::max(most.neither, o.neither);
                    ++interleavings;
                }
            }
        }
        CHECK(interleavings == 125);
        // What the interleavings show (the shell's order: the leader's feed, which writes the follower, is published
        // inside publishOwn, before the caller publishes the leader's own slot). Pinned, so that a change of order
        // shows up here.
        if (op == Op::Link) {
            CHECK(most.both == 2);
            CHECK(most.neither == 1);
        } else {
            CHECK(most.both == 0);
            CHECK(most.neither == 2);
        }
    }
}

TEST_CASE("link sim: audio threads keep running while links change and the leader edits (TSan)") {
    // The shell's threads: each instance's audio thread runs its blocks (select, process) without a lock while
    // the main thread follows, unfollows and edits. Nothing here waits for anything; at the end the last
    // operation's result plays.
    const Song s = song();
    World w;
    auto leader = w.make("L");
    leader->loadDocument(s.state);
    auto follower = w.make("F");
    follower->loadDocument(followerOwn());
    leader->quiet = true;
    follower->quiet = true;
    const std::string bassUid = leader->laneUid(s.bass);
    const std::string leadUid = leader->laneUid(s.lead);
    std::atomic<bool> stop{false};
    const auto audio = [&stop](FakeInstance& inst) {
        long position = 0;
        while (!stop.load(std::memory_order_acquire)) {
            inst.block(static_cast<double>(position) / kSamplesPerBeat, 256);
            position += 256;
        }
    };
    std::thread leaderAudio(audio, std::ref(*leader));
    std::thread followerAudio(audio, std::ref(*follower));
    for (int i = 0; i < 300; ++i) {
        switch (i % 5) {
            case 0: CHECK(follower->follow("L", bassUid, lineageOf(*follower, "L")) == Result::Applied); break;
            case 1: leader->edit([&](DocumentStore& d) { d.setTrackMute(s.bass, i % 2 == 0); }); break;
            case 2: CHECK(follower->follow("L", leadUid, lineageOf(*follower, "L")) == Result::Applied); break;
            case 3: leader->edit([&](DocumentStore& d) { d.setVelocity(d.doc().events[0].id, 60 + i % 40); }); break;
            default: follower->unfollow(); break;
        }
    }
    leader->edit([&](DocumentStore& d) { d.setTrackMute(s.bass, false); });
    REQUIRE(follower->follow("L", bassUid, lineageOf(*follower, "L")) == Result::Applied);
    stop.store(true, std::memory_order_release);
    leaderAudio.join();
    followerAudio.join();
    CHECK(leader->faults == 0);
    CHECK(follower->faults == 0);

    leader->block(1000.0, 256);
    follower->block(1000.0, 256);
    const Bits bass = pitches({kBass1, kBass2});
    CHECK((follower->played() & bass) == bass);
    CHECK((leader->played() & bass).none());
    leader->block(1000.0, 256, kBpm, false);
    follower->block(1000.0, 256, kBpm, false);
    CHECK(leader->silent());
    CHECK(follower->silent());
    CHECK(leader->faults == 0);
    CHECK(follower->faults == 0);
}

// --- 5. becoming a follower --------------------------------------------------------------------------------------

TEST_CASE("link sim: a leader that starts following silences its followers; when it stops they play again at once") {
    const Song a = song();
    const Song c = otherSong();
    const std::vector<Span> spans = steady(200);
    const auto aSoloBass = reference(a.state, [&](DocumentStore& d) { d.setTrackSolo(a.bass, true); }, spans);
    const auto aMutedBass = reference(a.state, [&](DocumentStore& d) { d.setTrackMute(a.bass, true); }, spans);
    const auto cSoloBass = reference(c.state, [&](DocumentStore& d) { d.setTrackSolo(c.bass, true); }, spans);
    const auto cMutedBass = reference(c.state, [&](DocumentStore& d) { d.setTrackMute(c.bass, true); }, spans);
    const auto cAll = reference(c.state, nullptr, spans);
    const auto nothing = silence(spans);

    World w;
    auto instA = w.make("A");
    instA->loadDocument(a.state);
    auto instB = w.make("B");
    auto instC = w.make("C");
    instC->loadDocument(c.state);
    REQUIRE(instB->follow("A", instA->laneUid(a.bass), lineageOf(*instB, "A")) == Result::Applied);
    Player pa(*instA, spans);
    Player pb(*instB, spans);
    Player pc(*instC, spans);
    const auto roundsTo = [&](std::size_t n) {
        while (pa.next < n) {
            pa.step();
            pb.step();
            pc.step();
        }
    };
    roundsTo(44);  // beat 5.5: B's 41 sounds
    checkExact(pb, *aSoloBass->player, 0, 44, "B before");
    checkExact(pa, *aMutedBass->player, 0, 44, "A before");
    REQUIRE(pb.sounding[43][kBass2]);

    REQUIRE(instA->follow("C", instC->laneUid(c.bass), lineageOf(*instA, "C")) == Result::Applied);
    CHECK(instB->view().status == LinkStatus::Waiting);
    REQUIRE(instB->view().follow.has_value());
    roundsTo(100);
    checkSettles(pb, *nothing->player, 44, 100, "B while A follows");
    CHECK(pb.out[44] == Midi{Msg{0, 0x80, kBass2, 0}});  // released with its note-off, at once
    checkSettles(pa, *cSoloBass->player, 44, 100, "A following C");
    checkSettles(pc, *cMutedBass->player, 44, 100, "C");

    instA->unfollow();
    CHECK(instB->view().status == LinkStatus::Playing);
    roundsTo(200);
    checkSettles(pb, *aSoloBass->player, 100, 200, "B after A stopped following");
    checkSettles(pa, *aMutedBass->player, 100, 200, "A leading again");
    checkSettles(pc, *cAll->player, 100, 200, "C");
    checkStopsClean(*instB, 0.0);
}

// --- 6. document replacement ----------------------------------------------------------------------------------

TEST_CASE("link sim: a different project in a bound leader is played by the leader alone; the same project keeps "
          "the follower playing") {
    const Song s = song();
    const Song other = otherSong();
    REQUIRE(other.bass == s.bass);  // the same track number
    const std::vector<Span> spans = steady(120);
    const auto soloBass = reference(s.state, [&](DocumentStore& d) { d.setTrackSolo(s.bass, true); }, spans);
    const auto mutedBass = reference(s.state, [&](DocumentStore& d) { d.setTrackMute(s.bass, true); }, spans);
    const auto otherAll = reference(other.state, nullptr, spans);
    const auto nothing = silence(spans);

    World w;
    auto leader = w.make("L");
    leader->loadDocument(s.state);
    auto follower = w.make("F");
    follower->loadDocument(followerOwn());
    const std::string lane = leader->laneUid(s.bass);
    REQUIRE(follower->follow("L", lane, lineageOf(*follower, "L")) == Result::Applied);
    Player pl(*leader, spans);
    Player pf(*follower, spans);
    rounds(pl, pf, 30);

    SUBCASE("a different project") {
        Saved otherProject;
        otherProject.doc = other.state;
        otherProject.link.id = "another-instance";
        otherProject.link.documentId = "another-document";
        SUBCASE("whose lane table gives that track number another uid") {
            otherProject.link.lanes = {{other.bass, "another-uid-1"}, {other.lead, "another-uid-2"}};
        }
        SUBCASE("whose lane table gives that track number the same uid (only the document id tells them apart)") {
            otherProject.link.lanes = {{other.bass, lane}, {other.lead, "another-uid-2"}};
        }
        leader->load(otherProject);
        CHECK(leader->id() == "L");  // bound followers: it keeps its id
        CHECK(follower->view().status == LinkStatus::ProjectChanged);
        rounds(pl, pf, spans.size());
        checkSettles(pl, *otherAll->player, 30, spans.size(), "leader with the other project");
        checkSettles(pf, *nothing->player, 30, spans.size(), "follower");
    }
    SUBCASE("the same project's own state") {
        const Saved same = leader->save();
        leader->load(same);
        CHECK(follower->view().status == LinkStatus::Playing);
        rounds(pl, pf, spans.size());
        checkExact(pl, *mutedBass->player, 0, spans.size(), "leader");  // no switch at all
        checkExact(pf, *soloBass->player, 0, spans.size(), "follower");
        for (std::size_t i = 31; i < spans.size(); ++i) CHECK_FALSE(pf.adoptedAt(i));
    }
    checkStopsClean(*leader, 0.0);
    checkStopsClean(*follower, 0.0);
}

// --- 7. revert ---------------------------------------------------------------------------------------------------

TEST_CASE("link sim: a reverted leader's new track with the followed track's number is not the followed lane") {
    // The leader before the bass existed: the chords and the lead (track 1).
    DocumentStore early;
    early.appendChord(chord(0, "maj"), 4.0, 100);
    early.appendChord(chord(9, "min"), 4.0, 100);
    const int lead = addTrackWith(early, TrackRole::Lead, "Lead", {ns(kLead1, 2880, 480)});
    const std::vector<Span> spans = steady(100);

    World w;
    auto leader = w.make("L");
    leader->loadDocument(docStateOf(early));
    const Saved before = leader->save();
    REQUIRE(before.link.lanes.size() == 1);
    int bass = 0;
    leader->edit([&](DocumentStore& d) { bass = addTrackWith(d, TrackRole::Bass, "Bass", {ns(kBass1, 0, 960)}); });
    auto follower = w.make("F");
    const std::string followed = leader->laneUid(bass);
    REQUIRE(follower->follow("L", followed, lineageOf(*follower, "L")) == Result::Applied);
    Player pl(*leader, spans);
    Player pf(*follower, spans);
    rounds(pl, pf, 20);
    REQUIRE(pf.sounding[0][kBass1]);

    leader->load(before);
    int again = 0;
    leader->edit([&](DocumentStore& d) { again = addTrackWith(d, TrackRole::Bass, "Bass", {ns(kBass1, 0, 960)}); });
    REQUIRE(again == bass);  // the number is handed out again
    CHECK(leader->laneUid(again) != followed);
    CHECK(follower->view().status == LinkStatus::LaneMissing);

    const auto leaderNow = reference(docStateOf(leader->store()), nullptr, spans);
    const auto nothing = silence(spans);
    rounds(pl, pf, spans.size());
    checkSettles(pl, *leaderNow->player, 20, spans.size(), "leader with its new track");
    checkSettles(pf, *nothing->player, 20, spans.size(), "follower");
    (void)lead;
}

// --- 8. races on the follower ------------------------------------------------------------------------------------

TEST_CASE("link sim: a follow with a stale leader lineage applies nothing") {
    const Song s = song();
    const std::vector<Span> spans = steady(60);
    const auto all = reference(s.state, nullptr, spans);
    const auto own = reference(followerOwn(), nullptr, spans);

    World w;
    auto leader = w.make("L");
    leader->loadDocument(s.state);
    auto follower = w.make("F");
    follower->loadDocument(followerOwn());
    const std::string lane = leader->laneUid(s.bass);
    const std::string seen = lineageOf(*follower, "L");  // what the follower's picker showed
    Player pl(*leader, spans);
    Player pf(*follower, spans);
    rounds(pl, pf, 10);

    leader->load(leader->save());  // the leader's document is replaced: a new lineage
    REQUIRE(lineageOf(*follower, "L") != seen);
    const std::uint64_t serial = follower->serial();
    CHECK(follower->follow("L", lane, seen) == Result::StaleLeader);
    CHECK(follower->serial() == serial);
    CHECK(follower->view().status == LinkStatus::NotFollowing);
    CHECK_FALSE(follower->view().follow.has_value());
    rounds(pl, pf, spans.size());
    checkExact(pl, *all->player, 0, spans.size(), "leader");
    checkExact(pf, *own->player, 0, spans.size(), "follower");
}

TEST_CASE("link sim: a follower's state load replaces its link atomically; the first block after plays the new one") {
    const Song s = song();
    const std::vector<Span> spans = steady(120);
    const auto soloBass = reference(s.state, [&](DocumentStore& d) { d.setTrackSolo(s.bass, true); }, spans);
    const auto mutedBass = reference(s.state, [&](DocumentStore& d) { d.setTrackMute(s.bass, true); }, spans);
    const auto soloLead = reference(s.state, [&](DocumentStore& d) { d.setTrackSolo(s.lead, true); }, spans);
    const auto mutedLead = reference(s.state, [&](DocumentStore& d) { d.setTrackMute(s.lead, true); }, spans);

    World w;
    auto leader = w.make("L");
    leader->loadDocument(s.state);
    auto follower = w.make("F");
    follower->loadDocument(followerOwn());
    REQUIRE(follower->follow("L", leader->laneUid(s.lead), lineageOf(*follower, "L")) == Result::Applied);
    Saved onLead = follower->save();  // the state to load: following the lead
    follower->unfollow();
    REQUIRE(follower->follow("L", leader->laneUid(s.bass), lineageOf(*follower, "L")) == Result::Applied);
    Player pl(*leader, spans);
    Player pf(*follower, spans);
    rounds(pl, pf, 30);
    checkExact(pf, *soloBass->player, 0, 30, "follower on the bass");

    // The load's own step: the link is the lead's at once, the buffer emptied; no moment follows nothing.
    const Hooks hooks = follower->loadStep(onLead);
    CHECK(follower->view().follow->laneUid == leader->laneUid(s.lead));
    pf.step(true);  // a block between the load and the leader's republish plays the emptied buffer
    CHECK(pf.sounding[30].none());
    CHECK((follower->played() & chordPitches(followerOwn().doc)).none());
    REQUIRE(hooks.size() == 1);  // the leader's, once: it was the old leader and is the new one
    FakeInstance::run(hooks);
    follower->republish();
    pl.step(true);  // span 30 of the leader, after the operation
    rounds(pl, pf, spans.size());
    checkSettles(pf, *soloLead->player, 31, spans.size(), "follower on the lead");
    checkSettles(pl, *mutedLead->player, 30, spans.size(), "leader");
    (void)mutedBass;
}

// --- 9. feed economy -----------------------------------------------------------------------------------------------

TEST_CASE("link sim: commands that leave revision, mutes, solos, flags and the followed set alone publish no feed") {
    const Song s = song();
    World w;
    auto leader = w.make("L");
    leader->loadDocument(s.state);
    auto follower = w.make("F");
    REQUIRE(follower->follow("L", leader->laneUid(s.bass), lineageOf(*follower, "L")) == Result::Applied);
    const std::vector<Span> spans = steady(40);
    Player pf(*follower, spans);
    pf.step();
    const std::uint32_t generation = pf.generation[0];

    const auto noFeed = [&](const char* what, const std::function<void(DocumentStore&)>& command) {
        CAPTURE(what);
        const std::uint64_t serial = leader->serial();
        leader->edit(command);
        CHECK(leader->serial() == serial);
        pf.step();
        CHECK(pf.generation[pf.next - 1] == generation);
    };
    noFeed("a chord selection", [](DocumentStore& d) { d.setSelectedEvent(d.doc().events[1].id); });
    noFeed("a note selection", [&](DocumentStore& d) {
        d.setNoteSelection(s.bass, {d.doc().tracks[0].notes[0].id});
    });
    noFeed("a browsed key", [](DocumentStore& d) { d.setBrowsedKey(Key{{'G', 0}, Mode::Ionian}); });
    noFeed("Thru", [](DocumentStore& d) { d.setMidiThru(false); });
    noFeed("a refused command", [](DocumentStore& d) { CHECK_FALSE(d.removeTrack(9999)); });
    noFeed("an undo with nothing to undo", [](DocumentStore& d) { CHECK_FALSE(d.undo()); });
    noFeed("a mute that is already set", [&](DocumentStore& d) { CHECK_FALSE(d.setTrackMute(s.lead, false)); });
    noFeed("nothing", [](DocumentStore&) {});

    SUBCASE("a mute of another lane publishes a feed; the follower's content stays as it is") {
        const std::uint64_t serial = leader->serial();
        leader->edit([&](DocumentStore& d) { REQUIRE(d.setTrackMute(s.lead, true)); });
        CHECK(leader->serial() != serial);
        pf.step();
        CHECK(pf.generation[pf.next - 1] == generation);
    }
    SUBCASE("Follow Live publishes a feed; the follower gets a fresh slot under the same generation") {
        const std::uint64_t serial = leader->serial();
        leader->edit([](DocumentStore& d) { d.setFollowHost(false); });
        CHECK(leader->serial() != serial);
        pf.step();
        CHECK(pf.generation[pf.next - 1] == generation);
        CHECK(pf.sounding[pf.next - 1].none());  // gated off by the leader's switch
    }
    SUBCASE("an edit publishes a feed") {
        const std::uint64_t serial = leader->serial();
        leader->edit([&](DocumentStore& d) { REQUIRE(d.insertNotes(s.lead, {ns(83, 960, 240)}).size() == 1); });
        CHECK(leader->serial() != serial);
    }
}

// --- 10. leader removed ------------------------------------------------------------------------------------------

TEST_CASE("link sim: a leader removed mid-run releases its followers, which wait and play again for its successor") {
    const Song s = song();
    const std::vector<Span> spans = steady(140);
    const auto soloBass = reference(s.state, [&](DocumentStore& d) { d.setTrackSolo(s.bass, true); }, spans);
    const auto soloLead = reference(s.state, [&](DocumentStore& d) { d.setTrackSolo(s.lead, true); }, spans);
    const auto chordsOnly = reference(s.state, [](DocumentStore& d) { d.setChordsSolo(true); }, spans);
    const auto nothing = silence(spans);

    World w;
    auto leader = w.make("L");
    leader->loadDocument(s.state);
    auto f1 = w.make("F1");
    auto f2 = w.make("F2");
    REQUIRE(f1->follow("L", leader->laneUid(s.bass), lineageOf(*f1, "L")) == Result::Applied);
    REQUIRE(f2->follow("L", leader->laneUid(s.lead), lineageOf(*f2, "L")) == Result::Applied);
    Player p1(*f1, spans);
    Player p2(*f2, spans);
    {
        Player pl(*leader, spans);
        for (std::size_t i = 0; i < 44; ++i) {
            pl.step();
            p1.step();
            p2.step();
        }
    }
    REQUIRE(p1.sounding[43][kBass2]);  // beat 5.5
    const Saved leaderState = leader->save();
    leader.reset();  // deleted mid-run
    CHECK(f1->view().status == LinkStatus::Waiting);
    CHECK(f2->view().status == LinkStatus::Waiting);
    CHECK(f1->view().follow->leaderId == "L");  // the link is kept
    for (std::size_t i = 44; i < 70; ++i) {
        p1.step();
        p2.step();
    }
    checkSettles(p1, *nothing->player, 44, 70, "first follower, leader gone");
    CHECK(p1.out[44] == Midi{Msg{0, 0x80, kBass2, 0}});  // released with its note-off
    checkSettles(p2, *nothing->player, 44, 70, "second follower, leader gone");

    auto successor = w.make("L2");  // the undo of the deletion: a new instance given the saved state
    successor->load(leaderState);
    CHECK(successor->id() == "L");
    CHECK(f1->view().status == LinkStatus::Playing);
    Player ps(*successor, spans, {}, 70);
    for (std::size_t i = 70; i < spans.size(); ++i) {
        ps.step();
        p1.step();
        p2.step();
    }
    checkSettles(p1, *soloBass->player, 70, spans.size(), "first follower, successor");
    checkSettles(p2, *soloLead->player, 70, spans.size(), "second follower, successor");
    checkSettles(ps, *chordsOnly->player, 70, spans.size(), "successor");
    checkStopsClean(*f1, 0.0);
    checkStopsClean(*f2, 0.0);
}

// --- 11. transport histories --------------------------------------------------------------------------------------

namespace {

/// A leader on the song and a follower of its bass.
struct Pair {
    World w;
    Song s = song();
    std::unique_ptr<FakeInstance> leader;
    std::unique_ptr<FakeInstance> follower;
    Pair() {
        leader = w.make("L");
        leader->loadDocument(s.state);
        follower = w.make("F");
        REQUIRE(follower->follow("L", leader->laneUid(s.bass), lineageOf(*follower, "L")) == Result::Applied);
    }
};

using Timeline = std::vector<std::pair<long, Msg>>;

/// The same messages in the same order, each at most `bound` samples from its counterpart. Returns the largest
/// distance seen.
long checkWithin(const Timeline& actual, const Timeline& expected, long bound, const std::string& what) {
    CAPTURE(what);
    REQUIRE(actual.size() == expected.size());
    REQUIRE(!expected.empty());
    long most = 0;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CAPTURE(i);
        CHECK(actual[i].second.status == expected[i].second.status);
        CHECK(actual[i].second.pitch == expected[i].second.pitch);
        CHECK(actual[i].second.velocity == expected[i].second.velocity);
        most = std::max(most, std::labs(actual[i].first - expected[i].first));
    }
    CHECK(most <= bound);
    return most;
}

}  // namespace

TEST_CASE("link sim: transport histories") {
    const long total = 20L * 24000L;  // 20 beats: two and a half loops

    SUBCASE("different block partitions for the two instances: exact") {
        Pair p;
        const auto ref = reference(p.s.state, [&](DocumentStore& d) { d.setTrackSolo(p.s.bass, true); },
                                   spansOf(total, {kFrames}));
        const auto refLeader = reference(p.s.state, [&](DocumentStore& d) { d.setTrackMute(p.s.bass, true); },
                                         spansOf(total, {kFrames}));
        Player pl(*p.leader, spansOf(total, {512, 1000, 37, 4096, 2048}));
        Player pf(*p.follower, spansOf(total, {7, 333, 4800, 1024}));
        pl.stepTo(pl.spans.size());
        pf.stepTo(pf.spans.size());
        CHECK(pf.absolute() == ref->player->absolute());
        CHECK(pl.absolute() == refLeader->player->absolute());
    }
    SUBCASE("a tempo change seen at different block boundaries: within the seek threshold") {
        // 120 to 119 bpm at beat 4. The follower's blocks of 7000 frames straddle the change (91000..98000) and
        // play that block at 120 throughout; 2000 samples later the host is 2000 * (1/24000 - 1/24201.7) beats,
        // about 7e-4, behind it: under kSeekThresholdBeats, so it re-anchors where it expected to be.
        Pair p;
        const long change = 96000;
        const std::vector<Span> fine = spansWithTempoChange(total, {3000}, change, 119.0);
        const std::vector<Span> coarse = spansWithTempoChange(total, {7000}, change, 119.0);
        const auto ref = reference(p.s.state, [&](DocumentStore& d) { d.setTrackSolo(p.s.bass, true); }, fine);
        const auto sameHistory =
            reference(p.s.state, [&](DocumentStore& d) { d.setTrackSolo(p.s.bass, true); }, coarse);
        Player pl(*p.leader, fine);
        Player pf(*p.follower, coarse);
        pl.stepTo(fine.size());
        pf.stepTo(coarse.size());
        CHECK(pf.absolute() == sameHistory->player->absolute());  // the same transport history: exact
        const double samplesPerBeat = kSr * 60.0 / 119.0;
        const long bound = static_cast<long>(std::ceil(Scheduler::kSeekThresholdBeats * samplesPerBeat)) + 1;
        const long most = checkWithin(pf.absolute(), ref->player->absolute(), bound, "follower against the reference");
        CHECK(most > 0);  // the histories do differ
    }
    SUBCASE("ppq jitter under the seek threshold: exact") {
        Pair p;
        const auto ref = reference(p.s.state, [&](DocumentStore& d) { d.setTrackSolo(p.s.bass, true); },
                                   spansOf(total, {kFrames}));
        Player pl(*p.leader, spansOf(total, {kFrames}));
        Player pf(*p.follower, spansOf(total, {kFrames}, 0.9 / 1024.0));
        rounds(pl, pf, pl.spans.size());
        CHECK(pf.absolute() == ref->player->absolute());
    }
    SUBCASE("a follower that starts processing mid-song: within the seek threshold") {
        Pair p;
        const std::vector<Span> spans = spansOf(total, {kFrames});
        const auto ref = reference(p.s.state, [&](DocumentStore& d) { d.setTrackSolo(p.s.bass, true); }, spans);
        Player pl(*p.leader, spans);
        pl.stepTo(45);
        Player pf(*p.follower, spans, {}, 45);  // its first block: the transport is at 5.62 beats, 41 sounding
        rounds(pl, pf, spans.size());
        REQUIRE(ref->player->sounding[45][kBass2]);
        CHECK(pf.sounding[45] == ref->player->sounding[45]);  // its start chases what sounds
        CHECK(tail(pf.out[45]) == tail(ref->player->out[45]));
        const long bound = static_cast<long>(std::ceil(Scheduler::kSeekThresholdBeats * kSamplesPerBeat)) + 1;
        checkWithin(pf.absolute(46), ref->player->absolute(46), bound, "follower after its first block");
    }
}
