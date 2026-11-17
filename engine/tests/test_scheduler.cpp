#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/compiler.h"
#include "chordlab/document.h"
#include "chordlab/publication.h"
#include "chordlab/scheduler.h"
#include "chordlab/tracks.h"
#include "chordlab/triple_buffer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
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
    // kMaxSnapshotEvents / 2 notes a thousandth of a beat apart: the last ends before beat 6.2 (with 12288 events).
    std::vector<NoteEvent> big;
    for (std::size_t i = 0; i < Scheduler::kMaxSnapshotEvents / 2; ++i) {
        big.push_back({static_cast<double>(i) * 0.001, true, 60, 100});
        big.push_back({static_cast<double>(i) * 0.001 + 0.0005, false, 60, 0});
    }
    REQUIRE(big.size() == Scheduler::kMaxSnapshotEvents);
    REQUIRE(big.back().beat < 8.0);
    Host host;
    host.adopt(snapshotOf(big, 8.0));
    const std::vector<Ev> played = host.run(0.0, 512, true);  // 512 samples = 0.0213 beats: 22 notes
    CHECK(played.size() == 43);
    CHECK(host.lastOk);
#ifdef NDEBUG
    // One more event than the bound: adopted as an empty snapshot (a debug build asserts instead).
    big.push_back({8.0, false, 60, 0});
    host.adopt(snapshotOf(big, 8.0, true, 2));
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

TEST_CASE("scheduler thru: releaseThru releases thru notes on the next block") {
    Host host;
    CHECK(host.pushThru(true, 60, 90, 0));
    REQUIRE(host.run(0.0, 512, false) == std::vector<Ev>{on(0, 60, 90)});
    host.scheduler.releaseThru();
    const std::vector<Ev> events = host.run(0.0, 512, false);
    CAPTURE(describe(events));
    CHECK(events == std::vector<Ev>{off(0, 60)});
    CHECK(host.lastOk);
    CHECK(host.owners(60) == 0);
    CHECK_FALSE(host.scheduler.sounding(60));
}

TEST_CASE("scheduler thru: releaseThru keeps the queued input, which plays after the release-all") {
    Host host;
    CHECK(host.pushThru(true, 60, 90, 0));
    REQUIRE(host.run(0.0, 512, false) == std::vector<Ev>{on(0, 60, 90)});
    host.scheduler.releaseThru();
    CHECK(host.pushThru(true, 62, 80, 5));
    const std::vector<Ev> events = host.run(0.0, 512, false);
    CAPTURE(describe(events));
    CHECK(events == std::vector<Ev>{off(0, 60), on(5, 62, 80)});
    CHECK(host.lastOk);
    CHECK(host.owners(60) == 0);
    CHECK(host.owners(62) == kOwnerThru);
}

TEST_CASE("scheduler thru: releaseThru then an attack on the same pitch at offset 0 comes out off then on") {
    Host host;
    CHECK(host.pushThru(true, 60, 90, 0));
    REQUIRE(host.run(0.0, 512, false) == std::vector<Ev>{on(0, 60, 90)});
    host.scheduler.releaseThru();
    CHECK(host.pushThru(true, 60, 70, 0));
    const std::vector<Ev> events = host.run(0.0, 512, false);
    CAPTURE(describe(events));
    CHECK(events == std::vector<Ev>{off(0, 60), on(0, 60, 70)});
    CHECK(host.lastOk);
    CHECK(host.owners(60) == kOwnerThru);
}

TEST_CASE("scheduler thru: a pending release from releaseThru or reset survives a zero-frame block") {
    SUBCASE("releaseThru") {
        Host host;
        CHECK(host.pushThru(true, 60, 90, 0));
        REQUIRE(host.run(0.0, 512, false) == std::vector<Ev>{on(0, 60, 90)});
        host.scheduler.releaseThru();
        CHECK(host.run(0.0, 0, false) == kNone);  // a parameter flush
        CHECK(host.lastOk);
        CHECK(host.owners(60) == kOwnerThru);
        const std::vector<Ev> events = host.run(0.0, 512, false);
        CAPTURE(describe(events));
        CHECK(events == std::vector<Ev>{off(0, 60)});
        CHECK(host.lastOk);
        CHECK(host.owners(60) == 0);
    }
    SUBCASE("reset") {
        Host host;
        CHECK(host.pushThru(true, 60, 90, 0));
        REQUIRE(host.run(0.0, 512, false) == std::vector<Ev>{on(0, 60, 90)});
        host.scheduler.reset(kSr);
        CHECK(host.run(0.0, 0, false) == kNone);
        CHECK(host.lastOk);
        const std::vector<Ev> events = host.run(0.0, 512, false);
        CAPTURE(describe(events));
        CHECK(events == std::vector<Ev>{off(0, 60)});
        CHECK(host.lastOk);
        CHECK(host.owners(60) == 0);
    }
}

TEST_CASE("scheduler thru: releaseThru on a pitch the progression co-owns leaves the note to it") {
    Host host;
    host.adopt(snapshotOf(cFour()));
    REQUIRE(host.run(0.0, 512, true) == std::vector<Ev>{on(0, 60), on(0, 64), on(0, 67)});
    CHECK(host.pushThru(true, 60, 80, 5));
    REQUIRE(host.run(512.0 / kSamplesPerBeat, 512, true) == std::vector<Ev>{off(5, 60), on(5, 60, 80)});
    REQUIRE(host.owners(60) == (kOwnerProgression | kOwnerThru));
    host.scheduler.releaseThru();
    const std::vector<Ev> events = host.run(1024.0 / kSamplesPerBeat, 512, true);
    CAPTURE(describe(events));
    CHECK(events == kNone);  // no physical off: the progression still holds 60
    CHECK(host.lastOk);
    CHECK(host.owners(60) == kOwnerProgression);
    CHECK(host.scheduler.sounding(60));
}

TEST_CASE("scheduler thru: releaseThru with nothing held writes nothing") {
    Host host;
    host.scheduler.releaseThru();
    const std::vector<Ev> events = host.run(0.0, 512, false);
    CAPTURE(describe(events));
    CHECK(events == kNone);
    CHECK(host.lastOk);
}

TEST_CASE("scheduler thru: dropThru forgets the queued input and releases thru notes on the next block") {
    Host host;
    CHECK(host.pushThru(true, 60, 90, 0));
    REQUIRE(host.run(0.0, 512, false) == std::vector<Ev>{on(0, 60, 90)});
    CHECK(host.pushThru(true, 62, 80, 5));  // queued before the drop: its note-off may be among what was lost
    host.scheduler.dropThru();
    CHECK(host.pushThru(true, 64, 70, 9));  // queued after it: new input, plays after the release-all
    const std::vector<Ev> events = host.run(0.0, 512, false);
    CAPTURE(describe(events));
    CHECK(events == std::vector<Ev>{off(0, 60), on(9, 64, 70)});
    CHECK(host.lastOk);
    CHECK(host.owners(60) == 0);
    CHECK(host.owners(62) == 0);
    CHECK(host.owners(64) == kOwnerThru);
}

TEST_CASE("scheduler thru: dropThru survives an empty block and releases on the next one with frames") {
    Host host;
    CHECK(host.pushThru(true, 60, 90, 0));
    REQUIRE(host.run(0.0, 512, false) == std::vector<Ev>{on(0, 60, 90)});
    host.scheduler.dropThru();
    CHECK(host.run(0.0, 0, false).empty());
    const std::vector<Ev> events = host.run(0.0, 512, false);
    CAPTURE(describe(events));
    CHECK(events == std::vector<Ev>{off(0, 60)});
    CHECK_FALSE(host.scheduler.sounding(60));
}

TEST_CASE("scheduler thru: dropThru with a backlog beyond one block's share forgets all of it") {
    Host host;
    for (int i = 0; i < 100; ++i) {
        CHECK(host.pushThru(true, 20 + i, 90, 0));
    }
    host.scheduler.dropThru();
    const std::vector<Ev> events = host.run(0.0, 512, false);
    CAPTURE(describe(events));
    CHECK(events.empty());  // nothing was sounding, nothing queued survives, no release-all is written for silence
    CHECK(host.lastOk);     // and the block is complete: a forgotten backlog is not "left for later"
}

TEST_CASE("scheduler thru: dropThru leaves previews and the progression alone") {
    Host host;
    host.adopt(snapshotOf(cFour()));
    REQUIRE(host.run(0.0, 512, true) == std::vector<Ev>{on(0, 60), on(0, 64), on(0, 67)});
    CHECK(host.pushThru(true, 60, 80, 5));
    REQUIRE(host.run(512.0 / kSamplesPerBeat, 512, true) == std::vector<Ev>{off(5, 60), on(5, 60, 80)});
    REQUIRE(host.owners(60) == (kOwnerProgression | kOwnerThru));
    host.scheduler.dropThru();
    const std::vector<Ev> events = host.run(1024.0 / kSamplesPerBeat, 512, true);
    CAPTURE(describe(events));
    CHECK(events == kNone);  // no physical off: the progression still holds 60, and is not flushed
    CHECK(host.lastOk);
    CHECK(host.owners(60) == kOwnerProgression);
    CHECK(host.scheduler.sounding(60));
    CHECK(host.scheduler.sounding(64));
    CHECK(host.scheduler.sounding(67));
}

// --- Capacity at the track caps: adversarial blocks (tracks design D10, Task 8) ----------------------------

