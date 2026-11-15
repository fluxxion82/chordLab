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

namespace {

/// An active note's authored interval [start, end) in ticks, 64-bit.
struct Span {
    std::int64_t start;
    std::int64_t end;
};

/// The intervals of every active note of every track, muted ones included; notes whose anchor is not a chord of
/// the timeline are skipped (`validateTracks` has rejected them before it gets here).
std::vector<Span> activeSpans(const Document& doc, const ChordTimeline& timeline) {
    std::size_t count = 0;
    for (const Track& t : doc.tracks) {
        count += t.notes.size();
    }
    std::vector<Span> spans;
    spans.reserve(count);
    for (const Track& t : doc.tracks) {
        for (const TrackNote& n : t.notes) {
            if (timeline.indexOf(n.anchorEventId) >= 0 && noteActive(timeline, n)) {
                const std::int64_t start = noteStart(timeline, n);
                spans.push_back(Span{start, start + n.lengthTicks});
            }
        }
    }
    return spans;
}

/// The most spans covering one tick. An end and a start at the same tick do not overlap: the end is counted first.
int maxOverlap(const std::vector<Span>& spans) {
    std::vector<std::pair<std::int64_t, int>> edges;  // (tick, -1 for an end, +1 for a start)
    edges.reserve(spans.size() * 2);
    for (const Span& s : spans) {
        edges.emplace_back(s.start, 1);
        edges.emplace_back(s.end, -1);
    }
    std::sort(edges.begin(), edges.end());  // at one tick, -1 sorts before +1
    int sounding = 0;
    int most = 0;
    for (const auto& edge : edges) {
        sounding += edge.second;
        most = std::max(most, sounding);
    }
    return most;
}

/// The most starts within any window [t, t + 960). The densest window can always be slid right until it begins at
/// a start, so only windows beginning at a start are counted.
int maxStartsPerBeat(const std::vector<Span>& spans) {
    std::vector<std::int64_t> starts;
    starts.reserve(spans.size());
    for (const Span& s : spans) {
        starts.push_back(s.start);
    }
    std::sort(starts.begin(), starts.end());
    int most = 0;
    std::size_t last = 0;  // one past the last start inside the window beginning at starts[first]
    for (std::size_t first = 0; first < starts.size(); ++first) {
        while (last < starts.size() && starts[last] < starts[first] + kFeelTicksPerBeat) {
            ++last;
        }
        most = std::max(most, static_cast<int>(last - first));
    }
    return most;
}

bool fail(std::string& error, std::string reason) {
    error = std::move(reason);
    return false;
}

}  // namespace

