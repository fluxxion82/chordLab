#pragma once

#include <cstdint>
#include <vector>

namespace chordlab {

/// One note of one lane on its way through the paired-note merge (tracks design D8), the step `compilePlayback`
/// and `compileTrack` (the lane exports) share.
struct LaneNote {
    double start;
    double end;
    int pitch;
    int velocity;
    int lane;  // 0 the chords, 1 + i track i
    int id;    // a track note's id; a chord note's index in pairing order
    bool shortened = false;  // the merge cut it short; an equal-start group is not a cut
    std::int64_t tick = 0;   // llround(start * 960): equal starts are equal rounded ticks
};

/// The paired-note merge over `notes` (D8; the exact rule is `compilePlayback`'s doc comment in compiler.h):
/// per pitch, ordered by (rounded start tick, lane, id, exact start); notes of one pitch whose starts round to the
/// same tick become one note at the first member's start with the latest end and the highest velocity (not
/// `shortened`: no member is cut); otherwise a note still sounding when the next of its pitch starts ends there
/// (`shortened`). Returns the merged notes in (pitch, start) order, every one with `start < end`; nothing is
/// dropped here (dropping a shortened note under `kMinNoteTicks` is the caller's, since it differs in what else
/// may shorten a note). Every note must have `start < end`. Main thread: allocates.
std::vector<LaneNote> mergeLaneNotes(std::vector<LaneNote> notes);

}  // namespace chordlab
