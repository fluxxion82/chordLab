#pragma once

#include "chordlab/pitch.h"
#include "chordlab/scale.h"

#include <string_view>
#include <vector>

namespace chordlab {

/// Klimper colour group of a chord type.
enum class Family { Major, Minor, Other };

/// A chord quality from the dictionary.
/// `id` is the stable identifier that persistence stores; `symbol` is the Klimper-style suffix the UI
/// appends to the root name (ASCII `b`/`#`, and the literal `Δ` character for a major seventh, so "Δ7",
/// "mΔ7"); `intervals` are semitones above the root, starting at 0, strictly increasing and below 24.
struct ChordType {
    const char* id;
    const char* symbol;
    std::vector<int> intervals;
    Family family;
};

/// The chord type with the given `id`. Throws `std::out_of_range` for an unknown id.
/// The returned reference is stable until module unload / static destruction, so a `Chord::type` may
/// point at it.
const ChordType& chordType(std::string_view id);

/// Every chord type, in dictionary order (power chord, triads, sixths, sevenths, ninths, 7sus4).
/// The vector is stable until module unload / static destruction and `chordType(id)` returns references
/// into it.
const std::vector<ChordType>& allChordTypes();

/// A chord: a root pitch class and a dictionary type. `type` must point into `allChordTypes()`.
/// Stored `root` values must be canonical (0..11): `pitchClasses` reduces an out-of-range root modulo
/// 12 as a convenience, but `operator==` compares raw roots, so `Chord{12, t} != Chord{0, t}`.
struct Chord {
    PitchClass root;
    const ChordType* type;
};

/// Chords are equal when they share a root and the same dictionary entry (compared by address).
constexpr bool operator==(const Chord& a, const Chord& b) noexcept {
    return a.root == b.root && a.type == b.type;
}

constexpr bool operator!=(const Chord& a, const Chord& b) noexcept {
    return !(a == b);
}

/// Pitch classes of the chord: the root plus each interval, each reduced modulo 12, in interval order.
std::vector<PitchClass> pitchClasses(const Chord& chord);

/// True if every pitch class of the chord belongs to the key's scale.
bool fullyInKey(const Chord& chord, const Key& key);

/// The stacked-third triad on scale degree `degree` of `key`: rooted on that degree, with the pitch
/// classes of degrees d+2 and d+4 of the key's scale as its third and fifth, typed as whichever of
/// `maj`/`min`/`dim`/`aug` has exactly those intervals (every supported mode yields one of the four).
/// `degree` is taken modulo 7, so 7 is the tonic triad again and -1 the triad on the seventh degree.
Chord diatonicTriad(const Key& key, int degree);

}  // namespace chordlab
