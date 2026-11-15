#pragma once

#include "chordlab/document.h"

#include <vector>

namespace chordlab {

/// Moves `events` by `semitones` (signed, -11..11): each chord's root moves, and each event's `octave`
/// changes by the octave the root crossed (-1, 0 or +1), so every note of every chord sounds exactly
/// `semitones` higher or lower than before (`voicing` in voicing.h places the root at 60 + root, then
/// everything else relative to it, so a root that wraps past B or below C takes one octave with it).
/// The octave of every event moves, the ones the user never voiced (`manualVoicing` clear) included: with
/// Auto voice-lead off their stored values are what sounds, and with it on a stored value is ignored
/// unless the chord is first. Inversion, type, length, velocity, id and `manualVoicing` are untouched.
/// An octave that would leave `kMinVoicingOctave`..`kMaxVoicingOctave` (only when a chord is already at an
/// extreme) is clamped, so that chord lands an octave off; the return value is how many octaves were
/// clamped. 0 semitones changes nothing and returns 0.
int transposeEvents(std::vector<ChordEvent>& events, int semitones);

/// The signed move for a key change of `interval` pitch classes (reduced modulo 12 to 1..11): `interval`
/// or `interval - 12`, chosen by, in order, the fewest octaves clamped, the smallest largest |octave|
/// after the move, the smallest sum of |octave| after the move, the smaller |move|, then up. 0 for an
/// interval of 0 (modulo 12). Pure: reads `events`, changes nothing.
int chooseKeyTranspose(const std::vector<ChordEvent>& events, int interval);

/// The signed move that carries track notes for a key change of `interval` pitch classes (reduced modulo 12)
/// when there are no chords to weigh (tracks design D18): `interval` for 1..6, `interval - 12` for 7..11, so
/// -5..+6, the smaller move, and the tritone goes up (+6). 0 for an interval of 0 (modulo 12). Pure.
int trackOnlyInterval(int interval);

/// Moves the pitch of every note of every track by `interval` (signed, -11..11), D18's pitches-only rule:
/// a result outside 0..127 moves by `interval` -/+ 12 instead (an octave down past 127, up below 0), so the
/// note keeps its pitch class and, with |interval| <= 11, always lands inside 0..127. Anchors, offsets,
/// lengths, velocities, ids, names and the order of the notes are untouched: the caller re-sorts, because
/// the notes' order is by (derived start, pitch, id) and a pitch change can reorder equal starts. An interval
/// of 0 changes nothing. Returns how many notes were folded by an octave.
int transposeNotes(std::vector<Track>& tracks, int interval);

}  // namespace chordlab
