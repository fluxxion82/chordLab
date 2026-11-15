#pragma once

#include "chordlab/compiler.h"
#include "chordlab/document.h"
#include "chordlab/tracks.h"

#include <cstdint>
#include <string>
#include <vector>

namespace chordlab {

/// How many chords `exportClipName` spells out before it ends the name with an ellipsis.
constexpr int kClipNameChords = 8;

/// The name Live gives a clip dropped from the export: the file's track name (`writeSmf0`'s `trackName`).
/// For the whole progression (`sectionIndex` < 0, or 0 in a document without sections) it is the chord
/// symbols spelled in the project key (`chordSymbol`, the sequence's spelling), separated by spaces: the
/// first `kClipNameChords` of them, followed by " ..." when there are more ("C Am F G7",
/// "C7 F7 C7 C7 F7 F7 C7 C7 ..."). For a section (`sectionIndex` into `doc.sections`) it is the section's
/// name, or, when that is empty, the same summary of the section's own chords. Empty for an empty
/// progression or a section index that names no section. Allocates: never on the audio thread.
std::string exportClipName(const Document& doc, int sectionIndex);

/// The .mid bytes for the whole progression (`sectionIndex` < 0) or the section at `sectionIndex` of
/// `doc.sections` (`compileSection`; a document without sections has one section, index 0), at `bpm`, with
/// the document's meter in the time signature and `exportClipName(doc, sectionIndex)` as the track name:
/// `writeSmf0(compiled, bpm, meter.num, meter.den, 480, 0, name)` of `compile(doc)` or
/// `compileSection(doc, sectionIndex)`, so the clip holds exactly what plays, feel included, and Live names
/// it after the chords or the section. Empty when there is nothing to write: no events, a section index
/// that names no section, or a tempo `writeSmf0` refuses. Same document preconditions as `compile`.
/// Allocates: never on the audio thread.
std::vector<std::uint8_t> exportMidi(const Document& doc, int sectionIndex, double bpm);

/// One lane of a clip export (tracks design D12): the chords, or the track `trackId`.
struct ClipLane {
    bool chords = true;
    int trackId = kNoTrackId;  ///< the track's id when `chords` is false; ignored for the chords

