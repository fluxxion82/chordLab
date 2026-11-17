#pragma once

// Plain C++17, header-only: no iPlug2, no Objective-C, no Foundation, so the plug-in shell's message
// handling can be unit-tested outside Xcode (ChordLab/bridge/tests). Engine headers: command_lineage.h's and
// link_state.h (plain data; LinkFollowResult lives there so that this needs no link.h).

#include "command_lineage.h"
#include "chordlab/link_state.h"

namespace chordlab::bridge {

/// The error a link message without its own `lineage` member is answered with (plan I9: every link command carries
/// the follower's lineage; no editor that predates links sends one, so a missing lineage is refused, not let through
/// unchecked as a command's is).
inline constexpr const char* kMissingLinkLineageError = "a link message must carry \"lineage\"";

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

/// Whether the shell calls the endpoint (LinkEndpoint::follow or unfollow) at all: only for an instance that links
/// (`linking`: it is in the process's shared registry, the VST3; plan I15), and then only for a current lineage. An
/// absent one applies nothing (unlike a command's: a link message must carry the follower's lineage, plan I9), nor
/// does a stale, malformed or unreadable one. An instance with a registry of its own (the AU, the app) never applies
/// a link message: nothing could ever bind there.
inline bool linkApplies(CommandLineage lineage, bool linking) noexcept
{
    return linking && lineage == CommandLineage::Current;
}

/// What a state load hands LinkEndpoint::adopt: the loaded instance block, without its follow when the instance does
/// not link (plan I15). In a registry of its own a follow could never bind, and the instance would play an empty
/// link buffer for ever; without it, the instance plays its own document and its next save writes no follow. The id,
/// the document id and the lane uids are adopted either way.
inline InstanceState adoptableState(InstanceState loaded, bool linking)
{
    if (!linking)
        loaded.follow.reset();
    return loaded;
}

/// The outcome of a link message, from:
///  - `linking`: whether the instance links (see linkApplies); when it does not, nothing was called and the outcome
///    is empty whatever the rest says (the link state alone answers);
///  - `lineage`: compareLineage's answer for the message's own `lineage` member;
///  - `follow`: the request's kind (true: follow, false: unfollow);
///  - `result`: what LinkEndpoint::follow returned (follow only; ignored for an unfollow, and when the lineage does
///    not apply, since then nothing was called);
///  - `savedFollowChanged`: whether the follow the state would save (LinkEndpoint::state().follow, read in the same
///    mModelMutex section before and after the call) differs. For an unfollow that is "it was following" (waiting
///    or bound: either is saved); for an applied follow it is false when the same link with the same hints was
///    already saved. Ignored when nothing was called.
/// Rows:
///  - Not linking: nothing applied, nothing else (no dirty, no snapshot, no error), for every lineage.
///  - Stale: nothing applied, `pushSnapshot`, no error (a click that a state load overtook; as a command).
///  - Malformed, Failed: nothing applied, `error` kMalformedLineageError / kLineageFailedError (as a command).
///  - Absent: nothing applied, `error` kMissingLinkLineageError (a link message must carry its lineage, plan I9).
///  - Current, follow: `markDirty` only when `result` is Applied AND the saved follow changed; a refused follow
///    (StaleLeader, UnknownLeader, UnknownLane) changes nothing and is no error.
///  - Current, unfollow: `markDirty` only when the saved follow changed (the instance was following).
inline LinkOutcome linkOutcome(bool linking, CommandLineage lineage, bool follow, LinkFollowResult result,
                               bool savedFollowChanged) noexcept
{
    LinkOutcome outcome;
    if (!linking)
        return outcome;
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
            outcome.error = kMissingLinkLineageError;
            return outcome;
        case CommandLineage::Current:
            break;
    }
    const bool changedByThis = follow ? result == LinkFollowResult::Applied : true;
    outcome.markDirty = changedByThis && savedFollowChanged;
    return outcome;
}

}  // namespace chordlab::bridge
