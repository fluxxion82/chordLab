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
/// under the new bass (the root under a ninth or a sharp eleventh), so that chord tone `inversion`
/// (counted up the close voicing) is the bass with the other tones close above it; then shifts
/// everything by `12 * octave`. `inversion` is clamped to 0..`inversionCount(chord) - 1` and `octave` to
/// `kMinVoicingOctave`..`kMaxVoicingOctave`.
/// Register: every note is at least `lowest + 12 * octave`. A lifted note is never folded back into the
/// range (the only place it could fold to is under the bass, which would change the inversion) nor
/// clamped (which would lose a note), so the top may exceed `highest + 12 * octave`, by this much: a
/// lifted note ends less than an octave above the bass, and the bass is at most the root plus the type's
/// largest interval `span`, the root below `lowest + 12`, so every note is at most
/// `min(highest + 11, lowest + span + 22) + 12 * octave`. At the default range that is `span + 82`: a
/// type spanning at most 14 semitones (every dictionary type but the sharp elevenths) stays inside
/// `60 + 12 * octave`..`96 + 12 * octave`; the sharp-eleventh types (span 18) reach at most
/// `100 + 12 * octave` (`7#11` and `maj7#11` on B in fourth inversion top out at 99, 111 at octave +1);
/// nothing exceeds 112. A range narrower than the default may see the top note up to eleven semitones
/// above the register for any type.
/// The range must satisfy `closeVoicing`'s conditions and, so that every octave and inversion stays
/// inside 0..127 (`highest + 11 + 12 <= 126`), leave an octave below `lowest` and two above `highest`
/// (`lowest >= 12`, `highest <= 103`). Anything else is a programmer error: it is ignored and the
/// defaults are used.
std::vector<int> voicing(const Chord& chord, int inversion, int octave, int lowest = 60, int highest = 96);

/// Upper bound on the number of notes `voiceLeadingCost` (here) and `pcVoiceLeadingCost` (harmony.h)
/// accept per side. A voicing has one note per chord tone, at most five today (the ninths); six leaves
/// headroom, and keeps the pitch-class cost's search over every bijection (n!) trivial.
constexpr int kMaxVoiceLeadingNotes = 6;

/// Cost of moving one concrete voicing onto another: the total displacement, in semitones, of the
/// cheapest one-to-one pairing of the MIDI notes of `from` with those of `to` (the minimum over every
/// bijection of the sum of |from[i] - to[j]|). This is the register-aware cost for actual voicings, as
/// the auto voice-leading of P2-D5 compares them (compiler.h); the smoothness of one chord into another
/// regardless of register is `pcVoiceLeadingCost` in harmony.h. The order of the notes in either voicing
/// is irrelevant. When the voicings differ in size, the shorter is first padded up to the longer's size
/// with its lowest note an octave up (the same doubled note as often as needed), so a triad against a
/// seventh chord moves three real voices plus one doubled root; the result is therefore symmetric for
/// any two voicings. The cheapest pairing never crosses voices, so it is the pairing of both voicings in
/// ascending order. Both voicings are MIDI note numbers (0..127), non-empty, and hold at most
/// `kMaxVoiceLeadingNotes` notes; anything else is a precondition violation.
int voiceLeadingCost(const std::vector<int>& from, const std::vector<int>& to);

}  // namespace chordlab