    static constexpr ClipLane chordsLane() { return ClipLane{true, kNoTrackId}; }
    static constexpr ClipLane trackLane(int id) { return ClipLane{false, id}; }
};

/// What `exportClip` exports: the lanes, in request order, over the whole progression or one section.
struct ClipSpec {
    /// The lanes to write, in this order. Ignored when `allLanes` is set. Not deduplicated: a lane listed twice is
    /// written twice (the shell's request parser refuses duplicates).
    std::vector<ClipLane> lanes;
    /// The "All" drag: the chords lane plus every track with at least one note in the span, in document order.
    bool allLanes = false;
    /// < 0: the whole progression; otherwise a position in `doc.sections`, as `exportMidi` reads it (a document
    /// without sections has one section, index 0, the whole progression).
    int sectionIndex = -1;
};

/// The span in beats of section `sectionIndex` as playback uses it (`chordPlaySpans`, compiler.h): from its first
/// chord's start to the next section's first chord's start, or the last chord's end for the last section; the whole
/// progression `[0, compile(doc).endBeat)` for `sectionIndex` < 0 (and for index 0 of a document without
/// sections). `{0, 0}` (an empty span) for an empty document or an index that names no section. Same preconditions
/// as `compile`.
/// Its length is `compileSection`'s `endBeat` under a performing feel that is within budget (both are tick
/// differences over 960, so they agree to the last bit or one ulp; at 480 ppq they always round to the same
/// tick). On the block path the span is cut from the whole document's accumulated beats while `compileSection`
/// accumulates from 0 over the section, so the two lengths can differ in the last bit; writing either at 480 ppq
/// gives the same tick unless a length lies exactly on a half tick. Runs `performDocument` when the feel performs.
PlaySpan sectionSpanBeats(const Document& doc, int sectionIndex);

/// The notes of track `trackId` over `sectionIndex` (< 0: the whole progression; the same convention as
/// `sectionSpanBeats`), as a track's own clip holds them: independent of the other lanes, mute and solo ignored
/// (D12). In this order:
///  1. the active notes only (`noteActive`), each from `noteStart` (D3) as the beats `[start / 960.0, (start +
///     lengthTicks) / 960.0)`;
///  2. the **whole track's** same-pitch overlaps resolved by D8's paired rule (`compilePlayback`'s merge, the same
///     code): per pitch, notes in (rounded start tick, id) order; equal rounded starts become one note at the first
///     start with the latest end and the highest velocity; otherwise a note still sounding when the next of its
///     pitch starts ends there. A note this shortens is "shortened"; a merged equal-start group is not;
///  3. clipped to `[0, endBeat)` with `endBeat = compile(doc).endBeat`: a note starting at or after it is skipped, one
///     ending after it ends there (and is "shortened");
///  4. for a section (and for the whole progression, whose span is `[0, endBeat)`), every note that **intersects**
///     `sectionSpanBeats` (starts before its end and ends after its start) cut to it at both ends (a cut note is
///     "shortened"); merging before cutting is deliberate: same-pitch `[0, 8)` and `[3, 5)` over a `[4, 6)` section
///     give `[0, 3)` and `[3, 5)` after step 2 and then one note, `[0, 1)` re-based, not a note sustained through
///     the section;
///  5. re-based to the span's start;
///  6. a shortened note (steps 2 to 4) whose length in rounded ticks, `llround((end - start) * 960)`, is under
///     `kMinNoteTicks` (feel.h) is dropped; a note none of those steps touched is never dropped.
/// Events are on at the start (the note's velocity) and off at the end (velocity 0), sorted by (beat, off before
/// on, pitch) as `compile`'s are. `endBeat` is the span's length (`compile(doc).endBeat` for the whole
/// progression); `feelLimited` is `compile(doc).feelLimited`.
/// Returns an empty `Compiled` (no events, `endBeat` 0) for an unknown `trackId`, an invalid `sectionIndex`, or an
/// empty progression. Same preconditions as `compile`, and the tracks must satisfy `validateTracks`. Allocates:
/// never on the audio thread.
Compiled compileTrack(const Document& doc, int trackId, int sectionIndex);

/// The name Live gives a track's clip: the track's name for the whole progression (`sectionIndex` < 0, or 0 in a
/// document without sections); for a section, the track's name, a space, an EN DASH (U+2013, the UTF-8 bytes
/// `E2 80 93`), a space and the section's name; a section with an empty name takes `exportClipName`'s summary of
/// its chords instead (the fallback the chords lane's own clip name uses). Empty for an unknown track id or an
/// invalid section index; the track's name, even an empty one, for the whole progression of a known track.
std::string trackClipName(const Document& doc, int trackId, int sectionIndex);

/// The .mid bytes of a clip (tracks design D12): the lanes of `spec` over the span `spec.sectionIndex` names, at
/// `bpm`, with the document's meter. Lanes are independent, each holding its own notes (a chord tone a bass note
/// shortens in playback keeps its full length in the chords lane); mute and solo are ignored everywhere.
///
/// - **Lanes.** `spec.allLanes`: the chords lane, then every track in document order whose `compileTrack` over the
///   span has at least one note. Otherwise `spec.lanes` in request order: the chords lane is `compile` /
///   `compileSection` (name `exportClipName`), a track lane is `compileTrack` (name `trackClipName`). An explicit
///   lane that names an unknown track, or any lane of an invalid section index, makes the whole export empty
///   (nothing is written for the lanes that were valid). An explicit lane that has no notes is kept: it is written
///   as an empty lane of full length (its name and end-of-track only) when another lane has notes, so the file has
///   the lanes that were asked for. An empty `lanes` without `allLanes` is empty.
/// - **One lane** (after resolving; `allLanes` with every track empty is the chords lane alone): a format 0 file,
///   `writeSmf0(events, bpm, meter.num, meter.den, 480, 0, name)`. For the chords lane over the same span that is
///   exactly `exportMidi(doc, sectionIndex, bpm)`, byte for byte. A track lane is `compileTrack`'s events named by
///   `trackClipName`.
/// - **Several lanes**: a format 1 file (`writeSmf1`) at 480 ppq, ntrks = lanes + 1, the conductor carrying the
///   tempo and the meter. Every lane ends at one end beat: the chords lane's `endBeat`, that of `compile` for the
///   whole progression or of `compileSection` for a section, the very value `exportMidi` writes, whether or not the
///   chords are among the lanes. Track lanes have their own length in beats (the span's, which can differ from it in
///   the last bit on the block path, see `sectionSpanBeats`), but the writer rounds both to the same 480-ppq tick
///   and extends an end past a lane's last event, so no lane is ever shorter or longer than another.
/// - **Empty** (no bytes): no resolved lane has an event, a tempo or meter the writer refuses, or the cases above.
/// Same preconditions as `compile`. Allocates: never on the audio thread.
std::vector<std::uint8_t> exportClip(const Document& doc, const ClipSpec& spec, double bpm);

}  // namespace chordlab
