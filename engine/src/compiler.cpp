#include "chordlab/compiler.h"

#include "chordlab/voicing.h"

#include <algorithm>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <tuple>
#include <utility>

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
    assert(event.inversion >= 0 && event.inversion < inversionCount(event.chord));
    assert(event.octave >= kMinVoicingOctave && event.octave <= kMaxVoicingOctave);
}

/// The voicing of `chord` closest to `previous` over every inversion at octaves -1..1 (at most 15
/// candidates): the one with the smallest key (cost, |octave|, inversion, octave), so that equally cheap
/// candidates go to the smaller |octave|, then the smaller inversion, then the lower octave.
std::vector<int> closestVoicing(const Chord& chord, const std::vector<int>& previous) {
    using Key = std::tuple<int, int, int, int>;
    std::vector<int> best;
    Key bestKey{INT_MAX, INT_MAX, INT_MAX, INT_MAX};
    const int count = inversionCount(chord);
    for (int octave = kMinVoicingOctave; octave <= kMaxVoicingOctave; ++octave) {
        for (int inversion = 0; inversion < count; ++inversion) {
            std::vector<int> candidate = voicing(chord, inversion, octave);
            const Key key{voiceLeadingCost(previous, candidate), std::abs(octave), inversion, octave};
            if (key < bestKey) {
                bestKey = key;
                best = std::move(candidate);
            }
        }
    }
    assert(!best.empty());
    return best;
}

/// The sort key: beat, then offs before ons, then ascending pitch. Over one compiled document the key
/// is unique (ons at a beat come from one chord, whose voicing has no duplicate pitches; likewise offs),
/// so this is a total order and the sorted output is deterministic.
bool keyLess(const NoteEvent& a, const NoteEvent& b) {
    return std::make_tuple(a.beat, a.on ? 1 : 0, a.pitch) < std::make_tuple(b.beat, b.on ? 1 : 0, b.pitch);
}

}  // namespace

std::vector<std::vector<int>> voicings(const Document& doc) {
    std::vector<std::vector<int>> result;
    result.reserve(doc.events.size());
    for (const ChordEvent& event : doc.events) {
        assertValidEvent(event);
        if (doc.autoVoiceLead && !event.manualVoicing && !result.empty()) {
            result.push_back(closestVoicing(event.chord, result.back()));
        } else {
            result.push_back(voicing(event.chord, event.inversion, event.octave));
        }
    }
    return result;
}

Compiled compile(const Document& doc) {
    Compiled compiled;
    const std::vector<std::vector<int>> chosen = voicings(doc);
    // Start beats are accumulated in a single pass, in the same order and with the same additions as
    // `Document::endBeat`. The running value is reused as the off beat of one chord and the on beat of
    // the next, so adjacent events compare equal exactly rather than merely closely.
    double beat = 0.0;
    for (std::size_t i = 0; i < doc.events.size(); ++i) {
        const ChordEvent& event = doc.events[i];
        const double start = beat;
        beat += event.lengthBeats;
        const double end = beat;
        for (const int pitch : chosen[i]) {
            compiled.events.push_back(NoteEvent{start, true, pitch, event.velocity});
            compiled.events.push_back(NoteEvent{end, false, pitch, 0});
        }
    }
    std::sort(compiled.events.begin(), compiled.events.end(), keyLess);
    // The accumulated value is bit-identical to `doc.endBeat()`, which performs the same additions; it is
    // taken from the accumulation so that the last off and `endBeat` can never drift apart.
    compiled.endBeat = beat;
    assert(compiled.endBeat == doc.endBeat());
    return compiled;
}

}  // namespace chordlab