namespace {

/// The plug-in's output capacity (ChordLab.h: kMidiOutCapacity), which must not rise (the VST3 output queue would
/// reallocate on the audio thread), and the supported envelope's worst block.
constexpr std::size_t kOutCapacity = 512;
constexpr double kEnvRate = 44100.0;
constexpr double kEnvTempo = 300.0;
constexpr int kEnvFrames = 4096;
constexpr double kEnvSamplesPerBeat = kEnvRate * 60.0 / kEnvTempo;  // 8820
static_assert(kEnvFrames / kEnvSamplesPerBeat <= 0.47, "the envelope's block spans at most 0.47 beat");
// The two-wrap claim (a block sees at most 2M track starts): the shortest loop is one chord of the document's
// minimum length (kMinLengthBeats, a document invariant; the scheduler itself accepts any loop), and a block spanning
// at most two of them touches at most two wraps' worth of the loop's starts.
static_assert(kEnvFrames / kEnvSamplesPerBeat <= 2 * kMinLengthBeats,
              "the envelope's block spans at most two of the shortest loops");

/// The derivation's terms (the capacity test's comment), from the constants they rest on.
constexpr int kBoundSounding = 128;                                    // S0: one physical note per pitch
constexpr int kBoundPreviewOns = static_cast<int>(Scheduler::kMaxPreviews) * 8;  // eight previews of eight
constexpr int kBoundThruOns = static_cast<int>(Scheduler::kMaxThruPerBlock);
constexpr int kBoundChordVoices = 5;                                   // the dictionary's largest type
constexpr int kBoundArpOctaves = 2;                                    // ArpRange::Two (pinned by arpOctaves)
constexpr int kBoundChordPitches = kBoundArpOctaves * kBoundChordVoices;  // Cs: an arpeggio over two octaves
constexpr int kBoundChordOns = kMaxFeelEventsPerBeat / 2 + kBoundChordPitches;  // chase + timeline, chord lane
constexpr int kBoundTrackOns = kMaxTrackPolyphony + 2 * kMaxTrackOnsetsPerBeat;  // chase + timeline, tracks
constexpr int kPhysicalBound = kBoundSounding + kBoundPreviewOns + 2 * kBoundThruOns + 2 * kBoundChordOns +
                               2 * kBoundTrackOns;
static_assert(kPhysicalBound <= static_cast<int>(kOutCapacity),
              "the track caps must keep the derived worst block inside the plug-in's output capacity");
constexpr int kScratchBound = (kBoundChordPitches + kMaxTrackPolyphony) + kBoundThruOns +
                              2 * (kMaxFeelEventsPerBeat + 2 * kMaxTrackOnsetsPerBeat) + kBoundChordPitches +
                              kMaxTrackPolyphony;
// The mandatory events share the scratch: the progression's release-all (one at most) and up to two expiry
// candidates per preview are pushed before the chase and the timeline, the thru release-all before the thru notes,
// and every attack-side push stops at kMaxEventsPerBlock - kReservedReleaseEvents counting them.
constexpr int kScratchMandatoryFirst = 2 + 2 * static_cast<int>(Scheduler::kMaxPreviews);
static_assert(kScratchBound + kScratchMandatoryFirst <=
                  static_cast<int>(Scheduler::kMaxEventsPerBlock - Scheduler::kReservedReleaseEvents),
              "a block inside the envelope must never carry timeline events over");

/// The octaves an arpeggio spans, per `ArpRange`. No default: a new range would warn here (-Wswitch), and the
/// derivation's Cs assumes two octaves at most.
constexpr int arpOctaves(ArpRange range) {
    switch (range) {
        case ArpRange::One:
            return 1;
        case ArpRange::Two:
            return 2;
    }
    return 0;
}
static_assert(arpOctaves(ArpRange::One) <= kBoundArpOctaves && arpOctaves(ArpRange::Two) <= kBoundArpOctaves,
              "Cs assumes arpeggios over two octaves at most");

std::int64_t envSample(double beat, double anchor) {
    return static_cast<std::int64_t>(std::floor((beat - anchor) * kEnvSamplesPerBeat + 0.5));
}

/// The expected output of one block, written from scheduler.h's contract rather than from scheduler.cpp: one
/// physical note per pitch shared by three owners; the first owner's attack sounds, the progression or a thru note
/// attacking a sounding pitch retriggers (off, on), a preview joining one is silent; the last owner's release
/// sounds the off; owed offs are paid first. Order: owed offs, preview commands in arrival order, then by sample:
/// the discontinuity's releases (progression, then thru), the chase by pitch, the thru notes in arrival order, the
/// timeline in playback order.
struct Oracle {
    std::array<std::uint8_t, 128> owners{};
    std::array<bool, 128> owed{};
    std::vector<std::pair<int, std::vector<int>>> previews;  // active previews: id, held pitches
    std::vector<Ev> out;

    void attack(std::uint8_t owner, int offset, int pitch, int velocity) {
        std::uint8_t& o = owners[static_cast<std::size_t>(pitch)];
        if (o == 0) {
            if (owed[static_cast<std::size_t>(pitch)]) {
                out.push_back(off(offset, pitch));
                owed[static_cast<std::size_t>(pitch)] = false;
            }
            out.push_back(on(offset, pitch, velocity));
        } else if (owner != kOwnerPreview) {
            out.push_back(off(offset, pitch));
            out.push_back(on(offset, pitch, velocity));
        }
        o = static_cast<std::uint8_t>(o | owner);
    }

    void release(std::uint8_t owner, int offset, int pitch) {
        std::uint8_t& o = owners[static_cast<std::size_t>(pitch)];
        if ((o & owner) == 0) return;
        o = static_cast<std::uint8_t>(o & ~owner);
        if (o == 0) out.push_back(off(offset, pitch));
    }

    bool previewHolds(int pitch) const {
        for (const auto& p : previews) {
            if (std::find(p.second.begin(), p.second.end(), pitch) != p.second.end()) return true;
        }
        return false;
    }

    void stopPreview(int id) {
        for (auto it = previews.begin(); it != previews.end();) {
            if (it->first != id) {
                ++it;
                continue;
            }
            const std::vector<int> held = it->second;
            it = previews.erase(it);
            for (int pitch : held) {
                if (!previewHolds(pitch)) release(kOwnerPreview, 0, pitch);
            }
        }
    }

    void command(const PreviewCmd& cmd) {
        if (cmd.kind == PreviewCmd::Extend) return;
        stopPreview(cmd.id);
        if (cmd.kind != PreviewCmd::Start) return;
        std::vector<int> held;
        for (int i = 0; i < cmd.count; ++i) {
            const int pitch = cmd.pitches[i];
            if (pitch >= 128 || std::find(held.begin(), held.end(), pitch) != held.end()) continue;
            attack(kOwnerPreview, 0, pitch, std::clamp(cmd.velocity, 1, 127));
            held.push_back(pitch);
        }
        if (!held.empty()) previews.emplace_back(cmd.id, held);
    }
};

/// What one block does, as the oracle needs it.
struct BlockPlan {
    Snapshot snapshot;
    double anchor;
    std::int64_t blockSample;
    bool playing;
    bool flush;
    bool thruReleaseAll;
    std::vector<PreviewCmd> batch;
    std::vector<ThruEvent> thru;
};

std::size_t endIndexOf(const Snapshot& s) {
    for (std::size_t i = 0; i < s.count; ++i) {
        if (s.events[i].beat >= s.endBeat) return i;
    }
    return s.count;
}

/// Occurrence (k, i) of the looped timeline: wrap k's start plus the event's beat, the final offs on the next
/// wrap's start (scheduler.h: the final offs of one wrap precede the first attacks of the next).
double occurrenceBeat(const Snapshot& s, std::size_t endIndex, std::int64_t k, std::size_t i) {
    if (i >= endIndex) return static_cast<double>(k + 1) * s.endBeat;
    return static_cast<double>(k) * s.endBeat + s.events[i].beat;
}

std::vector<Ev> expectedBlock(Oracle oracle, const BlockPlan& plan) {
    enum Op { ReleaseAll, ThruReleaseAll, Attack, ThruOn, ThruOff, Release };
    struct Item {
        std::int64_t sample;
        int phase;
        Op op;
        int pitch;
        int velocity;
    };
    oracle.out.clear();
    for (int pitch = 0; pitch < 128; ++pitch) {
        if (oracle.owed[static_cast<std::size_t>(pitch)]) {
            oracle.out.push_back(off(0, pitch));
            oracle.owed[static_cast<std::size_t>(pitch)] = false;
        }
    }
    for (const PreviewCmd& cmd : plan.batch) oracle.command(cmd);

    std::vector<Item> items;
    const std::int64_t lo = plan.blockSample;
    const std::int64_t hi = plan.blockSample + kEnvFrames;
    if (plan.flush) items.push_back(Item{lo, 0, ReleaseAll, 0, 0});
    if (plan.thruReleaseAll) items.push_back(Item{lo, 0, ThruReleaseAll, 0, 0});
    const Snapshot& s = plan.snapshot;
    const std::size_t endIndex = endIndexOf(s);
    const double position = plan.anchor + static_cast<double>(lo) / kEnvSamplesPerBeat;
    if (plan.playing && plan.flush && s.count > 0 && position >= 0.0 && (s.loop || position < s.endBeat)) {
        // The chase: every note, paired per pitch in the wrap holding the position, whose on lies before the
        // block's first sample and whose off lies after it.
        const std::int64_t k = s.loop ? static_cast<std::int64_t>(std::floor(position / s.endBeat)) : 0;
        std::array<int, 128> open;
        open.fill(-1);
        std::array<int, 128> chased{};
        for (std::size_t i = 0; i < s.count; ++i) {
            const NoteEvent& e = s.events[i];
            if (e.on) {
                open[static_cast<std::size_t>(e.pitch)] = static_cast<int>(i);
                continue;
            }
            const int o = open[static_cast<std::size_t>(e.pitch)];
            REQUIRE(o >= 0);
            open[static_cast<std::size_t>(e.pitch)] = -1;
            if (envSample(occurrenceBeat(s, endIndex, k, static_cast<std::size_t>(o)), plan.anchor) < lo &&
                envSample(occurrenceBeat(s, endIndex, k, i), plan.anchor) > lo) {
                chased[static_cast<std::size_t>(e.pitch)] = s.events[static_cast<std::size_t>(o)].velocity;
            }
        }
        for (int pitch = 0; pitch < 128; ++pitch) {
            if (chased[static_cast<std::size_t>(pitch)] != 0) {
                items.push_back(Item{lo, 1, Attack, pitch, chased[static_cast<std::size_t>(pitch)]});
            }
        }
    }
    for (const ThruEvent& t : plan.thru) {
        const std::int64_t offset = std::clamp<std::int64_t>(t.sampleOffset, 0, kEnvFrames - 1);
        items.push_back(Item{lo + offset, 2, t.on ? ThruOn : ThruOff, t.pitch, t.velocity});
    }
    if (plan.playing && s.count > 0) {
        const double first = plan.anchor + static_cast<double>(lo) / kEnvSamplesPerBeat;
        const double last = plan.anchor + static_cast<double>(hi) / kEnvSamplesPerBeat;
        const std::int64_t wrapOfFirst = static_cast<std::int64_t>(std::floor(first / s.endBeat));
        const std::int64_t k0 = s.loop ? std::max<std::int64_t>(0, wrapOfFirst - 1) : 0;
        const std::int64_t k1 = s.loop ? static_cast<std::int64_t>(std::floor(last / s.endBeat)) + 1 : 0;
        for (std::int64_t k = k0; k <= k1; ++k) {
            for (std::size_t i = 0; i < s.count; ++i) {
                const std::int64_t sample = envSample(occurrenceBeat(s, endIndex, k, i), plan.anchor);
                const NoteEvent& e = s.events[i];
                if (sample < lo || sample >= hi || (!s.loop && e.on && i >= endIndex)) continue;
                items.push_back(Item{sample, 3, e.on ? Attack : Release, e.pitch, e.velocity});
            }
        }
    }
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return a.sample != b.sample ? a.sample < b.sample : a.phase < b.phase;
    });
    for (const Item& item : items) {
        const int offset = static_cast<int>(std::clamp<std::int64_t>(item.sample - lo, 0, kEnvFrames - 1));
        switch (item.op) {
            case ReleaseAll:
                for (int p = 0; p < 128; ++p) oracle.release(kOwnerProgression, offset, p);
                break;
            case ThruReleaseAll:
                for (int p = 0; p < 128; ++p) oracle.release(kOwnerThru, offset, p);
                break;
            case Attack:
                oracle.attack(kOwnerProgression, offset, item.pitch, item.velocity);
                break;
            case Release:
                oracle.release(kOwnerProgression, offset, item.pitch);
                break;
            case ThruOn:
                oracle.attack(kOwnerThru, offset, item.pitch, item.velocity);
                break;
            case ThruOff:
                oracle.release(kOwnerThru, offset, item.pitch);
                break;
        }
    }
    return oracle.out;
}

