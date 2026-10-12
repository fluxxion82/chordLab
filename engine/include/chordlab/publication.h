#pragma once

#include "chordlab/compiler.h"
#include "chordlab/document.h"
#include "chordlab/limits.h"
#include "chordlab/note_event.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace chordlab {

/// One slot of the plug-in's snapshot handoff (a `TripleBuffer<SnapshotSlot>`): the compiled progression
/// as the scheduler plays it, plus `followHost`, the plug-in's gate on the transport, which rides along
/// so that the audio thread picks up a progression and the flag that belongs with it in one acquire. A
/// state load that replaces the progression and turns Follow Live on can therefore never make the audio
/// thread chase the old progression with the new flag. Preallocated: nothing here allocates.
///
/// The generation (`Publication::generation`, what `TripleBuffer::publish` is called with) covers the
/// scheduler's content only: `events`, `count`, `endBeat`, `loop`. `followHost` sits outside it. A
/// follow-only change publishes a fresh slot with identical content under the same generation, which the
/// scheduler adopts as unchanged (no flush, playback goes on) while the plug-in reads the new flag from
/// the slot. That keeps the scheduler's rule "same generation, same content" exact.
struct SnapshotSlot {
    std::array<NoteEvent, kMaxSnapshotEvents> events;
    std::size_t count = 0;
    double endBeat = 0.0;
    bool loop = true;
    bool followHost = true;
};

/// The producer's memory of its last publication, for change detection: the compiled content as
/// published, the two flags, and the generation it went out with (0 before the first).
struct Publication {
    Compiled compiled;
    bool loop = true;
    bool followHost = true;
    std::uint32_t generation = 0;
};

/// Compiles `doc` and, when anything a slot carries differs from `last` (or nothing was published yet),
/// fills `slot` with it, records it in `last` and returns true: the caller then publishes `slot` under
/// `last.generation`. The generation is bumped when the content (events, endBeat, loop) changed and kept
/// when only `followHost` did. Returns false, touching neither `slot` nor `last`, when the document
/// compiles to exactly what was last published (a browsed-key change, a no-op undo, a rejected command).
/// `kMaxChords` bounds a compiled document to the slot (document.h); a document that did not fit would be
/// adopted as empty by the scheduler anyway, so it is published as empty, not truncated (and asserts).
/// Allocates (compile): main thread only.
bool fillSlot(const Document& doc, SnapshotSlot& slot, Publication& last);

}  // namespace chordlab
