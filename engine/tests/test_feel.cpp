#include "doctest.h"

#include "chordlab/feel.h"

#include "chordlab/chord.h"
#include "chordlab/compiler.h"
#include "chordlab/document.h"
#include "chordlab/limits.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <ostream>
#include <random>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

using namespace chordlab;

namespace {

using Ints = std::vector<int>;

// Checks that `hits` has exactly the given starts, ends, velocities, strong flags and notes (one entry each).
void expectHits(const std::vector<Hit>& hits, const Ints& starts, const Ints& ends, const Ints& velocities,
                const std::vector<Ints>& notes) {
    REQUIRE(hits.size() == starts.size());
    REQUIRE(starts.size() == ends.size());
    REQUIRE(starts.size() == velocities.size());
    REQUIRE(starts.size() == notes.size());
    for (size_t i = 0; i < hits.size(); ++i) {
        INFO("hit " << i);
        CHECK(hits[i].start == starts[i]);
        CHECK(hits[i].end == ends[i]);
        CHECK(hits[i].velocity == velocities[i]);
        CHECK(hits[i].notes == notes[i]);
    }
}

const Ints kAll3{0, 1, 2};

}  // namespace

TEST_CASE("feel: kMinNoteTicks") { CHECK(kMinNoteTicks == 5); }

TEST_CASE("rhythmHits: block is one full-length hit at the chord velocity") {
    auto h = rhythmHits(Rhythm::Block, {4, 4}, 0, 3840, 3, 100);
    expectHits(h, {0}, {3840}, {100}, {kAll3});
    CHECK(h[0].strong);
    // Block keeps the velocity exactly, and starts off-grid just the same.
    h = rhythmHits(Rhythm::Block, {4, 4}, 240, 1680, 4, 127);
    expectHits(h, {240}, {1680}, {127}, {{0, 1, 2, 3}});
}

TEST_CASE("rhythmHits: pulse 4/4") {
    auto h = rhythmHits(Rhythm::Pulse, {4, 4}, 0, 3840, 3, 100);
    expectHits(h, {0, 960, 1920, 2880}, {864, 1824, 2784, 3744}, {100, 90, 90, 90}, {kAll3, kAll3, kAll3, kAll3});
    CHECK(h[0].strong);
    CHECK(h[1].strong);
}

TEST_CASE("rhythmHits: pulse, chord starting mid-bar crosses a bar start") {
    // Chord start 1920 gets the full accent, 3840 is a bar start (full accent), the rest are plain pulses.
    auto h = rhythmHits(Rhythm::Pulse, {4, 4}, 1920, 5760, 3, 100);
    expectHits(h, {1920, 2880, 3840, 4800}, {2784, 3744, 4704, 5664}, {100, 90, 100, 90},
               {kAll3, kAll3, kAll3, kAll3});
}

TEST_CASE("rhythmHits: pulse, a chord whose start is a grid point is not duplicated") {
    auto h = rhythmHits(Rhythm::Pulse, {4, 4}, 960, 1920, 3, 100);
    expectHits(h, {960}, {1824}, {100}, {kAll3});
}

TEST_CASE("rhythmHits: pulse, off-grid chord start") {
    // 240 is not on the grid: it keeps the full accent; the next pulse is 960.
    auto h = rhythmHits(Rhythm::Pulse, {4, 4}, 240, 1680, 3, 100);
    expectHits(h, {240, 960}, {888, 1608}, {100, 90}, {kAll3, kAll3});
    CHECK(h[0].strong);  // the chord's own start counts as strong
    CHECK(h[1].strong);
}

TEST_CASE("rhythmHits: subdivided 4/4") {
    auto h = rhythmHits(Rhythm::Subdivided, {4, 4}, 0, 1920, 3, 100);
    expectHits(h, {0, 480, 960, 1440}, {432, 912, 1392, 1872}, {100, 80, 90, 80}, {kAll3, kAll3, kAll3, kAll3});
    CHECK(h[0].strong);
    CHECK_FALSE(h[1].strong);
    CHECK(h[2].strong);
    CHECK_FALSE(h[3].strong);
}

TEST_CASE("rhythmHits: subdivided 6/8") {
    auto h = rhythmHits(Rhythm::Subdivided, {6, 8}, 0, 2880, 3, 100);
    expectHits(h, {0, 480, 960, 1440, 1920, 2400}, {432, 912, 1392, 1872, 2352, 2832}, {100, 80, 80, 90, 80, 80},
               {kAll3, kAll3, kAll3, kAll3, kAll3, kAll3});
    CHECK(h[3].strong);
    CHECK_FALSE(h[2].strong);
}

TEST_CASE("rhythmHits: subdivided 7/8") {
    // Pulse 480, subdivision 240.
    auto h = rhythmHits(Rhythm::Subdivided, {7, 8}, 0, 960, 3, 100);
    expectHits(h, {0, 240, 480, 720}, {216, 456, 696, 936}, {100, 80, 90, 80}, {kAll3, kAll3, kAll3, kAll3});
}

TEST_CASE("rhythmHits: subdivided, the shortest chord is one gated hit") {
    auto h = rhythmHits(Rhythm::Subdivided, {4, 4}, 0, 240, 3, 100);
    expectHits(h, {0}, {216}, {100}, {kAll3});
}

TEST_CASE("rhythmHits: the gate never drops below kMinNoteTicks") {
    // Last hit at 480 with a gap of 5 to the chord's end: 5 * 9 / 10 = 4 is raised to 5.
    auto h = rhythmHits(Rhythm::Subdivided, {4, 4}, 0, 485, 3, 100);
    expectHits(h, {0, 480}, {432, 485}, {100, 80}, {kAll3, kAll3});
    // A gap of 6 gives 54 / 10 = 5 by itself.
    h = rhythmHits(Rhythm::Subdivided, {4, 4}, 0, 486, 3, 100);
    expectHits(h, {0, 480}, {432, 485}, {100, 80}, {kAll3, kAll3});
    // A gap of 11 gives 99 / 10 = 9, above the minimum.
    h = rhythmHits(Rhythm::Subdivided, {4, 4}, 0, 491, 3, 100);
    expectHits(h, {0, 480}, {432, 489}, {100, 80}, {kAll3, kAll3});
}