/// A document published the plug-in's way (`fillSlot` into a heap `SnapshotSlot`, adopted under the publication's
/// generation; two slots, so the array an `adopt` replaces stays untouched until then) and played in the envelope's
/// worst blocks (300 bpm, 44.1 kHz, 4096 frames) into the plug-in's output capacity. Every block must be complete,
/// and the physical stream must stay consistent per pitch (no double on, no orphan off) and agree with
/// `Scheduler::sounding`.
struct Rig {
    Host host{kEnvRate};
    std::array<std::unique_ptr<SnapshotSlot>, 2> slots{std::make_unique<SnapshotSlot>(),
                                                       std::make_unique<SnapshotSlot>()};
    int front = 1;
    Publication last;
    Snapshot snapshot{nullptr, 0, 0.0, true, 0};
    bool playing = false;
    double anchor = 0.0;
    std::int64_t blockSample = 0;
    bool flushPending = false;
    bool thruReleasePending = false;
    std::vector<PreviewCmd> batch;
    std::vector<ThruEvent> thru;
    std::vector<std::pair<int, std::vector<int>>> previews;  // as the oracle models them
    std::array<bool, 128> sounding{};                        // what the output so far implies
    std::size_t worst = 0;
    /// The lanes this output plays. Every lane (the default) publishes through `fillSlot`, as the plug-in does
    /// today; any other selection publishes `compilePlaybackFor(doc, selection)` into the slot the same way, under
    /// a new generation every time (so an `adopt` always flushes, even when the selected lanes did not change).
    LaneSelection selection;
    /// The last oracle block's (or overloaded block's) expected output, as an unlimited output would carry it.
    std::vector<Ev> lastExpected;
    /// Per pitch, the attacks the progression alone makes in the last oracle block (the chase and the timeline,
    /// from `expectedBlock` with no previews, thru notes or prior owners): which lanes the block actually plays.
    std::array<int, 128> progressionOns{};

    bool everyLane() const {
        return selection.mode == LaneSelection::Mode::AllExcept && selection.lanes.empty();
    }

    void publish(const Document& doc) {
        const int back = 1 - front;
        if (everyLane()) {
            REQUIRE(fillSlot(doc, *slots[static_cast<std::size_t>(back)], last));
        } else {
            SnapshotSlot& target = *slots[static_cast<std::size_t>(back)];
            Compiled compiled = compilePlaybackFor(doc, selection);
            REQUIRE(compiled.events.size() <= target.events.size());
            std::copy(compiled.events.begin(), compiled.events.end(), target.events.begin());
            target.count = compiled.events.size();
            target.endBeat = compiled.endBeat;
            target.loop = doc.progressionLoop;
            last.compiled = std::move(compiled);
            ++last.generation;
        }
        const SnapshotSlot& slot = *slots[static_cast<std::size_t>(back)];
        snapshot = Snapshot{slot.events.data(), slot.count, slot.endBeat, slot.loop, last.generation};
        REQUIRE(slot.count > 0);
        host.adopt(snapshot);
        front = back;
        flushPending = true;
    }

    void startPreview(int id, const std::vector<int>& pitches) {
        const PreviewCmd cmd = previewStart(id, pitches);
        REQUIRE(host.scheduler.pushPreview(cmd));
        batch.push_back(cmd);
    }

    void stopPreview(int id) {
        const PreviewCmd cmd = previewStop(id);
        REQUIRE(host.scheduler.pushPreview(cmd));
        batch.push_back(cmd);
    }

    void thruNote(bool isOn, int pitch, int offset, int velocity = 90) {
        REQUIRE(host.pushThru(isOn, pitch, velocity, offset));
        thru.push_back(ThruEvent{isOn, static_cast<std::uint8_t>(pitch), static_cast<std::uint8_t>(velocity), offset});
    }

    void releaseThru() {
        host.scheduler.releaseThru();
        thruReleasePending = true;
    }

    void panic() {
        host.scheduler.panic();
        previews.clear();
        thru.clear();
    }

    double clockPpq() const {
        return anchor + static_cast<double>(blockSample) / kEnvSamplesPerBeat;
    }

    int soundingCount() const {
        return static_cast<int>(std::count(sounding.begin(), sounding.end(), true));
    }

    /// One block: at `ppq` while playing (a seek when it differs from the clock, a start after a stop), or a
    /// stopped block. With `oracle`, the output must equal `expectedBlock`'s exactly, every event of it written,
    /// and satisfy the derivation's identity, written = 2 x ons + S0 - E. With `requireComplete` false, a block at
    /// the plug-in's capacity may be incomplete (the overload policy); its unlimited expectation is still computed.
    std::vector<Ev> block(std::optional<double> ppq, bool oracle, std::size_t capacity = kOutCapacity,
                          bool requireComplete = true) {
        const bool play = ppq.has_value();
        const double at = play ? *ppq : clockPpq();
        const bool discontinuous = play && (!playing || std::fabs(at - clockPpq()) > Scheduler::kSeekThresholdBeats);
        BlockPlan plan{snapshot, discontinuous ? at : anchor, discontinuous ? 0 : blockSample, play,
                       discontinuous || flushPending || (playing && !play), thruReleasePending, batch, thru};
        std::vector<Ev> expected;
        lastExpected.clear();
        progressionOns.fill(0);
        if (oracle || !requireComplete) {
            Oracle model;
            for (int p = 0; p < 128; ++p) {
                model.owners[static_cast<std::size_t>(p)] = host.owners(p);
                model.owed[static_cast<std::size_t>(p)] = host.scheduler.sounding(p) && host.owners(p) == 0;
            }
            model.previews = previews;
            expected = expectedBlock(model, plan);
            lastExpected = expected;
            const BlockPlan alone{plan.snapshot, plan.anchor, plan.blockSample, plan.playing, plan.flush, false, {}, {}};
            for (const Ev& e : expectedBlock(Oracle{}, alone)) {
                progressionOns[static_cast<std::size_t>(e.pitch)] += e.on ? 1 : 0;
            }
        }
        const int before = soundingCount();
        const std::vector<Ev> events = host.run(at, kEnvFrames, play, kEnvTempo, capacity);
        if (capacity == kOutCapacity && requireComplete) {
            CAPTURE(describe(events));
            REQUIRE(host.lastOk);
        }
        int ons = 0;
        for (std::size_t i = 0; i < events.size(); ++i) {
            const Ev& e = events[i];
            ons += e.on ? 1 : 0;
            const bool retrigger = !e.on && i + 1 < events.size() && events[i + 1].on &&
                                   events[i + 1].pitch == e.pitch && events[i + 1].offset == e.offset;
            if (retrigger) {
                REQUIRE(sounding[static_cast<std::size_t>(e.pitch)]);
                ++i;
                ++ons;
                continue;
            }
            REQUIRE(sounding[static_cast<std::size_t>(e.pitch)] != e.on);  // no double on, no orphan off
            sounding[static_cast<std::size_t>(e.pitch)] = e.on;
        }
        for (int p = 0; p < 128; ++p) {
            REQUIRE(sounding[static_cast<std::size_t>(p)] == host.scheduler.sounding(p));
        }
        // The identity the derivation rests on, on every block.
        CHECK(static_cast<int>(events.size()) == 2 * ons + before - soundingCount());
        if (oracle) {
            CAPTURE(describe(events));
            CAPTURE(describe(expected));
            CHECK(events == expected);
            CHECK(host.lastWritten == expected.size());
            CHECK(host.lastOk);
        }
        worst = std::max(worst, host.lastWritten);

        // The preview model follows the commands (an oracle block's model is exact; otherwise replay them).
        Oracle replay;
        replay.previews = previews;
        for (int p = 0; p < 128; ++p) replay.owners[static_cast<std::size_t>(p)] = host.owners(p);
        for (const PreviewCmd& cmd : batch) replay.command(cmd);
        previews = replay.previews;
        batch.clear();
        thru.clear();
        flushPending = false;
        thruReleasePending = false;
        if (play) {
            if (discontinuous) {
                anchor = at;
                blockSample = 0;
            }
            blockSample += kEnvFrames;
        } else {
            blockSample = 0;
        }
        playing = play;
        return events;
    }

    /// Ends everything and checks that no note hangs: every preview stopped, every thru note released, the
    /// transport stopped.
    void silence() {
        for (const auto& p : std::vector<std::pair<int, std::vector<int>>>(previews)) stopPreview(p.first);
        std::size_t queued = 0;
        for (int pitch = 0; pitch < 128; ++pitch) {
            if ((host.owners(pitch) & kOwnerThru) == 0) continue;
            if (queued == Scheduler::kMaxThruPerBlock) {
                block(std::nullopt, false);
                queued = 0;
            }
            thruNote(false, pitch, 0);
            ++queued;
        }
        block(std::nullopt, false);
        block(std::nullopt, false);
        for (int p = 0; p < 128; ++p) {
            CHECK_FALSE(host.scheduler.sounding(p));
            CHECK_FALSE(sounding[static_cast<std::size_t>(p)]);
        }
    }
};

/// The pitches of `doc`'s chord lane sounding anywhere in it (compile, the chords alone).
std::vector<int> chordPitches(const Document& doc) {
    std::vector<int> pitches;
    for (const NoteEvent& e : compile(doc).events) {
        if (std::find(pitches.begin(), pitches.end(), e.pitch) == pitches.end()) pitches.push_back(e.pitch);
    }
    std::sort(pitches.begin(), pitches.end());
    return pitches;
}

/// Pitches handed out in ascending order, each at most once, skipping the ones excluded.
struct PitchPool {
    std::array<bool, 128> used{};
    explicit PitchPool(const std::vector<int>& excluded = {}) {
        for (int p : excluded) used[static_cast<std::size_t>(p)] = true;
    }
    std::vector<int> take(int n) {
        std::vector<int> pitches;
        for (int p = 0; p < 128 && static_cast<int>(pitches.size()) < n; ++p) {
            if (used[static_cast<std::size_t>(p)]) continue;
            used[static_cast<std::size_t>(p)] = true;
            pitches.push_back(p);
        }
        REQUIRE(static_cast<int>(pitches.size()) == n);
        return pitches;
    }
    std::vector<int> takeAll() {
        return take(static_cast<int>(std::count(used.begin(), used.end(), false)));
    }
};

/// The densest chord lane (the throughput test's fixture): eight-note chords in 7/8 played `Subdivided`, 96 events
/// in some beat (the feel's cap), endBeat 16.125.
DocumentStore denseChords() {
    DocumentStore store;
    store.appendChord(Chord{0, &kCluster8}, 0.625, 100);
    for (int i = 0; i < 31; ++i) store.appendChord(Chord{0, &kCluster8}, 0.5, 100);
    store.setMeter(Meter{7, 8});
    store.setFeel(Feel{Rhythm::Subdivided});
    const Performance performance = performDocument(store.doc());
    REQUIRE_FALSE(performance.feelLimited);
    REQUIRE(densestBeat(performance.events, performance.endTick()) == kMaxFeelEventsPerBeat);
    return store;
}

/// Inserts `specs` into a new track of `store`, all of them (the playback limits included).
void addNotes(DocumentStore& store, const std::vector<NoteSpec>& specs) {
    const int track = store.addTrack(TrackRole::Other);
    REQUIRE(track != kNoTrackId);
    std::string refusal;
    REQUIRE_MESSAGE(store.insertNotes(track, specs, &refusal).size() == specs.size(), refusal);
}

/// Eight groups of eight pitches for the previews.
std::vector<std::vector<int>> previewGroups(const std::vector<int>& pitches) {
    REQUIRE(pitches.size() == 64);
    std::vector<std::vector<int>> groups(8);
    for (std::size_t i = 0; i < 64; ++i) groups[i / 8].push_back(pitches[i]);
    return groups;
}

/// The assumed workload in one block: the eight sounding previews (ids 1..8) replaced by eight new ones (9..16) on
/// the same pitches, and kMaxThruPerBlock incoming notes on `thruPitches` (cycled), spread over the block.
void workload(Rig& rig, const std::vector<std::vector<int>>& groups, const std::vector<int>& thruPitches) {
    for (int id = 1; id <= 8; ++id) rig.stopPreview(id);
    for (int id = 9; id <= 16; ++id) rig.startPreview(id, groups[static_cast<std::size_t>(id - 9)]);
    for (std::size_t i = 0; i < Scheduler::kMaxThruPerBlock; ++i) {
        rig.thruNote(true, thruPitches[i % thruPitches.size()], static_cast<int>(i) * 61);
    }
}

