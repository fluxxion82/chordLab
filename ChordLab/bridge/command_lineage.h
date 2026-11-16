#pragma once

// Plain C++17, header-only: no iPlug2, no Objective-C, no Foundation, so the plug-in shell's message
// handling can be unit-tested outside Xcode (ChordLab/bridge/tests).

#include "message_fields.h"
#include "chordlab/json_api.h"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

namespace chordlab::bridge {

/// What a kMsgTagCommand's or kMsgTagExportClip's `lineage` member says about the document the message was computed
/// for (tracks plan Task 24a, design D21). The editor stamps every command and export request with its clipboard
/// token (`CommandLineage.stamp`, ChordLab/UI/Core/CommandLineage.swift; one token per user action, `ActionLineage`).
/// The shell reads the member BEFORE taking mModelMutex (readLineage: the parse can take milliseconds for a large
/// command) and compares it with `ChordLab::mClipboardToken` under the lock, right before the command is applied or
/// the export reads the document (compareLineage), and acts on the answer:
///   Absent    no `lineage` member, or text that is not a JSON object this reads (see readLineage): not checked;
///             the message is handled as it always was (tests, tools and older callers keep working).
///   Current   the member is the shell's token exactly: handled as usual.
///   Stale     the member is a string, any other string: the message was computed for a document a state load has
///             since replaced. Nothing is applied or exported. A command is answered with the current snapshot alone
///             (not an error: a click that raced a host's state load is not the user's mistake); an export with the
///             snapshot and then kMsgTagError kStaleExportError, so the drag's exchange fails cleanly.
///   Malformed the member is present but not a string (null included): nothing is applied or exported, and the shell
///             answers with kMsgTagError kMalformedLineageError, as the engine answers any mistyped field. The editor
///             never sends one.
///   Failed    reading the member threw (an allocation failure): nothing is applied or exported, never "apply" (the
///             engine's own parse might succeed afterwards on a stale command), and the shell answers with kMsgTagError
///             kLineageFailedError, the engine's wording for a failure it did not anticipate.
enum class CommandLineage { Absent, Current, Stale, Malformed, Failed };

/// The error a Malformed lineage is answered with (the engine's wording for a mistyped field).
inline constexpr const char* kMalformedLineageError = "\"lineage\" must be a string";
/// The error a Failed read is answered with: what applyCommandJson reports for an exception (json_api.h).
inline constexpr const char* kLineageFailedError = "internal error";
/// The error a stale export request is answered with (after the current snapshot, which would clear an earlier one).
inline constexpr const char* kStaleExportError = "The project was reloaded; drag again";

/// The `lineage` member as read from a message's text, without the shell's token (readLineage).
struct LineageField {
    enum class Kind { Absent, String, Malformed, Failed };
    Kind kind = Kind::Absent;
    std::string value;                                           // the decoded string, for Kind::String only
};

/// Reads `text`'s `lineage` member; the shell calls it outside mModelMutex. Only text the engine itself would read is
/// read here: a text beyond `withinMessageLimits` (too long, a raw NUL byte, nested too deep), malformed JSON or a
/// top level that is not an object is Absent, so the engine (or the export's own parse, which applies the same
/// limits) refuses it with exactly the message it always gave. In an
/// object, only the top-level member named `lineage` counts (after JSON decoding, so an escaped key counts too; a
/// nested one or another spelling is just a field the engine ignores); a member given twice counts with its last
/// value, as nlohmann (and so the engine) reads it. Never throws: anything thrown (an allocation failure) is Failed.
inline LineageField readLineage(std::string_view text) noexcept
{
    try
    {
        if (!withinMessageLimits(text))
            return {};
        const nlohmann::json message = nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false);
        if (!message.is_object())                                // a discarded parse is not an object either
            return {};
        const auto it = message.find("lineage");
        if (it == message.end())
            return {};
        LineageField field;
        if (!it->is_string())
        {
            field.kind = LineageField::Kind::Malformed;
            return field;
        }
        field.kind = LineageField::Kind::String;
        field.value = it->get_ref<const std::string&>();
        return field;
    }
    catch (...)
    {
        LineageField failed;                                     // no allocation: an empty string
        failed.kind = LineageField::Kind::Failed;
        return failed;
    }
}

/// Compares a field readLineage gave with `token`, the shell's current document lineage; the shell calls it under
/// mModelMutex. A byte-for-byte comparison: no parse, no allocation, no throw.
inline CommandLineage compareLineage(const LineageField& field, std::string_view token) noexcept
{
    switch (field.kind)
    {
        case LineageField::Kind::Absent:
            return CommandLineage::Absent;
        case LineageField::Kind::String:
            return std::string_view(field.value) == token ? CommandLineage::Current : CommandLineage::Stale;
        case LineageField::Kind::Malformed:
            return CommandLineage::Malformed;
        case LineageField::Kind::Failed:
            break;
    }
    return CommandLineage::Failed;
}

/// Whether the message goes on to be applied or exported: only for an absent or a current lineage.
inline bool lineageApplies(CommandLineage lineage) noexcept
{
    return lineage == CommandLineage::Absent || lineage == CommandLineage::Current;
}

/// readLineage and compareLineage in one call (the tests' shorthand; the shell splits them around the lock).
inline CommandLineage commandLineage(std::string_view text, std::string_view token) noexcept
{
    return compareLineage(readLineage(text), token);
}

}  // namespace chordlab::bridge
