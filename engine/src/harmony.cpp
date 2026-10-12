#include "chordlab/harmony.h"

#include "chordlab/relations.h"

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

/// The parallel modes in the order `borrowedFrom` tries them for a major-family project key: the
/// parallel minor first, then the other major modes, the minor modes, Locrian, and (never reached for
/// an Ionian key, which is skipped) Ionian. Every mode appears once.
constexpr std::array<Mode, kNumModes> kMajorFamilyBorrowOrder{
    Mode::Aeolian,       Mode::Mixolydian,   Mode::Lydian,  Mode::Dorian, Mode::Phrygian,
    Mode::HarmonicMinor, Mode::MelodicMinor, Mode::Locrian, Mode::Ionian,
};

/// The order for a minor-family or Locrian project key: the altered minor scales (so V in minor is
/// harmonic minor's), then the parallel major, then the remaining modes. Every mode appears once.
constexpr std::array<Mode, kNumModes> kMinorFamilyBorrowOrder{
    Mode::HarmonicMinor, Mode::MelodicMinor, Mode::Ionian, Mode::Aeolian, Mode::Dorian,
    Mode::Mixolydian,    Mode::Phrygian,     Mode::Lydian, Mode::Locrian,
};

/// True for the modes with a major third and a perfect fifth on the tonic (P2-D1's major family).
bool isMajorFamily(Mode mode) {
    return mode == Mode::Ionian || mode == Mode::Lydian || mode == Mode::Mixolydian;
}

const std::array<Mode, kNumModes>& borrowOrder(Mode projectMode) {
    return isMajorFamily(projectMode) ? kMajorFamilyBorrowOrder : kMinorFamilyBorrowOrder;
}

bool withinVoiceLimit(std::size_t count) {
    return count > 0 && count <= static_cast<std::size_t>(kMaxVoiceLeadingNotes);
}

/// Appends copies of `pcs`'s own entries, cycling from the first, until it holds `size` entries.
void padByCycling(std::vector<PitchClass>& pcs, std::size_t size) {
    const std::size_t original = pcs.size();
    for (std::size_t i = 0; pcs.size() < size; ++i) {
        pcs.push_back(pcs[i % original]);
    }
}

/// Shortest way round the circle between two pitch classes in 0..11.
int circularDistance(PitchClass a, PitchClass b) {
    const int d = std::abs(a - b);
    return std::min(d, kOctave - d);
}

/// The minimum over every bijection of `n` sources onto `n` targets of the summed `cost(i, j)`, by
/// trying each permutation of the targets; `n` is at most `kMaxVoiceLeadingNotes`, so n! stays small.
template <typename Cost>
int minimalBijectionCost(std::size_t n, Cost cost) {
    std::vector<std::size_t> order(n);
    std::iota(order.begin(), order.end(), std::size_t{0});
    int best = INT_MAX;
    do {
        int total = 0;
        for (std::size_t i = 0; i < n; ++i) {
            total += cost(i, order[i]);
        }
        best = std::min(best, total);
    } while (std::next_permutation(order.begin(), order.end()));
    return best;
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

int pcVoiceLeadingCost(const std::vector<PitchClass>& a, const std::vector<PitchClass>& b) {
    assert(withinVoiceLimit(a.size()) && withinVoiceLimit(b.size()));
    std::vector<PitchClass> from(a.size());
    std::vector<PitchClass> to(b.size());
    std::transform(a.begin(), a.end(), from.begin(), [](int pc) { return pitchClassOf(pc); });
    std::transform(b.begin(), b.end(), to.begin(), [](int pc) { return pitchClassOf(pc); });
    padByCycling(from, to.size());
    padByCycling(to, from.size());
    return minimalBijectionCost(from.size(), [&](std::size_t i, std::size_t j) {
        return circularDistance(from[i], to[j]);
    });
}

std::optional<Key> borrowedFrom(const Chord& chord, const Key& projectKey) {
    assert(chord.type != nullptr);
    if (fullyInKey(chord, projectKey)) {
        return std::nullopt;
    }
    const std::vector<Key> modes = parallelModes(projectKey);
    for (const Mode mode : borrowOrder(projectKey.mode)) {
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
    const std::vector<PitchClass> fromPcs = pitchClasses(from);
    const std::vector<PitchClass> toPcs = pitchClasses(to);
    std::vector<PitchClass> fromSorted = fromPcs;
    std::vector<PitchClass> toSorted = toPcs;
    std::sort(fromSorted.begin(), fromSorted.end());
    std::sort(toSorted.begin(), toSorted.end());
    Relation relation;
    std::set_intersection(fromSorted.begin(), fromSorted.end(), toSorted.begin(), toSorted.end(),
                          std::back_inserter(relation.commonPcs));
    relation.commonTones = static_cast<int>(relation.commonPcs.size());
    relation.motion = rootMotion(from.root, to.root);
    relation.voiceLeadingCost = pcVoiceLeadingCost(fromPcs, toPcs);  // root first: the root is doubled first
    relation.borrowedFrom = borrowedFrom(to, projectKey);
    return relation;
}

}  // namespace chordlab
