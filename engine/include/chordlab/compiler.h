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

/// Compiles `doc` for playback and export. This is the one duration policy shared by live playback and
/// the MIDI file (D10): each chord is voiced with `closeVoicing` at the default range, its ons sit at the
/// chord's start beat and its offs sit exactly at start + length, with no gap. A chord that repeats the
/// previous chord's pitch therefore has that pitch's off and on at the same beat, and the sort puts the
/// off first so the note retriggers.
///
/// Start beats are accumulated once, left to right, exactly as `Document::startBeatOf` and
/// `Document::endBeat` do, so a chord's offs and the next chord's ons share the identical boundary value
/// and `endBeat` is the beat of the last off. Events are sorted by `(beat, off before on, pitch)`, which
/// is a total order over the generated set, so the output is deterministic.
///
/// `doc` must satisfy the `DocumentStore` invariants: every chord valid, every length finite and within
/// `kMinLengthBeats`..`kMaxLengthBeats`, every velocity in `kMinVelocity`..`kMaxVelocity`. These are
/// preconditions (asserted, not checked): a document that came from outside the process is validated by
/// the store before it gets here.
Compiled compile(const Document& doc);

}  // namespace chordlab