std::vector<int> joined(const std::vector<std::vector<int>>& parts) {
    std::vector<int> all;
    for (const auto& part : parts) all.insert(all.end(), part.begin(), part.end());
    return all;
}

/// Reports one adversarial block's written count and checks it against the derived bound and the capacity.
void logBlock(const std::string& name, std::size_t written) {
    MESSAGE("capacity block " << name << ": " << written << " events written (capacity " << kOutCapacity
                              << ", derived bound " << kPhysicalBound << ", caps " << kMaxTrackPolyphony << "/"
                              << kMaxTrackOnsetsPerBeat << ")");
    CHECK(written <= static_cast<std::size_t>(kPhysicalBound));
    CHECK(written <= kOutCapacity);
}

/// The most pitches sounding at once in `compiled` (sorted by beat, offs before ons at one beat).
int mostPitchesAtOnce(const Compiled& compiled) {
    std::array<int, 128> open{};
    int sounding = 0;
    int most = 0;
    for (const NoteEvent& e : compiled.events) {
        int& n = open[static_cast<std::size_t>(e.pitch)];
        if (e.on) {
            if (n++ == 0) ++sounding;
            most = std::max(most, sounding);
        } else if (n > 0 && --n == 0) {
            --sounding;
        }
    }
    return most;
}

/// Rows of track notes 30 ticks (kMinTrackNoteTicks) apart over `slots` voices: note j in row j / slots.
std::vector<NoteSpec> rows(const std::vector<int>& pitches, int slots, std::int64_t start, std::int64_t length,
                           std::int64_t lastRowLength = 0) {
    std::vector<NoteSpec> specs;
    const int count = static_cast<int>(pitches.size());
    const int lastRow = (count - 1) / slots;
    for (int j = 0; j < count; ++j) {
        const int row = j / slots;
        specs.push_back(NoteSpec{pitches[static_cast<std::size_t>(j)], start + kMinTrackNoteTicks * row,
                                 row == lastRow && lastRowLength > 0 ? lastRowLength : length, 100});
    }
    return specs;
}

}  // namespace

/// The joint physical-message bound of one `process` block (tracks design D10, Task 8): what the plug-in's output
/// capacity (ChordLab.h kMidiOutCapacity, 512, which must not rise: the VST3 output queue would reallocate on the
/// audio thread) must hold so that every block inside the envelope is complete. The constants above recompute it
/// from the caps (`kPhysicalBound`, a static_assert against 512), and the adversarial blocks (a) to (f) below build
/// the cases from real `compilePlayback` output and check every event against an independent model.
///
/// Envelope. At most 300 bpm, at least 44.1 kHz, at most 4096 frames: at least 8820 samples a beat, so a block spans
/// at most 0.4644 beat, under one. What the block plays is `compilePlayback`'s snapshot:
/// - Chord lane (`compile`): at most kMaxFeelEventsPerBeat (96) events in any 960-tick window of the looped timeline
///   (feel D11; `densestBeat` counts across the wrap; the block path, the over-budget fallback included, holds at
///   most four boundaries of 5 offs and 5 ons a beat), so at most 96 in one block however many wraps it crosses.
///   At most Cs = 10 chord-lane pitches sound at once: every performed note lies inside its own chord, a chord
///   plays its arpeggio note list, at most 5 voices (the dictionary's largest type) over at most two octaves
///   (`ArpRange` ends at Two, `arpOctaves`), and the notes of one pitch are disjoint; the capacity assumptions case
///   below pins the voices and measures the most chord-lane pitches sounding at once from `compile` over the
///   densest feels (it reaches 10). (The tests' eight-voice cluster, played in one octave, stays inside.)
/// - Tracks: `validateTracks` holds the active notes of all tracks, muted ones included, to N = kMaxTrackPolyphony
///   sounding and M = kMaxTrackOnsetsPerBeat starting in any 960-tick window; clipping at endBeat and the merge only
///   shorten, merge or drop notes, so the snapshot keeps both. The window does not wrap, so one block sees at most
///   2M track starts: M before the loop end and M after its start, or, on a loop shorter than the block, the loop's
///   M starts at most twice. That last case needs the block to span at most two of the shortest loops: one chord of
///   kMinLengthBeats (0.25 beat, which one window holds whole), a minimum the document's invariants set, not the
///   scheduler (a static_assert above pins 0.4644 <= 2 x kMinLengthBeats).
/// - The merge never creates an attack (a merged on sits at a chord-lane on or a track start) and a pitch sounding
///   after it sounds in the chord lane or in a track.
/// - Workload, assumed: a preview budget of pitches held at the block's end + 2 x pitches started and released
///   within the block <= 64 (eight 8-pitch previews, kMaxPreviews of 8); at most kMaxThruPerBlock (64) incoming
///   notes a block (enforced: the rest waits for the next block, which reports it). A burst of queued preview
///   commands beyond that budget (the queue drains up to 64 commands a block, each able to replace sounding
///   previews) is outside the bound: the overload policy handles it (releases first, attacks dropped and counted,
///   `process` false).
///
/// The identity. Physical notes are per pitch: an on only from silence, an off only from sounding, a retrigger an
/// off and an on of a sounding pitch (`Writer`, `attack`, `physicalOff`, `payOwed`; an owed pitch has no owner).
/// So per pitch, offs minus ons is sounding-at-start minus sounding-at-end, and for the block
///     written = 2 x ONS + S0 - E,
/// ONS the physical note-ons, S0 and E the pitches sounding at the block's start and end, S0 <= 128. Releases are
/// therefore bounded by sounding pitches, not owners: a pitch with three owners is released once, and every
/// release beyond S0 - E pays for an on already counted twice. Every block below checks the identity.
///
/// ONS, phase by phase in the order `process` runs them:
/// 1. Owed releases (`payOwed`, first): no ons; they are among S0.
/// 2. Preview commands (a preview joining a sounding pitch is silent). A pitch a preview acquires and holds to the
///    block's end costs one on beyond S0 - E (it is among E); one acquired and released within the block costs an on
///    and an off. So previews add at most the budget, 64, beyond S0 (a stop and a start on the same 64 pitches: 64
///    offs inside S0 - E, 64 ons).
/// 3. The discontinuity (a seek, a start, a stop, `reset`, an `adopt` of a new generation): the progression's
///    release-all; the thru release-all after `reset`, `releaseThru`, `dropThru`, a thru on -> off edge or an
///    overflow. No ons; their offs are among S0 - E.
/// 4. The chase: one on per pitch the progression sounds at the block's first sample, c0 + t0, with c0 <= Cs
///    chord-lane pitches and t0 <= N track notes.
/// 5. Thru: at most 64 events, so at most 64 ons (each a retrigger at worst).
/// 6. The timeline, chords and tracks after `compilePlayback`, across every wrap the block crosses (a wrap's final
///    offs are releases before the next wrap's attacks): x chord-lane ons and y <= 2M track starts; a retrigger is
///    one of these ons. Per pitch the chord lane alternates, so x minus its offs in the block is c_end - c0 (its
///    pitches sounding at the end and at the start) while x plus those offs is at most 96: c0 + x <= 48 + (c0 +
///    c_end) / 2 <= 48 + Cs = 58. With the chase, the tracks give t0 + y <= N + 2M.
/// 7. Preview expiries (timeline phase): releases only.
/// Hence
///     written <= [S0 + 2 x previewOns - E] + 2 x thruOns + 2 x (c0 + x) + 2 x (t0 + y)
///              <= (128 + 64) + 2 x 64 + 2 x 58 + 2 x (N + 2M) = 436 + 2N + 4M.
/// The provisional caps, N = 8 and M = 24, give 548 > 512, so they could not stand. Every point with 2N + 4M <= 76
/// fits. The onset rate is the binding term (one start costs four events through the wrap doubling), so the
/// polyphony stays at 8 and M = 15 gives exactly 128 + 64 + 128 + 116 + 76 = 512 (S0, previews beyond it, thru,
/// chord lane, tracks); N = 6, M = 16 is the other point on that frontier (8 and 16 give 516).
///
/// Where the worst sits. It needs a flush (the chase) that also releases every thru note (a `reset`, a bypass end
/// or a raw-input overflow at the same block as a seek or an adopt) with all 128 pitches sounding, the workload, and
/// a chord lane and tracks at their per-block maxima, every on a retrigger. Without a thru release-all a
/// start-sounding thru note ends only through a thru note-off (one of the 64 events), so S0 - E holds only the
/// previews' pitches, thru note-offs and the progression's own start pitches (at most Cs + N): 128 (previews) +
/// 128 (2 x thru ons + thru offs) + 2 x (58 + N + 2M) + (10 + N) = 382 + 3N + 4M (466). A continuous block has no
/// chase: x minus the chord lane's offs is c_end - c_start, so x <= 48 + Cs / 2 = 53, and the block writes at most
/// (128 + 64) + 128 + 2 x (53 + 2M) = 426 + 4M (486). An `adopt` is a flush in a continuous block, a seek's equal.
/// Recovery: owed pitches are among S0 and paid first, so a block after a panic or after an overloaded block obeys
/// the same bound; inside the envelope no block exceeds the capacity, so only a panic owes anything.
///
/// Scratch. The block's logical events fit `Scheduler::kMaxEventsPerBlock` without carrying any timeline over: the
/// chase at most Cs + N, thru 64, the timeline at most 2 x (96 + 2M) + Cs + N (merged ons, and per pitch at most as
/// many offs as ons plus the pitches sounding at the start): 18 + 64 + 270 = 352 (`kScratchBound`). The mandatory
/// events share the array and are pushed first: the progression's release-all and up to 2 x kMaxPreviews (16)
/// expiry candidates before the chase and the timeline, the thru release-all before the thru notes; every
/// attack-side push stops at kMaxEventsPerBlock - kReservedReleaseEvents (480) counting them. The condition is
/// therefore 352 + 2 + 16 = 370 <= 480 (`kScratchMandatoryFirst`, a static_assert). So inside the envelope no release
/// is owed, no attack dropped and nothing carried over: `process` returns true.
///
/// Measured at 8 / 15 (each block's MESSAGE): (a) seek to 127.9 with all 128 pitches sounding 380, with a thru
/// release-all 436 (the worst); (b) wrap block 372; (c) two wraps of a 0.25-beat loop 348; (d) seek onto a dense
/// chord boundary 326; (e) adopt during sustained notes 336; (f) after owed releases 360, after a panic 368. (At
/// 6 / 16 they wrote 380, 436, 374, 352, 326, 332, 360, 368; at 8 / 24, 412, 468, 408, 384, 344, 336, 360, 368: all
/// complete, but the bound, not the blocks built, decides.) The chord lanes here reach about 16 ons a block;
/// the bound allows 58, which is where the measured worst and the bound part.
///
/// Per output (per-track instruments). An instance plays `compilePlaybackFor(doc, selection)`: a follower its one
/// lane (`Only`), the leader every lane no follower took (`AllExcept`). A selection holds at most the one chord lane,
/// so its chord-lane terms are at most the ones above (96 events in any window, Cs = 10 pitches at once), and a
/// subset of the tracks, whose polyphony (N) and onset rate (M) `validateTracks` bounds jointly over all tracks,
/// muted or not, so any subset keeps both; the merge runs among the selected lanes only, and still only shortens,
/// merges or drops notes. Each instance has its own thru allowance (kMaxThruPerBlock of its own input) and its own
/// preview workload (a follower's is the stream forwarded to it, a subset of what one instance receives today), and
/// its own S0 <= 128. The scheduler is unchanged. So every term is at most the every-lane term, and the bound,
/// 436 + 2N + 4M = 512 at the caps, holds per instance with the same envelope: at most 300 bpm, at least 44.1 kHz,
/// at most 4096 frames (at least 8820 samples a beat, so a block spans at most 0.4644 beat), a preview budget of
/// pitches held at the block's end + 2 x pitches started and released within the block <= 64, and at most
/// kMaxThruPerBlock (64) incoming notes a block; a burst of preview commands beyond that budget is the overload
/// policy's, per instance as before. The per-output cases below rerun (a) to (f) for every output of each case's
/// document and of the same notes split over two tracks (each lane alone, every lane but one; for (f), whose block
/// plays no track note, the selections with the chords): every block complete and exact against the model, and the
/// adversarial block at most the every-lane one (measured at most: (a) 350 of 380, with a thru release-all 406 of
/// 436; (b) 342 of 372; (c) 320 of 348; (d) 318 of 326; (e) 328 of 336; (f) equal, 360 and 368). One more case
/// sends a follower at the caps a forwarded burst beyond the budget and pins the overload policy.
///
/// The test below is the burst modelled before tracks (no track notes): the densest progression with eight previews
/// already sounding, then a seek block that replaces them and retriggers 64 pitches by thru.
TEST_CASE("scheduler: a worst-case block fits the plug-in's output capacity") {
    // The burst modelled before tracks (no track notes), kept as a regression: the densest progression (the
    // throughput test's fixture: eight-note chords at 96 events per beat) with eight previews of eight pitches
    // already sounding (0..55 and 68..75, apart from the progression's 60..67); then one seek block stops them,
    // starts eight new ones on the same pitches and carries kMaxThruPerBlock thru attacks on those pitches, each
    // a retrigger. It peaks at 320, above the old capacity of 256.
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
        MESSAGE("capacity budget, strum " << static_cast<int>(feel.strum) << ": modelled burst (full per-block thru allowance, "
                << "one preview replacement, progression) " << burst << " events, worst block " << maxWritten
                << " (of " << kCapacity << "); queued preview runs beyond it take the incomplete-block path");
        CHECK(incomplete == 0);
        CHECK(maxWritten <= kCapacity);
        CHECK(maxWritten > 256);  // the old capacity could not carry it: why kMidiOutCapacity is 512
    }
}
TEST_CASE("scheduler capacity: the dictionary and the feel budget the derivation assumes") {
    // Cs: at most kBoundChordVoices voices per chord (one note per chord tone), so an arpeggio over two octaves
    // sounds at most kBoundChordPitches pitches at once.
    for (const ChordType& type : allChordTypes()) {
        CAPTURE(type.id);
        CHECK(static_cast<int>(type.intervals.size()) <= kBoundChordVoices);
    }
    // Cs measured from real `compile` output: ninths (five voices) in long and quarter-beat chords, so that a strum
    // or humanise acts across every boundary, under every rhythm, strum (loose) and humanise, and every arpeggio
    // pattern and rate over one and two octaves, in 4/4 and 7/8.
    DocumentStore ninths;
    const char* const types[] = {"9", "maj9", "m9", "9", "m9", "maj9"};
    const double lengths[] = {1.0, 0.25, 0.75, 2.0, 0.25, 1.5};
    for (int i = 0; i < 6; ++i) ninths.appendChord(Chord{(i * 5) % 12, &chordType(types[i])}, lengths[i], 100);
    int most = 0;
    for (const Meter meter : {Meter{4, 4}, Meter{7, 8}}) {
        ninths.setMeter(meter);
        for (const Rhythm rhythm : {Rhythm::Block, Rhythm::Pulse, Rhythm::Subdivided, Rhythm::Offbeats,
                                    Rhythm::LowThenUpper, Rhythm::Arpeggio}) {
            for (const Strum strum : {Strum::Off, Strum::Down, Strum::Up, Strum::DownUp}) {
                for (const Humanise humanise : {Humanise::Off, Humanise::Heavy}) {
                    const int patterns = rhythm == Rhythm::Arpeggio ? kArpPatternCount : 1;
                    for (int pattern = 0; pattern < patterns; ++pattern) {
                        for (const ArpRate rate : {ArpRate::Pulse, ArpRate::Subdivision, ArpRate::Half}) {
                            for (const ArpRange range : {ArpRange::One, ArpRange::Two}) {
                                ninths.setFeel(Feel{rhythm, strum, StrumSpeed::Loose, humanise,
                                                    static_cast<ArpPattern>(pattern), rate, range});
                                const Compiled compiled = compile(ninths.doc());
                                REQUIRE_FALSE(compiled.feelLimited);
                                most = std::max(most, mostPitchesAtOnce(compiled));
                            }
                        }
                    }
                }
            }
        }
    }
    MESSAGE("most chord-lane pitches sounding at once over the densest feels: " << most << " (Cs "
                                                                                << kBoundChordPitches << ")");
    // It reaches Cs: in 7/8 a heavily humanised Chord-pattern arpeggio at the Half rate over two octaves overlaps one
    // hit's five notes with the next hit's, an octave up.
    CHECK(most >= kBoundChordVoices);  // a ninth's five voices do sound together
    CHECK(most <= kBoundChordPitches);
    CHECK(kMaxFeelEventsPerBeat == 96);
    CHECK(kPhysicalBound == 436 + 2 * kMaxTrackPolyphony + 4 * kMaxTrackOnsetsPerBeat);
    MESSAGE("derived physical bound " << kPhysicalBound << " of " << kOutCapacity << " at caps "
                                      << kMaxTrackPolyphony << "/" << kMaxTrackOnsetsPerBeat << "; scratch bound "
                                      << kScratchBound);
}

