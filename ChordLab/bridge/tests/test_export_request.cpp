#include "doctest.h"

#include "bridge/command_lineage.h"
#include "bridge/export_request.h"

#include <string>
#include <string_view>
#include <vector>

using chordlab::bridge::parseClipExportRequest;
using chordlab::bridge::parseSectionExportRequest;
using chordlab::bridge::resolveClipExport;

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

TEST_CASE("parseSectionExportRequest: a raw NUL byte anywhere is refused") {
    // As the engine's prescan. Without it nlohmann would stop at the NUL and accept the trailing case.
    CHECK_FALSE(parseSectionExportRequest(std::string("{\"schema\":1,\0\"id\":7}", 20)).has_value());
    CHECK_FALSE(parseSectionExportRequest(std::string("{\"schema\":1,\"id\":7}\0junk", 24)).has_value());
    CHECK_FALSE(parseSectionExportRequest(std::string("{\"schema\":1,\"id\":7}\0", 20)).has_value());
    CHECK_FALSE(parseSectionExportRequest(std::string("\0", 1)).has_value());
}

TEST_CASE("parseSectionExportRequest: an escaped \\u0000 is an ordinary string character") {
    const auto r = parseSectionExportRequest(R"({"schema":1,"id":7,"name":"a\u0000b"})");
    REQUIRE(r.has_value());
    CHECK(r->sectionId == 7);
}

// ---- parseClipExportRequest (kMsgTagExportClip, tag 17) ----

namespace {
// The request with `lanes` and `sectionId` spliced in as raw JSON text.
std::string clipRequest(const std::string& lanes, const std::string& section = "null") {
    return R"({"schema":1,"lanes":)" + lanes + R"(,"sectionId":)" + section + "}";
}
}  // namespace

TEST_CASE("parseClipExportRequest: \"all\" with a null section") {
    const auto r = parseClipExportRequest(R"({"schema":1,"lanes":"all","sectionId":null})");
    REQUIRE(r.has_value());
    CHECK(r->allLanes);
    CHECK(r->lanes.empty());
    CHECK_FALSE(r->sectionId.has_value());
}

TEST_CASE("parseClipExportRequest: one lane, the chords or a track") {
    const auto chords = parseClipExportRequest(clipRequest(R"(["chords"])"));
    REQUIRE(chords.has_value());
    CHECK_FALSE(chords->allLanes);
    REQUIRE(chords->lanes.size() == 1);
    CHECK(chords->lanes[0].chords);

    const auto track = parseClipExportRequest(clipRequest("[2]"));
    REQUIRE(track.has_value());
    CHECK_FALSE(track->allLanes);
    REQUIRE(track->lanes.size() == 1);
    CHECK_FALSE(track->lanes[0].chords);
    CHECK(track->lanes[0].trackId == 2);
}

TEST_CASE("parseClipExportRequest: several lanes keep the request order") {
    const auto r = parseClipExportRequest(clipRequest(R"(["chords",1,3])"));
    REQUIRE(r.has_value());
    REQUIRE(r->lanes.size() == 3);
    CHECK(r->lanes[0].chords);
    CHECK_FALSE(r->lanes[1].chords);
    CHECK(r->lanes[1].trackId == 1);
    CHECK(r->lanes[2].trackId == 3);

    const auto reversed = parseClipExportRequest(clipRequest(R"([3,"chords",1])"));
    REQUIRE(reversed.has_value());
    REQUIRE(reversed->lanes.size() == 3);
    CHECK(reversed->lanes[0].trackId == 3);
    CHECK(reversed->lanes[1].chords);
    CHECK(reversed->lanes[2].trackId == 1);
}

