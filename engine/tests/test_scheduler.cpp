#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/compiler.h"
#include "chordlab/document.h"
#include "chordlab/scheduler.h"
#include "chordlab/triple_buffer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ostream>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace chordlab;

namespace {

constexpr double kSr = 48000.0;
constexpr double kBpm = 120.0;
constexpr int kSamplesPerBeat = 24000;  // at 120 bpm, 48 kHz
constexpr std::size_t kBig = 1024;      // an output capacity nothing in these tests reaches

Chord chord(PitchClass root, std::string_view typeId) {
    return Chord{root, &chordType(typeId)};
}

/// A document holding the given chords in order, each `lengthBeats` long at velocity `velocity`.
Document documentOf(const std::vector<Chord>& chords, double lengthBeats = 4.0, int velocity = 100) {
    DocumentStore store;
    for (const Chord& c : chords) {
        store.appendChord(c, lengthBeats, velocity);
    }
    return store.doc();
}

/// One C chord of four beats: pitches 60, 64, 67 on at 0, off at 4, endBeat 4.
const Compiled& cFour() {
    static const Compiled compiled = compile(documentOf({chord(0, "maj")}, 4.0, 100));
    return compiled;
}

/// C (60, 64, 67) for beats 0..4 then Am (69, 72, 76) for beats 4..8, endBeat 8.
const Compiled& cThenAm() {
    static const Compiled compiled = compile(documentOf({chord(0, "maj"), chord(9, "min")}, 4.0, 100));
    return compiled;
}

/// Am (69, 72, 76) alone for four beats.
const Compiled& amFour() {
    static const Compiled compiled = compile(documentOf({chord(9, "min")}, 4.0, 100));
    return compiled;
}

Snapshot snapshotOf(const Compiled& compiled, bool loop = true, std::uint32_t generation = 1) {
    return Snapshot{compiled.events.data(), compiled.events.size(), compiled.endBeat, loop, generation};
}

Snapshot snapshotOf(const std::vector<NoteEvent>& events, double endBeat, bool loop = true,
                    std::uint32_t generation = 1) {
    return Snapshot{events.data(), events.size(), endBeat, loop, generation};
}

/// A decoded output event, for readable assertions.
struct Ev {
    int offset;
    bool on;
    int pitch;
    int velocity;
};

bool operator==(const Ev& a, const Ev& b) {
    return a.offset == b.offset && a.on == b.on && a.pitch == b.pitch && a.velocity == b.velocity;
}

std::ostream& operator<<(std::ostream& os, const Ev& e) {
    return os << (e.on ? "On" : "Off") << e.pitch << "@" << e.offset << "v" << e.velocity;
}

Ev on(int offset, int pitch, int velocity = 100) {
    return Ev{offset, true, pitch, velocity};
}

Ev off(int offset, int pitch) {
    return Ev{offset, false, pitch, 0};
}

std::string describe(const std::vector<Ev>& events) {
    std::ostringstream os;
    os << "[";
    for (std::size_t i = 0; i < events.size(); ++i) {
        os << (i ? ", " : "") << events[i];
    }
    os << "]";
    return os.str();
}

std::vector<Ev> decode(const MidiOut* out, std::size_t written) {
    std::vector<Ev> events;
    for (std::size_t i = 0; i < written; ++i) {
        const MidiOut& m = out[i];
        const bool isOn = m.status == 0x90;
        REQUIRE((isOn || m.status == 0x80));
        if (!isOn) {
            REQUIRE(m.data2 == 0);
        }
        events.push_back(Ev{m.sampleOffset, isOn, m.data1, m.data2});
    }
    return events;
}

/// Drives one scheduler block by block at `sampleRate` (48 kHz unless given).
struct Host {
    Scheduler scheduler;
    double sampleRate;
    std::array<MidiOut, kBig> buffer{};
    std::size_t lastWritten = 0;
    bool lastOk = true;
    bool thru = true;  // BlockInfo::thru of every block `run` processes

    explicit Host(double sampleRate_ = kSr) : sampleRate(sampleRate_) {
        scheduler.reset(sampleRate);
    }

    std::vector<Ev> run(double ppqStart, int nFrames, bool playing, double bpm = kBpm, std::size_t capacity = kBig) {
        const BlockInfo info{ppqStart, nFrames, sampleRate, bpm, playing, thru};
        lastOk = scheduler.process(info, buffer.data(), capacity, lastWritten);
        REQUIRE(lastWritten <= capacity);
        return decode(buffer.data(), lastWritten);
    }

    void adopt(const Snapshot& snapshot) {
        scheduler.adopt(snapshot);
    }

    /// Queues one incoming note for the next block (Scheduler::pushThru).
    bool pushThru(bool on, int pitch, int velocity, int offset) {
        return scheduler.pushThru(
            ThruEvent{on, static_cast<std::uint8_t>(pitch), static_cast<std::uint8_t>(velocity), offset});
    }

    std::uint8_t owners(int pitch) const {
        return scheduler.ownersOf(pitch);
    }
};

PreviewCmd previewStart(int id, std::vector<int> pitches, int velocity = 90) {
    PreviewCmd cmd{};
    cmd.kind = PreviewCmd::Start;
    cmd.id = id;
    cmd.velocity = velocity;
    cmd.count = static_cast<std::uint8_t>(pitches.size());
    for (std::size_t i = 0; i < pitches.size(); ++i) {
        cmd.pitches[i] = static_cast<std::uint8_t>(pitches[i]);
    }
    return cmd;
}

PreviewCmd previewStop(int id) {
    PreviewCmd cmd{};
    cmd.kind = PreviewCmd::Stop;
    cmd.id = id;
    return cmd;
}

PreviewCmd previewExtend(int id) {
    PreviewCmd cmd{};
    cmd.kind = PreviewCmd::Extend;
    cmd.id = id;
    return cmd;
}

/// One stopped 48 kHz block of `nFrames`, which must be complete (`process` returned true).
std::vector<Ev> previewBlock(Host& host, int nFrames = 4800) {
    std::vector<Ev> events = host.run(0.0, nFrames, false);
    REQUIRE(host.lastOk);
    return events;
}

const std::vector<Ev> kNone{};

}  // namespace

// --- 1. start ----------------------------------------------------------------------------------

TEST_CASE("scheduler: stopped -> playing at ppq 0 emits exactly three ons at offset 0") {
    Host host;
    host.adopt(snapshotOf(cFour()));

    const std::vector<Ev> events = host.run(0.0, 512, true);
    CAPTURE(describe(events));
    CHECK(host.lastOk);
    CHECK(events == std::vector<Ev>{on(0, 60), on(0, 64), on(0, 67)});
    CHECK(host.owners(60) == kOwnerProgression);
    CHECK(host.owners(64) == kOwnerProgression);
    CHECK(host.owners(67) == kOwnerProgression);
    CHECK(host.owners(48) == 0);  // a pitch outside the voicing
}

// --- 2. wrap inside a block ------------------------------------------------------------------

TEST_CASE("scheduler: block [3.9, 4.1) emits the offs at 2400 and the wrap's ons after them") {
    Host host;
    host.adopt(snapshotOf(cFour()));
    const std::vector<Ev> first = host.run(0.0, 93600, true);  // ends at 3.9
    CAPTURE(describe(first));
    CHECK(first == std::vector<Ev>{on(0, 60), on(0, 64), on(0, 67)});

    const std::vector<Ev> events = host.run(3.9, 4800, true);  // [3.9, 4.1)
    CAPTURE(describe(events));
    CHECK(host.lastOk);
    CHECK(events == std::vector<Ev>{off(2400, 60), off(2400, 64), off(2400, 67),  //
                                    on(2400, 60), on(2400, 64), on(2400, 67)});
}

// --- 3. stop -----------------------------------------------------------------------------------

TEST_CASE("scheduler: playing -> stopped releases with three offs at offset 0 and nothing else") {
    Host host;
    host.adopt(snapshotOf(cFour()));
    host.run(0.0, 512, true);

    const std::vector<Ev> events = host.run(0.5, 512, false);
    CAPTURE(describe(events));
    CHECK(events == std::vector<Ev>{off(0, 60), off(0, 64), off(0, 67)});
    CHECK(host.owners(60) == 0);

    // Staying stopped emits nothing more.
    CHECK(host.run(0.5, 512, false) == kNone);
}

// --- 4. host-loop wrap -------------------------------------------------------------------------

TEST_CASE("scheduler: host-loop wrap from 8.0 back to 2.0 releases then chases at offset 0") {
    Host host;
    host.adopt(snapshotOf(cFour()));
    const std::vector<Ev> first = host.run(0.0, 8 * kSamplesPerBeat, true);  // [0, 8): two passes
    CAPTURE(describe(first));
    CHECK(first == std::vector<Ev>{on(0, 60), on(0, 64), on(0, 67),  //
                                   off(96000, 60), off(96000, 64), off(96000, 67),  //
                                   on(96000, 60), on(96000, 64), on(96000, 67)});

    const std::vector<Ev> events = host.run(2.0, 512, true);
    CAPTURE(describe(events));
    CHECK(events == std::vector<Ev>{off(0, 60), off(0, 64), off(0, 67), on(0, 60), on(0, 64), on(0, 67)});
}

// --- 5. forward seek ---------------------------------------------------------------------------

TEST_CASE("scheduler: forward seek 1.0 -> 9.0 with C then Am chases C again") {
    Host host;
    host.adopt(snapshotOf(cThenAm()));
    host.run(0.0, kSamplesPerBeat, true);  // [0, 1): C sounding

    const std::vector<Ev> events = host.run(9.0, 512, true);  // 9 mod 8 = 1: still C
    CAPTURE(describe(events));
    CHECK(events == std::vector<Ev>{off(0, 60), off(0, 64), off(0, 67), on(0, 60), on(0, 64), on(0, 67)});

    // And a seek into the Am region picks Am.
    const std::vector<Ev> intoAm = host.run(13.5, 512, true);  // 13.5 mod 8 = 5.5
    CAPTURE(describe(intoAm));
    CHECK(intoAm == std::vector<Ev>{off(0, 60), off(0, 64), off(0, 67), on(0, 69), on(0, 72), on(0, 76)});
}

// --- 6. tempo ----------------------------------------------------------------------------------

TEST_CASE("scheduler: at 90 bpm offsets scale by 120/90 and the order is unchanged") {
    Host host;
    host.adopt(snapshotOf(cFour()));
    host.run(0.0, 124800, true, 90.0);  // 3.9 beats at 32000 samples per beat

    const std::vector<Ev> events = host.run(3.9, 6400, true, 90.0);  // [3.9, 4.1)
    CAPTURE(describe(events));
    CHECK(events == std::vector<Ev>{off(3200, 60), off(3200, 64), off(3200, 67),  //
                                    on(3200, 60), on(3200, 64), on(3200, 67)});
}

// --- 7. capacity -------------------------------------------------------------------------------

TEST_CASE("scheduler: capacity 4 with three releases and three attacks keeps every off and drops two ons") {
    Host host;
    host.adopt(snapshotOf(cFour()));
    host.run(0.0, 512, true);

    const std::vector<Ev> events = host.run(2.0, 512, true, kBpm, 4);  // seek: release all + chase
    CAPTURE(describe(events));
    CHECK_FALSE(host.lastOk);
    CHECK(host.lastWritten == 4);
    CHECK(events == std::vector<Ev>{off(0, 60), off(0, 64), off(0, 67), on(0, 60)});
    CHECK(host.owners(60) == kOwnerProgression);
    CHECK(host.owners(64) == 0);
    CHECK(host.owners(67) == 0);

    // The dropped pitches are not sounding: stopping releases only 60.
    const std::vector<Ev> stop = host.run(2.0, 512, false);
    CAPTURE(describe(stop));
    CHECK(host.lastOk);
    CHECK(stop == std::vector<Ev>{off(0, 60)});
}

TEST_CASE("scheduler: capacity 0 drops the attacks, keeps nothing sounding and reports the drop") {
    Host host;
    host.adopt(snapshotOf(cFour()));
    const std::vector<Ev> events = host.run(0.0, 512, true, kBpm, 0);
    CHECK(events == kNone);
    CHECK_FALSE(host.lastOk);
    CHECK(host.owners(60) == 0);
    // Nothing to release on stop.
    CHECK(host.run(0.0, 512, false) == kNone);
    CHECK(host.lastOk);
}

// --- 8. partition invariance -------------------------------------------------------------------

namespace {

struct AbsEv {
    long sample;
    bool on;
    int pitch;
};

bool operator==(const AbsEv& a, const AbsEv& b) {
    return a.sample == b.sample && a.on == b.on && a.pitch == b.pitch;
}

bool operator<(const AbsEv& a, const AbsEv& b) {
    if (a.sample != b.sample) {
        return a.sample < b.sample;
    }
    if (a.on != b.on) {
        return !a.on;
    }
    return a.pitch < b.pitch;
}

/// Plays `snapshot` from sample 0 to `totalSamples` in blocks of the given sizes (cycled, the last one
/// trimmed), with the host's ppq derived from the absolute sample position as a real host does. With
/// `jitterBeats` the reported ppqStart is nudged alternately up and down by that much (never on the
/// first block), as a host with its own rounding would.
std::vector<AbsEv> playPartitioned(const Snapshot& snapshot, long totalSamples, const std::vector<int>& sizes,
                                   double jitterBeats = 0.0) {
    Host host;
    host.adopt(snapshot);
    std::vector<AbsEv> events;
    long position = 0;
    std::size_t next = 0;
    while (position < totalSamples) {
        int nFrames = sizes[next % sizes.size()];
        nFrames = static_cast<int>(std::min<long>(nFrames, totalSamples - position));
        double ppq = static_cast<double>(position) * kBpm / (60.0 * kSr);
        if (position > 0) {
            ppq += (next % 2 == 0) ? jitterBeats : -jitterBeats;
        }
        ++next;
        for (const Ev& e : host.run(ppq, nFrames, true)) {
            REQUIRE(e.offset >= 0);
            REQUIRE(e.offset < nFrames);
            events.push_back(AbsEv{position + e.offset, e.on, e.pitch});
        }
        REQUIRE(host.lastOk);
        position += nFrames;
    }
    std::sort(events.begin(), events.end());
    return events;
}

/// Every occurrence of `snapshot` (looped) before `totalSamples`, at the sample the scheduler must put it
/// on: an event at beat b of wrap k sits at (k + 1) * endBeat when b is the final off beat, else at
/// k * endBeat + b, rounded half up to a sample.
std::vector<AbsEv> occurrencesBefore(const Snapshot& snapshot, long totalSamples) {
    std::vector<AbsEv> expected;
    for (int k = 0;; ++k) {
        bool any = false;
        for (std::size_t i = 0; i < snapshot.count; ++i) {
            const NoteEvent& e = snapshot.events[i];
            const double beat = e.beat >= snapshot.endBeat ? (k + 1) * snapshot.endBeat : k * snapshot.endBeat + e.beat;
            const long sample = static_cast<long>(std::floor(beat * kSamplesPerBeat + 0.5));
            if (sample < totalSamples) {
                expected.push_back(AbsEv{sample, e.on, e.pitch});
                any = true;
            }
        }
        if (!any) {
            break;
        }
    }
    std::sort(expected.begin(), expected.end());
    return expected;
}

const std::vector<std::vector<int>> kSchemes{
    {64}, {256}, {1024}, {100, 37, 500, 1, 999, 64, 2048, 3}, {7, 11, 13}, {kSamplesPerBeat * 3}};

void checkPartitionInvariance(const Snapshot& snapshot, long total, double jitterBeats = 0.0) {
    const std::vector<AbsEv> expected = occurrencesBefore(snapshot, total);
    REQUIRE(!expected.empty());
    for (std::size_t s = 0; s < kSchemes.size(); ++s) {
        CAPTURE(s);
        const std::vector<AbsEv> actual = playPartitioned(snapshot, total, kSchemes[s], jitterBeats);
        REQUIRE(actual.size() == expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i) {
            CAPTURE(i);
            CHECK(actual[i] == expected[i]);
        }
    }
}

}  // namespace

TEST_CASE("scheduler: partition invariance over beats 0..16 for several block sizes") {
    const Compiled& compiled = cThenAm();  // endBeat 8: two passes in 16 beats
    const long total = 16L * kSamplesPerBeat;
    const std::vector<AbsEv> expected = occurrencesBefore(snapshotOf(compiled), total);
    REQUIRE(expected.size() == 21);  // 2 x 12 minus the three offs that sit exactly at 16
    checkPartitionInvariance(snapshotOf(compiled), total);
}

