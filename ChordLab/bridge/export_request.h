#pragma once

// Plain C++17, header-only: no iPlug2, no Objective-C, no Foundation, so the plug-in shell's message
// handling can be unit-tested outside Xcode (ChordLab/bridge/tests).

#include "message_fields.h"
#include "chordlab/document.h"
#include "chordlab/export.h"
#include "chordlab/json_api.h"
#include "chordlab/tracks.h"

#include <nlohmann/json.hpp>

#include <climits>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
/// A raw NUL byte anywhere is refused before the parse, as the engine's readers refuse one (prescan in
/// engine/src/json_api.cpp); without that, nlohmann's lexer would read it as the end of input and accept
/// a complete message followed by `\0junk`. An escaped `\u0000` inside a JSON string is a different
/// thing: an ordinary character of that string, accepted.
inline std::optional<SectionExportRequest> parseSectionExportRequest(std::string_view json)
{
    if (json.find('\0') != std::string_view::npos)
        return std::nullopt;
    const nlohmann::json message = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (!boundedIntField(message, "schema", chordlab::kJsonSchema, chordlab::kJsonSchema))
        return std::nullopt;
    const std::optional<int> id = boundedIntField(message, "id", 1, INT_MAX);
    if (!id)
        return std::nullopt;
    return SectionExportRequest{*id};
}

/// A kMsgTagExportClip payload: which lanes to write, over the whole progression or one section.
struct ClipExportRequest {
    /// The "All" drag (`"lanes":"all"`): `lanes` is empty and `exportClip` picks the lanes itself.
    bool allLanes = false;
    /// The requested lanes in request order (`chordlab::ClipSpec::lanes` as is): 1..(1 + kMaxTracks) entries,
    /// none repeated, when `allLanes` is false; empty when it is true. A track lane's id is in 1..INT_MAX but
    /// is not checked against the document here.
    std::vector<chordlab::ClipLane> lanes;
    /// The section's stable id (1..INT_MAX); nullopt = the whole progression. Not checked against the document here.
    std::optional<int> sectionId;
};

/// Parses a kMsgTagExportClip payload, `{"schema":1,"lanes":["chords"|<trackId>,...]|"all","sectionId":<id>|null}`.
/// Accepted when the text parses as a JSON object where:
///  - `schema` is the integer kJsonSchema (boundedIntField);
///  - `lanes` is the string "all" exactly (any other string, including "ALL", is refused), or a non-empty array
///    of at most 1 + chordlab::kMaxTracks entries, each the string "chords" exactly or a JSON integer track id
///    in 1..INT_MAX (boundedInt: the full 64-bit value is range-checked before narrowing, so 2^31 and
///    4294967297 are refused, a float such as 1.0, a boolean, null, an object or any other string is refused).
///    One bad entry refuses the whole request. The same lane twice ("chords" twice, or one track id twice) is
///    refused: exportClip would write it twice. Request order is kept;
///  - `sectionId` is absent or JSON null (the whole progression), or a JSON integer in 1..INT_MAX (boundedInt, as
///    `id` in the section request); any other type or value, a float such as 1.0 included, is refused.
/// Other members are ignored. Anything else (malformed JSON, an empty payload, a non-object top level) is
/// nullopt; the caller answers it with kMsgTagError "clip export message needs schema 1, lanes and a section id
/// or null". The parse never throws on malformed input (allow_exceptions=false). Whether a track id or the
/// section id names something in the document is the caller's question (it answers "no such track" / "no such
/// section"). Raw NUL bytes are refused before the parse and an escaped `\u0000` is an ordinary string
/// character, exactly as in parseSectionExportRequest (see there).
inline std::optional<ClipExportRequest> parseClipExportRequest(std::string_view json)
{
    if (json.find('\0') != std::string_view::npos)
        return std::nullopt;
    const nlohmann::json message = nlohmann::json::parse(json, nullptr, /*allow_exceptions=*/false);
    if (!boundedIntField(message, "schema", chordlab::kJsonSchema, chordlab::kJsonSchema))
        return std::nullopt;

    ClipExportRequest request;
    const auto lanes = message.find("lanes");
    if (lanes == message.end())
        return std::nullopt;
    if (lanes->is_string())
    {
        if (lanes->get<std::string>() != "all")
            return std::nullopt;
        request.allLanes = true;
    }
    else if (lanes->is_array())
    {
        constexpr std::size_t kMaxLanes = 1 + static_cast<std::size_t>(chordlab::kMaxTracks);
        if (lanes->empty() || lanes->size() > kMaxLanes)
            return std::nullopt;
        bool haveChords = false;
        for (const nlohmann::json& entry : *lanes)
        {
            if (entry.is_string())
            {
                if (entry.get<std::string>() != "chords" || haveChords)
                    return std::nullopt;
                haveChords = true;
                request.lanes.push_back(chordlab::ClipLane::chordsLane());
                continue;
            }
            const std::optional<int> id = boundedInt(entry, 1, INT_MAX);
            if (!id)
                return std::nullopt;
            for (const chordlab::ClipLane& seen : request.lanes)
                if (!seen.chords && seen.trackId == *id)
                    return std::nullopt;
            request.lanes.push_back(chordlab::ClipLane::trackLane(*id));
        }
    }
    else
        return std::nullopt;

    const auto section = message.find("sectionId");
    if (section != message.end() && !section->is_null())
    {
        const std::optional<int> id = boundedInt(*section, 1, INT_MAX);
        if (!id)
            return std::nullopt;
        request.sectionId = *id;
    }
    return request;
}

