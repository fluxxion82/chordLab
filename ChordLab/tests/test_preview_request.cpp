#include "doctest.h"

#include "bridge/preview_request.h"

#include <cstdint>
#include <optional>
#include <string>

using chordlab::PreviewCmd;
using chordlab::bridge::parsePreviewRequest;

namespace {

const std::string kHeaderProblem = "preview message needs schema 1, an id and on:true|false";
const std::string kStartProblem = "preview start needs 1..8 pitches and a velocity in 1..127";
const std::string kPitchProblem = "preview pitches must be integers in 0..127";

// The problem text for a payload parsePreviewRequest refuses; fails the test if it was accepted.
std::string problemOf(const std::string& json) {
    std::string problem;
    const auto cmd = parsePreviewRequest(json, &problem);
    CHECK_FALSE(cmd.has_value());
    CHECK_FALSE(problem.empty());
    return problem;
}

// The command for a payload parsePreviewRequest accepts, leaving `problem` untouched.
PreviewCmd accepted(const std::string& json) {
    std::string problem = "untouched";
    const auto cmd = parsePreviewRequest(json, &problem);
    REQUIRE(cmd.has_value());
    CHECK(problem == "untouched");
    return *cmd;
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

TEST_CASE("parsePreviewRequest: id must be in 0..INT_MAX") {
    CHECK(accepted(R"({"schema":1,"id":0,"on":false})").id == 0);
    CHECK(accepted(R"({"schema":1,"id":2147483647,"on":false})").id == 2147483647);
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
    CHECK(problemOf(R"({"schema":1,"id":1,"on":true,"pitches":[18446744073709551615]})") == kPitchProblem);
}

TEST_CASE("parsePreviewRequest: the first failing rule decides the problem") {
    // A bad velocity and a bad pitch: the Start rule (velocity, array shape) is checked before pitches.
    CHECK(problemOf(R"({"schema":1,"id":1,"on":true,"velocity":0,"pitches":[200]})") == kStartProblem);
    // A bad id with a bad Start body: the header rule wins.
    CHECK(problemOf(R"({"schema":1,"id":-1,"on":true,"pitches":[]})") == kHeaderProblem);
}

TEST_CASE("parsePreviewRequest: extra members are ignored") {
    const PreviewCmd c = accepted(R"({"schema":1,"id":2,"on":true,"pitches":[60],"label":"I","extra":{"a":[1]}})");
    CHECK(c.kind == PreviewCmd::Start);
    CHECK(c.count == 1);
}
