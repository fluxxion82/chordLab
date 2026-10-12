#include "chordlab/publication.h"

#include <algorithm>
#include <cassert>
#include <utility>

namespace chordlab {

bool fillSlot(const Document& doc, SnapshotSlot& slot, Publication& last) {
    Compiled compiled = compile(doc);
    const bool first = last.generation == 0;
    const bool contentChanged = first || compiled.events != last.compiled.events ||
                                compiled.endBeat != last.compiled.endBeat || doc.progressionLoop != last.loop;
    const bool followChanged = first || doc.followHost != last.followHost;
    if (!contentChanged && !followChanged) {
        return false;
    }
    const bool fits = compiled.events.size() <= slot.events.size();
    assert(fits && "kMaxChords bounds a compiled document to one snapshot slot");
    slot.count = fits ? compiled.events.size() : 0;
    std::copy_n(compiled.events.begin(), slot.count, slot.events.begin());
    slot.endBeat = fits ? compiled.endBeat : 0.0;
    slot.loop = doc.progressionLoop;
    slot.followHost = doc.followHost;
    last.compiled = std::move(compiled);
    last.loop = doc.progressionLoop;
    last.followHost = doc.followHost;
    if (contentChanged) {
        ++last.generation;
    }
    return true;
}

}  // namespace chordlab
