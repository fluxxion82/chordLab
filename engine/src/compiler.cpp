#include "chordlab/compiler.h"

#include "chordlab/voicing.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <tuple>

namespace chordlab {

namespace {

/// Checks the `DocumentStore` invariants that the compiler relies on: a positive length keeps every off
/// strictly after its on, the length cap bounds the accumulated beats to `kMaxLengthBeats` per chord,
/// and a velocity of at least 1 keeps an on from reading as a MIDI note-off.
void assertValidEvent([[maybe_unused]] const ChordEvent& event) {
    assert(event.chord.type != nullptr);
    assert(event.chord.root >= 0 && event.chord.root < 12);
    assert(std::isfinite(event.lengthBeats) && event.lengthBeats >= kMinLengthBeats &&
           event.lengthBeats <= kMaxLengthBeats);
    assert(event.velocity >= kMinVelocity && event.velocity <= kMaxVelocity);
}

/// The sort key: beat, then offs before ons, then ascending pitch. Over one compiled document the key
/// is unique (ons at a beat come from one chord, whose voicing has no duplicate pitches; likewise offs),
/// so this is a total order and the sorted output is deterministic.
bool keyLess(const NoteEvent& a, const NoteEvent& b) {
    return std::make_tuple(a.beat, a.on ? 1 : 0, a.pitch) < std::make_tuple(b.beat, b.on ? 1 : 0, b.pitch);
}

}  // namespace

Compiled compileRange(const Document& doc, int firstEvent, int count) {
    assert(firstEvent >= 0 && count >= 0);
    assert(static_cast<std::size_t>(firstEvent) + static_cast<std::size_t>(count) <= doc.events.size());
    Compiled compiled;
    // Start beats are accumulated in a single pass, in the same order and with the same additions as
    // `Document::endBeat` does for a document holding just these chords. The running value is reused as
    // the off beat of one chord and the on beat of the next, so adjacent events compare equal exactly
    // rather than merely closely.
    double beat = 0.0;
    const auto begin = doc.events.begin() + firstEvent;
    for (auto it = begin; it != begin + count; ++it) {
        const ChordEvent& event = *it;
        assertValidEvent(event);
        const double start = beat;
        beat += event.lengthBeats;
        const double end = beat;
        for (const int pitch : closeVoicing(event.chord)) {
            compiled.events.push_back(NoteEvent{start, true, pitch, event.velocity});
            compiled.events.push_back(NoteEvent{end, false, pitch, 0});
        }
    }
    std::sort(compiled.events.begin(), compiled.events.end(), keyLess);
    // Taken from the accumulation so that the last off and `endBeat` can never drift apart.
    compiled.endBeat = beat;
    return compiled;
}

Compiled compile(const Document& doc) {
    Compiled compiled = compileRange(doc, 0, static_cast<int>(doc.events.size()));
    // The accumulated value is bit-identical to `doc.endBeat()`, which performs the same additions.
    assert(compiled.endBeat == doc.endBeat());
    return compiled;
}

Compiled compileSection(const Document& doc, int sectionIndex) {
    const int size = static_cast<int>(doc.events.size());
    if (doc.sections.empty()) {
        assert(sectionIndex == 0);
        return compileRange(doc, 0, size);
    }
    assert(sectionIndex >= 0 && static_cast<std::size_t>(sectionIndex) < doc.sections.size());
    const std::size_t at = static_cast<std::size_t>(sectionIndex);
    const int first = doc.sections[at].firstEventIndex;
    const int end = at + 1 < doc.sections.size() ? doc.sections[at + 1].firstEventIndex : size;
    assert(first >= 0 && first < end && end <= size);
    return compileRange(doc, first, end - first);
}

}  // namespace chordlab
