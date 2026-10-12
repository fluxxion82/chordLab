#include "chordlab/harmony.h"

#include "chordlab/relations.h"
#include "chordlab/voicing.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdlib>
#include <iterator>
#include <numeric>

namespace chordlab {

namespace {

constexpr int kOctave = 12;
constexpr int kMidiMax = 127;

/// The parallel modes in the order `borrowedFrom` tries them: the minor family first, the parallel
/// major last. Every mode appears once.
constexpr std::array<Mode, kNumModes> kBorrowOrder{
    Mode::Aeolian,       Mode::Dorian,       Mode::Mixolydian, Mode::Lydian, Mode::Phrygian,
    Mode::HarmonicMinor, Mode::MelodicMinor, Mode::Locrian,    Mode::Ionian,
};

bool isMidiVoicing(const std::vector<int>& notes) {
    return !notes.empty() && notes.size() <= static_cast<std::size_t>(kMaxVoiceLeadingNotes) &&
           std::all_of(notes.begin(), notes.end(), [](int n) { return n >= 0 && n <= kMidiMax; });
}

/// Appends the octave above the lowest note of `notes` until it holds `size` notes.
void padWithLowestOctave(std::vector<int>& notes, std::size_t size) {
    if (notes.size() >= size) {
        return;
    }
    const int doubling = *std::min_element(notes.begin(), notes.end()) + kOctave;
    notes.resize(size, doubling);
}

}  // namespace

const char* rootMotionName(RootMotion motion) {
    switch (motion) {
        case RootMotion::Same: return "same root";
        case RootMotion::DownFifth: return "down a fifth";
        case RootMotion::UpFifth: return "up a fifth";
        case RootMotion::StepUp: return "up a step";
        case RootMotion::StepDown: return "down a step";
        case RootMotion::ThirdUp: return "up a third";
        case RootMotion::ThirdDown: return "down a third";
        case RootMotion::Tritone: return "tritone";
        case RootMotion::SemitoneUp: return "up a semitone";
        case RootMotion::SemitoneDown: return "down a semitone";
    }
    assert(false && "chordlab: RootMotion outside the enum");
    return "";
}

RootMotion rootMotion(PitchClass from, PitchClass to) {
    // Reduce each root before subtracting so that any int is accepted without overflow.
    switch (pitchClassOf(pitchClassOf(to) - pitchClassOf(from))) {
        case 0: return RootMotion::Same;
        case 1: return RootMotion::SemitoneUp;
        case 2: return RootMotion::StepUp;
        case 3:
        case 4: return RootMotion::ThirdUp;
        case 5: return RootMotion::DownFifth;
        case 6: return RootMotion::Tritone;
        case 7: return RootMotion::UpFifth;
        case 8:
        case 9: return RootMotion::ThirdDown;
        case 10: return RootMotion::StepDown;
        default: return RootMotion::SemitoneDown;  // 11
    }
}

int voiceLeadingCost(const std::vector<int>& a, const std::vector<int>& b) {
    assert(isMidiVoicing(a) && isMidiVoicing(b));
    std::vector<int> from = a;
    std::vector<int> to = b;
    padWithLowestOctave(from, to.size());
    padWithLowestOctave(to, from.size());
    const std::size_t n = from.size();
    // Every bijection is a permutation of `to`'s indices; n <= kMaxVoiceLeadingNotes keeps n! small.
    std::vector<std::size_t> order(n);
    std::iota(order.begin(), order.end(), std::size_t{0});
    int best = INT_MAX;
    do {
        int cost = 0;
        for (std::size_t i = 0; i < n; ++i) {
            cost += std::abs(from[i] - to[order[i]]);
        }
        best = std::min(best, cost);
    } while (std::next_permutation(order.begin(), order.end()));
    return best;
}

std::optional<Key> borrowedFrom(const Chord& chord, const Key& projectKey) {
    assert(chord.type != nullptr);
    if (fullyInKey(chord, projectKey)) {
        return std::nullopt;
    }
    const std::vector<Key> modes = parallelModes(projectKey);
    for (const Mode mode : kBorrowOrder) {
        if (mode == projectKey.mode) {
            continue;
        }
        const Key& candidate = modes[static_cast<std::size_t>(mode)];
        if (fullyInKey(chord, candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}

Relation relate(const Chord& from, const Chord& to, const Key& projectKey) {
    assert(from.type != nullptr && to.type != nullptr);
    std::vector<PitchClass> fromPcs = pitchClasses(from);
    std::vector<PitchClass> toPcs = pitchClasses(to);
    std::sort(fromPcs.begin(), fromPcs.end());
    std::sort(toPcs.begin(), toPcs.end());
    Relation relation;
    std::set_intersection(fromPcs.begin(), fromPcs.end(), toPcs.begin(), toPcs.end(),
                          std::back_inserter(relation.commonPcs));
    relation.commonTones = static_cast<int>(relation.commonPcs.size());
    relation.motion = rootMotion(from.root, to.root);
    relation.voiceLeadingCost = voiceLeadingCost(closeVoicing(from), closeVoicing(to));
    relation.borrowedFrom = borrowedFrom(to, projectKey);
    return relation;
}

}  // namespace chordlab
