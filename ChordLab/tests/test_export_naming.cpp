#include "doctest.h"

#include "bridge/export_naming.h"

#include <string>

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