bool validateTracks(const Document& doc, int nextTrackId, int nextNoteId, std::string& error) {
    error.clear();
    if (nextTrackId < 1 || nextNoteId < 1) {
        return fail(error, "The track and note id counters must be at least 1");
    }
    if (doc.tracks.size() > static_cast<std::size_t>(kMaxTracks)) {
        return fail(error, "At most " + std::to_string(kMaxTracks) + " tracks");
    }
    std::vector<int> trackIds;
    std::size_t noteCount = 0;
    for (const Track& t : doc.tracks) {
        const auto which = [&t] { return "Track " + std::to_string(t.id); };  // built only for a failure
        if (t.id < 1 || t.id >= nextTrackId) {
            return fail(error, which() + " has an id outside 1.." + std::to_string(nextTrackId - 1));
        }
        if (std::find(trackIds.begin(), trackIds.end(), t.id) != trackIds.end()) {
            return fail(error, which() + " appears twice");
        }
        trackIds.push_back(t.id);
        if (t.role != TrackRole::Bass && t.role != TrackRole::Lead && t.role != TrackRole::Other) {
            return fail(error, which() + " has an unknown role");
        }
        if (t.name.size() > static_cast<std::size_t>(kMaxTrackNameBytes)) {
            return fail(error, which() + " has a name longer than " + std::to_string(kMaxTrackNameBytes) + " bytes");
        }
        if (t.notes.size() > static_cast<std::size_t>(kMaxNotesPerTrack)) {
            return fail(error, "At most " + std::to_string(kMaxNotesPerTrack) + " notes per track");
        }
        noteCount += t.notes.size();
    }

    const ChordTimeline timeline = chordTimeline(doc);
    std::vector<int> noteIds;
    noteIds.reserve(noteCount);
    for (const Track& t : doc.tracks) {
        std::int64_t previousStart = 0;
        const TrackNote* previous = nullptr;
        for (const TrackNote& n : t.notes) {
            const auto which = [&n] { return "Note " + std::to_string(n.id); };  // built only for a failure
            if (n.id < 1 || n.id >= nextNoteId) {
                return fail(error, which() + " has an id outside 1.." + std::to_string(nextNoteId - 1));
            }
            noteIds.push_back(n.id);
            if (timeline.indexOf(n.anchorEventId) < 0) {  // before any start is derived from it
                return fail(error, which() + " belongs to no chord");
            }
            if (n.pitch < 0 || n.pitch > 127) {
                return fail(error, which() + " has a pitch outside 0..127");
            }
            if (n.velocity < 1 || n.velocity > 127) {
                return fail(error, which() + " has a velocity outside 1..127");
            }
            if (n.offsetTick < 0) {
                return fail(error, which() + " starts before its chord");
            }
            const std::int64_t start = noteStart(timeline, n);  // 64-bit: no offset can overflow it
            if (start > kMaxNoteTick - kMinTrackNoteTicks) {
                return fail(error, which() + " starts past the end of the note range");
            }
            if (n.lengthTicks < kMinTrackNoteTicks || n.lengthTicks > kMaxNoteTick - start) {
                return fail(error, which() + " has a length outside " + std::to_string(kMinTrackNoteTicks) + ".." +
                                       std::to_string(kMaxNoteTick - start) + " ticks");
            }
            if (previous != nullptr && std::tie(start, n.pitch, n.id) <=
                                           std::tie(previousStart, previous->pitch, previous->id)) {
                return fail(error, "Track " + std::to_string(t.id) + "'s notes are out of order");
            }
            previous = &n;
            previousStart = start;
        }
    }
    std::sort(noteIds.begin(), noteIds.end());
    const auto repeated = std::adjacent_find(noteIds.begin(), noteIds.end());
    if (repeated != noteIds.end()) {
        return fail(error, "Note " + std::to_string(*repeated) + " appears twice");
    }

    const std::vector<Span> spans = activeSpans(doc, timeline);
    if (maxOverlap(spans) > kMaxTrackPolyphony) {
        return fail(error, "At most " + std::to_string(kMaxTrackPolyphony) + " track notes can sound at once");
    }
    if (maxStartsPerBeat(spans) > kMaxTrackOnsetsPerBeat) {
        return fail(error, "At most " + std::to_string(kMaxTrackOnsetsPerBeat) +
                               " notes can start within one beat across all tracks");
    }
    return true;
}

int trackPolyphony(const Document& doc) {
    return maxOverlap(activeSpans(doc, chordTimeline(doc)));
}

int densestOnsetWindow(const Document& doc) {
    return maxStartsPerBeat(activeSpans(doc, chordTimeline(doc)));
}

bool anyLaneSoloed(const Document& doc) noexcept {
    return doc.chordsSolo || std::any_of(doc.tracks.begin(), doc.tracks.end(), [](const Track& t) { return t.solo; });
}

bool chordsAudible(const Document& doc) noexcept {
    return laneAudible(doc.chordsMuted, doc.chordsSolo, anyLaneSoloed(doc));
}

bool trackAudible(const Document& doc, std::size_t trackIndex) noexcept {
    assert(trackIndex < doc.tracks.size());
    const Track& t = doc.tracks[trackIndex];
    return laneAudible(t.mute, t.solo, anyLaneSoloed(doc));
}

}  // namespace chordlab