TEST_CASE("rhythmHits: offbeats 4/4") {
    // Start plus the off-pulse subdivisions 480 and 1440; gaps 480, 960, 480.
    auto h = rhythmHits(Rhythm::Offbeats, {4, 4}, 0, 1920, 3, 100);
    expectHits(h, {0, 480, 1440}, {432, 1344, 1872}, {100, 80, 80}, {kAll3, kAll3, kAll3});
    CHECK(h[0].strong);
    CHECK_FALSE(h[1].strong);
    CHECK_FALSE(h[2].strong);
}

TEST_CASE("rhythmHits: offbeats 6/8") {
    // Pulse 1440: off-pulse subdivisions 480, 960, 1920, 2400; gaps 480, 480, 960, 480, 480.
    auto h = rhythmHits(Rhythm::Offbeats, {6, 8}, 0, 2880, 3, 100);
    expectHits(h, {0, 480, 960, 1920, 2400}, {432, 912, 1824, 2352, 2832}, {100, 80, 80, 80, 80},
               {kAll3, kAll3, kAll3, kAll3, kAll3});
}

TEST_CASE("rhythmHits: offbeats, a chord starting on an offbeat is not duplicated") {
    auto h = rhythmHits(Rhythm::Offbeats, {4, 4}, 480, 1920, 3, 100);
    // 480 (start, full accent), then 1440; the pulse at 960 is skipped.
    expectHits(h, {480, 1440}, {1344, 1872}, {100, 80}, {kAll3, kAll3});
}

TEST_CASE("rhythmHits: low then upper 3/4") {
    auto h = rhythmHits(Rhythm::LowThenUpper, {3, 4}, 0, 5760, 3, 100);
    const Ints up{1, 2}, low{0};
    expectHits(h, {0, 960, 1920, 2880, 3840, 4800}, {864, 1824, 2784, 3744, 4704, 5664},
               {100, 90, 90, 100, 90, 90}, {low, up, up, low, up, up});
}

TEST_CASE("rhythmHits: low then upper, chord starting mid-bar") {
    // 960 is the chord's own start: the low note plays there; 2880 is the next bar start.
    auto h = rhythmHits(Rhythm::LowThenUpper, {3, 4}, 960, 3840, 3, 100);
    expectHits(h, {960, 1920, 2880}, {1824, 2784, 3744}, {100, 90, 100}, {{0}, {1, 2}, {0}});
}

TEST_CASE("rhythmHits: low then upper, off-grid chord start") {
    // 240 is the chord start (low, full accent); 3840 is a bar start (low).
    auto h = rhythmHits(Rhythm::LowThenUpper, {4, 4}, 240, 4000, 3, 100);
    expectHits(h, {240, 960, 1920, 2880, 3840}, {888, 1824, 2784, 3744, 3984}, {100, 90, 90, 90, 100},
               {{0}, {1, 2}, {1, 2}, {1, 2}, {0}});
}

TEST_CASE("rhythmHits: low then upper with two notes and one note") {
    auto h = rhythmHits(Rhythm::LowThenUpper, {3, 4}, 0, 2880, 2, 100);
    expectHits(h, {0, 960, 1920}, {864, 1824, 2784}, {100, 90, 90}, {{0}, {1}, {1}});
    h = rhythmHits(Rhythm::LowThenUpper, {3, 4}, 0, 2880, 1, 100);
    expectHits(h, {0, 960, 1920}, {864, 1824, 2784}, {100, 90, 90}, {{0}, {0}, {0}});
}

TEST_CASE("rhythmHits: arp up") {
    auto h = rhythmHits(Rhythm::ArpUp, {4, 4}, 0, 1920, 4, 100);
    expectHits(h, {0, 480, 960, 1440}, {432, 912, 1392, 1872}, {100, 80, 90, 80}, {{0}, {1}, {2}, {3}});
    h = rhythmHits(Rhythm::ArpUp, {4, 4}, 0, 3840, 3, 100);
    REQUIRE(h.size() == 8);
    const Ints expected{0, 1, 2, 0, 1, 2, 0, 1};
    for (size_t i = 0; i < h.size(); ++i) {
        INFO("hit " << i);
        CHECK(h[i].notes == Ints{expected[i]});
    }
}

TEST_CASE("rhythmHits: arp up counts from the chord's own first hit") {
    // Chord start 240 is hit 0; then 480, 960.
    auto h = rhythmHits(Rhythm::ArpUp, {4, 4}, 240, 1200, 3, 100);
    expectHits(h, {240, 480, 960}, {456, 912, 1176}, {100, 80, 90}, {{0}, {1}, {2}});
}

TEST_CASE("rhythmHits: arp up-down does not repeat its ends") {
    auto h = rhythmHits(Rhythm::ArpUpDown, {4, 4}, 0, 3840, 4, 100);
    REQUIRE(h.size() == 8);
    const Ints four{0, 1, 2, 3, 2, 1, 0, 1};
    for (size_t i = 0; i < h.size(); ++i) {
        INFO("hit " << i);
        CHECK(h[i].notes == Ints{four[i]});
    }
    h = rhythmHits(Rhythm::ArpUpDown, {4, 4}, 0, 3840, 5, 100);
    REQUIRE(h.size() == 8);
    const Ints five{0, 1, 2, 3, 4, 3, 2, 1};
    for (size_t i = 0; i < h.size(); ++i) {
        INFO("hit " << i);
        CHECK(h[i].notes == Ints{five[i]});
    }
}

TEST_CASE("rhythmHits: arp up-down with two notes, and one note always plays 0") {
    auto h = rhythmHits(Rhythm::ArpUpDown, {4, 4}, 0, 1920, 2, 100);
    expectHits(h, {0, 480, 960, 1440}, {432, 912, 1392, 1872}, {100, 80, 90, 80}, {{0}, {1}, {0}, {1}});
    h = rhythmHits(Rhythm::ArpUpDown, {4, 4}, 0, 1920, 1, 100);
    expectHits(h, {0, 480, 960, 1440}, {432, 912, 1392, 1872}, {100, 80, 90, 80}, {{0}, {0}, {0}, {0}});
    h = rhythmHits(Rhythm::ArpUp, {4, 4}, 0, 1920, 1, 100);
    expectHits(h, {0, 480, 960, 1440}, {432, 912, 1392, 1872}, {100, 80, 90, 80}, {{0}, {0}, {0}, {0}});
}

