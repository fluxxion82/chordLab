#pragma once

#include "chordlab/document.h"

#include <cstdint>
#include <string>
#include <vector>

namespace chordlab {

/// How many chords `exportClipName` spells out before it ends the name with an ellipsis.
constexpr int kClipNameChords = 8;

/// The name Live gives a clip dropped from the export: the file's track name (`writeSmf0`'s `trackName`).
/// For the whole progression (`sectionIndex` < 0, or 0 in a document without sections) it is the chord
/// symbols spelled in the project key (`chordSymbol`, the sequence's spelling), separated by spaces: the
/// first `kClipNameChords` of them, followed by " ..." when there are more ("C Am F G7",
/// "C7 F7 C7 C7 F7 F7 C7 C7 ..."). For a section (`sectionIndex` into `doc.sections`) it is the section's
/// name, or, when that is empty, the same summary of the section's own chords. Empty for an empty
/// progression or a section index that names no section. Allocates: never on the audio thread.
std::string exportClipName(const Document& doc, int sectionIndex);

/// The .mid bytes for the whole progression (`sectionIndex` < 0) or the section at `sectionIndex` of
/// `doc.sections` (`compileSection`; a document without sections has one section, index 0), at `bpm`, with
/// the document's meter in the time signature and `exportClipName(doc, sectionIndex)` as the track name:
/// `writeSmf0(compiled, bpm, meter.num, meter.den, 480, 0, name)` of `compile(doc)` or
/// `compileSection(doc, sectionIndex)`, so the clip holds exactly what plays, feel included, and Live names
/// it after the chords or the section. Empty when there is nothing to write: no events, a section index
/// that names no section, or a tempo `writeSmf0` refuses. Same document preconditions as `compile`.
/// Allocates: never on the audio thread.
std::vector<std::uint8_t> exportMidi(const Document& doc, int sectionIndex, double bpm);

}  // namespace chordlab
