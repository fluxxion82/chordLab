#pragma once

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

}  // namespace chordlab
