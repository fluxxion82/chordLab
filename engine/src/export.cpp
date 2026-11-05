#include "chordlab/export.h"

#include "chordlab/compiler.h"
#include "chordlab/smf.h"
#include "chordlab/symbol.h"

#include <algorithm>
#include <cstddef>
#include <string>

namespace chordlab {

namespace {

/// The symbols of `doc.events[first, last)` in the project key, space-separated, the first `kClipNameChords`
/// of them and " ..." when there are more.
std::string chordSummary(const Document& doc, std::size_t first, std::size_t last) {
    const std::size_t shown = std::min(last - first, static_cast<std::size_t>(kClipNameChords));
    std::string out;
    for (std::size_t i = first; i < first + shown; ++i) {
        if (!out.empty()) {
            out += ' ';
        }
        out += chordSymbol(doc.events[i].chord, doc.projectKey);
    }
    if (last - first > shown) {
        out += " ...";
    }
    return out;
}

}  // namespace

std::string exportClipName(const Document& doc, int sectionIndex) {
    const std::size_t count = doc.events.size();
    if (sectionIndex < 0 || doc.sections.empty()) {
        // Without sections the progression is one unnamed section, index 0 (compileSection).
        return sectionIndex > 0 ? std::string() : chordSummary(doc, 0, count);
    }
    const std::size_t index = static_cast<std::size_t>(sectionIndex);
    if (index >= doc.sections.size()) {
        return {};
    }
    const Section& section = doc.sections[index];
    if (!section.name.empty()) {
        return section.name;
    }
    const std::size_t first = static_cast<std::size_t>(section.firstEventIndex);
    const std::size_t last =
        index + 1 < doc.sections.size() ? static_cast<std::size_t>(doc.sections[index + 1].firstEventIndex) : count;
    return chordSummary(doc, first, last);
}

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
    return writeSmf0(compiled, bpm, doc.meter.num, doc.meter.den, 480, 0, exportClipName(doc, sectionIndex));
}

}  // namespace chordlab