TEST_CASE("scheduler: partition invariance with fractional chord lengths") {
    SUBCASE("thirds of a beat") {
        const Compiled compiled = compile(documentOf({chord(0, "maj"), chord(9, "min"), chord(0, "maj")}, 1.0 / 3.0));
        checkPartitionInvariance(snapshotOf(compiled), 7L * kSamplesPerBeat);
    }
    SUBCASE("0.333 beats") {
        const Compiled compiled = compile(documentOf({chord(0, "maj"), chord(9, "min")}, 0.333));
        checkPartitionInvariance(snapshotOf(compiled), 5L * kSamplesPerBeat);
    }
    SUBCASE("0.25001 beats, where k*endBeat + endBeat and (k+1)*endBeat differ by an ulp") {
        const std::vector<NoteEvent> one{{0.0, true, 60, 100}, {0.25001, false, 60, 0}};
        checkPartitionInvariance(snapshotOf(one, 0.25001), 4L * kSamplesPerBeat);
    }
}

TEST_CASE("scheduler: host jitter below the seek threshold has no effect on the output") {
    const Snapshot snapshot = snapshotOf(cThenAm());
    const long total = 16L * kSamplesPerBeat;
    checkPartitionInvariance(snapshot, total, 1.0 / kSamplesPerBeat);   // one sample of jitter
    checkPartitionInvariance(snapshot, total, 0.9 / 1024.0);             // just under the seek threshold
    const Compiled thirds = compile(documentOf({chord(0, "maj"), chord(9, "min"), chord(0, "maj")}, 1.0 / 3.0));
    checkPartitionInvariance(snapshotOf(thirds), 7L * kSamplesPerBeat, 1.0 / kSamplesPerBeat);
}

// --- 9. negative ppq ---------------------------------------------------------------------------

TEST_CASE("scheduler: negative ppq while playing emits nothing until ppq 0") {
    Host host;
    host.adopt(snapshotOf(cFour()));  // loop: positiveMod would say 3.5, but nothing sounds before 0

    const std::vector<Ev> before = host.run(-0.5, 4800, true);  // [-0.5, -0.3)
    CAPTURE(describe(before));
    CHECK(before == kNone);
    CHECK(host.owners(60) == 0);

    const std::vector<Ev> crossing = host.run(-0.3, 9600, true);  // [-0.3, 0.1)
    CAPTURE(describe(crossing));
    CHECK(crossing == std::vector<Ev>{on(7200, 60), on(7200, 64), on(7200, 67)});
}

TEST_CASE("scheduler: a seek to a negative ppq releases and chases nothing") {
    Host host;
    host.adopt(snapshotOf(cFour()));
    host.run(0.0, 512, true);
    const std::vector<Ev> events = host.run(-1.0, 512, true);
    CAPTURE(describe(events));
    CHECK(events == std::vector<Ev>{off(0, 60), off(0, 64), off(0, 67)});
}

// --- 10. one-shot ------------------------------------------------------------------------------

TEST_CASE("scheduler: loop=false emits nothing at or after endBeat and chases nothing past it") {
    SUBCASE("playing through the end") {
        Host host;
        host.adopt(snapshotOf(cThenAm(), /*loop=*/false));
        const std::vector<Ev> events = host.run(0.0, 8 * kSamplesPerBeat + 4800, true);  // [0, 8.2)
        CAPTURE(describe(events));
        CHECK(events == std::vector<Ev>{on(0, 60), on(0, 64), on(0, 67),  //
                                        off(96000, 60), off(96000, 64), off(96000, 67),  //
                                        on(96000, 69), on(96000, 72), on(96000, 76),  //
                                        off(192000, 69), off(192000, 72), off(192000, 76)});
        CHECK(host.run(8.2, 4800, true) == kNone);
        CHECK(host.run(8.4, 4 * kSamplesPerBeat, true) == kNone);  // no wrap at 16 either
        CHECK(host.owners(69) == 0);
    }
    SUBCASE("chase at ppq 20 with endBeat 8") {
        Host host;
        host.adopt(snapshotOf(cThenAm(), /*loop=*/false));
        CHECK(host.run(20.0, 512, true) == kNone);
        CHECK(host.lastOk);
    }
    SUBCASE("a seek back inside the progression chases normally") {
        Host host;
        host.adopt(snapshotOf(cThenAm(), /*loop=*/false));
        host.run(20.0, 512, true);
        const std::vector<Ev> events = host.run(5.0, 512, true);
        CAPTURE(describe(events));
        CHECK(events == std::vector<Ev>{on(0, 69), on(0, 72), on(0, 76)});
    }
}

// --- 11. previews ------------------------------------------------------------------------------

TEST_CASE("scheduler: preview start, progression retrigger, preview stop") {
    Host host;
    // The progression attacks 64 only, at beat 0, for four beats.
    const std::vector<NoteEvent> only64{{0.0, true, 64, 100}, {4.0, false, 64, 0}};
    host.adopt(snapshotOf(only64, 4.0));

    CHECK(host.scheduler.pushPreview(previewStart(1, {60, 64, 67}, 90)));
    const std::vector<Ev> started = host.run(0.0, 512, false);  // transport stopped: previews still play
    CAPTURE(describe(started));
    CHECK(started == std::vector<Ev>{on(0, 60, 90), on(0, 64, 90), on(0, 67, 90)});
    CHECK(host.owners(60) == kOwnerPreview);
    CHECK(host.owners(64) == kOwnerPreview);

    // The progression attacks 64 while the preview holds it: Off then On (retrigger), now co-owned.
    const std::vector<Ev> attacked = host.run(0.0, 512, true);
    CAPTURE(describe(attacked));
    CHECK(attacked == std::vector<Ev>{off(0, 64), on(0, 64, 100)});
    CHECK(host.owners(64) == (kOwnerProgression | kOwnerPreview));

    // Stopping the preview releases 60 and 67 only; 64 stays with the progression.
    CHECK(host.scheduler.pushPreview(previewStop(1)));
    const std::vector<Ev> stopped = host.run(512.0 / kSamplesPerBeat, 512, true);
    CAPTURE(describe(stopped));
    CHECK(stopped == std::vector<Ev>{off(0, 60), off(0, 67)});
    CHECK(host.owners(64) == kOwnerProgression);
    CHECK(host.owners(60) == 0);

    // Stopping the transport finally releases 64.
    const std::vector<Ev> transportStop = host.run(1024.0 / kSamplesPerBeat, 512, false);
    CAPTURE(describe(transportStop));
    CHECK(transportStop == std::vector<Ev>{off(0, 64)});
}

TEST_CASE("scheduler: a preview on a pitch the progression holds co-owns silently") {
    Host host;
    host.adopt(snapshotOf(cFour()));
    host.run(0.0, 512, true);  // 60, 64, 67 sounding

    CHECK(host.scheduler.pushPreview(previewStart(7, {64, 71})));
    const std::vector<Ev> started = host.run(512.0 / kSamplesPerBeat, 512, true);
    CAPTURE(describe(started));
    CHECK(started == std::vector<Ev>{on(0, 71, 90)});
    CHECK(host.owners(64) == (kOwnerProgression | kOwnerPreview));

    CHECK(host.scheduler.pushPreview(previewStop(7)));
    const std::vector<Ev> stopped = host.run(1024.0 / kSamplesPerBeat, 512, true);
    CAPTURE(describe(stopped));
    CHECK(stopped == std::vector<Ev>{off(0, 71)});
    CHECK(host.owners(64) == kOwnerProgression);

    // The progression's own release of 64 is a physical off (the preview no longer holds it).
    const std::vector<Ev> offs = host.run(1536.0 / kSamplesPerBeat, 4 * kSamplesPerBeat, true);
    const int offset = 4 * kSamplesPerBeat - 1536;
    CAPTURE(describe(offs));
    CHECK(offs == std::vector<Ev>{off(offset, 60), off(offset, 64), off(offset, 67),  //
                                  on(offset, 60), on(offset, 64), on(offset, 67)});
}

TEST_CASE("scheduler: a preview auto-releases after four seconds of blocks without a stop") {
    SUBCASE("expiry on a block boundary") {
        Host host;
        CHECK(host.scheduler.pushPreview(previewStart(1, {60, 64, 67})));
        const std::vector<Ev> started = host.run(0.0, 4800, false);
        CHECK(started == std::vector<Ev>{on(0, 60, 90), on(0, 64, 90), on(0, 67, 90)});
        // 4 s = 192000 samples = 40 blocks of 4800 including the first one.
        for (int block = 1; block < 40; ++block) {
            CAPTURE(block);
            REQUIRE(host.run(0.0, 4800, false) == kNone);
        }
        const std::vector<Ev> expired = host.run(0.0, 4800, false);
        CAPTURE(describe(expired));
        CHECK(expired == std::vector<Ev>{off(0, 60), off(0, 64), off(0, 67)});
        CHECK(host.owners(60) == 0);
        CHECK(host.run(0.0, 4800, false) == kNone);
    }
    SUBCASE("expiry inside a block, at the exact sample") {
        Host host;
        CHECK(host.scheduler.pushPreview(previewStart(2, {72})));
        REQUIRE(host.run(0.0, 7000, false) == std::vector<Ev>{on(0, 72, 90)});
        for (int block = 1; block < 27; ++block) {
            CAPTURE(block);
            REQUIRE(host.run(0.0, 7000, false) == kNone);
        }
        // 27 blocks = 189000 samples; the preview ends 3000 samples into the 28th.
        const std::vector<Ev> expired = host.run(0.0, 7000, false);
        CAPTURE(describe(expired));
        CHECK(expired == std::vector<Ev>{off(3000, 72)});
    }
    SUBCASE("a stop before expiry releases once and the expiry is silent") {
        Host host;
        CHECK(host.scheduler.pushPreview(previewStart(3, {72})));
        REQUIRE(host.run(0.0, 4800, false) == std::vector<Ev>{on(0, 72, 90)});
        CHECK(host.scheduler.pushPreview(previewStop(3)));
        REQUIRE(host.run(0.0, 4800, false) == std::vector<Ev>{off(0, 72)});
        for (int block = 0; block < 50; ++block) {
            CAPTURE(block);
            REQUIRE(host.run(0.0, 4800, false) == kNone);
        }
    }
}

TEST_CASE("scheduler: an extend moves a preview's deadline out and retriggers nothing") {
    Host host;
    CHECK(host.scheduler.pushPreview(previewStart(1, {60, 64, 67})));
    REQUIRE(previewBlock(host) == std::vector<Ev>{on(0, 60, 90), on(0, 64, 90), on(0, 67, 90)});
    for (int block = 1; block < 20; ++block) {
        CAPTURE(block);
        REQUIRE(previewBlock(host) == kNone);
    }
    // Applied at clock 96000 (2 s): the deadline becomes 96000 + 192000 = 288000, block 60.
    CHECK(host.scheduler.pushPreview(previewExtend(1)));
    for (int block = 20; block < 60; ++block) {  // block 40, the old deadline, included
        CAPTURE(block);
        REQUIRE(previewBlock(host) == kNone);
    }
    const std::vector<Ev> expired = previewBlock(host);
    CAPTURE(describe(expired));
    CHECK(expired == std::vector<Ev>{off(0, 60), off(0, 64), off(0, 67)});
    CHECK(host.owners(60) == 0);
    CHECK(previewBlock(host) == kNone);
}

TEST_CASE("scheduler: an extend applied in the block of the old deadline keeps the preview sounding") {
    SUBCASE("old deadline at offset 0 of the block") {
        Host host;
        CHECK(host.scheduler.pushPreview(previewStart(1, {60})));
        REQUIRE(previewBlock(host) == std::vector<Ev>{on(0, 60, 90)});
        for (int block = 1; block < 40; ++block) {
            CAPTURE(block);
            REQUIRE(previewBlock(host) == kNone);
        }
        CHECK(host.scheduler.pushPreview(previewExtend(1)));  // applied at clock 192000, the old deadline itself
        REQUIRE(previewBlock(host) == kNone);                 // block 40: the stale expiry at offset 0 is a no-op
        for (int block = 41; block < 80; ++block) {
            CAPTURE(block);
            REQUIRE(previewBlock(host) == kNone);
        }
        CHECK(previewBlock(host) == std::vector<Ev>{off(0, 60)});  // block 80 = 384000
    }
    SUBCASE("old deadline inside the block") {
        // 7000-frame blocks: the first deadline is 3000 samples into block 27 (27 * 7000 = 189000).
        Host host;
        CHECK(host.scheduler.pushPreview(previewStart(2, {72})));
        REQUIRE(previewBlock(host, 7000) == std::vector<Ev>{on(0, 72, 90)});
        for (int block = 1; block < 27; ++block) {
            CAPTURE(block);
            REQUIRE(previewBlock(host, 7000) == kNone);
        }
        CHECK(host.scheduler.pushPreview(previewExtend(2)));  // applied at clock 189000, before the expiry event
        REQUIRE(previewBlock(host, 7000) == kNone);           // block 27: the stale deadline at 3000 is a no-op
        // New deadline 189000 + 192000 = 381000 = block 54 at offset 3000.
        for (int block = 28; block < 54; ++block) {
            CAPTURE(block);
            REQUIRE(previewBlock(host, 7000) == kNone);
        }
        const std::vector<Ev> expired = previewBlock(host, 7000);
        CAPTURE(describe(expired));
        CHECK(expired == std::vector<Ev>{off(3000, 72)});
    }
}

TEST_CASE("scheduler: an extend for an id that is not sounding does nothing") {
    SUBCASE("nothing sounds") {
        Host host;
        CHECK(host.scheduler.pushPreview(previewExtend(9)));
        CHECK(previewBlock(host) == kNone);
        CHECK(host.owners(60) == 0);
    }
    SUBCASE("after its stop") {
        Host host;
        CHECK(host.scheduler.pushPreview(previewStart(3, {60})));
        REQUIRE(previewBlock(host) == std::vector<Ev>{on(0, 60, 90)});
        CHECK(host.scheduler.pushPreview(previewStop(3)));
        REQUIRE(previewBlock(host) == std::vector<Ev>{off(0, 60)});
        CHECK(host.scheduler.pushPreview(previewExtend(3)));
        for (int block = 0; block < 50; ++block) {
            CAPTURE(block);
            REQUIRE(previewBlock(host) == kNone);  // never resurrected, never released twice
        }
        CHECK(host.owners(60) == 0);
    }
}

TEST_CASE("scheduler: extend with a stop or a start in the same batch, both orders") {
    SUBCASE("extend then stop") {
        Host host;
        CHECK(host.scheduler.pushPreview(previewStart(4, {64})));
        REQUIRE(previewBlock(host) == std::vector<Ev>{on(0, 64, 90)});
        CHECK(host.scheduler.pushPreview(previewExtend(4)));
        CHECK(host.scheduler.pushPreview(previewStop(4)));
        REQUIRE(previewBlock(host) == std::vector<Ev>{off(0, 64)});
        CHECK(host.owners(64) == 0);
    }
    SUBCASE("stop then extend") {
        Host host;
        CHECK(host.scheduler.pushPreview(previewStart(4, {64})));
        REQUIRE(previewBlock(host) == std::vector<Ev>{on(0, 64, 90)});
        CHECK(host.scheduler.pushPreview(previewStop(4)));
        CHECK(host.scheduler.pushPreview(previewExtend(4)));
        REQUIRE(previewBlock(host) == std::vector<Ev>{off(0, 64)});
        for (int block = 0; block < 50; ++block) {
            CAPTURE(block);
            REQUIRE(previewBlock(host) == kNone);
        }
        CHECK(host.owners(64) == 0);
    }
    SUBCASE("start then extend, and extend then start: the deadline is the start's") {
        for (const bool startFirst : {true, false}) {
            CAPTURE(startFirst);
            Host host;
            if (startFirst) {
                CHECK(host.scheduler.pushPreview(previewStart(6, {67})));
                CHECK(host.scheduler.pushPreview(previewExtend(6)));
            } else {
                CHECK(host.scheduler.pushPreview(previewExtend(6)));
                CHECK(host.scheduler.pushPreview(previewStart(6, {67})));
            }
            REQUIRE(previewBlock(host) == std::vector<Ev>{on(0, 67, 90)});
            for (int block = 1; block < 40; ++block) {
                CAPTURE(block);
                REQUIRE(previewBlock(host) == kNone);
            }
            CHECK(previewBlock(host) == std::vector<Ev>{off(0, 67)});
        }
    }
}

