#include "chordlab/feel.h"

#include <algorithm>

namespace chordlab {

namespace {

int clampVelocity(int v) { return std::min(127, std::max(1, v)); }

// Which indices of an n-note voicing the k-th arpeggio hit plays (D5).
int arpIndex(Rhythm rhythm, int k, int n) {
    if (n <= 1) return 0;
    if (rhythm == Rhythm::ArpUp) return k % n;
    const int period = 2 * (n - 1);
    const int m = k % period;
    return m < n ? m : period - m;
}

std::vector<int> allNotes(int n) {
    std::vector<int> notes(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) notes[static_cast<size_t>(i)] = i;
    return notes;
}

}  // namespace

std::vector<Hit> rhythmHits(Rhythm rhythm, Meter meter, int s, int e, int noteCount, int velocity) {
    std::vector<Hit> hits;
    if (e <= s || noteCount < 1) return hits;

    const int pulse = pulseTicks(meter);
    const int sub = subdivisionTicks(meter);
    const int bar = barTicks(meter);

    // Hit starts: the chord's start plus the grid points strictly inside (s, e), ascending, no duplicates.
    std::vector<int> starts{s};
    switch (rhythm) {
        case Rhythm::Block:
            break;
        case Rhythm::Pulse:
        case Rhythm::LowThenUpper:
            for (int t : gridPointsInside(s, e, pulse)) starts.push_back(t);
            break;
        case Rhythm::Subdivided:
        case Rhythm::ArpUp:
        case Rhythm::ArpUpDown:
            for (int t : gridPointsInside(s, e, sub)) starts.push_back(t);
            break;
        case Rhythm::Offbeats:
            for (int t : gridPointsInside(s, e, sub))
                if (t % pulse != 0) starts.push_back(t);
            break;
    }

    if (rhythm == Rhythm::Block) {
        hits.push_back(Hit{s, e, allNotes(noteCount), velocity, true});
        return hits;
    }

    for (size_t i = 0; i < starts.size(); ++i) {
        const int t = starts[i];
        const int next = i + 1 < starts.size() ? starts[i + 1] : e;
        const int gap = next - t;
        const int length = std::max(kMinNoteTicks, gap * 9 / 10);

        const bool chordStart = t == s;
        const bool barStart = t % bar == 0;
        const bool onPulse = t % pulse == 0;

        int v;
        if (chordStart || barStart)
            v = velocity;
        else if (onPulse)
            v = (velocity * 9 + 5) / 10;
        else
            v = (velocity * 8 + 5) / 10;

        std::vector<int> notes;
        if (rhythm == Rhythm::LowThenUpper) {
            if (noteCount == 1)
                notes = {0};
            else if (chordStart || barStart)
                notes = {0};
            else
                for (int k = 1; k < noteCount; ++k) notes.push_back(k);
        } else if (rhythm == Rhythm::ArpUp || rhythm == Rhythm::ArpUpDown) {
            notes = {arpIndex(rhythm, static_cast<int>(i), noteCount)};
        } else {
            notes = allNotes(noteCount);
        }

        hits.push_back(Hit{t, t + length, std::move(notes), clampVelocity(v), chordStart || onPulse});
    }
    return hits;
}

}  // namespace chordlab
