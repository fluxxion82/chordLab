#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/document.h"
#include "chordlab/limits.h"
#include "chordlab/link.h"
#include "chordlab/publication.h"
#include "chordlab/tracks.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
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

/// A slot on the heap: kMaxSnapshotEvents events (~288 KB) is a lot of stack.
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
    CHECK(slot->midiThru == true);
    CHECK_FALSE(fillSlot(store.doc(), *slot, last));  // nothing changed: nothing to publish
    CHECK(last.generation == 1);
}

TEST_CASE("publication: a midiThru-only change publishes a fresh slot under the same generation") {
    DocumentStore store;
    store.appendChord(chord(0, "maj"), 4.0, 100);
    Publication last;
    auto slot = newSlot();
    REQUIRE(fillSlot(store.doc(), *slot, last));
    REQUIRE(last.generation == 1);
    REQUIRE(slot->midiThru == true);
    const std::vector<NoteEvent> cMajor = eventsOf(*slot);

    store.setMidiThru(false);
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 1);
    CHECK(slot->midiThru == false);
    CHECK(last.midiThru == false);
    CHECK(eventsOf(*slot) == cMajor);
    CHECK(slot->endBeat == 4.0);
    CHECK(slot->followHost == true);
    CHECK_FALSE(fillSlot(store.doc(), *slot, last));

    store.setMidiThru(true);
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 1);
    CHECK(slot->midiThru == true);

    // A browsed-key-only change still publishes nothing, whatever midiThru is.
    store.setMidiThru(false);
    REQUIRE(fillSlot(store.doc(), *slot, last));
    store.setBrowsedKey(Key{{'G', 0}, Mode::Ionian});
    CHECK_FALSE(fillSlot(store.doc(), *slot, last));
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

    // Auto voice-leading would choose exactly that voicing, and the manual one is honoured anyway: the
    // compiled content is identical, so nothing is published.
    store.setAutoVoiceLead(true);
    CHECK_FALSE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 5);
    store.setInversion(f, 0);  // manual root position under auto: C F A -> F A C
    store.setOctave(f, 0);
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 6);
    CHECK(eventsOf(*slot) == rootPositions);
    store.resetVoicing(f);  // auto decides again: C F A
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 7);
    CHECK(eventsOf(*slot) != rootPositions);
    store.setAutoVoiceLead(false);  // back to F in root position
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 8);
    CHECK(eventsOf(*slot) == rootPositions);
}

TEST_CASE("publication: the manual flag alone publishes only when it changes the compiled content") {
    DocumentStore store;
    store.appendChord(chord(0, "maj"), 4.0, 100);
    const int f = store.appendChord(chord(5, "maj"), 4.0, 100);
    Publication last;
    auto slot = newSlot();
    REQUIRE(fillSlot(store.doc(), *slot, last));
    REQUIRE(last.generation == 1);

    // Auto off: a manual root position compiles exactly like the default one. The flag flips, the
    // document differs, nothing is published.
    store.setInversion(f, 0);
    REQUIRE(store.doc().events[1].manualVoicing == true);
    CHECK_FALSE(fillSlot(store.doc(), *slot, last));
    store.resetVoicing(f);
    REQUIRE(store.doc().events[1].manualVoicing == false);
    CHECK_FALSE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 1);

    // Auto on: the same flag flip moves F between C F A (chosen) and F A C (manual): content, published.
    store.setAutoVoiceLead(true);
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 2);
    const std::vector<NoteEvent> chosen = eventsOf(*slot);
    store.setInversion(f, 0);
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 3);
    CHECK(eventsOf(*slot) != chosen);
    CHECK(eventsOf(*slot) == compile(store.doc()).events);
    store.resetVoicing(f);
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 4);
    CHECK(eventsOf(*slot) == chosen);
    CHECK_FALSE(fillSlot(store.doc(), *slot, last));
}

// --- Feel (feel plan, Task 4) --------------------------------------------------------------------------------

TEST_CASE("publication: fillSlot publishes the performed events") {
    DocumentStore store;
    store.appendChord(chord(0, "maj"), 4.0, 100);
    store.setFeel(Feel{Rhythm::Pulse});
    Publication last;
    auto slot = newSlot();
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(slot->count == 24);  // four hits of three notes, ons and offs
    CHECK(eventsOf(*slot) == compile(store.doc()).events);
    CHECK(slot->events[3] == NoteEvent{0.9, false, 60, 0});  // the first gated off, 864 / 960
    CHECK(slot->events[6] == NoteEvent{1.0, true, 60, 90});
    CHECK(slot->endBeat == 4.0);
}

