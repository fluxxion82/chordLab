#pragma once

// Plain C++17, header-only: no iPlug2, no Objective-C, no Foundation, so the plug-in shell's message
// handling can be unit-tested outside Xcode (ChordLab/tests).

#include "message_fields.h"
#include "chordlab/json_api.h"

#include <nlohmann/json.hpp>

#include <climits>
#include <optional>
#include <string_view>

namespace chordlab::bridge {

/// A kMsgTagExportSection payload: the section to export, by its stable id.
struct SectionExportRequest {
    int sectionId;
};

/// Parses a kMsgTagExportSection payload, `{"schema":1,"id":n}`. Accepted when the text parses as a JSON
/// object whose `schema` is the integer kJsonSchema and whose `id` is an integer in 1..INT_MAX (both via
/// boundedIntField, so an oversized or float value is refused, never narrowed); other members are
/// ignored. Anything else (malformed JSON, an empty payload, a non-object top level) is nullopt; the
/// caller answers it with kMsgTagError "section export message needs schema 1 and a section id".
/// The parse never throws on malformed input (allow_exceptions=false); whether the id names an existing
/// section is the caller's question.
/// Raw NUL bytes: nlohmann's lexer reads a raw NUL as the end of input, so a complete message followed by
/// `\0junk` is accepted (the tail is never read), while a NUL inside the object leaves it unterminated and
/// is refused. An escaped `\u0000` inside a JSON string is a different thing: an ordinary character of
/// that string.
inline std::optional<SectionExportRequest> parseSectionExportRequest(std::string_view json)
{
    const nlohmann::json message = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (!boundedIntField(message, "schema", chordlab::kJsonSchema, chordlab::kJsonSchema))
        return std::nullopt;
    const std::optional<int> id = boundedIntField(message, "id", 1, INT_MAX);
    if (!id)
        return std::nullopt;
    return SectionExportRequest{*id};
}

}  // namespace chordlab::bridge