TEST_CASE("rhythmHits: accents round half up and clamp to 1..127") {
    // v 127: pulse (127*9+5)/10 = 114, off-pulse (127*8+5)/10 = 102.
    auto h = rhythmHits(Rhythm::Subdivided, {4, 4}, 0, 1920, 3, 127);
    expectHits(h, {0, 480, 960, 1440}, {432, 912, 1392, 1872}, {127, 102, 114, 102}, {kAll3, kAll3, kAll3, kAll3});
    // v 1: (9+5)/10 = 1, (8+5)/10 = 1.
    h = rhythmHits(Rhythm::Subdivided, {4, 4}, 0, 1920, 3, 1);
    expectHits(h, {0, 480, 960, 1440}, {432, 912, 1392, 1872}, {1, 1, 1, 1}, {kAll3, kAll3, kAll3, kAll3});
    // v 5: pulse (45+5)/10 = 5 (4.5 rounds up), off-pulse (40+5)/10 = 4.
    h = rhythmHits(Rhythm::Subdivided, {4, 4}, 0, 1920, 3, 5);
    expectHits(h, {0, 480, 960, 1440}, {432, 912, 1392, 1872}, {5, 4, 5, 4}, {kAll3, kAll3, kAll3, kAll3});
    // v 15: pulse (135+5)/10 = 14 (13.5 rounds up), off-pulse (120+5)/10 = 12 (12.5 rounds up).
    h = rhythmHits(Rhythm::Subdivided, {4, 4}, 0, 1920, 3, 15);
    expectHits(h, {0, 480, 960, 1440}, {432, 912, 1392, 1872}, {15, 12, 14, 12}, {kAll3, kAll3, kAll3, kAll3});
    // An out-of-range chord velocity is clamped on the accented hits too.
    h = rhythmHits(Rhythm::Pulse, {4, 4}, 0, 1920, 3, 0);
    expectHits(h, {0, 960}, {864, 1824}, {1, 1}, {kAll3, kAll3});
}

TEST_CASE("rhythmHits: empty spans and note counts give no hits") {
    CHECK(rhythmHits(Rhythm::Pulse, {4, 4}, 960, 960, 3, 100).empty());
    CHECK(rhythmHits(Rhythm::Pulse, {4, 4}, 960, 480, 3, 100).empty());
    CHECK(rhythmHits(Rhythm::Pulse, {4, 4}, 0, 3840, 0, 100).empty());
}

// --- Task 4: hash, strum, humanise, safety, budgets ---------------------------------------------------------

namespace {

Chord chordOf(PitchClass root, const char* typeId) { return Chord{root, &chordType(typeId)}; }

/// A test-only chord type of eight distinct pitch classes (C C# D D# E F F# G): the voice count the snapshot
/// budget leaves headroom for (`kMaxChords`, document.h). The dictionary's largest type has five notes, which
/// cannot reach the per-beat cap; this one can.
const ChordType kCluster8{"cluster8", "cl8", {0, 1, 2, 3, 4, 5, 6, 7}, Family::Other};

/// One chord at a given id, length in beats and velocity.
ChordEvent chordEvent(int id, Chord chord, double lengthBeats, int velocity = 100) {
    return ChordEvent{id, chord, lengthBeats, velocity};
}

Document docOf(std::vector<ChordEvent> events, Feel feel, Meter meter = {4, 4}) {
    Document doc;
    doc.events = std::move(events);
    doc.feel = feel;
    doc.meter = meter;
    return doc;
}

/// A performed event without its ownership, for literal comparison.
struct Ev {
    int tick;
    bool on;
    int pitch;
    int velocity;
};

bool operator==(const Ev& a, const Ev& b) {
    return a.tick == b.tick && a.on == b.on && a.pitch == b.pitch && a.velocity == b.velocity;
}

Ev on(int tick, int pitch, int velocity) { return Ev{tick, true, pitch, velocity}; }
Ev off(int tick, int pitch) { return Ev{tick, false, pitch, 0}; }

std::vector<Ev> plain(const Performance& p) {
    std::vector<Ev> out;
    for (const PerformedEvent& e : p.events) out.push_back(Ev{e.tick, e.on, e.pitch, e.velocity});
    return out;
}

std::string describe(const std::vector<Ev>& events) {
    std::ostringstream os;
    for (const Ev& e : events) os << (e.on ? " on" : " off") << e.pitch << "@" << e.tick << "v" << e.velocity;
    return os.str();
}

void expectEvents(const Performance& p, const std::vector<Ev>& expected) {
    const std::vector<Ev> actual = plain(p);
    INFO("actual:  " << describe(actual));
    INFO("expected:" << describe(expected));
    CHECK(actual == expected);
}

const Feel kPulse{Rhythm::Pulse, Strum::Off, StrumSpeed::Medium, Humanise::Off};
const Feel kSubdivided{Rhythm::Subdivided, Strum::Off, StrumSpeed::Medium, Humanise::Off};

/// Checks every D10 rule on `p` of `doc`: strict order, every on in [s, e - 5] and every off in [s + 5, e] of
/// its own chord, no on at or after the end, and per pitch strictly alternating on/off pairs of at least 5
/// ticks, never overlapping.
void checkSafety(const Document& doc, const Performance& p) {
    REQUIRE(p.chordTicks.size() == doc.events.size() + 1);
    for (std::size_t i = 1; i < p.events.size(); ++i) {
        const PerformedEvent& a = p.events[i - 1];
        const PerformedEvent& b = p.events[i];
        REQUIRE(std::make_tuple(a.tick, a.on, a.pitch) < std::make_tuple(b.tick, b.on, b.pitch));
    }
    std::map<int, int> openOn;  // pitch -> tick of the sounding on
    std::map<int, int> openChord;
    for (const PerformedEvent& e : p.events) {
        REQUIRE(e.chord >= 0);
        REQUIRE(static_cast<std::size_t>(e.chord) < doc.events.size());
        const int s = p.chordTicks[static_cast<std::size_t>(e.chord)];
        const int end = p.chordTicks[static_cast<std::size_t>(e.chord) + 1];
        if (e.on) {
            CHECK(e.tick >= s);
            CHECK(e.tick <= end - kMinNoteTicks);
            CHECK(e.tick < p.endTick());
            CHECK(e.velocity >= 1);
            CHECK(e.velocity <= 127);
            REQUIRE(openOn.count(e.pitch) == 0);  // never two ons of a pitch without an off between
            openOn[e.pitch] = e.tick;
            openChord[e.pitch] = e.chord;
        } else {
            CHECK(e.tick <= end);
            CHECK(e.velocity == 0);
            REQUIRE(openOn.count(e.pitch) == 1);  // every off closes an on
            CHECK(openChord[e.pitch] == e.chord);
            CHECK(e.tick - openOn[e.pitch] >= kMinNoteTicks);
            openOn.erase(e.pitch);
        }
    }
    CHECK(openOn.empty());
}

}  // namespace

