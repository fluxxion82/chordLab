#include "chordlab/voicing.h"

#include "chordlab/scale.h"

#include <algorithm>
#include <cassert>

namespace chordlab {

namespace {

constexpr int kDefaultLowest = 48;
constexpr int kDefaultHighest = 84;
constexpr int kOctave = 12;
constexpr int kMinRange = 2 * kOctave;
constexpr int kMidiMax = 127;

/// True if `lowest`..`highest` is a usable range: inside MIDI and at least two octaves wide.
bool validRange(int lowest, int highest) {
    return lowest >= 0 && highest <= kMidiMax && highest - lowest >= kMinRange;
}

}  // namespace

std::vector<int> closeVoicing(const Chord& chord, int lowest, int highest) {
    assert(chord.type != nullptr);
    if (!validRange(lowest, highest)) {
        lowest = kDefaultLowest;
        highest = kDefaultHighest;
    }
    const int root = lowest + pitchClassOf(chord.root - lowest);
    std::vector<int> notes;
    notes.reserve(chord.type->intervals.size());
    for (const int interval : chord.type->intervals) {
        int note = root + interval;
        while (note > highest) {
            note -= kOctave;
        }
        assert(note >= lowest);  // a two-octave range always has room for the octave below `highest`
        notes.push_back(note);
    }
    std::sort(notes.begin(), notes.end());
    notes.erase(std::unique(notes.begin(), notes.end()), notes.end());
    return notes;
}

}  // namespace chordlab