TEST_CASE("publication: a feel change bumps the generation only when it changes the events") {
    DocumentStore store;
    store.appendChord(chord(0, "maj"), 4.0, 100);
    store.appendChord(chord(9, "min"), 2.0, 90);
    Publication last;
    auto slot = newSlot();
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 1);

    // The strum speed alone, with the strum off, is no feel: same block events, nothing published.
    store.setFeel(Feel{Rhythm::Block, Strum::Off, StrumSpeed::Loose, Humanise::Off});
    CHECK_FALSE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 1);

    store.setFeel(Feel{Rhythm::Arpeggio, Strum::Down, StrumSpeed::Medium, Humanise::Off});
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 2);
    const std::vector<NoteEvent> arp = eventsOf(*slot);

    // Arpeggios ignore the strum: neither its speed nor its direction changes an event.
    store.setFeel(Feel{Rhythm::Arpeggio, Strum::Down, StrumSpeed::Loose, Humanise::Off});
    CHECK_FALSE(fillSlot(store.doc(), *slot, last));
    store.setFeel(Feel{Rhythm::Arpeggio, Strum::Up, StrumSpeed::Tight, Humanise::Off});
    CHECK_FALSE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 2);
    CHECK(eventsOf(*slot) == arp);

    // Recompiling the same document publishes nothing.
    CHECK_FALSE(fillSlot(store.doc(), *slot, last));

    // A rhythm or a humanise that moves notes is content.
    store.setFeel(Feel{Rhythm::Arpeggio, Strum::Up, StrumSpeed::Tight, Humanise::Off, ArpPattern::UpDown});
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 3);
    store.setFeel(Feel{Rhythm::Arpeggio, Strum::Up, StrumSpeed::Tight, Humanise::Light, ArpPattern::UpDown});
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 4);
    CHECK(eventsOf(*slot) == compile(store.doc()).events);

    // Back to block chords: today's events again.
    store.setFeel(Feel{});
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 5);
    CHECK(slot->count == 12);
}

TEST_CASE("publication: a pattern change publishes under the arpeggio and not under pulse") {
    DocumentStore store;
    store.appendChord(chord(0, "maj"), 4.0, 100);
    Publication last;
    auto slot = newSlot();
    REQUIRE(fillSlot(store.doc(), *slot, last));

    Feel feel{Rhythm::Pulse};
    store.setFeel(feel);
    REQUIRE(fillSlot(store.doc(), *slot, last));
    const std::uint32_t pulseGeneration = last.generation;
    const std::vector<NoteEvent> pulse = eventsOf(*slot);

    // The arpeggio fields are not heard under another rhythm: same events, nothing published.
    feel.pattern = ArpPattern::Random;
    store.setFeel(feel);
    CHECK_FALSE(fillSlot(store.doc(), *slot, last));
    feel.rate = ArpRate::Half;
    store.setFeel(feel);
    CHECK_FALSE(fillSlot(store.doc(), *slot, last));
    feel.range = ArpRange::Two;
    store.setFeel(feel);
    CHECK_FALSE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == pulseGeneration);
    CHECK(eventsOf(*slot) == pulse);

    // Switching to the arpeggio is content.
    feel = Feel{Rhythm::Arpeggio};
    store.setFeel(feel);
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == pulseGeneration + 1);

    // Each of the pattern, the rate and the range changes the events and bumps the generation once.
    feel.pattern = ArpPattern::Down;
    store.setFeel(feel);
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == pulseGeneration + 2);
    feel.rate = ArpRate::Pulse;
    store.setFeel(feel);
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == pulseGeneration + 3);
    feel.range = ArpRange::Two;
    store.setFeel(feel);
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == pulseGeneration + 4);
    CHECK(eventsOf(*slot) == compile(store.doc()).events);

    // The same feel again publishes nothing.
    store.setFeel(feel);
    CHECK_FALSE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == pulseGeneration + 4);
}

TEST_CASE("publication: an over-budget feel publishes block chords") {
    DocumentStore store;
    for (int i = 0; i < 64; ++i) store.appendChord(chord(0, "9"), 4.0, 100);
    Publication last;
    auto slot = newSlot();
    REQUIRE(fillSlot(store.doc(), *slot, last));
    const std::vector<NoteEvent> block = eventsOf(*slot);
    store.setFeel(Feel{Rhythm::Subdivided});  // 5120 performed events: over 4096
    CHECK_FALSE(fillSlot(store.doc(), *slot, last));  // the same block events: nothing to publish
    CHECK(eventsOf(*slot) == block);
    CHECK(compile(store.doc()).feelLimited);
}

