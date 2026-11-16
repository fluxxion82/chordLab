#pragma once

// Plain C++17, header-only: no iPlug2, no Objective-C, no Foundation, so the plug-in shell's message
// handling can be unit-tested outside Xcode (ChordLab/bridge/tests).

#include "chordlab/json_api.h"

#include <nlohmann/json.hpp>

#include <string_view>

namespace chordlab::bridge {

/// What a kMsgTagCommand's `lineage` member says about the document the command was computed for (tracks plan
/// Task 24a, design D21). The editor stamps every command with its clipboard token (`CommandLineage.stamp`,
/// ChordLab/UI/Core/CommandLineage.swift); the shell compares it with `ChordLab::mClipboardToken` under mModelMutex,
/// right before applyCommandJson, and acts on the answer:
///   Absent    no `lineage` member, or text that is not a JSON object this reads (see commandLineage): not checked;
///             applyCommandJson decides and reports as it always has (tests, tools and older callers keep working).
///   Current   the member is the shell's token exactly: applied as usual.
///   Stale     the member is a string, any other string: the command was computed for a document a state load has
///             since replaced. Nothing is applied (no apply, palette rebuild, publish or host dirty); the shell pushes
///             the current snapshot, which hands the editor the current token first. Not an error: a click that
///             raced a host's state load is not the user's mistake.
///   Malformed the member is present but not a string (null included): nothing is applied, and the shell answers
///             with kMsgTagError kMalformedLineageError, as the engine answers any mistyped field. The editor never
///             sends one.
enum class CommandLineage { Absent, Current, Stale, Malformed };

/// The error a Malformed lineage is answered with (the engine's wording for a mistyped field).
inline constexpr const char* kMalformedLineageError = "\"lineage\" must be a string";

/// Classifies `command`, a kMsgTagCommand payload, against `token`, the shell's current document lineage.
/// Only text the engine itself would read is read here: longer than kMaxJsonBytes, containing a raw NUL byte,
/// nested deeper than kMaxJsonDepth (counted as the engine's prescan counts: `{` and `[` outside string literals),
/// malformed JSON or a top level that is not an object is Absent, so applyCommandJson refuses it with exactly the
/// message it always gave. In an object, only the top-level member named exactly `lineage` counts (a nested one or
/// another spelling is just a field the engine ignores); a member given twice counts with its last value, as
/// nlohmann (and so the engine) reads it. Strings are compared after JSON decoding, byte for byte. Never throws on
/// malformed input (allow_exceptions=false); an allocation failure propagates as std::bad_alloc.
inline CommandLineage commandLineage(std::string_view command, std::string_view token)
{
    if (command.size() > chordlab::kMaxJsonBytes || command.find('\0') != std::string_view::npos)
        return CommandLineage::Absent;
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (const char c : command)
    {
        if (inString)
        {
            if (escaped)
                escaped = false;
            else if (c == '\\')
                escaped = true;
            else if (c == '"')
                inString = false;
        }
        else if (c == '"')
            inString = true;
        else if (c == '{' || c == '[')
        {
            if (++depth > chordlab::kMaxJsonDepth)
                return CommandLineage::Absent;
        }
        else if ((c == '}' || c == ']') && depth > 0)
            --depth;
    }
    const nlohmann::json message = nlohmann::json::parse(command, nullptr, /*allow_exceptions=*/false);
    if (!message.is_object())                                    // a discarded parse is not an object either
        return CommandLineage::Absent;
    const auto it = message.find("lineage");
    if (it == message.end())
        return CommandLineage::Absent;
    if (!it->is_string())
        return CommandLineage::Malformed;
    return it->get_ref<const std::string&>() == token ? CommandLineage::Current : CommandLineage::Stale;
}

}  // namespace chordlab::bridge
