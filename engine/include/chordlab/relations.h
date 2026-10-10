#pragma once

#include "chordlab/pitch.h"
#include "chordlab/scale.h"

#include <vector>

namespace chordlab {

/// The key `steps` fifths along the circle from `key`: +1 is up a fifth (C -> G), -1 down (C -> F).
/// The mode is kept and the new tonic takes `canonicalTonic` for that mode, so B Ionian +1 is F# Ionian
/// rather than Gb. Any int is accepted (12 steps returns to the same key); a non-canonical tonic is
/// respelled canonically even at zero steps.
Key fifthsNeighbour(const Key& key, int steps);

/// The nine keys on the tonic pitch class of `key`, one per `Mode` in enum order, each spelled with
/// `canonicalTonic` for its own mode (so Eb Ionian's list holds Eb Dorian but D# Phrygian). The entry
/// for `key.mode` equals `key` whenever `key` is canonical.
std::vector<Key> parallelModes(const Key& key);

/// The relative key: Ionian goes to the Aeolian a minor third below (C Ionian -> A Aeolian), Aeolian to
/// the Ionian a minor third above, each with the canonical spelling of the new tonic. Keys in any other
/// mode are returned unchanged.
Key relativeKey(const Key& key);

/// Signed number of fifths from the tonic of `from` to the tonic of `to`, in -5..6: +1 when `to` is a
/// fifth above, -1 when a fifth below. Only tonic pitch classes matter (mode and spelling are ignored),
/// and the tritone, which is six fifths either way, is reported as +6.
int fifthsDistance(const Key& from, const Key& to);

}  // namespace chordlab
