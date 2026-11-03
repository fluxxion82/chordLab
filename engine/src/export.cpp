#include "chordlab/export.h"

#include "chordlab/compiler.h"
#include "chordlab/smf.h"

#include <cstddef>

namespace chordlab {

std::vector<std::uint8_t> exportMidi(const Document& doc, int sectionIndex, double bpm) {
    Compiled compiled;
    if (sectionIndex < 0) {
        compiled = compile(doc);
    } else {
        // Without sections the progression is one unnamed section, index 0 (compileSection).
        const std::size_t sections = doc.sections.empty() ? 1 : doc.sections.size();
        if (static_cast<std::size_t>(sectionIndex) >= sections) {
            return {};
        }
        compiled = compileSection(doc, sectionIndex);
    }
    if (compiled.events.empty()) {
        return {};
    }
    return writeSmf0(compiled, bpm, doc.meter.num, doc.meter.den, 480);
}

}  // namespace chordlab
