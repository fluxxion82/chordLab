#include "chordlab/meter.h"

namespace chordlab {

bool operator==(Meter a, Meter b) { return a.num == b.num && a.den == b.den; }

bool validMeter(Meter m) { return m.num >= 1 && m.num <= 16 && (m.den == 2 || m.den == 4 || m.den == 8); }

int barTicks(Meter m) { return m.num * (4 * kFeelTicksPerBeat) / m.den; }

bool isCompound(Meter m) { return m.den == 8 && m.num % 3 == 0 && m.num >= 6; }

int pulseTicks(Meter m) { return isCompound(m) ? 1440 : (4 * kFeelTicksPerBeat) / m.den; }

int subdivisionTicks(Meter m) { return isCompound(m) ? 480 : pulseTicks(m) / 2; }

std::vector<int> gridPointsInside(int start, int end, int step) {
    std::vector<int> out;
    if (step < 1) return out;
    for (int t = (start / step + 1) * step; t < end; t += step) out.push_back(t);
    return out;
}

}  // namespace chordlab