namespace {

/// A lane selection named by document position (0 the chords, 1 + i track i), resolved once a scenario has built
/// its document: track ids exist only then.
struct LanePick {
    LaneSelection::Mode mode = LaneSelection::Mode::AllExcept;
    std::vector<int> lanes;
};

LanePick everyLane() {
    return LanePick{};
}

LaneKey laneKeyAt(const Document& doc, int lane) {
    return lane == 0 ? chordsLane() : trackLane(doc.tracks.at(static_cast<std::size_t>(lane - 1)).id);
}

LaneSelection resolve(const Document& doc, const LanePick& pick) {
    LaneSelection selection;
    selection.mode = pick.mode;
    for (int lane : pick.lanes) selection.lanes.push_back(laneKeyAt(doc, lane));
    return selection;
}

std::string nameOf(const LanePick& pick) {
    if (pick.mode == LaneSelection::Mode::AllExcept && pick.lanes.empty()) return "every lane";
    std::ostringstream os;
    os << (pick.mode == LaneSelection::Mode::Only ? "Only{" : "AllExcept{");
    for (std::size_t i = 0; i < pick.lanes.size(); ++i) {
        os << (i ? ", " : "");
        if (pick.lanes[i] == 0) {
            os << "chords";
        } else {
            os << "track " << pick.lanes[i];
        }
    }
    os << "}";
    return os.str();
}

/// The selections an output plays in a document of `tracks` tracks: each lane alone (`Only`, a follower's output
/// or a leader's chords alone) and every lane but one (`AllExcept`, the leader's output when that lane is followed
/// elsewhere). With `chordless` false, only the selections that include the chords lane.
std::vector<LanePick> outputPicks(int tracks, bool chordless) {
    std::vector<LanePick> picks;
    for (int lane = 0; lane <= tracks; ++lane) {
        if (chordless || lane == 0) picks.push_back(LanePick{LaneSelection::Mode::Only, {lane}});
        if (chordless || lane != 0) picks.push_back(LanePick{LaneSelection::Mode::AllExcept, {lane}});
    }
    return picks;
}

/// One adversarial block of a scenario as one output played it.
struct Measured {
    std::string name;
    std::size_t written;
    std::vector<Ev> events;
    std::array<int, 128> progressionOns;  // Rig::progressionOns of the block
};

/// A scenario run for one output: its adversarial blocks, the pitches of each lane of its document (0 the chords,
/// 1 + i track i), whether the output's selection includes each lane, and the pitches of the snapshot it played.
struct ScenarioRun {
    std::vector<Measured> blocks;
    std::vector<std::vector<int>> lanePitches;
    std::vector<bool> selected;
    std::vector<int> snapshotPitches;

