#include "chordlab/tracks.h"

#include "chordlab/document.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <tuple>
#include <utility>

namespace chordlab {

static_assert(static_cast<long long>(kMaxNoteTick) ==
                  static_cast<long long>(kMaxChords) * static_cast<long long>(kMaxLengthBeats) * kFeelTicksPerBeat,
              "the note domain ends where the longest progression ends");
static_assert(kMaxTrackNameBytes == kMaxSectionNameBytes, "track names are bounded as section names are");

std::vector<std::int32_t> chordBoundaryTicks(const Document& doc) {
    std::vector<std::int32_t> ticks;
    ticks.reserve(doc.events.size() + 1);
    // The cumulative beats, accumulated as `Document::startBeatOf` does, rounded once each.
    double beat = 0.0;
    ticks.push_back(0);
    for (const ChordEvent& event : doc.events) {
        beat += event.lengthBeats;
        ticks.push_back(static_cast<std::int32_t>(std::lround(beat * kFeelTicksPerBeat)));
    }
    return ticks;
}

int ChordTimeline::indexOf(int eventId) const {
    const auto it = indexById.find(eventId);
    return it == indexById.end() ? -1 : it->second;
}

ChordTimeline chordTimeline(const Document& doc) {
    ChordTimeline timeline;
    timeline.boundaries = chordBoundaryTicks(doc);
    timeline.eventIds.reserve(doc.events.size());
    timeline.indexById.reserve(doc.events.size());
    for (std::size_t i = 0; i < doc.events.size(); ++i) {
        timeline.eventIds.push_back(doc.events[i].id);
        timeline.indexById.emplace(doc.events[i].id, static_cast<int>(i));
    }
    return timeline;
}

std::optional<NoteAnchor> resolveStart(const ChordTimeline& timeline, std::int64_t tick) {
    const std::vector<std::int32_t>& b = timeline.boundaries;
    if (b.size() < 2 || tick < 0 || tick >= b.back()) {
        return std::nullopt;
    }
    // The first boundary after `tick`; the chord before it owns the tick (half-open spans).
    const auto after = std::upper_bound(b.begin(), b.end(), tick,
                                        [](std::int64_t t, std::int32_t boundary) { return t < boundary; });
    const auto index = static_cast<std::size_t>(after - b.begin()) - 1;
    return NoteAnchor{static_cast<int>(index), timeline.eventIds[index], static_cast<std::int32_t>(tick - b[index])};
}

std::optional<NoteAnchor> resolveStart(const Document& doc, std::int64_t tick) {
    return resolveStart(chordTimeline(doc), tick);
}

std::int64_t noteStart(const ChordTimeline& timeline, const TrackNote& note) {
    const int index = timeline.indexOf(note.anchorEventId);
    assert(index >= 0);
    return static_cast<std::int64_t>(timeline.boundaries[static_cast<std::size_t>(index)]) + note.offsetTick;
}

std::int64_t noteStart(const Document& doc, const TrackNote& note) {
    return noteStart(chordTimeline(doc), note);
}

bool noteActive(const ChordTimeline& timeline, const TrackNote& note) {
    const int index = timeline.indexOf(note.anchorEventId);
    assert(index >= 0);
    const auto i = static_cast<std::size_t>(index);
    return note.offsetTick >= 0 && note.offsetTick < timeline.boundaries[i + 1] - timeline.boundaries[i];
}

bool noteActive(const Document& doc, const TrackNote& note) {
    return noteActive(chordTimeline(doc), note);
}

void sortTrackNotes(std::vector<TrackNote>& notes, const ChordTimeline& timeline) {
    std::vector<std::pair<std::int64_t, TrackNote>> keyed;
    keyed.reserve(notes.size());
    for (const TrackNote& n : notes) {
        keyed.emplace_back(noteStart(timeline, n), n);
    }
    // Ids are unique within a track, so this is a total order and the result is deterministic.
    std::sort(keyed.begin(), keyed.end(), [](const auto& a, const auto& b) {
        return std::tie(a.first, a.second.pitch, a.second.id) < std::tie(b.first, b.second.pitch, b.second.id);
    });
    for (std::size_t i = 0; i < notes.size(); ++i) {
        notes[i] = keyed[i].second;
    }
}

int clampNotePitch(int pitch) {
    return std::clamp(pitch, 0, 127);
}

int clampNoteVelocity(int velocity) {
    return std::clamp(velocity, 1, 127);
}

std::int32_t clampNoteStart(std::int64_t tick) {
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(tick, 0, kMaxNoteTick - kMinTrackNoteTicks));
}

std::int32_t clampNoteLength(std::int64_t length, std::int32_t start) {
    assert(start >= 0 && start <= kMaxNoteTick - kMinTrackNoteTicks);
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(length, kMinTrackNoteTicks, kMaxNoteTick - start));
}

std::string defaultTrackName(const Document& doc, TrackRole role) {
    const std::string base = role == TrackRole::Bass ? "Bass" : role == TrackRole::Lead ? "Lead" : "Track";
    const auto taken = [&doc](const std::string& name) {
        return std::any_of(doc.tracks.begin(), doc.tracks.end(), [&name](const Track& t) { return t.name == name; });
    };
    if (!taken(base)) {
        return base;
    }
    // At most kMaxTracks names are taken, so this ends by " 6" at the latest.
    int n = 1;
    std::string name;
    do {
        name = base + " " + std::to_string(++n);
    } while (taken(name));
    return name;
}

}  // namespace chordlab
