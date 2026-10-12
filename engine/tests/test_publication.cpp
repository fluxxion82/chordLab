#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/document.h"
#include "chordlab/publication.h"

#include <cstddef>
#include <memory>
#include <vector>

using namespace chordlab;

namespace {

Chord chord(PitchClass root, const char* typeId) {
    return Chord{root, &chordType(typeId)};
}

/// The slot's events as a vector, for comparison with a `Compiled`.
std::vector<NoteEvent> eventsOf(const SnapshotSlot& slot) {
    return std::vector<NoteEvent>(slot.events.begin(), slot.events.begin() + static_cast<std::ptrdiff_t>(slot.count));
}

/// A slot on the heap: 4096 events is a lot of stack.
std::unique_ptr<SnapshotSlot> newSlot() {
    return std::make_unique<SnapshotSlot>();
}

}  // namespace

TEST_CASE("publication: the first fill publishes even an empty document, at generation 1") {
    DocumentStore store;
    Publication last;
    auto slot = newSlot();
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 1);
    CHECK(slot->count == 0);
    CHECK(slot->endBeat == 0.0);
    CHECK(slot->loop == true);
    CHECK(slot->followHost == true);
    CHECK_FALSE(fillSlot(store.doc(), *slot, last));  // nothing changed: nothing to publish
    CHECK(last.generation == 1);
}

TEST_CASE("publication: content changes bump the generation, follow-only changes keep it") {
    DocumentStore store;
    Publication last;
    auto slot = newSlot();
    REQUIRE(fillSlot(store.doc(), *slot, last));

    store.appendChord(chord(0, "maj"), 4.0, 100);
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 2);
    CHECK(slot->count == 6);
    CHECK(slot->endBeat == 4.0);
    const std::vector<NoteEvent> cMajor = eventsOf(*slot);

    // Follow Live off: a new slot, the same content, the same generation (the scheduler sees no change).
    store.setFollowHost(false);
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 2);
    CHECK(slot->followHost == false);
    CHECK(eventsOf(*slot) == cMajor);
    CHECK(slot->endBeat == 4.0);
    CHECK(slot->loop == true);
    CHECK_FALSE(fillSlot(store.doc(), *slot, last));

    // The loop flag and a length are content: they bump.
    store.setProgressionLoop(false);
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 3);
    CHECK(slot->loop == false);
    CHECK(slot->followHost == false);
    store.setLength(1, 2.0);
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 4);
    CHECK(slot->endBeat == 2.0);

    // Pitch-only edits are content too (the same count and endBeat): a different root bumps.
    const int id = store.appendChord(chord(9, "min"), 2.0, 100);
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 5);
    store.removeChord(id);
    store.appendChord(chord(2, "min"), 2.0, 100);
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 6);
}

TEST_CASE("publication: a browsed-key change or a no-op undo publishes nothing") {
    DocumentStore store;
    store.appendChord(chord(0, "maj"), 4.0, 100);
    Publication last;
    auto slot = newSlot();
    REQUIRE(fillSlot(store.doc(), *slot, last));
    store.setBrowsedKey(Key{{'G', 0}, Mode::Ionian});
    CHECK_FALSE(fillSlot(store.doc(), *slot, last));
    CHECK_FALSE(store.redo());
    CHECK_FALSE(fillSlot(store.doc(), *slot, last));
    store.setProjectKey(Key{{'D', 0}, Mode::Dorian});  // the key respells, it does not revoice
    CHECK_FALSE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 1);
}

TEST_CASE("publication: a state load that replaces the progression and turns Follow Live on fills one slot with both") {
    // The case the review raised: with the flag in a separate atomic the audio thread could gate the old
    // progression with the new flag for a block. One slot carries both, so there is no such block.
    DocumentStore store;
    store.appendChord(chord(0, "maj"), 4.0, 100);
    store.setFollowHost(false);
    Publication last;
    auto slot = newSlot();
    REQUIRE(fillSlot(store.doc(), *slot, last));
    REQUIRE(slot->followHost == false);
    const std::vector<NoteEvent> before = eventsOf(*slot);

    Document loaded;
    loaded.events.push_back(ChordEvent{7, chord(9, "min"), 2.0, 90});
    loaded.followHost = true;
    loaded.progressionLoop = false;
    store.replace(loaded, 8);

    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 2);
    CHECK(slot->followHost == true);
    CHECK(slot->loop == false);
    CHECK(slot->endBeat == 2.0);
    CHECK(eventsOf(*slot) == compile(store.doc()).events);
    CHECK(eventsOf(*slot) != before);
    CHECK_FALSE(fillSlot(store.doc(), *slot, last));
}

TEST_CASE("publication: an unchanged document leaves the slot untouched") {
    DocumentStore store;
    store.appendChord(chord(5, "maj7"), 1.0, 100);
    Publication last;
    auto slot = newSlot();
    REQUIRE(fillSlot(store.doc(), *slot, last));
    // Scribble on the slot as the producer's next back slot might hold anything; an unchanged document
    // must not write it.
    slot->count = 99;
    slot->endBeat = -1.0;
    slot->followHost = false;
    CHECK_FALSE(fillSlot(store.doc(), *slot, last));
    CHECK(slot->count == 99);
    CHECK(slot->endBeat == -1.0);
    CHECK(slot->followHost == false);
}

TEST_CASE("publication: inversion, octave and auto voice-leading change the compiled content and bump the generation") {
    DocumentStore store;
    store.appendChord(chord(0, "maj"), 4.0, 100);
    const int f = store.appendChord(chord(5, "maj"), 4.0, 100);
    Publication last;
    auto slot = newSlot();
    REQUIRE(fillSlot(store.doc(), *slot, last));
    REQUIRE(last.generation == 1);
    const std::vector<NoteEvent> rootPositions = eventsOf(*slot);

    store.setAutoVoiceLead(true);  // F moves to C F A
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 2);
    CHECK(eventsOf(*slot) == compile(store.doc()).events);
    CHECK(eventsOf(*slot) != rootPositions);
    CHECK(slot->count == 12);  // the same number of events, different pitches
    CHECK(slot->endBeat == 8.0);
    CHECK_FALSE(fillSlot(store.doc(), *slot, last));

    REQUIRE(store.undo());  // auto off again: back to the root positions
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 3);
    CHECK(eventsOf(*slot) == rootPositions);

    store.setInversion(f, 2);
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 4);
    store.setOctave(f, -1);  // now explicitly C F A
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 5);

    // Auto voice-leading would choose exactly that voicing, and the explicit one is honoured anyway: the
    // compiled content is identical, so nothing is published.
    store.setAutoVoiceLead(true);
    CHECK_FALSE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 5);
    store.setInversion(f, 0);  // an unused inversion under auto: still the same content
    store.setOctave(f, 0);
    CHECK_FALSE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 5);
    store.setAutoVoiceLead(false);  // back to F in root position
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 6);
    CHECK(eventsOf(*slot) == rootPositions);
}
