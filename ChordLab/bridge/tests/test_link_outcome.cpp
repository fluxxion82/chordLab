#include "doctest.h"

#include "bridge/command_lineage.h"
#include "bridge/link_outcome.h"
#include "chordlab/link_state.h"

#include <optional>

#include <cstring>

using chordlab::InstanceState;
using chordlab::LinkFollow;
using chordlab::LinkFollowResult;
using chordlab::bridge::adoptableState;
using chordlab::bridge::CommandLineage;
using chordlab::bridge::kLineageFailedError;
using chordlab::bridge::kMalformedLineageError;
using chordlab::bridge::kMissingLinkLineageError;
using chordlab::bridge::LinkOutcome;
using chordlab::bridge::linkOutcome;

namespace {

constexpr bool kLinking = true;      // the instance is in the shared registry (the VST3)
constexpr bool kNotLinking = false;  // a registry of its own (the AU, the app): plan I15
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
    for (const CommandLineage lineage : {CommandLineage::Current}) {   // Absent is refused (plan I9)
        const LinkOutcome o = linkOutcome(kLinking, lineage, kFollow, LinkFollowResult::Applied, kChanged);
        CHECK(o.markDirty);
        CHECK_FALSE(o.pushSnapshot);
        CHECK(o.error == nullptr);
    }
}

TEST_CASE("linkOutcome: an applied follow that saves what was saved already (the same link, the same hints) is clean") {
    for (const CommandLineage lineage : {CommandLineage::Current}) {   // Absent is refused (plan I9)
        CHECK(quiet(linkOutcome(kLinking, lineage, kFollow, LinkFollowResult::Applied, kUnchanged)));
    }
}

TEST_CASE("linkOutcome: a refused follow changes nothing and is no error: the fresh link state is the answer") {
    for (const CommandLineage lineage : {CommandLineage::Current}) {   // Absent is refused (plan I9)
        for (const LinkFollowResult result :
             {LinkFollowResult::StaleLeader, LinkFollowResult::UnknownLeader, LinkFollowResult::UnknownLane}) {
            CHECK(quiet(linkOutcome(kLinking, lineage, kFollow, result, kUnchanged)));
            // A refusal never changes the follow; even if a caller said so, it never dirties the host.
            CHECK(quiet(linkOutcome(kLinking, lineage, kFollow, result, kChanged)));
        }
    }
}

TEST_CASE("linkOutcome: an unfollow dirties the host only when the instance was following") {
    for (const CommandLineage lineage : {CommandLineage::Current}) {   // Absent is refused (plan I9)
        const LinkOutcome wasFollowing = linkOutcome(kLinking, lineage, kUnfollow, LinkFollowResult::Applied, kChanged);
        CHECK(wasFollowing.markDirty);
        CHECK_FALSE(wasFollowing.pushSnapshot);
        CHECK(wasFollowing.error == nullptr);
        CHECK(quiet(linkOutcome(kLinking, lineage, kUnfollow, LinkFollowResult::Applied, kUnchanged)));
        // An unfollow has no follow result: whatever is passed for it is ignored.
        for (const LinkFollowResult result : kResults) {
            CHECK(linkOutcome(kLinking, lineage, kUnfollow, result, kChanged).markDirty);
            CHECK(quiet(linkOutcome(kLinking, lineage, kUnfollow, result, kUnchanged)));
        }
    }
}

TEST_CASE("linkOutcome: a stale lineage applies nothing and is answered with the snapshot, no error") {
    for (const bool follow : {kFollow, kUnfollow}) {
        for (const LinkFollowResult result : kResults) {
            for (const bool changed : {kChanged, kUnchanged}) {
                const LinkOutcome o = linkOutcome(kLinking, CommandLineage::Stale, follow, result, changed);
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
                const LinkOutcome malformed = linkOutcome(kLinking, CommandLineage::Malformed, follow, result, changed);
                CHECK_FALSE(malformed.markDirty);
                CHECK_FALSE(malformed.pushSnapshot);
                REQUIRE(malformed.error != nullptr);
                CHECK(std::strcmp(malformed.error, kMalformedLineageError) == 0);

                const LinkOutcome failed = linkOutcome(kLinking, CommandLineage::Failed, follow, result, changed);
                CHECK_FALSE(failed.markDirty);
                CHECK_FALSE(failed.pushSnapshot);
                REQUIRE(failed.error != nullptr);
                CHECK(std::strcmp(failed.error, kLineageFailedError) == 0);
            }
        }
    }
}

