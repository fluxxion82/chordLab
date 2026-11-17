#pragma once

// Plain C++17, header-only: no iPlug2, no Objective-C, no Foundation, so the plug-in shell's message
// handling can be unit-tested outside Xcode (ChordLab/bridge/tests).

#include "message_fields.h"
#include "chordlab/json_api.h"
#include "chordlab/link.h"        // kForwardedPreviewIdFirst only: the ids a link registry forwards under
#include "chordlab/scheduler.h"

#include <nlohmann/json.hpp>

#include <climits>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace chordlab::bridge {

/// A parsed kMsgTagPreview payload: the command the plug-in hands to Scheduler::pushPreview (or offers to its
/// link endpoint first), and the lane it belongs to.
struct PreviewRequest {
    chordlab::PreviewCmd cmd{};
    /// A Start's lane: the id of the track whose roll started it, or empty for the chords lane. Always empty for an
    /// Extend and a Stop, which follow their Start.
    std::optional<int> track;
};

/// Parses a kMsgTagPreview payload, `{"schema":1,"id":n,"on":true|false,"velocity":v,"pitches":[..]}` or
/// `{"schema":1,"id":n,"extend":true}`, a Start optionally with `"track":t`, into a PreviewRequest: the PreviewCmd
/// the plug-in hands to Scheduler::pushPreview or its link endpoint, and the Start's lane. Main thread, never real-time: nlohmann parses here, the scheduler only ever sees plain PreviewCmd.
///
/// Rules, checked in this order (the first failure decides the message written to `*problem`):
///  0. A raw NUL byte anywhere is "preview message could not be parsed", before any parse, as the
///     engine's readers refuse one (prescan in engine/src/json_api.cpp). An escaped `\u0000` inside a
///     JSON string is a different thing: an ordinary character of that string, accepted.
///  1. The text parses as a JSON object with `schema` the integer kJsonSchema, `id` an integer in
///     0..kForwardedPreviewIdFirst - 1 and, when it has no `extend` member, `on` a boolean; else "preview
///     message needs schema 1, an id and on:true|false" (malformed JSON parses without throwing, as a
///     non-object). The ids from kForwardedPreviewIdFirst to INT_MAX are the ones a link registry forwards
///     previews under (chordlab/link.h, its rule 12): refused from an editor, so that a forwarded id never
///     meets an editor's in one scheduler. A message with an `extend` member goes to rule 6 instead of rules
///     2 to 4 and 7.
///  2. `on:false` is a Stop for `id`; nothing else is read (no pitches needed, a bad velocity or track
///     ignored).
///  3. `on:true` is a Start: `velocity`, when present, must be an integer in 1..127 (default 100), and
///     `pitches` must be an array of 1..8 elements; else "preview start needs 1..8 pitches and a velocity
///     in 1..127".
///  4. Every pitch must be an integer in 0..127, kept in order (duplicates allowed); else "preview pitches
///     must be integers in 0..127".
///  5. An exception (in practice only std::bad_alloc) is "preview message could not be parsed".
///  6. `extend:true` with no `on` is an Extend for `id`; nothing else is read (a track included). `extend`
///     must be the boolean true and `on` absent; else "preview extend needs extend:true and no on". (The UI
///     and the engine ship together, so an older engine never sees `extend`; an older UI sends none, and its
///     previews end after four seconds as before.)
///  7. After rule 4, a Start's `track`, when present, must be an integer in 1..INT_MAX, and the Start then
///     belongs to that track's lane (`PreviewRequest::track`); else "preview track must be an integer of 1
///     or more". Without `track` it belongs to the chords lane. Whether such a track exists is not checked
///     here: the link endpoint forwards only a lane it knows (LinkEndpoint::forwardPreview), and plays the
///     rest itself.
/// Without rule 0, nlohmann's lexer would read a raw NUL as the end of input and accept a complete message
/// followed by `\0junk`. Duplicate members are nlohmann's call: the last one wins.
/// On success `*problem` is left untouched. The returned command is value-initialised first, so a Stop
/// or an Extend carries velocity 0, count 0 and zero pitches, and a Start's unused pitch slots are 0.
/// PreviewCmd has no plug-in-only fields: the lane travels beside it, in PreviewRequest, and the plug-in itself
/// only remembers a Start's id for CloseWindow.
inline std::optional<PreviewRequest> parsePreviewRequest(std::string_view json, std::string* problem = nullptr)
{
    if (json.find('\0') != std::string_view::npos)
    {
        if (problem)
            *problem = "preview message could not be parsed";
        return std::nullopt;
    }
    std::string error;
    chordlab::PreviewCmd cmd{};
    std::optional<int> track;
    try
    {
        const nlohmann::json message = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
        const std::optional<int> schema = boundedIntField(message, "schema", chordlab::kJsonSchema, chordlab::kJsonSchema);
        const std::optional<int> id =
            schema ? boundedIntField(message, "id", 0, chordlab::kForwardedPreviewIdFirst - 1) : std::nullopt;
        const auto on = message.is_object() ? message.find("on") : message.end();
        const auto extend = message.is_object() ? message.find("extend") : message.end();
        if (!message.is_object() || !schema || !id)
            error = "preview message needs schema 1, an id and on:true|false";
        else if (extend != message.end())
        {
            if (!extend->is_boolean() || !extend->get<bool>() || on != message.end())
                error = "preview extend needs extend:true and no on";
            else
            {
                cmd.kind = chordlab::PreviewCmd::Extend;
                cmd.id = *id;
            }
        }
        else if (on == message.end() || !on->is_boolean())
            error = "preview message needs schema 1, an id and on:true|false";
        else if (on->get<bool>())
        {
            std::optional<int> velocity = 100;
            if (message.contains("velocity"))
                velocity = boundedIntField(message, "velocity", 1, 127);
            const auto pitches = message.find("pitches");
            if (!velocity || pitches == message.end() || !pitches->is_array() || pitches->empty() || pitches->size() > 8)
                error = "preview start needs 1..8 pitches and a velocity in 1..127";
            else
            {
                cmd.kind = chordlab::PreviewCmd::Start;
                cmd.id = *id;
                cmd.velocity = *velocity;
                cmd.count = 0;
                for (const auto& p : *pitches)
                {
                    const std::optional<int> pitch = boundedInt(p, 0, 127);
                    if (!pitch)
                    {
                        error = "preview pitches must be integers in 0..127";
                        break;
                    }
                    cmd.pitches[cmd.count++] = static_cast<std::uint8_t>(*pitch);
                }
                if (error.empty() && message.contains("track"))
                {
                    track = boundedIntField(message, "track", 1, INT_MAX);
                    if (!track)
                        error = "preview track must be an integer of 1 or more";
                }
            }
        }
        else
        {
            cmd.kind = chordlab::PreviewCmd::Stop;
            cmd.id = *id;
        }
    }
    catch (...)
    {
        error = "preview message could not be parsed";
    }

    if (!error.empty())
    {
        if (problem)
            *problem = std::move(error);
        return std::nullopt;
    }
    return PreviewRequest{cmd, track};
}

}  // namespace chordlab::bridge