TEST_CASE("publication: a performed non-tick-aligned document publishes the compiled period as endBeat") {
    // Three chords of 0.2506 beat: boundaries 241, 481 and 722 ticks, so the period is 722 / 960 beat, while
    // the authored sum is 0.7518 (721.7 ticks).
    DocumentStore store;
    for (int i = 0; i < 3; ++i) store.appendChord(chord(0, "maj"), 0.2506, 100);
    Publication last;
    auto slot = newSlot();
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(slot->endBeat == store.doc().endBeat());  // block chords: the authored sum
    CHECK(last.generation == 1);

    store.setFeel(Feel{Rhythm::Pulse});
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 2);
    CHECK(slot->endBeat == 722.0 / 960.0);
    CHECK(slot->endBeat != store.doc().endBeat());
    CHECK(slot->endBeat == compile(store.doc()).endBeat);
    // Pulse: one hit per chord, gated to 9/10 of its length: ons at 0, 241 and 481, offs at 216, 457 and 697.
    CHECK(slot->count == 18);
    CHECK(slot->events[0] == NoteEvent{0.0, true, 60, 100});
    CHECK(slot->events[3] == NoteEvent{216.0 / 960.0, false, 60, 0});
    CHECK(slot->events[6] == NoteEvent{241.0 / 960.0, true, 60, 100});
    CHECK(slot->events[17] == NoteEvent{697.0 / 960.0, false, 67, 0});
    CHECK(slot->events[17].beat < slot->endBeat);

    // Back to block chords: the authored sum again, published as a change.
    store.setFeel(Feel{});
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(slot->endBeat == store.doc().endBeat());
}

TEST_CASE("publication: an over-budget non-tick-aligned document publishes the authored sum as endBeat") {
    DocumentStore store;
    for (int i = 0; i < 64; ++i) store.appendChord(chord(0, "9"), 4.0, 100);
    store.appendChord(chord(0, "9"), 4.0004, 100);
    store.setFeel(Feel{Rhythm::Subdivided});
    REQUIRE(compile(store.doc()).feelLimited);
    Publication last;
    auto slot = newSlot();
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(slot->endBeat == store.doc().endBeat());
    CHECK(slot->endBeat != 260.0);
    CHECK(slot->count == 650);
}

// --- tracks (design D8): fillSlot publishes compilePlayback -----------------------------------------------------

namespace {

/// Adds a track holding `specs` and returns its id.
int addTrackWith(DocumentStore& store, const std::vector<NoteSpec>& specs) {
    const int id = store.addTrack(TrackRole::Bass);
    REQUIRE(id != kNoTrackId);
    std::string why;
    REQUIRE_MESSAGE(store.insertNotes(id, specs, &why).size() == specs.size(), why);
    return id;
}

}  // namespace

TEST_CASE("publication: fillSlot publishes the audible lanes merged, compilePlayback's events") {
    DocumentStore store;
    store.appendChord(chord(0, "maj"), 4.0, 100);
    store.appendChord(chord(9, "min"), 4.0, 100);
    addTrackWith(store, {NoteSpec{36, 0, 960, 90}, NoteSpec{60, 960, 960, 110}});  // the 60 cuts C's
    const Compiled playback = compilePlayback(store.doc());
    REQUIRE(playback.events != compile(store.doc()).events);
    Publication last;
    auto slot = newSlot();
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(eventsOf(*slot) == playback.events);
    CHECK(slot->endBeat == playback.endBeat);
    CHECK(last.compiled.events == playback.events);
}

