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
    /// The document's feel was over budget (`performDocument`, D11), so these are block chords.
    bool feelLimited = false;
};

/// The voicing `compile` gives each chord of `doc`, in playback order: `voicing(chord, inversion, octave)`
/// at the default range. With `doc.autoVoiceLead` on, every chord after the first whose `manualVoicing`
/// is clear instead takes, over all of its inversions at octaves -1, 0 and 1, the pair with the smallest
/// `voiceLeadingCost` from the previous chord's voicing as chosen here; a tie goes to the smaller
/// |octave|, then the smaller inversion, then the lower octave. A chord whose `manualVoicing` is set is
/// voiced as stored, auto or not (so a manual root position stays put), and so is the first chord.
/// Same preconditions as `compile`.
std::vector<std::vector<int>> voicings(const Document& doc);

/// Compiles `doc` for playback and export.
///
/// With a feel (`feelPerforms(doc.feel)`), the events are those of `performDocument(doc)`, each at beat
/// `tick / 960.0`, and `endBeat` is the last chord's end tick / 960; when that performance is over budget
/// (`feelLimited`), the document compiles as block chords exactly as below and `Compiled::feelLimited` is
/// set. Without one, the block path below runs, unchanged from before feels existed:
///
/// This is the one duration policy shared by live playback and
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

/// Compiles what the plug-in plays (tracks design D8): the audible lanes of `doc`, the chords and every track,
/// merged into one event list. `fillSlot` is its only caller; `compile` keeps meaning the chords alone (snapshot,
/// export, sections). Main thread: allocates.
///
/// - **Audible lanes** (`laneAudible`, tracks.h): with no solo anywhere (chords or tracks), every unmuted lane; with
///   any solo, only the soloed lanes, mute ignored on a soloed lane. The chords lane is `chordsAudible(doc)`, track i
///   `trackAudible(doc, i)`.
/// - **Chord notes** are `compile(doc)`'s, in its beats exactly (authored doubles on the block path and in the
///   over-budget fallback, `tick / 960.0` when a feel performs), paired back into notes per pitch in event order:
///   per pitch `compile` alternates on, off, on, off (block chords do not overlap themselves, a retrigger's off
///   sorts before the on at its beat, and the feel's safety leaves the notes of one pitch disjoint), so the n-th
///   off of a pitch closes its n-th on.
/// - **Track notes**: the active notes (`noteActive`) of the audible tracks, inactive ones skipped. A note of
///   derived start `s` (`noteStart`, D3) and length `l` ticks becomes `[s / 960.0, min((s + l) / 960.0, endBeat))`
///   with `endBeat` = `compile(doc).endBeat`; one starting at or after `endBeat` is skipped (in a store's document
///   an active note always starts before it, so this is a guard). Nothing is re-rounded.
/// - **Paired merge** (the feel's D10 safety over lanes, on notes before any event exists): per pitch the notes
///   are ordered by start, then lane (the chords first, then the tracks in document order), then id (a track
///   note's id; a chord note's index in that order of pairing, which never decides anything, since chord notes of
///   one pitch never share a start). Notes of one pitch with equal starts become one note with the latest end and
///   the highest velocity; that note counts as untouched (no member is cut short: the merged end is the longest
///   member's). Otherwise a note still sounding when the next note of its pitch starts ends at that start (the
///   later note retriggers), and is then "shortened". A shortened note whose length in rounded ticks,
///   `llround((end - start) * 960)`, is under `kMinNoteTicks` (feel.h) is dropped; a note the merge did not
///   shorten is never dropped, so a track can never remove an exact five-tick feel note (`96005 / 960.0 - 96000 /
///   960.0` is a hair under `5 / 960.0`). Clipping a track note at `endBeat` is not a merge either: a note
///   clipped to under five ticks is kept.
/// - **Events**: an on at each note's start (its velocity) and an off at its end (velocity 0), sorted by `(beat,
///   off before on, pitch)` as `compile` sorts them; per pitch the merged notes are disjoint with positive length,
///   so the order is total. Every beat lies in [0, endBeat].
///
/// `endBeat` and `feelLimited` are `compile(doc)`'s: tracks never lengthen the loop. When the chords lane is
/// audible and no track contributes a note, `events` equals `compile(doc).events` exactly (nothing is merged,
/// shortened or dropped, and the events are re-emitted bit for bit in the same order). At most
/// `kMaxSnapshotEvents` events: `kMaxChordEvents` for the chords, two per track note, and the merge only removes.
/// Same preconditions as `compile`; the tracks must satisfy `validateTracks` (every anchor a chord of `doc`).
Compiled compilePlayback(const Document& doc);

