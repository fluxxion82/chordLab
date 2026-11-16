#include "doctest.h"

#include "bridge/command_lineage.h"
#include "chordlab/json_api.h"

#include <string>

using chordlab::bridge::CommandLineage;
using chordlab::bridge::commandLineage;
using chordlab::bridge::kMalformedLineageError;

namespace {
const char* const kToken = "6F1C2A9E-0B7D-4E52-9C1A-3D8E5F7A1B20";
}  // namespace

TEST_CASE("commandLineage: a command without lineage is not checked (tests, tools, older callers)") {
    CHECK(commandLineage(R"({"schema":1,"cmd":"undo"})", kToken) == CommandLineage::Absent);
    CHECK(commandLineage(R"({"schema":1,"cmd":"removeChord","id":3,"Lineage":"other"})", kToken) == CommandLineage::Absent);
    CHECK(commandLineage(R"({"schema":1,"cmd":"x","notes":[{"lineage":"other"}]})", kToken) == CommandLineage::Absent);
}

TEST_CASE("commandLineage: the shell's own token is current") {
    const std::string command = std::string(R"({"schema":1,"cmd":"removeChord","id":3,"lineage":")") + kToken + "\"}";
    CHECK(commandLineage(command, kToken) == CommandLineage::Current);
    CHECK(commandLineage(R"({"lineage":"A"})", "A") == CommandLineage::Current);
}

TEST_CASE("commandLineage: any other token is stale, compared exactly") {
    CHECK(commandLineage(R"({"schema":1,"cmd":"removeChord","id":3,"lineage":"A"})", "B") == CommandLineage::Stale);
    CHECK(commandLineage(R"({"lineage":"a"})", "A") == CommandLineage::Stale);
    CHECK(commandLineage(R"({"lineage":"A "})", "A") == CommandLineage::Stale);
    CHECK(commandLineage(R"({"lineage":"AB"})", "A") == CommandLineage::Stale);
    // An empty lineage is a lineage like any other: never the shell's token, which is a UUID.
    CHECK(commandLineage(R"({"lineage":""})", kToken) == CommandLineage::Stale);
    // An escaped character is compared decoded, as the engine reads every string.
    CHECK(commandLineage(R"({"lineage":"A"})", "A") == CommandLineage::Current);
}

TEST_CASE("commandLineage: a lineage that is not a string is malformed") {
    CHECK(commandLineage(R"({"schema":1,"cmd":"undo","lineage":5})", kToken) == CommandLineage::Malformed);
    CHECK(commandLineage(R"({"lineage":null})", kToken) == CommandLineage::Malformed);
    CHECK(commandLineage(R"({"lineage":true})", kToken) == CommandLineage::Malformed);
    CHECK(commandLineage(R"({"lineage":["A"]})", "A") == CommandLineage::Malformed);
    CHECK(commandLineage(R"({"lineage":{"id":"A"}})", "A") == CommandLineage::Malformed);
    CHECK(std::string(kMalformedLineageError) == "\"lineage\" must be a string");
}

TEST_CASE("commandLineage: a member given twice counts with its last value, as the engine reads members") {
    CHECK(commandLineage(R"({"lineage":"B","lineage":"A"})", "A") == CommandLineage::Current);
    CHECK(commandLineage(R"({"lineage":"A","lineage":"B"})", "A") == CommandLineage::Stale);
    CHECK(commandLineage(R"({"lineage":"A","lineage":1})", "A") == CommandLineage::Malformed);
}

TEST_CASE("commandLineage: text that is not a readable JSON object is left to the engine's own error") {
    // Absent, so the shell hands it to applyCommandJson, which answers with the message it always gave.
    CHECK(commandLineage("", kToken) == CommandLineage::Absent);
    CHECK(commandLineage("not json", kToken) == CommandLineage::Absent);
    CHECK(commandLineage(R"({"lineage":"A")", "A") == CommandLineage::Absent);
    CHECK(commandLineage(R"(["lineage","A"])", "A") == CommandLineage::Absent);
    CHECK(commandLineage(R"("lineage")", "A") == CommandLineage::Absent);
    CHECK(commandLineage("42", "A") == CommandLineage::Absent);
    // A raw NUL: nlohmann would stop at it; the engine refuses the whole text.
    const std::string withNul = std::string(R"({"lineage":"B"})") + '\0' + "junk";
    CHECK(commandLineage(withNul, "A") == CommandLineage::Absent);
    // Longer than the engine reads: not parsed here at all.
    std::string huge = R"({"lineage":"B","pad":")";
    huge.append(chordlab::kMaxJsonBytes, 'x');
    huge += "\"}";
    CHECK(commandLineage(huge, "A") == CommandLineage::Absent);
}

TEST_CASE("commandLineage: nesting deeper than the engine reads is left to the engine") {
    std::string deep = R"({"lineage":"B","x":)";
    for (int i = 0; i < chordlab::kMaxJsonDepth + 1; ++i)
        deep += '[';
    for (int i = 0; i < chordlab::kMaxJsonDepth + 1; ++i)
        deep += ']';
    deep += '}';
    CHECK(commandLineage(deep, "A") == CommandLineage::Absent);
    // At the limit the text is read: a stale lineage is stale.
    std::string atLimit = R"({"lineage":"B","x":)";
    for (int i = 0; i < chordlab::kMaxJsonDepth - 1; ++i)
        atLimit += '[';
    for (int i = 0; i < chordlab::kMaxJsonDepth - 1; ++i)
        atLimit += ']';
    atLimit += '}';
    CHECK(commandLineage(atLimit, "A") == CommandLineage::Stale);
}