TEST_CASE("parseClipExportRequest: the section id is present, null or absent") {
    const auto present = parseClipExportRequest(clipRequest(R"(["chords"])", "7"));
    REQUIRE(present.has_value());
    REQUIRE(present->sectionId.has_value());
    CHECK(*present->sectionId == 7);

    const auto null = parseClipExportRequest(clipRequest(R"(["chords"])", "null"));
    REQUIRE(null.has_value());
    CHECK_FALSE(null->sectionId.has_value());

    const auto absent = parseClipExportRequest(R"({"schema":1,"lanes":"all"})");
    REQUIRE(absent.has_value());
    CHECK(absent->allLanes);
    CHECK_FALSE(absent->sectionId.has_value());

    const auto one = parseClipExportRequest(clipRequest("\"all\"", "1"));
    REQUIRE(one.has_value());
    CHECK(*one->sectionId == 1);
    const auto top = parseClipExportRequest(clipRequest("\"all\"", "2147483647"));
    REQUIRE(top.has_value());
    CHECK(*top->sectionId == 2147483647);
}

TEST_CASE("parseClipExportRequest: the section id is a bounded integer or refused") {
    for (const char* bad : {"0", "-1", "-1099511627776", "2147483648", "4294967297", "1.0", "1.0e0", "7.5", "\"1\"",
                            "true", "false", "{}", "[]", "[1]"})
        CHECK_FALSE(parseClipExportRequest(clipRequest("\"all\"", bad)).has_value());
}

TEST_CASE("parseClipExportRequest: lanes is \"all\" exactly or a non-empty array") {
    for (const char* bad : {"\"ALL\"", "\"All\"", "\"chord\"", "\"chords\"", "\"\"", "\"all \"", "3", "1.5", "true", "null",
                            "{}", R"({"chords":true})", "[]"})
        CHECK_FALSE(parseClipExportRequest(clipRequest(bad)).has_value());
    CHECK_FALSE(parseClipExportRequest(R"({"schema":1,"sectionId":null})").has_value());   // no lanes at all
}

TEST_CASE("parseClipExportRequest: an array entry is \"chords\" exactly or a track id in 1..INT_MAX") {
    for (const char* bad : {"1.5", "1.0", "true", "false", "null", "-1", "0", "2147483648", "4294967297",
                            "-1099511627776", "18446744073709551615", "{}", "[]", "[1]", "\"all\"", "\"Chords\"",
                            "\"chord\"", "\"1\"", "\"\""})
        CHECK_FALSE(parseClipExportRequest(clipRequest(std::string("[") + bad + "]")).has_value());
    // One bad entry refuses the whole request, wherever it is.
    CHECK_FALSE(parseClipExportRequest(clipRequest(R"(["chords",1,0])")).has_value());
    CHECK_FALSE(parseClipExportRequest(clipRequest(R"(["chords",null,3])")).has_value());
    CHECK_FALSE(parseClipExportRequest(clipRequest(R"([2,1.5])")).has_value());
    const auto one = parseClipExportRequest(clipRequest("[1]"));
    REQUIRE(one.has_value());
    CHECK(one->lanes[0].trackId == 1);
    const auto top = parseClipExportRequest(clipRequest("[2147483647]"));
    REQUIRE(top.has_value());
    CHECK(top->lanes[0].trackId == 2147483647);
}

TEST_CASE("parseClipExportRequest: duplicate lanes are refused") {
    CHECK_FALSE(parseClipExportRequest(clipRequest(R"(["chords","chords"])")).has_value());
    CHECK_FALSE(parseClipExportRequest(clipRequest("[1,1]")).has_value());
    CHECK_FALSE(parseClipExportRequest(clipRequest(R"(["chords",2,3,"chords"])")).has_value());
    CHECK_FALSE(parseClipExportRequest(clipRequest(R"([4,"chords",1,4])")).has_value());
    CHECK(parseClipExportRequest(clipRequest(R"(["chords",1,2])")).has_value());   // different lanes are fine
}

TEST_CASE("parseClipExportRequest: at most 1 + kMaxTracks lanes") {
    static_assert(chordlab::kMaxTracks == 4, "the five-lane cases below assume four tracks");
    const auto five = parseClipExportRequest(clipRequest(R"(["chords",1,2,3,4])"));
    REQUIRE(five.has_value());
    CHECK(five->lanes.size() == 5);
    // Six are refused even when they are all distinct.
    CHECK_FALSE(parseClipExportRequest(clipRequest(R"(["chords",1,2,3,4,5])")).has_value());
    CHECK_FALSE(parseClipExportRequest(clipRequest("[1,2,3,4,5,6]")).has_value());
    // The bound is on the count only: five distinct track ids pass the parser (the document says which exist).
    CHECK(parseClipExportRequest(clipRequest("[1,2,3,4,5]")).has_value());
}

