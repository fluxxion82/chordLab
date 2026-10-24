#include "chordlab/voicing.h"

#include "chordlab/scale.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdlib>

namespace chordlab {

namespace {

/// The middle-C octave for the root (see the header); must match the header's default arguments.
constexpr int kDefaultLowest = 60;
constexpr int kDefaultHighest = 96;
constexpr int kOctave = 12;
constexpr int kMinRange = 2 * kOctave;
constexpr int kMidiMax = 127;

/// True if `lowest`..`highest` is a usable range: both ends inside MIDI and at least two octaves wide.
/// Both endpoints are checked before `highest - lowest` is formed, so the subtraction cannot overflow.
bool validRange(int lowest, int highest) {
    if (lowest < 0 || lowest > kMidiMax || highest < 0 || highest > kMidiMax) {
        return false;
    }
    return highest - lowest >= kMinRange;
}

/// True if `lowest`..`highest` is usable by `voicing`: a `validRange` with an octave of MIDI below it
/// (octave -1) and two above it (a moved note at octave +1).
bool validVoicingRange(int lowest, int highest) {
    return validRange(lowest, highest) && lowest >= kOctave && highest <= kMidiMax - 2 * kOctave;
}

/// `voiceLeadingCost`'s precondition: one to `kMaxVoiceLeadingNotes` MIDI notes.
[[maybe_unused]] bool isMidiVoicing(const std::vector<int>& notes) {
    return !notes.empty() && notes.size() <= static_cast<std::size_t>(kMaxVoiceLeadingNotes) &&
           std::all_of(notes.begin(), notes.end(), [](int n) { return n >= 0 && n <= kMidiMax; });
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

int inversionCount(const Chord& chord) {
    assert(chord.type != nullptr);
    bool seen[12] = {};
    int count = 0;
    for (const PitchClass pc : pitchClasses(chord)) {
        if (!seen[static_cast<std::size_t>(pc)]) {
            seen[static_cast<std::size_t>(pc)] = true;
            ++count;
        }
    }
    return count;
}

std::vector<int> voicing(const Chord& chord, int inversion, int octave, int lowest, int highest) {
    assert(chord.type != nullptr);
    if (!validVoicingRange(lowest, highest)) {
        lowest = kDefaultLowest;
        highest = kDefaultHighest;
    }
    std::vector<int> notes = closeVoicing(chord, lowest, highest);
    assert(!notes.empty());
    // Every dictionary type stacks distinct pitch classes, so the close voicing holds one note per chord
    // tone and `inversionCount` is its size. The clamp uses the size, which keeps the rotation in bounds
    // even for a type that folded two intervals onto one note.
    assert(static_cast<int>(notes.size()) == inversionCount(chord));
    inversion = std::clamp(inversion, 0, static_cast<int>(notes.size()) - 1);
    octave = std::clamp(octave, kMinVoicingOctave, kMaxVoicingOctave);
    // The lowest `inversion` notes go up an octave, so that chord tone `inversion` is the bass. A chord
    // that spans more than an octave (a ninth, a sharp eleventh) needs a second lift for the tones still
    // under the new bass: the close voicing spans less than two octaves, so two lifts always clear it.
    const int bass = notes[static_cast<std::size_t>(inversion)];
    for (std::size_t i = 0; i < static_cast<std::size_t>(inversion); ++i) {
        notes[i] += kOctave;
        while (notes[i] < bass) {
            notes[i] += kOctave;
        }
    }
    for (int& note : notes) {
        note += kOctave * octave;
    }
    std::sort(notes.begin(), notes.end());
    notes.erase(std::unique(notes.begin(), notes.end()), notes.end());
    assert(notes.front() >= lowest + kOctave * octave);
    assert(notes.front() == bass + kOctave * octave);
    // The register bound of the header: a lifted note ends under an octave above the bass, and the bass is
    // at most the root (below `lowest + 12`) plus the type's largest interval.
    [[maybe_unused]] const int span = chord.type->intervals.back();
    assert(notes.back() <= std::min(highest + kOctave - 1, lowest + span + 22) + kOctave * octave);
    assert(notes.front() >= 0 && notes.back() <= kMidiMax);  // what `validVoicingRange` guarantees
    return notes;
}

int voiceLeadingCost(const std::vector<int>& from, const std::vector<int>& to) {
    assert(isMidiVoicing(from) && isMidiVoicing(to));
    std::vector<int> a(from);
    std::vector<int> b(to);
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    // Each missing voice of the shorter voicing is the octave above its lowest note, inserted in order so
    // that the sorted pairing below stays the optimal one.
    const auto pad = [](std::vector<int>& shorter, std::size_t size) {
        const int doubling = shorter.front() + kOctave;
        while (shorter.size() < size) {
            shorter.insert(std::upper_bound(shorter.begin(), shorter.end(), doubling), doubling);
        }
    };
    pad(a, b.size());
    pad(b, a.size());
    assert(a.size() == b.size());
    // For one-dimensional points and an absolute-difference cost the pairing in ascending order is a
    // minimum-cost pairing: swapping any two crossed voices never lowers the total.
    int cost = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        cost += std::abs(a[i] - b[i]);
    }
    return cost;
}

}  // namespace chordlab
