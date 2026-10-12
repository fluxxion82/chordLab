// suggest_tune: the grid search behind the weights frozen in src/suggest.cpp (not a test).
//
// Build and run:  cmake --build engine/build --target suggest_tune && engine/build/suggest_tune
// It re-implements suggestNext's scoring with the weights as variables (keep `rank` in step with
// suggest.cpp when the formula changes), precomputes every candidate's features once per scenario,
// then walks a grid around the frozen values and scores each point by its *minimum margin*: the
// smallest score gap by which the Pop expectations pinned in tests/test_suggest.cpp hold (after C,
// G7, Dm, F and Am in C major, the opening, and "every key opens with its tonic triad followed by
// diatonic chords only" over all 108 canonical keys). A negative margin means a broken expectation.
// It prints how many grid points pass, the best point and that point's top-nine lists.

#include "chordlab/applied.h"
#include "chordlab/chord.h"
#include "chordlab/function.h"
#include "chordlab/harmony.h"
#include "chordlab/palette.h"
#include "chordlab/pitch.h"
#include "chordlab/scale.h"
#include "chordlab/symbol.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

using namespace chordlab;

namespace {

/// The Pop transition table and degree prior, as src/suggest.cpp has them (score points).
constexpr double kTable[4][4] = {{.3, .8, .6, .2}, {.4, .3, .9, .2}, {1.0, .3, .4, .2}, {.2, .2, .2, .2}};
constexpr double kOpening[4] = {1.0, .6, .5, .2};
constexpr double kPrior[12] = {1.0, .2, .6, .4, .35, .9, .1, .9, .5, .8, .5, .1};

/// The weights under search, in score points. The defaults are the frozen values.
struct Weights {
    double commonTone = .10;
    int commonTonesCounted = 2;
    double voiceLeading = .05;
    double cadence = .30;
    double inKey = .20;
    double triad = .25;
    double powerChord = .50;
    double extension = .25;
    double prior = 1.0;
    double appliedBonus = .20;
    double appliedSetup = .15;
    double appliedPriorShare = .50;
    double home = .10;
};

/// Everything the score of one candidate depends on, computed once.
struct Feature {
    std::string label;
    int function;  // Function enum value; applied chords count as Dominant
    bool inKey;
    bool applied;
    bool secondaryDominant;
    bool setupTarget;  // applied chord targeting V or ii
    bool triad;
    bool power;
    bool onTonic;
    int tones;
    int priorIndex;        // semitones above the tonic
    int targetPriorIndex;  // for an applied chord, its target's
    int commonTones;
    int voiceLeadingCost;
};

struct Scenario {
    std::string name;
    bool hasPrevious;
    int previousFunction;
    std::vector<Feature> features;
    std::string tonicLabel;  // the tonic triad's label, for the opening property
};

int semitonesAbove(PitchClass root, const Key& key) {
    return pitchClassOf(root - key.tonicPc());
}

Scenario buildScenario(std::string name, const Chord* previous, const Key& project, const Key& browsed) {
    Scenario s{std::move(name), previous != nullptr, static_cast<int>(Function::Other), {},
               chordSymbol(diatonicTriad(project, 0), project)};
    if (previous != nullptr) {
        s.previousFunction = static_cast<int>(functionOf(*previous, project));
    }
    const std::vector<AppliedChord> applied = appliedChords(project);
    std::vector<Chord> seen;
    auto add = [&](const Chord& chord, std::string label, const AppliedChord* a) {
        if (std::find(seen.begin(), seen.end(), chord) != seen.end()) {
            return;
        }
        seen.push_back(chord);
        if (previous != nullptr && chord == *previous) {
            return;
        }
        const std::string_view id = chord.type->id;
        Feature f;
        f.label = std::move(label);
        f.applied = a != nullptr;
        f.function = f.applied ? static_cast<int>(Function::Dominant) : static_cast<int>(functionOf(chord, project));
        f.inKey = fullyInKey(chord, project);
        f.secondaryDominant = a != nullptr && std::string_view(a->label).substr(0, 3) == "V7/";
        f.setupTarget = a != nullptr && (a->targetDegree == 4 || a->targetDegree == 1);
        f.triad = id == "maj" || id == "min" || id == "dim" || id == "aug";
        f.power = id == "5";
        f.onTonic = chord.root == project.tonicPc();
        f.tones = static_cast<int>(chord.type->intervals.size());
        f.priorIndex = semitonesAbove(chord.root, project);
        f.targetPriorIndex = a != nullptr ? semitonesAbove(diatonicTriad(project, a->targetDegree).root, project) : 0;
        f.commonTones = 0;
        f.voiceLeadingCost = 0;
        if (previous != nullptr) {
            const Relation r = relate(*previous, chord, project);
            f.commonTones = r.commonTones;
            f.voiceLeadingCost = r.voiceLeadingCost;
        }
        s.features.push_back(std::move(f));
    };
    for (const PaletteCell& cell : buildPalette(browsed, project).cells) {
        add(cell.chord, cell.symbol, nullptr);
    }
    for (const AppliedChord& a : applied) {
        add(a.chord, chordSymbol(a.chord, a.spellingKey), &a);
    }
    for (const int semitones : {1, 3, 8, 10}) {
        const Chord chord{pitchClassOf(project.tonicPc() + semitones), &chordType("maj")};
        add(chord, chordSymbol(chord, project), nullptr);
    }
    return s;
}

struct Ranked {
    double score;
    std::string label;
    bool inKey;
};

int hundredths(double points) {
    return static_cast<int>(std::lround(points * 100));
}

/// suggestNext's score for every feature of `s`, best first. Mirror of src/suggest.cpp.
std::vector<Ranked> rank(const Scenario& s, const Weights& w) {
    std::vector<Ranked> out;
    for (const Feature& f : s.features) {
        int h = 0;
        if (s.hasPrevious) {
            h += hundredths(kTable[s.previousFunction][f.function]);
            h += hundredths(w.commonTone) * std::min(f.commonTones, w.commonTonesCounted);
            h -= hundredths(w.voiceLeading) * f.voiceLeadingCost;
            const bool resolves = s.previousFunction == static_cast<int>(Function::Dominant) &&
                                  f.function == static_cast<int>(Function::Tonic);
            if (resolves && f.inKey && f.onTonic && f.triad) {
                h += hundredths(w.cadence);
            }
        } else {
            h += hundredths(kOpening[f.function]);
            if (f.onTonic) {
                h += hundredths(w.home);
            }
        }
        if (f.inKey) {
            h += hundredths(w.inKey);
        }
        if (f.triad) {
            h += hundredths(w.triad);
        } else if (f.power) {
            h -= hundredths(w.powerChord);
        } else if (f.tones > 3) {
            h -= hundredths(w.extension) * (f.tones - 3);
        }
        if (f.applied) {
            const int targetPrior = hundredths(w.prior * kPrior[f.targetPriorIndex]);
            h += hundredths(w.appliedBonus) + (targetPrior * hundredths(w.appliedPriorShare) + 50) / 100;
            const bool afterTonicOrPredominant = s.hasPrevious && (s.previousFunction == static_cast<int>(Function::Tonic) ||
                                                                   s.previousFunction == static_cast<int>(Function::Predominant));
            if (afterTonicOrPredominant && f.secondaryDominant && f.setupTarget) {
                h += hundredths(w.appliedSetup);
            }
        } else {
            h += hundredths(w.prior * kPrior[f.priorIndex]);
        }
        out.push_back(Ranked{h / 100.0, f.label, f.inKey});
    }
    std::sort(out.begin(), out.end(), [](const Ranked& a, const Ranked& b) {
        return a.score != b.score ? a.score > b.score : a.label < b.label;
    });
    return out;
}

// --- Margins: how far each pinned expectation is from flipping (negative = broken) --------------

constexpr double kAbsent = -9.0;

double scoreOf(const std::vector<Ranked>& r, std::string_view label) {
    for (const Ranked& x : r) {
        if (x.label == label) {
            return x.score;
        }
    }
    return kAbsent;
}

/// `label` within the first `k`: its gap to the chord that would push it out.
double inTop(const std::vector<Ranked>& r, std::string_view label, std::size_t k) {
    const double own = scoreOf(r, label);
    std::size_t others = 0;
    for (const Ranked& x : r) {
        if (x.label == label) {
            continue;
        }
        if (++others == k) {
            return own - x.score;
        }
    }
    return 9.0;
}

/// `label` outside the first `k`: the k-th chord's lead over it.
double notInTop(const std::vector<Ranked>& r, std::string_view label, std::size_t k) {
    return r[k].score - scoreOf(r, label);
}

double first(const std::vector<Ranked>& r, std::string_view label) {
    return inTop(r, label, 1);
}

double minMargin(const Weights& w, const std::vector<Scenario>& scenarios, const std::vector<Scenario>& openings) {
    auto r = [&](std::size_t i) { return rank(scenarios[i], w); };
    const std::vector<Ranked> afterC = r(0), afterG7 = r(1), afterDm = r(2), afterF = r(3), afterAm = r(4), opening = r(5);
    double m = 9.0;
    auto take = [&](double x) { m = std::min(m, x); };
    // after C: F, G and Am in the top three, Em below Am, vii° out of the top five
    take(inTop(afterC, "F", 3));
    take(inTop(afterC, "G", 3));
    take(inTop(afterC, "Am", 3));
    take(scoreOf(afterC, "Am") - scoreOf(afterC, "Em"));
    take(notInTop(afterC, "Bmb5", 5));
    // after G7: C first, Am in the top four
    take(first(afterG7, "C"));
    take(inTop(afterG7, "Am", 4));
    // after Dm: G or G7 first, vii° out of the top three
    take(std::max(first(afterDm, "G"), first(afterDm, "G7")));
    take(notInTop(afterDm, "Bmb5", 3));
    // after F: G first or second, C in the top three, V7/V in the top eight
    take(inTop(afterF, "G", 2));
    take(inTop(afterF, "C", 3));
    take(inTop(afterF, "D7", 8));
    // after Am: F or Dm first, G in the top four
    take(std::max(first(afterAm, "F"), first(afterAm, "Dm")));
    take(inTop(afterAm, "G", 4));
    // opening: C first, then one of F, G, Am within the next three
    take(first(opening, "C"));
    take(std::max({inTop(opening, "F", 4), inTop(opening, "G", 4), inTop(opening, "Am", 4)}));
    // every key: the tonic triad first, the top eight in key
    for (const Scenario& s : openings) {
        const std::vector<Ranked> rr = rank(s, w);
        take(rr[0].label == s.tonicLabel ? rr[0].score - rr[1].score : kAbsent);
        double eighthInKey = 9.0, bestOutOfKey = kAbsent;
        std::size_t inKey = 0;
        for (const Ranked& x : rr) {
            if (x.inKey) {
                if (inKey < 8) {
                    eighthInKey = x.score;
                    ++inKey;
                }
            } else {
                bestOutOfKey = std::max(bestOutOfKey, x.score);
            }
        }
        take(eighthInKey - bestOutOfKey);
    }
    return m;
}

void print(const Scenario& s, const Weights& w) {
    std::printf("  %-9s", s.name.c_str());
    const std::vector<Ranked> r = rank(s, w);
    for (std::size_t i = 0; i < 9 && i < r.size(); ++i) {
        std::printf(" %s %.2f |", r[i].label.c_str(), r[i].score);
    }
    std::printf("\n");
}

}  // namespace