TEST_CASE("feel hash: literal splitmix64 vectors from an independent implementation") {
    // Vectors computed with Python 3 (arbitrary-precision ints masked to 64 bits), run as `python3 -I -c` with:
    //   M = (1 << 64) - 1
    //   def mix(x):
    //       x = (x + 0x9E3779B97F4A7C15) & M
    //       x = ((x ^ (x >> 30)) * 0xBF58476D1CE4E5B9) & M
    //       x = ((x ^ (x >> 27)) * 0x94D049BB133111EB) & M
    //       return x ^ (x >> 31)
    //   def key(i, h, n, s): return mix(mix(mix(mix(i & 0xFFFFFFFF) ^ h) ^ n) ^ s)
    //   def off(k, m): return k % (2 * m + 1) - m
    //   def late(k, m): return k % (m + 1)
    CHECK(feelMix(0) == 0xE220A8397B1DCDAFull);  // splitmix64's first output for state 0
    CHECK(feelMix(1) == 0x910A2DEC89025CC1ull);
    CHECK(feelMix(0xFFFFFFFFFFFFFFFFull) == 0xE4D971771B652C20ull);
    CHECK(feelKey(1, 0, 0, 1) == 0x3FA25E1657E024C1ull);
    CHECK(feelOffset(0x3FA25E1657E024C1ull, 20) == -16);
    CHECK(feelLateOffset(0x3FA25E1657E024C1ull, 20) == 8);
    CHECK(feelKey(7, 3, 2, 3) == 0x1F4906C2AAD44E9Bull);
    CHECK(feelOffset(0x1F4906C2AAD44E9Bull, 12) == -3);
    // A negative id is taken as its 32-bit pattern (0xFFFFFFFF), not sign-extended.
    CHECK(feelKey(-1, 0, 0, 2) == 0x0E5FB98FD6941103ull);
    CHECK(feelOffset(0x0E5FB98FD6941103ull, 4) == -4);
    CHECK(feelOffset(0, 4) == -4);
    CHECK(feelOffset(8, 4) == 4);
    CHECK(feelOffset(9, 4) == -4);
    CHECK(feelOffset(12345, 0) == 0);
    CHECK(feelLateOffset(5, 4) == 0);
}

TEST_CASE("feel hash: offsets stay in [-m, m] and reach every value") {
    for (int m : {1, 2, 4, 5, 8, 10, 12, 20}) {
        CAPTURE(m);
        std::vector<int> seen(static_cast<std::size_t>(2 * m + 1), 0);
        std::vector<int> late(static_cast<std::size_t>(m + 1), 0);
        bool inRange = true;
        for (int i = 0; i < 10000; ++i) {
            const std::uint64_t key = feelKey(i, i % 7, i % 5, 1 + i % 3);
            const int o = feelOffset(key, m);
            const int l = feelLateOffset(key, m);
            if (o < -m || o > m || l < 0 || l > m) {
                inRange = false;
                continue;
            }
            ++seen[static_cast<std::size_t>(o + m)];
            ++late[static_cast<std::size_t>(l)];
        }
        CHECK(inRange);
        if (m == 4) {
            CHECK(std::all_of(seen.begin(), seen.end(), [](int n) { return n > 0; }));
            CHECK(std::all_of(late.begin(), late.end(), [](int n) { return n > 0; }));
        }
    }
}

TEST_CASE("feelPerforms: only a rhythm, a strum or a humanise counts; the speed alone does not") {
    CHECK_FALSE(feelPerforms(Feel{}));
    CHECK_FALSE(feelPerforms(Feel{Rhythm::Block, Strum::Off, StrumSpeed::Loose, Humanise::Off}));
    CHECK(feelPerforms(Feel{Rhythm::Pulse, Strum::Off, StrumSpeed::Medium, Humanise::Off}));
    CHECK(feelPerforms(Feel{Rhythm::Block, Strum::Down, StrumSpeed::Medium, Humanise::Off}));
    CHECK(feelPerforms(Feel{Rhythm::Block, Strum::Off, StrumSpeed::Medium, Humanise::Light}));
}

TEST_CASE("performDocument: an empty document performs nothing, ending at tick 0") {
    const Performance p = performDocument(docOf({}, kPulse));
    CHECK(p.events.empty());
    CHECK(p.chordTicks == Ints{0});
    CHECK(p.endTick() == 0);
    CHECK_FALSE(p.feelLimited);
}

TEST_CASE("performDocument: chord ticks round the cumulative beats, so neighbours share a boundary") {
    // 0.3 + 0.3 + 0.7 beats: 288, 576, 1248 ticks (0.3 * 960 = 288 exactly after rounding).
    const Document doc = docOf({chordEvent(1, chordOf(0, "maj"), 0.3), chordEvent(2, chordOf(9, "min"), 0.3),
                                chordEvent(3, chordOf(0, "maj"), 0.7)},
                               kPulse);
    CHECK(performDocument(doc).chordTicks == Ints{0, 288, 576, 1248});
}

