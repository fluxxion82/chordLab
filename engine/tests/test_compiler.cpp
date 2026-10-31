#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/compiler.h"
#include "chordlab/document.h"
#include "chordlab/limits.h"
#include "chordlab/voicing.h"

#include <algorithm>
#include <climits>
#include <cmath>
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
            CHECK(v.back() <= 60 + e.chord.type->intervals.back() + 22 + 12);  // voicing.h's bound at octave +1
            CHECK(v.back() <= 127);
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

// --- Feel (Task 4) ------------------------------------------------------------------------------------------

namespace {

/// `compileRange` exactly as it was before feels existed (commit 50dac2e), copied verbatim apart from the
/// name: the reference that the block path must keep matching bit for bit.
Compiled preFeelCompileRange(const Document& doc, int firstEvent, int count) {
    const std::vector<std::vector<int>> chosen = voicings(doc);
    Compiled compiled;
    double beat = 0.0;
    const std::size_t first = static_cast<std::size_t>(firstEvent);
    const std::size_t last = first + static_cast<std::size_t>(count);
    for (std::size_t i = first; i < last; ++i) {
        const ChordEvent& event = doc.events[i];
        const double start = beat;
        beat += event.lengthBeats;
        const double end = beat;
        for (const int pitch : chosen[i]) {
            compiled.events.push_back(NoteEvent{start, true, pitch, event.velocity});
            compiled.events.push_back(NoteEvent{end, false, pitch, 0});
        }
    }
    std::sort(compiled.events.begin(), compiled.events.end(), [](const NoteEvent& a, const NoteEvent& b) {
        return std::make_tuple(a.beat, a.on ? 1 : 0, a.pitch) < std::make_tuple(b.beat, b.on ? 1 : 0, b.pitch);
    });
    compiled.endBeat = beat;
    return compiled;
}

/// Bitwise equality of two compiled results (beats compared by bit pattern).
void checkIdentical(const Compiled& a, const Compiled& b) {
    REQUIRE(a.events.size() == b.events.size());
    for (std::size_t i = 0; i < a.events.size(); ++i) {
        CAPTURE(i);
        CHECK(bitsOf(a.events[i].beat) == bitsOf(b.events[i].beat));
        CHECK(a.events[i].on == b.events[i].on);
        CHECK(a.events[i].pitch == b.events[i].pitch);
        CHECK(a.events[i].velocity == b.events[i].velocity);
    }
    CHECK(bitsOf(a.endBeat) == bitsOf(b.endBeat));
}

const Chord kF7 = chord(5, "maj");
const Chord kG7 = chord(7, "7");

/// A compiled event in feel ticks (every performed beat is tick / 960 exactly).
struct TickEv {
    int tick;
    bool on;
    int pitch;
    int velocity;
};

bool operator==(const TickEv& a, const TickEv& b) {
    return a.tick == b.tick && a.on == b.on && a.pitch == b.pitch && a.velocity == b.velocity;
}

std::vector<TickEv> ticksOf(const Compiled& compiled) {
    std::vector<TickEv> out;
    for (const NoteEvent& e : compiled.events) {
        const int tick = static_cast<int>(std::lround(e.beat * kFeelTicksPerBeat));
        REQUIRE(e.beat == tick / 960.0);  // exactly on the grid
        out.push_back(TickEv{tick, e.on, e.pitch, e.velocity});
    }
    return out;
}

}  // namespace

