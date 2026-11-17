#include "chordlab/link.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <set>
#include <utility>

namespace chordlab {

namespace {

bool holdsTrack(const Document& doc, int trackId) {
    return std::any_of(doc.tracks.begin(), doc.tracks.end(), [&](const Track& t) { return t.id == trackId; });
}

}  // namespace

bool LaneUids::cover(const Document& doc, const Generator& make) {
    bool changed = false;
    for (const Track& track : doc.tracks) {
        if (entries_.count(track.id) != 0) continue;
        std::string uid;
        for (;;) {
            uid = make();
            const bool usable = !uid.empty() && uid != kChordsLaneUid;
            assert(usable && "a lane uid generator never returns kChordsLaneUid or an empty string");
            if (usable) break;
        }
        entries_.emplace(track.id, std::move(uid));
        changed = true;
    }
    // Vanished entries beyond the cap go, lowest track numbers first (the map iterates in ascending order).
    std::size_t vanished = 0;
    for (const auto& entry : entries_) {
        if (!holdsTrack(doc, entry.first)) ++vanished;
    }
    for (auto it = entries_.begin(); vanished > kMaxVanishedLaneUids && it != entries_.end();) {
        if (holdsTrack(doc, it->first)) {
            ++it;
            continue;
        }
        it = entries_.erase(it);
        --vanished;
        changed = true;
    }
    return changed;
}

std::string LaneUids::uidOf(int trackId) const {
    const auto it = entries_.find(trackId);
    return it == entries_.end() ? std::string() : it->second;
}

std::optional<LaneKey> LaneUids::laneOf(const Document& doc, std::string_view uid) const {
    if (uid == kChordsLaneUid) return chordsLane();
    if (uid.empty()) return std::nullopt;
    for (const auto& entry : entries_) {
        if (entry.second == uid && holdsTrack(doc, entry.first)) return trackLane(entry.first);
    }
    return std::nullopt;
}

const std::map<int, std::string>& LaneUids::entries() const noexcept {
    return entries_;
}

void LaneUids::replace(std::map<int, std::string> entries) {
#ifndef NDEBUG
    std::set<std::string_view> seen;
    for (const auto& entry : entries) {
        assert(!entry.second.empty() && entry.second != kChordsLaneUid &&
               "LaneUids::replace: a lane uid is never empty nor kChordsLaneUid (the caller validates the table)");
        assert(seen.insert(entry.second).second && "LaneUids::replace: lane uids are pairwise distinct");
    }
#endif
    entries_ = std::move(entries);
}

std::shared_ptr<const LinkFeed> buildLinkFeed(const Document& doc, std::string documentId, const LaneUids& uids,
                                              std::string lineage, std::string label,
                                              const std::vector<std::string>& followed) {
    const auto isFollowed = [&](std::string_view uid) {  // an empty uid (an uncovered track) is never followed
        return !uid.empty() && std::find(followed.begin(), followed.end(), uid) != followed.end();
    };
    auto feed = std::make_shared<LinkFeed>();
    feed->documentId = std::move(documentId);
    feed->lineage = std::move(lineage);
    feed->label = std::move(label);
    const Compiled chordsCompiled = compile(doc);  // once, for the frame and for every compiled lane
    feed->endBeat = chordsCompiled.endBeat;
    feed->loop = doc.progressionLoop;
    feed->followHost = doc.followHost;
    feed->lanes.reserve(1 + doc.tracks.size());

    const auto addLane = [&](LinkLane lane, const LaneKey& key) {
        lane.compiled = isFollowed(lane.uid);
        if (lane.compiled) {
            lane.events =
                compilePlaybackFor(doc, LaneSelection{LaneSelection::Mode::Only, {key}}, chordsCompiled).events;
        }
        feed->lanes.push_back(std::move(lane));
    };
    LinkLane chords;
    chords.uid = std::string(kChordsLaneUid);
    chords.chords = true;
    chords.name = "Chords";
    chords.audible = chordsAudible(doc);
    addLane(std::move(chords), chordsLane());
    for (std::size_t i = 0; i < doc.tracks.size(); ++i) {
        const Track& track = doc.tracks[i];
        LinkLane lane;
        lane.uid = uids.uidOf(track.id);
        assert(!lane.uid.empty() && "buildLinkFeed: the lane uids must cover the document");
        lane.name = track.name;
        lane.role = track.role;
        lane.audible = trackAudible(doc, i);
        addLane(std::move(lane), trackLane(track.id));
    }
    return feed;
}

bool fillSlotFromFeed(const LinkFeed* feed, std::string_view laneUid, std::string_view documentId,
                      SnapshotSlot& slot, Publication& last) {
    assert((last.generation == 0 || last.generation > kLinkGenerationBase) &&
           "fillSlotFromFeed: `last` belongs to a link buffer");
    static const std::vector<NoteEvent> kNoEvents;
    const std::vector<NoteEvent>* events = &kNoEvents;
    if (feed != nullptr && feed->documentId == documentId) {
        const auto lane = std::find_if(feed->lanes.begin(), feed->lanes.end(),
                                       [&](const LinkLane& l) { return l.uid == laneUid; });
        if (lane != feed->lanes.end() && lane->compiled) events = &lane->events;
    }
    const double endBeat = feed != nullptr ? feed->endBeat : 0.0;
    const bool loop = feed != nullptr ? feed->loop : true;
    const bool followHost = feed != nullptr ? feed->followHost : true;

    const bool first = last.generation == 0;
    const bool contentChanged =
        first || *events != last.compiled.events || endBeat != last.compiled.endBeat || loop != last.loop;
    const bool followChanged = first || followHost != last.followHost;
    if (!contentChanged && !followChanged) {
        return false;
    }
    const bool fits = events->size() <= slot.events.size();
    assert(fits && "a lane of a leader's document fits one snapshot slot (publication.h)");
    slot.count = fits ? events->size() : 0;
    std::copy_n(events->begin(), slot.count, slot.events.begin());
    slot.endBeat = fits ? endBeat : 0.0;
    slot.loop = loop;
    slot.followHost = followHost;
    slot.midiThru = true;
    last.compiled.events = *events;
    last.compiled.endBeat = endBeat;
    last.compiled.feelLimited = false;
    last.loop = loop;
    last.followHost = followHost;
    last.midiThru = true;
    if (contentChanged) {
        // Reaching UINT32_MAX takes 2^31 - 1 content changes of one link buffer.
        assert(last.generation != UINT32_MAX && "fillSlotFromFeed: the link generation would wrap");
        last.generation = first ? kLinkGenerationBase + 1 : last.generation + 1;
    }
    return true;
}

}  // namespace chordlab
