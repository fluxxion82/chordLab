#pragma once

#include "chordlab/document.h"

#include <cstdint>
#include <vector>

namespace chordlab {

/// The .mid bytes for the whole progression (`sectionIndex` < 0) or the section at `sectionIndex` of
/// `doc.sections` (`compileSection`; a document without sections has one section, index 0), at `bpm`, with
/// the document's meter in the time signature: `writeSmf0(compiled, bpm, meter.num, meter.den, 480)` of
/// `compile(doc)` or `compileSection(doc, sectionIndex)`, so the clip holds exactly what plays, feel
/// included. Empty when there is nothing to write: no events, a section index that names no section, or a
/// tempo `writeSmf0` refuses. Same document preconditions as `compile`. Allocates: never on the audio thread.
std::vector<std::uint8_t> exportMidi(const Document& doc, int sectionIndex, double bpm);

}  // namespace chordlab
