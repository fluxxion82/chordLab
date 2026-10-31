#pragma once

#include <vector>

namespace chordlab {

/// Feel ticks in one quarter-note beat: the grid every rhythm, strum and humanise offset lives on.
constexpr int kFeelTicksPerBeat = 960;

/// A time signature. Valid: `num` 1..16, `den` 2, 4 or 8 (see `validMeter`).
struct Meter {
    int num = 4;
    int den = 4;
};

bool operator==(Meter a, Meter b);
inline bool operator!=(Meter a, Meter b) { return !(a == b); }

/// num 1..16 and den 2, 4 or 8.
bool validMeter(Meter m);

/// Ticks in one bar: num * 3840 / den.
int barTicks(Meter m);

/// Compound meter (felt in dotted-quarter pulses): den == 8, num a multiple of 3, num >= 6.
bool isCompound(Meter m);

/// Ticks in one pulse: 1440 when compound, else 3840 / den.
int pulseTicks(Meter m);

/// Ticks in one subdivision: 480 when compound, else pulseTicks / 2.
int subdivisionTicks(Meter m);

/// Multiples of `step` strictly between `start` and `end`, ascending. Empty when `step` < 1 or the span
/// holds none.
std::vector<int> gridPointsInside(int start, int end, int step);

}  // namespace chordlab