TEST_CASE("feel: with Feel{} compile is the pre-feel compiler bit for bit on random documents and sections") {
    std::mt19937 rng(20261030u);
    auto randInt = [&rng](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
    auto randReal = [&rng](double lo, double hi) { return std::uniform_real_distribution<double>(lo, hi)(rng); };
    const auto& types = allChordTypes();
    for (int round = 0; round < 200; ++round) {
        CAPTURE(round);
        DocumentStore store;
        const int count = randInt(0, 20);
        for (int i = 0; i < count; ++i) {
            const bool quarters = randInt(0, 1) == 1;
            const int id = store.appendChord(
                Chord{randInt(0, 11), &types[static_cast<std::size_t>(randInt(0, static_cast<int>(types.size()) - 1))]},
                quarters ? 0.25 * randInt(1, 32) : randReal(0.25, 8.0), randInt(1, 127));
            if (randInt(0, 3) == 0) store.setInversion(id, randInt(0, 2));
        }
        store.setAutoVoiceLead(randInt(0, 1) == 1);
        // The strum speed alone is not a feel: it too keeps the block path.
        if (randInt(0, 1) == 1) store.setFeel(Feel{Rhythm::Block, Strum::Off, StrumSpeed::Loose, Humanise::Off});
        if (count > 2) store.addSection(randInt(1, count - 1), "S");
        const Document& doc = store.doc();
        const Compiled compiled = compile(doc);
        checkIdentical(compiled, preFeelCompileRange(doc, 0, count));
        CHECK_FALSE(compiled.feelLimited);
        const int sections = doc.sections.empty() ? 1 : static_cast<int>(doc.sections.size());
        for (int s = 0; s < sections && count > 0; ++s) {
            const int first = doc.sections.empty() ? 0 : doc.sections[static_cast<std::size_t>(s)].firstEventIndex;
            const int end = doc.sections.empty() || s + 1 == sections
                                ? count
                                : doc.sections[static_cast<std::size_t>(s) + 1].firstEventIndex;
            checkIdentical(compileSection(doc, s), preFeelCompileRange(doc, first, end - first));
        }
    }
}

TEST_CASE("feel: Feel{} C-Am-F-G7 with auto voice-leading off and on is the pre-feel compile") {
    for (bool autoOn : {false, true}) {
        CAPTURE(autoOn);
        DocumentStore store;
        store.appendChord(kC, 4.0, 100);
        store.appendChord(kAm, 4.0, 100);
        store.appendChord(kF7, 4.0, 100);
        store.appendChord(kG7, 4.0, 100);
        store.setAutoVoiceLead(autoOn);
        checkIdentical(compile(store.doc()), preFeelCompileRange(store.doc(), 0, 4));
    }
    DocumentStore store;
    store.appendChord(kC, 4.0, 100);
    store.appendChord(kAm, 4.0, 100);
    store.appendChord(kF7, 4.0, 100);
    store.appendChord(kG7, 4.0, 100);
    const Events expected{
        {0.0, true, 60, 100},   {0.0, true, 64, 100},   {0.0, true, 67, 100},   {4.0, false, 60, 0},
        {4.0, false, 64, 0},    {4.0, false, 67, 0},    {4.0, true, 69, 100},   {4.0, true, 72, 100},
        {4.0, true, 76, 100},   {8.0, false, 69, 0},    {8.0, false, 72, 0},    {8.0, false, 76, 0},
        {8.0, true, 65, 100},   {8.0, true, 69, 100},   {8.0, true, 72, 100},   {12.0, false, 65, 0},
        {12.0, false, 69, 0},   {12.0, false, 72, 0},   {12.0, true, 67, 100},  {12.0, true, 71, 100},
        {12.0, true, 74, 100},  {12.0, true, 77, 100},  {16.0, false, 67, 0},   {16.0, false, 71, 0},
        {16.0, false, 74, 0},   {16.0, false, 77, 0},
    };
    CHECK(compile(store.doc()).events == expected);
}

TEST_CASE("feel: compile plays the performance at tick / 960 with endBeat from the last chord's end tick") {
    DocumentStore store;
    store.appendChord(kC, 4.0, 100);
    store.setFeel(Feel{Rhythm::Pulse});
    const Compiled compiled = compile(store.doc());
    CHECK_FALSE(compiled.feelLimited);
    CHECK(compiled.endBeat == 4.0);
    const std::vector<TickEv> expected{
        {0, true, 60, 100},   {0, true, 64, 100},   {0, true, 67, 100},   {864, false, 60, 0},  {864, false, 64, 0},
        {864, false, 67, 0},  {960, true, 60, 90},  {960, true, 64, 90},  {960, true, 67, 90},  {1824, false, 60, 0},
        {1824, false, 64, 0}, {1824, false, 67, 0}, {1920, true, 60, 90}, {1920, true, 64, 90}, {1920, true, 67, 90},
        {2784, false, 60, 0}, {2784, false, 64, 0}, {2784, false, 67, 0}, {2880, true, 60, 90}, {2880, true, 64, 90},
        {2880, true, 67, 90}, {3744, false, 60, 0}, {3744, false, 64, 0}, {3744, false, 67, 0},
    };
    CHECK(ticksOf(compiled) == expected);
    CHECK(compiled.events[3].beat == 0.9);  // 864 / 960

    // Off-grid lengths: endBeat is the end tick over 960 (0.3 + 0.3 + 0.7 = 1248 ticks = 1.3 beats).
    DocumentStore odd;
    odd.appendChord(kC, 0.3, 100);
    odd.appendChord(kAm, 0.3, 100);
    odd.appendChord(kC, 0.7, 100);
    odd.setFeel(Feel{Rhythm::Pulse});
    CHECK(compile(odd.doc()).endBeat == 1248 / 960.0);
    CHECK(compileRange(odd.doc(), 1, 2).endBeat == 960 / 960.0);  // [288, 1248)
    CHECK(compileRange(odd.doc(), 1, 0).events.empty());
    CHECK(compileRange(odd.doc(), 1, 0).endBeat == 0.0);

    // An empty document with a feel compiles to nothing.
    Document empty;
    empty.feel = Feel{Rhythm::Subdivided, Strum::Up, StrumSpeed::Loose, Humanise::Heavy};
    CHECK(compile(empty).events.empty());
    CHECK(compile(empty).endBeat == 0.0);
}

TEST_CASE("feel: three sections slice the whole performance, humanise and strum at both boundaries") {
    // C | Am F | G, one bar each, ids 1..4, velocity 100, Block, strum up (highest first, 20 apart), humanise
    // medium (first hit late by 0..10, velocity +-8). From the hash reference (test_feel.cpp):
    //   late(key(id,0,0,1), 10) = 5, 7, 10, 7 for ids 1, 2, 3, 4
    //   off(key(id,0,n,3), 8) for voicing index n = 0, 1, 2: id 1: -3, 5, -8; id 2: -6, 6, -8; id 3: -3, 6, -8;
    //   id 4: -7, 7, 0
    // Whole: C 67@5 v92, 64@25 v105, 60@45 v97, offs 3840 (3845 cut to the chord's end); Am 76@3847 v92,
    // 72@3867 v106, 69@3887 v94, offs 7680; F 72@7690 v92, 69@7710 v106, 65@7730 v97, offs 11520; G 74@11527
    // v100, 71@11547 v107, 67@11567 v93, offs 15360.
    DocumentStore store;
    store.appendChord(kC, 4.0, 100);
    store.appendChord(kAm, 4.0, 100);
    store.appendChord(kF7, 4.0, 100);
    store.appendChord(chord(7, "maj"), 4.0, 100);
    store.addSection(0, "A");
    store.addSection(1, "B");
    store.addSection(3, "C");
    store.setFeel(Feel{Rhythm::Block, Strum::Up, StrumSpeed::Medium, Humanise::Medium});
    const Document& doc = store.doc();
    REQUIRE(doc.events[3].id == 4);

    const std::vector<TickEv> whole{
        {5, true, 67, 92},       {25, true, 64, 105},     {45, true, 60, 97},      {3840, false, 60, 0},
        {3840, false, 64, 0},    {3840, false, 67, 0},    {3847, true, 76, 92},    {3867, true, 72, 106},
        {3887, true, 69, 94},    {7680, false, 69, 0},    {7680, false, 72, 0},    {7680, false, 76, 0},
        {7690, true, 72, 92},    {7710, true, 69, 106},   {7730, true, 65, 97},    {11520, false, 65, 0},
        {11520, false, 69, 0},   {11520, false, 72, 0},   {11527, true, 74, 100},  {11547, true, 71, 107},
        {11567, true, 67, 93},   {15360, false, 67, 0},   {15360, false, 71, 0},   {15360, false, 74, 0},
    };
    CHECK(ticksOf(compile(doc)) == whole);
    CHECK(compile(doc).endBeat == 16.0);

    const std::vector<TickEv> a{
        {5, true, 67, 92}, {25, true, 64, 105}, {45, true, 60, 97}, {3840, false, 60, 0}, {3840, false, 64, 0},
        {3840, false, 67, 0},
    };
    const std::vector<TickEv> b{
        {7, true, 76, 92},     {27, true, 72, 106},   {47, true, 69, 94},    {3840, false, 69, 0},
        {3840, false, 72, 0},  {3840, false, 76, 0},  {3850, true, 72, 92},  {3870, true, 69, 106},
        {3890, true, 65, 97},  {7680, false, 65, 0},  {7680, false, 69, 0},  {7680, false, 72, 0},
    };
    const std::vector<TickEv> c{
        {7, true, 74, 100}, {27, true, 71, 107}, {47, true, 67, 93}, {3840, false, 67, 0}, {3840, false, 71, 0},
        {3840, false, 74, 0},
    };
    CHECK(ticksOf(compileSection(doc, 0)) == a);
    CHECK(compileSection(doc, 0).endBeat == 4.0);
    CHECK(ticksOf(compileSection(doc, 1)) == b);
    CHECK(compileSection(doc, 1).endBeat == 8.0);
    CHECK(ticksOf(compileSection(doc, 2)) == c);
    CHECK(compileSection(doc, 2).endBeat == 4.0);
}

TEST_CASE("feel: every range is the whole performance's events of its chords, rebased") {
    std::mt19937 rng(77);
    const char* types[] = {"maj", "min", "7", "m9", "sus4"};
    for (int round = 0; round < 60; ++round) {
        CAPTURE(round);
        DocumentStore store;
        const int count = 1 + static_cast<int>(rng() % 8);
        for (int i = 0; i < count; ++i) {
            store.appendChord(chord(static_cast<int>(rng() % 12), types[rng() % 5]),
                              rng() % 2 ? 0.25 * (1 + rng() % 16) : 0.25 + static_cast<double>(rng() % 50) / 13.0,
                              1 + static_cast<int>(rng() % 127));
        }
        store.setAutoVoiceLead(rng() % 2 == 0);
        store.setFeel(Feel{static_cast<Rhythm>(rng() % 7), static_cast<Strum>(rng() % 4),
                           static_cast<StrumSpeed>(rng() % 3), static_cast<Humanise>(rng() % 4)});
        const Meter meters[] = {{4, 4}, {3, 4}, {6, 8}, {7, 8}};
        store.setMeter(meters[rng() % 4]);
        const Document& doc = store.doc();
        if (!feelPerforms(doc.feel)) continue;
        const Performance p = performDocument(doc);
        REQUIRE_FALSE(p.feelLimited);
        for (int first = 0; first <= count; ++first) {
            for (int n = 0; first + n <= count; ++n) {
                const int base = p.chordTicks[static_cast<std::size_t>(first)];
                std::vector<TickEv> expected;
                for (const PerformedEvent& e : p.events) {
                    if (e.chord >= first && e.chord < first + n) {
                        expected.push_back(TickEv{e.tick - base, e.on, e.pitch, e.velocity});
                    }
                }
                const Compiled range = compileRange(doc, first, n);
                CHECK(ticksOf(range) == expected);
                CHECK(range.endBeat == (p.chordTicks[static_cast<std::size_t>(first + n)] - base) / 960.0);
            }
        }
    }
}

TEST_CASE("feel: an over-budget document compiles as block chords everywhere, sections included") {
    // 64 one-bar ninths with Subdivided: 5120 performed events, over 4096. A one-chord section at the end would
    // fit alone (80 events) but exports block chords too, because the decision is the whole document's.
    DocumentStore store;
    for (int i = 0; i < 64; ++i) store.appendChord(chord(0, "9"), 4.0, 100);
    store.appendChord(kC, 4.0, 100);
    store.addSection(0, "Dense");
    store.addSection(64, "Light");
    store.setFeel(Feel{Rhythm::Subdivided, Strum::Down, StrumSpeed::Medium, Humanise::Light});
    const Document& doc = store.doc();
    REQUIRE(performDocument(doc).feelLimited);

    Document block = doc;
    block.feel = Feel{};
    const Compiled compiled = compile(doc);
    CHECK(compiled.feelLimited);
    checkIdentical(compiled, compile(block));
    const Compiled light = compileSection(doc, 1);
    CHECK(light.feelLimited);
    checkIdentical(light, compileSection(block, 1));
    CHECK(light.events.size() == 6);
    CHECK(compileRange(doc, 64, 1).feelLimited);
    CHECK_FALSE(compile(block).feelLimited);

    // A beat too dense (eight-note chords every 120 ticks in 7/8) falls back the same way.
    static const ChordType cluster{"cluster8", "cl8", {0, 1, 2, 3, 4, 5, 6, 7}, Family::Other};
    DocumentStore dense;
    dense.appendChord(Chord{0, &cluster}, 0.375, 100);
    for (int i = 0; i < 15; ++i) dense.appendChord(Chord{0, &cluster}, 0.25, 100);
    dense.appendChord(kC, 4.0, 100);
    dense.addSection(16, "Tail");
    dense.setMeter(Meter{7, 8});
    dense.setFeel(Feel{Rhythm::Subdivided});
    Document denseBlock = dense.doc();
    denseBlock.feel = Feel{};
    CHECK(compile(dense.doc()).feelLimited);
    checkIdentical(compile(dense.doc()), compile(denseBlock));
    CHECK(compileSection(dense.doc(), 1).feelLimited);
    checkIdentical(compileSection(dense.doc(), 1), compileSection(denseBlock, 1));
}