    /// Whether `pitch` belongs to a lane the selection includes.
    bool plays(int pitch) const {
        for (std::size_t lane = 0; lane < lanePitches.size(); ++lane) {
            const std::vector<int>& pitches = lanePitches[lane];
            if (std::find(pitches.begin(), pitches.end(), pitch) != pitches.end()) return selected[lane];
        }
        return false;
    }
};

/// Points `rig` at the selection `pick` names in `doc` and records `doc`'s lanes for the checks.
void selectLanes(ScenarioRun& run, Rig& rig, const Document& doc, const LanePick& pick) {
    rig.selection = resolve(doc, pick);
    run.lanePitches.push_back(chordPitches(doc));
    for (const Track& track : doc.tracks) {
        std::vector<int> pitches;
        for (const TrackNote& note : track.notes) {
            if (std::find(pitches.begin(), pitches.end(), note.pitch) == pitches.end()) pitches.push_back(note.pitch);
        }
        std::sort(pitches.begin(), pitches.end());
        run.lanePitches.push_back(pitches);
    }
    for (int lane = 0; lane <= static_cast<int>(doc.tracks.size()); ++lane) {
        run.selected.push_back(rig.selection.includes(laneKeyAt(doc, lane)));
    }
}

void measure(ScenarioRun& run, const Rig& rig, const std::string& name, const std::vector<Ev>& events) {
    run.blocks.push_back(Measured{name, rig.host.lastWritten, events, rig.progressionOns});
}

/// Records the pitches of the snapshot `rig` plays.
void finish(ScenarioRun& run, const Rig& rig) {
    for (std::size_t i = 0; i < rig.snapshot.count; ++i) {
        const int pitch = rig.snapshot.events[i].pitch;
        if (std::find(run.snapshotPitches.begin(), run.snapshotPitches.end(), pitch) == run.snapshotPitches.end()) {
            run.snapshotPitches.push_back(pitch);
        }
    }
    std::sort(run.snapshotPitches.begin(), run.snapshotPitches.end());
}

/// Adds `parts` to `store` as tracks: all in one track (the capacity cases' own documents, notes in the order
/// given) or one track per part (the same notes, so the every-lane output is unchanged, as two lanes).
void addTracks(DocumentStore& store, const std::vector<std::vector<NoteSpec>>& parts, bool split) {
    if (split) {
        for (const std::vector<NoteSpec>& part : parts) addNotes(store, part);
        return;
    }
    std::vector<NoteSpec> all;
    for (const std::vector<NoteSpec>& part : parts) all.insert(all.end(), part.begin(), part.end());
    addNotes(store, all);
}

/// Case (a)'s scenario. 128 beats: sixteen quarter-beat clusters at each end, thirty four-beat clusters (another
/// root) between. Tracks at both caps around the wrap: s notes sustained through 127.9 into the end (chased,
/// clipped: final offs), M starts in [127.9, 128) on the other N - s voices, M starts in [0, 0.125); split, the
/// notes before the wrap are one track and the head another. The transport plays in the middle; eight previews
/// (64 pitches, among them the end's chord and track pitches) and thru notes on every other pitch make all 128
/// sound (without the chord lane, thru notes take the middle chord's pitches too). The seek block then replaces
/// the previews, retriggers 64 pitches by thru, releases the old chord, chases at 127.9, crosses the wrap and
/// plays the head.
ScenarioRun capacityA(const LanePick& pick, bool split, bool releaseThru) {
    constexpr int N = kMaxTrackPolyphony;
    constexpr int M = kMaxTrackOnsetsPerBeat;
    constexpr int tailVoices = (M + 3) / 4;  // four 30-tick starts fit the 96-tick tail per voice
    constexpr int sustained = N - tailVoices;
    static_assert(sustained >= 1, "a voice is left to sustain through the seek");
    ScenarioRun run;
    DocumentStore store;
    for (int i = 0; i < 16; ++i) store.appendChord(Chord{0, &kCluster8}, 0.25, 100);
    for (int i = 0; i < 30; ++i) store.appendChord(Chord{8, &kCluster8}, 4.0, 100);
    for (int i = 0; i < 16; ++i) store.appendChord(Chord{0, &kCluster8}, 0.25, 100);
    DocumentStore endOnly;
    endOnly.appendChord(Chord{0, &kCluster8}, 0.25, 100);
    DocumentStore middleOnly;
    middleOnly.appendChord(Chord{8, &kCluster8}, 4.0, 100);
    const std::vector<int> end = chordPitches(endOnly.doc());
    const std::vector<int> middle = chordPitches(middleOnly.doc());
    const std::vector<int> excluded = joined({end, middle});
    PitchPool pool(excluded);
    const std::vector<int> sustainPitches = pool.take(sustained);
    const std::vector<int> tailPitches = pool.take(M);
    const std::vector<int> headPitches = pool.take(M);
    const std::int64_t E = chordBoundaryTicks(store.doc()).back();
    REQUIRE(E == 128 * 960);
    std::vector<NoteSpec> beforeWrap;
    for (int p : sustainPitches) beforeWrap.push_back(NoteSpec{p, E - 96 - 1920, 1920 + 96 + 200, 100});
    for (const NoteSpec& n : rows(tailPitches, tailVoices, E - 96, kMinTrackNoteTicks)) beforeWrap.push_back(n);
    addTracks(store, {beforeWrap, rows(headPitches, N, 0, kMinTrackNoteTicks)}, split);
    REQUIRE(trackPolyphony(store.doc()) == N);
    REQUIRE(densestOnsetWindow(store.doc()) == M);

    std::vector<int> previewPitches = end;
    previewPitches.insert(previewPitches.end(), sustainPitches.begin(), sustainPitches.end());
    previewPitches.insert(previewPitches.end(), tailPitches.begin(), tailPitches.end());
    previewPitches.insert(previewPitches.end(), headPitches.begin(), headPitches.end());
    PitchPool rest(joined({previewPitches, middle}));
    const std::vector<int> filler = rest.take(64 - static_cast<int>(previewPitches.size()));
    previewPitches.insert(previewPitches.end(), filler.begin(), filler.end());
    const std::vector<std::vector<int>> groups = previewGroups(previewPitches);
    std::vector<int> thruFill = rest.takeAll();

    Rig rig;
    selectLanes(run, rig, store.doc(), pick);
    if (!rig.selection.includes(chordsLane())) thruFill.insert(thruFill.end(), middle.begin(), middle.end());
    rig.publish(store.doc());
    rig.block(60.0, false);
    rig.block(rig.clockPpq(), false);
    for (int id = 1; id <= 8; ++id) rig.startPreview(id, groups[static_cast<std::size_t>(id - 1)]);
    rig.block(rig.clockPpq(), false);
    for (std::size_t i = 0; i < thruFill.size(); ++i) rig.thruNote(true, thruFill[i], static_cast<int>(i));
    rig.block(rig.clockPpq(), false);
    REQUIRE(rig.soundingCount() == 128);

    workload(rig, groups, previewPitches);
    if (releaseThru) rig.releaseThru();
    const std::vector<Ev> events = rig.block(127.9, true);
    measure(run, rig, releaseThru ? "a: seek to 127.9 + releaseThru" : "a: seek to 127.9", events);
    for (int b = 0; b < 4; ++b) rig.block(rig.clockPpq(), false);
    rig.silence();
    finish(run, rig);
    return run;
}

/// Case (b)'s scenario. Codex's round-2 block (64 ons, 80 offs from 16/32) is 2M track starts and N + 2M track
/// ends in one wrap block: N notes sounding into the block end 30 ticks in, M starts follow on those voices before
/// the end (the last row long, clipped: final offs), M start right after the wrap and end inside the block (split:
/// the notes before the wrap one track, the head another). Over the densest chord lane, with the workload, in a
/// continuous block 0.2 beat before the end.
ScenarioRun capacityB(const LanePick& pick, bool split) {
    constexpr int N = kMaxTrackPolyphony;
    constexpr int M = kMaxTrackOnsetsPerBeat;
    ScenarioRun run;
    DocumentStore store = denseChords();
    const std::vector<int> chords = chordPitches(store.doc());
    const std::int64_t E = chordBoundaryTicks(store.doc()).back();
    const std::int64_t blockStart = E - 192;  // 0.2 beat before the wrap
    PitchPool pool(chords);
    const std::vector<int> oldPitches = pool.take(N);
    const std::vector<int> tailPitches = pool.take(M);
    const std::vector<int> headPitches = pool.take(M);
    std::vector<NoteSpec> beforeWrap;
    for (int p : oldPitches) beforeWrap.push_back(NoteSpec{p, blockStart - 1440, 1440 + 30, 100});
    for (const NoteSpec& n : rows(tailPitches, N, blockStart + 30, kMinTrackNoteTicks, 400)) beforeWrap.push_back(n);
    addTracks(store, {beforeWrap, rows(headPitches, N, 0, kMinTrackNoteTicks)}, split);
    REQUIRE(trackPolyphony(store.doc()) == N);
    REQUIRE(densestOnsetWindow(store.doc()) == M);
    // The previews take the free pitches (exactly 64 at 8/24), topped up from the chord's.
    const int available = 128 - static_cast<int>(chords.size()) - N - 2 * M;
    std::vector<int> groupPitches = pool.take(std::min(64, available));
    for (std::size_t i = 0; groupPitches.size() < 64; ++i) {
        REQUIRE(i < chords.size());
        groupPitches.push_back(chords[i]);
    }
    const std::vector<std::vector<int>> groups = previewGroups(groupPitches);

    Rig rig;
    selectLanes(run, rig, store.doc(), pick);
    rig.publish(store.doc());
    const double blockBeats = kEnvFrames / kEnvSamplesPerBeat;
    const double startBeat = static_cast<double>(blockStart) / 960.0 - 2.0 * blockBeats;
    for (int id = 1; id <= 8; ++id) rig.startPreview(id, groups[static_cast<std::size_t>(id - 1)]);
    rig.block(startBeat, false);
    rig.block(rig.clockPpq(), false);
    REQUIRE(std::fabs(rig.clockPpq() - static_cast<double>(blockStart) / 960.0) < 1e-9);
    workload(rig, groups, groupPitches);
    const std::vector<Ev> events = rig.block(rig.clockPpq(), true);
    measure(run, rig, "b: wrap block (2M on, N + 2M off)", events);
    for (int b = 0; b < 4; ++b) rig.block(rig.clockPpq(), false);
    rig.silence();
    finish(run, rig);
    return run;
}

/// Case (c)'s scenario. One quarter-beat cluster (the shortest loop) with M track starts on N voices inside it,
/// the last row held past the loop end (clipped: final offs at every wrap); split, the first half of the starts
/// one track and the rest another. A 0.464-beat block from 10.24 crosses the wraps at 10.25 and 10.5, so it
/// plays the loop's track starts twice (each selected start twice: 2M with every lane) and its chord twice, with
/// the workload.
ScenarioRun capacityC(const LanePick& pick, bool split) {
    constexpr int N = kMaxTrackPolyphony;
    constexpr int M = kMaxTrackOnsetsPerBeat;
    ScenarioRun run;
    DocumentStore store;
    store.appendChord(Chord{0, &kCluster8}, 0.25, 100);
    const std::vector<int> chords = chordPitches(store.doc());
    REQUIRE(chordBoundaryTicks(store.doc()).back() == 240);
    PitchPool pool(chords);
    const std::vector<int> trackPitches = pool.take(M);
    const std::vector<NoteSpec> starts = rows(trackPitches, N, 0, kMinTrackNoteTicks, 300);
    const auto half = starts.begin() + static_cast<std::ptrdiff_t>(starts.size() / 2);
    addTracks(store, {std::vector<NoteSpec>(starts.begin(), half), std::vector<NoteSpec>(half, starts.end())}, split);
    REQUIRE(trackPolyphony(store.doc()) == N);
    REQUIRE(densestOnsetWindow(store.doc()) == M);
    const std::vector<int> previewPitches = pool.take(64);
    const std::vector<std::vector<int>> groups = previewGroups(previewPitches);

    Rig rig;
    selectLanes(run, rig, store.doc(), pick);
    rig.publish(store.doc());
    for (int id = 1; id <= 8; ++id) rig.startPreview(id, groups[static_cast<std::size_t>(id - 1)]);
    rig.block(10.24 - kEnvFrames / kEnvSamplesPerBeat, false);
    REQUIRE(std::fabs(rig.clockPpq() - 10.24) < 1e-9);
    workload(rig, groups, previewPitches);
    const std::vector<Ev> events = rig.block(rig.clockPpq(), true);
    int trackOns = 0;
    int selectedStarts = 0;
    for (const Ev& e : events) {
        trackOns += e.on && std::find(trackPitches.begin(), trackPitches.end(), e.pitch) != trackPitches.end() ? 1 : 0;
    }
    for (int p : trackPitches) selectedStarts += run.plays(p) ? 1 : 0;
    CHECK(trackOns == 2 * selectedStarts);  // both wraps' starts in one block (2M with every lane)
    measure(run, rig, "c: two wraps of a 0.25-beat loop", events);
    for (int b = 0; b < 4; ++b) rig.block(rig.clockPpq(), false);
    rig.silence();
    finish(run, rig);
    return run;
}

/// Case (d)'s scenario. The densest chord lane; a seek exactly onto chord 16's start (8.125), with N / 2 track
/// notes sustained through it (chased) and M starts on the other voices right after it (split: the sustained
/// notes one track, the starts another). Eight previews hold the chord's and the tracks' pitches (so the chase and
/// the timeline retrigger them), thru notes every other pitch; the seek block replaces the previews and
/// retriggers 64 of their pitches by thru.
ScenarioRun capacityD(const LanePick& pick, bool split) {
    constexpr int N = kMaxTrackPolyphony;
    constexpr int M = kMaxTrackOnsetsPerBeat;
    constexpr int sustained = N / 2;
    ScenarioRun run;
    DocumentStore store = denseChords();
    const std::vector<int> chords = chordPitches(store.doc());
    const std::int64_t boundary = 7800;  // 0.625 + 15 x 0.5 = 8.125 beats
    REQUIRE(chordBoundaryTicks(store.doc())[16] == boundary);
    PitchPool pool(chords);
    const std::vector<int> sustainPitches = pool.take(sustained);
    const std::vector<int> startPitches = pool.take(M);
    std::vector<NoteSpec> held;
    for (int p : sustainPitches) held.push_back(NoteSpec{p, boundary - 1440, 1440 + 600, 100});
    addTracks(store, {held, rows(startPitches, N - sustained, boundary, kMinTrackNoteTicks)}, split);
    REQUIRE(trackPolyphony(store.doc()) == N);
    REQUIRE(densestOnsetWindow(store.doc()) == M);
    std::vector<int> previewPitches = joined({chords, sustainPitches, startPitches});
    const std::vector<int> filler = pool.take(64 - static_cast<int>(previewPitches.size()));
    previewPitches.insert(previewPitches.end(), filler.begin(), filler.end());
    const std::vector<std::vector<int>> groups = previewGroups(previewPitches);
    const std::vector<int> thruFill = pool.takeAll();

    Rig rig;
    selectLanes(run, rig, store.doc(), pick);
    rig.publish(store.doc());
    rig.block(2.0, false);
    for (int id = 1; id <= 8; ++id) rig.startPreview(id, groups[static_cast<std::size_t>(id - 1)]);
    rig.block(rig.clockPpq(), false);
    for (std::size_t i = 0; i < thruFill.size(); ++i) rig.thruNote(true, thruFill[i], static_cast<int>(i));
    rig.block(rig.clockPpq(), false);
    REQUIRE(rig.soundingCount() == 128);
    workload(rig, groups, previewPitches);
    const std::vector<Ev> events = rig.block(8.125, true);
    measure(run, rig, "d: seek onto a dense chord boundary", events);
    for (int b = 0; b < 4; ++b) rig.block(rig.clockPpq(), false);
    rig.silence();
    finish(run, rig);
    return run;
}

/// Case (e)'s scenario. N track notes held over beats 4..8 under the densest chord lane (split: the first half one
/// track, the rest another); a new generation (the first track's first note's velocity changed) adopted mid-note:
/// the block releases every progression pitch and chases the new snapshot at the same position, with the workload
/// on top. The track notes an output plays are released by the flush and chased again; the others never sound.
ScenarioRun capacityE(const LanePick& pick, bool split) {
    constexpr int N = kMaxTrackPolyphony;
    ScenarioRun run;
    DocumentStore store = denseChords();
    const std::vector<int> chords = chordPitches(store.doc());
    PitchPool pool(chords);
    const std::vector<int> sustainPitches = pool.take(N);
    std::vector<NoteSpec> specs;
    for (int p : sustainPitches) specs.push_back(NoteSpec{p, 4 * 960, 4 * 960, 100});
    std::vector<std::vector<NoteSpec>> parts{specs};
    if (split) {
        const auto half = specs.begin() + static_cast<std::ptrdiff_t>(specs.size() / 2);
        parts = {std::vector<NoteSpec>(specs.begin(), half), std::vector<NoteSpec>(half, specs.end())};
    }
    std::vector<int> trackIds;
    for (const std::vector<NoteSpec>& part : parts) {
        const int id = store.addTrack(TrackRole::Other);
        REQUIRE(store.insertNotes(id, part).size() == part.size());
        trackIds.push_back(id);
    }
    const int track = trackIds.front();
    REQUIRE(trackPolyphony(store.doc()) == N);
    const std::vector<int> previewPitches = pool.take(64);
    const std::vector<std::vector<int>> groups = previewGroups(previewPitches);
    const std::vector<int> thruFill = pool.takeAll();

    Rig rig;
    selectLanes(run, rig, store.doc(), pick);
    rig.publish(store.doc());
    rig.block(4.5, false);
    for (int id = 1; id <= 8; ++id) rig.startPreview(id, groups[static_cast<std::size_t>(id - 1)]);
    rig.block(rig.clockPpq(), false);
    for (std::size_t i = 0; i < thruFill.size(); ++i) rig.thruNote(true, thruFill[i], static_cast<int>(i));
    rig.block(rig.clockPpq(), false);
    for (int p : sustainPitches) REQUIRE(rig.host.owners(p) == (run.plays(p) ? kOwnerProgression : 0));
    const int firstNote = store.doc().tracks.at(0).notes.at(0).id;
    REQUIRE(store.updateNotes(track, {NoteEdit{firstNote, std::nullopt, std::nullopt, std::nullopt, 64}}));
    workload(rig, groups, previewPitches);
    rig.publish(store.doc());
    const std::vector<Ev> events = rig.block(rig.clockPpq(), true);
    for (int p : sustainPitches) {
        const bool plays = run.plays(p);
        CHECK(std::count(events.begin(), events.end(), off(0, p)) == (plays ? 1 : 0));  // released by the flush ...
        CHECK(rig.host.owners(p) == (plays ? kOwnerProgression : 0));                     // ... and chased again
    }
    measure(run, rig, "e: adopt during sustained track notes", events);
    for (int b = 0; b < 4; ++b) rig.block(rig.clockPpq(), false);
    rig.silence();
    finish(run, rig);
    return run;
}

/// Case (f)'s scenario. As (d) up to the burst. Then a block the host gives too little output (8 events) owes
/// most of its releases (a seek that also releases every thru note), or a panic owes every sounding pitch; either
/// way the next envelope block pays them first, then carries the workload over the densest chord lane, complete.
/// (The track notes sound only around 8.125, inside the starved block, whose attacks are dropped: the block after
/// it plays the chord lane alone, whatever the selection.)
ScenarioRun capacityF(const LanePick& pick, bool split, bool viaPanic) {
    constexpr int N = kMaxTrackPolyphony;
    constexpr int M = kMaxTrackOnsetsPerBeat;
    constexpr int sustained = N / 2;
    ScenarioRun run;
    DocumentStore store = denseChords();
    const std::vector<int> chords = chordPitches(store.doc());
    const std::int64_t boundary = 7800;
    PitchPool pool(chords);
    const std::vector<int> sustainPitches = pool.take(sustained);
    const std::vector<int> startPitches = pool.take(M);
    std::vector<NoteSpec> held;
    for (int p : sustainPitches) held.push_back(NoteSpec{p, boundary - 1440, 1440 + 600, 100});
    addTracks(store, {held, rows(startPitches, N - sustained, boundary, kMinTrackNoteTicks)}, split);
    std::vector<int> previewPitches = joined({chords, sustainPitches, startPitches});
    const std::vector<int> filler = pool.take(64 - static_cast<int>(previewPitches.size()));
    previewPitches.insert(previewPitches.end(), filler.begin(), filler.end());
    const std::vector<std::vector<int>> groups = previewGroups(previewPitches);
    const std::vector<int> thruFill = pool.takeAll();

    Rig rig;
    selectLanes(run, rig, store.doc(), pick);
    rig.publish(store.doc());
    rig.block(2.0, false);
    for (int id = 1; id <= 8; ++id) rig.startPreview(id, groups[static_cast<std::size_t>(id - 1)]);
    rig.block(rig.clockPpq(), false);
    for (std::size_t i = 0; i < thruFill.size(); ++i) rig.thruNote(true, thruFill[i], static_cast<int>(i));
    rig.block(rig.clockPpq(), false);
    REQUIRE(rig.soundingCount() == 128);
    if (viaPanic) {
        rig.panic();
        for (int id = 9; id <= 16; ++id) rig.startPreview(id, groups[static_cast<std::size_t>(id - 9)]);
        for (std::size_t i = 0; i < Scheduler::kMaxThruPerBlock; ++i) {
            rig.thruNote(true, previewPitches[i], static_cast<int>(i) * 61);
        }
    } else {
        rig.releaseThru();
        rig.block(8.0, false, 8);  // outside the envelope: too little output
        CHECK_FALSE(rig.host.lastOk);
        workload(rig, groups, previewPitches);
    }
    int owed = 0;
    for (int p = 0; p < 128; ++p) owed += rig.host.scheduler.sounding(p) && rig.host.owners(p) == 0 ? 1 : 0;
    CHECK(owed == (viaPanic ? 128 : static_cast<int>(thruFill.size()) - 8));  // the thru release-all past 8 slots
    const std::vector<Ev> events = rig.block(rig.clockPpq(), true);
    REQUIRE(events.size() >= static_cast<std::size_t>(owed));
    for (int i = 0; i < owed; ++i) CHECK_FALSE(events[static_cast<std::size_t>(i)].on);  // the owed offs first
    measure(run, rig, viaPanic ? "f: after a panic" : "f: after a block that owed releases", events);
    for (int b = 0; b < 4; ++b) rig.block(rig.clockPpq(), false);
    rig.silence();
    finish(run, rig);
    return run;
}

/// Pins one output's run of a scenario against the every-lane run of the same notes (the derivation's per-output
/// paragraph): the snapshot holds exactly the selected lanes; each adversarial block completed (the rig requires
/// `process` true and checks the oracle, the identity and the per-pitch stream on every block), writes at most
/// what the every-lane block wrote and at most the capacity; the progression's own attacks in it are the every-lane
/// block's on the selected lanes' pitches and none elsewhere, and there are some (the block really plays the
/// selection: the bound is not met by an empty output).
void checkOutput(const ScenarioRun& reference, const ScenarioRun& run) {
    REQUIRE(run.lanePitches.size() == run.selected.size());
    std::array<int, 128> laneOf;
    laneOf.fill(-1);
    for (std::size_t lane = 0; lane < run.lanePitches.size(); ++lane) {
        for (int p : run.lanePitches[lane]) {
            REQUIRE(laneOf[static_cast<std::size_t>(p)] == -1);  // the lanes share no pitch: no merge between them
            laneOf[static_cast<std::size_t>(p)] = static_cast<int>(lane);
        }
    }
    std::vector<int> selectedPitches;
    for (std::size_t lane = 0; lane < run.lanePitches.size(); ++lane) {
        if (run.selected[lane]) {
            selectedPitches.insert(selectedPitches.end(), run.lanePitches[lane].begin(), run.lanePitches[lane].end());
        }
    }
    std::sort(selectedPitches.begin(), selectedPitches.end());
    REQUIRE_FALSE(selectedPitches.empty());
    CHECK(run.snapshotPitches == selectedPitches);
    REQUIRE(run.blocks.size() == reference.blocks.size());
    for (std::size_t i = 0; i < run.blocks.size(); ++i) {
        const Measured& m = run.blocks[i];
        const Measured& r = reference.blocks[i];
        CAPTURE(m.name);
        CHECK(m.written <= r.written);
        CHECK(m.written <= kOutCapacity);
        CHECK(m.written <= static_cast<std::size_t>(kPhysicalBound));
        int selectedOns = 0;
        for (int p = 0; p < 128; ++p) {
            CAPTURE(p);
            const int lane = laneOf[static_cast<std::size_t>(p)];
            const int ons = m.progressionOns[static_cast<std::size_t>(p)];
            if (lane >= 0 && run.selected[static_cast<std::size_t>(lane)]) {
                CHECK(ons == r.progressionOns[static_cast<std::size_t>(p)]);
                selectedOns += ons;
            } else {
                CHECK(ons == 0);
            }
        }
        REQUIRE(selectedOns > 0);
    }
}

/// Runs `scenario(pick, split)` for every output of the case's document (one track: the chords, the track, and
/// every lane but one) and of the same notes split over two tracks (each of the three lanes alone, every lane but
/// one), pinning each against the every-lane run; the split's every-lane run must play exactly what the case's
/// does. `tracksPlay`: every lane plays in the adversarial blocks (else the tracks are silent there, and the
/// selections without the chord lane are left out, as they would hold vacuously). Reports the counts per output.
template <typename Scenario>
void perOutput(const std::string& label, Scenario scenario, bool tracksPlay) {
    const ScenarioRun reference = scenario(everyLane(), false);
    const ScenarioRun splitReference = scenario(everyLane(), true);
    REQUIRE(splitReference.blocks.size() == reference.blocks.size());
    REQUIRE(splitReference.lanePitches.size() == 3);
    for (std::size_t i = 0; i < reference.blocks.size(); ++i) {
        CAPTURE(reference.blocks[i].name);
        CHECK(splitReference.blocks[i].events == reference.blocks[i].events);
        for (std::size_t lane = 0; lane < splitReference.lanePitches.size(); ++lane) {
            CAPTURE(lane);
            int ons = 0;
            for (int p : splitReference.lanePitches[lane]) {
                ons += splitReference.blocks[i].progressionOns[static_cast<std::size_t>(p)];
            }
            if (tracksPlay || lane == 0) {
                CHECK(ons > 0);
            } else {
                CHECK(ons == 0);
            }
        }
    }
    std::ostringstream summary;
    summary << "capacity per output " << label << ", events written per adversarial block: every lane";
    for (const Measured& m : reference.blocks) summary << " " << m.written;
    for (const bool split : {false, true}) {
        for (const LanePick& pick : outputPicks(split ? 2 : 1, tracksPlay)) {
            const std::string name = nameOf(pick);
            CAPTURE(split);
            CAPTURE(name);
            const ScenarioRun run = scenario(pick, split);
            checkOutput(reference, run);
            summary << "; " << (split ? "two tracks " : "") << name;
            for (const Measured& m : run.blocks) summary << " " << m.written;
        }
    }
    MESSAGE(summary.str() << " (capacity " << kOutCapacity << ", derived bound " << kPhysicalBound << ")");
}

}  // namespace

