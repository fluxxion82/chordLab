#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/compiler.h"
#include "chordlab/document.h"
#include "chordlab/scheduler.h"

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

/// One C chord of four beats: pitches 48, 52, 55 on at 0, off at 4, endBeat 4.
const Compiled& cFour() {
    static const Compiled compiled = compile(documentOf({chord(0, "maj")}, 4.0, 100));
    return compiled;
}

/// C (48, 52, 55) for beats 0..4 then Am (57, 60, 64) for beats 4..8, endBeat 8.
const Compiled& cThenAm() {
    static const Compiled compiled = compile(documentOf({chord(0, "maj"), chord(9, "min")}, 4.0, 100));
    return compiled;
}

/// Am (57, 60, 64) alone for four beats.
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

/// Drives one scheduler block by block at 48 kHz.
struct Host {
    Scheduler scheduler;
    std::array<MidiOut, kBig> buffer{};
    std::size_t lastWritten = 0;
    bool lastOk = true;

    Host() {
        scheduler.reset(kSr);
    }

    std::vector<Ev> run(double ppqStart, int nFrames, bool playing, double bpm = kBpm, std::size_t capacity = kBig) {
        const BlockInfo info{ppqStart, nFrames, kSr, bpm, playing};
        lastOk = scheduler.process(info, buffer.data(), capacity, lastWritten);
        REQUIRE(lastWritten <= capacity);
        return decode(buffer.data(), lastWritten);
    }

