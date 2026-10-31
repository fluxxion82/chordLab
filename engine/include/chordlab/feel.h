#pragma once

#include "chordlab/meter.h"

#include <vector>

namespace chordlab {

/// How a chord's notes are spread over its span (see the feel plan, D4-D7).
enum class Rhythm { Block, Pulse, Subdivided, Offbeats, LowThenUpper, ArpUp, ArpUpDown };

/// Direction of the strum that staggers a hit's note onsets.
enum class Strum { Off, Down, Up, DownUp };

/// How far apart the strummed notes start.
enum class StrumSpeed { Tight, Medium, Loose };

/// Amount of random timing and velocity variation.
enum class Humanise { Off, Light, Medium, Heavy };

/// The playing feel of the whole progression. Defaults: block chords, no strum, medium speed, no humanise.
struct Feel {
    Rhythm rhythm = Rhythm::Block;
    Strum strum = Strum::Off;
    StrumSpeed speed = StrumSpeed::Medium;
    Humanise humanise = Humanise::Off;
};

inline bool operator==(const Feel& a, const Feel& b) {
    return a.rhythm == b.rhythm && a.strum == b.strum && a.speed == b.speed && a.humanise == b.humanise;
}

inline bool operator!=(const Feel& a, const Feel& b) { return !(a == b); }

/// Shortest gated note, in ticks (D6).
constexpr int kMinNoteTicks = 5;

/// One hit: tick span after the gate, the voicing indices it plays (ascending pitch order), its velocity
/// before humanise, and whether it is on a pulse or the chord's start (for the down-up strum).
struct Hit {
    int start;
    int end;
    std::vector<int> notes;
    int velocity;
    bool strong;
};

/// The hits of a chord spanning ticks [s, e) of the whole progression (D4-D7). Bars count from tick 0 of the
/// progression. Empty when the span or `noteCount` is empty.
std::vector<Hit> rhythmHits(Rhythm rhythm, Meter meter, int s, int e, int noteCount, int velocity);

}  // namespace chordlab
