#include "doctest.h"

#include "bridge/preview_request.h"

#include <cstdint>
#include <optional>
#include <string>

using chordlab::PreviewCmd;
using chordlab::bridge::parsePreviewRequest;
using chordlab::bridge::PreviewRequest;

namespace {

const std::string kHeaderProblem = "preview message needs schema 1, an id and on:true|false";
const std::string kStartProblem = "preview start needs 1..8 pitches and a velocity in 1..127";
const std::string kPitchProblem = "preview pitches must be integers in 0..127";
const std::string kExtendProblem = "preview extend needs extend:true and no on";
const std::string kTrackProblem = "preview track must be an integer of 1 or more";

// The problem text for a payload parsePreviewRequest refuses; fails the test if it was accepted.
std::string problemOf(const std::string& json) {
    std::string problem;
    const auto cmd = parsePreviewRequest(json, &problem);
    CHECK_FALSE(cmd.has_value());
    CHECK_FALSE(problem.empty());
    return problem;
}

// The request for a payload parsePreviewRequest accepts, leaving `problem` untouched.
PreviewRequest request(const std::string& json) {
    std::string problem = "untouched";
    const auto parsed = parsePreviewRequest(json, &problem);
    REQUIRE(parsed.has_value());
    CHECK(problem == "untouched");
    return *parsed;
}

// Its command.
PreviewCmd accepted(const std::string& json) {
    return request(json).cmd;
}

}  // namespace

TEST_CASE("parsePreviewRequest: a Start with every field") {
    const PreviewCmd c = accepted(R"({"schema":1,"id":4,"on":true,"velocity":90,"pitches":[60,64,67]})");
    CHECK(c.kind == PreviewCmd::Start);
    CHECK(c.id == 4);
    CHECK(c.velocity == 90);
    REQUIRE(c.count == 3);
    CHECK(c.pitches[0] == 60);
    CHECK(c.pitches[1] == 64);
    CHECK(c.pitches[2] == 67);
    for (int i = 3; i < 8; ++i)
        CHECK(c.pitches[i] == 0);                                 // value-initialised
}

TEST_CASE("parsePreviewRequest: a Stop needs no pitches and reads nothing else") {
    const PreviewCmd c = accepted(R"({"schema":1,"id":4,"on":false})");
    CHECK(c.kind == PreviewCmd::Stop);
    CHECK(c.id == 4);
    CHECK(c.velocity == 0);
    CHECK(c.count == 0);
    for (int i = 0; i < 8; ++i)
        CHECK(c.pitches[i] == 0);                                 // value-initialised, every slot
    // A bad velocity and bad pitches are ignored on a Stop.
    const PreviewCmd d = accepted(R"({"schema":1,"id":5,"on":false,"velocity":0,"pitches":[999]})");
    CHECK(d.kind == PreviewCmd::Stop);
    CHECK(d.id == 5);
    CHECK(d.count == 0);
}

TEST_CASE("parsePreviewRequest: the problem pointer is optional") {
    CHECK(parsePreviewRequest(R"({"schema":1,"id":1,"on":false})").has_value());
    CHECK_FALSE(parsePreviewRequest("nope").has_value());
}

TEST_CASE("parsePreviewRequest: schema must be the integer 1") {
    accepted(R"({"schema":1,"id":0,"on":false})");
    CHECK(problemOf(R"({"schema":2,"id":0,"on":false})") == kHeaderProblem);
    CHECK(problemOf(R"({"schema":0,"id":0,"on":false})") == kHeaderProblem);
    CHECK(problemOf(R"({"schema":1.0,"id":0,"on":false})") == kHeaderProblem);
    CHECK(problemOf(R"({"schema":"1","id":0,"on":false})") == kHeaderProblem);
    CHECK(problemOf(R"({"schema":4294967297,"id":0,"on":false})") == kHeaderProblem);
    CHECK(problemOf(R"({"id":0,"on":false})") == kHeaderProblem);
}

TEST_CASE("parsePreviewRequest: id must be in 0..kForwardedPreviewIdFirst - 1") {
    CHECK(accepted(R"({"schema":1,"id":0,"on":false})").id == 0);
    CHECK(accepted(R"({"schema":1,"id":2146435070,"on":false})").id == 2146435070);
    CHECK(problemOf(R"({"schema":1,"id":-1,"on":false})") == kHeaderProblem);
    CHECK(problemOf(R"({"schema":1,"id":2147483648,"on":false})") == kHeaderProblem);
    CHECK(problemOf(R"({"schema":1,"id":18446744073709551615,"on":false})") == kHeaderProblem);
    CHECK(problemOf(R"({"schema":1,"id":1.0,"on":false})") == kHeaderProblem);
    CHECK(problemOf(R"({"schema":1,"on":false})") == kHeaderProblem);
}