TEST_CASE("parseClipExportRequest: schema must be the integer 1") {
    CHECK_FALSE(parseClipExportRequest(R"({"lanes":"all","sectionId":null})").has_value());
    CHECK_FALSE(parseClipExportRequest(R"({"schema":0,"lanes":"all"})").has_value());
    CHECK_FALSE(parseClipExportRequest(R"({"schema":2,"lanes":"all"})").has_value());
    CHECK_FALSE(parseClipExportRequest(R"({"schema":1.0,"lanes":"all"})").has_value());
    CHECK_FALSE(parseClipExportRequest(R"({"schema":"1","lanes":"all"})").has_value());
    CHECK_FALSE(parseClipExportRequest(R"({"schema":4294967297,"lanes":"all"})").has_value());
    CHECK_FALSE(parseClipExportRequest(R"({"schema":true,"lanes":"all"})").has_value());
}

TEST_CASE("parseClipExportRequest: extra members are ignored") {
    const auto r = parseClipExportRequest(
        R"({"lanes":["chords",2],"extra":[1,2,{"x":null}],"schema":1,"sectionId":4,"name":"Verse"})");
    REQUIRE(r.has_value());
    REQUIRE(r->lanes.size() == 2);
    CHECK(r->lanes[1].trackId == 2);
    CHECK(*r->sectionId == 4);
}

TEST_CASE("parseClipExportRequest: the editor's lineage member is ignored here, whatever its type (the shell checks it)") {
    for (const char* text : {R"({"schema":1,"lanes":"all","sectionId":null,"lineage":"6F1C2A9E"})",
                             R"({"schema":1,"lanes":"all","sectionId":null,"lineage":5})",
                             R"({"schema":1,"lanes":"all","sectionId":null,"lineage":null})"}) {
        const auto r = parseClipExportRequest(text);
        REQUIRE(r.has_value());
        CHECK(r->allLanes);
        CHECK_FALSE(r->sectionId.has_value());
    }
    const auto lanes = parseClipExportRequest(R"({"lineage":"A","schema":1,"lanes":["chords",1],"sectionId":2})");
    REQUIRE(lanes.has_value());
    CHECK(lanes->lanes.size() == 2);
    CHECK(*lanes->sectionId == 2);
}

TEST_CASE("parseClipExportRequest: malformed or non-object payloads are nullopt, never a throw") {
    CHECK_FALSE(parseClipExportRequest(R"({"schema":1,"lanes":"all")").has_value());
    CHECK_FALSE(parseClipExportRequest(R"({"schema":1,"lanes":"all"}trailing)").has_value());
    CHECK_FALSE(parseClipExportRequest("").has_value());
    CHECK_FALSE(parseClipExportRequest(std::string_view()).has_value());
    CHECK_FALSE(parseClipExportRequest(R"([{"schema":1,"lanes":"all"}])").has_value());
    CHECK_FALSE(parseClipExportRequest("\"all\"").has_value());
    CHECK_FALSE(parseClipExportRequest("null").has_value());
    CHECK_FALSE(parseClipExportRequest(R"({"schema":1,"lanes":["chords",]})").has_value());
}

TEST_CASE("parseClipExportRequest: a raw NUL byte anywhere is refused") {
    const std::string ok = R"({"schema":1,"lanes":"all","sectionId":null})";
    for (const size_t at : {size_t{0}, size_t{5}, ok.size() - 1, ok.size()}) {
        std::string withNul = ok;
        withNul.insert(at, 1, '\0');
        CHECK_FALSE(parseClipExportRequest(withNul).has_value());
    }
    CHECK_FALSE(parseClipExportRequest(ok + std::string("\0junk", 5)).has_value());
    CHECK_FALSE(parseClipExportRequest(std::string("\0", 1)).has_value());
}

