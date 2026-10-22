#pragma once

// Plain C++17, header-only: no iPlug2, no Objective-C, no Foundation, so the plug-in shell's message
// handling can be unit-tested outside Xcode (ChordLab/tests).

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>

namespace chordlab::bridge {

/// The integer member `name` of the JSON object `object`, if it is present, is a JSON integer and lies in
/// `lo..hi` (both inclusive); otherwise nullopt. A non-object `object`, a missing member, a float (even
/// `1.0`), a string, a boolean or null is nullopt.
///
/// The full 64-bit value is read and range-checked BEFORE narrowing to int, so an oversized schema
/// (4294967297, which narrows to 1) or id (2^31, -2^40) is refused rather than read as something in
/// range. nlohmann stores a JSON integer as int64 (or uint64 beyond INT64_MAX, which get<int64_t> wraps
/// negative: still out of every range the plug-in uses).
///
/// Precondition: `0 <= lo <= hi <= INT_MAX`. The upper bound keeps the narrowing exact; the lower one is
/// what makes the uint64 wrap above harmless (every call site in ChordLab.mm satisfies it).
inline std::optional<int> boundedIntField(const nlohmann::json& object, const char* name, std::int64_t lo,
                                          std::int64_t hi)
{
    if (!object.is_object())
        return std::nullopt;
    const auto it = object.find(name);
    if (it == object.end() || !it->is_number_integer())
        return std::nullopt;
    const auto v = it->get<std::int64_t>();
    if (v < lo || v > hi)
        return std::nullopt;
    return static_cast<int>(v);
}

}  // namespace chordlab::bridge