TEST_CASE("scheduler capacity (a): a seek to 127.9 with 128 pitches sounding, the workload and tracks at the caps") {
    // capacityA, every lane.
    for (const bool releaseThru : {false, true}) {
        CAPTURE(releaseThru);
        const ScenarioRun run = capacityA(everyLane(), false, releaseThru);
        logBlock(run.blocks.at(0).name, run.blocks.at(0).written);
    }
}

TEST_CASE("scheduler capacity (b): a wrap block, the 64-on/80-off construction at the caps") {
    // capacityB, every lane.
    const ScenarioRun run = capacityB(everyLane(), false);
    logBlock(run.blocks.at(0).name, run.blocks.at(0).written);
}

TEST_CASE("scheduler capacity (c): a block across two wraps of a quarter-beat loop") {
    // capacityC, every lane.
    const ScenarioRun run = capacityC(everyLane(), false);
    logBlock(run.blocks.at(0).name, run.blocks.at(0).written);
}

TEST_CASE("scheduler capacity (d): a seek onto a dense chord boundary with eight 8-pitch previews") {
    // capacityD, every lane.
    const ScenarioRun run = capacityD(everyLane(), false);
    logBlock(run.blocks.at(0).name, run.blocks.at(0).written);
}

TEST_CASE("scheduler capacity (e): an adopt during sustained track notes flushes and re-chases") {
    // capacityE, every lane.
    const ScenarioRun run = capacityE(everyLane(), false);
    logBlock(run.blocks.at(0).name, run.blocks.at(0).written);
}