TEST_CASE("parsePreviewRequest: on must be a boolean") {
    accepted(R"({"schema":1,"id":1,"on":true,"pitches":[60]})");
    CHECK(problemOf(R"({"schema":1,"id":1,"on":1,"pitches":[60]})") == kHeaderProblem);
    CHECK(problemOf(R"({"schema":1,"id":1,"on":"true","pitches":[60]})") == kHeaderProblem);
    CHECK(problemOf(R"({"schema":1,"id":1,"on":null})") == kHeaderProblem);
    CHECK(problemOf(R"({"schema":1,"id":1})") == kHeaderProblem);
}

TEST_CASE("parsePreviewRequest: malformed and non-object payloads") {
    CHECK(problemOf("") == kHeaderProblem);
    CHECK(problemOf("{") == kHeaderProblem);
    CHECK(problemOf(R"([{"schema":1,"id":1,"on":false}])") == kHeaderProblem);
    CHECK(problemOf("true") == kHeaderProblem);
}

TEST_CASE("parsePreviewRequest: velocity defaults to 100 and must be an integer in 1..127") {
    CHECK(accepted(R"({"schema":1,"id":1,"on":true,"pitches":[60]})").velocity == 100);
    CHECK(accepted(R"({"schema":1,"id":1,"on":true,"velocity":1,"pitches":[60]})").velocity == 1);
    CHECK(accepted(R"({"schema":1,"id":1,"on":true,"velocity":127,"pitches":[60]})").velocity == 127);
    CHECK(problemOf(R"({"schema":1,"id":1,"on":true,"velocity":0,"pitches":[60]})") == kStartProblem);
    CHECK(problemOf(R"({"schema":1,"id":1,"on":true,"velocity":128,"pitches":[60]})") == kStartProblem);
    CHECK(problemOf(R"({"schema":1,"id":1,"on":true,"velocity":100.0,"pitches":[60]})") == kStartProblem);
    CHECK(problemOf(R"({"schema":1,"id":1,"on":true,"velocity":null,"pitches":[60]})") == kStartProblem);
    CHECK(problemOf(R"({"schema":1,"id":1,"on":true,"velocity":4294967297,"pitches":[60]})") == kStartProblem);
    // Beyond INT64_MAX (stored as uint64): refused explicitly, never through a signed conversion.
    CHECK(problemOf(R"({"schema":1,"id":1,"on":true,"velocity":18446744073709551615,"pitches":[60]})") == kStartProblem);
    CHECK(problemOf(R"({"schema":1,"id":1,"on":true,"velocity":9223372036854775808,"pitches":[60]})") == kStartProblem);
}

TEST_CASE("parsePreviewRequest: a Start needs an array of 1..8 pitches") {
    CHECK(accepted(R"({"schema":1,"id":1,"on":true,"pitches":[60]})").count == 1);
    const PreviewCmd eight = accepted(R"({"schema":1,"id":1,"on":true,"pitches":[60,61,62,63,64,65,66,67]})");
    REQUIRE(eight.count == 8);
    CHECK(eight.pitches[7] == 67);
    CHECK(problemOf(R"({"schema":1,"id":1,"on":true,"pitches":[60,61,62,63,64,65,66,67,68]})") == kStartProblem);
    CHECK(problemOf(R"({"schema":1,"id":1,"on":true,"pitches":[]})") == kStartProblem);
    CHECK(problemOf(R"({"schema":1,"id":1,"on":true})") == kStartProblem);
    CHECK(problemOf(R"({"schema":1,"id":1,"on":true,"pitches":60})") == kStartProblem);
    CHECK(problemOf(R"({"schema":1,"id":1,"on":true,"pitches":{"0":60}})") == kStartProblem);
}

TEST_CASE("parsePreviewRequest: every pitch must be an integer in 0..127, order and duplicates kept") {
    const PreviewCmd c = accepted(R"({"schema":1,"id":1,"on":true,"pitches":[127,0,60,60]})");
    REQUIRE(c.count == 4);
    CHECK(c.pitches[0] == 127);
    CHECK(c.pitches[1] == 0);
    CHECK(c.pitches[2] == 60);
    CHECK(c.pitches[3] == 60);
    CHECK(problemOf(R"({"schema":1,"id":1,"on":true,"pitches":[60,128]})") == kPitchProblem);
    CHECK(problemOf(R"({"schema":1,"id":1,"on":true,"pitches":[-1]})") == kPitchProblem);
    CHECK(problemOf(R"({"schema":1,"id":1,"on":true,"pitches":[60.0]})") == kPitchProblem);
    CHECK(problemOf(R"({"schema":1,"id":1,"on":true,"pitches":["60"]})") == kPitchProblem);
    CHECK(problemOf(R"({"schema":1,"id":1,"on":true,"pitches":[null]})") == kPitchProblem);
    CHECK(problemOf(R"({"schema":1,"id":1,"on":true,"pitches":[true]})") == kPitchProblem);
    // Seven good pitches, then a bad eighth: the whole Start is refused.
    CHECK(problemOf(R"({"schema":1,"id":1,"on":true,"pitches":[60,62,64,65,67,69,71,128]})") == kPitchProblem);
    CHECK(problemOf(R"({"schema":1,"id":1,"on":true,"pitches":[18446744073709551615]})") == kPitchProblem);
    CHECK(problemOf(R"({"schema":1,"id":1,"on":true,"pitches":[60,9223372036854775808]})") == kPitchProblem);
    CHECK(problemOf(R"({"schema":1,"id":1,"on":true,"pitches":[4294967356]})") == kPitchProblem);   // 2^32 + 60
}

