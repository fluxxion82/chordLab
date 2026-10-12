#pragma once

#include "chordlab/document.h"
#include "chordlab/note_event.h"

#include <vector>

namespace chordlab {

/// The playback form of a document: every note of every chord as on/off events, plus where the
/// progression ends (the point a looping scheduler wraps at, and where the MIDI file's end-of-track sits).
struct Compiled {
    std::vector<NoteEvent> events;  // sorted by (beat, off before on, pitch)
    double endBeat = 0.0;
};

/// The voicing `compile` gives each chord of `doc`, in playback order: `voicing(chord, inversion, octave)`
/// at the default range. With `doc.autoVoiceLead` on, every chord after the first whose `manualVoicing`
/// is clear instead takes, over all of its inversions at octaves -1, 0 and 1, the pair with the smallest
/// `voiceLeadingCost` from the previous chord's voicing as chosen here; a tie goes to the smaller
/// |octave|, then the smaller inversion, then the lower octave. A chord whose `manualVoicing` is set is
/// voiced as stored, auto or not (so a manual root position stays put), and so is the first chord.
/// Same preconditions as `compile`.
std::vector<std::vector<int>> voicings(const Document& doc);

/// Compiles `doc` for playback and export. This is the one duration policy shared by live playback and
/// the MIDI file (D10): each chord is voiced as `voicings` says, its ons sit at the chord's start beat
/// and its offs sit exactly at start + length, with no gap. A chord that repeats the previous chord's
/// pitch therefore has that pitch's off and on at the same beat, and the sort puts the off first so the
/// note retriggers.
///
/// Start beats are accumulated once, left to right, exactly as `Document::startBeatOf` and
/// `Document::endBeat` do, so a chord's offs and the next chord's ons share the identical boundary value
/// and `endBeat` is the beat of the last off. Events are sorted by `(beat, off before on, pitch)`, which
/// is a total order over the generated set, so the output is deterministic.
///
/// `doc` must satisfy the `DocumentStore` invariants: every chord valid, every length finite and within
/// `kMinLengthBeats`..`kMaxLengthBeats`, every velocity in `kMinVelocity`..`kMaxVelocity`, every
/// inversion and octave within the ranges `voicing` accepts. These are preconditions (asserted, not
/// checked): a document that came from outside the process is validated by the store before it gets here.
Compiled compile(const Document& doc);

/// Compiles the `count` chords from `firstEvent` on, as they sound in the whole document, re-based to 0:
/// each chord takes the voicing `voicings(doc)` gives it in the whole progression (the context of auto
/// voice-leading is the whole progression, so a chord at the start of a range still follows the chord
/// before the range; the voicings are sliced, never recomputed on the range alone), and the beats are
/// accumulated from 0 (not the whole document's beats shifted, which could differ in the last bit), so
/// the range starts at 0 and `endBeat` is its length. `compile(doc)` is `compileRange(doc, 0,
/// events.size())`, and with auto voice-leading off a range compiles exactly as a document holding only
/// its chords would. `firstEvent` must be in 0..events.size() and `count` in 0..events.size()-firstEvent
/// (preconditions, asserted); the same document invariants as `compile`.
Compiled compileRange(const Document& doc, int firstEvent, int count);

/// Compiles the section at `sectionIndex` of `doc.sections` as `compileRange` would: from its first chord
/// up to the next section's first chord, or to the end for the last one. A document without sections
/// behaves as one unnamed section over every chord, and `sectionIndex` must then be 0; otherwise it must be
/// in 0..sections.size()-1. The section invariants of `Document::sections` are preconditions (asserted).
Compiled compileSection(const Document& doc, int sectionIndex);

}  // namespace chordlab
