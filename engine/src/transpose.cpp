#include "chordlab/transpose.h"

#include "chordlab/voicing.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>

namespace chordlab {

namespace {

constexpr int kOctave = 12;

/// `value` reduced into 0..11 (also for negative values).
int wrapPitchClass(int value) {
    return ((value % kOctave) + kOctave) % kOctave;
}

/// What moving one event by `semitones` does to its root and stored octave.
struct Moved {
    int root;
    int octave;     // after clamping into kMinVoicingOctave..kMaxVoicingOctave
    bool clamped;   // the octave the root crossed did not fit
};

/// The new root of `event` and the octave that keeps every note exactly `semitones` away. `voicing`
/// places the root at 60 + root, so a root that wraps past B or below C leaves the 60..71 window by one
/// octave, and the octave field takes that octave: k = (root + semitones - newRoot) / 12, exact because
/// the numerator is a multiple of 12.
Moved move(const ChordEvent& event, int semitones) {
    const int root = wrapPitchClass(event.chord.root + semitones);
    const int crossed = (event.chord.root + semitones - root) / kOctave;
    const int wanted = event.octave + crossed;
    const int octave = std::clamp(wanted, kMinVoicingOctave, kMaxVoicingOctave);
    return Moved{root, octave, octave != wanted};
}

/// How one candidate move scores, compared in this order (lower is better).
struct Score {
    int clamped = 0;
    int largestOctave = 0;
    int octaveSum = 0;
    int size = 0;
};

bool operator<(const Score& a, const Score& b) {
    if (a.clamped != b.clamped) {
        return a.clamped < b.clamped;
    }
    if (a.largestOctave != b.largestOctave) {
        return a.largestOctave < b.largestOctave;
    }
    if (a.octaveSum != b.octaveSum) {
        return a.octaveSum < b.octaveSum;
    }
    return a.size < b.size;
}

Score scoreOf(const std::vector<ChordEvent>& events, int semitones) {
    Score score;
    score.size = std::abs(semitones);
    for (const ChordEvent& event : events) {
        const Moved moved = move(event, semitones);
        score.clamped += moved.clamped ? 1 : 0;
        score.largestOctave = std::max(score.largestOctave, std::abs(moved.octave));
        score.octaveSum += std::abs(moved.octave);
    }
    return score;
}

}  // namespace

int transposeEvents(std::vector<ChordEvent>& events, int semitones) {
    int clamped = 0;
    for (ChordEvent& event : events) {
        const Moved moved = move(event, semitones);
        event.chord.root = moved.root;
        event.octave = moved.octave;
        clamped += moved.clamped ? 1 : 0;
    }
    return clamped;
}

int chooseKeyTranspose(const std::vector<ChordEvent>& events, int interval) {
    const int up = wrapPitchClass(interval);
    if (up == 0) {
        return 0;
    }
    const int down = up - kOctave;
    // `up` is tried first and `down` must be strictly better, so a full tie goes up.
    return scoreOf(events, down) < scoreOf(events, up) ? down : up;
}

}  // namespace chordlab
