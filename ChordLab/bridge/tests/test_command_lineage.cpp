#include "doctest.h"

#include "bridge/command_lineage.h"
#include "chordlab/json_api.h"

#include <string>

using chordlab::bridge::CommandLineage;
using chordlab::bridge::commandLineage;
using chordlab::bridge::compareLineage;
using chordlab::bridge::kLineageFailedError;
using chordlab::bridge::kMalformedLineageError;
using chordlab::bridge::kStaleExportError;
using chordlab::bridge::LineageField;
using chordlab::bridge::lineageApplies;
using chordlab::bridge::readLineage;

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
}

TEST_CASE("commandLineage: escapes are read decoded, in the value and in the key, as the engine reads every string") {
    // Plain literals, so the JSON text really holds the backslash escapes (checked first).
    CHECK(std::string("{\"lineage\":\"\\u0041\"}").find('\\') != std::string::npos);
    CHECK(std::string("{\"\\u006cineage\":\"A\"}").find('\\') != std::string::npos);
    CHECK(commandLineage("{\"lineage\":\"\\u0041\"}", "A") == CommandLineage::Current);
    CHECK(commandLineage("{\"lineage\":\"\\u0041\"}", "B") == CommandLineage::Stale);
    CHECK(commandLineage("{\"\\u006cineage\":\"A\"}", "A") == CommandLineage::Current);
    CHECK(commandLineage("{\"\\u006cineage\":\"A\"}", "B") == CommandLineage::Stale);
    CHECK(commandLineage("{\"\\u006cineage\":5}", "A") == CommandLineage::Malformed);
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

// ---- Read outside the model lock, compared under it (review fix 4), and never a throw into the shell (fix 3) ----

TEST_CASE("readLineage: what the member holds, read without the shell's token") {
    CHECK(readLineage(R"({"schema":1,"cmd":"undo"})").kind == LineageField::Kind::Absent);
    CHECK(readLineage("not json").kind == LineageField::Kind::Absent);
    CHECK(readLineage(R"({"lineage":5})").kind == LineageField::Kind::Malformed);
    const LineageField field = readLineage(R"({"schema":1,"cmd":"undo","lineage":"6F1C"})");
    CHECK(field.kind == LineageField::Kind::String);
    CHECK(field.value == "6F1C");
    CHECK(readLineage(R"({"lineage":""})").kind == LineageField::Kind::String);
}

TEST_CASE("compareLineage: the mapping under the lock") {
    LineageField field;
    CHECK(compareLineage(field, kToken) == CommandLineage::Absent);
    field.kind = LineageField::Kind::String;
    field.value = kToken;
    CHECK(compareLineage(field, kToken) == CommandLineage::Current);
    CHECK(compareLineage(field, "other") == CommandLineage::Stale);
    field.value.clear();
    CHECK(compareLineage(field, kToken) == CommandLineage::Stale);
    field.kind = LineageField::Kind::Malformed;
    CHECK(compareLineage(field, kToken) == CommandLineage::Malformed);
    // A read that failed (an allocation failure in the parse: readLineage catches everything) is never "apply": the
    // engine's own parse might succeed afterwards and apply a stale command. The failure itself cannot be injected
    // into nlohmann's parse here, so only the mapping is pinned.
    field.kind = LineageField::Kind::Failed;
    CHECK(compareLineage(field, kToken) == CommandLineage::Failed);
    CHECK(compareLineage(field, "") == CommandLineage::Failed);
}

TEST_CASE("lineageApplies: only an absent or current lineage lets the command or export through") {
    CHECK(lineageApplies(CommandLineage::Absent));
    CHECK(lineageApplies(CommandLineage::Current));
    CHECK_FALSE(lineageApplies(CommandLineage::Stale));
    CHECK_FALSE(lineageApplies(CommandLineage::Malformed));
    CHECK_FALSE(lineageApplies(CommandLineage::Failed));
}

TEST_CASE("lineage errors: a failed read uses the engine's internal-error wording; a stale export says what to do") {
    CHECK(std::string(kLineageFailedError) == "internal error");
    CHECK(std::string(kStaleExportError) == "The project was reloaded; drag again");
}

TEST_CASE("readLineage, compareLineage and commandLineage never throw") {
    CHECK(noexcept(readLineage(std::string_view())));
    CHECK(noexcept(compareLineage(LineageField{}, std::string_view())));
    CHECK(noexcept(commandLineage(std::string_view(), std::string_view())));
}