TEST_CASE("performDocument: pulse without strum or humanise is the rhythm's hits, notes from the voicing") {
    const Document doc = docOf({chordEvent(1, chordOf(0, "maj"), 4.0)}, kPulse);
    const Performance p = performDocument(doc);
    CHECK(p.endTick() == 3840);
    expectEvents(p, {on(0, 60, 100), on(0, 64, 100), on(0, 67, 100), off(864, 60), off(864, 64), off(864, 67),
                     on(960, 60, 90), on(960, 64, 90), on(960, 67, 90), off(1824, 60), off(1824, 64), off(1824, 67),
                     on(1920, 60, 90), on(1920, 64, 90), on(1920, 67, 90), off(2784, 60), off(2784, 64),
                     off(2784, 67), on(2880, 60, 90), on(2880, 64, 90), on(2880, 67, 90), off(3744, 60),
                     off(3744, 64), off(3744, 67)});
    for (const PerformedEvent& e : p.events) {
        CHECK(e.chord == 0);
        CHECK(e.hit == e.tick / 960);
    }
}

TEST_CASE("performDocument: the voicings come from voicings(doc), auto voice-leading included") {
    Document doc = docOf({chordEvent(1, chordOf(0, "maj"), 1.0), chordEvent(2, chordOf(5, "maj"), 1.0)}, kPulse);
    doc.autoVoiceLead = true;  // F follows C into its second inversion: C F A (60 65 69)
    REQUIRE(voicings(doc)[1] == Ints{60, 65, 69});
    expectEvents(performDocument(doc), {on(0, 60, 100), on(0, 64, 100), on(0, 67, 100), off(864, 60),
                                        off(864, 64), off(864, 67), on(960, 60, 100), on(960, 65, 100),
                                        on(960, 69, 100), off(1824, 60), off(1824, 65), off(1824, 69)});
}

TEST_CASE("strum: down, up and the three speeds over a block chord") {
    const Chord c = chordOf(0, "maj");
    SUBCASE("down medium: lowest first, 20 ticks apart, every off at the chord's end") {
        const Document doc = docOf({chordEvent(1, c, 4.0)}, Feel{Rhythm::Block, Strum::Down, StrumSpeed::Medium});
        expectEvents(performDocument(doc), {on(0, 60, 100), on(20, 64, 100), on(40, 67, 100), off(3840, 60),
                                            off(3840, 64), off(3840, 67)});
    }
    SUBCASE("up medium: highest first") {
        const Document doc = docOf({chordEvent(1, c, 4.0)}, Feel{Rhythm::Block, Strum::Up, StrumSpeed::Medium});
        expectEvents(performDocument(doc), {on(0, 67, 100), on(20, 64, 100), on(40, 60, 100), off(3840, 60),
                                            off(3840, 64), off(3840, 67)});
    }
    SUBCASE("tight: 10 ticks") {
        const Document doc = docOf({chordEvent(1, c, 4.0)}, Feel{Rhythm::Block, Strum::Down, StrumSpeed::Tight});
        expectEvents(performDocument(doc), {on(0, 60, 100), on(10, 64, 100), on(20, 67, 100), off(3840, 60),
                                            off(3840, 64), off(3840, 67)});
    }
    SUBCASE("loose: 40 ticks") {
        const Document doc = docOf({chordEvent(1, c, 4.0)}, Feel{Rhythm::Block, Strum::Down, StrumSpeed::Loose});
        expectEvents(performDocument(doc), {on(0, 60, 100), on(40, 64, 100), on(80, 67, 100), off(3840, 60),
                                            off(3840, 64), off(3840, 67)});
    }
}

TEST_CASE("strum: down-up strums a strong hit down and an off-pulse hit up") {
    // Subdivided 4/4 over [0, 960): hit 0 [0, 432) on the chord start, hit 480 [480, 912) off the pulse.
    const Document doc =
        docOf({chordEvent(1, chordOf(0, "maj"), 1.0)}, Feel{Rhythm::Subdivided, Strum::DownUp, StrumSpeed::Medium});
    expectEvents(performDocument(doc), {on(0, 60, 100), on(20, 64, 100), on(40, 67, 100), off(432, 60),
                                        off(432, 64), off(432, 67), on(480, 67, 80), on(500, 64, 80),
                                        on(520, 60, 80), off(912, 60), off(912, 64), off(912, 67)});
}

TEST_CASE("strum: the spread is capped at half the gated hit") {
    // Eight notes, loose (40), a Subdivided hit of 432 gated ticks: step min(40, (432 / 2) / 7) = min(40, 30).
    const Document doc =
        docOf({chordEvent(1, Chord{0, &kCluster8}, 1.0)}, Feel{Rhythm::Subdivided, Strum::Down, StrumSpeed::Loose});
    REQUIRE(voicings(doc)[0] == Ints{60, 61, 62, 63, 64, 65, 66, 67});
    const Performance p = performDocument(doc);
    std::vector<Ev> firstHit;
    for (const PerformedEvent& e : p.events) {
        if (e.hit == 0) firstHit.push_back(Ev{e.tick, e.on, e.pitch, e.velocity});
    }
    INFO(describe(firstHit));
    CHECK(firstHit == std::vector<Ev>{on(0, 60, 100), on(30, 61, 100), on(60, 62, 100), on(90, 63, 100),
                                      on(120, 64, 100), on(150, 65, 100), on(180, 66, 100), on(210, 67, 100),
                                      off(432, 60), off(432, 61), off(432, 62), off(432, 63), off(432, 64),
                                      off(432, 65), off(432, 66), off(432, 67)});
}

TEST_CASE("strum: low-then-upper strums the upper notes; arpeggios ignore the strum") {
    // 4/4 [0, 1920): hit 0 the low note alone, hit 960 the upper two, down, 20 apart.
    const Document low =
        docOf({chordEvent(1, chordOf(0, "maj"), 2.0)}, Feel{Rhythm::LowThenUpper, Strum::Down, StrumSpeed::Medium});
    expectEvents(performDocument(low), {on(0, 60, 100), off(864, 60), on(960, 64, 90), on(980, 67, 90),
                                        off(1824, 64), off(1824, 67)});
    for (Rhythm arp : {Rhythm::ArpUp, Rhythm::ArpUpDown}) {
        const Document strummed =
            docOf({chordEvent(1, chordOf(0, "maj"), 2.0)}, Feel{arp, Strum::Up, StrumSpeed::Loose});
        const Document plainArp = docOf({chordEvent(1, chordOf(0, "maj"), 2.0)}, Feel{arp});
        CHECK(plain(performDocument(strummed)) == plain(performDocument(plainArp)));
    }
}