/// What a parsed clip export request means for one document: the engine's spec, the lane names for the label, the
/// section's name, or the first problem found.
struct ResolvedClipExport {
    /// `allLanes` and `lanes` as requested; `sectionIndex` the section's position in `doc.sections`, -1 for the
    /// whole progression. Meaningful only when `problem` is null.
    chordlab::ClipSpec spec;
    /// The name of each requested lane, in request order: "Chords" for the chords lane, the track's name for a
    /// track. Empty for `allLanes` (the label does not use names then). Meaningful only when `problem` is null.
    std::vector<std::string> laneNames;
    /// The requested section's name (empty for an unnamed one); nullopt for the whole progression.
    std::optional<std::string> sectionName;
    /// null, or the text of the kMsgTagError to send: "no such section" or "no such track". Static storage.
    const char* problem = nullptr;
};

/// Resolves the ids of `request` against `doc` (the caller holds the model lock). `exportClip` answers an unknown
/// track or section with empty bytes ("nothing to export"), so this names which id is wrong. The section is
/// checked first ("no such section": its id matches no `doc.sections[i].id`; a document without sections has
/// none, whatever `exportClip` would read index 0 as), then the lanes in request order, the first track id that
/// matches no `doc.tracks[i].id` giving "no such track". One problem is reported, the first. With `allLanes` no
/// lane is checked. Pure over `doc`: no engine function is called.
inline ResolvedClipExport resolveClipExport(const chordlab::Document& doc, const ClipExportRequest& request)
{
    ResolvedClipExport out;
    out.spec.allLanes = request.allLanes;
    out.spec.lanes = request.lanes;
    out.spec.sectionIndex = -1;
    if (request.sectionId)
    {
        std::size_t found = doc.sections.size();
        for (std::size_t i = 0; i < doc.sections.size(); ++i)
            if (doc.sections[i].id == *request.sectionId)
            {
                found = i;
                break;
            }
        if (found == doc.sections.size())
        {
            out.problem = "no such section";
            return out;
        }
        out.spec.sectionIndex = static_cast<int>(found);
        out.sectionName = doc.sections[found].name;
    }
    for (const chordlab::ClipLane& lane : request.lanes)
    {
        if (lane.chords)
        {
            out.laneNames.push_back("Chords");
            continue;
        }
        const chordlab::Track* track = nullptr;
        for (const chordlab::Track& candidate : doc.tracks)
            if (candidate.id == lane.trackId)
            {
                track = &candidate;
                break;
            }
        if (!track)
        {
            out.problem = "no such track";
            return out;
        }
        out.laneNames.push_back(track->name);
    }
    return out;
}

}  // namespace chordlab::bridge
