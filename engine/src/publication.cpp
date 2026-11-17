#include "chordlab/publication.h"

#include <algorithm>
#include <cassert>
#include <utility>

namespace chordlab {

bool fillSlot(const Document& doc, SnapshotSlot& slot, Publication& last) {
    return fillSlot(doc, LaneSelection{}, slot, last);
}

bool fillSlot(const Document& doc, const LaneSelection& own, SnapshotSlot& slot, Publication& last) {
    Compiled compiled = compilePlaybackFor(doc, own);
    const bool first = last.generation == 0;
    const bool contentChanged = first || compiled.events != last.compiled.events ||
                                compiled.endBeat != last.compiled.endBeat || doc.progressionLoop != last.loop;
    const bool followChanged = first || doc.followHost != last.followHost;
    const bool thruChanged = first || doc.midiThru != last.midiThru;
    if (!contentChanged && !followChanged && !thruChanged) {
        return false;
    }
    const bool fits = compiled.events.size() <= slot.events.size();
    assert(fits && "the chord budget and the track caps bound compilePlaybackFor to one snapshot slot");
    slot.count = fits ? compiled.events.size() : 0;
    std::copy_n(compiled.events.begin(), slot.count, slot.events.begin());
    slot.endBeat = fits ? compiled.endBeat : 0.0;
    slot.loop = doc.progressionLoop;
    slot.followHost = doc.followHost;
    slot.midiThru = doc.midiThru;
    last.compiled = std::move(compiled);
    last.loop = doc.progressionLoop;
    last.followHost = doc.followHost;
    last.midiThru = doc.midiThru;
    if (contentChanged) {
        ++last.generation;
    }
    return true;
}

}  // namespace chordlab
