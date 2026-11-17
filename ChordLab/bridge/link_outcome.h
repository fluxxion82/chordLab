#pragma once

// Plain C++17, header-only: no iPlug2, no Objective-C, no Foundation, so the plug-in shell's message
// handling can be unit-tested outside Xcode (ChordLab/bridge/tests). Engine headers: command_lineage.h's and
// link_state.h (plain data; LinkFollowResult lives there so that this needs no link.h).

#include "command_lineage.h"
#include "chordlab/link_state.h"

namespace chordlab::bridge {

/// What a kMsgTagLink message (per-track instruments plan I9, I12) leads to after it was parsed (parseLinkRequest)
/// and its document lineage compared (readLineage before mModelMutex, compareLineage under it), beyond what the
/// shell always does: whatever the outcome, an open editor then gets the fresh link state (kMsgTagLinkState), which
/// is the answer to a refused follow.
struct LinkOutcome
{
    bool markDirty = false;          // MarkHostDirty(): the state SerializeState would write has changed
    bool pushSnapshot = false;       // PushSnapshot(): a stale lineage, so the editor gets the current token first
    const char* error = nullptr;     // non-null: PushError(error), the command path's wording
};

/// Whether the shell calls the endpoint (LinkEndpoint::follow or unfollow) at all: exactly as for a command, only
/// for an absent or a current lineage (lineageApplies). A stale, malformed or unreadable one applies nothing.
inline bool linkApplies(CommandLineage lineage) noexcept
{
    return lineageApplies(lineage);
}

/// The outcome of a link message, from:
///  - `lineage`: compareLineage's answer for the message's own `lineage` member;
///  - `follow`: the request's kind (true: follow, false: unfollow);
///  - `result`: what LinkEndpoint::follow returned (follow only; ignored for an unfollow, and when the lineage does
///    not apply, since then nothing was called);
///  - `savedFollowChanged`: whether the follow the state would save (LinkEndpoint::state().follow, read in the same
///    mModelMutex section before and after the call) differs. For an unfollow that is "it was following" (waiting
///    or bound: either is saved); for an applied follow it is false when the same link with the same hints was
///    already saved. Ignored when nothing was called.
/// Rows:
///  - Stale: nothing applied, `pushSnapshot`, no error (a click that a state load overtook; as a command).
///  - Malformed, Failed: nothing applied, `error` kMalformedLineageError / kLineageFailedError (as a command).
///  - Absent or Current, follow: `markDirty` only when `result` is Applied AND the saved follow changed; a refused
///    follow (StaleLeader, UnknownLeader, UnknownLane) changes nothing and is no error.
///  - Absent or Current, unfollow: `markDirty` only when the saved follow changed (the instance was following).
inline LinkOutcome linkOutcome(CommandLineage lineage, bool follow, LinkFollowResult result,
                               bool savedFollowChanged) noexcept
{
    LinkOutcome outcome;
    switch (lineage)
    {
        case CommandLineage::Stale:
            outcome.pushSnapshot = true;
            return outcome;
        case CommandLineage::Malformed:
            outcome.error = kMalformedLineageError;
            return outcome;
        case CommandLineage::Failed:
            outcome.error = kLineageFailedError;
            return outcome;
        case CommandLineage::Absent:
        case CommandLineage::Current:
            break;
    }
    const bool changedByThis = follow ? result == LinkFollowResult::Applied : true;
    outcome.markDirty = changedByThis && savedFollowChanged;
    return outcome;
}

}  // namespace chordlab::bridge