TEST_CASE("publication: the generation ignores a note selection, and moves with mute, solo and note edits that sound") {
    DocumentStore store;
    store.appendChord(chord(0, "maj"), 4.0, 100);
    store.appendChord(chord(5, "maj"), 4.0, 100);
    const int bass = addTrackWith(store, {NoteSpec{36, 0, 960}, NoteSpec{41, 3840, 960}});
    const int lead = addTrackWith(store, {NoteSpec{72, 480, 480}});
    Publication last;
    auto slot = newSlot();
    REQUIRE(fillSlot(store.doc(), *slot, last));
    REQUIRE(last.generation == 1);
    const std::vector<int> bassIds = [&] {
        std::vector<int> ids;
        for (const TrackNote& n : store.doc().tracks.at(0).notes) ids.push_back(n.id);
        return ids;
    }();

    // A note selection is store state, not content: nothing to publish.
    store.setNoteSelection(bass, bassIds);
    REQUIRE(store.noteSelection().ids == bassIds);
    CHECK_FALSE(fillSlot(store.doc(), *slot, last));
    store.setNoteSelection(kNoTrackId, {});
    CHECK_FALSE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 1);

    // A mute toggle changes what sounds: the generation moves, and moves back on with unmute.
    REQUIRE(store.setTrackMute(bass, true));
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 2);
    CHECK(eventsOf(*slot) == compilePlayback(store.doc()).events);

    // An edit of a muted track's note changes nothing that sounds: no publication.
    REQUIRE(store.updateNotes(bass, {NoteEdit{bassIds[0], std::nullopt, std::nullopt, std::nullopt, 50}}));
    CHECK_FALSE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 2);

    REQUIRE(store.setTrackMute(bass, false));
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 3);

    // A note edit of an audible track moves it: velocity, then pitch.
    REQUIRE(store.updateNotes(bass, {NoteEdit{bassIds[0], std::nullopt, std::nullopt, std::nullopt, 70}}));
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 4);
    REQUIRE(store.updateNotes(lead, {NoteEdit{store.doc().tracks.at(1).notes.at(0).id, 74, std::nullopt, std::nullopt,
                                              std::nullopt}}));
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 5);

    // Soloing the lead silences the chords and the bass; muting the chords then changes nothing that sounds.
    REQUIRE(store.setTrackSolo(lead, true));
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 6);
    CHECK(eventsOf(*slot) == std::vector<NoteEvent>{{0.5, true, 74, 100}, {1.0, false, 74, 0}});
    REQUIRE(store.setChordsMute(true));
    CHECK_FALSE(fillSlot(store.doc(), *slot, last));
    CHECK(last.generation == 6);
}

TEST_CASE("publication: a real document at the caps fills a slot with exactly kMaxSnapshotEvents events") {
    // 128 one-bar sevenths played Pulse: 128 x 4 hits x 4 notes x 2 = kMaxChordEvents. Then kMaxTracks tracks of
    // kMaxNotesPerTrack short notes spread evenly over the 512 beats, every track on its own pitch below the chords',
    // so nothing merges: kMaxTracks x kMaxNotesPerTrack x 2 more events, the whole slot.
    DocumentStore store;
    for (int i = 0; i < 128; ++i) store.appendChord(chord(i % 12, "7"), 4.0, 100);
    store.setFeel(Feel{Rhythm::Pulse});
    const std::int64_t end = chordBoundaryTicks(store.doc()).back();
    const std::int64_t spacing = end / kMaxNotesPerTrack;
    REQUIRE(kMaxTracks * kFeelTicksPerBeat / spacing <= kMaxTrackOnsetsPerBeat);
    REQUIRE(kMaxTracks <= kMaxTrackPolyphony);
    for (int t = 0; t < kMaxTracks; ++t) {
        std::vector<NoteSpec> specs;
        for (int k = 0; k < kMaxNotesPerTrack; ++k) specs.push_back(NoteSpec{24 + 2 * t, k * spacing, kMinTrackNoteTicks, 100});
        addTrackWith(store, specs);
    }
    REQUIRE(compile(store.doc()).events.size() == kMaxChordEvents);
    Publication last;
    auto slot = newSlot();
    REQUIRE(fillSlot(store.doc(), *slot, last));
    CHECK(slot->count == kMaxSnapshotEvents);
    CHECK(eventsOf(*slot) == compilePlayback(store.doc()).events);
    CHECK(slot->events[kMaxSnapshotEvents - 1].beat <= slot->endBeat);
}

// --- per-track instruments (M1): the leader's own fill -----------------------------------------------------------

namespace {

LaneSelection allExcept(std::vector<LaneKey> lanes) {
    return LaneSelection{LaneSelection::Mode::AllExcept, std::move(lanes)};
}

/// Two chords, track A (36 on beat 0 and a 60 on beat 1 that cuts C's 60 in the every-lane result) and track B
/// (38 on beat 2).
struct Leader {
    DocumentStore store;
    int a = 0;
    int b = 0;
    Leader() {
        store.appendChord(chord(0, "maj"), 4.0, 100);
        store.appendChord(chord(9, "min"), 4.0, 100);
        a = addTrackWith(store, {NoteSpec{36, 0, 960, 90}, NoteSpec{60, 960, 960, 110}});
        b = addTrackWith(store, {NoteSpec{38, 1920, 480, 100}});
    }
};

}  // namespace