TEST_CASE("linkOutcome: the endpoint is called exactly for a current lineage (a link message must carry one)") {
    CHECK_FALSE(chordlab::bridge::linkApplies(CommandLineage::Absent, kLinking));
    CHECK(chordlab::bridge::linkApplies(CommandLineage::Current, kLinking));
    CHECK_FALSE(chordlab::bridge::linkApplies(CommandLineage::Stale, kLinking));
    CHECK_FALSE(chordlab::bridge::linkApplies(CommandLineage::Malformed, kLinking));
    CHECK_FALSE(chordlab::bridge::linkApplies(CommandLineage::Failed, kLinking));
}

TEST_CASE("linkOutcome: a link message without its own lineage applies nothing and is an error of its own") {
    // Plan I9: every link command carries the follower's own lineage; no editor that predates links sends tag 18,
    // so, unlike a command, a missing one is not let through unchecked.
    for (const bool follow : {kFollow, kUnfollow}) {
        for (const LinkFollowResult result : kResults) {
            for (const bool changed : {kChanged, kUnchanged}) {
                const LinkOutcome o = linkOutcome(kLinking, CommandLineage::Absent, follow, result, changed);
                CHECK_FALSE(o.markDirty);
                CHECK_FALSE(o.pushSnapshot);
                REQUIRE(o.error != nullptr);
                CHECK(std::strcmp(o.error, kMissingLinkLineageError) == 0);
                CHECK(std::strcmp(o.error, kMalformedLineageError) != 0);
                CHECK(std::strcmp(o.error, kLineageFailedError) != 0);
            }
        }
    }
}

TEST_CASE("linkOutcome: an instance that is not linking applies no link message and answers with the link state alone") {
    for (const CommandLineage lineage : {CommandLineage::Absent, CommandLineage::Current, CommandLineage::Stale,
                                         CommandLineage::Malformed, CommandLineage::Failed}) {
        CHECK_FALSE(chordlab::bridge::linkApplies(lineage, kNotLinking));
        for (const bool follow : {kFollow, kUnfollow}) {
            for (const LinkFollowResult result : kResults) {
                for (const bool changed : {kChanged, kUnchanged}) {
                    CHECK(quiet(linkOutcome(kNotLinking, lineage, follow, result, changed)));
                }
            }
        }
    }
}

TEST_CASE("adoptableState: a linking instance adopts the loaded block as it is") {
    InstanceState loaded;
    loaded.id = "A";
    loaded.documentId = "D";
    loaded.lanes = {{1, "U1"}, {2, "U2"}};
    loaded.follow = LinkFollow{"L", "LD", "chords", "Chords", "", "ChordLab A"};
    CHECK(adoptableState(loaded, kLinking) == loaded);
    loaded.follow.reset();
    CHECK(adoptableState(loaded, kLinking) == loaded);
}

TEST_CASE("adoptableState: an instance that is not linking drops a loaded follow and keeps the rest") {
    InstanceState loaded;
    loaded.id = "A";
    loaded.documentId = "D";
    loaded.lanes = {{1, "U1"}};
    loaded.follow = LinkFollow{"L", "LD", "U9", "Bass", "bass", "ChordLab B"};
    const InstanceState adopted = adoptableState(loaded, kNotLinking);
    CHECK_FALSE(adopted.follow.has_value());  // it plays its own document; its next save writes no follow
    CHECK(adopted.id == "A");
    CHECK(adopted.documentId == "D");
    CHECK(adopted.lanes == loaded.lanes);
    loaded.follow.reset();
    CHECK(adoptableState(loaded, kNotLinking) == loaded);  // nothing to drop
}
