#include "note_merge.h"

#include "chordlab/meter.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <tuple>
#include <utility>

namespace chordlab {

std::vector<LaneNote> mergeLaneNotes(std::vector<LaneNote> notes) {
    // Per pitch in (rounded start tick, lane, id) order; the exact start only breaks what cannot tie.
    for (LaneNote& note : notes) note.tick = std::llround(note.start * kFeelTicksPerBeat);
    std::sort(notes.begin(), notes.end(), [](const LaneNote& a, const LaneNote& b) {
        return std::tie(a.pitch, a.tick, a.lane, a.id, a.start) < std::tie(b.pitch, b.tick, b.lane, b.id, b.start);
    });
    std::vector<LaneNote> merged;
    merged.reserve(notes.size());
    for (std::size_t i = 0; i < notes.size();) {
        LaneNote note = notes[i];
        std::size_t j = i + 1;
        // Equal starts of one pitch (one rounded tick): one note at the first member's start (a chord note's beat
        // when there is one), the latest end, the highest velocity; the first's lane and id.
        for (; j < notes.size() && notes[j].pitch == note.pitch && notes[j].tick == note.tick; ++j) {
            note.end = std::max(note.end, notes[j].end);
            note.velocity = std::max(note.velocity, notes[j].velocity);
        }
        i = j;
        assert(note.start < note.end);  // the latest end is at least the first member's, which ends after its start
        // The previous note of this pitch, still sounding, ends where this one starts (a retrigger). Its rounded
        // tick is lower, so its start is strictly earlier.
        if (!merged.empty() && merged.back().pitch == note.pitch && merged.back().end > note.start) {
            assert(merged.back().start < note.start);
            merged.back().end = note.start;
            merged.back().shortened = true;
        }
        merged.push_back(note);
    }
    return merged;
}

}  // namespace chordlab
