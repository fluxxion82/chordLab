#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>

namespace chordlab {

// What an instance saves of its link (per-track instruments plan I11): plain data only, shared by link.h (the
// endpoint that produces and adopts it) and json_api.h (the state's `instance` block). Standard library only.

/// The longest instance id, document id, lane uid or lineage token the state and the link messages accept, in bytes.
constexpr std::size_t kMaxLinkIdBytes = 64;
/// The longest leader label (a follow's `leaderLabel` hint) the state accepts, in bytes.
constexpr std::size_t kMaxLinkLabelBytes = 96;

/// What a follower follows: persisted with the follower (plan I11). `leaderId`, `documentId` and `laneUid` name
/// the link; the other three are hints for messages only and never decide anything.
struct LinkFollow {
    std::string leaderId;      // the leader instance's persistent id
    std::string documentId;    // the document the link was made in
    std::string laneUid;       // kChordsLaneUid or a lane uid of that document
    std::string laneName;      // hints for messages only
    std::string laneRole;
    std::string leaderLabel;
};

/// All six fields equal.
inline bool operator==(const LinkFollow& a, const LinkFollow& b) noexcept {
    return a.leaderId == b.leaderId && a.documentId == b.documentId && a.laneUid == b.laneUid &&
           a.laneName == b.laneName && a.laneRole == b.laneRole && a.leaderLabel == b.leaderLabel;
}
inline bool operator!=(const LinkFollow& a, const LinkFollow& b) noexcept {
    return !(a == b);
}

/// What an instance saves and loads of its link (`LinkEndpoint::state` / `adopt`; the state's `instance` block,
/// json_api.h).
struct InstanceState {
    std::string id;                          // the instance's persistent id
    std::string documentId;                  // its document's id
    std::map<int, std::string> lanes;        // track number -> lane uid (`LaneUids::entries`)
    std::optional<LinkFollow> follow;        // set: it follows
};

/// All four fields equal.
inline bool operator==(const InstanceState& a, const InstanceState& b) {
    return a.id == b.id && a.documentId == b.documentId && a.lanes == b.lanes && a.follow == b.follow;
}
inline bool operator!=(const InstanceState& a, const InstanceState& b) {
    return !(a == b);
}

}  // namespace chordlab
