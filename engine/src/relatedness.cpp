#include "chordlab/relatedness.h"

#include "chordlab/function.h"
#include "chordlab/harmony.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <cstddef>

namespace chordlab {

namespace {

constexpr int kFull = 100;
constexpr int kVoiceLeadingStep = 10;  // score lost per unit of pitch-class voice-leading cost

/// Wire names in enum order.
constexpr std::array<const char*, kNumRelationModes> kModeNames{
    {"off", "commonTones", "voiceLeading", "function", "blend"}};

/// `numerator / denominator` rounded half up; both non-negative, `denominator` positive.
int roundedRatio(int numerator, int denominator) {
    return (2 * numerator + denominator) / (2 * denominator);
}

/// Number of distinct pitch classes of `chord`.
int distinctPcs(const Chord& chord) {
    std::vector<PitchClass> pcs = pitchClasses(chord);
    std::sort(pcs.begin(), pcs.end());
    return static_cast<int>(std::unique(pcs.begin(), pcs.end()) - pcs.begin());
}

/// The score of `candidate` under a per-candidate mode (everything but `Off` and `Blend`), given its
/// relation to the reference and the reference's distinct pitch-class count and function.
int singleScore(const Chord& candidate, const Relation& relation, int referencePcs,
                Function referenceFunction, const Key& projectKey, RelationMode mode, Style style) {
    switch (mode) {
        case RelationMode::CommonTones:
            return roundedRatio(kFull * relation.commonTones, std::max(referencePcs, distinctPcs(candidate)));
        case RelationMode::VoiceLeading:
            return std::max(0, kFull - kVoiceLeadingStep * relation.voiceLeadingCost);
        case RelationMode::Function:
            return transitionHundredths(referenceFunction, functionOf(candidate, projectKey), style);
        case RelationMode::Off:
        case RelationMode::Blend:
            break;
    }
    assert(false && "singleScore: not a per-candidate mode");
    return 0;
}

}  // namespace

const char* relationModeName(RelationMode mode) {
    const auto i = static_cast<std::size_t>(mode);
    assert(i < kModeNames.size() && "RelationMode outside the enum");
    return i < kModeNames.size() ? kModeNames[i] : "";
}

std::optional<RelationMode> parseRelationMode(std::string_view name) {
    for (std::size_t i = 0; i < kModeNames.size(); ++i) {
        if (name == kModeNames[i]) {
            return static_cast<RelationMode>(i);
        }
    }
    return std::nullopt;
}

std::vector<Relatedness> relatedness(const Chord& reference, const std::vector<Chord>& candidates,
                                     const Key& projectKey, const Key& browsedKey, RelationMode mode, Style style) {
    return relatedness(reference, candidates, {}, projectKey, browsedKey, mode, style);
}

std::vector<Relatedness> relatedness(const Chord& reference, const std::vector<Chord>& candidates,
                                     const std::vector<Chord>& extras, const Key& projectKey, const Key& browsedKey,
                                     RelationMode mode, Style style) {
    assert(reference.type != nullptr && reference.root >= 0 && reference.root < 12);
    std::vector<Relatedness> out;
    if (mode == RelationMode::Off) {
        return out;
    }
    out.reserve(candidates.size() + extras.size());

    if (mode == RelationMode::Blend) {
        const std::vector<int> raw = suggestionHundredths(&reference, candidates, projectKey, browsedKey, style);
        int lo = INT_MAX;
        int hi = INT_MIN;
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            if (candidates[i] != reference) {  // the reference is 100 and stays out of the range
                lo = std::min(lo, raw[i]);
                hi = std::max(hi, raw[i]);
            }
        }
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            const bool full = candidates[i] == reference || hi == lo;
            const int score = full ? kFull : roundedRatio(kFull * (raw[i] - lo), hi - lo);
            out.push_back(Relatedness{score, relationText(relate(reference, candidates[i], projectKey), projectKey)});
        }
        // Extras are placed on the population's scale without widening it: clamped at both ends, and with
        // no range to place them on (a tie, one chord, none) at or above it is 100 and below it 0.
        const std::vector<int> extraRaw = suggestionHundredths(&reference, extras, projectKey, browsedKey, style);
        for (std::size_t i = 0; i < extras.size(); ++i) {
            const int r = extraRaw[i];
            int score = 0;
            if (extras[i] == reference || r >= hi) {  // hi is INT_MIN when the range is empty
                score = kFull;
            } else if (r <= lo) {
                score = 0;
            } else {
                score = roundedRatio(kFull * (r - lo), hi - lo);
            }
            out.push_back(Relatedness{score, relationText(relate(reference, extras[i], projectKey), projectKey)});
        }
        return out;
    }

    const int referencePcs = distinctPcs(reference);
    const Function referenceFunction = functionOf(reference, projectKey);
    for (const std::vector<Chord>* list : {&candidates, &extras}) {
        for (const Chord& candidate : *list) {
            const Relation relation = relate(reference, candidate, projectKey);
            const bool same = candidate == reference && mode != RelationMode::Function;
            const int score = same ? kFull
                                   : singleScore(candidate, relation, referencePcs, referenceFunction, projectKey,
                                                 mode, style);
            out.push_back(Relatedness{score, relationText(relation, projectKey)});
        }
    }
    return out;
}

}  // namespace chordlab