TEST_CASE("parsePreviewRequest: the first failing rule decides the problem") {
    // A bad velocity and a bad pitch: the Start rule (velocity, array shape) is checked before pitches.
    CHECK(problemOf(R"({"schema":1,"id":1,"on":true,"velocity":0,"pitches":[200]})") == kStartProblem);
    // A bad id with a bad Start body: the header rule wins.
    CHECK(problemOf(R"({"schema":1,"id":-1,"on":true,"pitches":[]})") == kHeaderProblem);
}

TEST_CASE("parsePreviewRequest: trailing junk after the object is malformed") {
    CHECK(problemOf(R"({"schema":1,"id":1,"on":false}junk)") == kHeaderProblem);
    CHECK(problemOf(R"({"schema":1,"id":1,"on":false} {})") == kHeaderProblem);
}

TEST_CASE("parsePreviewRequest: a raw NUL byte anywhere is refused before the parse") {
    // As the engine's prescan. Without it nlohmann would stop at the NUL and accept the trailing case.
    const std::string kParseProblem = "preview message could not be parsed";
    CHECK(problemOf(std::string("{\"schema\":1,\"id\":3,\"on\":false}\0junk", 35)) == kParseProblem);
    CHECK(problemOf(std::string("{\"schema\":1,\"id\":3,\"on\":false}\0", 31)) == kParseProblem);
    CHECK(problemOf(std::string("{\"schema\":1,\0\"id\":3,\"on\":false}", 31)) == kParseProblem);
    CHECK(problemOf(std::string("\0", 1)) == kParseProblem);
}

TEST_CASE("parsePreviewRequest: an escaped \\u0000 is an ordinary string character") {
    const PreviewCmd c = accepted(R"({"schema":1,"id":3,"on":false,"label":"a\u0000b"})");
    CHECK(c.kind == PreviewCmd::Stop);
    CHECK(c.id == 3);
}

TEST_CASE("parsePreviewRequest: duplicate members, the last one wins") {
    // Pinned parser behaviour (nlohmann overwrites a repeated key), not a rule of ours.
    CHECK(problemOf(R"({"schema":1,"schema":2,"id":1,"on":false})") == kHeaderProblem);
    CHECK(accepted(R"({"schema":2,"schema":1,"id":1,"on":false})").id == 1);
    CHECK(accepted(R"({"schema":1,"id":1,"id":9,"on":false})").id == 9);
}

TEST_CASE("parsePreviewRequest: extra members are ignored") {
    const PreviewCmd c = accepted(R"({"schema":1,"id":2,"on":true,"pitches":[60],"label":"I","extra":{"a":[1]}})");
    CHECK(c.kind == PreviewCmd::Start);
    CHECK(c.count == 1);
}

TEST_CASE("parsePreviewRequest: an Extend") {
    const PreviewCmd c = accepted(R"({"schema":1,"id":4,"extend":true})");
    CHECK(c.kind == PreviewCmd::Extend);
    CHECK(c.id == 4);
    CHECK(c.count == 0);
}

TEST_CASE("parsePreviewRequest: extend must be true, alone, with schema and id") {
    CHECK(problemOf(R"({"schema":1,"id":4,"extend":false})") == kExtendProblem);
    CHECK(problemOf(R"({"schema":1,"id":4,"extend":1})") == kExtendProblem);
    CHECK(problemOf(R"({"schema":1,"id":4,"extend":true,"on":true,"pitches":[60]})") == kExtendProblem);
    CHECK(problemOf(R"({"schema":1,"id":4,"extend":true,"on":false})") == kExtendProblem);
    CHECK(problemOf(R"({"schema":1,"extend":true})") == kHeaderProblem);
    CHECK(problemOf(R"({"id":4,"extend":true})") == kHeaderProblem);
}