TEST_CASE("parseClipExportRequest: an escaped \\u0000 is an ordinary string character") {
    const auto r = parseClipExportRequest(R"({"schema":1,"lanes":["chords"],"name":"a\u0000b"})");
    REQUIRE(r.has_value());
    CHECK(r->lanes.size() == 1);
    // ...but inside a lane entry it is just a string that is not "chords".
    CHECK_FALSE(parseClipExportRequest(R"({"schema":1,"lanes":["chords\u0000"]})").has_value());
}

// ---- resolveClipExport (what HandleExportClip does with a parsed request) ----

namespace {
// A document with sections 11 "Verse" and 12 "" (unnamed) and tracks 5 "Bass", 8 "Lead".
chordlab::Document resolveDoc() {
    chordlab::Document doc;
    doc.sections.push_back(chordlab::Section{11, "Verse", 0, 0});
    doc.sections.push_back(chordlab::Section{12, "", 1, 2});
    doc.tracks.push_back(chordlab::Track{5, "Bass", chordlab::TrackRole::Bass, false, false, {}});
    doc.tracks.push_back(chordlab::Track{8, "Lead", chordlab::TrackRole::Lead, false, false, {}});
    return doc;
}

chordlab::bridge::ClipExportRequest requestOf(const std::string& lanes, const std::string& section = "null") {
    const auto r = parseClipExportRequest(clipRequest(lanes, section));
    REQUIRE(r.has_value());
    return *r;
}
}  // namespace

TEST_CASE("resolveClipExport: the whole progression has no section and spec.sectionIndex -1") {
    const auto r = resolveClipExport(resolveDoc(), requestOf(R"(["chords"])"));
    CHECK(r.problem == nullptr);
    CHECK(r.spec.sectionIndex == -1);
    CHECK_FALSE(r.sectionName.has_value());
    CHECK_FALSE(r.spec.allLanes);
    REQUIRE(r.spec.lanes.size() == 1);
    CHECK(r.spec.lanes[0].chords);
    CHECK(r.laneNames == std::vector<std::string>{"Chords"});
}

TEST_CASE("parseClipExportRequest: a text beyond the lineage reader's limits is no request") {
    // readLineage reads nothing from a text longer than kMaxJsonBytes or nested deeper than kMaxJsonDepth (it
    // answers Absent), so such a text must not be an export request either: a stale `lineage` hidden behind an
    // over-limit ignored member would otherwise be exported unchecked.
    const std::string deep = std::string(R"({"schema":1,"lanes":"all","sectionId":null,"lineage":"A","x":)") +
                             std::string(64, '[') + std::string(64, ']') + "}";
    CHECK(chordlab::bridge::readLineage(deep).kind == chordlab::bridge::LineageField::Kind::Absent);
    CHECK_FALSE(parseClipExportRequest(deep).has_value());
    const std::string shallow = std::string(R"({"schema":1,"lanes":"all","sectionId":null,"lineage":"A","x":)") +
                                std::string(63, '[') + std::string(63, ']') + "}";
    CHECK(chordlab::bridge::readLineage(shallow).kind == chordlab::bridge::LineageField::Kind::String);
    CHECK(parseClipExportRequest(shallow).has_value());

    const std::string head = R"({"schema":1,"lanes":"all","sectionId":null,"lineage":"A","x":")";
    const std::string atLimit = head + std::string(chordlab::kMaxJsonBytes - head.size() - 2, 'a') + "\"}";
    REQUIRE(atLimit.size() == chordlab::kMaxJsonBytes);
    CHECK(chordlab::bridge::readLineage(atLimit).kind == chordlab::bridge::LineageField::Kind::String);
    CHECK(parseClipExportRequest(atLimit).has_value());
    const std::string tooLong = head + std::string(chordlab::kMaxJsonBytes - head.size() - 1, 'a') + "\"}";
    REQUIRE(tooLong.size() == chordlab::kMaxJsonBytes + 1);
    CHECK(chordlab::bridge::readLineage(tooLong).kind == chordlab::bridge::LineageField::Kind::Absent);
    CHECK_FALSE(parseClipExportRequest(tooLong).has_value());
}

