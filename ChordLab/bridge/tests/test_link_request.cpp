#include "doctest.h"

#include "bridge/link_request.h"

#include <optional>
#include <string>

using chordlab::bridge::LinkRequest;
using chordlab::bridge::parseLinkRequest;

namespace {

const std::string kHeaderProblem = "link message needs schema 1 and op \"follow\" or \"unfollow\"";
const std::string kFollowProblem = "link follow needs a leader, a lane and a leaderLineage of 1..64 bytes";
const std::string kParseProblem = "link message could not be parsed";

// The problem text for a payload parseLinkRequest refuses; fails the test if it was accepted.
std::string problemOf(const std::string& json) {
    std::string problem;
    const auto request = parseLinkRequest(json, &problem);
    CHECK_FALSE(request.has_value());
    CHECK_FALSE(problem.empty());
    return problem;
}

// The request for a payload parseLinkRequest accepts, leaving `problem` untouched.
LinkRequest accepted(const std::string& json) {
    std::string problem = "untouched";
    const auto request = parseLinkRequest(json, &problem);
    REQUIRE(request.has_value());
    CHECK(problem == "untouched");
    return *request;
}

// A follow request with the three strings given verbatim (already JSON-quoted, or any other JSON value).
std::string follow(const std::string& leader, const std::string& lane, const std::string& lineage) {
    return R"({"schema":1,"op":"follow","leader":)" + leader + R"(,"lane":)" + lane + R"(,"leaderLineage":)" + lineage +
           "}";
}

}  // namespace

TEST_CASE("parseLinkRequest: a follow carries the leader, the lane and the leader's lineage") {
    const LinkRequest r = accepted(follow(R"("leader-1")", R"("uid-7")", R"("lineage-3")"));
    CHECK(r.follow);
    CHECK(r.leaderId == "leader-1");
    CHECK(r.laneUid == "uid-7");
    CHECK(r.leaderLineage == "lineage-3");
    const LinkRequest chords = accepted(follow(R"("leader-1")", R"("chords")", R"("l")"));
    CHECK(chords.laneUid == "chords");
}

TEST_CASE("parseLinkRequest: an unfollow reads nothing else") {
    const LinkRequest r = accepted(R"({"schema":1,"op":"unfollow"})");
    CHECK_FALSE(r.follow);
    CHECK(r.leaderId.empty());
    CHECK(r.laneUid.empty());
    CHECK(r.leaderLineage.empty());
    // Bad follow members are not read on an unfollow.
    const LinkRequest d = accepted(R"({"schema":1,"op":"unfollow","leader":5,"lane":"","leaderLineage":null})");
    CHECK_FALSE(d.follow);
    CHECK(d.leaderId.empty());
}

TEST_CASE("parseLinkRequest: the problem pointer is optional") {
    CHECK(parseLinkRequest(R"({"schema":1,"op":"unfollow"})").has_value());
    CHECK_FALSE(parseLinkRequest("nope").has_value());
}

TEST_CASE("parseLinkRequest: schema must be the integer 1 and op one of the two strings") {
    CHECK(problemOf(R"({"op":"unfollow"})") == kHeaderProblem);
    CHECK(problemOf(R"({"schema":2,"op":"unfollow"})") == kHeaderProblem);
    CHECK(problemOf(R"({"schema":1.0,"op":"unfollow"})") == kHeaderProblem);
    CHECK(problemOf(R"({"schema":"1","op":"unfollow"})") == kHeaderProblem);
    CHECK(problemOf(R"({"schema":4294967297,"op":"unfollow"})") == kHeaderProblem);
    CHECK(problemOf(R"({"schema":1})") == kHeaderProblem);
    CHECK(problemOf(R"({"schema":1,"op":null})") == kHeaderProblem);
    CHECK(problemOf(R"({"schema":1,"op":1})") == kHeaderProblem);
    CHECK(problemOf(R"({"schema":1,"op":["follow"]})") == kHeaderProblem);
    CHECK(problemOf(R"({"schema":1,"op":"Follow"})") == kHeaderProblem);
    CHECK(problemOf(R"({"schema":1,"op":"link"})") == kHeaderProblem);
    CHECK(problemOf(R"({"schema":1,"op":""})") == kHeaderProblem);
    CHECK(problemOf(R"({"schema":1,"op":"unfollow "})") == kHeaderProblem);
}

TEST_CASE("parseLinkRequest: malformed JSON, an empty payload and a non-object are refused") {
    CHECK(problemOf("") == kHeaderProblem);
    CHECK(problemOf("{") == kHeaderProblem);
    CHECK(problemOf(R"({"schema":1,"op":"unfollow")") == kHeaderProblem);
    CHECK(problemOf("[]") == kHeaderProblem);
    CHECK(problemOf("null") == kHeaderProblem);
    CHECK(problemOf(R"("unfollow")") == kHeaderProblem);
    CHECK(problemOf(R"({"schema":1,"op":"unfollow"} x)") == kHeaderProblem);
}