/// Compiles the `count` chords from `firstEvent` on, as they sound in the whole document, re-based to 0:
/// each chord takes the voicing `voicings(doc)` gives it in the whole progression (the context of auto
/// voice-leading is the whole progression, so a chord at the start of a range still follows the chord
/// before the range; the voicings are sliced, never recomputed on the range alone), and the beats are
/// accumulated from 0 (not the whole document's beats shifted, which could differ in the last bit), so
/// the range starts at 0 and `endBeat` is its length. `compile(doc)` is `compileRange(doc, 0,
/// events.size())`, and with auto voice-leading off a range compiles exactly as a document holding only
/// its chords would. With a feel, the range is a slice of the whole performance (D12): the events of its
/// chords, `chordTicks[firstEvent]` subtracted from every tick, `endBeat` its tick length / 960; because every
/// performed note lies inside its own chord, that is exactly what those chords play inside the progression.
/// An over-budget feel falls back to block chords for every range, as for the whole. `firstEvent` must be in 0..events.size() and `count` in 0..events.size()-firstEvent
/// (preconditions, asserted); the same document invariants as `compile`.
Compiled compileRange(const Document& doc, int firstEvent, int count);

/// Compiles the section at `sectionIndex` of `doc.sections` as `compileRange` would: from its first chord
/// up to the next section's first chord, or to the end for the last one. A document without sections
/// behaves as one unnamed section over every chord, and `sectionIndex` must then be 0; otherwise it must be
/// in 0..sections.size()-1. The section invariants of `Document::sections` are preconditions (asserted).
Compiled compileSection(const Document& doc, int sectionIndex);

/// One chord's playback span in beats, half-open: [start, end).
struct PlaySpan {
    double start;
    double end;
};

/// Each chord's span as playback uses it (tracks design D8), one per event in playback order (empty for an empty
/// document): the one helper for the snapshot's `playStart`/`playEnd`, the harmonic guides, section slicing and the
/// generator, so their boundaries are exactly the ones `compile` sounds.
/// - With a feel that performs (`feelPerforms(doc.feel)`) and is within budget, `chordTicks[i] / 960.0` to
///   `chordTicks[i + 1] / 960.0` of `performDocument` (that is `chordBoundaryTicks(doc)`, tracks.h): the beats
///   `compile` gives the performed notes of chord i, all of which lie inside it, and the last end is `compile`'s
///   `endBeat`.
/// - Otherwise (block chords, and the over-budget fallback, `Compiled::feelLimited`) the authored boundaries,
///   accumulated left to right exactly as `compile`'s block path does (`Document::startBeatOf`): chord i's ons sit at
///   `start` and its offs at `end`, bit for bit, and adjacent spans share their boundary value.
/// So for a fractional length the two paths differ by under half a tick (three 0.2506-beat chords: 0.2506 authored,
/// 241 / 960 performed). Same preconditions as `compile`; runs `performDocument` when the feel performs.
std::vector<PlaySpan> chordPlaySpans(const Document& doc);

/// `chordPlaySpans(doc)` for a caller that already holds `compiled == compile(doc)`, whose `feelLimited` verdict it
/// reuses instead of performing the document again (the snapshot compiles once for both). `compiled` must be
/// `compile(doc)`'s result for this very document, not `compileSection`'s or `compileRange`'s: a debug build asserts
/// that its `endBeat` is the spans' last end (0 for an empty document).
std::vector<PlaySpan> chordPlaySpans(const Document& doc, const Compiled& compiled);

}  // namespace chordlab