TEST_CASE("scheduler: an extend after a restarted id extends the restart") {
    Host host;
    CHECK(host.scheduler.pushPreview(previewStart(5, {60})));
    REQUIRE(previewBlock(host) == std::vector<Ev>{on(0, 60, 90)});
    for (int block = 1; block < 10; ++block) {
        REQUIRE(previewBlock(host) == kNone);
    }
    CHECK(host.scheduler.pushPreview(previewStart(5, {62})));  // block 10: restart, same id
    REQUIRE(previewBlock(host) == std::vector<Ev>{off(0, 60), on(0, 62, 90)});
    for (int block = 11; block < 30; ++block) {
        REQUIRE(previewBlock(host) == kNone);
    }
    CHECK(host.scheduler.pushPreview(previewExtend(5)));  // block 30: deadline 144000 + 192000 = 336000 = block 70
    for (int block = 30; block < 70; ++block) {
        CAPTURE(block);
        REQUIRE(previewBlock(host) == kNone);
    }
    CHECK(previewBlock(host) == std::vector<Ev>{off(0, 62)});
}

TEST_CASE("scheduler: an extend applied in a block with no usable rate leaves the deadline alone") {
    // No reset: the rate comes from each block. A block with a non-finite ppqStart is invalid
    // (scheduler.h, "Bounded work"): previews are served, but no rate is usable for a new deadline
    // (previewSamples is 0) and the preview clock does not advance (the block counts as 0 frames).
    Scheduler scheduler;
    std::array<MidiOut, kBig> buffer{};
    std::size_t written = 0;
    bool ok = true;
    auto run = [&](double ppq, int nFrames) {
        const BlockInfo info{ppq, nFrames, kSr, kBpm, false, true};
        ok = scheduler.process(info, buffer.data(), buffer.size(), written);
        return decode(buffer.data(), written);
    };
    CHECK(scheduler.pushPreview(previewStart(7, {60})));
    REQUIRE(run(0.0, 4800) == std::vector<Ev>{on(0, 60, 90)});  // deadline 192000
    REQUIRE(ok);
    for (int block = 1; block < 20; ++block) {
        REQUIRE(run(0.0, 4800) == kNone);
        REQUIRE(ok);
    }
    CHECK(scheduler.pushPreview(previewExtend(7)));
    REQUIRE(run(std::numeric_limits<double>::quiet_NaN(), 4800) == kNone);  // invalid: deadline kept, clock stays 96000
    CHECK_FALSE(ok);                                                        // an invalid block is reported
    // The clock reaches 192000 at the start of the 40th valid block, which the loop index calls 41.
    for (int block = 21; block < 41; ++block) {
        CAPTURE(block);
        REQUIRE(run(0.0, 4800) == kNone);  // not expired at once
        REQUIRE(ok);
    }
    CHECK(run(0.0, 4800) == std::vector<Ev>{off(0, 60)});  // the original deadline, 192000
    CHECK(ok);
}

TEST_CASE("scheduler: two previews extended at different blocks expire at their own deadlines") {
    Host host;
    CHECK(host.scheduler.pushPreview(previewStart(1, {60})));
    CHECK(host.scheduler.pushPreview(previewStart(2, {64})));
    REQUIRE(previewBlock(host) == std::vector<Ev>{on(0, 60, 90), on(0, 64, 90)});
    for (int block = 1; block < 65; ++block) {
        CAPTURE(block);
        if (block == 10) {
            CHECK(host.scheduler.pushPreview(previewExtend(1)));  // 48000 + 192000 = 240000, block 50
        }
        if (block == 25) {
            CHECK(host.scheduler.pushPreview(previewExtend(2)));  // 120000 + 192000 = 312000, block 65
        }
        const std::vector<Ev> events = previewBlock(host);
        CAPTURE(describe(events));
        REQUIRE(events == (block == 50 ? std::vector<Ev>{off(0, 60)} : kNone));
    }
    CHECK(previewBlock(host) == std::vector<Ev>{off(0, 64)});  // block 65
    CHECK(host.owners(60) == 0);
    CHECK(host.owners(64) == 0);
}

TEST_CASE("scheduler: an extend for a start that acquired no pitch does nothing") {
    // Every pitch invalid: the start acquires nothing and its slot is inactive at once.
    Host host;
    CHECK(host.scheduler.pushPreview(previewStart(8, {200, 255})));
    REQUIRE(previewBlock(host) == kNone);
    CHECK(host.scheduler.pushPreview(previewExtend(8)));
    for (int block = 1; block < 50; ++block) {
        CAPTURE(block);
        REQUIRE(previewBlock(host) == kNone);
    }
    for (int pitch = 0; pitch < 128; ++pitch) {
        REQUIRE_FALSE(host.scheduler.sounding(pitch));
    }
}

TEST_CASE("scheduler: an extend in a block with output capacity 0 completes and still moves the deadline") {
    Host host;
    CHECK(host.scheduler.pushPreview(previewStart(1, {60})));
    REQUIRE(previewBlock(host) == std::vector<Ev>{on(0, 60, 90)});
    CHECK(host.scheduler.pushPreview(previewExtend(1)));
    // Block 1, nothing may be written: the extend writes nothing, so the block is complete.
    CHECK(host.run(0.0, 4800, false, kBpm, 0) == kNone);
    CHECK(host.lastOk);
    // Deadline 4800 + 192000 = 196800, block 41: block 40, the old deadline, stays silent.
    for (int block = 2; block < 41; ++block) {
        CAPTURE(block);
        REQUIRE(previewBlock(host) == kNone);
    }
    CHECK(previewBlock(host) == std::vector<Ev>{off(0, 60)});
}

TEST_CASE("scheduler: pending extends for one id coalesce in the queue") {
    Host host;
    CHECK(host.scheduler.pushPreview(previewStart(1, {60})));
    for (int i = 0; i < 200; ++i) {
        CAPTURE(i);
        REQUIRE(host.scheduler.pushPreview(previewExtend(1)));  // never refused: the second and later merge into the first
    }
    // The ring now holds Start + one Extend: 62 more commands fit, the 63rd does not.
    for (int id = 2; id < 64; ++id) {
        CAPTURE(id);
        REQUIRE(host.scheduler.pushPreview(previewStop(id)));
    }
    CHECK_FALSE(host.scheduler.pushPreview(previewStop(64)));
    CHECK_FALSE(host.scheduler.pushPreview(previewExtend(1)));  // the newest entry is a stop: nothing to merge into, ring full
    REQUIRE(previewBlock(host) == std::vector<Ev>{on(0, 60, 90)});  // all 64 drained in one block
    CHECK(host.scheduler.pushPreview(previewStop(1)));              // drained: a stop fits again
    REQUIRE(previewBlock(host) == std::vector<Ev>{off(0, 60)});
}

TEST_CASE("scheduler: an extend merges into the pending extend even when the ring is full") {
    Host host;
    CHECK(host.scheduler.pushPreview(previewStart(1, {60})));
    for (int id = 2; id < 64; ++id) {
        CAPTURE(id);
        REQUIRE(host.scheduler.pushPreview(previewStop(id)));  // 63 entries
    }
    CHECK(host.scheduler.pushPreview(previewExtend(1)));      // 64: full
    CHECK(host.scheduler.pushPreview(previewExtend(1)));      // merged into the newest entry, reported queued
    CHECK_FALSE(host.scheduler.pushPreview(previewStop(1)));  // still full
    REQUIRE(previewBlock(host) == std::vector<Ev>{on(0, 60, 90)});
    CHECK(host.scheduler.pushPreview(previewStop(1)));
    REQUIRE(previewBlock(host) == std::vector<Ev>{off(0, 60)});
}

TEST_CASE("scheduler: a pitch repeated within one preview command sounds and releases once") {
    Host host;
    CHECK(host.scheduler.pushPreview(previewStart(4, {75, 65, 65, 60})));
    const std::vector<Ev> started = host.run(0.0, 512, false);
    CAPTURE(describe(started));
    CHECK(started == std::vector<Ev>{on(0, 75, 90), on(0, 65, 90), on(0, 60, 90)});
    CHECK(host.scheduler.pushPreview(previewStop(4)));
    const std::vector<Ev> stopped = host.run(0.0, 512, false);
    CAPTURE(describe(stopped));
    CHECK(stopped == std::vector<Ev>{off(0, 75), off(0, 65), off(0, 60)});
    CHECK_FALSE(host.scheduler.sounding(65));
}

TEST_CASE("scheduler: two previews sharing a pitch release it only when both are gone") {
    Host host;
    CHECK(host.scheduler.pushPreview(previewStart(1, {60, 64})));
    CHECK(host.scheduler.pushPreview(previewStart(2, {64, 67})));
    const std::vector<Ev> started = host.run(0.0, 512, false);
    CAPTURE(describe(started));
    CHECK(started == std::vector<Ev>{on(0, 60, 90), on(0, 64, 90), on(0, 67, 90)});

    CHECK(host.scheduler.pushPreview(previewStop(1)));
    const std::vector<Ev> firstStop = host.run(0.0, 512, false);
    CAPTURE(describe(firstStop));
    CHECK(firstStop == std::vector<Ev>{off(0, 60)});
    CHECK(host.owners(64) == kOwnerPreview);

    CHECK(host.scheduler.pushPreview(previewStop(2)));
    const std::vector<Ev> secondStop = host.run(0.0, 512, false);
    CAPTURE(describe(secondStop));
    CHECK(secondStop == std::vector<Ev>{off(0, 64), off(0, 67)});
}

TEST_CASE("scheduler: restarting a preview id and stopping an unknown id") {
    Host host;
    CHECK(host.scheduler.pushPreview(previewStart(1, {60})));
    REQUIRE(host.run(0.0, 512, false) == std::vector<Ev>{on(0, 60, 90)});
    // Same id again: the old preview is released first, then the new one attacks.
    CHECK(host.scheduler.pushPreview(previewStart(1, {60, 62})));
    const std::vector<Ev> restarted = host.run(0.0, 512, false);
    CAPTURE(describe(restarted));
    CHECK(restarted == std::vector<Ev>{off(0, 60), on(0, 60, 90), on(0, 62, 90)});
    CHECK(host.scheduler.pushPreview(previewStop(99)));
    CHECK(host.run(0.0, 512, false) == kNone);
    CHECK(host.lastOk);
    CHECK(host.scheduler.pushPreview(previewStop(1)));
    CHECK(host.run(0.0, 512, false) == std::vector<Ev>{off(0, 60), off(0, 62)});
}

TEST_CASE("scheduler: the preview queue holds 64 commands and refuses the 65th") {
    Host host;
    for (int i = 0; i < 64; ++i) {
        CAPTURE(i);
        REQUIRE(host.scheduler.pushPreview(previewStop(i)));
    }
    CHECK_FALSE(host.scheduler.pushPreview(previewStop(64)));
    host.run(0.0, 512, false);  // drains
    CHECK(host.scheduler.pushPreview(previewStop(64)));
}

// --- 12. adopt ---------------------------------------------------------------------------------

TEST_CASE("scheduler: adopting a new generation mid-playback releases all then chases at offset 0") {
    Host host;
    host.adopt(snapshotOf(cFour(), true, 1));
    host.run(0.0, 2 * kSamplesPerBeat, true);  // [0, 2): C sounding

    host.adopt(snapshotOf(amFour(), true, 2));
    const std::vector<Ev> events = host.run(2.0, 512, true);
    CAPTURE(describe(events));
    CHECK(events == std::vector<Ev>{off(0, 60), off(0, 64), off(0, 67), on(0, 69), on(0, 72), on(0, 76)});

    // Re-adopting the same generation is not a discontinuity.
    host.adopt(snapshotOf(amFour(), true, 2));
    CHECK(host.run(2.0 + 512.0 / kSamplesPerBeat, 512, true) == kNone);
}

TEST_CASE("scheduler: an empty snapshot only releases") {
    Host host;
    host.adopt(snapshotOf(cFour(), true, 1));
    host.run(0.0, 512, true);
    const Snapshot empty{nullptr, 0, 0.0, true, 2};
    host.adopt(empty);
    const std::vector<Ev> events = host.run(512.0 / kSamplesPerBeat, 512, true);
    CAPTURE(describe(events));
    CHECK(events == std::vector<Ev>{off(0, 60), off(0, 64), off(0, 67)});
    CHECK(host.run(1024.0 / kSamplesPerBeat, 4 * kSamplesPerBeat, true) == kNone);
}

TEST_CASE("scheduler: reset keeps ownership and flushes on the next block") {
    Host host;
    host.adopt(snapshotOf(cFour()));
    host.run(0.0, 2 * kSamplesPerBeat, true);
    host.scheduler.reset(kSr);
    CHECK(host.owners(60) == kOwnerProgression);
    const std::vector<Ev> events = host.run(2.0, 512, true);
    CAPTURE(describe(events));
    CHECK(events == std::vector<Ev>{off(0, 60), off(0, 64), off(0, 67), on(0, 60), on(0, 64), on(0, 67)});
}

// --- 13. property test -------------------------------------------------------------------------

