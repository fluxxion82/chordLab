#include "doctest.h"

#include "bridge/message_fields.h"

#include <climits>
#include <nlohmann/json.hpp>

using chordlab::bridge::boundedInt;
using chordlab::bridge::boundedIntField;
using nlohmann::json;

namespace {
json parsed(const char* text) { return json::parse(text); }
}  // namespace

TEST_CASE("boundedIntField: an integer in range is returned, both bounds inclusive") {
    const json m = parsed(R"({"lo":1,"mid":5,"hi":10})");
    CHECK(boundedIntField(m, "mid", 1, 10) == 5);
    CHECK(boundedIntField(m, "lo", 1, 10) == 1);
    CHECK(boundedIntField(m, "hi", 1, 10) == 10);
    CHECK_FALSE(boundedIntField(m, "lo", 2, 10).has_value());
    CHECK_FALSE(boundedIntField(m, "hi", 1, 9).has_value());
}

TEST_CASE("boundedIntField: INT_MAX is accepted at the top of 0..INT_MAX") {
    const json m = parsed(R"({"v":2147483647})");
    CHECK(boundedIntField(m, "v", 0, INT_MAX) == INT_MAX);
}

TEST_CASE("boundedIntField: out-of-range values are refused before narrowing") {
    // 4294967297 = 2^32 + 1 narrows to 1 as an int: it must not pass a 1..1 check.
    CHECK_FALSE(boundedIntField(parsed(R"({"v":4294967297})"), "v", 1, 1).has_value());
    CHECK_FALSE(boundedIntField(parsed(R"({"v":4294967297})"), "v", 0, INT_MAX).has_value());
    CHECK_FALSE(boundedIntField(parsed(R"({"v":-1})"), "v", 0, INT_MAX).has_value());
    CHECK_FALSE(boundedIntField(parsed(R"({"v":2147483648})"), "v", 0, INT_MAX).has_value());
    CHECK_FALSE(boundedIntField(parsed(R"({"v":-1099511627776})"), "v", 0, INT_MAX).has_value());
    // Stored as uint64 by nlohmann; get<int64_t> wraps it to -1, still out of a non-negative range.
    CHECK_FALSE(boundedIntField(parsed(R"({"v":18446744073709551615})"), "v", 0, INT_MAX).has_value());
    CHECK_FALSE(boundedIntField(parsed(R"({"v":9223372036854775808})"), "v", 0, INT_MAX).has_value());
}

TEST_CASE("boundedIntField: only JSON integers count") {
    CHECK_FALSE(boundedIntField(parsed(R"({"v":1.0})"), "v", 0, 10).has_value());
    CHECK_FALSE(boundedIntField(parsed(R"({"v":1.5})"), "v", 0, 10).has_value());
    CHECK_FALSE(boundedIntField(parsed(R"({"v":"1"})"), "v", 0, 10).has_value());
    CHECK_FALSE(boundedIntField(parsed(R"({"v":true})"), "v", 0, 10).has_value());
    CHECK_FALSE(boundedIntField(parsed(R"({"v":false})"), "v", 0, 10).has_value());
    CHECK_FALSE(boundedIntField(parsed(R"({"v":null})"), "v", 0, 10).has_value());
    CHECK_FALSE(boundedIntField(parsed(R"({"v":[1]})"), "v", 0, 10).has_value());
    CHECK_FALSE(boundedIntField(parsed(R"({"v":{"x":1}})"), "v", 0, 10).has_value());
}

TEST_CASE("boundedInt: a bare value, with the same rules") {
    CHECK(boundedInt(json(0), 0, 127) == 0);
    CHECK(boundedInt(json(127), 0, 127) == 127);
    CHECK_FALSE(boundedInt(json(128), 0, 127).has_value());
    CHECK_FALSE(boundedInt(json(-1), 0, 127).has_value());
    CHECK_FALSE(boundedInt(json(1.0), 0, 127).has_value());
    CHECK_FALSE(boundedInt(json(true), 0, 127).has_value());
    CHECK_FALSE(boundedInt(json("1"), 0, 127).has_value());
    CHECK_FALSE(boundedInt(json(nullptr), 0, 127).has_value());
    CHECK_FALSE(boundedInt(parsed("18446744073709551615"), 0, 127).has_value());
    // With a negative lower bound the uint64 guard is what refuses it (a wrap would read -1).
    CHECK_FALSE(boundedInt(parsed("18446744073709551615"), -5, 5).has_value());
    CHECK_FALSE(boundedIntField(parsed(R"({"v":18446744073709551615})"), "v", -5, 5).has_value());
    CHECK(boundedInt(json(-1), -5, 5) == -1);
}

TEST_CASE("boundedIntField: a missing member is nullopt") {
    CHECK_FALSE(boundedIntField(parsed(R"({"other":1})"), "v", 0, 10).has_value());
    CHECK_FALSE(boundedIntField(parsed(R"({})"), "v", 0, 10).has_value());
}

TEST_CASE("boundedIntField: a non-object is nullopt") {
    CHECK_FALSE(boundedIntField(parsed(R"([1,2])"), "v", 0, 10).has_value());
    CHECK_FALSE(boundedIntField(parsed(R"(1)"), "v", 0, 10).has_value());
    CHECK_FALSE(boundedIntField(parsed(R"("v")"), "v", 0, 10).has_value());
    CHECK_FALSE(boundedIntField(parsed(R"(null)"), "v", 0, 10).has_value());
    const json discarded = json::parse("{", nullptr, /*allow_exceptions=*/false);
    REQUIRE(discarded.is_discarded());
    CHECK_FALSE(boundedIntField(discarded, "v", 0, 10).has_value());
}
