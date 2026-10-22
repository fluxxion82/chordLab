#include "doctest.h"

#include "bridge/export_request.h"

#include <string>
#include <string_view>

using chordlab::bridge::parseSectionExportRequest;

TEST_CASE("parseSectionExportRequest: the happy path") {
    const auto r = parseSectionExportRequest(R"({"schema":1,"id":7})");
    REQUIRE(r.has_value());
    CHECK(r->sectionId == 7);
}

TEST_CASE("parseSectionExportRequest: schema must be the integer 1") {
    CHECK_FALSE(parseSectionExportRequest(R"({"schema":0,"id":7})").has_value());
    CHECK_FALSE(parseSectionExportRequest(R"({"schema":2,"id":7})").has_value());
    CHECK_FALSE(parseSectionExportRequest(R"({"schema":1.0,"id":7})").has_value());
    CHECK_FALSE(parseSectionExportRequest(R"({"schema":"1","id":7})").has_value());
    CHECK_FALSE(parseSectionExportRequest(R"({"schema":4294967297,"id":7})").has_value());
    CHECK_FALSE(parseSectionExportRequest(R"({"id":7})").has_value());
}

TEST_CASE("parseSectionExportRequest: id must be in 1..INT_MAX") {
    CHECK_FALSE(parseSectionExportRequest(R"({"schema":1,"id":0})").has_value());
    CHECK_FALSE(parseSectionExportRequest(R"({"schema":1,"id":-3})").has_value());
    CHECK_FALSE(parseSectionExportRequest(R"({"schema":1,"id":-1099511627776})").has_value());
    CHECK_FALSE(parseSectionExportRequest(R"({"schema":1,"id":2147483648})").has_value());
    CHECK_FALSE(parseSectionExportRequest(R"({"schema":1,"id":7.0})").has_value());
    CHECK_FALSE(parseSectionExportRequest(R"({"schema":1,"id":"7"})").has_value());
    CHECK_FALSE(parseSectionExportRequest(R"({"schema":1})").has_value());
    const auto one = parseSectionExportRequest(R"({"schema":1,"id":1})");
    REQUIRE(one.has_value());
    CHECK(one->sectionId == 1);
    const auto top = parseSectionExportRequest(R"({"schema":1,"id":2147483647})");
    REQUIRE(top.has_value());
    CHECK(top->sectionId == 2147483647);
}

TEST_CASE("parseSectionExportRequest: extra members are ignored") {
    const auto r = parseSectionExportRequest(R"({"id":3,"schema":1,"name":"Verse","extra":[1,2,{"x":null}]})");
    REQUIRE(r.has_value());
    CHECK(r->sectionId == 3);
}

TEST_CASE("parseSectionExportRequest: malformed or non-object payloads are nullopt, never a throw") {
    CHECK_FALSE(parseSectionExportRequest(R"({"schema":1,"id":7)").has_value());
    CHECK_FALSE(parseSectionExportRequest(R"({"schema":1,"id":7}trailing)").has_value());
    CHECK_FALSE(parseSectionExportRequest("").has_value());
    CHECK_FALSE(parseSectionExportRequest(std::string_view()).has_value());
    CHECK_FALSE(parseSectionExportRequest(R"([{"schema":1,"id":7}])").has_value());
    CHECK_FALSE(parseSectionExportRequest("7").has_value());
    CHECK_FALSE(parseSectionExportRequest("null").has_value());
}

TEST_CASE("parseSectionExportRequest: a NUL ends the payload") {
    // nlohmann's lexer reads a NUL byte as the end of input: one inside the object leaves it unterminated
    // (refused), one after a complete object is where the parse stops (accepted, the tail unread).
    const std::string inside("{\"schema\":1,\0\"id\":7}", 20);
    REQUIRE(inside.size() == 20);
    CHECK_FALSE(parseSectionExportRequest(inside).has_value());
    const std::string trailing("{\"schema\":1,\"id\":7}\0junk", 24);
    REQUIRE(trailing.size() == 24);
    const auto r = parseSectionExportRequest(trailing);
    REQUIRE(r.has_value());
    CHECK(r->sectionId == 7);
}