TEST_CASE("scheduler capacity (f): a block after owed releases pays them first and completes") {
    // capacityF, every lane.
    for (const bool viaPanic : {false, true}) {
        CAPTURE(viaPanic);
        const ScenarioRun run = capacityF(everyLane(), false, viaPanic);
        logBlock(run.blocks.at(0).name, run.blocks.at(0).written);
    }
}

// --- Capacity per output (per-track instruments M1, Task 2) ---------------------------------------------------

TEST_CASE("scheduler capacity per output (a): the seek to 127.9 for every selection of lanes") {
    for (const bool releaseThru : {false, true}) {
        CAPTURE(releaseThru);
        perOutput(releaseThru ? "a + releaseThru" : "a",
                  [&](const LanePick& pick, bool split) { return capacityA(pick, split, releaseThru); }, true);
    }
}

TEST_CASE("scheduler capacity per output (b): the wrap block for every selection of lanes") {
    perOutput("b", [](const LanePick& pick, bool split) { return capacityB(pick, split); }, true);
}

TEST_CASE("scheduler capacity per output (c): two wraps of a quarter-beat loop for every selection of lanes") {
    perOutput("c", [](const LanePick& pick, bool split) { return capacityC(pick, split); }, true);
}

TEST_CASE("scheduler capacity per output (d): the seek onto a dense chord boundary for every selection of lanes") {
    perOutput("d", [](const LanePick& pick, bool split) { return capacityD(pick, split); }, true);
}

TEST_CASE("scheduler capacity per output (e): the adopt during sustained track notes for every selection of lanes") {
    perOutput("e", [](const LanePick& pick, bool split) { return capacityE(pick, split); }, true);
}

TEST_CASE("scheduler capacity per output (f): the block after owed releases for every selection with the chords") {
    // The block after the starved one plays no track note (they all fall inside the starved block), so only the
    // selections that include the chord lane are non-vacuous here; perOutput checks the tracks are silent there.
    for (const bool viaPanic : {false, true}) {
        CAPTURE(viaPanic);
        perOutput(viaPanic ? "f after a panic" : "f after owed releases",
                  [&](const LanePick& pick, bool split) { return capacityF(pick, split, viaPanic); }, false);
    }
}

TEST_CASE("scheduler capacity per output: a preview burst beyond the budget on one track lane takes the overload policy") {
    // (b)'s document: a track at both caps over the densest chord lane, played by a follower (Only{track}) and,
    // for comparison, by every lane. Eight previews sound on 64 pitches no lane uses. Then, in the wrap block
    // (N track notes ending, 2M starting, the last row's final offs at the wrap), a burst of forwarded preview
    // commands far beyond the budget the derivation assumes (64 pitches in all): six restarts of all eight
    // previews, 48 commands, 384 pitches released and attacked again. The overload policy, and nothing worse:
    // `process` false; the output is the unlimited output with attacks removed and nothing else (a release goes
    // missing only with its own dropped retrigger or after its pitch's attack was dropped); nothing is owed or
    // left sounding without an owner; the following blocks are complete and exact again; nothing hangs.
    constexpr int N = kMaxTrackPolyphony;
    constexpr int M = kMaxTrackOnsetsPerBeat;
    constexpr int kRounds = 6;
    DocumentStore store = denseChords();
    const std::vector<int> chords = chordPitches(store.doc());
    const std::int64_t E = chordBoundaryTicks(store.doc()).back();
    const std::int64_t blockStart = E - 192;
    PitchPool pool(chords);
    const std::vector<int> oldPitches = pool.take(N);
    const std::vector<int> tailPitches = pool.take(M);
    const std::vector<int> headPitches = pool.take(M);
    std::vector<NoteSpec> specs;
    for (int p : oldPitches) specs.push_back(NoteSpec{p, blockStart - 1440, 1440 + 30, 100});
    for (const NoteSpec& n : rows(tailPitches, N, blockStart + 30, kMinTrackNoteTicks, 400)) specs.push_back(n);
    for (const NoteSpec& n : rows(headPitches, N, 0, kMinTrackNoteTicks)) specs.push_back(n);
    addNotes(store, specs);
    REQUIRE(trackPolyphony(store.doc()) == N);
    REQUIRE(densestOnsetWindow(store.doc()) == M);
    const std::vector<std::vector<int>> groups = previewGroups(pool.take(64));
    const std::vector<int> trackStarts = joined({tailPitches, headPitches});

    for (const bool follower : {true, false}) {
        CAPTURE(follower);
        Rig rig;
        if (follower) {
            rig.selection = LaneSelection{LaneSelection::Mode::Only, {trackLane(store.doc().tracks.at(0).id)}};
        }
        rig.publish(store.doc());
        const double blockBeats = kEnvFrames / kEnvSamplesPerBeat;
        for (int id = 1; id <= 8; ++id) rig.startPreview(id, groups[static_cast<std::size_t>(id - 1)]);
        rig.block(static_cast<double>(blockStart) / 960.0 - 2.0 * blockBeats, true);
        rig.block(rig.clockPpq(), true);
        REQUIRE(std::fabs(rig.clockPpq() - static_cast<double>(blockStart) / 960.0) < 1e-9);
        for (int p : oldPitches) REQUIRE(rig.host.owners(p) == kOwnerProgression);
        for (int round = 0; round < kRounds; ++round) {
            for (int id = 1; id <= 8; ++id) rig.startPreview(id, groups[static_cast<std::size_t>(id - 1)]);
        }

        const std::vector<Ev> events = rig.block(rig.clockPpq(), false, kOutCapacity, false);
        const std::vector<Ev> expected = rig.lastExpected;
        CHECK_FALSE(rig.host.lastOk);
        REQUIRE(expected.size() > kOutCapacity);  // beyond what any block could carry
        int trackOns = 0;
        for (const Ev& e : expected) {
            trackOns += e.on && std::find(trackStarts.begin(), trackStarts.end(), e.pitch) != trackStarts.end() ? 1 : 0;
        }
        CHECK(trackOns == 2 * M);  // the track's lane plays in the block
        // Walk the unlimited output: every event written is the next one of it, and every one skipped is an
        // attack, the off of a dropped retrigger, or a release of a pitch whose attack was dropped in this block.
        std::array<bool, 128> attackDropped{};
        std::size_t j = 0;
        int droppedOns = 0;
        for (std::size_t i = 0; i < expected.size(); ++i) {
            const Ev& e = expected[i];
            if (j < events.size() && events[j] == e) {
                ++j;
                continue;
            }
            if (e.on) {
                attackDropped[static_cast<std::size_t>(e.pitch)] = true;
                ++droppedOns;
                continue;
            }
            const bool retrigger = i + 1 < expected.size() && expected[i + 1].on && expected[i + 1].pitch == e.pitch &&
                                   expected[i + 1].offset == e.offset;
            CAPTURE(i);
            CAPTURE(e);
            CHECK((retrigger || attackDropped[static_cast<std::size_t>(e.pitch)]));  // no release is lost
        }
        CHECK(j == events.size());  // nothing written that the unlimited output does not carry
        CHECK(droppedOns > 0);
        CHECK(std::count_if(events.begin(), events.end(), [](const Ev& e) { return e.on; }) > 0);
        for (int p : oldPitches) {
            CAPTURE(p);
            CHECK(std::count_if(events.begin(), events.end(), [p](const Ev& e) { return !e.on && e.pitch == p; }) == 1);
        }
        // Nothing owed (the releases all fit) and nothing left sounding without an owner.
        CHECK(rig.host.lastWritten < kOutCapacity);
        for (int p = 0; p < 128; ++p) {
            CAPTURE(p);
            CHECK(rig.host.scheduler.sounding(p) == (rig.host.owners(p) != 0));
        }
        const std::string output = follower ? "Only{track}" : "every lane";
        MESSAGE("capacity per output, preview burst (" << output << "): "
                                                       << rig.host.lastWritten << " events written of "
                                                       << expected.size() << " unlimited, " << droppedOns
                                                       << " attacks dropped");
        // The previews as the scheduler kept them (each id owns its own eight pitches).
        rig.previews.clear();
        for (int id = 1; id <= 8; ++id) {
            std::vector<int> held;
            for (int p : groups[static_cast<std::size_t>(id - 1)]) {
                if ((rig.host.owners(p) & kOwnerPreview) != 0) held.push_back(p);
            }
            if (!held.empty()) rig.previews.emplace_back(id, held);
        }
        for (int b = 0; b < 4; ++b) rig.block(rig.clockPpq(), true);  // complete and exact again
        rig.silence();
    }
}
