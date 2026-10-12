#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/compiler.h"
#include "chordlab/document.h"
#include "chordlab/limits.h"
#include "chordlab/voicing.h"

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <random>
#include <string_view>
#include <type_traits>
#include <vector>

using namespace chordlab;

namespace {

Chord chord(PitchClass root, std::string_view typeId) {
    return Chord{root, &chordType(typeId)};
}

const Chord kC = chord(0, "maj");
const Chord kAm = chord(9, "min");

using Events = std::vector<NoteEvent>;

/// The bit pattern of a double, so that two beats can be compared for identity rather than through `==`
/// (which also calls -0.0 and +0.0 equal). Adjacent chords must share the exact boundary value.
std::uint64_t bitsOf(double value) {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

/// The compiler's sort key: beat, then offs before ons, then ascending pitch.
bool keyLess(const NoteEvent& a, const NoteEvent& b) {
    if (a.beat != b.beat) {
        return a.beat < b.beat;
    }
    if (a.on != b.on) {
        return !a.on;  // off sorts first
    }
    return a.pitch < b.pitch;
}

/// Events of `compiled` at exactly `beat`, in their compiled order.
Events eventsAt(const Compiled& compiled, double beat) {
    Events at;
    for (const NoteEvent& e : compiled.events) {
        if (e.beat == beat) {
            at.push_back(e);
        }
    }
    return at;
}

/// Pitches of the events in `events`, in order.
std::vector<int> pitchesOf(const Events& events) {
    std::vector<int> pitches;
    for (const NoteEvent& e : events) {
        pitches.push_back(e.pitch);
    }
    return pitches;
}

bool allOn(const Events& events, bool on) {
    return std::all_of(events.begin(), events.end(), [on](const NoteEvent& e) { return e.on == on; });
}

bool allVelocity(const Events& events, int velocity) {
    return std::all_of(events.begin(), events.end(),
                       [velocity](const NoteEvent& e) { return e.velocity == velocity; });
}

/// A document holding the given chords in order, each `lengthBeats` long at velocity `velocity`.
Document documentOf(const std::vector<Chord>& chords, double lengthBeats = 4.0, int velocity = 100) {
    DocumentStore store;
    for (const Chord& c : chords) {
        store.appendChord(c, lengthBeats, velocity);
    }
    return store.doc();
}

}  // namespace

// --- NoteEvent -----------------------------------------------------------------------------

// The scheduler snapshots an array of these with a memcpy and reads it from the audio thread, so the
// type must stay plain data: no constructors, destructors or virtuals, and one access control.
static_assert(std::is_trivially_copyable_v<NoteEvent>);
static_assert(std::is_standard_layout_v<NoteEvent>);

TEST_CASE("NoteEvent: equality compares every field") {
    const NoteEvent a{0.0, true, 48, 100};
    CHECK(a == NoteEvent{0.0, true, 48, 100});
    CHECK(a != NoteEvent{1.0, true, 48, 100});
    CHECK(a != NoteEvent{0.0, false, 48, 100});
    CHECK(a != NoteEvent{0.0, true, 49, 100});
    CHECK(a != NoteEvent{0.0, true, 48, 99});
}

// --- Plan cases ----------------------------------------------------------------------------

TEST_CASE("compile: one C chord of four beats gives three ons at 0 and three offs exactly at 4") {
    const Compiled compiled = compile(documentOf({kC}, 4.0, 100));

    CHECK(compiled.endBeat == 4.0);
    REQUIRE(compiled.events.size() == 6);

    const Events ons = eventsAt(compiled, 0.0);
    REQUIRE(ons.size() == 3);
    CHECK(allOn(ons, true));
    CHECK(allVelocity(ons, 100));
    CHECK(pitchesOf(ons) == std::vector<int>{60, 64, 67});  // ascending pitch

    const Events offs = eventsAt(compiled, 4.0);
    REQUIRE(offs.size() == 3);
    CHECK(allOn(offs, false));
    CHECK(allVelocity(offs, 0));
    CHECK(pitchesOf(offs) == std::vector<int>{60, 64, 67});
}

TEST_CASE("compile: two identical chords back to back retrigger with offs before ons at beat 4") {
    const Compiled compiled = compile(documentOf({kC, kC}, 4.0, 100));

    CHECK(compiled.endBeat == 8.0);
    REQUIRE(compiled.events.size() == 12);

    const Events atFour = eventsAt(compiled, 4.0);
    REQUIRE(atFour.size() == 6);
    CHECK(allOn(Events(atFour.begin(), atFour.begin() + 3), false));
    CHECK(allOn(Events(atFour.begin() + 3, atFour.end()), true));
    CHECK(pitchesOf(atFour) == std::vector<int>{60, 64, 67, 60, 64, 67});

    // The first chord's offs and the second chord's ons carry the very same boundary value.
    for (std::size_t i = 0; i < 3; ++i) {
        CAPTURE(i);
        CHECK(bitsOf(atFour[i].beat) == bitsOf(atFour[i + 3].beat));
    }
}

TEST_CASE("compile: C then Am of four and two beats") {
    DocumentStore store;
    store.appendChord(kC, 4.0, 100);
    store.appendChord(kAm, 2.0, 100);
    const Compiled compiled = compile(store.doc());

    CHECK(compiled.endBeat == 6.0);
    REQUIRE(compiled.events.size() == 12);

    CHECK(pitchesOf(eventsAt(compiled, 0.0)) == std::vector<int>{60, 64, 67});
    CHECK(allOn(eventsAt(compiled, 0.0), true));

    const Events atFour = eventsAt(compiled, 4.0);
    REQUIRE(atFour.size() == 6);
    CHECK(pitchesOf(atFour) == std::vector<int>{60, 64, 67, 69, 72, 76});  // C offs, then Am ons
    CHECK(allOn(Events(atFour.begin(), atFour.begin() + 3), false));
    CHECK(allOn(Events(atFour.begin() + 3, atFour.end()), true));

    const Events atSix = eventsAt(compiled, 6.0);
    CHECK(pitchesOf(atSix) == std::vector<int>{69, 72, 76});
    CHECK(allOn(atSix, false));
}

TEST_CASE("compile: lengths that are not quarter multiples share exact boundaries with the document") {
    // 0.3 + 0.3 + 0.3 is not 0.9 in binary floating point; the compiler must accumulate exactly as the
    // document does so that an off and the next on compare equal, and the last off lands on endBeat.
    const Document doc = documentOf({kC, kC, kC}, 0.3, 100);
    const Compiled compiled = compile(doc);

    double accumulated = 0.0;
    std::vector<double> starts{accumulated};
    for (const ChordEvent& e : doc.events) {
        accumulated += e.lengthBeats;
        starts.push_back(accumulated);
    }
    REQUIRE(starts.size() == 4);

    REQUIRE(compiled.events.size() == 18);
    for (std::size_t i = 1; i < 3; ++i) {
        CAPTURE(i);
        const Events boundary = eventsAt(compiled, starts[i]);
        REQUIRE(boundary.size() == 6);
        CHECK(allOn(Events(boundary.begin(), boundary.begin() + 3), false));
        CHECK(allOn(Events(boundary.begin() + 3, boundary.end()), true));
        for (std::size_t k = 0; k < 3; ++k) {
            CHECK(bitsOf(boundary[k].beat) == bitsOf(boundary[k + 3].beat));
        }
    }

    const Events lastOffs = eventsAt(compiled, starts[3]);
    REQUIRE(lastOffs.size() == 3);
    CHECK(allOn(lastOffs, false));
    CHECK(bitsOf(compiled.endBeat) == bitsOf(starts[3]));
    CHECK(bitsOf(compiled.endBeat) == bitsOf(doc.endBeat()));
    CHECK(bitsOf(compiled.events.back().beat) == bitsOf(compiled.endBeat));
}

TEST_CASE("compile: an empty document has no events and endBeat 0") {
    const Compiled compiled = compile(Document{});
    CHECK(compiled.events.empty());
    CHECK(compiled.endBeat == 0.0);
}

TEST_CASE("compile: each chord's ons carry that chord's velocity; offs carry 0") {
    DocumentStore store;
    store.appendChord(kC, 4.0, 80);
    store.appendChord(kAm, 4.0, 120);
    const Compiled compiled = compile(store.doc());

    for (const NoteEvent& e : compiled.events) {
        CAPTURE(e.beat);
        CAPTURE(e.pitch);
        if (!e.on) {
            CHECK(e.velocity == 0);
        } else if (e.beat == 0.0) {
            CHECK(e.velocity == 80);
        } else {
            CHECK(e.beat == 4.0);
            CHECK(e.velocity == 120);
        }
    }
}

TEST_CASE("compile: exact event order for C then C") {
    const Compiled compiled = compile(documentOf({kC, kC}, 4.0, 100));
    const Events expected{
        {0.0, true, 60, 100},  {0.0, true, 64, 100},  {0.0, true, 67, 100},
        {4.0, false, 60, 0},   {4.0, false, 64, 0},   {4.0, false, 67, 0},
        {4.0, true, 60, 100},  {4.0, true, 64, 100},  {4.0, true, 67, 100},
        {8.0, false, 60, 0},   {8.0, false, 64, 0},   {8.0, false, 67, 0},
    };
    CHECK(compiled.events == expected);
    CHECK(compiled.endBeat == 8.0);
}

// --- Property test ---------------------------------------------------------------------

// The engine is seeded, but what the distributions make of it is implementation-defined, so the test
// checks only invariants that must hold for every trace, never specific values.
TEST_CASE("compile: random documents of up to 20 chords keep the event invariants") {
    std::mt19937 rng(20261011u);
    auto randInt = [&rng](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    auto randReal = [&rng](double lo, double hi) { return std::uniform_real_distribution<double>(lo, hi)(rng); };
    const auto& types = allChordTypes();

    for (int round = 0; round < 100; ++round) {
        CAPTURE(round);
        DocumentStore store;
        const int count = randInt(0, 20);
        for (int i = 0; i < count; ++i) {
            const int root = randInt(0, 11);
            const std::size_t typeIndex = static_cast<std::size_t>(randInt(0, static_cast<int>(types.size()) - 1));
            const bool quarters = randInt(0, 1) == 1;
            const double length = quarters ? 0.25 * randInt(1, 32) : randReal(0.25, 8.0);
            const int velocity = randInt(1, 127);
            store.appendChord(Chord{root, &types[typeIndex]}, length, velocity);
        }
        const Document& doc = store.doc();
        REQUIRE(doc.events.size() == static_cast<std::size_t>(count));

        const Compiled compiled = compile(doc);

        // Two events per voiced pitch.
        std::size_t voicedPitches = 0;
        for (const ChordEvent& e : doc.events) {
            voicedPitches += closeVoicing(e.chord).size();
        }
        CHECK(compiled.events.size() == 2 * voicedPitches);

        // Sorted strictly by (beat, off-before-on, pitch): no two events share a key.
        for (std::size_t i = 1; i < compiled.events.size(); ++i) {
            CAPTURE(i);
            CHECK(keyLess(compiled.events[i - 1], compiled.events[i]));
        }

        // Per pitch, ons and offs alternate and every note is released by the end.
        bool sounding[128] = {};
        for (const NoteEvent& e : compiled.events) {
            CAPTURE(e.beat);
            CAPTURE(e.pitch);
            REQUIRE(e.pitch >= 0);
            REQUIRE(e.pitch < 128);
            CHECK(sounding[e.pitch] != e.on);
            sounding[e.pitch] = e.on;
            if (e.on) {
                CHECK(e.velocity >= kMinVelocity);
                CHECK(e.velocity <= kMaxVelocity);
            } else {
                CHECK(e.velocity == 0);
            }
        }
        CHECK(std::none_of(std::begin(sounding), std::end(sounding), [](bool s) { return s; }));

        // The ons at each accumulated start are exactly that chord's voicing at that chord's velocity.
        double start = 0.0;
        for (const ChordEvent& e : doc.events) {
            CAPTURE(e.id);
            Events ons = eventsAt(compiled, start);
            ons.erase(std::remove_if(ons.begin(), ons.end(), [](const NoteEvent& n) { return !n.on; }), ons.end());
            CHECK(pitchesOf(ons) == closeVoicing(e.chord));
            CHECK(allVelocity(ons, e.velocity));
            start += e.lengthBeats;
        }

        // The last event is an off on endBeat, and endBeat is the document's.
        CHECK(bitsOf(compiled.endBeat) == bitsOf(doc.endBeat()));
        if (!compiled.events.empty()) {
            CHECK_FALSE(compiled.events.back().on);
            CHECK(bitsOf(compiled.events.back().beat) == bitsOf(compiled.endBeat));
        } else {
            CHECK(compiled.endBeat == 0.0);
        }
    }
}

// --- Capacity --------------------------------------------------------------------------

TEST_CASE("compile: kMaxChords chords of the widest dictionary type fit a scheduler snapshot") {
    const ChordType* widest = nullptr;
    for (const ChordType& t : allChordTypes()) {
        if (widest == nullptr || t.intervals.size() > widest->intervals.size()) {
            widest = &t;
        }
    }
    REQUIRE(widest != nullptr);
    CHECK(widest->intervals.size() <= 8);  // the headroom kMaxChords is sized for

    DocumentStore store;
    for (int i = 0; i < kMaxChords; ++i) {
        REQUIRE(store.appendChord(Chord{i % 12, widest}) != kNoEventId);
    }
    const Compiled compiled = compile(store.doc());
    CHECK(compiled.events.size() == static_cast<std::size_t>(kMaxChords) * widest->intervals.size() * 2);
    CHECK(compiled.events.size() <= kMaxSnapshotEvents);
}

// --- Inversions, octaves and auto voice-leading ----------------------------------------

namespace {

using Voicings = std::vector<std::vector<int>>;

const Chord kF = chord(5, "maj");
const Chord kG = chord(7, "maj");

/// Pitches of the ons at `beat`, in compiled (ascending) order.
std::vector<int> onsAt(const Compiled& compiled, double beat) {
    Events ons = eventsAt(compiled, beat);
    ons.erase(std::remove_if(ons.begin(), ons.end(), [](const NoteEvent& n) { return !n.on; }), ons.end());
    return pitchesOf(ons);
}

}  // namespace

TEST_CASE("voicings: each chord is voiced with its own inversion and octave; the defaults are the close voicing") {
    DocumentStore store;
    const int c = store.appendChord(kC);
    const int f = store.appendChord(kF);
    CHECK(voicings(store.doc()) == Voicings{{60, 64, 67}, {65, 69, 72}});
    store.setInversion(f, 2);
    store.setOctave(f, -1);
    CHECK(voicings(store.doc()) == Voicings{{60, 64, 67}, {60, 65, 69}});
    CHECK(voicings(store.doc())[1] == voicing(kF, 2, -1));
    store.setOctave(c, 1);
    CHECK(voicings(store.doc())[0] == voicing(kC, 0, 1));
    CHECK(voicings(Document{}).empty());
}

TEST_CASE("compile: the ons and offs of a chord follow its inversion and octave") {
    DocumentStore store;
    const int c = store.appendChord(kC, 4.0, 100);
    store.setInversion(c, 1);
    store.setOctave(c, -1);
    const Compiled compiled = compile(store.doc());
    REQUIRE(compiled.events.size() == 6);
    CHECK(onsAt(compiled, 0.0) == std::vector<int>{52, 55, 60});
    CHECK(pitchesOf(eventsAt(compiled, 4.0)) == std::vector<int>{52, 55, 60});
    CHECK(allOn(eventsAt(compiled, 4.0), false));
}

TEST_CASE("compile: auto voice-leading takes F to its second inversion under the C: C E G -> C F A") {
    DocumentStore store;
    store.appendChord(kC);
    store.appendChord(kF);
    store.setAutoVoiceLead(true);
    CHECK(voicings(store.doc()) == Voicings{{60, 64, 67}, {60, 65, 69}});
    const Compiled compiled = compile(store.doc());
    CHECK(onsAt(compiled, 0.0) == std::vector<int>{60, 64, 67});
    CHECK(onsAt(compiled, 4.0) == std::vector<int>{60, 65, 69});
    // The stored document is untouched: the choice is made at compile time.
    CHECK(store.doc().events[1].inversion == 0);
    CHECK(store.doc().events[1].octave == 0);
}

TEST_CASE("compile: auto voice-leading takes G to its first inversion under the C: C E G -> B D G") {
    DocumentStore store;
    store.appendChord(kC);
    store.appendChord(kG);
    store.setAutoVoiceLead(true);
    CHECK(voicings(store.doc()) == Voicings{{60, 64, 67}, {59, 62, 67}});
}

TEST_CASE("compile: auto voice-leading takes Am to its first inversion under the C: C E G -> C E A") {
    DocumentStore store;
    store.appendChord(kC);
    store.appendChord(kAm);
    store.setAutoVoiceLead(true);
    CHECK(voicings(store.doc()) == Voicings{{60, 64, 67}, {60, 64, 69}});
}

TEST_CASE("compile: auto voice-leading chains, each chord following the previous chosen voicing") {
    DocumentStore store;
    store.appendChord(kC);
    store.appendChord(kF);
    store.appendChord(kG);
    store.appendChord(kC);
    store.setAutoVoiceLead(true);
    // From C F A, G/B (cost 6) and G/D (cost 6) tie: the smaller inversion wins. From B D G, C in root
    // position costs 3 and nothing beats it.
    CHECK(voicings(store.doc()) == Voicings{{60, 64, 67}, {60, 65, 69}, {59, 62, 67}, {60, 64, 67}});
}

TEST_CASE("compile: a repeated chord keeps its voicing under auto voice-leading") {
    DocumentStore store;
    store.appendChord(kC);
    store.appendChord(kC);
    store.setAutoVoiceLead(true);
    CHECK(voicings(store.doc()) == Voicings{{60, 64, 67}, {60, 64, 67}});
}

TEST_CASE("compile: with auto voice-leading an explicit inversion or octave is honoured as stored") {
    DocumentStore store;
    store.appendChord(kC);
    const int f = store.appendChord(kF);
    store.setAutoVoiceLead(true);
    store.setInversion(f, 1);
    CHECK(voicings(store.doc())[1] == voicing(kF, 1, 0));
    CHECK(voicings(store.doc())[1] == std::vector<int>{69, 72, 77});
    store.setInversion(f, 0);
    store.setOctave(f, -1);
    CHECK(voicings(store.doc())[1] == std::vector<int>{53, 57, 60});
    store.setOctave(f, 0);  // still manual: root position in the default register, not auto's C F A
    CHECK(voicings(store.doc())[1] == std::vector<int>{65, 69, 72});
    store.resetVoicing(f);  // auto decides again
    CHECK(voicings(store.doc())[1] == std::vector<int>{60, 65, 69});
}

TEST_CASE("compile: a chord pinned to root position with setInversion(id, 0) stays there under auto voice-leading") {
    DocumentStore store;
    store.appendChord(kC);
    const int f = store.appendChord(kF);
    store.setAutoVoiceLead(true);
    REQUIRE(voicings(store.doc())[1] == std::vector<int>{60, 65, 69});
    store.setInversion(f, 0);
    CHECK(store.doc().events[1].manualVoicing == true);
    CHECK(voicings(store.doc()) == Voicings{{60, 64, 67}, {65, 69, 72}});
    store.resetVoicing(f);
    CHECK(voicings(store.doc()) == Voicings{{60, 64, 67}, {60, 65, 69}});
    store.setOctave(f, 0);  // pinning through the octave works the same way
    CHECK(voicings(store.doc())[1] == std::vector<int>{65, 69, 72});
}

TEST_CASE("compile: with auto voice-leading off a manual and a reset chord sound the same at the defaults") {
    DocumentStore store;
    store.appendChord(kC);
    const int f = store.appendChord(kF);
    const Voicings stored = voicings(store.doc());
    store.setInversion(f, 0);
    CHECK(voicings(store.doc()) == stored);
    store.resetVoicing(f);
    CHECK(voicings(store.doc()) == stored);
}

TEST_CASE("compile: the first chord keeps its own values under auto voice-leading and the rest follow it") {
    DocumentStore store;
    const int c = store.appendChord(kC);
    store.appendChord(kF);
    store.setAutoVoiceLead(true);
    store.setInversion(c, 2);
    // From G C E, F/A at octave 0 (A C F: cost 3) is the closest.
    CHECK(voicings(store.doc()) == Voicings{{67, 72, 76}, {69, 72, 77}});
}

TEST_CASE("compile: with auto voice-leading off every chord is voiced as stored") {
    DocumentStore store;
    store.appendChord(kC);
    store.appendChord(kF);
    store.appendChord(kG);
    store.setAutoVoiceLead(true);
    store.setAutoVoiceLead(false);
    CHECK(voicings(store.doc()) == Voicings{{60, 64, 67}, {65, 69, 72}, {67, 71, 74}});
}

TEST_CASE("compile: auto voice-leading chooses the cheapest of every inversion at octaves -1..1") {
    // Brute-force oracle over the same candidate set, for a progression of mixed sizes.
    DocumentStore store;
    store.appendChord(chord(2, "m7"));
    store.appendChord(chord(7, "7"));
    store.appendChord(chord(0, "maj9"));
    store.appendChord(chord(9, "min"));
    store.appendChord(chord(5, "5"));
    store.setAutoVoiceLead(true);
    const Voicings chosen = voicings(store.doc());
    REQUIRE(chosen.size() == 5);
    CHECK(chosen[0] == closeVoicing(chord(2, "m7")));
    for (std::size_t i = 1; i < chosen.size(); ++i) {
        CAPTURE(i);
        const Chord c = store.doc().events[i].chord;
        int best = INT_MAX;
        for (int inversion = 0; inversion < inversionCount(c); ++inversion) {
            for (int octave = -1; octave <= 1; ++octave) {
                best = std::min(best, voiceLeadingCost(chosen[i - 1], voicing(c, inversion, octave)));
            }
        }
        CHECK(voiceLeadingCost(chosen[i - 1], chosen[i]) == best);
        CHECK(chosen[i].size() == closeVoicing(c).size());
    }
}

TEST_CASE("compile: random documents with auto voice-leading keep the event invariants") {
    std::mt19937 rng(20261012u);
    auto randInt = [&rng](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    const auto& types = allChordTypes();

    for (int round = 0; round < 50; ++round) {
        CAPTURE(round);
        DocumentStore store;
        store.setAutoVoiceLead(true);
        const int count = randInt(1, 16);
        for (int i = 0; i < count; ++i) {
            const std::size_t typeIndex = static_cast<std::size_t>(randInt(0, static_cast<int>(types.size()) - 1));
            const int id = store.appendChord(Chord{randInt(0, 11), &types[typeIndex]}, 1.0, 100);
            if (randInt(0, 3) == 0) {
                store.setInversion(id, randInt(0, 4));  // some manual voicings among the automatic ones
                store.setOctave(id, randInt(-1, 1));
                if (randInt(0, 2) == 0) {
                    store.resetVoicing(id);
                }
            }
        }
        const Document& doc = store.doc();
        const Voicings chosen = voicings(doc);
        REQUIRE(chosen.size() == doc.events.size());
        const Compiled compiled = compile(doc);

        std::size_t voicedPitches = 0;
        double start = 0.0;
        for (std::size_t i = 0; i < doc.events.size(); ++i) {
            CAPTURE(i);
            const ChordEvent& e = doc.events[i];
            const std::vector<int>& v = chosen[i];
            CHECK(v.size() == closeVoicing(e.chord).size());
            CHECK(std::is_sorted(v.begin(), v.end()));
            CHECK(std::adjacent_find(v.begin(), v.end()) == v.end());
            CHECK(v.front() >= 48);
            CHECK(v.back() <= 108);
            if (e.manualVoicing || i == 0) {
                CHECK(v == voicing(e.chord, e.inversion, e.octave));  // manual chords, and the first chord
            }
            CHECK(onsAt(compiled, start) == v);
            voicedPitches += v.size();
            start += e.lengthBeats;
        }
        CHECK(compiled.events.size() == 2 * voicedPitches);
        for (std::size_t i = 1; i < compiled.events.size(); ++i) {
            CHECK(keyLess(compiled.events[i - 1], compiled.events[i]));
        }
    }
}

// --- compileRange / compileSection -----------------------------------------------------

TEST_CASE("compileRange: a middle section compiles exactly like a document holding only its chords") {
    // Lengths that are not quarter multiples: the range's beats must be accumulated from 0, not taken from
    // the whole document's accumulation and shifted, or the boundaries would differ in the last bit.
    DocumentStore store;
    store.appendChord(kC, 0.3, 100);
    store.appendChord(kAm, 0.3, 90);
    store.appendChord(kC, 0.7, 80);
    store.appendChord(kAm, 1.1, 70);
    store.appendChord(kC, 0.3, 60);
    const Compiled range = compileRange(store.doc(), 1, 3);

    DocumentStore only;
    only.appendChord(kAm, 0.3, 90);
    only.appendChord(kC, 0.7, 80);
    only.appendChord(kAm, 1.1, 70);
    const Compiled whole = compile(only.doc());

    REQUIRE(range.events.size() == whole.events.size());
    REQUIRE(range.events.size() == 18);
    for (std::size_t i = 0; i < range.events.size(); ++i) {
        CAPTURE(i);
        CHECK(range.events[i] == whole.events[i]);
        CHECK(bitsOf(range.events[i].beat) == bitsOf(whole.events[i].beat));
    }
    CHECK(bitsOf(range.endBeat) == bitsOf(whole.endBeat));
    CHECK(range.events.front().beat == 0.0);  // re-based: the range starts at 0, not at 0.3
    CHECK(bitsOf(range.events.back().beat) == bitsOf(range.endBeat));
}

TEST_CASE("compileRange: the whole document is what compile gives; an empty range is empty") {
    DocumentStore store;
    store.appendChord(kC, 4.0, 100);
    store.appendChord(kAm, 2.0, 100);
    const Compiled whole = compile(store.doc());
    const Compiled all = compileRange(store.doc(), 0, 2);
    CHECK(all.events == whole.events);
    CHECK(bitsOf(all.endBeat) == bitsOf(whole.endBeat));

    const Compiled none = compileRange(store.doc(), 1, 0);
    CHECK(none.events.empty());
    CHECK(none.endBeat == 0.0);
    CHECK(compileRange(store.doc(), 2, 0).events.empty());  // one past the last chord, zero chords
    CHECK(compileRange(Document{}, 0, 0).events.empty());
}

TEST_CASE("compileRange: the last chord alone starts at 0 with its own length as endBeat") {
    DocumentStore store;
    store.appendChord(kC, 4.0, 100);
    store.appendChord(kAm, 2.0, 90);
    const Compiled last = compileRange(store.doc(), 1, 1);
    const Events expected{
        {0.0, true, 69, 90}, {0.0, true, 72, 90}, {0.0, true, 76, 90},
        {2.0, false, 69, 0}, {2.0, false, 72, 0}, {2.0, false, 76, 0},
    };
    CHECK(last.events == expected);
    CHECK(last.endBeat == 2.0);
}

TEST_CASE("compileSection: without sections, section 0 is the whole document") {
    DocumentStore store;
    store.appendChord(kC, 4.0, 100);
    store.appendChord(kAm, 2.0, 100);
    const Compiled whole = compile(store.doc());
    const Compiled section = compileSection(store.doc(), 0);
    CHECK(section.events == whole.events);
    CHECK(bitsOf(section.endBeat) == bitsOf(whole.endBeat));
    CHECK(compileSection(Document{}, 0).events.empty());
    CHECK(compileSection(Document{}, 0).endBeat == 0.0);
}

TEST_CASE("compileSection: each section runs to the next section's start, the last to the end") {
    DocumentStore store;
    for (int i = 1; i <= 5; ++i) {
        store.appendChord(i % 2 == 1 ? kC : kAm, static_cast<double>(i), 100);  // lengths 1..5
    }
    store.addSection(0, "A");
    store.addSection(2, "B");
    store.addSection(3, "C");
    const Document& doc = store.doc();

    const Compiled a = compileSection(doc, 0);
    CHECK(a.events == compileRange(doc, 0, 2).events);
    CHECK(a.endBeat == 3.0);
    const Compiled b = compileSection(doc, 1);
    CHECK(b.events == compileRange(doc, 2, 1).events);
    CHECK(b.endBeat == 3.0);
    const Compiled c = compileSection(doc, 2);
    CHECK(c.events == compileRange(doc, 3, 2).events);
    CHECK(c.endBeat == 9.0);
}

TEST_CASE("compileSection: a section added at an inner chord of an unsectioned document gets a leading section") {
    DocumentStore store;
    for (int i = 0; i < 5; ++i) {
        store.appendChord(kC, 1.0, 100);
    }
    store.addSection(1, "A");  // also creates "Section 1" at 0
    store.addSection(3, "B");
    const Document& doc = store.doc();
    REQUIRE(doc.sections.size() == 3);
    CHECK(compileSection(doc, 0).events == compileRange(doc, 0, 1).events);
    CHECK(compileSection(doc, 0).endBeat == 1.0);
    CHECK(compileSection(doc, 1).events == compileRange(doc, 1, 2).events);
    CHECK(compileSection(doc, 1).endBeat == 2.0);
    CHECK(compileSection(doc, 2).events == compileRange(doc, 3, 2).events);
    CHECK(compileSection(doc, 2).endBeat == 2.0);
}

// --- compileRange / compileSection under auto voice-leading ----------------------------------

TEST_CASE("compileRange: a middle section under auto voice-leading is the slice of the whole compile, re-based to 0") {
    // C | F G | C Am, auto on: F follows the C before the section, G follows F, and the second C follows G.
    DocumentStore store;
    store.appendChord(kC, 4.0, 100);
    store.appendChord(kF, 2.0, 90);
    store.appendChord(kG, 2.0, 80);
    store.appendChord(kC, 4.0, 70);
    store.appendChord(kAm, 4.0, 60);
    store.addSection(0, "A");
    store.addSection(1, "B");
    store.addSection(3, "C");
    store.setAutoVoiceLead(true);
    const Document& doc = store.doc();
    const Voicings chosen = voicings(doc);
    REQUIRE(chosen == Voicings{{60, 64, 67}, {60, 65, 69}, {59, 62, 67}, {60, 64, 67}, {60, 64, 69}});

    const Compiled whole = compile(doc);
    const Compiled range = compileRange(doc, 1, 2);
    const Compiled section = compileSection(doc, 1);
    CHECK(section.events == range.events);
    CHECK(bitsOf(section.endBeat) == bitsOf(range.endBeat));

    // The slice of the whole compile that belongs to chords 1..2, shifted back to 0 (quarter-multiple
    // lengths, so the subtraction is exact).
    const double base = doc.startBeatOf(1);
    const double end = doc.startBeatOf(3);
    Events expected;
    for (const NoteEvent& e : whole.events) {
        if (e.beat >= base && e.beat <= end && (e.on ? e.beat < end : e.beat > base)) {
            expected.push_back(NoteEvent{e.beat - base, e.on, e.pitch, e.velocity});
        }
    }
    REQUIRE(expected.size() == 12);
    CHECK(range.events == expected);
    CHECK(range.endBeat == 4.0);
    CHECK(onsAt(range, 0.0) == std::vector<int>{60, 65, 69});  // F as it follows the C before the section
    CHECK(onsAt(range, 2.0) == std::vector<int>{59, 62, 67});

    // Not what the section's chords give on their own: alone, F would be the first chord, in root position.
    DocumentStore alone;
    alone.appendChord(kF, 2.0, 90);
    alone.appendChord(kG, 2.0, 80);
    alone.setAutoVoiceLead(true);
    CHECK(onsAt(compile(alone.doc()), 0.0) == std::vector<int>{65, 69, 72});
    CHECK(compile(alone.doc()).events != range.events);

    // The last section follows the chord before it too.
    CHECK(onsAt(compileSection(doc, 2), 0.0) == std::vector<int>{60, 64, 67});
    CHECK(onsAt(compileSection(doc, 2), 4.0) == std::vector<int>{60, 64, 69});
}

TEST_CASE("compileRange: with auto voice-leading off a range still compiles as a document of its own chords") {
    DocumentStore store;
    store.appendChord(kC, 1.0, 100);
    const int f = store.appendChord(kF, 1.0, 100);
    store.appendChord(kG, 1.0, 100);
    store.setInversion(f, 2);
    store.setOctave(f, -1);
    DocumentStore only;
    only.appendChord(kF, 1.0, 100);
    only.setInversion(only.doc().events[0].id, 2);
    only.setOctave(only.doc().events[0].id, -1);
    only.appendChord(kG, 1.0, 100);
    CHECK(compileRange(store.doc(), 1, 2).events == compile(only.doc()).events);
}
