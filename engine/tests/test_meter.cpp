#include "doctest.h"

#include "chordlab/meter.h"

#include <vector>

using namespace chordlab;

TEST_CASE("meter: 4/4 ticks") {
    Meter m;
    CHECK(m == Meter{4, 4});
    CHECK(validMeter(m));
    CHECK(barTicks(m) == 3840);
    CHECK(pulseTicks(m) == 960);
    CHECK(subdivisionTicks(m) == 480);
    CHECK_FALSE(isCompound(m));
    CHECK(kFeelTicksPerBeat == 960);
}

TEST_CASE("meter: simple meters bar lengths") {
    CHECK(barTicks({3, 4}) == 2880);
    CHECK(barTicks({2, 4}) == 1920);
    CHECK(barTicks({5, 4}) == 4800);
    CHECK(barTicks({2, 2}) == 3840);
    CHECK(pulseTicks({2, 2}) == 1920);
    CHECK(subdivisionTicks({2, 2}) == 960);
    CHECK_FALSE(isCompound({2, 2}));
}

TEST_CASE("meter: compound meters") {
    CHECK(barTicks({6, 8}) == 2880);
    CHECK(pulseTicks({6, 8}) == 1440);
    CHECK(subdivisionTicks({6, 8}) == 480);
    CHECK(isCompound({6, 8}));
    CHECK(barTicks({9, 8}) == 4320);
    CHECK(isCompound({9, 8}));
    CHECK(barTicks({12, 8}) == 5760);
    CHECK(isCompound({12, 8}));
}

TEST_CASE("meter: eighth-note meters that are not compound") {
    CHECK(barTicks({7, 8}) == 3360);
    CHECK(pulseTicks({7, 8}) == 480);
    CHECK(subdivisionTicks({7, 8}) == 240);
    CHECK_FALSE(isCompound({7, 8}));
    CHECK(barTicks({3, 8}) == 1440);
    CHECK(pulseTicks({3, 8}) == 480);
    CHECK(subdivisionTicks({3, 8}) == 240);
    CHECK_FALSE(isCompound({3, 8}));
}

TEST_CASE("meter: validMeter") {
    CHECK(validMeter({1, 2}));
    CHECK(validMeter({16, 8}));
    CHECK_FALSE(validMeter({0, 4}));
    CHECK_FALSE(validMeter({17, 4}));
    CHECK_FALSE(validMeter({4, 3}));
    CHECK_FALSE(validMeter({4, 16}));
    CHECK_FALSE(validMeter({4, 1}));
}

TEST_CASE("meter: gridPointsInside") {
    CHECK(gridPointsInside(960, 2880, 960) == std::vector<int>{1920});
    CHECK(gridPointsInside(240, 1920, 480) == std::vector<int>{480, 960, 1440});
    CHECK(gridPointsInside(0, 0, 480).empty());
    CHECK(gridPointsInside(1000, 1001, 480).empty());
    CHECK(gridPointsInside(0, 1441, 480) == std::vector<int>{480, 960, 1440});
    CHECK(gridPointsInside(100, 500, 480) == std::vector<int>{480});
}
