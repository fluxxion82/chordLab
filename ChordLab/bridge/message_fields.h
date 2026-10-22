#pragma once

// Plain C++17, header-only: no iPlug2, no Objective-C, no Foundation, so the plug-in shell's message
// handling can be unit-tested outside Xcode (ChordLab/bridge/tests).

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>

namespace chordlab::bridge {

/// `value` as an int, if it is a JSON integer in `lo..hi` (both inclusive); otherwise nullopt. A float
/// (even `1.0`), a string, a boolean, null, an array or an object is nullopt. The one place the plug-in
/// bounds a JSON integer: boundedIntField uses it for object members, parsePreviewRequest for pitches.
///
/// The full 64-bit value is read and range-checked BEFORE narrowing to int, so an oversized schema
/// (4294967297, which narrows to 1) or id (2^31, -2^40) is refused rather than read as something in
/// range. nlohmann stores a JSON integer as int64, or as uint64 beyond INT64_MAX; such a uint64 is refused
/// before any signed conversion (converting it to int64 is implementation-defined in C++17).
///
/// Precondition: `lo <= hi`, both within int, so the narrowing of an accepted value is exact.
inline std::optional<int> boundedInt(const nlohmann::json& value, std::int64_t lo, std::int64_t hi)
{
    if (!value.is_number_integer())
        return std::nullopt;
    // Beyond INT64_MAX: out of every int range. Checked as uint64, because get<int64_t> on it would be an
    // implementation-defined conversion (it wraps negative on the compilers we use, which was refused too).
    if (value.is_number_unsigned() && value.get<std::uint64_t>() > static_cast<std::uint64_t>(INT64_MAX))
        return std::nullopt;
    const auto v = value.get<std::int64_t>();
    if (v < lo || v > hi)
        return std::nullopt;
    return static_cast<int>(v);
}

/// The integer member `name` of the JSON object `object`, if it is present and boundedInt accepts it
/// (a JSON integer in `lo..hi`, same precondition); otherwise nullopt. A non-object `object` or a missing
/// member is nullopt too.
inline std::optional<int> boundedIntField(const nlohmann::json& object, const char* name, std::int64_t lo,
                                          std::int64_t hi)
{
    if (!object.is_object())
        return std::nullopt;
    const auto it = object.find(name);
    if (it == object.end())
        return std::nullopt;
    return boundedInt(*it, lo, hi);
}

}  // namespace chordlab::bridge