TEST_CASE("humanise: literal ons and velocities for one heavy pulse chord") {
    // C major (voicing indices 0 1 2 = 60 64 67), id 1, velocity 100, Pulse 4/4 over [0, 3840), heavy
    // (hit 20, note 4, velocity 12), strum off. Hits (rhythmHits): 0, 960, 1920, 2880, gated length 864,
    // velocities 100, 90, 90, 90. From the Python reference in the hash test:
    //   hit 0: H = late(key(1,0,0,1), 20) = 8;  notes off(key(1,0,i,2), 4) = 1, -4, -1;  vel off(.., 12) = 8, 4, -3
    //   hit 1: H = off(key(1,1,0,1), 20) = 0;   notes -4, 3, -4;  vel 12, 12, 1
    //   hit 2: H = 2;                            notes 0, -1, 3;   vel 9, -12, 6
    //   hit 3: H = -12;                          notes -4, 4, -1;  vel -5, 11, 2
    // on = hitStart + H + note offset; off = hitStart + H + 864; velocity = hit velocity + vel offset.
    const Document doc =
        docOf({chordEvent(1, chordOf(0, "maj"), 4.0)}, Feel{Rhythm::Pulse, Strum::Off, StrumSpeed::Medium, Humanise::Heavy});
    REQUIRE(feelLateOffset(feelKey(1, 0, 0, kFeelSaltHitTiming), 20) == 8);
    REQUIRE(feelOffset(feelKey(1, 3, 0, kFeelSaltHitTiming), 20) == -12);
    REQUIRE(feelOffset(feelKey(1, 2, 1, kFeelSaltVelocity), 12) == -12);
    expectEvents(performDocument(doc),
                 {on(4, 64, 104), on(7, 67, 97), on(9, 60, 108), off(872, 60), off(872, 64), off(872, 67),
                  on(956, 60, 102), on(956, 67, 91), on(963, 64, 102), off(1824, 60), off(1824, 64), off(1824, 67),
                  on(1921, 64, 78), on(1922, 60, 99), on(1925, 67, 96), off(2786, 60), off(2786, 64), off(2786, 67),
                  on(2864, 60, 85), on(2867, 67, 92), on(2872, 64, 101), off(3732, 60), off(3732, 64),
                  off(3732, 67)});
}

TEST_CASE("humanise: off is exactly the unhumanised performance") {
    for (Rhythm r : {Rhythm::Block, Rhythm::Pulse, Rhythm::Subdivided, Rhythm::Offbeats, Rhythm::LowThenUpper,
                     Rhythm::ArpUp, Rhythm::ArpUpDown}) {
        CAPTURE(static_cast<int>(r));
        const Document doc = docOf({chordEvent(1, chordOf(0, "maj"), 4.0), chordEvent(2, chordOf(9, "m7"), 2.5, 77)},
                                   Feel{r, Strum::Off, StrumSpeed::Medium, Humanise::Off});
        // Rebuild the expected events from rhythmHits alone: ons at the hit start, offs at its end.
        const std::vector<std::vector<int>> v = voicings(doc);
        const Ints ticks{0, 3840, 6240};
        std::vector<std::tuple<int, int, int, int>> expected;  // (tick, on, pitch, velocity)
        for (int c = 0; c < 2; ++c) {
            const ChordEvent& ce = doc.events[static_cast<std::size_t>(c)];
            for (const Hit& h : rhythmHits(r, doc.meter, ticks[static_cast<std::size_t>(c)],
                                           ticks[static_cast<std::size_t>(c) + 1],
                                           static_cast<int>(v[static_cast<std::size_t>(c)].size()), ce.velocity)) {
                for (int n : h.notes) {
                    const int pitch = v[static_cast<std::size_t>(c)][static_cast<std::size_t>(n)];
                    expected.emplace_back(h.start, 1, pitch, h.velocity);
                    expected.emplace_back(h.end, 0, pitch, 0);
                }
            }
        }
        std::sort(expected.begin(), expected.end());
        std::vector<std::tuple<int, int, int, int>> actual;
        for (const PerformedEvent& e : performDocument(doc).events) {
            actual.emplace_back(e.tick, e.on ? 1 : 0, e.pitch, e.velocity);
        }
        CHECK(actual == expected);
    }
}

TEST_CASE("humanise: a chord's first hit never starts before the chord; strum order survives") {
    std::mt19937 rng(20261030);
    const char* types[] = {"maj", "m7", "9", "5", "maj9"};
    for (int trial = 0; trial < 300; ++trial) {
        CAPTURE(trial);
        std::vector<ChordEvent> events;
        const int count = 1 + static_cast<int>(rng() % 6);
        for (int i = 0; i < count; ++i) {
            const double len = 0.25 * (1 + static_cast<int>(rng() % 16));
            events.push_back(chordEvent(static_cast<int>(rng() % 100000) + 1,
                                        chordOf(static_cast<int>(rng() % 12), types[rng() % 5]), len,
                                        1 + static_cast<int>(rng() % 127)));
        }
        const Feel feel{static_cast<Rhythm>(rng() % 7), static_cast<Strum>(rng() % 4),
                        static_cast<StrumSpeed>(rng() % 3), static_cast<Humanise>(1 + rng() % 3)};
        const Meter meters[] = {{4, 4}, {3, 4}, {6, 8}, {7, 8}, {5, 4}};
        const Document doc = docOf(events, feel, meters[rng() % 5]);
        const Performance p = performDocument(doc);
        REQUIRE_FALSE(p.feelLimited);
        checkSafety(doc, p);
        const std::vector<std::vector<int>> v = voicings(doc);
        // Group the ons by (chord, hit).
        std::map<std::pair<int, int>, std::vector<const PerformedEvent*>> hits;
        for (const PerformedEvent& e : p.events) {
            if (e.on) hits[{e.chord, e.hit}].push_back(&e);
        }
        const bool arp = feel.rhythm == Rhythm::ArpUp || feel.rhythm == Rhythm::ArpUpDown;
        for (const auto& [where, ons] : hits) {
            const int s = p.chordTicks[static_cast<std::size_t>(where.first)];
            for (const PerformedEvent* e : ons) CHECK(e->tick >= s);
            if (feel.strum == Strum::Off || arp) continue;
            // Strum order: down = ascending voicing index, up = descending; down-up depends on the hit. The
            // ons in that order must have non-decreasing ticks; check whichever direction the hit used by
            // requiring the order to be monotone in pitch one way or the other, and the right way when fixed.
            std::vector<const PerformedEvent*> byPitch(ons);
            std::sort(byPitch.begin(), byPitch.end(),
                      [](const PerformedEvent* a, const PerformedEvent* b) { return a->pitch < b->pitch; });
            bool ascending = true;
            bool descending = true;
            for (std::size_t i = 1; i < byPitch.size(); ++i) {
                ascending = ascending && byPitch[i - 1]->tick <= byPitch[i]->tick;
                descending = descending && byPitch[i - 1]->tick >= byPitch[i]->tick;
            }
            if (feel.strum == Strum::Down) CHECK(ascending);
            if (feel.strum == Strum::Up) CHECK(descending);
            if (feel.strum == Strum::DownUp) CHECK((ascending || descending));
        }
    }
}