TEST_CASE("scheduler: 10000 random blocks and commands keep the ownership invariants") {
    std::mt19937 rng(20261011u);
    auto randInt = [&rng](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    auto randReal = [&rng](double lo, double hi) { return std::uniform_real_distribution<double>(lo, hi)(rng); };

    // A few random documents to switch between; the compiled events must outlive the scheduler.
    std::vector<Compiled> docs;
    const auto& types = allChordTypes();
    for (int d = 0; d < 6; ++d) {
        DocumentStore store;
        const int count = randInt(0, 5);
        for (int i = 0; i < count; ++i) {
            const double lengths[] = {0.25, 0.5, 1.0, 2.0, 4.0, 1.0 / 3.0};
            const std::size_t typeIndex = static_cast<std::size_t>(randInt(0, static_cast<int>(types.size()) - 1));
            store.appendChord(Chord{randInt(0, 11), &types[typeIndex]}, lengths[randInt(0, 5)], randInt(1, 127));
        }
        docs.push_back(compile(store.doc()));
    }

    Host host;
    std::uint32_t generation = 1;
    bool loop = true;
    std::size_t current = 0;  // the generation names the content: a re-adopt of the same generation is identical
    host.adopt(snapshotOf(docs[current], loop, generation));

    bool sounding[128] = {};
    bool playing = false;
    double ppq = 0.0;
    double bpm = kBpm;
    const double bpms[] = {60.0, 90.0, 120.0, 150.0};

    for (int iteration = 0; iteration < 10000; ++iteration) {
        CAPTURE(iteration);
        const int action = randInt(0, 99);
        if (action < 5) {
            playing = !playing;
        } else if (action < 10) {
            ppq = randReal(-2.0, 40.0);
        } else if (action < 15) {
            const std::size_t d = static_cast<std::size_t>(randInt(0, static_cast<int>(docs.size()) - 1));
            if (randInt(0, 3) != 0) {
                ++generation;
                loop = randInt(0, 3) != 0;
                current = d;
            }
            host.adopt(snapshotOf(docs[current], loop, generation));
        } else if (action < 25) {
            const int draw = randInt(0, 7);  // 3 in 8 a start, 3 in 8 a stop, 1 in 4 an extend
            if (draw < 3) {
                std::vector<int> pitches;
                const int count = randInt(1, 8);
                for (int i = 0; i < count; ++i) {
                    pitches.push_back(randInt(36, 84));
                }
                host.scheduler.pushPreview(previewStart(randInt(1, 4), pitches, randInt(1, 127)));
            } else if (draw < 6) {
                host.scheduler.pushPreview(previewStop(randInt(1, 4)));
            } else {
                host.scheduler.pushPreview(previewExtend(randInt(1, 4)));
            }
        } else if (action < 28) {
            host.scheduler.reset(kSr);
        } else if (action < 30) {
            bpm = bpms[randInt(0, 3)];
        } else if (action < 32) {
            host.scheduler.panic();
        }

        const int nFrames = randInt(1, 2048);
        const std::size_t capacity = randInt(0, 9) == 0 ? static_cast<std::size_t>(randInt(0, 8)) : kBig;
        // A real host's ppq carries rounding noise well below the seek threshold.
        const double jitter = randInt(0, 3) == 0 ? randReal(-1e-9, 1e-9) : 0.0;
        const std::vector<Ev> events = host.run(ppq + jitter, nFrames, playing, bpm, capacity);

        int previousOffset = 0;
        for (const Ev& e : events) {
            CAPTURE(e.offset);
            CAPTURE(e.pitch);
            REQUIRE(e.offset >= 0);
            REQUIRE(e.offset < nFrames);
            REQUIRE(e.offset >= previousOffset);
            previousOffset = e.offset;
            REQUIRE(e.pitch >= 0);
            REQUIRE(e.pitch < 128);
            // Never two ons without an off between; never an off for a silent pitch.
            REQUIRE(sounding[e.pitch] != e.on);
            sounding[e.pitch] = e.on;
            if (e.on) {
                REQUIRE(e.velocity >= 1);
                REQUIRE(e.velocity <= 127);
            }
        }
        if (capacity == kBig) {
            REQUIRE(host.lastOk);
        }

        // The scheduler's idea of what sounds is exactly what the emitted ons and offs say; an owned
        // pitch always sounds (an unowned one may still sound while its note-off is owed).
        int mismatches = 0;
        for (int pitch = 0; pitch < 128; ++pitch) {
            if (host.scheduler.sounding(pitch) != sounding[pitch]) {
                ++mismatches;
            }
            if (host.owners(pitch) != 0 && !sounding[pitch]) {
                ++mismatches;
            }
        }
        REQUIRE(mismatches == 0);

        if (playing) {
            ppq += nFrames * bpm / (60.0 * kSr);
        }
    }

    // Stopping and draining the previews brings everything to silence.
    for (int id = 1; id <= 4; ++id) {
        host.scheduler.pushPreview(previewStop(id));
    }
    for (const Ev& e : host.run(ppq, 512, false)) {
        REQUIRE(sounding[e.pitch] != e.on);
        sounding[e.pitch] = e.on;
    }
    CHECK(std::none_of(std::begin(sounding), std::end(sounding), [](bool s) { return s; }));
}

// --- Bounded work under absurd input ---------------------------------------------------------------

TEST_CASE("scheduler: absurd snapshots and host timing return promptly with releases only") {
    SUBCASE("endBeat 1e-9: every block spans too many wraps and is skipped") {
        Host host;
        host.adopt(snapshotOf(cFour(), true, 1));
        host.run(0.0, 512, true);  // C sounding
        const std::vector<NoteEvent> tiny{{0.0, true, 60, 100}, {1e-9, false, 60, 0}};
        host.adopt(snapshotOf(tiny, 1e-9, true, 2));
        const std::vector<Ev> adopted = host.run(512.0 / kSamplesPerBeat, 512, true);
        CAPTURE(describe(adopted));
        CHECK(adopted == std::vector<Ev>{off(0, 60), off(0, 64), off(0, 67)});
        CHECK_FALSE(host.lastOk);  // the block's timeline was skipped: incomplete, never silent
        CHECK(host.run(1024.0 / kSamplesPerBeat, 2048, true) == kNone);
        CHECK_FALSE(host.lastOk);
        CHECK(host.run(1e12, 2048, true) == kNone);  // a huge seek on top
        CHECK(host.run(1e12, 0, true) == kNone);
        CHECK(host.lastOk);  // an empty block does nothing and says so
        CHECK(host.owners(60) == 0);
    }
    SUBCASE("bpm 1e-9: samples per beat beyond 1e9, so the block counts as stopped") {
        Host host;
        host.adopt(snapshotOf(cFour()));
        REQUIRE(host.run(0.0, 512, true) == std::vector<Ev>{on(0, 60), on(0, 64), on(0, 67)});
        const std::vector<Ev> absurd = host.run(512.0 / kSamplesPerBeat, 512, true, 1e-9);
        CAPTURE(describe(absurd));
        CHECK(absurd == std::vector<Ev>{off(0, 60), off(0, 64), off(0, 67)});
        CHECK_FALSE(host.lastOk);  // rejected timing on a non-empty block is reported
        CHECK(host.run(1e12, 512, true, 1e-9) == kNone);
        CHECK_FALSE(host.lastOk);
        CHECK(host.run(1e12, 512, false, 1e-9) == kNone);  // stopped or not: the timing was unusable
        CHECK_FALSE(host.lastOk);
    }
    SUBCASE("derived values are validated: samples per beat and the preview timer must be sane") {
        struct Bad {
            const char* what;
            double sampleRate;
            double bpm;
        };
        const Bad bads[] = {
            {"spb overflows to inf", 1e300, 1e-300},
            {"spb 5e11 > 1e9", 1e12, kBpm},
            {"spb 2.88e15 > 1e9", kSr, 1e-9},
            {"sampleRate 1e300 (preview timer cannot fit)", 1e300, 1e300 * 60.0 / 24000.0},
        };
        for (const Bad& bad : bads) {
            CAPTURE(bad.what);
            Host host;
            host.adopt(snapshotOf(cFour()));
            REQUIRE(host.run(0.0, 512, true) == std::vector<Ev>{on(0, 60), on(0, 64), on(0, 67)});
            const BlockInfo info{512.0 / kSamplesPerBeat, 512, bad.sampleRate, bad.bpm, true, true};
            std::size_t written = 0;
            const bool ok = host.scheduler.process(info, host.buffer.data(), kBig, written);
            const std::vector<Ev> events = decode(host.buffer.data(), written);
            CAPTURE(describe(events));
            CHECK_FALSE(ok);
            CHECK(events == std::vector<Ev>{off(0, 60), off(0, 64), off(0, 67)});
            CHECK(host.owners(60) == 0);
        }
    }
    SUBCASE("a block at ppq 1e300 and one at the largest finite ppq") {
        Host host;
        host.adopt(snapshotOf(cFour()));
        CHECK(host.run(1e300, 512, true) == kNone);
        CHECK(host.run(1.7e308, 512, true) == kNone);
        CHECK(host.lastOk);
    }
    SUBCASE("unusable host timing is a stopped block: releases only, then playback resumes") {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        struct Bad {
            const char* what;
            double ppq;
            int nFrames;
            double sampleRate;
            double bpm;
        };
        const Bad bads[] = {
            {"bpm 0", 0.5, 512, kSr, 0.0},     {"bpm -120", 0.5, 512, kSr, -120.0}, {"bpm nan", 0.5, 512, kSr, nan},
            {"bpm inf", 0.5, 512, kSr, inf},   {"sampleRate 0", 0.5, 512, 0.0, kBpm}, {"nFrames -1", 0.5, -1, kSr, kBpm},
            {"ppq nan", nan, 512, kSr, kBpm},  {"ppq +inf", inf, 512, kSr, kBpm},   {"ppq -inf", -inf, 512, kSr, kBpm},
        };
        for (const Bad& bad : bads) {
            CAPTURE(bad.what);
            Host host;
            host.adopt(snapshotOf(cFour()));
            REQUIRE(host.run(0.0, 512, true) == std::vector<Ev>{on(0, 60), on(0, 64), on(0, 67)});

            const BlockInfo info{bad.ppq, bad.nFrames, bad.sampleRate, bad.bpm, true, true};
            std::size_t written = 0;
            const bool ok = host.scheduler.process(info, host.buffer.data(), kBig, written);
            const std::vector<Ev> events = decode(host.buffer.data(), written);
            CAPTURE(describe(events));
            CHECK_FALSE(ok);  // the block was not played as asked: reported
            CHECK(events == std::vector<Ev>{off(0, 60), off(0, 64), off(0, 67)});
            CHECK(host.owners(60) == 0);

            // The next good block starts afresh and chases.
            const std::vector<Ev> resumed = host.run(2.0, 512, true);
            CAPTURE(describe(resumed));
            CHECK(resumed == std::vector<Ev>{on(0, 60), on(0, 64), on(0, 67)});
        }
    }
    SUBCASE("a legitimately long block still plays every wrap it spans") {
        Host host;
        host.adopt(snapshotOf(cFour()));  // 64 wraps of 4 beats = 256 beats
        const std::vector<Ev> events = host.run(0.0, 64 * 4 * kSamplesPerBeat, true);
        CHECK(events.size() == 3 + 63 * 6);  // first ons, then off+on per wrap boundary inside the block
        CHECK(host.lastOk);
        // One wrap more than the bound: skipped and released.
        const std::vector<Ev> skipped = host.run(256.0, 65 * 4 * kSamplesPerBeat + 4800, true);
        CAPTURE(describe(skipped));
        CHECK(skipped == std::vector<Ev>{off(0, 60), off(0, 64), off(0, 67)});
        CHECK_FALSE(host.lastOk);  // skipped: reported, not silent
    }
}

// --- Review regressions ----------------------------------------------------------------------------

TEST_CASE("regression: a release refused by output capacity is owed and paid by the next block") {
    Host host;
    host.adopt(snapshotOf(cFour()));
    host.run(0.0, 512, true);

    const std::vector<Ev> refused = host.run(0.5, 512, false, kBpm, 0);  // stop with no room at all
    CHECK(refused == kNone);
    CHECK_FALSE(host.lastOk);
    CHECK(host.owners(48) == 0);         // the progression has let go...
    CHECK(host.scheduler.sounding(60));  // ...but the note still physically sounds

    const std::vector<Ev> owed = host.run(0.5, 512, false);
    CAPTURE(describe(owed));
    CHECK(owed == std::vector<Ev>{off(0, 60), off(0, 64), off(0, 67)});
    CHECK(host.lastOk);
    CHECK_FALSE(host.scheduler.sounding(60));
    CHECK(host.run(0.5, 512, false) == kNone);
}

TEST_CASE("regression: owed releases are paid before anything else; an attack on an owed pitch retriggers") {
    SUBCASE("the next playing block pays first, then attacks") {
        Host host;
        host.adopt(snapshotOf(cFour()));
        host.run(0.0, 512, true);
        REQUIRE(host.run(0.5, 512, false, kBpm, 0) == kNone);
        const std::vector<Ev> events = host.run(0.0, 512, true);
        CAPTURE(describe(events));
        CHECK(events == std::vector<Ev>{off(0, 60), off(0, 64), off(0, 67), on(0, 60), on(0, 64), on(0, 67)});
        CHECK(host.lastOk);
    }
    SUBCASE("a preview on an owed pitch sounds it afresh") {
        Host host;
        host.adopt(snapshotOf(cFour()));
        host.run(0.0, 512, true);
        REQUIRE(host.run(0.5, 512, false, kBpm, 0) == kNone);
        CHECK(host.scheduler.pushPreview(previewStart(1, {60})));
        const std::vector<Ev> events = host.run(0.5, 512, false);
        CAPTURE(describe(events));
        CHECK(events == std::vector<Ev>{off(0, 60), off(0, 64), off(0, 67), on(0, 60, 90)});
        CHECK(host.owners(60) == kOwnerPreview);
    }
    SUBCASE("a capacity of 128 pays every owed release at once") {
        std::vector<NoteEvent> all;
        for (int p = 1; p < 128; ++p) {
            all.push_back({0.0, true, p, 100});
        }
        for (int p = 1; p < 128; ++p) {
            all.push_back({4.0, false, p, 0});
        }
        Host host;
        host.adopt(snapshotOf(all, 4.0));
        REQUIRE(host.run(0.0, 512, true).size() == 127);
        REQUIRE(host.run(0.5, 512, false, kBpm, 0) == kNone);
        const std::vector<Ev> owed = host.run(0.5, 512, false, kBpm, Scheduler::kCapacityForImmediateRelease);
        CHECK(owed.size() == 127);
        CHECK(host.lastOk);
        CHECK(std::all_of(owed.begin(), owed.end(), [](const Ev& e) { return !e.on; }));
    }
}

TEST_CASE("regression: mandatory releases survive a flood of preview commands") {
    Host host;
    host.adopt(snapshotOf(cFour()));
    host.run(0.0, 512, true);
    for (int id = 1; id <= 64; ++id) {
        CAPTURE(id);
        REQUIRE(host.scheduler.pushPreview(previewStart(id, {72, 73, 74, 75, 76, 77, 78, 79})));
    }
    const std::vector<Ev> events = host.run(0.5, 512, false);  // transport stops under the flood
    CAPTURE(describe(events));
    for (const int pitch : {60, 64, 67}) {
        CAPTURE(pitch);
        CHECK(std::count(events.begin(), events.end(), off(0, pitch)) == 1);
        CHECK(host.owners(pitch) == 0);
        CHECK_FALSE(host.scheduler.sounding(pitch));
    }
    CHECK(host.scheduler.sounding(72));
    CHECK_FALSE(host.lastOk);  // only eight previews fit; the rest were refused
}

TEST_CASE("regression: a timeline too large for one block is carried over, never lost") {
    std::vector<NoteEvent> dense;  // 127 pitches for a quarter beat: 254 events per wrap
    for (int p = 1; p < 128; ++p) {
        dense.push_back({0.0, true, p, 100});
    }
    for (int p = 1; p < 128; ++p) {
        dense.push_back({0.25, false, p, 0});
    }
    Host host;
    host.adopt(snapshotOf(dense, 0.25));
    bool sounding[128] = {};
    long ons = 0;
    long offs = 0;
    long position = 0;
    auto play = [&](int nFrames, bool playing) {
        const double ppq = static_cast<double>(position) * kBpm / (60.0 * kSr);
        int previous = 0;
        for (const Ev& e : host.run(ppq, nFrames, playing)) {
            REQUIRE(e.offset >= previous);
            previous = e.offset;
            REQUIRE(sounding[e.pitch] != e.on);
            sounding[e.pitch] = e.on;
            (e.on ? ons : offs) += 1;
        }
        for (int p = 0; p < 128; ++p) {
            REQUIRE(host.scheduler.sounding(p) == sounding[p]);
        }
        if (playing) {
            position += nFrames;
        }
    };
    for (int i = 0; i < 3; ++i) {
        play(18000, true);  // three wraps = 762 events per block: more than the scratch holds
    }
    for (int i = 0; i < 200; ++i) {
        play(600, true);  // small blocks let the backlog drain
    }
    play(512, false);
    // 174000 samples = 7.25 beats: 29 wraps attacked, 28 released by the timeline, the last by the stop.
    CHECK(ons == 29 * 127);
    CHECK(offs == 29 * 127);
    CHECK(std::none_of(std::begin(sounding), std::end(sounding), [](bool s) { return s; }));
}

TEST_CASE("regression: an attack coincident with a deferred off at a wrap boundary is never skipped") {
    // endBeat 0.25001: 11 * endBeat + endBeat = 3.0001200000000003 but 12 * endBeat = 3.0001199999999999.
    // Wrap 12 starts at 12 * 0.25001 * 24000 = 72002.88, i.e. sample 72003. A 64-sample block ending at
    // 72003 sees that off at offset 63.88 -> 64 == nFrames, so it is deferred; the on must go with it.
    const std::vector<NoteEvent> one{{0.0, true, 60, 100}, {0.25001, false, 60, 0}};
    const Snapshot snapshot = snapshotOf(one, 0.25001);
    const long total = 72003 + 64;
    const std::vector<AbsEv> actual = playPartitioned(snapshot, total, {71939, 64});
    const std::vector<AbsEv> expected = occurrencesBefore(snapshot, total);
    REQUIRE(actual.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        CAPTURE(i);
        CHECK(actual[i] == expected[i]);
    }
    CHECK(std::count(actual.begin(), actual.end(), AbsEv{72003, false, 60}) == 1);
    CHECK(std::count(actual.begin(), actual.end(), AbsEv{72003, true, 60}) == 1);
}

TEST_CASE("regression: an empty block is a no-op") {
    Host host;
    host.adopt(snapshotOf(cFour()));
    host.run(0.0, 512, true);
    CHECK(host.scheduler.pushPreview(previewStart(1, {48})));

    const std::vector<Ev> empty = host.run(0.5, 0, false);  // a stop with no frames
    CHECK(empty == kNone);
    CHECK(host.lastOk);
    CHECK(host.owners(60) == kOwnerProgression);  // nothing released...
    CHECK(host.owners(48) == 0);                  // ...and the preview still queued

    const std::vector<Ev> events = host.run(0.5, 512, false);
    CAPTURE(describe(events));
    CHECK(events == std::vector<Ev>{on(0, 48, 90), off(0, 60), off(0, 64), off(0, 67)});

    // An empty playing block advances nothing either: the next block is still contiguous.
    Host other;
    other.adopt(snapshotOf(cFour()));
    other.run(0.0, 93600, true);  // ends at 3.9
    CHECK(other.run(3.9, 0, true) == kNone);
    const std::vector<Ev> wrap = other.run(3.9, 4800, true);
    CAPTURE(describe(wrap));
    CHECK(wrap == std::vector<Ev>{off(2400, 60), off(2400, 64), off(2400, 67), on(2400, 60), on(2400, 64), on(2400, 67)});
}

TEST_CASE("regression: a stop in the same batch frees its preview slot for a start") {
    Host host;
    for (int id = 1; id <= 8; ++id) {
        REQUIRE(host.scheduler.pushPreview(previewStart(id, {60 + id})));
    }
    REQUIRE(host.run(0.0, 512, false).size() == 8);
    REQUIRE(host.lastOk);
    SUBCASE("stop then start") {
        CHECK(host.scheduler.pushPreview(previewStop(1)));
        CHECK(host.scheduler.pushPreview(previewStart(9, {80})));
        const std::vector<Ev> events = host.run(0.0, 512, false);
        CAPTURE(describe(events));
        CHECK(events == std::vector<Ev>{off(0, 61), on(0, 80, 90)});
        CHECK(host.lastOk);
    }
    SUBCASE("restart of a running id") {
        CHECK(host.scheduler.pushPreview(previewStart(1, {61, 70})));
        const std::vector<Ev> events = host.run(0.0, 512, false);
        CAPTURE(describe(events));
        CHECK(events == std::vector<Ev>{off(0, 61), on(0, 61, 90), on(0, 70, 90)});
        CHECK(host.lastOk);
    }
    SUBCASE("a ninth concurrent preview is refused and reported") {
        CHECK(host.scheduler.pushPreview(previewStart(9, {80})));
        CHECK(host.run(0.0, 512, false) == kNone);
        CHECK_FALSE(host.lastOk);
    }
}

TEST_CASE("regression: the snapshot size bound") {
    std::vector<NoteEvent> big;
    for (int i = 0; i < 2048; ++i) {
        big.push_back({i * 0.001, true, 60, 100});
        big.push_back({i * 0.001 + 0.0005, false, 60, 0});
    }
    REQUIRE(big.size() == Scheduler::kMaxSnapshotEvents);
    Host host;
    host.adopt(snapshotOf(big, 4.0));
    const std::vector<Ev> played = host.run(0.0, 512, true);  // 512 samples = 0.0213 beats: 22 notes
    CHECK(played.size() == 43);
    CHECK(host.lastOk);
#ifdef NDEBUG
    // One more event than the bound: adopted as an empty snapshot (a debug build asserts instead).
    big.push_back({4.0, false, 60, 0});
    host.adopt(snapshotOf(big, 4.0, true, 2));
    const std::vector<Ev> flushed = host.run(512.0 / kSamplesPerBeat, 512, true);
    CAPTURE(describe(flushed));
    CHECK(flushed == std::vector<Ev>{off(0, 60)});
    CHECK(host.run(1024.0 / kSamplesPerBeat, 4 * kSamplesPerBeat, true) == kNone);
#endif
}

TEST_CASE("scheduler: panic releases the progression and the previews, with no chase") {
    Host host;
    host.adopt(snapshotOf(cFour()));
    host.run(0.0, 512, true);
    CHECK(host.scheduler.pushPreview(previewStart(1, {48, 52})));
    REQUIRE(host.run(512.0 / kSamplesPerBeat, 512, true) == std::vector<Ev>{on(0, 48, 90), on(0, 52, 90)});

    host.scheduler.panic();
    CHECK(host.owners(60) == 0);  // the progression's
    CHECK(host.owners(48) == 0);  // the preview's
    CHECK(host.scheduler.sounding(60));  // owed until the next block
    CHECK(host.scheduler.sounding(48));

    const std::vector<Ev> events = host.run(1024.0 / kSamplesPerBeat, 512, true);
    CAPTURE(describe(events));
    CHECK(events == std::vector<Ev>{off(0, 48), off(0, 52), off(0, 60), off(0, 64), off(0, 67)});
    CHECK(host.lastOk);
    CHECK_FALSE(host.scheduler.sounding(48));

    // No chase: silence until the progression's next attack, at the wrap.
    const std::vector<Ev> wrap = host.run(1536.0 / kSamplesPerBeat, 4 * kSamplesPerBeat, true);
    const int offset = 4 * kSamplesPerBeat - 1536;
    CAPTURE(describe(wrap));
    CHECK(wrap == std::vector<Ev>{on(offset, 60), on(offset, 64), on(offset, 67)});

    // The preview's slot is gone: its stop is a no-op.
    CHECK(host.scheduler.pushPreview(previewStop(1)));
    CHECK(host.run((1536.0 + 4 * kSamplesPerBeat) / kSamplesPerBeat, 512, true) == kNone);
    CHECK(host.lastOk);
}

TEST_CASE("regression: panic discards the preview commands queued before it and keeps those pushed after") {
    Host host;
    host.run(0.0, 512, false);

    // The repro: a start queued before the panic must not sound in the block that follows it.
    CHECK(host.scheduler.pushPreview(previewStart(1, {48, 52})));
    host.scheduler.panic();
    CHECK(host.run(0.0, 0, false) == kNone);  // an empty block drains nothing and must not consume the cut
    const std::vector<Ev> after = host.run(0.0, 512, false);
    CAPTURE(describe(after));
    CHECK(after == kNone);
    CHECK(host.lastOk);
    CHECK(host.owners(48) == 0);
    CHECK(host.owners(52) == 0);
    CHECK_FALSE(host.scheduler.sounding(48));

    // A command pushed after panic returned is served as usual: the cut is consumed by one drain.
    CHECK(host.scheduler.pushPreview(previewStart(2, {55})));
    CHECK(host.run(0.0, 512, false) == std::vector<Ev>{on(0, 55, 90)});
    CHECK(host.owners(55) == kOwnerPreview);

    // Both sides of the cut in one ring: the queued start is dropped, the one pushed after plays, and the
    // panic's own release of the running preview is paid first.
    CHECK(host.scheduler.pushPreview(previewStart(3, {60})));
    CHECK(host.scheduler.pushPreview(previewStop(2)));
    host.scheduler.panic();
    CHECK(host.scheduler.pushPreview(previewStart(4, {62})));
    const std::vector<Ev> mixed = host.run(0.0, 512, false);
    CAPTURE(describe(mixed));
    CHECK(mixed == std::vector<Ev>{off(0, 55), on(0, 62, 90)});
    CHECK(host.lastOk);
    CHECK(host.owners(60) == 0);
    CHECK(host.owners(62) == kOwnerPreview);

    // Two panics before one drain: the later cut wins, so a start between them is dropped too.
    CHECK(host.scheduler.pushPreview(previewStart(5, {64})));
    host.scheduler.panic();
    CHECK(host.scheduler.pushPreview(previewStart(6, {65})));
    host.scheduler.panic();
    CHECK(host.run(0.0, 512, false) == std::vector<Ev>{off(0, 62)});
    CHECK(host.owners(64) == 0);
    CHECK(host.owners(65) == 0);
}

TEST_CASE("scheduler: reset keeps the queued preview commands; only panic discards them") {
    Host host;
    CHECK(host.scheduler.pushPreview(previewStart(1, {48})));
    host.scheduler.reset(kSr);
    CHECK(host.run(0.0, 512, false) == std::vector<Ev>{on(0, 48, 90)});
    CHECK(host.owners(48) == kOwnerPreview);
}

// --- Follow-up review regressions ------------------------------------------------------------------

namespace {

/// Tracks what physically sounds and fails on a double on, an orphan off, a disordered offset or a
/// mismatch with the scheduler's own `sounding`.
struct Model {
    bool sounding[128] = {};
    long ons = 0;
    long offs = 0;
    std::vector<Ev> sequence;  // every emitted event, in order

    void take(const Host& host, const std::vector<Ev>& events, int nFrames) {
        int previous = 0;
        for (const Ev& e : events) {
            CAPTURE(e.pitch);
            CAPTURE(e.offset);
            REQUIRE(e.offset >= previous);
            REQUIRE(e.offset < nFrames);
            previous = e.offset;
            REQUIRE(sounding[e.pitch] != e.on);
            sounding[e.pitch] = e.on;
            (e.on ? ons : offs) += 1;
            sequence.push_back(e);
        }
        for (int p = 0; p < 128; ++p) {
            REQUIRE(host.scheduler.sounding(p) == sounding[p]);
        }
    }

    bool silent() const {
        return std::none_of(std::begin(sounding), std::end(sounding), [](bool s) { return s; });
    }
};

/// The (on, pitch) sequence of every occurrence of `snapshot` with canonical beat below `untilBeat`, in
/// playback order: wrap by wrap, the snapshot's own order within a wrap, its final offs on the next
/// wrap's start.
std::vector<std::pair<bool, int>> occurrenceSequence(const Snapshot& snapshot, double untilBeat) {
    std::vector<std::pair<bool, int>> sequence;
    for (int k = 0;; ++k) {
        bool any = false;
        for (std::size_t i = 0; i < snapshot.count; ++i) {
            const NoteEvent& e = snapshot.events[i];
            const double beat = e.beat >= snapshot.endBeat ? (k + 1) * snapshot.endBeat : k * snapshot.endBeat + e.beat;
            if (beat < untilBeat) {
                sequence.emplace_back(e.on, e.pitch);
                any = true;
            }
        }
        if (!any || !snapshot.loop) {
            break;
        }
    }
    return sequence;
}

}  // namespace

TEST_CASE("regression: carried-over events keep their chronological order") {
    SUBCASE("300 notes on one pitch inside one beat") {
        std::vector<NoteEvent> notes;
        for (int i = 0; i < 300; ++i) {
            notes.push_back({i * 0.002, true, 60, 100});
            notes.push_back({i * 0.002 + 0.001, false, 60, 0});
        }
        Host host;
        host.adopt(snapshotOf(notes, 1.0, /*loop=*/false));
        Model model;
        const std::vector<Ev> first = host.run(0.0, 24000, true);  // 600 events for one block
        model.take(host, first, 24000);
        // Everything the scratch holds beyond the reserve and the start's own release-all.
        CHECK(first.size() == Scheduler::kMaxEventsPerBlock - Scheduler::kReservedReleaseEvents - 1);
        CHECK_FALSE(host.lastOk);
        const std::vector<Ev> rest = host.run(1.0, 512, true);  // the second of ceil(600 / 480) blocks
        model.take(host, rest, 512);
        CHECK(rest.size() == 600 - first.size());
        CHECK(std::all_of(rest.begin(), rest.end(), [](const Ev& e) { return e.offset == 0; }));
        CHECK(host.lastOk);
        REQUIRE(!rest.empty());
        CHECK_FALSE(rest.back().on);
        CHECK(model.silent());
        CHECK(host.run(1.0 + 512.0 / kSamplesPerBeat, 512, true) == kNone);
        CHECK(host.lastOk);
    }
    SUBCASE("100 alternating quarter-beat chords in one 25-beat block: the stale backlog is dropped loudly") {
        DocumentStore store;
        for (int i = 0; i < 100; ++i) {
            store.appendChord(i % 2 ? chord(9, "min") : chord(0, "maj"), 0.25, 100);
        }
        const Compiled compiled = compile(store.doc());
        REQUIRE(compiled.endBeat == 25.0);
        Host host;
        host.adopt(snapshotOf(compiled, /*loop=*/false));
        Model model;
        const std::vector<Ev> first = host.run(0.0, 600001, true);
        model.take(host, first, 600001);
        CHECK_FALSE(host.lastOk);
        // The next block starts 25 beats later: the backlog is more than kMaxLagBeats old. It is dropped
        // (reported), every progression note released, and the position chased (past the end: nothing).
        const std::vector<Ev> recovery = host.run(600001.0 / kSamplesPerBeat, 512, true);
        model.take(host, recovery, 512);
        CHECK_FALSE(host.lastOk);
        CHECK(model.silent());
        CHECK(std::all_of(recovery.begin(), recovery.end(), [](const Ev& e) { return !e.on; }));
        CHECK(host.run((600001.0 + 512.0) / kSamplesPerBeat, 512, true) == kNone);
        CHECK(host.lastOk);
    }
}

TEST_CASE("regression: a coincident group larger than the scratch drains across blocks") {
    std::vector<NoteEvent> notes;  // 600 distinct beats that all quantise to sample 24000
    for (int i = 0; i < 300; ++i) {
        notes.push_back({1.0 + i * 1e-9, true, 60, 100});
        notes.push_back({1.0 + i * 1e-9 + 5e-10, false, 60, 0});
    }
    Host host;
    host.adopt(snapshotOf(notes, 2.0, /*loop=*/false));
    Model model;
    const std::vector<Ev> first = host.run(0.0, 48000, true);
    model.take(host, first, 48000);
    CHECK(first.size() == Scheduler::kMaxEventsPerBlock - Scheduler::kReservedReleaseEvents - 1);
    CHECK(std::all_of(first.begin(), first.end(), [](const Ev& e) { return e.offset == 24000; }));
    CHECK_FALSE(host.lastOk);
    const std::vector<Ev> rest = host.run(2.0, 512, true);  // the second of ceil(600 / 480) blocks
    model.take(host, rest, 512);
    CHECK(rest.size() == 600 - first.size());
    CHECK(std::all_of(rest.begin(), rest.end(), [](const Ev& e) { return e.offset == 0; }));
    CHECK(host.lastOk);
    CHECK(model.silent());
    for (int block = 0; block < 10; ++block) {
        CAPTURE(block);
        REQUIRE(host.run(2.0 + (block + 1) * 512.0 / kSamplesPerBeat, 512, true) == kNone);
        REQUIRE(host.lastOk);
    }
}

TEST_CASE("regression: a backlog older than kMaxLagBeats is dropped loudly, with release-all and chase") {
    std::vector<NoteEvent> dense;  // 127 pitches for a quarter beat: 254 events per wrap
    for (int p = 1; p < 128; ++p) {
        dense.push_back({0.0, true, p, 100});
    }
    for (int p = 1; p < 128; ++p) {
        dense.push_back({0.25, false, p, 0});
    }
    Host host;
    host.adopt(snapshotOf(dense, 0.25));
    Model model;
    // Two beats in one block: 2032 events, of which one scratch-full is emitted; the rest is ~1.5 beats
    // behind by the time the next block starts.
    model.take(host, host.run(0.0, 48000, true), 48000);
    CHECK_FALSE(host.lastOk);
    const std::vector<Ev> dropped = host.run(2.0, 512, true);
    model.take(host, dropped, 512);
    CHECK_FALSE(host.lastOk);  // the stale backlog is dropped: reported
    // Every progression note was released, then the position chased: at beat 2.0 the wrap's attacks are
    // the timeline's own (an attack exactly on the block start is not chased), so the block holds offs
    // for whatever sounded, then 127 ons.
    REQUIRE(!dropped.empty());
    CHECK(std::all_of(dropped.begin(), dropped.end(), [](const Ev& e) { return e.offset == 0; }));
    const auto firstOn = std::find_if(dropped.begin(), dropped.end(), [](const Ev& e) { return e.on; });
    CHECK(std::all_of(firstOn, dropped.end(), [](const Ev& e) { return e.on; }));
    CHECK(std::count_if(dropped.begin(), dropped.end(), [](const Ev& e) { return e.on; }) == 127);
    // In sync again: the next block is complete and carries exactly its own events.
    const std::vector<Ev> next = host.run(2.0 + 512.0 / kSamplesPerBeat, 5488, true);  // up to beat 2.25
    model.take(host, next, 5488);
    CHECK(next == kNone);
    CHECK(host.lastOk);
    const std::vector<Ev> boundary = host.run(2.25, 6000, true);
    model.take(host, boundary, 6000);
    CHECK(boundary.size() == 254);
    CHECK(std::all_of(boundary.begin(), boundary.end(), [](const Ev& e) { return e.offset == 0; }));
    CHECK(host.lastOk);
    model.take(host, host.run(2.5, 512, false), 512);
    CHECK(model.silent());
}

TEST_CASE("regression: sustained overload stays bounded and never leaves stuck notes") {
    std::vector<NoteEvent> dense;  // 127 pitches for a quarter beat: 254 events per wrap
    for (int p = 1; p < 128; ++p) {
        dense.push_back({0.0, true, p, 100});
    }
    for (int p = 1; p < 128; ++p) {
        dense.push_back({0.25, false, p, 0});
    }
    Host host;
    host.adopt(snapshotOf(dense, 0.25));
    Model model;
    long position = 0;
    int incomplete = 0;
    int allLateRun = 0;
    for (int block = 0; block < 40; ++block) {
        CAPTURE(block);
        const double ppq = static_cast<double>(position) * kBpm / (60.0 * kSr);
        const std::vector<Ev> events = host.run(ppq, 18000, true);  // 762 events per block
        model.take(host, events, 18000);
        incomplete += host.lastOk ? 0 : 1;
        position += 18000;
        // With the backlog bounded, the scheduler keeps catching up: a block whose events are all overdue
        // (every offset 0) is followed within a few blocks by one that plays in real time again.
        const bool allLate = std::all_of(events.begin(), events.end(), [](const Ev& e) { return e.offset == 0; });
        allLateRun = allLate ? allLateRun + 1 : 0;
        CHECK(allLateRun < 4);
    }
    CHECK(incomplete == 40);
    // Small blocks drain whatever is left, then a wrap boundary arrives in sync: exactly its 254 events.
    for (int block = 0; block < 20 && !host.lastOk; ++block) {
        const double ppq = static_cast<double>(position) * kBpm / (60.0 * kSr);
        model.take(host, host.run(ppq, 600, true), 600);
        position += 600;
    }
    REQUIRE(host.lastOk);
    const long toBoundary = 6000 - position % 6000;
    model.take(host, host.run(static_cast<double>(position) * kBpm / (60.0 * kSr), static_cast<int>(toBoundary), true),
               static_cast<int>(toBoundary));
    position += toBoundary;
    const std::vector<Ev> boundary = host.run(static_cast<double>(position) * kBpm / (60.0 * kSr), 6000, true);
    model.take(host, boundary, 6000);
    CHECK(boundary.size() == 254);
    CHECK(host.lastOk);
    model.take(host, host.run(static_cast<double>(position + 6000) * kBpm / (60.0 * kSr), 512, false), 512);
    CHECK(model.silent());
}

TEST_CASE("regression: a tempo change neither loses nor duplicates a boundary attack") {
    SUBCASE("an attack rounded just past the block edge is deferred, then emitted once (120 -> 119.94)") {
        const std::vector<NoteEvent> one{{(512.0 - 0.4999) / 24000.0, true, 60, 100}, {2.0, false, 60, 0}};
        Host host;
        host.adopt(snapshotOf(one, 2.0, /*loop=*/false));
        CHECK(host.run(0.0, 512, true, 120.0) == kNone);
        const std::vector<Ev> second = host.run(512.0 / 24000.0, 512, true, 119.94);
        CAPTURE(describe(second));
        CHECK(second == std::vector<Ev>{on(0, 60)});
        CHECK(host.scheduler.sounding(60));
        CHECK(host.run(512.0 / 24000.0 + 512.0 * 119.94 / (60.0 * kSr), 512, true, 119.94) == kNone);
    }
    SUBCASE("an attack rounded just before the block edge is emitted once (120 -> 120.06)") {
        const std::vector<NoteEvent> one{{(512.0 - 0.5001) / 24000.0, true, 60, 100}, {2.0, false, 60, 0}};
        Host host;
        host.adopt(snapshotOf(one, 2.0, /*loop=*/false));
        const std::vector<Ev> first = host.run(0.0, 512, true, 120.0);
        CAPTURE(describe(first));
        CHECK(first == std::vector<Ev>{on(511, 60)});
        CHECK(host.run(512.0 / 24000.0, 512, true, 120.06) == kNone);
        CHECK(host.scheduler.sounding(60));
    }
}

TEST_CASE("regression: tempo jitter on every block emits each occurrence exactly once, in order") {
    const Snapshot snapshot = snapshotOf(cThenAm());
    Host host;
    host.adopt(snapshot);
    Model model;
    std::mt19937 rng(7u);
    auto randInt = [&rng](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    auto randReal = [&rng](double lo, double hi) { return std::uniform_real_distribution<double>(lo, hi)(rng); };
    long double ppq = 0.0L;  // the host's own running position
    for (int block = 0; block < 3000; ++block) {
        CAPTURE(block);
        const int nFrames = randInt(1, 2048);
        const double bpm = 120.0 + randReal(-1.0, 1.0);
        model.take(host, host.run(static_cast<double>(ppq), nFrames, true, bpm), nFrames);
        REQUIRE(host.lastOk);
        ppq += static_cast<long double>(nFrames) * bpm / (60.0L * kSr);
    }
    // Everything emitted is the occurrence sequence, as a prefix, and that prefix reaches the end.
    const double finalBeat = static_cast<double>(ppq);
    const auto atLeast = occurrenceSequence(snapshot, finalBeat - 0.001);
    const auto atMost = occurrenceSequence(snapshot, finalBeat + 0.001);
    REQUIRE(model.sequence.size() >= atLeast.size());
    REQUIRE(model.sequence.size() <= atMost.size());
    for (std::size_t i = 0; i < model.sequence.size(); ++i) {
        CAPTURE(i);
        REQUIRE(model.sequence[i].on == atMost[i].first);
        REQUIRE(model.sequence[i].pitch == atMost[i].second);
    }
}

// --- Third review regressions ------------------------------------------------------------------------

TEST_CASE("regression: re-adopting the same generation changes nothing about playback") {
    const std::vector<NoteEvent> one{{0.0, true, 60, 100}, {4.0, false, 60, 0}};
    Host host;
    host.adopt(snapshotOf(one, 4.0, true, 1));
    Model model;
    model.take(host, host.run(0.0, 24000, true), 24000);  // on 60 at beat 0
    host.adopt(snapshotOf(one, 4.0, true, 1));            // the identical generation again, at beat 1
    const std::vector<Ev> crossing = host.run(1.0, 96000, true);  // [1, 5): the beat-4 release and wrap
    model.take(host, crossing, 96000);
    CAPTURE(describe(crossing));
    CHECK(crossing == std::vector<Ev>{off(72000, 60), on(72000, 60)});
    CHECK(host.lastOk);
    const std::vector<Ev> later = host.run(5.0, 96000, true);  // [5, 9): the beat-8 release and wrap
    model.take(host, later, 96000);
    CAPTURE(describe(later));
    CHECK(later == std::vector<Ev>{off(72000, 60), on(72000, 60)});
    CHECK(host.lastOk);
    // Stopped, an identical re-adopt is just as silent.
    model.take(host, host.run(9.0, 512, false), 512);
    CHECK(model.silent());
    host.adopt(snapshotOf(one, 4.0, true, 1));
    CHECK(host.run(9.0, 512, false) == kNone);
    CHECK(host.lastOk);
}

TEST_CASE("regression: a changed adopt that abandons overdue occurrences reports it") {
    std::vector<NoteEvent> dense;  // 127 pitches for a quarter beat: 254 events per wrap
    for (int p = 1; p < 128; ++p) {
        dense.push_back({0.0, true, p, 100});
    }
    for (int p = 1; p < 128; ++p) {
        dense.push_back({0.25, false, p, 0});
    }
    Host host;
    host.adopt(snapshotOf(dense, 0.25, true, 1));
    Model model;
    model.take(host, host.run(0.0, 48000, true), 48000);  // two beats: 2032 events, a scratch-full emitted
    REQUIRE_FALSE(host.lastOk);

    const std::vector<NoteEvent> one{{0.0, true, 60, 100}, {4.0, false, 60, 0}};
    host.adopt(snapshotOf(one, 4.0, true, 2));  // the overdue remainder of the old timeline is abandoned
    const std::vector<Ev> next = host.run(2.0, 512, true);
    model.take(host, next, 512);
    CAPTURE(describe(next));
    CHECK_FALSE(host.lastOk);
    // ...and the discontinuity still does its job: every old note released, the new position chased.
    REQUIRE(!next.empty());
    CHECK(next.back() == on(0, 60));
    CHECK(std::all_of(next.begin(), next.end() - 1, [](const Ev& e) { return !e.on; }));
    CHECK(host.owners(60) == kOwnerProgression);
    for (int p = 1; p < 128; ++p) {
        if (p != 60) {
            CHECK_FALSE(host.scheduler.sounding(p));
        }
    }
    CHECK(host.run(2.0 + 512.0 / kSamplesPerBeat, 512, true) == kNone);
    CHECK(host.lastOk);
}

// --- Snapshot lifetime: the array an adopt replaces is never read by that adopt ---------------------

TEST_CASE("regression: adopt judges the abandoned backlog without reading the previous array") {
    // From the review of the triple-buffer handoff: `acquire` hands the old front slot back to the producer
    // before `adopt` runs, so the array the scheduler last adopted may already be overwritten. `adopt`
    // used to read it (the cursor's next occurrence) to decide whether an overdue backlog was abandoned,
    // and a recycled slot could flip that verdict. Both runs must now agree with the preserved-array one.
    const auto run = [](bool recycle) {
        std::vector<NoteEvent> old{{0.0, true, 60, 100}, {2.0, false, 60, 0}, {2.0, true, 64, 100}, {4.0, false, 64, 0}};
        Host host;
        host.adopt(snapshotOf(old, 4.0, true, 1));
        host.run(0.0, kSamplesPerBeat, true);  // one beat: the cursor rests on the off at beat 2, nothing overdue
        REQUIRE(host.lastOk);
        if (recycle) {
            for (NoteEvent& e : old) {
                e = NoteEvent{0.0, true, 1, 1};  // garbage, every event now "overdue" if anyone looked
            }
        }
        const std::vector<NoteEvent> fresh{{0.0, true, 62, 100}, {4.0, false, 62, 0}};
        host.adopt(snapshotOf(fresh, 4.0, true, 2));
        std::vector<std::vector<Ev>> blocks;
        std::vector<bool> ok;
        for (int i = 0; i < 4; ++i) {
            blocks.push_back(host.run(1.0 + i * 512.0 / kSamplesPerBeat, 512, true));
            ok.push_back(host.lastOk);
        }
        return std::make_pair(blocks, ok);
    };
    const auto control = run(false);
    const auto recycled = run(true);
    CHECK(control.second == std::vector<bool>{true, true, true, true});
    CHECK(recycled.second == control.second);
    CHECK(recycled.first == control.first);
    REQUIRE(!control.first.empty());
    CHECK(control.first[0] == std::vector<Ev>{off(0, 60), on(0, 62)});  // the flush releases, the chase attacks
}

TEST_CASE("regression: a slot released by acquire may be rewritten before adopt without affecting playback") {
    // TripleBuffer + Scheduler as ProcessBlock uses them: acquire, then adopt. Between the two the
    // producer may publish twice, which makes the released front its back slot and overwrites it. The
    // poisoned run must play exactly like the clean one.
    struct Slot {
        std::array<NoteEvent, 8> events;
        std::size_t count;
        double endBeat;
        bool loop;
    };
    const auto publish = [](TripleBuffer<Slot>& buffer, std::uint32_t generation, int pitch) {
        Slot& back = buffer.beginWrite();
        back.events = {{{0.0, true, pitch, 100}, {2.0, false, pitch, 0}, {2.0, true, pitch + 4, 100}, {4.0, false, pitch + 4, 0}}};
        back.count = 4;
        back.endBeat = 4.0;
        back.loop = true;
        buffer.publish(generation);
    };
    const auto snapshotOfSlot = [](const Slot* slot, std::uint32_t generation) {
        return Snapshot{slot->events.data(), slot->count, slot->endBeat, slot->loop, generation};
    };
    const auto run = [&](bool poison) {
        TripleBuffer<Slot> buffer;
        Host host;
        publish(buffer, 1, 60);
        const Slot* first = buffer.acquire();
        REQUIRE(first != nullptr);
        host.adopt(snapshotOfSlot(first, buffer.currentGeneration()));
        host.run(0.0, kSamplesPerBeat, true);
        REQUIRE(host.lastOk);

        publish(buffer, 2, 62);
        const Slot* fresh = buffer.acquire();  // releases `first` to the producer
        REQUIRE(fresh != nullptr);
        REQUIRE(fresh != first);
        if (poison) {
            publish(buffer, 3, 64);  // the producer's back slot is now `first`
            Slot& back = buffer.beginWrite();
            REQUIRE(&back == first);
            for (NoteEvent& e : back.events) {
                e = NoteEvent{0.0, true, 1, 1};
            }
            back.count = back.events.size();
            back.endBeat = 0.25;
        }
        host.adopt(snapshotOfSlot(fresh, buffer.currentGeneration()));  // generation 2, never 3: not acquired
        std::vector<std::vector<Ev>> blocks;
        std::vector<bool> ok;
        for (int i = 0; i < 4; ++i) {
            blocks.push_back(host.run(1.0 + i * 512.0 / kSamplesPerBeat, 512, true));
            ok.push_back(host.lastOk);
        }
        return std::make_pair(blocks, ok);
    };
    const auto clean = run(false);
    const auto poisoned = run(true);
    CHECK(clean.second == std::vector<bool>{true, true, true, true});
    CHECK(poisoned.second == clean.second);
    CHECK(poisoned.first == clean.first);
    REQUIRE(!clean.first.empty());
    CHECK(clean.first[0] == std::vector<Ev>{off(0, 60), on(0, 62)});
}

// --- Performed events (feel plan, Task 4) ------------------------------------------------------------

namespace {

/// One bar of C major with `feel`.
Compiled performedC(const Feel& feel) {
    DocumentStore store;
    store.appendChord(chord(0, "maj"), 4.0, 100);
    store.setFeel(feel);
    return compile(store.doc());
}

/// A test-only eight-note chord type (the voice count the snapshot budget leaves room for), so that a
/// document can sit exactly at the per-beat cap.
const ChordType kCluster8{"cluster8", "cl8", {0, 1, 2, 3, 4, 5, 6, 7}, Family::Other};

}  // namespace

TEST_CASE("feel scheduler: a strummed block wraps with its final offs at endBeat and its ons inside") {
    const Compiled compiled = performedC(Feel{Rhythm::Block, Strum::Down, StrumSpeed::Medium, Humanise::Off});
    REQUIRE(compiled.endBeat == 4.0);
    for (const NoteEvent& e : compiled.events) {
        CHECK((e.on ? e.beat < compiled.endBeat : e.beat <= compiled.endBeat));
    }
    Host host;
    host.adopt(snapshotOf(compiled));
    // Ons 20 and 40 ticks apart: 20 / 960 beat = 500 samples at 24000 per beat.
    const std::vector<Ev> first = host.run(0.0, 84000, true);  // [0, 3.5)
    CAPTURE(describe(first));
    CHECK(first == std::vector<Ev>{on(0, 60), on(500, 64), on(1000, 67)});
    const std::vector<Ev> wrap = host.run(3.5, 24000, true);  // [3.5, 4.5)
    CAPTURE(describe(wrap));
    CHECK(host.lastOk);
    CHECK(wrap == std::vector<Ev>{off(12000, 60), off(12000, 64), off(12000, 67), on(12000, 60), on(12500, 64),
                                  on(13000, 67)});
}

TEST_CASE("feel scheduler: a non-tick-aligned progression wraps at the compiled period, 722 ticks") {
    // Three C chords of 0.2506 beat, strummed down: boundaries lround(0.2506 * 960) = 241, 481 and 722 ticks
    // (the authored sum, 0.7518 beat, is 721.7 ticks). Ons 20 ticks apart at 25 samples a tick (24000 per beat).
    DocumentStore store;
    for (int i = 0; i < 3; ++i) store.appendChord(chord(0, "maj"), 0.2506, 100);
    store.setFeel(Feel{Rhythm::Block, Strum::Down, StrumSpeed::Medium, Humanise::Off});
    const Compiled compiled = compile(store.doc());
    REQUIRE_FALSE(compiled.feelLimited);
    REQUIRE(compiled.endBeat == 722.0 / 960.0);
    REQUIRE(compiled.endBeat != store.doc().endBeat());
    REQUIRE(compiled.events.size() == 18);
    for (const NoteEvent& e : compiled.events) {
        CHECK((e.on ? e.beat < compiled.endBeat : e.beat <= compiled.endBeat));
    }
    CHECK(compiled.events.back() == NoteEvent{722.0 / 960.0, false, 67, 0});  // the final offs sit on the period

    Host host;
    host.adopt(snapshotOf(compiled));
    // [0, 1) beat: the period is 18050 samples, so the block holds the whole first pass, its final offs on the
    // wrap at 18050 and the start of the second pass.
    const std::vector<Ev> first = host.run(0.0, 24000, true);
    CAPTURE(describe(first));
    CHECK(host.lastOk);
    CHECK(first == std::vector<Ev>{on(0, 60), on(500, 64), on(1000, 67),
                                   off(6025, 60), off(6025, 64), off(6025, 67),
                                   on(6025, 60), on(6525, 64), on(7025, 67),
                                   off(12025, 60), off(12025, 64), off(12025, 67),
                                   on(12025, 60), on(12525, 64), on(13025, 67),
                                   off(18050, 60), off(18050, 64), off(18050, 67),
                                   on(18050, 60), on(18550, 64), on(19050, 67)});

    // Several loops in odd blocks: every note released, nothing dropped, and five full passes at least.
    Host looping;
    looping.adopt(snapshotOf(compiled));
    Model model;
    long position = 0;
    const int sizes[] = {37, 512, 1999, 64, 4096, 3};
    for (int block = 0; position < 5 * 18050 + 7000; ++block) {
        const int n = sizes[block % 6];
        model.take(looping, looping.run(static_cast<double>(position) / kSamplesPerBeat, n, true), n);
        REQUIRE(looping.lastOk);
        position += n;
    }
    // Stop: whatever sounds is released at once.
    model.take(looping, looping.run(static_cast<double>(position) / kSamplesPerBeat, 512, false), 512);
    CHECK(model.silent());
    CHECK(model.ons == model.offs);
    CHECK(model.ons >= 9 * 5);  // five full passes at least, 9 ons each
}

TEST_CASE("feel scheduler: an over-budget progression wraps at the authored sum, not a tick-rounded period") {
    // 64 one-bar C9 and a 4.0004-beat one: 5200+ Subdivided events, so block chords play (the fallback) and
    // the period is the authored sum, 260.0004 beats, not the 260 beats (249600 ticks) a performance would round to.
    DocumentStore store;
    for (int i = 0; i < 64; ++i) store.appendChord(chord(0, "9"), 4.0, 100);
    store.appendChord(chord(0, "9"), 4.0004, 100);
    store.setFeel(Feel{Rhythm::Subdivided});
    double authored = 0.0;
    for (const ChordEvent& e : store.doc().events) authored += e.lengthBeats;
    const Compiled compiled = compile(store.doc());
    REQUIRE(compiled.feelLimited);
    REQUIRE(compiled.endBeat == authored);
    REQUIRE(authored != 260.0);
    REQUIRE(compiled.events.size() == 650);

    Host host;
    host.adopt(snapshotOf(compiled));
    // [259.5, 260.5) starts inside the last chord (it began at beat 256), so it is chased at once; its five
    // offs and the first chord's five ons share the wrap, 0.5004 beat in: 12009.6 samples, placed at 12010.
    const std::vector<Ev> wrap = host.run(259.5, 24000, true);
    CAPTURE(describe(wrap));
    CHECK(host.lastOk);
    CHECK(wrap == std::vector<Ev>{on(0, 60), on(0, 64), on(0, 67), on(0, 70), on(0, 74),
                                  off(12010, 60), off(12010, 64), off(12010, 67), off(12010, 70), off(12010, 74),
                                  on(12010, 60), on(12010, 64), on(12010, 67), on(12010, 70), on(12010, 74)});
}

TEST_CASE("feel scheduler: a seek into a pulse hit chases it; a seek into the gate's gap chases nothing") {
    const Compiled compiled = performedC(Feel{Rhythm::Pulse});
    Host host;
    host.adopt(snapshotOf(compiled));
    CHECK(host.run(0.0, 4800, true) == std::vector<Ev>{on(0, 60), on(0, 64), on(0, 67)});
    // 1.5 is inside the hit [1.0, 1.9): release, then chase that hit at its own velocity (90).
    const std::vector<Ev> chased = host.run(1.5, 4800, true);
    CAPTURE(describe(chased));
    CHECK(chased == std::vector<Ev>{off(0, 60), off(0, 64), off(0, 67), on(0, 60, 90), on(0, 64, 90), on(0, 67, 90)});
    // 1.95 is in the gap [1.9, 2.0): release, chase nothing, then the hit at 2.0 (1200 samples in).
    const std::vector<Ev> gap = host.run(1.95, 4800, true);
    CAPTURE(describe(gap));
    CHECK(gap == std::vector<Ev>{off(0, 60), off(0, 64), off(0, 67), on(1200, 60, 90), on(1200, 64, 90),
                                 on(1200, 67, 90)});
}

TEST_CASE("feel scheduler: a preview overlapping a performed pitch survives the gate and the retrigger") {
    const Compiled compiled = performedC(Feel{Rhythm::Pulse});
    Host host;
    host.adopt(snapshotOf(compiled));
    Model model;
    model.take(host, host.run(0.0, 4800, true), 4800);
    CHECK(host.scheduler.pushPreview(previewStart(1, {64, 71})));
    const std::vector<Ev> started = host.run(0.2, 4800, true);
    model.take(host, started, 4800);
    CHECK(started == std::vector<Ev>{on(0, 71, 90)});
    CHECK(host.owners(64) == (kOwnerProgression | kOwnerPreview));
    // [0.4, 1.4): the gate's offs at 0.9 leave 64 to the preview; the hit at 1.0 retriggers it.
    const std::vector<Ev> gated = host.run(0.4, 24000, true);
    CAPTURE(describe(gated));
    model.take(host, gated, 24000);
    CHECK(gated == std::vector<Ev>{off(12000, 60), off(12000, 67), on(14400, 60, 90), off(14400, 64),
                                   on(14400, 64, 90), on(14400, 67, 90)});
    CHECK(host.scheduler.pushPreview(previewStop(1)));
    const std::vector<Ev> stopped = host.run(1.4, 4800, true);
    model.take(host, stopped, 4800);
    CHECK(stopped == std::vector<Ev>{off(0, 71)});
    CHECK(host.owners(64) == kOwnerProgression);
    const std::vector<Ev> next = host.run(1.6, 24000, true);  // [1.6, 2.6): offs at 1.9, ons at 2.0
    CAPTURE(describe(next));
    model.take(host, next, 24000);
    CHECK(next == std::vector<Ev>{off(7200, 60), off(7200, 64), off(7200, 67), on(9600, 60, 90), on(9600, 64, 90),
                                  on(9600, 67, 90)});
    model.take(host, host.run(2.6, 512, false), 512);
    CHECK(model.silent());
    CHECK(model.ons == model.offs);
}

TEST_CASE("feel scheduler: a humanised, strummed performance is block-size invariant and drops no pair") {
    DocumentStore store;
    store.appendChord(chord(0, "maj7"), 4.0, 100);
    store.appendChord(chord(9, "m9"), 2.0, 90);
    store.appendChord(chord(5, "maj"), 1.5, 110);
    store.setFeel(Feel{Rhythm::Subdivided, Strum::DownUp, StrumSpeed::Loose, Humanise::Heavy});
    const Compiled compiled = compile(store.doc());
    REQUIRE_FALSE(compiled.feelLimited);
    REQUIRE(compiled.endBeat == 7.5);
    checkPartitionInvariance(snapshotOf(compiled), 16L * kSamplesPerBeat);

    // Odd block sizes with every block complete, every pair closed, nothing left sounding after a stop.
    Host host;
    host.adopt(snapshotOf(compiled));
    Model model;
    long position = 0;
    const int sizes[] = {37, 512, 1999, 64, 4096, 3};
    for (int block = 0; block < 400; ++block) {
        const int n = sizes[block % 6];
        model.take(host, host.run(static_cast<double>(position) / kSamplesPerBeat, n, true), n);
        REQUIRE(host.lastOk);
        position += n;
    }
    model.take(host, host.run(static_cast<double>(position) / kSamplesPerBeat, 512, false), 512);
    CHECK(model.silent());
    CHECK(model.ons == model.offs);
    CHECK(model.ons > 100);
}

TEST_CASE("feel scheduler: throughput at the density cap, 300 bpm, 44.1 kHz, 4096-frame blocks, shell buffer") {
    // Eight-note chords of half a beat from tick 600 in 7/8, Subdivided: hits at 0, 120 and 360 of every 480
    // ticks, so every beat holds exactly 96 events (the cap). 300 bpm at 44.1 kHz is 8820 samples per beat; a
    // 4096-frame block spans 0.464 beat, the supported maximum (D11). A palette preview of eight pitches (80..87,
    // apart from the progression's 60..67) starts every fifth block and stops two blocks later, so the
    // shell-sized output (512, ChordLab.h kMidiOutCapacity) also carries preview ons and releases. A second
    // pass with one-beat blocks (8820 frames, beyond the supported range) shows the cap holds per beat too.
    struct Pass {
        int frames;
        int blocks;
    };
    for (const Feel feel : {Feel{Rhythm::Subdivided}, Feel{Rhythm::Subdivided, Strum::DownUp, StrumSpeed::Loose}}) {
        for (const Pass pass : {Pass{4096, 2000}, Pass{8820, 900}}) {
            CAPTURE(static_cast<int>(feel.strum));
            CAPTURE(pass.frames);
            DocumentStore store;
            store.appendChord(Chord{0, &kCluster8}, 0.625, 100);
            for (int i = 0; i < 31; ++i) store.appendChord(Chord{0, &kCluster8}, 0.5, 100);
            store.setMeter(Meter{7, 8});
            store.setFeel(feel);
            const Performance performance = performDocument(store.doc());
            REQUIRE_FALSE(performance.feelLimited);
            REQUIRE(densestBeat(performance.events, performance.endTick()) == kMaxFeelEventsPerBeat);
            const Compiled compiled = compile(store.doc());
            REQUIRE(compiled.endBeat == 16.125);

            constexpr double kRate = 44100.0;
            constexpr double kTempo = 300.0;
            const double samplesPerBeat = kRate * 60.0 / kTempo;  // 8820
            Scheduler scheduler;
            scheduler.reset(kRate);
            scheduler.adopt(snapshotOf(compiled));
            std::array<MidiOut, 512> out{};  // ChordLab.h: kMidiOutCapacity
            bool sounding[128] = {};
            long progressionOns = 0;
            std::size_t maxProgression = 0;
            std::size_t maxWritten = 0;
            int incomplete = 0;
            int previewId = 0;
            for (int block = 0; block < pass.blocks; ++block) {
                if (block % 5 == 0) {
                    REQUIRE(scheduler.pushPreview(previewStart(++previewId, {80, 81, 82, 83, 84, 85, 86, 87})));
                } else if (block % 5 == 2) {
                    REQUIRE(scheduler.pushPreview(previewStop(previewId)));
                }
                const double ppq = static_cast<double>(block) * pass.frames / samplesPerBeat;
                std::size_t written = 0;
                const bool ok = scheduler.process(BlockInfo{ppq, pass.frames, kRate, kTempo, true, true}, out.data(),
                                                  out.size(), written);
                incomplete += ok ? 0 : 1;
                REQUIRE(written <= out.size());
                maxWritten = std::max(maxWritten, written);
                std::size_t progression = 0;
                for (std::size_t i = 0; i < written; ++i) {
                    const bool isOn = out[i].status == 0x90;
                    REQUIRE(sounding[out[i].data1] != isOn);  // no double on, no orphan off
                    sounding[out[i].data1] = isOn;
                    if (out[i].data1 < 80) {
                        ++progression;
                        progressionOns += isOn ? 1 : 0;
                    }
                }
                maxProgression = std::max(maxProgression, progression);
            }
            // Every progression on whose sample falls before the last block's end was played, once.
            const long total = static_cast<long>(pass.blocks) * pass.frames;
            long expectedOns = 0;
            for (long k = 0;; ++k) {
                bool any = false;
                for (const NoteEvent& e : compiled.events) {
                    const double beat = k * compiled.endBeat + e.beat;
                    if (e.on && std::floor(beat * samplesPerBeat + 0.5) < static_cast<double>(total)) {
                        ++expectedOns;
                        any = true;
                    }
                }
                if (!any) break;
            }
            MESSAGE("throughput, strum " << static_cast<int>(feel.strum) << ", " << pass.frames
                                         << "-frame blocks: worst block " << maxProgression
                                         << " progression events, " << maxWritten << " events in all (of 512), "
                                         << progressionOns << " progression ons");
            CHECK(incomplete == 0);  // nothing dropped, carried over or skipped
            CHECK(maxProgression <= static_cast<std::size_t>(kMaxFeelEventsPerBeat));
            CHECK(maxWritten <= out.size());
            CHECK(progressionOns == expectedOns);
            CHECK(previewId > 100);
        }
    }
}

// --- Thru notes (listening plan, Task 1) --------------------------------------------------------------

TEST_CASE("scheduler thru: an incoming note alone sounds at its offset and is released") {
    Host host;
    CHECK(host.pushThru(true, 64, 90, 100));
    CHECK(host.pushThru(false, 64, 0, 300));
    const std::vector<Ev> events = host.run(0.0, 512, false);
    CAPTURE(describe(events));
    CHECK(host.lastOk);
    CHECK(events == std::vector<Ev>{on(100, 64, 90), off(300, 64)});
    CHECK(host.owners(64) == 0);
    CHECK_FALSE(host.scheduler.sounding(64));
}

TEST_CASE("scheduler thru: a repeated attack retriggers, one release ends it") {
    Host host;
    CHECK(host.pushThru(true, 64, 90, 10));
    CHECK(host.pushThru(true, 64, 90, 20));
    CHECK(host.pushThru(false, 64, 0, 30));
    const std::vector<Ev> events = host.run(0.0, 512, false);
    CAPTURE(describe(events));
    CHECK(host.lastOk);
    CHECK(events == std::vector<Ev>{on(10, 64, 90), off(20, 64), on(20, 64, 90), off(30, 64)});
    CHECK(host.owners(64) == 0);
}

TEST_CASE("scheduler thru: over the progression it retriggers and gives the pitch back") {
    Host host;
    host.adopt(snapshotOf(cFour()));
    REQUIRE(host.run(0.0, 10, true) == std::vector<Ev>{on(0, 60), on(0, 64), on(0, 67)});
    CHECK(host.pushThru(true, 64, 80, 5));
    CHECK(host.pushThru(false, 64, 0, 8));
    const std::vector<Ev> events = host.run(10.0 / kSamplesPerBeat, 10, true);
    CAPTURE(describe(events));
    CHECK(host.lastOk);
    CHECK(events == std::vector<Ev>{off(5, 64), on(5, 64, 80)});
    CHECK(host.owners(64) == kOwnerProgression);
    CHECK(host.scheduler.sounding(64));
}

TEST_CASE("scheduler thru: over a preview it retriggers and the preview keeps the pitch") {
    Host host;
    CHECK(host.scheduler.pushPreview(previewStart(1, {64})));
    REQUIRE(previewBlock(host, 512) == std::vector<Ev>{on(0, 64, 90)});
    CHECK(host.pushThru(true, 64, 70, 5));
    CHECK(host.pushThru(false, 64, 0, 8));
    const std::vector<Ev> events = previewBlock(host, 512);
    CAPTURE(describe(events));
    CHECK(events == std::vector<Ev>{off(5, 64), on(5, 64, 70)});
    CHECK(host.owners(64) == kOwnerPreview);
    CHECK(host.scheduler.pushPreview(previewStop(1)));
    CHECK(previewBlock(host, 512) == std::vector<Ev>{off(0, 64)});
    CHECK(host.owners(64) == 0);
}

TEST_CASE("scheduler thru: the progression attacking a thru note retriggers it; the last owner releases it") {
    Host host;
    CHECK(host.pushThru(true, 60, 70, 0));
    REQUIRE(host.run(0.0, 512, false) == std::vector<Ev>{on(0, 60, 70)});
    host.adopt(snapshotOf(cFour(), false));  // one-shot: the progression's offs at beat 4 are its last word
    const std::vector<Ev> started = host.run(0.0, 512, true);
    CAPTURE(describe(started));
    CHECK(started == std::vector<Ev>{off(0, 60), on(0, 60), on(0, 64), on(0, 67)});
    CHECK(host.owners(60) == (kOwnerProgression | kOwnerThru));
    CHECK(host.pushThru(false, 60, 0, 3));
    CHECK(host.run(512.0 / kSamplesPerBeat, 512, true) == kNone);  // the progression still holds it
    CHECK(host.owners(60) == kOwnerProgression);
    const std::vector<Ev> released = host.run(1024.0 / kSamplesPerBeat, 4 * kSamplesPerBeat - 1024 + 16, true);
    CAPTURE(describe(released));
    CHECK(host.lastOk);
    const int at = 4 * kSamplesPerBeat - 1024;
    CHECK(released == std::vector<Ev>{off(at, 60), off(at, 64), off(at, 67)});
    CHECK(host.owners(60) == 0);
    CHECK_FALSE(host.scheduler.sounding(60));
}

TEST_CASE("scheduler thru: at one sample the thru note comes before the timeline, whose velocity wins") {
    Host host;
    host.adopt(snapshotOf(cFour()));
    CHECK(host.pushThru(true, 64, 80, 0));
    const std::vector<Ev> events = host.run(0.0, 512, true);
    CAPTURE(describe(events));
    CHECK(host.lastOk);
    CHECK(events == std::vector<Ev>{on(0, 64, 80), on(0, 60), off(0, 64), on(0, 64, 100), on(0, 67)});
    CHECK(host.owners(64) == (kOwnerProgression | kOwnerThru));
}

TEST_CASE("scheduler thru: switching thru off releases its notes and discards input until it is on again") {
    Host host;
    CHECK(host.pushThru(true, 60, 90, 0));
    CHECK(host.pushThru(true, 67, 90, 0));
    REQUIRE(host.run(0.0, 512, false) == std::vector<Ev>{on(0, 60, 90), on(0, 67, 90)});
    host.thru = false;
    const std::vector<Ev> edge = host.run(0.0, 512, false);
    CAPTURE(describe(edge));
    CHECK(host.lastOk);
    CHECK(edge == std::vector<Ev>{off(0, 60), off(0, 67)});
    CHECK(host.pushThru(true, 62, 90, 0));
    CHECK(host.run(0.0, 512, false) == kNone);  // queued input is discarded on every disabled block
    CHECK(host.lastOk);
    CHECK(host.owners(62) == 0);
    host.thru = true;
    CHECK(host.pushThru(true, 62, 90, 0));
    CHECK(host.run(0.0, 512, false) == std::vector<Ev>{on(0, 62, 90)});
    CHECK(host.lastOk);
}

TEST_CASE("scheduler thru: panic releases thru notes and forgets queued input") {
    Host host;
    CHECK(host.pushThru(true, 62, 90, 0));
    REQUIRE(host.run(0.0, 512, false) == std::vector<Ev>{on(0, 62, 90)});
    host.scheduler.panic();
    CHECK(host.run(0.0, 512, false) == std::vector<Ev>{off(0, 62)});
    CHECK(host.owners(62) == 0);
    CHECK(host.pushThru(true, 64, 90, 0));
    host.scheduler.panic();
    CHECK(host.run(0.0, 512, false) == kNone);  // pushed before the panic, never processed: gone
    CHECK(host.owners(64) == 0);
    CHECK(host.lastOk);
}

TEST_CASE("scheduler thru: a release without a thru attack is ignored, whoever else holds the pitch") {
    Host host;
    CHECK(host.pushThru(false, 64, 0, 0));
    CHECK(host.run(0.0, 512, false) == kNone);
    CHECK(host.lastOk);
    host.adopt(snapshotOf(cFour()));
    REQUIRE(host.run(0.0, 512, true) == std::vector<Ev>{on(0, 60), on(0, 64), on(0, 67)});
    CHECK(host.pushThru(false, 64, 0, 5));
    CHECK(host.run(512.0 / kSamplesPerBeat, 512, true) == kNone);
    CHECK(host.owners(64) == kOwnerProgression);
    CHECK(host.lastOk);
}

TEST_CASE("scheduler thru: the progression's flush, seek and adopt leave a thru note alone") {
    Host host;
    CHECK(host.pushThru(true, 64, 90, 0));
    REQUIRE(host.run(0.0, 512, false) == std::vector<Ev>{on(0, 64, 90)});
    host.adopt(snapshotOf(amFour()));
    REQUIRE(host.run(0.0, 512, true) == std::vector<Ev>{on(0, 69), on(0, 72), on(0, 76)});

    const std::vector<Ev> sought = host.run(2.0, 512, true);  // a seek: release all, then chase
    CAPTURE(describe(sought));
    CHECK(sought == std::vector<Ev>{off(0, 69), off(0, 72), off(0, 76), on(0, 69), on(0, 72), on(0, 76)});
    CHECK(host.owners(64) == kOwnerThru);
    CHECK(host.scheduler.sounding(64));

    host.adopt(snapshotOf(amFour(), true, 2));  // a new generation: flush and chase on the next block
    const std::vector<Ev> adopted = host.run(2.0 + 512.0 / kSamplesPerBeat, 512, true);
    CAPTURE(describe(adopted));
    CHECK(adopted == std::vector<Ev>{off(0, 69), off(0, 72), off(0, 76), on(0, 69), on(0, 72), on(0, 76)});
    CHECK(host.owners(64) == kOwnerThru);

    const std::vector<Ev> stopped = host.run(2.0 + 1024.0 / kSamplesPerBeat, 512, false);  // stop: release all
    CAPTURE(describe(stopped));
    CHECK(stopped == std::vector<Ev>{off(0, 69), off(0, 72), off(0, 76)});
    CHECK(host.owners(64) == kOwnerThru);
    CHECK(host.scheduler.sounding(64));
}

TEST_CASE("scheduler thru: more than a block's share is carried over in order and reported") {
    Host host;
    for (int i = 0; i < 80; ++i) {
        REQUIRE(host.pushThru(true, 36 + i, 90, i));
    }
    const std::vector<Ev> first = host.run(0.0, 128, false);
    std::vector<Ev> expected;
    for (int i = 0; i < 64; ++i) {
        expected.push_back(on(i, 36 + i, 90));
    }
    CHECK(first == expected);
    CHECK_FALSE(host.lastOk);  // late: part of the input waits for the next block

    const std::vector<Ev> second = host.run(0.0, 128, false);
    expected.clear();
    for (int pitch = 100; pitch <= 115; ++pitch) {
        expected.push_back(on(0, pitch, 90));  // overdue: at offset 0, in arrival order
    }
    CHECK(second == expected);
    CHECK(host.lastOk);

    // Nothing is left behind: a new note is all the next block plays. 40 is still held (pitch 36 + 4), so
    // the new attack retriggers it.
    CHECK(host.pushThru(true, 40, 90, 0));
    CHECK(host.run(0.0, 128, false) == std::vector<Ev>{off(0, 40), on(0, 40, 90)});
    CHECK(host.lastOk);
}

TEST_CASE("scheduler thru: an offset beyond a smaller block is clamped into it, never stalled") {
    Host host;
    CHECK(host.pushThru(true, 60, 90, 1000));
    CHECK(host.run(0.0, 64, false) == std::vector<Ev>{on(63, 60, 90)});
    CHECK(host.lastOk);
}

TEST_CASE("scheduler thru: an overflowing ring releases every thru note, discards the input and reports it") {
    Host host;
    CHECK(host.pushThru(true, 60, 90, 0));
    REQUIRE(host.run(0.0, 512, false) == std::vector<Ev>{on(0, 60, 90)});
    for (std::size_t i = 0; i < Scheduler::kThruQueueCapacity; ++i) {
        REQUIRE(host.pushThru(i % 2 == 0, 70, 90, 0));
    }
    CHECK_FALSE(host.pushThru(true, 71, 90, 0));  // full: refused, nothing queued
    const std::vector<Ev> events = host.run(0.0, 512, false);
    CAPTURE(describe(events));
    CHECK(events == std::vector<Ev>{off(0, 60)});
    CHECK_FALSE(host.lastOk);
    CHECK(host.owners(60) == 0);
    CHECK(host.owners(70) == 0);
    CHECK(host.run(0.0, 512, false) == kNone);  // the next block is clean
    CHECK(host.lastOk);
    CHECK(host.pushThru(true, 72, 90, 0));  // and the ring takes input again
    CHECK(host.run(0.0, 512, false) == std::vector<Ev>{on(0, 72, 90)});
}

TEST_CASE("scheduler thru: a pitch of 128 or more is ignored and reported as queued") {
    Host host;
    CHECK(host.pushThru(true, 200, 90, 0));
    CHECK(host.run(0.0, 512, false) == kNone);
    CHECK(host.lastOk);
}

TEST_CASE("scheduler thru: an attack pushed into the on -> off edge block is discarded; only the offs play") {
    Host host;
    CHECK(host.pushThru(true, 60, 90, 0));
    REQUIRE(host.run(0.0, 512, false) == std::vector<Ev>{on(0, 60, 90)});
    host.thru = false;
    CHECK(host.pushThru(true, 62, 90, 10));
    const std::vector<Ev> edge = host.run(0.0, 512, false);
    CAPTURE(describe(edge));
    CHECK(host.lastOk);
    CHECK(edge == std::vector<Ev>{off(0, 60)});
    CHECK(host.owners(60) == 0);
    CHECK(host.owners(62) == 0);
    CHECK_FALSE(host.scheduler.sounding(62));
}

TEST_CASE("scheduler thru: the edge's release-all at capacity 1 owes the rest to the next block") {
    Host host;
    CHECK(host.pushThru(true, 60, 90, 0));
    CHECK(host.pushThru(true, 67, 90, 0));
    REQUIRE(host.run(0.0, 512, false) == std::vector<Ev>{on(0, 60, 90), on(0, 67, 90)});
    host.thru = false;
    const std::vector<Ev> edge = host.run(0.0, 512, false, kBpm, 1);
    CAPTURE(describe(edge));
    CHECK(edge == std::vector<Ev>{off(0, 60)});
    CHECK_FALSE(host.lastOk);  // a release had to be owed
    CHECK(host.owners(67) == 0);
    CHECK(host.scheduler.sounding(67));
    const std::vector<Ev> paid = host.run(0.0, 512, false);
    CAPTURE(describe(paid));
    CHECK(paid == std::vector<Ev>{off(0, 67)});
    CHECK(host.lastOk);
    CHECK_FALSE(host.scheduler.sounding(67));
}

TEST_CASE("scheduler thru: an overflow while the progression co-owns a thru note leaves the note to it") {
    Host host;
    host.adopt(snapshotOf(cFour()));
    REQUIRE(host.run(0.0, 512, true) == std::vector<Ev>{on(0, 60), on(0, 64), on(0, 67)});
    CHECK(host.pushThru(true, 64, 80, 5));
    REQUIRE(host.run(512.0 / kSamplesPerBeat, 512, true) == std::vector<Ev>{off(5, 64), on(5, 64, 80)});
    REQUIRE(host.owners(64) == (kOwnerProgression | kOwnerThru));
    for (std::size_t i = 0; i < Scheduler::kThruQueueCapacity; ++i) {
        REQUIRE(host.pushThru(i % 2 == 0, 70, 90, 0));
    }
    CHECK_FALSE(host.pushThru(true, 71, 90, 0));
    const std::vector<Ev> events = host.run(1024.0 / kSamplesPerBeat, 512, true);
    CAPTURE(describe(events));
    CHECK(events == kNone);  // no physical off: the progression still holds 64
    CHECK_FALSE(host.lastOk);
    CHECK(host.owners(64) == kOwnerProgression);
    CHECK(host.scheduler.sounding(64));
}

TEST_CASE("scheduler thru: a block of unusable timing still delivers its thru notes, at offset 0") {
    Host host;
    CHECK(host.pushThru(true, 60, 90, 100));
    CHECK(host.pushThru(true, 62, 90, 300));
    const std::vector<Ev> events = host.run(0.0, 512, false, 0.0);  // bpm 0: unusable timing
    CAPTURE(describe(events));
    CHECK(events == std::vector<Ev>{on(0, 60, 90), on(0, 62, 90)});
    CHECK_FALSE(host.lastOk);  // the block is not the one the host asked for
    CHECK(host.owners(60) == kOwnerThru);
    CHECK(host.owners(62) == kOwnerThru);
}

TEST_CASE("scheduler thru: reset releases thru notes on the next block and forgets queued input") {
    Host host;
    CHECK(host.pushThru(true, 60, 90, 0));
    REQUIRE(host.run(0.0, 512, false) == std::vector<Ev>{on(0, 60, 90)});
    CHECK(host.pushThru(true, 64, 90, 0));  // queued before the reset, never processed
    host.scheduler.reset(kSr);
    const std::vector<Ev> events = host.run(0.0, 512, false);
    CAPTURE(describe(events));
    CHECK(events == std::vector<Ev>{off(0, 60)});
    CHECK(host.lastOk);
    CHECK(host.owners(60) == 0);
    CHECK(host.owners(64) == 0);
    CHECK_FALSE(host.scheduler.sounding(60));
    CHECK_FALSE(host.scheduler.sounding(64));

    // Input pushed after the reset plays in that same block, after the release-all.
    CHECK(host.pushThru(true, 62, 90, 0));
    REQUIRE(host.run(0.0, 512, false) == std::vector<Ev>{on(0, 62, 90)});
    host.scheduler.reset(kSr);
    CHECK(host.pushThru(true, 62, 70, 0));
    const std::vector<Ev> again = host.run(0.0, 512, false);
    CAPTURE(describe(again));
    CHECK(again == std::vector<Ev>{off(0, 62), on(0, 62, 70)});
    CHECK(host.lastOk);
    CHECK(host.owners(62) == kOwnerThru);
}

TEST_CASE("scheduler: a worst-case block fits the plug-in's output capacity") {
    // The densest progression (the throughput test's fixture above: eight-note chords at 96 events per beat,
    // 300 bpm, 44.1 kHz, 4096-frame blocks) with eight previews of eight pitches each already sounding (0..55
    // and 68..75, apart from the progression's 60..67). Then one block that is also a seek stops the eight
    // previews, starts eight new ones on the same pitches and carries 64 thru attacks on those pitches, each
    // a retrigger (off and on): 64 + 64 + 128 events on top of the progression's release-all, chase and
    // timeline. That block needs more than the old 256; ChordLab.h's kMidiOutCapacity (512) must carry every
    // block complete.
    constexpr std::size_t kCapacity = 512;  // ChordLab.h: kMidiOutCapacity
    constexpr double kRate = 44100.0;
    constexpr double kTempo = 300.0;
    constexpr int kFrames = 4096;
    constexpr double kDenseSamplesPerBeat = kRate * 60.0 / kTempo;  // 8820
    const int bases[8] = {0, 8, 16, 24, 32, 40, 48, 68};            // each preview's eight pitches from here
    auto pitchesFrom = [](int base) {
        std::vector<int> pitches;
        for (int i = 0; i < 8; ++i) pitches.push_back(base + i);
        return pitches;
    };
    for (const Feel feel : {Feel{Rhythm::Subdivided}, Feel{Rhythm::Subdivided, Strum::DownUp, StrumSpeed::Loose}}) {
        CAPTURE(static_cast<int>(feel.strum));
        DocumentStore store;
        store.appendChord(Chord{0, &kCluster8}, 0.625, 100);
        for (int i = 0; i < 31; ++i) store.appendChord(Chord{0, &kCluster8}, 0.5, 100);
        store.setMeter(Meter{7, 8});
        store.setFeel(feel);
        const Performance performance = performDocument(store.doc());
        REQUIRE(densestBeat(performance.events, performance.endTick()) == kMaxFeelEventsPerBeat);
        const Compiled compiled = compile(store.doc());

        Host host(kRate);
        host.adopt(snapshotOf(compiled));
        bool sounding[128] = {};
        std::size_t maxWritten = 0;
        int incomplete = 0;
        double ppq = 0.0;
        auto block = [&](double at) {
            const std::vector<Ev> events = host.run(at, kFrames, true, kTempo, kCapacity);
            incomplete += host.lastOk ? 0 : 1;
            maxWritten = std::max(maxWritten, host.lastWritten);
            for (std::size_t i = 0; i < events.size(); ++i) {
                const Ev& e = events[i];
                const bool retrigger = !e.on && i + 1 < events.size() && events[i + 1].on &&
                                       events[i + 1].pitch == e.pitch && events[i + 1].offset == e.offset;
                if (retrigger) {
                    REQUIRE(sounding[e.pitch]);
                    ++i;  // the on that follows keeps it sounding
                    continue;
                }
                REQUIRE(sounding[e.pitch] != e.on);  // no double on, no orphan off
                sounding[e.pitch] = e.on;
            }
            ppq = at + static_cast<double>(kFrames) / kDenseSamplesPerBeat;
        };
        for (int b = 0; b < 40; ++b) {
            block(ppq);
        }
        for (int id = 1; id <= 8; ++id) {
            REQUIRE(host.scheduler.pushPreview(previewStart(id, pitchesFrom(bases[id - 1]))));
        }
        block(ppq);  // the previews start sounding
        // The burst, all in the seek block: stop the eight, start eight new ones on the same pitches, and 64
        // thru attacks on those pitches.
        for (int id = 1; id <= 8; ++id) {
            REQUIRE(host.scheduler.pushPreview(previewStop(id)));
        }
        for (int id = 9; id <= 16; ++id) {
            REQUIRE(host.scheduler.pushPreview(previewStart(id, pitchesFrom(bases[id - 9]))));
        }
        for (std::size_t i = 0; i < Scheduler::kMaxThruPerBlock; ++i) {
            REQUIRE(host.pushThru(true, bases[i / 8] + static_cast<int>(i % 8), 90, 0));
        }
        block(ppq + 7.25);  // the seek
        const std::size_t burst = host.lastWritten;
        for (int b = 0; b < 40; ++b) {
            block(ppq);
        }
        MESSAGE("capacity budget, strum " << static_cast<int>(feel.strum) << ": seek block " << burst
                                          << " events, worst block " << maxWritten << " (of " << kCapacity << ")");
        CHECK(incomplete == 0);
        CHECK(maxWritten <= kCapacity);
        CHECK(maxWritten > 256);  // the old capacity could not carry it: why kMidiOutCapacity is 512
    }
}
