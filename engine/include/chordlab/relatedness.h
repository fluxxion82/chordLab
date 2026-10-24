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
/// - `Blend`: the candidates' `suggestionHundredths` after `reference` (suggest.h, with `browsedKey`
///   deciding which chords count as applied), min–max normalised over the candidates that differ from
///   the reference and rounded: the best 100, the worst 0, and 100 for all when they tie or there is
///   only one. Sorting by it is therefore `suggestNext`'s order (ties aside, since the rounding can
///   merge neighbours). The candidates passed are the population: pass every cell the UI sizes at once.
/// In every mode but `Function`, a candidate equal to the reference (same root and type) scores 100.
/// Allocates; not for the audio thread.
///
/// Preconditions: `reference` and every candidate with a non-null type and a root in 0..11; both keys
/// as documented in pitch.h; `mode` and `style` declared enumerators.
std::vector<Relatedness> relatedness(const Chord& reference, const std::vector<Chord>& candidates,
                                     const Key& projectKey, const Key& browsedKey, RelationMode mode,
                                     Style style = Style::Pop);

}  // namespace chordlab
