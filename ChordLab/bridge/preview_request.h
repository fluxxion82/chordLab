#pragma once

// Plain C++17, header-only: no iPlug2, no Objective-C, no Foundation, so the plug-in shell's message
// handling can be unit-tested outside Xcode (ChordLab/tests).

#include "message_fields.h"
#include "chordlab/json_api.h"
#include "chordlab/scheduler.h"

#include <nlohmann/json.hpp>

#include <climits>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace chordlab::bridge {

/// Parses a kMsgTagPreview payload, `{"schema":1,"id":n,"on":true|false,"velocity":v,"pitches":[..]}`,
/// into the PreviewCmd the plug-in hands to Scheduler::pushPreview. Main thread, never real-time:
/// nlohmann parses here, the scheduler only ever sees plain PreviewCmd.
///
/// Rules, checked in this order (the first failure decides the message written to `*problem`):
///  1. The text parses as a JSON object with `schema` the integer kJsonSchema, `id` an integer in
///     0..INT_MAX and `on` a boolean; else "preview message needs schema 1, an id and on:true|false"
///     (malformed JSON parses without throwing, as a non-object).
///  2. `on:false` is a Stop for `id`; nothing else is read (no pitches needed, a bad velocity ignored).
///  3. `on:true` is a Start: `velocity`, when present, must be an integer in 1..127 (default 100), and
///     `pitches` must be an array of 1..8 elements; else "preview start needs 1..8 pitches and a velocity
///     in 1..127".
///  4. Every pitch must be an integer in 0..127, kept in order (duplicates allowed); else "preview pitches
///     must be integers in 0..127".
///  5. An exception (in practice only std::bad_alloc) is "preview message could not be parsed".
/// On success `*problem` is left untouched. The returned command is value-initialised first, so a Stop
/// carries velocity 0, count 0 and zero pitches, and a Start's unused pitch slots are 0. PreviewCmd has no
/// plug-in-only fields; the plug-in itself only remembers a Start's id for CloseWindow.
inline std::optional<chordlab::PreviewCmd> parsePreviewRequest(std::string_view json, std::string* problem = nullptr)
{
    std::string error;
    chordlab::PreviewCmd cmd{};
    try
    {
        const nlohmann::json message = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
        const std::optional<int> schema = boundedIntField(message, "schema", chordlab::kJsonSchema, chordlab::kJsonSchema);
        const std::optional<int> id = schema ? boundedIntField(message, "id", 0, INT_MAX) : std::nullopt;
        const auto on = message.is_object() ? message.find("on") : message.end();
        if (!message.is_object() || !schema || !id || on == message.end() || !on->is_boolean())
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
                    if (!p.is_number_integer() || p.get<std::int64_t>() < 0 || p.get<std::int64_t>() > 127)
                    {
                        error = "preview pitches must be integers in 0..127";
                        break;
                    }
                    cmd.pitches[cmd.count++] = static_cast<std::uint8_t>(p.get<std::int64_t>());
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
    return cmd;
}

}  // namespace chordlab::bridge