int main() {
    const Key cMajor{{'C', 0}, Mode::Ionian};
    const Chord c{0, &chordType("maj")}, g7{7, &chordType("7")}, dm{2, &chordType("min")}, f{5, &chordType("maj")},
        am{9, &chordType("min")};
    const std::vector<Scenario> scenarios{
        buildScenario("after C", &c, cMajor, cMajor),   buildScenario("after G7", &g7, cMajor, cMajor),
        buildScenario("after Dm", &dm, cMajor, cMajor), buildScenario("after F", &f, cMajor, cMajor),
        buildScenario("after Am", &am, cMajor, cMajor), buildScenario("opening", nullptr, cMajor, cMajor),
    };
    std::vector<Scenario> openings;
    for (PitchClass pc = 0; pc < 12; ++pc) {
        for (int m = 0; m < kNumModes; ++m) {
            const Key k{canonicalTonic(pc, Mode(m)), Mode(m)};
            openings.push_back(buildScenario("opening", nullptr, k, k));
        }
    }

    const Weights frozen;
    std::printf("frozen weights: min margin %.2f\n", minMargin(frozen, scenarios, openings));

    long combos = 0, passing = 0;
    Weights best = frozen;
    double bestMargin = -99.0;
    for (const double prior : {.8, 1.0, 1.2}) {
        for (const double commonTone : {.05, .10, .15}) {
            for (const double voiceLeading : {.03, .05, .07}) {
                for (const double inKey : {.15, .20, .25, .30}) {
                    for (const double triad : {.20, .25, .30}) {
                        for (const double extension : {.20, .25, .30}) {
                            for (const double powerChord : {.30, .50}) {
                                for (const double appliedBonus : {.10, .20, .30}) {
                                    for (const double appliedPriorShare : {.25, .50, .75}) {
                                        Weights w;
                                        w.prior = prior;
                                        w.commonTone = commonTone;
                                        w.voiceLeading = voiceLeading;
                                        w.inKey = inKey;
                                        w.triad = triad;
                                        w.extension = extension;
                                        w.powerChord = powerChord;
                                        w.appliedBonus = appliedBonus;
                                        w.appliedPriorShare = appliedPriorShare;
                                        const double m = minMargin(w, scenarios, openings);
                                        ++combos;
                                        if (m >= 0) {
                                            ++passing;
                                        }
                                        if (m > bestMargin) {
                                            bestMargin = m;
                                            best = w;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    std::printf("%ld of %ld grid points pass; best min margin %.2f at prior %.2f commonTone %.2f voiceLeading %.2f inKey %.2f "
                "triad %.2f extension %.2f powerChord %.2f appliedBonus %.2f appliedPriorShare %.2f\n",
                passing, combos, bestMargin, best.prior, best.commonTone, best.voiceLeading, best.inKey, best.triad,
                best.extension, best.powerChord, best.appliedBonus, best.appliedPriorShare);
    for (const Scenario& s : scenarios) {
        print(s, best);
    }
    return 0;
}