TEST_CASE("publication: fillSlot with a selection publishes compilePlaybackFor of that selection") {
    const Leader d;
    const Document& doc = d.store.doc();
    const std::vector<LaneSelection> selections{
        LaneSelection{},
        allExcept({trackLane(d.a)}),
        allExcept({trackLane(d.b)}),
        allExcept({trackLane(d.a), trackLane(d.b)}),
        allExcept({chordsLane()}),
        LaneSelection{LaneSelection::Mode::Only, {trackLane(d.b)}},
    };
    for (std::size_t i = 0; i < selections.size(); ++i) {
        CAPTURE(i);
        const Compiled expected = compilePlaybackFor(doc, selections[i]);
        if (i > 0) REQUIRE(expected.events != compilePlayback(doc).events);
        Publication last;
        auto slot = newSlot();
        REQUIRE(fillSlot(doc, selections[i], *slot, last));
        CHECK(last.generation == 1);
        CHECK(eventsOf(*slot) == expected.events);
        CHECK(last.compiled.events == expected.events);
        CHECK(slot->endBeat == expected.endBeat);
        CHECK(slot->endBeat == 8.0);
        CHECK(slot->loop == true);
        CHECK(slot->followHost == true);
        CHECK(slot->midiThru == true);
        CHECK_FALSE(fillSlot(doc, selections[i], *slot, last));
    }
}

TEST_CASE("publication: a change of the own selection alone is content when it changes what plays") {
    Leader d;
    Publication last;
    auto slot = newSlot();
    REQUIRE(fillSlot(d.store.doc(), LaneSelection{}, *slot, last));
    REQUIRE(last.generation == 1);
    const std::vector<NoteEvent> every = eventsOf(*slot);

    // A becomes followed: the leader stops playing it.
    REQUIRE(fillSlot(d.store.doc(), allExcept({trackLane(d.a)}), *slot, last));
    CHECK(last.generation == 2);
    CHECK(eventsOf(*slot) == compilePlaybackFor(d.store.doc(), allExcept({trackLane(d.a)})).events);
    CHECK(eventsOf(*slot) != every);
    CHECK_FALSE(fillSlot(d.store.doc(), allExcept({trackLane(d.a)}), *slot, last));

    // B instead.
    REQUIRE(fillSlot(d.store.doc(), allExcept({trackLane(d.b)}), *slot, last));
    CHECK(last.generation == 3);
    CHECK(eventsOf(*slot) == compilePlaybackFor(d.store.doc(), allExcept({trackLane(d.b)})).events);

    // Follow Live alone under a selection: the same generation.
    d.store.setFollowHost(false);
    REQUIRE(fillSlot(d.store.doc(), allExcept({trackLane(d.b)}), *slot, last));
    CHECK(last.generation == 3);
    CHECK(slot->followHost == false);

    // A muted lane plays nowhere: following it or not changes nothing that sounds, so nothing is published.
    REQUIRE(d.store.setTrackMute(d.a, true));
    REQUIRE(fillSlot(d.store.doc(), allExcept({trackLane(d.a)}), *slot, last));
    CHECK(last.generation == 4);
    CHECK_FALSE(fillSlot(d.store.doc(), LaneSelection{}, *slot, last));
    CHECK_FALSE(fillSlot(d.store.doc(), *slot, last));  // the three-argument fill: the same every-lane content
    CHECK(last.generation == 4);
}

TEST_CASE("publication: own generations count up from 1, below kLinkGenerationBase where link generations start") {
    // A leader's own buffer and a follower's link buffer feed one scheduler in turn (link.h), so one generation must
    // never stand for two contents: own generations count 1, 2, ... (one per content change: 2^31 - 1 changes before
    // reaching the base), link generations start at kLinkGenerationBase + 1.
    const Leader d;
    Publication last;
    auto slot = newSlot();
    for (std::uint32_t i = 1; i <= 1000; ++i) {
        const LaneSelection own = (i % 2 == 0) ? allExcept({trackLane(d.a)}) : LaneSelection{};
        REQUIRE(fillSlot(d.store.doc(), own, *slot, last));
        CHECK(last.generation == i);
        CHECK(last.generation < kLinkGenerationBase);
    }
}