TEST_CASE("resolveClipExport: a section is its position in doc.sections and its name") {
    const auto verse = resolveClipExport(resolveDoc(), requestOf("[5]", "11"));
    CHECK(verse.problem == nullptr);
    CHECK(verse.spec.sectionIndex == 0);
    REQUIRE(verse.sectionName.has_value());
    CHECK(*verse.sectionName == "Verse");

    // An unnamed section is present with an empty name (not nullopt: the label says "Section").
    const auto unnamed = resolveClipExport(resolveDoc(), requestOf("[5]", "12"));
    CHECK(unnamed.problem == nullptr);
    CHECK(unnamed.spec.sectionIndex == 1);
    REQUIRE(unnamed.sectionName.has_value());
    CHECK(unnamed.sectionName->empty());
}

TEST_CASE("resolveClipExport: an unknown section is \"no such section\"") {
    const auto r = resolveClipExport(resolveDoc(), requestOf(R"(["chords"])", "99"));
    REQUIRE(r.problem != nullptr);
    CHECK(std::string(r.problem) == "no such section");
    // A document without sections has none to name, even though exportClip reads index 0 as the whole.
    const auto none = resolveClipExport(chordlab::Document{}, requestOf(R"(["chords"])", "1"));
    REQUIRE(none.problem != nullptr);
    CHECK(std::string(none.problem) == "no such section");
}

TEST_CASE("resolveClipExport: an unknown track is \"no such track\", the first one found") {
    const auto r = resolveClipExport(resolveDoc(), requestOf(R"(["chords",5,77,88])"));
    REQUIRE(r.problem != nullptr);
    CHECK(std::string(r.problem) == "no such track");
    const auto only = resolveClipExport(resolveDoc(), requestOf("[3]"));
    REQUIRE(only.problem != nullptr);
    CHECK(std::string(only.problem) == "no such track");
}

TEST_CASE("resolveClipExport: the section is checked before the tracks") {
    const auto r = resolveClipExport(resolveDoc(), requestOf("[77]", "99"));
    REQUIRE(r.problem != nullptr);
    CHECK(std::string(r.problem) == "no such section");
    // With a good section the track is the problem.
    const auto track = resolveClipExport(resolveDoc(), requestOf("[77]", "11"));
    REQUIRE(track.problem != nullptr);
    CHECK(std::string(track.problem) == "no such track");
}

TEST_CASE("resolveClipExport: all lanes need no lane check and carry no lane names") {
    const auto r = resolveClipExport(resolveDoc(), requestOf("\"all\""));
    CHECK(r.problem == nullptr);
    CHECK(r.spec.allLanes);
    CHECK(r.spec.lanes.empty());
    CHECK(r.laneNames.empty());
    CHECK(r.spec.sectionIndex == -1);
    const auto inSection = resolveClipExport(resolveDoc(), requestOf("\"all\"", "12"));
    CHECK(inSection.problem == nullptr);
    CHECK(inSection.spec.allLanes);
    CHECK(inSection.spec.sectionIndex == 1);
    const auto badSection = resolveClipExport(resolveDoc(), requestOf("\"all\"", "3"));
    REQUIRE(badSection.problem != nullptr);
    CHECK(std::string(badSection.problem) == "no such section");
}

TEST_CASE("resolveClipExport: lane names and spec lanes follow the request order") {
    const auto r = resolveClipExport(resolveDoc(), requestOf(R"([8,"chords",5])"));
    CHECK(r.problem == nullptr);
    CHECK(r.laneNames == std::vector<std::string>{"Lead", "Chords", "Bass"});
    REQUIRE(r.spec.lanes.size() == 3);
    CHECK(r.spec.lanes[0].trackId == 8);
    CHECK_FALSE(r.spec.lanes[0].chords);
    CHECK(r.spec.lanes[1].chords);
    CHECK(r.spec.lanes[2].trackId == 5);
}
