#include "chordlab/export.h"

#include "chordlab/compiler.h"
#include "chordlab/feel.h"
#include "chordlab/smf.h"
#include "chordlab/symbol.h"
#include "note_merge.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>
#include <utility>

namespace chordlab {

namespace {

/// The symbols of `doc.events[first, last)` in the project key, space-separated, the first `kClipNameChords`
/// of them and " ..." when there are more.
std::string chordSummary(const Document& doc, std::size_t first, std::size_t last) {
    const std::size_t shown = std::min(last - first, static_cast<std::size_t>(kClipNameChords));
    std::string out;
    for (std::size_t i = first; i < first + shown; ++i) {
        if (!out.empty()) {
            out += ' ';
        }
        out += chordSymbol(doc.events[i].chord, doc.projectKey);
    }
    if (last - first > shown) {
        out += " ...";
    }
    return out;
}

}  // namespace

std::string exportClipName(const Document& doc, int sectionIndex) {
    const std::size_t count = doc.events.size();
    if (sectionIndex < 0 || doc.sections.empty()) {
        // Without sections the progression is one unnamed section, index 0 (compileSection).
        return sectionIndex > 0 ? std::string() : chordSummary(doc, 0, count);
    }
    const std::size_t index = static_cast<std::size_t>(sectionIndex);
    if (index >= doc.sections.size()) {
        return {};
    }
    const Section& section = doc.sections[index];
    if (!section.name.empty()) {
        return section.name;
    }
    const std::size_t first = static_cast<std::size_t>(section.firstEventIndex);
    const std::size_t last =
        index + 1 < doc.sections.size() ? static_cast<std::size_t>(doc.sections[index + 1].firstEventIndex) : count;
    return chordSummary(doc, first, last);
}

std::vector<std::uint8_t> exportMidi(const Document& doc, int sectionIndex, double bpm) {
    Compiled compiled;
    if (sectionIndex < 0) {
        compiled = compile(doc);
    } else {
        // Without sections the progression is one unnamed section, index 0 (compileSection).
        const std::size_t sections = doc.sections.empty() ? 1 : doc.sections.size();
        if (static_cast<std::size_t>(sectionIndex) >= sections) {
            return {};
        }
        compiled = compileSection(doc, sectionIndex);
    }
    if (compiled.events.empty()) {
        return {};
    }
    return writeSmf0(compiled, bpm, doc.meter.num, doc.meter.den, 480, 0, exportClipName(doc, sectionIndex));
}

namespace {

/// The span of `sectionIndex` within `spans` (`chordPlaySpans(doc)`, one per chord), or false when the index names
/// no section. Index < 0, or 0 in a document without sections, is the whole progression.
bool spanOf(const Document& doc, const std::vector<PlaySpan>& spans, int sectionIndex, PlaySpan& out) {
    out = PlaySpan{0.0, 0.0};
    if (spans.empty()) {
        // An empty progression has no span; a section index can only be valid for the whole of nothing.
        return sectionIndex < 0 || (doc.sections.empty() && sectionIndex == 0);
    }
    if (sectionIndex < 0 || doc.sections.empty()) {
        if (sectionIndex > 0) {
            return false;
        }
        out = PlaySpan{spans.front().start, spans.back().end};
        return true;
    }
    const std::size_t index = static_cast<std::size_t>(sectionIndex);
    if (index >= doc.sections.size()) {
        return false;
    }
    const std::size_t first = static_cast<std::size_t>(doc.sections[index].firstEventIndex);
    const std::size_t end = index + 1 < doc.sections.size()
                                ? static_cast<std::size_t>(doc.sections[index + 1].firstEventIndex)
                                : spans.size();
    assert(first < end && end <= spans.size());
    out = PlaySpan{spans[first].start, spans[end - 1].end};
    return true;
}

/// The sort `compile` uses: (beat, off before on, pitch).
bool eventLess(const NoteEvent& a, const NoteEvent& b) {
    return std::make_tuple(a.beat, a.on ? 1 : 0, a.pitch) < std::make_tuple(b.beat, b.on ? 1 : 0, b.pitch);
}

/// `compileTrack` once the chords are compiled and the spans known.
Compiled trackClip(const Document& doc, const Track& track, const Compiled& chords, const PlaySpan& span,
                   bool wholeProgression) {
    Compiled clip;
    clip.feelLimited = chords.feelLimited;
    clip.endBeat = wholeProgression ? chords.endBeat : span.end - span.start;
    const double endBeat = chords.endBeat;

    // (1) Active notes only, from their derived starts, as beats; no clipping yet.
    const ChordTimeline timeline = chordTimeline(doc);
    std::vector<LaneNote> notes;
    notes.reserve(track.notes.size());
    for (const TrackNote& note : track.notes) {
        if (!noteActive(timeline, note)) continue;
        const std::int64_t start = noteStart(timeline, note);
        notes.push_back(LaneNote{static_cast<double>(start) / kFeelTicksPerBeat,
                                 static_cast<double>(start + note.lengthTicks) / kFeelTicksPerBeat, note.pitch,
                                 note.velocity, 1, note.id});
    }
    // (2) The whole track's same-pitch overlaps, D8's paired rule (shared with compilePlayback).
    const std::vector<LaneNote> merged = mergeLaneNotes(std::move(notes));

    clip.events.reserve(2 * merged.size());
    for (LaneNote note : merged) {
        // (3) Clipped to [0, endBeat): one starting at or after it is skipped, one ending after it is cut.
        if (note.start >= endBeat) continue;
        if (note.end > endBeat) {
            note.end = endBeat;
            note.shortened = true;
        }
        // (4) Cut to the span at both ends; one that does not intersect it is skipped.
        if (!(note.start < span.end && note.end > span.start)) continue;
        if (note.start < span.start) {
            note.start = span.start;
            note.shortened = true;
        }
        if (note.end > span.end) {
            note.end = span.end;
            note.shortened = true;
        }
        // (6) A note a step shortened, left under kMinNoteTicks rounded ticks, is dropped; judged before the
        // re-basing's subtraction, in the beats the cut left it. Never a note nothing touched.
        if (note.shortened && std::llround((note.end - note.start) * kFeelTicksPerBeat) < kMinNoteTicks) continue;
        if (!(note.start < note.end)) continue;  // impossible for valid input: never an off before its on
        // (5) Re-based to the span's start.
        clip.events.push_back(NoteEvent{note.start - span.start, true, note.pitch, note.velocity});
        clip.events.push_back(NoteEvent{note.end - span.start, false, note.pitch, 0});
    }
    std::sort(clip.events.begin(), clip.events.end(), eventLess);
    return clip;
}

const Track* findTrack(const Document& doc, int trackId) {
    for (const Track& track : doc.tracks) {
        if (track.id == trackId) return &track;
    }
    return nullptr;
}

/// A lane of an export on its way to the writer: its name, its notes and the length its own source gives it.
struct ResolvedLane {
    std::string name;
    Compiled compiled;
};

}  // namespace

PlaySpan sectionSpanBeats(const Document& doc, int sectionIndex) {
    PlaySpan span{0.0, 0.0};
    if (!spanOf(doc, chordPlaySpans(doc), sectionIndex, span)) {
        return PlaySpan{0.0, 0.0};
    }
    return span;
}

Compiled compileTrack(const Document& doc, int trackId, int sectionIndex) {
    const Track* track = findTrack(doc, trackId);
    if (track == nullptr || doc.events.empty()) {
        return {};
    }
    const Compiled chords = compile(doc);
    PlaySpan span{0.0, 0.0};
    if (!spanOf(doc, chordPlaySpans(doc, chords), sectionIndex, span)) {
        return {};
    }
    return trackClip(doc, *track, chords, span, sectionIndex < 0 || doc.sections.empty());
}

std::string trackClipName(const Document& doc, int trackId, int sectionIndex) {
    const Track* track = findTrack(doc, trackId);
    if (track == nullptr) {
        return {};
    }
    if (sectionIndex < 0 || doc.sections.empty()) {
        // Without sections the progression is one unnamed section, index 0.
        return sectionIndex > 0 ? std::string() : track->name;
    }
    if (static_cast<std::size_t>(sectionIndex) >= doc.sections.size()) {
        return {};
    }
    const std::string section = exportClipName(doc, sectionIndex);  // the section's name, or its chords
    if (section.empty()) {
        return track->name;
    }
    return track->name + " \xE2\x80\x93 " + section;  // an EN DASH, U+2013, between single spaces
}

std::vector<std::uint8_t> exportClip(const Document& doc, const ClipSpec& spec, double bpm) {
    const int sectionIndex = spec.sectionIndex;
    // Without sections the progression is one unnamed section, index 0 (as exportMidi and compileSection read it).
    if (sectionIndex >= 0 && static_cast<std::size_t>(sectionIndex) >= (doc.sections.empty() ? 1 : doc.sections.size())) {
        return {};
    }
    if (doc.events.empty()) {
        return {};
    }
    // The chords are compiled even when they are not among the lanes: `chords.endBeat` (compile's, or
    // compileSection's for a section: the end beat exportMidi writes) is the one end beat of a format 1 file.
    const Compiled whole = compile(doc);
    const Compiled chords = sectionIndex < 0 ? whole : compileSection(doc, sectionIndex);
    PlaySpan span{0.0, 0.0};
    const bool spanned = spanOf(doc, chordPlaySpans(doc, whole), sectionIndex, span);
    assert(spanned);
    if (!spanned) {
        return {};
    }
    const bool wholeProgression = sectionIndex < 0 || doc.sections.empty();

    const auto chordsLane = [&]() { return ResolvedLane{exportClipName(doc, sectionIndex), chords}; };
    const auto trackLane = [&](const Track& track) {
        return ResolvedLane{trackClipName(doc, track.id, sectionIndex),
                            trackClip(doc, track, whole, span, wholeProgression)};
    };

    std::vector<ResolvedLane> lanes;
    if (spec.allLanes) {
        lanes.push_back(chordsLane());
        for (const Track& track : doc.tracks) {
            ResolvedLane lane = trackLane(track);
            if (!lane.compiled.events.empty()) lanes.push_back(std::move(lane));
        }
    } else {
        for (const ClipLane& requested : spec.lanes) {
            if (requested.chords) {
                lanes.push_back(chordsLane());
                continue;
            }
            const Track* track = findTrack(doc, requested.trackId);
            if (track == nullptr) {
                return {};
            }
            lanes.push_back(trackLane(*track));
        }
    }
    if (std::none_of(lanes.begin(), lanes.end(), [](const ResolvedLane& l) { return !l.compiled.events.empty(); })) {
        return {};
    }

    if (lanes.size() == 1) {
        return writeSmf0(lanes[0].compiled, bpm, doc.meter.num, doc.meter.den, 480, 0, lanes[0].name);
    }
    std::vector<SmfLane> smfLanes;
    smfLanes.reserve(lanes.size());
    for (ResolvedLane& lane : lanes) {
        smfLanes.push_back(SmfLane{std::move(lane.name), std::move(lane.compiled.events)});
    }
    return writeSmf1(smfLanes, bpm, doc.meter.num, doc.meter.den, 480, chords.endBeat);
}

}  // namespace chordlab
