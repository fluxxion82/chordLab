#include "doctest.h"

#include "bridge/command_lineage.h"
#include "bridge/link_outcome.h"
#include "chordlab/link_state.h"

#include <cstring>

using chordlab::LinkFollowResult;
using chordlab::bridge::CommandLineage;
using chordlab::bridge::kLineageFailedError;
using chordlab::bridge::kMalformedLineageError;
using chordlab::bridge::LinkOutcome;
using chordlab::bridge::linkOutcome;

namespace {

constexpr bool kFollow = true;
constexpr bool kUnfollow = false;
constexpr bool kChanged = true;
constexpr bool kUnchanged = false;

const LinkFollowResult kResults[] = {LinkFollowResult::Applied, LinkFollowResult::StaleLeader,
                                     LinkFollowResult::UnknownLeader, LinkFollowResult::UnknownLane};

bool quiet(const LinkOutcome& o) {
    return !o.markDirty && !o.pushSnapshot && o.error == nullptr;
}

}  // namespace

TEST_CASE("linkOutcome: an applied follow that changed the saved follow marks the host dirty, nothing else") {
    for (const CommandLineage lineage : {CommandLineage::Absent, CommandLineage::Current}) {
        const LinkOutcome o = linkOutcome(lineage, kFollow, LinkFollowResult::Applied, kChanged);
        CHECK(o.markDirty);
        CHECK_FALSE(o.pushSnapshot);
        CHECK(o.error == nullptr);
    }
}

TEST_CASE("linkOutcome: an applied follow that saves what was saved already (the same link, the same hints) is clean") {
    for (const CommandLineage lineage : {CommandLineage::Absent, CommandLineage::Current}) {
        CHECK(quiet(linkOutcome(lineage, kFollow, LinkFollowResult::Applied, kUnchanged)));
    }
}

TEST_CASE("linkOutcome: a refused follow changes nothing and is no error: the fresh link state is the answer") {
    for (const CommandLineage lineage : {CommandLineage::Absent, CommandLineage::Current}) {
        for (const LinkFollowResult result :
             {LinkFollowResult::StaleLeader, LinkFollowResult::UnknownLeader, LinkFollowResult::UnknownLane}) {
            CHECK(quiet(linkOutcome(lineage, kFollow, result, kUnchanged)));
            // A refusal never changes the follow; even if a caller said so, it never dirties the host.
            CHECK(quiet(linkOutcome(lineage, kFollow, result, kChanged)));
        }
    }
}

TEST_CASE("linkOutcome: an unfollow dirties the host only when the instance was following") {
    for (const CommandLineage lineage : {CommandLineage::Absent, CommandLineage::Current}) {
        const LinkOutcome wasFollowing = linkOutcome(lineage, kUnfollow, LinkFollowResult::Applied, kChanged);
        CHECK(wasFollowing.markDirty);
        CHECK_FALSE(wasFollowing.pushSnapshot);
        CHECK(wasFollowing.error == nullptr);
        CHECK(quiet(linkOutcome(lineage, kUnfollow, LinkFollowResult::Applied, kUnchanged)));
        // An unfollow has no follow result: whatever is passed for it is ignored.
        for (const LinkFollowResult result : kResults) {
            CHECK(linkOutcome(lineage, kUnfollow, result, kChanged).markDirty);
            CHECK(quiet(linkOutcome(lineage, kUnfollow, result, kUnchanged)));
        }
    }
}

TEST_CASE("linkOutcome: a stale lineage applies nothing and is answered with the snapshot, no error") {
    for (const bool follow : {kFollow, kUnfollow}) {
        for (const LinkFollowResult result : kResults) {
            for (const bool changed : {kChanged, kUnchanged}) {
                const LinkOutcome o = linkOutcome(CommandLineage::Stale, follow, result, changed);
                CHECK_FALSE(o.markDirty);
                CHECK(o.pushSnapshot);
                CHECK(o.error == nullptr);
            }
        }
    }
}

TEST_CASE("linkOutcome: a malformed or unreadable lineage applies nothing and is the command path's error") {
    for (const bool follow : {kFollow, kUnfollow}) {
        for (const LinkFollowResult result : kResults) {
            for (const bool changed : {kChanged, kUnchanged}) {
                const LinkOutcome malformed = linkOutcome(CommandLineage::Malformed, follow, result, changed);
                CHECK_FALSE(malformed.markDirty);
                CHECK_FALSE(malformed.pushSnapshot);
                REQUIRE(malformed.error != nullptr);
                CHECK(std::strcmp(malformed.error, kMalformedLineageError) == 0);

                const LinkOutcome failed = linkOutcome(CommandLineage::Failed, follow, result, changed);
                CHECK_FALSE(failed.markDirty);
                CHECK_FALSE(failed.pushSnapshot);
                REQUIRE(failed.error != nullptr);
                CHECK(std::strcmp(failed.error, kLineageFailedError) == 0);
            }
        }
    }
}

TEST_CASE("linkOutcome: the endpoint is called exactly for an absent or current lineage, as for a command") {
    CHECK(chordlab::bridge::linkApplies(CommandLineage::Absent));
    CHECK(chordlab::bridge::linkApplies(CommandLineage::Current));
    CHECK_FALSE(chordlab::bridge::linkApplies(CommandLineage::Stale));
    CHECK_FALSE(chordlab::bridge::linkApplies(CommandLineage::Malformed));
    CHECK_FALSE(chordlab::bridge::linkApplies(CommandLineage::Failed));
}