TEST_CASE("humanise: chord A's offsets do not depend on chord B's id") {
    // The raw offsets are functions of (id, hit, note, salt) only: chord A (id 1) uses feelKey(1, ...) whatever
    // B is called. A's performed events, which no safety rule couples to B (every note stays inside its own
    // chord), are the literal heavy-pulse events of the test above; B's change with its id.
    const Feel heavy{Rhythm::Pulse, Strum::Off, StrumSpeed::Medium, Humanise::Heavy};
    for (int i = 0; i < 4; ++i) {
        CAPTURE(i);
        const int h = i == 0 ? feelLateOffset(feelKey(1, 0, 0, kFeelSaltHitTiming), 20)
                             : feelOffset(feelKey(1, i, 0, kFeelSaltHitTiming), 20);
        CHECK(h == Ints{8, 0, 2, -12}[static_cast<std::size_t>(i)]);
    }
    const auto chordEvents = [](const Performance& p, int chord) {
        std::vector<Ev> out;
        for (const PerformedEvent& e : p.events) {
            if (e.chord == chord) out.push_back(Ev{e.tick, e.on, e.pitch, e.velocity});
        }
        return out;
    };
    const Performance withTwo =
        performDocument(docOf({chordEvent(1, chordOf(0, "maj"), 4.0), chordEvent(2, chordOf(0, "maj"), 4.0)}, heavy));
    const Performance withLarge = performDocument(
        docOf({chordEvent(1, chordOf(0, "maj"), 4.0), chordEvent(987654, chordOf(0, "maj"), 4.0)}, heavy));
    const std::vector<Ev> expectedA{
        on(4, 64, 104), on(7, 67, 97), on(9, 60, 108), off(872, 60), off(872, 64), off(872, 67),
        on(956, 60, 102), on(956, 67, 91), on(963, 64, 102), off(1824, 60), off(1824, 64), off(1824, 67),
        on(1921, 64, 78), on(1922, 60, 99), on(1925, 67, 96), off(2786, 60), off(2786, 64), off(2786, 67),
        on(2864, 60, 85), on(2867, 67, 92), on(2872, 64, 101), off(3732, 60), off(3732, 64), off(3732, 67)};
    CHECK(chordEvents(withTwo, 0) == expectedA);
    CHECK(chordEvents(withLarge, 0) == expectedA);
    CHECK(chordEvents(withTwo, 1) != chordEvents(withLarge, 1));
}

TEST_CASE("humanise: deterministic, and a moved chord keeps its own feel") {
    const Feel feel{Rhythm::Subdivided, Strum::DownUp, StrumSpeed::Loose, Humanise::Heavy};
    const Document doc = docOf({chordEvent(5, chordOf(0, "maj7"), 4.0), chordEvent(9, chordOf(7, "9"), 2.0)}, feel);
    const Performance a = performDocument(doc);
    const Performance b = performDocument(doc);
    CHECK(plain(a) == plain(b));
    CHECK(compile(doc).events == compile(doc).events);
    // The same chord (id 9) moved to the front: its events, shifted by its old start (3840), are unchanged
    // (both starts are bar starts, so the accents agree too).
    const Document moved = docOf({chordEvent(9, chordOf(7, "9"), 2.0), chordEvent(5, chordOf(0, "maj7"), 4.0)}, feel);
    const Performance m = performDocument(moved);
    std::vector<Ev> before;
    std::vector<Ev> after;
    for (const PerformedEvent& e : a.events) {
        if (e.chord == 1) before.push_back(Ev{e.tick - 3840, e.on, e.pitch, e.velocity});
    }
    for (const PerformedEvent& e : m.events) {
        if (e.chord == 0) after.push_back(Ev{e.tick, e.on, e.pitch, e.velocity});
    }
    CHECK(!before.empty());
    CHECK(before == after);
}

TEST_CASE("safety: a hit squeezed by the next one on the same pitches is dropped") {
    // Am over [0, 479) (0.499 beat rounds to 479 ticks), then C over [479, 1439), Subdivided 4/4. C's hits:
    // 479 (gap 1, gated to the 5-tick floor: [479, 484)), 480 ([480, 912), off-pulse 80) and 960 ([960, 1391),
    // pulse 90). The 480 hit's ons cut the 479 hit's offs to 480: one tick, so those notes are dropped.
    const Document doc = docOf({chordEvent(1, chordOf(9, "min"), 0.499), chordEvent(2, chordOf(0, "maj"), 1.0)},
                               kSubdivided);
    const Performance p = performDocument(doc);
    CHECK(p.chordTicks == Ints{0, 479, 1439});
    expectEvents(p, {on(0, 69, 100), on(0, 72, 100), on(0, 76, 100), off(431, 69), off(431, 72), off(431, 76),
                     on(480, 60, 80), on(480, 64, 80), on(480, 67, 80), off(912, 60), off(912, 64), off(912, 67),
                     on(960, 60, 90), on(960, 64, 90), on(960, 67, 90), off(1391, 60), off(1391, 64),
                     off(1391, 67)});
    checkSafety(doc, p);
}

