#pragma once

#include "chordlab/chord.h"

#include <vector>

namespace chordlab {

/// Root-position close voicing of `chord` as MIDI note numbers in `lowest`..`highest` (default 60..96).
/// The default register puts the root in the middle-C octave (60–71), matching Klimper's preview
/// register; C3-rooted close voicings of 7ths/9ths are muddy.
/// The root is the first note at or above `lowest` with the chord's root pitch class; every interval is
/// stacked above it, and any note above `highest` drops by octaves until it fits. The result is sorted
/// ascending with duplicates removed, so a folded extension (a ninth landing a whole tone above the root)
/// sits inside the chord rather than on top of it.
/// A range narrower than two octaves, or outside 0..127, is a programmer error: it is ignored and the
/// defaults are used. Two octaves guarantee that every folded note still lands at or above `lowest`.
std::vector<int> closeVoicing(const Chord& chord, int lowest = 60, int highest = 96);

/// The octave offsets `voicing` accepts: an octave below the register, the register itself, an octave above.
constexpr int kMinVoicingOctave = -1;
constexpr int kMaxVoicingOctave = 1;

/// Number of inversions of `chord`: its number of distinct pitch classes (two for a power chord, three
/// for a triad, four for a seventh, five for a ninth). Inversion 0 is root position and inversion
/// `inversionCount(chord) - 1` has the last chord tone in the bass. `chord.type` must be non-null.
int inversionCount(const Chord& chord);

/// `chord` in the given inversion and octave, as sorted, duplicate-free MIDI note numbers.
/// Starts from `closeVoicing(chord, lowest, highest)`, so `voicing(chord, 0, 0)` is `closeVoicing(chord)`;
/// lifts the lowest `inversion` notes of it by an octave, and by a second octave where one leaves them
/// under the new bass (the root under a ninth), so that chord tone `inversion` (counted up the close
/// voicing) is the bass with the other tones close above it; then shifts everything by `12 * octave`.
/// The register of the result is therefore `lowest`..`highest` shifted by the octave. `inversion` is
/// clamped to 0..`inversionCount(chord) - 1` and `octave` to `kMinVoicingOctave`..`kMaxVoicingOctave`.
/// A lifted note that ends above the register is kept there, never folded back down: the only place it
/// could fold to is under the bass, which would change the inversion. At the default range this never
/// happens (the root sits below 72 and a lifted note ends within two octaves of it, at most 95), and the
/// result lies within 48..108 over the three octaves; a range narrower than four octaves may see its
/// top note up to an octave above the register.
/// The range must satisfy `closeVoicing`'s conditions and, so that every octave and inversion stays
/// inside 0..127, leave an octave below `lowest` and two above `highest` (`lowest >= 12`,
/// `highest <= 103`). Anything else is a programmer error: it is ignored and the defaults are used.
std::vector<int> voicing(const Chord& chord, int inversion, int octave, int lowest = 60, int highest = 96);

/// Voice-leading cost between two voicings: the smallest total displacement, in semitones, over every
/// pairing of the notes of `from` with the notes of `to`. The shorter voicing is padded to the longer
/// one's size with the octave above its lowest note, once per missing voice, so that a triad moving to a
/// seventh pays for the voice that enters. The best pairing never crosses voices, so it is the pairing in
/// ascending order; the notes may be given in any order. Two empty voicings cost 0; one empty and one not
/// is a precondition violation.
int voiceLeadingCost(const std::vector<int>& from, const std::vector<int>& to);

}  // namespace chordlab
