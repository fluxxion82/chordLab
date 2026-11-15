#include "doctest.h"

#include "bridge/export_naming.h"

#include <string>

using chordlab::bridge::clipExportLabel;
using chordlab::bridge::exportFileName;
using chordlab::bridge::isChordLabExportName;
using chordlab::bridge::safeExportLabel;

TEST_CASE("safeExportLabel: letters and digits kept, '#' -> 's', anything else '-' per byte") {
    // U+266B (beamed eighth notes) is three UTF-8 bytes, E2 99 AB: three '-'.
    CHECK(safeExportLabel("C# Dorian-Verse \xE2\x99\xAB") == "Cs-Dorian-Verse----");
    CHECK(safeExportLabel("C Ionian") == "C-Ionian");
    CHECK(safeExportLabel("F# Dorian") == "Fs-Dorian");
    CHECK(safeExportLabel("Bb HarmonicMinor-Chorus 2") == "Bb-HarmonicMinor-Chorus-2");
    CHECK(safeExportLabel("a/b\\c:d.e_f") == "a-b-c-d-e-f");
}

TEST_CASE("safeExportLabel: empty and all-safe labels") {
    CHECK(safeExportLabel("") == "");
    CHECK(safeExportLabel("CIonian0123456789xyz") == "CIonian0123456789xyz");
}

TEST_CASE("exportFileName: ChordLab-<label>-<stamp>-<pid>-<counter>-<uuid8>.mid") {
    CHECK(exportFileName("Cs-Dorian", "20261021-230615", 4242, 7, "1A2B3C4D") ==
          "ChordLab-Cs-Dorian-20261021-230615-4242-7-1A2B3C4D.mid");
    CHECK(exportFileName("", "20260101-000000", 1, 0, "00000000") == "ChordLab--20260101-000000-1-0-00000000.mid");
    CHECK(exportFileName("C-Ionian", "20261021-230615", 99999, 4294967295UL, "ABCDEF01") ==
          "ChordLab-C-Ionian-20261021-230615-99999-4294967295-ABCDEF01.mid");
}

TEST_CASE("isChordLabExportName: own names match") {
    CHECK(isChordLabExportName(exportFileName(safeExportLabel("C# Dorian-Verse"), "20261021-230615", 4242, 7, "1A2B3C4D")));
    CHECK(isChordLabExportName("ChordLab-C-Ionian-20261021-230615-1-1-ABCDEF01.mid"));
    CHECK(isChordLabExportName("ChordLab-.mid"));                // the shortest name the rule accepts
}

TEST_CASE("isChordLabExportName: foreign files do not match") {
    CHECK_FALSE(isChordLabExportName("Song.mid"));
    CHECK_FALSE(isChordLabExportName("chordlab-C-Ionian.mid"));  // case-sensitive, as NSString hasPrefix
    CHECK_FALSE(isChordLabExportName("ChordLab_C-Ionian.mid"));
    CHECK_FALSE(isChordLabExportName("ChordLab-C-Ionian.MID"));
    CHECK_FALSE(isChordLabExportName("ChordLab-C-Ionian.txt"));
    CHECK_FALSE(isChordLabExportName("ChordLab-C-Ionian.mid.txt"));
    CHECK_FALSE(isChordLabExportName("notes.txt"));
    CHECK_FALSE(isChordLabExportName(""));
    CHECK_FALSE(isChordLabExportName("ChordLab-"));
    CHECK_FALSE(isChordLabExportName(".mid"));
    CHECK_FALSE(isChordLabExportName("x/ChordLab-C.mid"));      // a name, never a path
}

TEST_CASE("isChordLabExportName: directories are judged by name only") {
    // The purge's own folder and ordinary sub-folders do not match...
    CHECK_FALSE(isChordLabExportName("ChordLab"));
    CHECK_FALSE(isChordLabExportName("ChordLab-exports"));
    CHECK_FALSE(isChordLabExportName("."));
    CHECK_FALSE(isChordLabExportName(".."));
    // ...but the predicate sees names, not file types: a directory named like an export matches, exactly as
    // PurgeStaleExports's hasPrefix/hasSuffix test always has.
    CHECK(isChordLabExportName("ChordLab-folder.mid"));
}

TEST_CASE("clipExportLabel: one lane is <key>-<lane name>") {
    CHECK(clipExportLabel("C Ionian", {"Chords"}, false, "") == "C Ionian-Chords");
    CHECK(clipExportLabel("C Ionian", {"Bass"}, false, "") == "C Ionian-Bass");
    CHECK(safeExportLabel(clipExportLabel("F# Dorian", {"Lead 2"}, false, "")) == "Fs-Dorian-Lead-2");
}

TEST_CASE("clipExportLabel: all lanes is <key>-All, several explicit lanes <key>-Lanes") {
    CHECK(clipExportLabel("C Ionian", {}, true, "") == "C Ionian-All");
    // allLanes wins over whatever names are passed.
    CHECK(clipExportLabel("C Ionian", {"Chords", "Bass"}, true, "") == "C Ionian-All");
    CHECK(clipExportLabel("C Ionian", {"Chords", "Bass"}, false, "") == "C Ionian-Lanes");
    CHECK(clipExportLabel("C Ionian", {"Bass", "Lead", "Other", "Extra", "Chords"}, false, "") == "C Ionian-Lanes");
}

TEST_CASE("clipExportLabel: a section name is appended, as the section export's label") {
    CHECK(clipExportLabel("C Ionian", {"Chords"}, false, "Verse") == "C Ionian-Chords-Verse");
    CHECK(clipExportLabel("C Ionian", {"Bass"}, false, "Chorus 2") == "C Ionian-Bass-Chorus 2");
    CHECK(clipExportLabel("C Ionian", {}, true, "Verse") == "C Ionian-All-Verse");
    CHECK(clipExportLabel("C Ionian", {"Chords", "Lead"}, false, "Verse") == "C Ionian-Lanes-Verse");
    CHECK(safeExportLabel(clipExportLabel("C Ionian", {"Bass"}, false, "Chorus 2")) == "C-Ionian-Bass-Chorus-2");
}

TEST_CASE("clipExportLabel: an empty lane name leaves no dangling dash; no lanes is just the key") {
    CHECK(clipExportLabel("C Ionian", {""}, false, "") == "C Ionian");
    CHECK(clipExportLabel("C Ionian", {""}, false, "Verse") == "C Ionian-Verse");
    CHECK(clipExportLabel("C Ionian", {}, false, "") == "C Ionian");
}

TEST_CASE("clipExportLabel: a non-ASCII track name becomes one '-' per byte in the safe label") {
    // U+00E9 is two UTF-8 bytes (C3 A9), U+266B three (E2 99 AB).
    CHECK(safeExportLabel(clipExportLabel("C Ionian", {"Caf\xC3\xA9 \xE2\x99\xAB"}, false, "")) == "C-Ionian-Caf------");
    CHECK(safeExportLabel(clipExportLabel("C Ionian", {"Caf\xC3\xA9"}, false, "Verse")) == "C-Ionian-Caf---Verse");
}

TEST_CASE("clipExportLabel: the label never grows with the number of lanes") {
    // Five 64-byte names joined would be over the file system's 255-byte name limit; "Lanes" cannot be.
    const std::string longName(64, 'x');
    const std::string label = clipExportLabel("C# HarmonicMinor", {longName, longName, longName, longName, longName}, false, "");
    CHECK(label == "C# HarmonicMinor-Lanes");
}
