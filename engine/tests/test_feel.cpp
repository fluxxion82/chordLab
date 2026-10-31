#include "doctest.h"

#include "chordlab/feel.h"

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
