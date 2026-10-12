#pragma once

#include "chordlab/chord.h"
#include "chordlab/pitch.h"

#include <cstddef>
#include <string>
#include <vector>

namespace chordlab {

/// The hand-authored progression style behind the transition table (decision P2-D3). One for now; the
/// enum is the seam for the next.
enum class Style { Pop };

/// Why a chord is suggested. Each reason is one line of the "why" the UI shows; `Suggestion::reasons`
/// and `Suggestion::reasonTexts` are parallel, `reasonTexts[i]` being the sentence for `reasons[i]`.
enum class Reason {
    /// The previous chord is a dominant and this one is in the tonic group (D -> T).
    Resolves,
    /// `Resolves`, and this is the project's own tonic chord: V -> I, vii° -> I, V7 -> IΔ7.
    Cadence,
    /// The previous chord is a predominant and this one a dominant (PD -> D).
    Prepares,
    /// Two or more pitch classes are shared with the previous chord.
    CommonTones,
    /// `pcVoiceLeadingCost` from the previous chord is at most 2.
    Smooth,
    /// Every note is in the project key.
    Diatonic,
    /// Not in the project key, but a parallel mode of the project tonic holds it (`borrowedFrom`).
    /// Never given to an applied chord, whose foreign notes its target explains (D7 in C major is
    /// V7/V, not a Lydian chord).
    Borrowed,
    /// One of `appliedChords(projectKey)`: a secondary dominant or leading-tone chord of a degree.
    AppliedDominant,
    /// The chord's own function in the project key (`functionOf`); `Function::Other` adds no reason.
    Tonic,
    Predominant,
    Dominant
};

/// One ranked candidate for the next chord.
struct Suggestion {
    Chord chord;
    /// The Klimper-style `chordSymbol`, spelled in the browsed key for a palette cell or a borrowed
    /// triad and in its own `AppliedChord::spellingKey` for an applied chord (`F#°7`, not `Gb°7`).
    std::string label;
    /// Relative to the project key: the Standard-style `romanNumeral` for a palette cell or a borrowed
    /// triad (`IV`, `bVI`), the `AppliedChord::label` for an applied chord (`V7/V`, `vii°7/ii`).
    std::string roman;
    /// The ranking score; see `suggestNext`. Higher is better; comparable only within one call.
    double score;
    /// Why, as data, in the order Resolves, Cadence, Prepares, CommonTones, Smooth, Diatonic or
    /// Borrowed, AppliedDominant, then the function. Never contains both Diatonic and Borrowed.
    std::vector<Reason> reasons;
    /// One sentence per entry of `reasons`, for the UI: "resolves (D→T)", "cadence (V7→I)",
    /// "prepares (PD→D)", "2 common tones (C, E)" (spelled in the project key), "smooth voice leading
    /// (cost 2)", "diatonic", "borrowed from C Aeolian", "secondary dominant of ii (V7/ii)" or
    /// "leading-tone chord of V (vii°7/V)", "tonic" / "predominant" / "dominant". The arrows are the
    /// literal UTF-8 character.
    std::vector<std::string> reasonTexts;
};

/// The chords to try after `previous` (or to open with, when `previous` is null), best first
/// (decision P2-D3).
///
/// Candidates, each once (the first source that lists a chord keeps it): every cell of
/// `buildPalette(browsedKey, projectKey)`; every `appliedChords(projectKey)`; and the four common
/// borrowed major triads on bII, bIII, bVI and bVII of the project tonic (a semitone, a minor third, a
/// minor sixth and a minor seventh above it). `previous` itself is left out.
///
/// Score, in hundredths so that equal scores are exactly equal, with f() = `functionOf` in the project
/// key and an applied chord counted as a Dominant whatever `functionOf` says:
/// - after a chord: `transition[f(previous)][f(candidate)]` for the style, plus 0.15 per common tone,
///   minus 0.03 per unit of `pcVoiceLeadingCost`, plus a cadence bonus of 0.3 when f(previous) is
///   Dominant and the candidate is in key on the project tonic;
/// - opening (no previous chord): Tonic 1.0, Predominant 0.6, Dominant 0.5, Other 0.2, plus 0.1 for a
///   chord on the project tonic so that the tonic chord leads its group rather than tying with iii
///   and vi;
/// - either way: plus 0.4 when fully in the project key; plus 0.25 for a plain triad (maj, min, dim,
///   aug), nothing for a power chord or a suspended chord, minus 0.1 per chord tone beyond the third
///   (a seventh chord -0.1, a ninth -0.2); minus 0.05 for an applied chord (so a diatonic dominant
///   wins a tie).
/// The Pop table (rows the previous function, columns the next): T -> T .3, T -> PD .8, T -> D .6;
/// PD -> T .4, PD -> PD .3, PD -> D .9; D -> T 1.0, D -> PD .3, D -> D .4; .2 wherever Other is
/// involved. The in-key and simplicity terms are deliberately larger than the plan's first guess
/// (0.1 in key, 0.05 for a triad): at 0.15 per common tone a seventh or ninth chord shares more with
/// any neighbour than its triad does (CΔ9 holds G, B and D of G7; FΔ9 holds all of C), so a graded
/// preference for the plain triad is what makes V7 -> I beat V7 -> IΔ9 and I -> V beat I -> IVΔ9, and
/// the in-key term must then exceed the triad bonus so that the opening list stays diatonic (bIII
/// and bVII are tonic-group triads). The price is that an applied chord, which earns none of the
/// three bonuses, ranks below every diatonic chord of its function in a full key: V7/V after IV scores
/// 0.93 in C major, below the top eight.
///
/// Sorted by score descending, then label ascending (byte order), so the order is total and the same
/// input always gives the same vector; the first `maxCount` are returned (fewer when there are fewer
/// candidates). Allocates; not for the audio thread.
///
/// Preconditions: `previous` null or a chord with a non-null type and a root in 0..11; both keys as
/// documented in pitch.h; `style` a declared enumerator.
std::vector<Suggestion> suggestNext(const Chord* previous, const Key& projectKey, const Key& browsedKey,
                                    Style style = Style::Pop, std::size_t maxCount = 8);

}  // namespace chordlab
