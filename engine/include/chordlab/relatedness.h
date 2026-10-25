#pragma once

#include "chordlab/chord.h"
#include "chordlab/pitch.h"
#include "chordlab/suggest.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chordlab {

/// Which relation to a reference chord sizes the palette cells (decision R2): none, the shared pitch
/// classes, the voice-leading distance, the function-to-function transition, or the suggestion score
/// (`blend`, so that the palette and the suggestions strip agree). The document's live
/// `relationMode` (document.h) holds the user's choice; `Blend` is the default. A `RelationMode` passed
/// to any engine function must be one of the declared enumerators.
enum class RelationMode { Off, CommonTones, VoiceLeading, Function, Blend };

constexpr int kNumRelationModes = 5;

/// The wire name of `mode`, as the snapshot, the `setRelationMode` command and the saved state spell it:
/// "off", "commonTones", "voiceLeading", "function", "blend". The pointer is to a string literal and
/// never goes stale.
const char* relationModeName(RelationMode mode);

/// The mode whose `relationModeName` is `name` (exact, case-sensitive), or nullopt.
std::optional<RelationMode> parseRelationMode(std::string_view name);

/// How related one candidate chord is to the reference chord.
struct Relatedness {
    /// 0..100, higher is more related (R3, R4); see `relatedness`.
    int score;
    /// `relationText(relate(reference, candidate, projectKey), projectKey)` (harmony.h): "2 common tones
    /// (C, E) · down a third · cost 2", with " · borrowed from C Aeolian" for a borrowed candidate. The
    /// same whatever the mode.
    std::string text;
};

/// The relatedness of each of `candidates` to `reference` under `mode` (decisions R3, R4), parallel to
/// `candidates`; empty for `RelationMode::Off`. Scores, with `relate` and `functionOf` read in
/// `projectKey`:
/// - `CommonTones`: `round(100 · common / max(nRef, nCand))`, the counts being distinct pitch classes
///   (C against Am 67, against CΔ7 75, against F 33, against Dm 0);
/// - `VoiceLeading`: `max(0, 100 − 10 · pcVoiceLeadingCost)` (C against Am 80, against F and G 70);
/// - `Function`: `transitionHundredths(functionOf(reference), functionOf(candidate), style)`, the
///   transition table read as a percentage (C to G 60, C to F 80, G7 to C 100, 20 wherever Other is
///   involved). The one mode in which a candidate equal to the reference is not 100: C to C is T -> T, 30;
/// - `Blend`: the candidates' `suggestionHundredths` after `reference` (suggest.h), min–max normalised
///   over the candidates that differ from the reference and rounded: the best 100, the worst 0, and 100
///   for all when they tie or there is only one. Sorting by it is therefore `suggestNext`'s order (ties
///   aside, since the rounding can merge neighbours). The candidates passed are the population: pass
///   every cell the UI sizes at once. As all scoring, it is project-key relative: a chord scores as an
///   applied chord exactly when it is one of `appliedChords(projectKey)` and not a cell of
///   `buildPalette(browsedKey, projectKey)` (`browsedKey` decides only which chords are palette cells),
///   so an applied chord of a browsed key that is not one of the project key's scores as the plain
///   chromatic chord it is there.
/// In every mode but `Function`, a candidate equal to the reference (same root and type) scores 100.
/// Allocates; not for the audio thread.
///
/// Preconditions: `reference` and every candidate with a non-null type and a root in 0..11; both keys
/// as documented in pitch.h; `mode` and `style` declared enumerators.
std::vector<Relatedness> relatedness(const Chord& reference, const std::vector<Chord>& candidates,
                                     const Key& projectKey, const Key& browsedKey, RelationMode mode,
                                     Style style = Style::Pop);

/// `relatedness(reference, candidates, ...)` followed by a score for each of `extras`, chords sized on the
/// same scale as the population without being part of it (the snapshot's applied chords, sized beside
/// the palette and the home row). The first `candidates.size()` entries are exactly the call without
/// `extras`; then one entry per extra, in order. `Off` returns nothing. In the per-candidate modes an
/// extra scores as it would anywhere (those scores are absolute). In `Blend`, an extra's raw
/// `suggestionHundredths` is normalised with the population's min and max (the reference excluded, as
/// above) and clamped to 0..100, so an extra better than the population's best is 100 and one worse
/// than its worst 0; when the population has no range (its non-reference chords tie, or there are
/// none), an extra at or above their score, or every extra when there are none, is 100 and one below
/// it 0. An extra equal to the reference is 100 (in every mode but `Function`, as above). Same
/// preconditions; every extra as a candidate. Allocates; not for the audio thread.
std::vector<Relatedness> relatedness(const Chord& reference, const std::vector<Chord>& candidates,
                                     const std::vector<Chord>& extras, const Key& projectKey, const Key& browsedKey,
                                     RelationMode mode, Style style = Style::Pop);

}  // namespace chordlab
