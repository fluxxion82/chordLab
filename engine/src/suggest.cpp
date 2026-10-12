#include "chordlab/suggest.h"

#include "chordlab/applied.h"
#include "chordlab/function.h"
#include "chordlab/harmony.h"
#include "chordlab/palette.h"
#include "chordlab/scale.h"
#include "chordlab/symbol.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace chordlab {

namespace {

// --- Weights, in hundredths of a score point (see suggest.h) -----------------------------------

constexpr int kFunctionCount = 4;  // Tonic, Predominant, Dominant, Other, in enum order

using TransitionTable = std::array<std::array<int, kFunctionCount>, kFunctionCount>;

/// Pop: rows are the previous chord's function, columns the candidate's.
constexpr TransitionTable kPopTransition{{
    //  T    PD   D    O
    {{30, 80, 60, 20}},   // from Tonic
    {{40, 30, 90, 20}},   // from Predominant
    {{100, 30, 40, 20}},  // from Dominant
    {{20, 20, 20, 20}},   // from Other
}};

/// Opening scores by function when there is no previous chord.
constexpr std::array<int, kFunctionCount> kOpening{{100, 60, 50, 20}};

/// Pop degree prior (see suggest.h), in hundredths, indexed by semitones above the tonic, which is the
/// numeral `numeralDegree` names: I bII ii bIII iii IV #IV V bVI vi bVII vii°.
using PriorTable = std::array<int, 12>;
constexpr PriorTable kPopPrior{{100, 20, 60, 40, 35, 90, 10, 90, 50, 80, 50, 10}};

constexpr int kCommonToneWeight = 10;
constexpr int kCommonTonesCounted = 2;  // further shared tones are the previous chord dressed up
constexpr int kVoiceLeadingWeight = 5;
constexpr int kCadenceBonus = 30;
constexpr int kInKeyBonus = 20;
constexpr int kTriadBonus = 25;
constexpr int kPowerChordPenalty = 50;
constexpr int kExtensionPenalty = 25;  // per chord tone beyond the third
constexpr int kPriorWeight = 100;      // percent of the prior table
constexpr int kAppliedBonus = 20;
constexpr int kAppliedSetupBonus = 15;  // V7/V or V7/ii after a tonic or predominant chord
constexpr int kAppliedPriorShare = 50;  // percent of the target's prior
constexpr int kHomeBonus = 10;

/// Thresholds for the CommonTones and Smooth reasons.
constexpr int kNotableCommonTones = 2;
constexpr int kSmoothCost = 2;

const TransitionTable& transitionTable(Style style) {
    switch (style) {
        case Style::Pop:
            return kPopTransition;
    }
    assert(false && "Style outside the enum");
    return kPopTransition;
}

const PriorTable& priorTable(Style style) {
    switch (style) {
        case Style::Pop:
            return kPopPrior;
    }
    assert(false && "Style outside the enum");
    return kPopPrior;
}

/// The prior of `chord`'s degree in `projectKey`: its numeral's semitone distance above the tonic.
int priorOf(const Chord& chord, const Key& projectKey, const PriorTable& table) {
    constexpr std::array<int, 7> kMajorScale{{0, 2, 4, 5, 7, 9, 11}};
    const NumeralDegree numeral = numeralDegree(chord, projectKey);
    const int semitones = pitchClassOf(kMajorScale[static_cast<std::size_t>(numeral.degree)] + numeral.alter);
    return table[static_cast<std::size_t>(semitones)] * kPriorWeight / 100;
}

constexpr std::size_t index(Function function) {
    return static_cast<std::size_t>(function);
}

// --- Candidates -------------------------------------------------------------------------------

struct Candidate {
    Chord chord;
    std::string label;
    std::string roman;
    /// The applied chord this candidate came from, or nullptr for a palette cell or a borrowed triad.
    const AppliedChord* applied;
};

bool listed(const std::vector<Candidate>& candidates, const Chord& chord) {
    return std::any_of(candidates.begin(), candidates.end(),
                       [&](const Candidate& c) { return c.chord == chord; });
}

/// Palette cells, then the applied chords, then the four borrowed triads; a chord listed twice keeps
/// its first entry. `applied` must outlive the result.
std::vector<Candidate> collectCandidates(const Key& projectKey, const Key& browsedKey,
                                         const std::vector<AppliedChord>& applied) {
    std::vector<Candidate> out;
    for (const PaletteCell& cell : buildPalette(browsedKey, projectKey).cells) {
        if (!listed(out, cell.chord)) {
            out.push_back(Candidate{cell.chord, cell.symbol, cell.roman, nullptr});
        }
    }
    for (const AppliedChord& a : applied) {
        if (!listed(out, a.chord)) {
            out.push_back(Candidate{a.chord, chordSymbol(a.chord, a.spellingKey, SymbolStyle::Klimper), a.label, &a});
        }
    }
    constexpr std::array<int, 4> kBorrowedDegrees{{1, 3, 8, 10}};  // bII, bIII, bVI, bVII above the tonic
    const ChordType& major = chordType("maj");
    for (const int semitones : kBorrowedDegrees) {
        const Chord chord{pitchClassOf(projectKey.tonicPc() + semitones), &major};
        if (!listed(out, chord)) {
            // Spelled in the project key: they are degrees of its tonic (bII of C is Db whatever is browsed).
            out.push_back(Candidate{chord, chordSymbol(chord, projectKey, SymbolStyle::Klimper),
                                    romanNumeral(chord, projectKey, SymbolStyle::Standard), nullptr});
        }
    }
    return out;
}

// --- Text -------------------------------------------------------------------------------------

constexpr std::array<const char*, kNumModes> kModeText{{"Ionian", "Dorian", "Phrygian", "Lydian", "Mixolydian",
                                                        "Aeolian", "Locrian", "harmonic minor", "melodic minor"}};

std::string keyText(const Key& key) {
    return Spelling{key.tonic.letter, key.tonic.alter}.text() + " " + kModeText[static_cast<std::size_t>(key.mode)];
}

std::string commonTonesText(const std::vector<PitchClass>& pcs, const Key& projectKey) {
    std::string text = std::to_string(pcs.size()) + " common tones (";
    for (std::size_t i = 0; i < pcs.size(); ++i) {
        if (i > 0) {
            text += ", ";
        }
        text += spell(pcs[i], projectKey).text();
    }
    return text + ")";
}

std::string appliedText(const AppliedChord& applied) {
    const std::string_view label = applied.label;
    const std::size_t slash = label.find('/');
    const std::string target(label.substr(slash + 1));
    const bool dominant = label.substr(0, slash) == "V7";
    return (dominant ? "secondary dominant of " : "leading-tone chord of ") + target + " (" + applied.label + ")";
}

bool isTriad(const ChordType& type) {
    const std::string_view id = type.id;
    return id == "maj" || id == "min" || id == "dim" || id == "aug";
}

/// "Prefer simpler": a plain triad earns the bonus, a power chord pays for being incomplete, a
/// suspended chord is neutral, and every chord tone beyond the third costs a step, so a seventh is
/// 0.25 and a ninth 0.5 below neutral.
int simplicity(const ChordType& type) {
    if (isTriad(type)) {
        return kTriadBonus;
    }
    if (std::string_view(type.id) == "5") {
        return -kPowerChordPenalty;
    }
    const int extra = static_cast<int>(type.intervals.size()) - 3;
    return extra > 0 ? -kExtensionPenalty * extra : 0;
}

/// Whether `applied` is a secondary dominant (`V7/x`) rather than a leading-tone chord (`vii°7/x`).
bool isSecondaryDominant(const AppliedChord& applied) {
    return std::string_view(applied.label).substr(0, 3) == "V7/";
}

/// Whether `previous` -> `applied` is one of the common setups the extra bonus rewards: a secondary
/// dominant of V or of ii after a tonic or predominant chord (I -> V7/V -> V, IV -> V7/ii -> ii).
bool isSetup(Function previous, const AppliedChord& applied) {
    constexpr int kSupertonic = 1, kDominant = 4;  // target degrees, 0-based
    return (previous == Function::Tonic || previous == Function::Predominant) && isSecondaryDominant(applied) &&
           (applied.targetDegree == kDominant || applied.targetDegree == kSupertonic);
}

// --- Scoring ----------------------------------------------------------------------------------

/// What `suggestNext` knows about the previous chord, computed once.
struct Previous {
    const Chord* chord;
    Function function;
    std::string roman;
};

void addReason(Suggestion& s, Reason reason, std::string text) {
    s.reasons.push_back(reason);
    s.reasonTexts.push_back(std::move(text));
}

Suggestion score(const Candidate& candidate, const Previous& previous, const Key& projectKey,
                 const TransitionTable& table, const PriorTable& prior) {
    Suggestion s{candidate.chord, candidate.label, candidate.roman, 0.0, {}, {}};
    const bool applied = candidate.applied != nullptr;
    const Function function = applied ? Function::Dominant : functionOf(candidate.chord, projectKey);
    const bool inKey = fullyInKey(candidate.chord, projectKey);
    const bool onTonic = candidate.chord.root == projectKey.tonicPc();
    int hundredths = 0;

    if (previous.chord != nullptr) {
        const Relation relation = relate(*previous.chord, candidate.chord, projectKey);
        hundredths += table[index(previous.function)][index(function)];
        hundredths += kCommonToneWeight * std::min(relation.commonTones, kCommonTonesCounted);
        hundredths -= kVoiceLeadingWeight * relation.voiceLeadingCost;
        const bool resolves = previous.function == Function::Dominant && function == Function::Tonic;
        const bool cadence = resolves && inKey && onTonic && isTriad(*candidate.chord.type);
        if (resolves) {
            addReason(s, Reason::Resolves, "resolves (D→T)");
        }
        if (cadence) {
            hundredths += kCadenceBonus;
            addReason(s, Reason::Cadence, "cadence (" + previous.roman + "→" + candidate.roman + ")");
        }
        if (previous.function == Function::Predominant && function == Function::Dominant) {
            addReason(s, Reason::Prepares, "prepares (PD→D)");
        }
        if (relation.commonTones >= kNotableCommonTones) {
            addReason(s, Reason::CommonTones, commonTonesText(relation.commonPcs, projectKey));
        }
        if (relation.voiceLeadingCost <= kSmoothCost) {
            addReason(s, Reason::Smooth, "smooth voice leading (cost " + std::to_string(relation.voiceLeadingCost) + ")");
        }
    } else {
        hundredths += kOpening[index(function)];
        if (onTonic) {
            hundredths += kHomeBonus;
        }
    }

    if (inKey) {
        hundredths += kInKeyBonus;
        addReason(s, Reason::Diatonic, "diatonic");
    } else if (!applied) {  // an applied chord's foreign notes are explained by its target, not by a mode
        if (const std::optional<Key> source = borrowedFrom(candidate.chord, projectKey)) {
            addReason(s, Reason::Borrowed, "borrowed from " + keyText(*source));
        }
    }
    hundredths += simplicity(*candidate.chord.type);
    if (applied) {
        // No prior of its own: a tonicisation borrows half of its target's.
        const Chord target = diatonicTriad(projectKey, candidate.applied->targetDegree);
        hundredths += kAppliedBonus + (priorOf(target, projectKey, prior) * kAppliedPriorShare + 50) / 100;
        if (previous.chord != nullptr && isSetup(previous.function, *candidate.applied)) {
            hundredths += kAppliedSetupBonus;
        }
        addReason(s, Reason::AppliedDominant, appliedText(*candidate.applied));
    } else {
        hundredths += priorOf(candidate.chord, projectKey, prior);
    }
    switch (function) {
        case Function::Tonic:
            addReason(s, Reason::Tonic, "tonic");
            break;
        case Function::Predominant:
            addReason(s, Reason::Predominant, "predominant");
            break;
        case Function::Dominant:
            addReason(s, Reason::Dominant, "dominant");
            break;
        case Function::Other:
            break;
    }

    s.score = hundredths / 100.0;
    return s;
}

/// Score descending, then label ascending; the chord itself last, for a total order even if two
/// labels ever coincided.
bool before(const Suggestion& a, const Suggestion& b) {
    if (a.score != b.score) {
        return a.score > b.score;
    }
    if (a.label != b.label) {
        return a.label < b.label;
    }
    if (a.chord.root != b.chord.root) {
        return a.chord.root < b.chord.root;
    }
    return std::string_view(a.chord.type->id) < std::string_view(b.chord.type->id);
}

}  // namespace

std::vector<Suggestion> suggestNext(const Chord* previous, const Key& projectKey, const Key& browsedKey, Style style,
                                    std::size_t maxCount) {
    assert(previous == nullptr || (previous->type != nullptr && previous->root >= 0 && previous->root < 12));
    const TransitionTable& table = transitionTable(style);
    const PriorTable& prior = priorTable(style);
    const std::vector<AppliedChord> applied = appliedChords(projectKey);
    const std::vector<Candidate> candidates = collectCandidates(projectKey, browsedKey, applied);

    Previous prev{previous, Function::Other, {}};
    if (previous != nullptr) {
        prev.function = functionOf(*previous, projectKey);
        prev.roman = romanNumeral(*previous, projectKey, SymbolStyle::Standard);
    }

    std::vector<Suggestion> out;
    out.reserve(candidates.size());
    for (const Candidate& candidate : candidates) {
        if (previous != nullptr && candidate.chord == *previous) {
            continue;
        }
        out.push_back(score(candidate, prev, projectKey, table, prior));
    }
    std::sort(out.begin(), out.end(), before);
    if (out.size() > maxCount) {
        out.resize(maxCount);
    }
    return out;
}

}  // namespace chordlab