TEST_CASE("parsePreviewRequest: the ids a link registry forwards under are refused, whatever the kind") {
    // D5: an editor can never produce an id of the reserved range, so a forwarded id never meets an editor's.
    static_assert(chordlab::kForwardedPreviewIdFirst == 2146435070 + 1, "the bound the payloads below spell out");
    static_assert(chordlab::kForwardedPreviewIdLast == 2147483647, "the bound the payloads below spell out");
    CHECK(problemOf(R"({"schema":1,"id":2146435071,"on":false})") == kHeaderProblem);          // the first
    CHECK(problemOf(R"({"schema":1,"id":2147000000,"on":false})") == kHeaderProblem);
    CHECK(problemOf(R"({"schema":1,"id":2147483647,"on":false})") == kHeaderProblem);          // the last, INT_MAX
    CHECK(problemOf(R"({"schema":1,"id":2146435071,"on":true,"pitches":[60]})") == kHeaderProblem);
    CHECK(problemOf(R"({"schema":1,"id":2146435071,"on":true,"pitches":[60],"track":2})") == kHeaderProblem);
    CHECK(problemOf(R"({"schema":1,"id":2147483647,"extend":true})") == kHeaderProblem);
}

TEST_CASE("parsePreviewRequest: a Start without track belongs to the chords lane") {
    const PreviewRequest r = request(R"({"schema":1,"id":4,"on":true,"pitches":[60]})");
    CHECK(r.cmd.kind == PreviewCmd::Start);
    CHECK_FALSE(r.track.has_value());
}

TEST_CASE("parsePreviewRequest: a Start's track is the lane of that track") {
    const PreviewRequest r = request(R"({"schema":1,"id":4,"on":true,"velocity":90,"pitches":[36,43],"track":3})");
    CHECK(r.cmd.kind == PreviewCmd::Start);
    CHECK(r.cmd.id == 4);
    CHECK(r.cmd.velocity == 90);
    CHECK(r.cmd.count == 2);
    REQUIRE(r.track.has_value());
    CHECK(*r.track == 3);
    CHECK(*request(R"({"schema":1,"id":4,"on":true,"pitches":[60],"track":1})").track == 1);
    CHECK(*request(R"({"schema":1,"id":4,"on":true,"pitches":[60],"track":2147483647})").track == 2147483647);
}

TEST_CASE("parsePreviewRequest: a Start's track must be an integer of 1 or more") {
    CHECK(problemOf(R"({"schema":1,"id":4,"on":true,"pitches":[60],"track":0})") == kTrackProblem);
    CHECK(problemOf(R"({"schema":1,"id":4,"on":true,"pitches":[60],"track":-1})") == kTrackProblem);
    CHECK(problemOf(R"({"schema":1,"id":4,"on":true,"pitches":[60],"track":2147483648})") == kTrackProblem);
    CHECK(problemOf(R"({"schema":1,"id":4,"on":true,"pitches":[60],"track":18446744073709551615})") == kTrackProblem);
    CHECK(problemOf(R"({"schema":1,"id":4,"on":true,"pitches":[60],"track":4294967297})") == kTrackProblem);   // 2^32 + 1
    CHECK(problemOf(R"({"schema":1,"id":4,"on":true,"pitches":[60],"track":2.0})") == kTrackProblem);
    CHECK(problemOf(R"({"schema":1,"id":4,"on":true,"pitches":[60],"track":"2"})") == kTrackProblem);
    CHECK(problemOf(R"({"schema":1,"id":4,"on":true,"pitches":[60],"track":null})") == kTrackProblem);
    CHECK(problemOf(R"({"schema":1,"id":4,"on":true,"pitches":[60],"track":true})") == kTrackProblem);
    CHECK(problemOf(R"({"schema":1,"id":4,"on":true,"pitches":[60],"track":[2]})") == kTrackProblem);
    // Checked after the Start's other rules: a bad pitch or a bad velocity decides first.
    CHECK(problemOf(R"({"schema":1,"id":4,"on":true,"pitches":[200],"track":0})") == kPitchProblem);
    CHECK(problemOf(R"({"schema":1,"id":4,"on":true,"velocity":0,"pitches":[60],"track":0})") == kStartProblem);
}

TEST_CASE("parsePreviewRequest: an Extend or a Stop reads no track") {
    const PreviewRequest stop = request(R"({"schema":1,"id":4,"on":false,"track":3})");
    CHECK(stop.cmd.kind == PreviewCmd::Stop);
    CHECK_FALSE(stop.track.has_value());
    CHECK(request(R"({"schema":1,"id":4,"on":false,"track":"bad"})").cmd.kind == PreviewCmd::Stop);
    const PreviewRequest extend = request(R"({"schema":1,"id":4,"extend":true,"track":3})");
    CHECK(extend.cmd.kind == PreviewCmd::Extend);
    CHECK_FALSE(extend.track.has_value());
    CHECK(request(R"({"schema":1,"id":4,"extend":true,"track":0})").cmd.kind == PreviewCmd::Extend);
}