TEST_CASE("safety: a gated hit past the chord's end is pulled inside it") {
    // One chord over [0, 481), Subdivided 4/4: rhythmHits gives [0, 432) and [480, 485) (the 5-tick floor). The
    // second hit's on is clamped to 481 - 5 = 476 and its off to 481: nothing passes the chord's end.
    const Document doc = docOf({chordEvent(1, chordOf(0, "maj"), 481.0 / 960.0)}, kSubdivided);
    const Performance p = performDocument(doc);
    CHECK(p.endTick() == 481);
    expectEvents(p, {on(0, 60, 100), on(0, 64, 100), on(0, 67, 100), off(432, 60), off(432, 64), off(432, 67),
                     on(476, 60, 80), on(476, 64, 80), on(476, 67, 80), off(481, 60), off(481, 64), off(481, 67)});
}

TEST_CASE("safety: random documents and feels keep every note paired, inside its chord and before the end") {
    std::mt19937 rng(424242);
    const char* types[] = {"maj", "min", "7", "m9", "5", "69", "dim7"};
    for (int trial = 0; trial < 400; ++trial) {
        CAPTURE(trial);
        std::vector<ChordEvent> events;
        const int count = 1 + static_cast<int>(rng() % 10);
        for (int i = 0; i < count; ++i) {
            // Lengths off the tick grid as well as on it: 0.25 .. 4.25 in steps of 1/7 of a beat or quarters.
            const double len = rng() % 2 ? 0.25 + static_cast<double>(rng() % 29) / 7.0 : 0.25 * (1 + rng() % 16);
            events.push_back(chordEvent(i + 1, chordOf(static_cast<int>(rng() % 12), types[rng() % 7]), len,
                                        1 + static_cast<int>(rng() % 127)));
        }
        const Feel feel{static_cast<Rhythm>(rng() % 7), static_cast<Strum>(rng() % 4),
                        static_cast<StrumSpeed>(rng() % 3), static_cast<Humanise>(rng() % 4)};
        const Meter meters[] = {{4, 4}, {3, 4}, {2, 4}, {6, 8}, {7, 8}, {9, 8}, {12, 8}, {3, 8}, {5, 4}, {2, 2}};
        Document doc = docOf(events, feel, meters[rng() % 10]);
        doc.autoVoiceLead = rng() % 2 == 0;
        const Performance p = performDocument(doc);
        REQUIRE_FALSE(p.feelLimited);
        checkSafety(doc, p);
    }
}

TEST_CASE("densestBeat: half-open windows of 960 ticks, across the wrap and over several short wraps") {
    const auto ev = [](int tick, bool isOn) { return PerformedEvent{tick, isOn, 60, isOn ? 100 : 0, 0, 0}; };
    CHECK(densestBeat({}, 0) == 0);
    // 0, 0, 100, 959 fall in [0, 960); 960 does not.
    CHECK(densestBeat({ev(0, true), ev(0, true), ev(100, false), ev(959, true), ev(960, false)}, 3840) == 4);
    // Across the wrap: two events at 3800 and two at 10 (3850 one wrap later) share a beat.
    CHECK(densestBeat({ev(10, true), ev(10, true), ev(3800, false), ev(3800, false)}, 3840) == 4);
    // A 240-tick progression with an on at 0 and an off at 240: the window [0, 960) holds 0, 240 (the off),
    // 240 (the next wrap's on), 480, 480, 720, 720 (7); the densest, [240, 1200), holds 240, 240, 480, 480, 720,
    // 720, 960, 960.
    CHECK(densestBeat({ev(0, true), ev(240, false)}, 240) == 8);
}

TEST_CASE("budgets: 4096 events fit exactly; 4097 or more fall back for the whole document") {
    SUBCASE("64 one-bar four-note chords, Subdivided: 64 x 8 hits x 4 notes x 2 = 4096") {
        std::vector<ChordEvent> events;
        for (int i = 0; i < 64; ++i) events.push_back(chordEvent(i + 1, chordOf(7, "7"), 4.0));
        const Performance p = performDocument(docOf(events, kSubdivided));
        CHECK_FALSE(p.feelLimited);
        CHECK(p.events.size() == kMaxSnapshotEvents);
    }
    SUBCASE("64 one-bar ninths, Subdivided: 5120 events") {
        std::vector<ChordEvent> events;
        for (int i = 0; i < 64; ++i) events.push_back(chordEvent(i + 1, chordOf(0, "9"), 4.0));
        const Performance p = performDocument(docOf(events, kSubdivided));
        CHECK(p.feelLimited);
        CHECK(p.events.empty());
        CHECK(p.endTick() == 64 * 3840);
    }
}

TEST_CASE("budgets: a beat holding more than 96 events falls back; exactly 96 fits") {
    SUBCASE("eight-note chords of a quarter beat off the 7/8 grid: a hit every 120 ticks, 128 events a beat") {
        std::vector<ChordEvent> events{chordEvent(1, Chord{0, &kCluster8}, 0.375)};
        for (int i = 0; i < 15; ++i) events.push_back(chordEvent(i + 2, Chord{0, &kCluster8}, 0.25));
        const Document doc = docOf(events, kSubdivided, {7, 8});
        const Performance p = performDocument(doc);
        CHECK(p.feelLimited);
        CHECK(p.events.empty());
        CHECK(p.endTick() == 3960);
    }
    SUBCASE("eight-note chords of half a beat from tick 600 in 7/8: hits at 0, 120 and 360 of every 480 ticks") {
        // Each 480-tick period holds three hits of eight notes, ons and offs: 48 events; a beat is two periods.
        std::vector<ChordEvent> events{chordEvent(1, Chord{0, &kCluster8}, 0.625)};
        for (int i = 0; i < 31; ++i) events.push_back(chordEvent(i + 2, Chord{0, &kCluster8}, 0.5));
        const Performance p = performDocument(docOf(events, kSubdivided, {7, 8}));
        REQUIRE_FALSE(p.feelLimited);
        CHECK(densestBeat(p.events, p.endTick()) == 96);
    }
    SUBCASE("32 one-bar four-note chords, Subdivided 4/4: 16 events a beat") {
        std::vector<ChordEvent> events;
        for (int i = 0; i < 32; ++i) events.push_back(chordEvent(i + 1, chordOf(2, "m7"), 4.0));
        const Performance p = performDocument(docOf(events, kSubdivided));
        CHECK_FALSE(p.feelLimited);
        CHECK(p.events.size() == 2048);
        CHECK(densestBeat(p.events, p.endTick()) == 16);
    }
}
