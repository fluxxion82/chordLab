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
    /// `Resolves`, and this is the project's own tonic triad: V -> I, vii° -> I (IΔ7 after V7 resolves
    /// but is not the cadence chord).
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
    Dominant,
    /// Not in the project key, in no parallel mode of its tonic, and not an applied chord: a colour
    /// chord with nothing else to say for it. Diatonic, Borrowed and Chromatic are exclusive, and a
    /// chord that is not applied always has exactly one of them, so no suggestion is without a reason.
    Chromatic
};

constexpr int kNumReasons = 12;

/// A stable, serialisable name for each reason: "resolves", "cadence", "prepares", "commonTones",
/// "smooth", "diatonic", "borrowed", "appliedDominant", "tonic", "predominant", "dominant",
/// "chromatic". The pointer is to a string literal and never goes stale. `reason` must be a declared
/// enumerator.
const char* reasonName(Reason reason);

/// One ranked candidate for the next chord.
struct Suggestion {
    Chord chord;
    /// The Klimper-style `chordSymbol`: a palette cell is spelled in the browsed key, a borrowed triad in
    /// the project key (it is a degree of that tonic: bII of C is `Db` even when browsing G major) and an
    /// applied chord in its own `AppliedChord::spellingKey` (`F#dim7`, not `Gbdim7`).
    std::string label;
    /// Relative to the project key: the Standard-style `romanNumeral` for a palette cell or a borrowed
    /// triad (`IV`, `bVI`), the `AppliedChord::label` for an applied chord (`V7/V`, `vii°7/ii`).
    std::string roman;
    /// The ranking score; see `suggestNext`. Higher is better; comparable only within one call.
    double score;
    /// Why, as data, in the order Resolves, Cadence, Prepares, CommonTones, Smooth, one of Diatonic /
    /// Borrowed / Chromatic, AppliedDominant, then the function. Never empty.
    std::vector<Reason> reasons;
    /// One sentence per entry of `reasons`, for the UI: "resolves (D→T)", "cadence (V7→I)",
    /// "prepares (PD→D)", "2 common tones (C, E)" (spelled in the project key), "smooth voice leading
    /// (cost 2)", "diatonic", "borrowed from C Aeolian", "outside the key — colour chord", "secondary
    /// dominant of ii (V7/ii)" or "leading-tone chord of V (vii°7/V)", "tonic" / "predominant" /
    /// "dominant". The arrows and the dash are literal UTF-8 characters.
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
/// - after a chord: `transition[f(previous)][f(candidate)]` for the style, plus 0.1 per common tone
///   counting at most two (beyond two shared tones the candidate is mostly the previous sonority, and
///   counting them all would let a chord's extensions outrank its triad), minus 0.05 per unit of
///   `pcVoiceLeadingCost`, plus a cadence bonus of 0.3 when f(previous) is Dominant and the candidate
///   is the in-key triad on the project tonic (its extensions still "resolve" but are not the
///   cadence chord);
/// - opening (no previous chord): Tonic 1.0, Predominant 0.6, Dominant 0.5, Other 0.2, plus 0.1 for a
///   chord on the project tonic so that the tonic chord leads its group rather than tying with iii
///   and vi;
/// - either way: plus 0.2 when fully in the project key; a simplicity term of +0.25 for a plain triad
///   (maj, min, dim, aug), 0 for a suspended chord, -0.5 for a power chord and -0.25 per chord tone
///   beyond the third (a seventh chord -0.25, a ninth -0.5);
/// - a degree prior for the style, `+1.0 x prior[degree]` with the degree from `numeralDegree` in the
///   project key (chromatic degrees have their own entries). The Pop prior is a hand-authored,
///   Hooktheory-style frequency table, numerals read against the parallel major as `numeralDegree`
///   does (so a minor key's own VI, III and VII take the bVI, bIII and bVII entries):
///
///       I 1.0   V 0.9   IV 0.9   vi 0.8   ii 0.6   iii 0.35   vii° 0.1
///       bVII 0.5   bVI 0.5   bIII 0.4   bII 0.2   #IV 0.1
///
/// - an applied chord has no prior of its own and takes half of its target's instead, plus a flat 0.2,
///   plus 0.15 for a secondary dominant of V or of ii after a tonic or predominant chord (the common
///   I -> V7/V -> V and IV -> V7/ii -> ii setups; the leading-tone chords vii°7/x do not get it).
/// The Pop table (rows the previous function, columns the next): T -> T .3, T -> PD .8, T -> D .6;
/// PD -> T .4, PD -> PD .3, PD -> D .9; D -> T 1.0, D -> PD .3, D -> D .4; .2 wherever Other is
/// involved.
///
/// The weights are tuned to a fixed set of Pop expectations in C major (test_suggest.cpp pins them:
/// F, G and Am are the top three after C with Em below Am; C leads after G7 with Am in the top four;
/// G leads after Dm and after F with vii° out of the top three; C is in the top three and V7/V in the
/// top eight after F; F leads after Am with G in the top four; the opening list of every key is its
/// tonic triad followed by diatonic chords only). The function table alone cannot order vi against
/// iii or ii (all it sees is T versus PD), so the prior outweighs it; the in-key bonus is small so
/// that an applied chord can reach the strip; and the simplicity term is steep so that a seventh
/// that contains the previous triad does not outrank a plain triad. Several of those orderings hold
/// by a single hundredth: change one weight and re-run the suite.
///
/// Sorted by score descending (compared as the integer hundredths, so equal scores are exactly equal),
/// then label ascending (byte order), so the order is total and the same input always gives the same
/// vector; the first `maxCount` are returned (fewer when there are fewer candidates). Allocates; not
/// for the audio thread.
///
/// Preconditions: `previous` null or a chord with a non-null type and a root in 0..11; both keys as
/// documented in pitch.h; `style` a declared enumerator.
std::vector<Suggestion> suggestNext(const Chord* previous, const Key& projectKey, const Key& browsedKey,
                                    Style style = Style::Pop, std::size_t maxCount = 8);

}  // namespace chordlab
