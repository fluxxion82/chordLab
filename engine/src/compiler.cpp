#include "chordlab/compiler.h"

#include "chordlab/feel.h"
#include "chordlab/tracks.h"
#include "chordlab/voicing.h"

#include <algorithm>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <tuple>
#include <unordered_map>
#include <utility>

namespace chordlab {

namespace {

/// Checks the `DocumentStore` invariants that the compiler relies on: a positive length keeps every off
/// strictly after its on, the length cap bounds the accumulated beats to `kMaxLengthBeats` per chord,
/// and a velocity of at least 1 keeps an on from reading as a MIDI note-off.
void assertValidEvent([[maybe_unused]] const ChordEvent& event) {
    assert(event.chord.type != nullptr);
    assert(event.chord.root >= 0 && event.chord.root < 12);
    assert(std::isfinite(event.lengthBeats) && event.lengthBeats >= kMinLengthBeats &&
           event.lengthBeats <= kMaxLengthBeats);
    assert(event.velocity >= kMinVelocity && event.velocity <= kMaxVelocity);
    assert(event.inversion >= 0 && event.inversion < inversionCount(event.chord));
    assert(event.octave >= kMinVoicingOctave && event.octave <= kMaxVoicingOctave);
}

/// The voicing of `chord` closest to `previous` over every inversion at octaves -1..1 (at most 15
/// candidates): the one with the smallest key (cost, |octave|, inversion, octave), so that equally cheap
/// candidates go to the smaller |octave|, then the smaller inversion, then the lower octave.
std::vector<int> closestVoicing(const Chord& chord, const std::vector<int>& previous) {
    using Key = std::tuple<int, int, int, int>;
    std::vector<int> best;
    Key bestKey{INT_MAX, INT_MAX, INT_MAX, INT_MAX};
    const int count = inversionCount(chord);
    for (int octave = kMinVoicingOctave; octave <= kMaxVoicingOctave; ++octave) {
        for (int inversion = 0; inversion < count; ++inversion) {
            std::vector<int> candidate = voicing(chord, inversion, octave);
            const Key key{voiceLeadingCost(previous, candidate), std::abs(octave), inversion, octave};
            if (key < bestKey) {
                bestKey = key;
                best = std::move(candidate);
            }
        }
    }
    assert(!best.empty());
    return best;
}

/// The sort key: beat, then offs before ons, then ascending pitch. Over one compiled document the key
/// is unique (ons at a beat come from one chord, whose voicing has no duplicate pitches; likewise offs),
/// so this is a total order and the sorted output is deterministic.
bool keyLess(const NoteEvent& a, const NoteEvent& b) {
    return std::make_tuple(a.beat, a.on ? 1 : 0, a.pitch) < std::make_tuple(b.beat, b.on ? 1 : 0, b.pitch);
}

}  // namespace

std::vector<std::vector<int>> voicings(const Document& doc) {
    std::vector<std::vector<int>> result;
    result.reserve(doc.events.size());
    for (const ChordEvent& event : doc.events) {
        assertValidEvent(event);
        if (doc.autoVoiceLead && !event.manualVoicing && !result.empty()) {
            result.push_back(closestVoicing(event.chord, result.back()));
        } else {
            result.push_back(voicing(event.chord, event.inversion, event.octave));
        }
    }
    return result;
}

namespace {

/// The block path: every chord's voicing on at its start beat and off at its end, accumulated from 0 over
/// the range. Exactly `compileRange` as it was before feels existed.
Compiled blockRange(const Document& doc, int firstEvent, int count) {
    // The voicings are those of the whole progression, sliced: under auto voice-leading a chord follows
    // the chord before it, section boundary or not, so a range sounds as it does in the whole (header).
    const std::vector<std::vector<int>> chosen = voicings(doc);
    Compiled compiled;
    // Start beats are accumulated in a single pass, in the same order and with the same additions as
    // `Document::endBeat` does for a document holding just these chords. The running value is reused as
    // the off beat of one chord and the on beat of the next, so adjacent events compare equal exactly
    // rather than merely closely.
    double beat = 0.0;
    const std::size_t first = static_cast<std::size_t>(firstEvent);
    const std::size_t last = first + static_cast<std::size_t>(count);
    for (std::size_t i = first; i < last; ++i) {
        const ChordEvent& event = doc.events[i];
        const double start = beat;
        beat += event.lengthBeats;
        const double end = beat;
        for (const int pitch : chosen[i]) {
            compiled.events.push_back(NoteEvent{start, true, pitch, event.velocity});
            compiled.events.push_back(NoteEvent{end, false, pitch, 0});
        }
    }
    std::sort(compiled.events.begin(), compiled.events.end(), keyLess);
    // Taken from the accumulation so that the last off and `endBeat` can never drift apart.
    compiled.endBeat = beat;
    return compiled;
}

/// The chords [firstEvent, firstEvent + count) of a performance within budget, rebased to tick 0 (D12).
Compiled sliceRange(const Performance& performance, int firstEvent, int count) {
    const std::size_t first = static_cast<std::size_t>(firstEvent);
    const int base = performance.chordTicks[first];
    const int end = performance.chordTicks[first + static_cast<std::size_t>(count)];
    Compiled compiled;
    for (const PerformedEvent& e : performance.events) {
        if (e.chord >= firstEvent && e.chord < firstEvent + count) {
            compiled.events.push_back(
                NoteEvent{static_cast<double>(e.tick - base) / kFeelTicksPerBeat, e.on, e.pitch, e.velocity});
        }
    }
    compiled.endBeat = static_cast<double>(end - base) / kFeelTicksPerBeat;
    return compiled;
}

}  // namespace

Compiled compileRange(const Document& doc, int firstEvent, int count) {
    assert(firstEvent >= 0 && count >= 0);
    assert(static_cast<std::size_t>(firstEvent) + static_cast<std::size_t>(count) <= doc.events.size());
    if (!feelPerforms(doc.feel)) {
        return blockRange(doc, firstEvent, count);
    }
    // The fallback is the whole document's (D11): a range never plays a feel the progression cannot.
    const Performance performance = performDocument(doc);
    if (performance.feelLimited) {
        Compiled compiled = blockRange(doc, firstEvent, count);
        compiled.feelLimited = true;
        return compiled;
    }
    return sliceRange(performance, firstEvent, count);
}

Compiled compile(const Document& doc) {
    Compiled compiled = compileRange(doc, 0, static_cast<int>(doc.events.size()));
    // On the block path the accumulated value is bit-identical to `doc.endBeat()`, which performs the same
    // additions; a performance ends on the last chord's end tick instead.
    assert(compiled.endBeat == doc.endBeat() || (feelPerforms(doc.feel) && !compiled.feelLimited));
    return compiled;
}

namespace {

/// One note of one lane on its way through `compilePlayback`'s merge.
struct LaneNote {
    double start;
    double end;
    int pitch;
    int velocity;
    int lane;  // 0 the chords, 1 + i track i
    int id;    // a track note's id; a chord note's index in pairing order
    bool shortened = false;
};

/// `compiled`'s events paired back into notes per pitch, appended to `notes` in the order of their ons.
void pairChordNotes(const std::vector<NoteEvent>& events, std::vector<LaneNote>& notes) {
    std::unordered_map<int, std::size_t> open;  // pitch -> index into `notes` of its sounding note
    for (const NoteEvent& e : events) {
        if (e.on) {
            [[maybe_unused]] const bool fresh =
                open.emplace(e.pitch, notes.size()).second;  // compile never overlaps a pitch with itself
            assert(fresh);
            notes.push_back(LaneNote{e.beat, e.beat, e.pitch, e.velocity, 0, static_cast<int>(notes.size())});
        } else {
            const auto it = open.find(e.pitch);
            assert(it != open.end());  // every off closes an on of its pitch
            if (it == open.end()) continue;
            notes[it->second].end = e.beat;
            open.erase(it);
        }
    }
    assert(open.empty());
}

}  // namespace

Compiled compilePlayback(const Document& doc) {
    const Compiled chords = compile(doc);
    Compiled playback;
    playback.endBeat = chords.endBeat;
    playback.feelLimited = chords.feelLimited;

    std::vector<LaneNote> notes;
    if (chordsAudible(doc)) {
        notes.reserve(chords.events.size() / 2);
        pairChordNotes(chords.events, notes);
    }
    bool anyTrack = false;
    for (std::size_t i = 0; i < doc.tracks.size(); ++i) {
        anyTrack = anyTrack || (trackAudible(doc, i) && !doc.tracks[i].notes.empty());
    }
    if (anyTrack) {
        const ChordTimeline timeline = chordTimeline(doc);
        for (std::size_t i = 0; i < doc.tracks.size(); ++i) {
            if (!trackAudible(doc, i)) continue;
            for (const TrackNote& note : doc.tracks[i].notes) {
                if (!noteActive(timeline, note)) continue;
                const std::int64_t start = noteStart(timeline, note);
                const double startBeat = static_cast<double>(start) / kFeelTicksPerBeat;
                if (startBeat >= playback.endBeat) continue;
                const double endBeat =
                    std::min(static_cast<double>(start + note.lengthTicks) / kFeelTicksPerBeat, playback.endBeat);
                notes.push_back(
                    LaneNote{startBeat, endBeat, note.pitch, note.velocity, 1 + static_cast<int>(i), note.id});
            }
        }
    }

    // The merge, per pitch in (start, lane, id) order.
    std::sort(notes.begin(), notes.end(), [](const LaneNote& a, const LaneNote& b) {
        return std::tie(a.pitch, a.start, a.lane, a.id) < std::tie(b.pitch, b.start, b.lane, b.id);
    });
    std::vector<LaneNote> merged;
    merged.reserve(notes.size());
    for (std::size_t i = 0; i < notes.size();) {
        LaneNote note = notes[i];
        std::size_t j = i + 1;
        // Equal starts of one pitch: one note, the latest end, the highest velocity; the first's lane and id.
        for (; j < notes.size() && notes[j].pitch == note.pitch && notes[j].start == note.start; ++j) {
            note.end = std::max(note.end, notes[j].end);
            note.velocity = std::max(note.velocity, notes[j].velocity);
        }
        i = j;
        // The previous note of this pitch, still sounding, ends where this one starts (a retrigger).
        if (!merged.empty() && merged.back().pitch == note.pitch && merged.back().end > note.start) {
            merged.back().end = note.start;
            merged.back().shortened = true;
        }
        merged.push_back(note);
    }

    playback.events.reserve(2 * merged.size());
    for (const LaneNote& note : merged) {
        // Only a note the merge shortened may be dropped, judged in rounded ticks (never on the raw beats).
        if (note.shortened && std::llround((note.end - note.start) * kFeelTicksPerBeat) < kMinNoteTicks) continue;
        assert(note.start < note.end);
        assert(note.start >= 0.0 && note.end <= playback.endBeat);
        playback.events.push_back(NoteEvent{note.start, true, note.pitch, note.velocity});
        playback.events.push_back(NoteEvent{note.end, false, note.pitch, 0});
    }
    std::sort(playback.events.begin(), playback.events.end(), keyLess);
    // Per pitch the kept notes are disjoint with positive length, so no two events share the key.
    assert(std::adjacent_find(playback.events.begin(), playback.events.end(),
                              [](const NoteEvent& a, const NoteEvent& b) { return !keyLess(a, b); }) ==
           playback.events.end());
    assert(playback.events.size() <= kMaxSnapshotEvents);
    return playback;
}

Compiled compileSection(const Document& doc, int sectionIndex) {
    const int size = static_cast<int>(doc.events.size());
    if (doc.sections.empty()) {
        assert(sectionIndex == 0);
        return compileRange(doc, 0, size);
    }
    assert(sectionIndex >= 0 && static_cast<std::size_t>(sectionIndex) < doc.sections.size());
    const std::size_t at = static_cast<std::size_t>(sectionIndex);
    const int first = doc.sections[at].firstEventIndex;
    const int end = at + 1 < doc.sections.size() ? doc.sections[at + 1].firstEventIndex : size;
    assert(first >= 0 && first < end && end <= size);
    return compileRange(doc, first, end - first);
}

namespace {

/// The spans on the performance's tick grid (`performed`) or on the authored, accumulated boundaries; the last end is
/// written to `end` (0 for an empty document).
std::vector<PlaySpan> playSpans(const Document& doc, bool performed, double& end) {
    std::vector<PlaySpan> spans;
    spans.reserve(doc.events.size());
    end = 0.0;
    if (performed) {
        // performDocument's chordTicks is chordBoundaryTicks (tracks.h), so this is the performance's own grid.
        const std::vector<std::int32_t> ticks = chordBoundaryTicks(doc);
        for (std::size_t i = 0; i < doc.events.size(); ++i) {
            spans.push_back(PlaySpan{static_cast<double>(ticks[i]) / kFeelTicksPerBeat,
                                     static_cast<double>(ticks[i + 1]) / kFeelTicksPerBeat});
        }
        end = static_cast<double>(ticks.back()) / kFeelTicksPerBeat;
        return spans;
    }
    double beat = 0.0;  // the block path's accumulation (blockRange over the whole document)
    for (const ChordEvent& event : doc.events) {
        const double start = beat;
        beat += event.lengthBeats;
        spans.push_back(PlaySpan{start, beat});
    }
    end = beat;
    return spans;
}

}  // namespace

std::vector<PlaySpan> chordPlaySpans(const Document& doc, const Compiled& compiled) {
    double end = 0.0;
    std::vector<PlaySpan> spans = playSpans(doc, feelPerforms(doc.feel) && !compiled.feelLimited, end);
    // `compiled` must be compile(doc)'s (not compileSection's, nor another document's): its end is the spans' end on
    // either path, bit for bit (an empty document: compile's 0).
    assert(compiled.endBeat == end);
    (void)end;
    return spans;
}

std::vector<PlaySpan> chordPlaySpans(const Document& doc) {
    double end = 0.0;
    return playSpans(doc, feelPerforms(doc.feel) && !performDocument(doc).feelLimited, end);
}

}  // namespace chordlab