TEST_CASE("parseLinkRequest: a follow needs each of the three strings, present, non-empty and at most 64 bytes") {
    const std::string ok = R"("ok")";
    for (int which = 0; which < 3; ++which) {
        CAPTURE(which);
        auto with = [&](const std::string& value) {
            return follow(which == 0 ? value : ok, which == 1 ? value : ok, which == 2 ? value : ok);
        };
        CHECK(problemOf(with(R"("")")) == kFollowProblem);
        CHECK(problemOf(with("null")) == kFollowProblem);
        CHECK(problemOf(with("7")) == kFollowProblem);
        CHECK(problemOf(with("true")) == kFollowProblem);
        CHECK(problemOf(with("[]")) == kFollowProblem);
        CHECK(problemOf(with(R"({"id":"x"})")) == kFollowProblem);
        CHECK(problemOf(with("\"" + std::string(65, 'a') + "\"")) == kFollowProblem);
        // Exactly 64 bytes is fine (the bound is bytes: 32 two-byte characters are 64).
        std::string twoByte;
        for (int i = 0; i < 32; ++i)
            twoByte += "\xC3\xA9";
        CHECK(accepted(with("\"" + std::string(64, 'a') + "\"")).follow);
        CHECK(accepted(with("\"" + twoByte + "\"")).follow);
        CHECK(problemOf(with("\"" + twoByte + "a\"")) == kFollowProblem);
    }
    CHECK(problemOf(R"({"schema":1,"op":"follow","lane":"u","leaderLineage":"l"})") == kFollowProblem);
    CHECK(problemOf(R"({"schema":1,"op":"follow","leader":"x","leaderLineage":"l"})") == kFollowProblem);
    CHECK(problemOf(R"({"schema":1,"op":"follow","leader":"x","lane":"u"})") == kFollowProblem);
    CHECK(problemOf(R"({"schema":1,"op":"follow"})") == kFollowProblem);
    // An escaped character counts as its decoded bytes: "é" is two bytes, so 32 of them fit.
    std::string escaped;
    for (int i = 0; i < 32; ++i)
        escaped += "\\u00e9";
    CHECK(accepted(follow("\"" + escaped + "\"", R"("u")", R"("l")")).leaderId.size() == 64);
}

TEST_CASE("parseLinkRequest: a raw NUL byte is refused before any parse; an escaped one is a character") {
    std::string withNul = R"({"schema":1,"op":"unfollow"})";
    withNul.insert(withNul.size() / 2, 1, '\0');
    CHECK(problemOf(withNul) == kParseProblem);
    CHECK(problemOf(std::string(R"({"schema":1,"op":"unfollow"})") + std::string(1, '\0') + "junk") == kParseProblem);
    CHECK(problemOf(std::string(1, '\0')) == kParseProblem);
    const LinkRequest r = accepted(follow(R"("a\u0000b")", R"("u")", R"("l")"));
    CHECK(r.leaderId == std::string("a\0b", 3));
}

TEST_CASE("parseLinkRequest: extra members are ignored, the message's own lineage among them, whatever its type") {
    const LinkRequest r = accepted(R"({"schema":1,"op":"follow","leader":"x","lane":"u","leaderLineage":"l",
                                       "lineage":"editor-token","future":[1,2],"leaderLabel":5})");
    CHECK(r.follow);
    CHECK(r.leaderId == "x");
    CHECK(r.leaderLineage == "l");  // never the message's own lineage
    for (const char* lineage : {R"("t")", "null", "5", "[]", "{}", R"("")"}) {
        CAPTURE(lineage);
        CHECK(accepted(std::string(R"({"schema":1,"op":"unfollow","lineage":)") + lineage + "}").follow == false);
        CHECK(accepted(std::string(R"({"schema":1,"op":"follow","leader":"x","lane":"u","leaderLineage":"l","lineage":)") +
                       lineage + "}")
                  .leaderLineage == "l");
    }
    // A follow without leaderLineage is refused even when the message's own lineage is there.
    CHECK(problemOf(R"({"schema":1,"op":"follow","leader":"x","lane":"u","lineage":"l"})") == kFollowProblem);
}

TEST_CASE("parseLinkRequest: a member given twice takes its last value, as nlohmann reads it") {
    CHECK_FALSE(accepted(R"({"schema":1,"op":"follow","op":"unfollow"})").follow);
    CHECK(accepted(follow(R"("a","leader":"b")", R"("u")", R"("l")")).leaderId == "b");
}