    void adopt(const Snapshot& snapshot) {
        scheduler.adopt(snapshot);
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

const std::vector<Ev> kNone{};

}  // namespace

// --- 1. start ----------------------------------------------------------------------------------

TEST_CASE("scheduler: stopped -> playing at ppq 0 emits exactly three ons at offset 0") {
    Host host;
    host.adopt(snapshotOf(cFour()));

    const std::vector<Ev> events = host.run(0.0, 512, true);
    CAPTURE(describe(events));
    CHECK(host.lastOk);
    CHECK(events == std::vector<Ev>{on(0, 48), on(0, 52), on(0, 55)});
    CHECK(host.owners(48) == kOwnerProgression);
    CHECK(host.owners(52) == kOwnerProgression);
    CHECK(host.owners(55) == kOwnerProgression);
    CHECK(host.owners(60) == 0);
}

// --- 2. wrap inside a block ------------------------------------------------------------------

TEST_CASE("scheduler: block [3.9, 4.1) emits the offs at 2400 and the wrap's ons after them") {
    Host host;
    host.adopt(snapshotOf(cFour()));
    const std::vector<Ev> first = host.run(0.0, 93600, true);  // ends at 3.9
    CAPTURE(describe(first));
    CHECK(first == std::vector<Ev>{on(0, 48), on(0, 52), on(0, 55)});

    const std::vector<Ev> events = host.run(3.9, 4800, true);  // [3.9, 4.1)
    CAPTURE(describe(events));
    CHECK(host.lastOk);
    CHECK(events == std::vector<Ev>{off(2400, 48), off(2400, 52), off(2400, 55),  //
                                    on(2400, 48), on(2400, 52), on(2400, 55)});
}

// --- 3. stop -----------------------------------------------------------------------------------

TEST_CASE("scheduler: playing -> stopped releases with three offs at offset 0 and nothing else") {
    Host host;
    host.adopt(snapshotOf(cFour()));
    host.run(0.0, 512, true);

    const std::vector<Ev> events = host.run(0.5, 512, false);
    CAPTURE(describe(events));
    CHECK(events == std::vector<Ev>{off(0, 48), off(0, 52), off(0, 55)});
    CHECK(host.owners(48) == 0);

    // Staying stopped emits nothing more.
    CHECK(host.run(0.5, 512, false) == kNone);
}

// --- 4. host-loop wrap -------------------------------------------------------------------------

TEST_CASE("scheduler: host-loop wrap from 8.0 back to 2.0 releases then chases at offset 0") {
    Host host;
    host.adopt(snapshotOf(cFour()));
    const std::vector<Ev> first = host.run(0.0, 8 * kSamplesPerBeat, true);  // [0, 8): two passes
    CAPTURE(describe(first));
    CHECK(first == std::vector<Ev>{on(0, 48), on(0, 52), on(0, 55),  //
                                   off(96000, 48), off(96000, 52), off(96000, 55),  //
                                   on(96000, 48), on(96000, 52), on(96000, 55)});

    const std::vector<Ev> events = host.run(2.0, 512, true);
    CAPTURE(describe(events));
    CHECK(events == std::vector<Ev>{off(0, 48), off(0, 52), off(0, 55), on(0, 48), on(0, 52), on(0, 55)});
}

// --- 5. forward seek ---------------------------------------------------------------------------

TEST_CASE("scheduler: forward seek 1.0 -> 9.0 with C then Am chases C again") {
    Host host;
    host.adopt(snapshotOf(cThenAm()));
    host.run(0.0, kSamplesPerBeat, true);  // [0, 1): C sounding

    const std::vector<Ev> events = host.run(9.0, 512, true);  // 9 mod 8 = 1: still C
    CAPTURE(describe(events));
    CHECK(events == std::vector<Ev>{off(0, 48), off(0, 52), off(0, 55), on(0, 48), on(0, 52), on(0, 55)});

    // And a seek into the Am region picks Am.
    const std::vector<Ev> intoAm = host.run(13.5, 512, true);  // 13.5 mod 8 = 5.5
    CAPTURE(describe(intoAm));
    CHECK(intoAm == std::vector<Ev>{off(0, 48), off(0, 52), off(0, 55), on(0, 57), on(0, 60), on(0, 64)});
}

// --- 6. tempo ----------------------------------------------------------------------------------

TEST_CASE("scheduler: at 90 bpm offsets scale by 120/90 and the order is unchanged") {
    Host host;
    host.adopt(snapshotOf(cFour()));
    host.run(0.0, 124800, true, 90.0);  // 3.9 beats at 32000 samples per beat

    const std::vector<Ev> events = host.run(3.9, 6400, true, 90.0);  // [3.9, 4.1)
    CAPTURE(describe(events));
    CHECK(events == std::vector<Ev>{off(3200, 48), off(3200, 52), off(3200, 55),  //
                                    on(3200, 48), on(3200, 52), on(3200, 55)});
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
    CHECK(events == std::vector<Ev>{off(0, 48), off(0, 52), off(0, 55), on(0, 48)});
    CHECK(host.owners(48) == kOwnerProgression);
    CHECK(host.owners(52) == 0);
    CHECK(host.owners(55) == 0);

    // The dropped pitches are not sounding: stopping releases only 48.
    const std::vector<Ev> stop = host.run(2.0, 512, false);
    CAPTURE(describe(stop));
    CHECK(host.lastOk);
    CHECK(stop == std::vector<Ev>{off(0, 48)});
}

TEST_CASE("scheduler: capacity 0 drops the attacks, keeps nothing sounding and reports the drop") {
    Host host;
    host.adopt(snapshotOf(cFour()));
    const std::vector<Ev> events = host.run(0.0, 512, true, kBpm, 0);
    CHECK(events == kNone);
    CHECK_FALSE(host.lastOk);
    CHECK(host.owners(48) == 0);
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

/// Plays `compiled` (looped) from sample 0 to `totalSamples` in blocks of the given sizes (cycled, the
/// last one trimmed), with the host's ppq derived from the absolute sample position as a real host does.
std::vector<AbsEv> playPartitioned(const Compiled& compiled, long totalSamples, const std::vector<int>& sizes) {
    Host host;
    host.adopt(snapshotOf(compiled));
    std::vector<AbsEv> events;
    long position = 0;
    std::size_t next = 0;
    while (position < totalSamples) {
        int nFrames = sizes[next++ % sizes.size()];
        nFrames = static_cast<int>(std::min<long>(nFrames, totalSamples - position));
        const double ppq = static_cast<double>(position) * kBpm / (60.0 * kSr);
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

}  // namespace

TEST_CASE("scheduler: partition invariance over beats 0..16 for several block sizes") {
    const Compiled& compiled = cThenAm();  // endBeat 8: two passes in 16 beats
    const long total = 16L * kSamplesPerBeat;

    // The reference: every occurrence in [0, 16) at its exact sample.
    std::vector<AbsEv> expected;
    for (int k = 0; k < 2; ++k) {
        for (const NoteEvent& e : compiled.events) {
            const double beat = k * compiled.endBeat + e.beat;
            if (beat < 16.0) {
                expected.push_back(AbsEv{std::lround(beat * kSamplesPerBeat), e.on, e.pitch});
            }
        }
    }
    std::sort(expected.begin(), expected.end());
    REQUIRE(expected.size() == 21);  // 2 x 12 minus the three offs that sit exactly at 16

    const std::vector<std::vector<int>> schemes{
        {64}, {256}, {1024}, {100, 37, 500, 1, 999, 64, 2048, 3}, {7, 11, 13}, {kSamplesPerBeat * 3}};
    for (std::size_t s = 0; s < schemes.size(); ++s) {
        CAPTURE(s);
        const std::vector<AbsEv> actual = playPartitioned(compiled, total, schemes[s]);
        REQUIRE(actual.size() == expected.size());
        for (std::size_t i = 0; i < expected.size(); ++i) {
            CAPTURE(i);
            CHECK(actual[i] == expected[i]);
        }
    }
}

// --- 9. negative ppq ---------------------------------------------------------------------------

TEST_CASE("scheduler: negative ppq while playing emits nothing until ppq 0") {
    Host host;
    host.adopt(snapshotOf(cFour()));  // loop: positiveMod would say 3.5, but nothing sounds before 0

    const std::vector<Ev> before = host.run(-0.5, 4800, true);  // [-0.5, -0.3)
    CAPTURE(describe(before));
    CHECK(before == kNone);
    CHECK(host.owners(48) == 0);

    const std::vector<Ev> crossing = host.run(-0.3, 9600, true);  // [-0.3, 0.1)
    CAPTURE(describe(crossing));
    CHECK(crossing == std::vector<Ev>{on(7200, 48), on(7200, 52), on(7200, 55)});
}

TEST_CASE("scheduler: a seek to a negative ppq releases and chases nothing") {
    Host host;
    host.adopt(snapshotOf(cFour()));
    host.run(0.0, 512, true);
    const std::vector<Ev> events = host.run(-1.0, 512, true);
    CAPTURE(describe(events));
    CHECK(events == std::vector<Ev>{off(0, 48), off(0, 52), off(0, 55)});
}

// --- 10. one-shot ------------------------------------------------------------------------------

TEST_CASE("scheduler: loop=false emits nothing at or after endBeat and chases nothing past it") {
    SUBCASE("playing through the end") {
        Host host;
        host.adopt(snapshotOf(cThenAm(), /*loop=*/false));
        const std::vector<Ev> events = host.run(0.0, 8 * kSamplesPerBeat + 4800, true);  // [0, 8.2)
        CAPTURE(describe(events));
        CHECK(events == std::vector<Ev>{on(0, 48), on(0, 52), on(0, 55),  //
                                        off(96000, 48), off(96000, 52), off(96000, 55),  //
                                        on(96000, 57), on(96000, 60), on(96000, 64),  //
                                        off(192000, 57), off(192000, 60), off(192000, 64)});
        CHECK(host.run(8.2, 4800, true) == kNone);
        CHECK(host.run(8.4, 4 * kSamplesPerBeat, true) == kNone);  // no wrap at 16 either
        CHECK(host.owners(57) == 0);
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
        CHECK(events == std::vector<Ev>{on(0, 57), on(0, 60), on(0, 64)});
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
    host.run(0.0, 512, true);  // 48, 52, 55 sounding

    CHECK(host.scheduler.pushPreview(previewStart(7, {52, 59})));
    const std::vector<Ev> started = host.run(512.0 / kSamplesPerBeat, 512, true);
    CAPTURE(describe(started));
    CHECK(started == std::vector<Ev>{on(0, 59, 90)});
    CHECK(host.owners(52) == (kOwnerProgression | kOwnerPreview));

    CHECK(host.scheduler.pushPreview(previewStop(7)));
    const std::vector<Ev> stopped = host.run(1024.0 / kSamplesPerBeat, 512, true);
    CAPTURE(describe(stopped));
    CHECK(stopped == std::vector<Ev>{off(0, 59)});
    CHECK(host.owners(52) == kOwnerProgression);

    // The progression's own release of 52 is a physical off (the preview no longer holds it).
    const std::vector<Ev> offs = host.run(1536.0 / kSamplesPerBeat, 4 * kSamplesPerBeat, true);
    const int offset = 4 * kSamplesPerBeat - 1536;
    CAPTURE(describe(offs));
    CHECK(offs == std::vector<Ev>{off(offset, 48), off(offset, 52), off(offset, 55),  //
                                  on(offset, 48), on(offset, 52), on(offset, 55)});
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
    CHECK(events == std::vector<Ev>{off(0, 48), off(0, 52), off(0, 55), on(0, 57), on(0, 60), on(0, 64)});

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
    CHECK(events == std::vector<Ev>{off(0, 48), off(0, 52), off(0, 55)});
    CHECK(host.run(1024.0 / kSamplesPerBeat, 4 * kSamplesPerBeat, true) == kNone);
}

TEST_CASE("scheduler: reset keeps ownership and flushes on the next block") {
    Host host;
    host.adopt(snapshotOf(cFour()));
    host.run(0.0, 2 * kSamplesPerBeat, true);
    host.scheduler.reset(kSr);
    CHECK(host.owners(48) == kOwnerProgression);
    const std::vector<Ev> events = host.run(2.0, 512, true);
    CAPTURE(describe(events));
    CHECK(events == std::vector<Ev>{off(0, 48), off(0, 52), off(0, 55), on(0, 48), on(0, 52), on(0, 55)});
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
    host.adopt(snapshotOf(docs[0], loop, generation));

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
            }
            host.adopt(snapshotOf(docs[d], loop, generation));
        } else if (action < 25) {
            if (randInt(0, 1) == 0) {
                std::vector<int> pitches;
                const int count = randInt(1, 8);
                for (int i = 0; i < count; ++i) {
                    pitches.push_back(randInt(36, 84));
                }
                host.scheduler.pushPreview(previewStart(randInt(1, 4), pitches, randInt(1, 127)));
            } else {
                host.scheduler.pushPreview(previewStop(randInt(1, 4)));
            }
        } else if (action < 28) {
            host.scheduler.reset(kSr);
        } else if (action < 30) {
            bpm = bpms[randInt(0, 3)];
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

        // The owner bits are exactly the set of pitches that physically sound.
        int mismatches = 0;
        for (int pitch = 0; pitch < 128; ++pitch) {
            if ((host.owners(pitch) != 0) != sounding[pitch]) {
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
        CHECK(adopted == std::vector<Ev>{off(0, 48), off(0, 52), off(0, 55)});
        CHECK(host.lastOk);
        CHECK(host.run(1024.0 / kSamplesPerBeat, 2048, true) == kNone);
        CHECK(host.run(1e12, 2048, true) == kNone);  // a huge seek on top
        CHECK(host.run(1e12, 0, true) == kNone);
        CHECK(host.lastOk);
        CHECK(host.owners(60) == 0);
    }
    SUBCASE("bpm 1e-9: a block is a sliver of a beat, a seek to 1e12 only releases") {
        Host host;
        host.adopt(snapshotOf(cFour()));
        const std::vector<Ev> started = host.run(0.0, 512, true, 1e-9);
        CAPTURE(describe(started));
        CHECK(started == std::vector<Ev>{on(0, 48), on(0, 52), on(0, 55)});
        const std::vector<Ev> sought = host.run(1e12, 512, true, 1e-9);
        CAPTURE(describe(sought));
        CHECK(sought == std::vector<Ev>{off(0, 48), off(0, 52), off(0, 55)});
        CHECK(host.run(1e12, 512, true, 1e-9) == kNone);
        CHECK(host.lastOk);
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
            REQUIRE(host.run(0.0, 512, true) == std::vector<Ev>{on(0, 48), on(0, 52), on(0, 55)});

            const BlockInfo info{bad.ppq, bad.nFrames, bad.sampleRate, bad.bpm, true};
            std::size_t written = 0;
            const bool ok = host.scheduler.process(info, host.buffer.data(), kBig, written);
            const std::vector<Ev> events = decode(host.buffer.data(), written);
            CAPTURE(describe(events));
            CHECK(ok);
            CHECK(events == std::vector<Ev>{off(0, 48), off(0, 52), off(0, 55)});
            CHECK(host.owners(48) == 0);

            // The next good block starts afresh and chases.
            const std::vector<Ev> resumed = host.run(2.0, 512, true);
            CAPTURE(describe(resumed));
            CHECK(resumed == std::vector<Ev>{on(0, 48), on(0, 52), on(0, 55)});
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
        CHECK(skipped == std::vector<Ev>{off(0, 48), off(0, 52), off(0, 55)});
        CHECK(host.lastOk);
    }
}
