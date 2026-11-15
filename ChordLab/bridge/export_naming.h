#pragma once

// Plain C++17, header-only: no iPlug2, no Objective-C, no Foundation, so the plug-in shell's export file
// naming can be unit-tested outside Xcode (ChordLab/bridge/tests).

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace chordlab::bridge {

/// The export label made safe for a file name: an ASCII letter or digit is kept, '#' becomes 's' (a
/// sharp), and every other byte becomes '-', one per byte, so a multi-byte UTF-8 character becomes as
/// many '-' as it has bytes. "C Ionian" -> "C-Ionian", "F# Dorian" -> "Fs-Dorian", "C Ionian-Verse" ->
/// "C-Ionian-Verse". The letter/digit test is std::isalnum in the process's C locale (plug-ins run in
/// the "C" locale unless a host changes it).
inline std::string safeExportLabel(std::string_view label)
{
    std::string safe;
    for (const char c : label)
        safe += std::isalnum(static_cast<unsigned char>(c)) ? c : (c == '#' ? 's' : '-');
    return safe;
}

/// The label (before safeExportLabel) of a clip export, D12: `<key>-<lane label>`, then `-<section name>` for a
/// section. The lane label is the lane's name for one lane ("Chords" for the chords lane, the track's name for a
/// track: the caller passes the names in request order), "All" when `allLanes` (whatever `laneNames` holds), and
/// "Lanes" for several explicit lanes: a joined list of up to five 64-byte track names could pass the file
/// system's 255-byte name limit, so the label does not grow with the lanes. `sectionName` empty means the whole
/// progression (or an unnamed section: no segment is added). An empty lane name, or no lanes at all without
/// `allLanes`, leaves its segment out rather than a dangling '-'. "C Ionian" + {"Bass"} -> "C Ionian-Bass",
/// + {"Chords"} and "Verse" -> "C Ionian-Chords-Verse", all lanes -> "C Ionian-All".
inline std::string clipExportLabel(std::string_view keyName, const std::vector<std::string>& laneNames, bool allLanes,
                                   std::string_view sectionName)
{
    std::string label(keyName);
    std::string_view lane;
    if (allLanes)
        lane = "All";
    else if (laneNames.size() == 1)
        lane = laneNames[0];
    else if (laneNames.size() > 1)
        lane = "Lanes";
    if (!lane.empty())
    {
        label += '-';
        label += lane;
    }
    if (!sectionName.empty())
    {
        label += '-';
        label += sectionName;
    }
    return label;
}

/// The file name (no directory) of one export:
/// `ChordLab-<safeLabel>-<stamp>-<pid>-<counter>-<uuid8>.mid`, where `stamp` is the local time as
/// yyyyMMdd-HHmmss, `pid` the process id, `counter` the instance's export counter and `uuid8` the first
/// eight characters of a fresh UUID; pid, counter and uuid make it unique across instances and
/// processes. Every name it produces satisfies isChordLabExportName.
inline std::string exportFileName(std::string_view safeLabel, std::string_view stamp, long pid,
                                  unsigned long counter, std::string_view uuid8)
{
    std::string name = "ChordLab-";
    name += safeLabel;
    name += '-';
    name += stamp;
    name += '-';
    name += std::to_string(pid);
    name += '-';
    name += std::to_string(counter);
    name += '-';
    name += uuid8;
    name += ".mid";
    return name;
}

/// Whether `fileName` (one directory entry's name, no path) is what the stale-export purge treats as an
/// export: it starts with "ChordLab-" and ends with ".mid". Only the name is looked at: whether the entry
/// is a regular file is not checked here.
inline bool isChordLabExportName(std::string_view fileName)
{
    constexpr std::string_view prefix = "ChordLab-";
    constexpr std::string_view suffix = ".mid";
    return fileName.size() >= prefix.size() && fileName.size() >= suffix.size() &&
           fileName.substr(0, prefix.size()) == prefix &&
           fileName.substr(fileName.size() - suffix.size()) == suffix;
}

}  // namespace chordlab::bridge
