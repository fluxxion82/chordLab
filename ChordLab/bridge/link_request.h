#pragma once

// Plain C++17, header-only: no iPlug2, no Objective-C, no Foundation, so the plug-in shell's message
// handling can be unit-tested outside Xcode (ChordLab/bridge/tests).

#include "message_fields.h"
#include "chordlab/json_api.h"
#include "chordlab/link_state.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace chordlab::bridge {

static_assert(chordlab::kMaxLinkIdBytes == 64, "the follow problem below names the bound");

/// A link request from the editor (per-track instruments plan I12): follow a leader's lane, or stop following.
struct LinkRequest
{
    bool follow = false;           // true: follow; false: unfollow, and the strings below are empty
    std::string leaderId;          // the leader instance's id, as the link state lists it
    std::string laneUid;           // "chords" or a lane uid of that leader
    std::string leaderLineage;     // the leader's lineage as the editor saw it (LinkEndpoint::follow checks it)
};

/// Parses a link payload, `{"schema":1,"op":"follow","leader":"<id>","lane":"chords"|"<uid>","leaderLineage":"<token>"}`
/// or `{"schema":1,"op":"unfollow"}`, into the LinkRequest the shell hands to LinkEndpoint::follow / unfollow.
/// Main thread, never real-time.
///
/// Rules, checked in this order (the first failure decides the message written to `*problem`):
///  0. A raw NUL byte anywhere is "link message could not be parsed", before any parse, as the engine's readers
///     refuse one (prescan in engine/src/json_api.cpp). An escaped `\u0000` inside a JSON string is a different
///     thing: an ordinary character of that string, accepted.
///  1. The text parses as a JSON object with `schema` the integer kJsonSchema (boundedIntField) and `op` the string
///     "follow" or "unfollow" exactly; else "link message needs schema 1 and op \"follow\" or \"unfollow\""
///     (malformed JSON parses without throwing, as a non-object).
///  2. "unfollow": nothing else is read; the request has `follow` false and empty strings.
///  3. "follow": `leader`, `lane` and `leaderLineage` must each be a string of 1..chordlab::kMaxLinkIdBytes bytes
///     (after JSON decoding; "chords" is a lane like any other); else "link follow needs a leader, a lane and a
///     leaderLineage of 1..64 bytes". Their content is not checked further: whether the leader, its lane and its
///     lineage are current is the registry's question (LinkRegistry::follow).
///  4. An exception (in practice only std::bad_alloc) is "link message could not be parsed".
/// Other members are ignored, and in particular the message's own `lineage` (the editor's document lineage, on
/// every command): the shell reads it separately with readLineage (command_lineage.h), so it is neither required,
/// parsed nor rejected here, whatever its type. Duplicate members are nlohmann's call: the last one wins.
/// On success `*problem` is left untouched.
inline std::optional<LinkRequest> parseLinkRequest(std::string_view json, std::string* problem = nullptr)
{
    if (json.find('\0') != std::string_view::npos)
    {
        if (problem)
            *problem = "link message could not be parsed";
        return std::nullopt;
    }
    std::string error;
    LinkRequest request;
    try
    {
        const nlohmann::json message = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
        const std::optional<int> schema = boundedIntField(message, "schema", chordlab::kJsonSchema, chordlab::kJsonSchema);
        const auto op = message.is_object() ? message.find("op") : message.end();
        const bool isFollow = schema && op != message.end() && op->is_string() && op->get_ref<const std::string&>() == "follow";
        const bool isUnfollow =
            schema && op != message.end() && op->is_string() && op->get_ref<const std::string&>() == "unfollow";
        // A member that is a string of 1..kMaxLinkIdBytes bytes, else nullopt.
        auto linkString = [&message](const char* name) -> std::optional<std::string> {
            const auto it = message.find(name);
            if (it == message.end() || !it->is_string())
                return std::nullopt;
            const std::string& value = it->get_ref<const std::string&>();
            if (value.empty() || value.size() > chordlab::kMaxLinkIdBytes)
                return std::nullopt;
            return value;
        };
        if (!isFollow && !isUnfollow)
            error = "link message needs schema 1 and op \"follow\" or \"unfollow\"";
        else if (isFollow)
        {
            std::optional<std::string> leader = linkString("leader");
            std::optional<std::string> lane = linkString("lane");
            std::optional<std::string> lineage = linkString("leaderLineage");
            if (!leader || !lane || !lineage)
                error = "link follow needs a leader, a lane and a leaderLineage of 1..64 bytes";
            else
            {
                request.follow = true;
                request.leaderId = std::move(*leader);
                request.laneUid = std::move(*lane);
                request.leaderLineage = std::move(*lineage);
            }
        }
    }
    catch (...)
    {
        error = "link message could not be parsed";
    }

    if (!error.empty())
    {
        if (problem)
            *problem = std::move(error);
        return std::nullopt;
    }
    return request;
}

}  // namespace chordlab::bridge
