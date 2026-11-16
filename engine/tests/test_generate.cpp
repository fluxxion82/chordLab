#include "doctest.h"

#include "chordlab/generate.h"

#include "chordlab/chord.h"
#include "chordlab/compiler.h"
#include "chordlab/document.h"
#include "chordlab/feel.h"
#include "chordlab/meter.h"
#include "chordlab/tracks.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

using namespace chordlab;

namespace {

/// A pinned note of D16, `(startTick, lengthTicks, pitch, velocity)`, the start absolute (the chord's boundary plus
/// the note's offset), so a chord that starts at 0 reads exactly as D16 writes it.
struct Pin {
    int start;
    int length;
    int pitch;
    int velocity;
};

bool operator==(const Pin& a, const Pin& b) {
    return a.start == b.start && a.length == b.length && a.pitch == b.pitch && a.velocity == b.velocity;
}

std::ostream& operator<<(std::ostream& os, const Pin& p) {
    return os << "(" << p.start << ", " << p.length << ", " << p.pitch << ", " << p.velocity << ")";
}

std::ostream& operator<<(std::ostream& os, const std::vector<Pin>& pins) {
    os << "[";
    for (std::size_t i = 0; i < pins.size(); ++i) {
        os << (i == 0 ? "" : ", ") << pins[i];
    }
    return os << "]";
}

using Pins = std::vector<Pin>;
using Ints = std::vector<int>;

Chord chordOf(PitchClass root, const char* typeId) {
    return Chord{root, &chordType(typeId)};
}

const Chord kC = chordOf(0, "maj");
const Chord kG = chordOf(7, "maj");

/// A chord event of `beats` beats; `inversion` given makes it manual.
ChordEvent ev(int id, Chord chord, double beats, int velocity = 100, int inversion = 0) {
    ChordEvent e{id, chord, beats, velocity};
    e.inversion = inversion;
    e.manualVoicing = inversion != 0;
    return e;
}

/// A chord of exactly `ticks` ticks (the boundaries round the cumulative beats once, D3).
ChordEvent evTicks(int id, Chord chord, int ticks) {
    return ev(id, chord, ticks / static_cast<double>(kFeelTicksPerBeat));
}

Document docOf(std::vector<ChordEvent> events, Meter meter = {4, 4}, Feel feel = {}, bool loop = false) {
    Document doc;
    doc.events = std::move(events);
    doc.meter = meter;
    doc.feel = feel;
    doc.progressionLoop = loop;
    return doc;
}

/// An arpeggio feel: strum off, humanise off.
Feel arp(ArpPattern pattern, ArpRate rate = ArpRate::Subdivision, ArpRange range = ArpRange::One) {
    Feel feel;
    feel.rhythm = Rhythm::Arpeggio;
    feel.pattern = pattern;
    feel.rate = rate;
    feel.range = range;
    return feel;
}

/// `generateBass` as pins, each start made absolute through its anchor's boundary.
Pins generated(const Document& doc, BassPattern pattern, const std::vector<int>& scope = {}) {
    const ChordTimeline timeline = chordTimeline(doc);
    Pins pins;
    for (const GeneratedNote& n : generateBass(doc, pattern, scope)) {
        const int index = timeline.indexOf(n.anchorEventId);
        REQUIRE(index >= 0);
        pins.push_back(Pin{timeline.boundaries[static_cast<std::size_t>(index)] + n.offsetTick, n.lengthTicks, n.pitch,
                           n.velocity});
    }
    return pins;
}

Ints pitchesOf(const Pins& pins) {
    Ints out;
    for (const Pin& p : pins) {
        out.push_back(p.pitch);
    }
    return out;
}

Ints startsOf(const Pins& pins) {
    Ints out;
    for (const Pin& p : pins) {
        out.push_back(p.start);
    }
    return out;
}

Ints lengthsOf(const Pins& pins) {
    Ints out;
    for (const Pin& p : pins) {
        out.push_back(p.length);
    }
    return out;
}

Ints velocitiesOf(const Pins& pins) {
    Ints out;
    for (const Pin& p : pins) {
        out.push_back(p.velocity);
    }
    return out;
}

/// `count` values `value`.
Ints repeated(int count, int value) {
    return Ints(static_cast<std::size_t>(count), value);
}

/// A test-only chord type without a fifth (a dominant seventh, no fifth), not in the dictionary.
const ChordType kNoFifth{"test-no-fifth", "7no5", {0, 4, 10}, Family::Major};

}  // namespace

// --- names and helpers -------------------------------------------------------------------

TEST_CASE("bassPatternName / bassPatternFromName: one table, six names, exact spelling") {
    const char* const names[] = {"roots", "rootPulse", "rootFifth", "octaves", "followFeel", "walking"};
    static_assert(sizeof(names) / sizeof(names[0]) == kBassPatternCount, "one name per pattern");
    for (int i = 0; i < kBassPatternCount; ++i) {
        const auto pattern = static_cast<BassPattern>(i);
        CHECK(std::string(bassPatternName(pattern)) == names[i]);
        CHECK(bassPatternFromName(names[i]) == pattern);
    }
    CHECK(bassPatternFromName("Roots") == std::nullopt);
    CHECK(bassPatternFromName(" roots") == std::nullopt);
    CHECK(bassPatternFromName("") == std::nullopt);
    CHECK(bassPatternFromName("root-fifth") == std::nullopt);
}

TEST_CASE("placeBass: the lowest pitch of the class in 28..39") {
    CHECK(kBassLowest == 28);
    CHECK(kBassHighest == 39);
    CHECK(kWalkingHighest == 52);
    CHECK(kMinBassHitTicks == kMinTrackNoteTicks);
    CHECK(placeBass(60) == 36);
    CHECK(placeBass(64) == 28);
    CHECK(placeBass(67) == 31);
    CHECK(placeBass(63) == 39);
    CHECK(placeBass(28) == 28);
    CHECK(placeBass(39) == 39);
    CHECK(placeBass(40) == 28);
    CHECK(placeBass(27) == 39);
    CHECK(placeBass(0) == 36);
    CHECK(placeBass(127) == 31);
    CHECK(placeBass(-1) == 35);
}

TEST_CASE("structuralFifth: perfect when the type has one, else the altered fifth by structure; none without") {
    CHECK(structuralFifth(chordType("maj")) == 7);
    CHECK(structuralFifth(chordType("min")) == 7);
    CHECK(structuralFifth(chordType("5")) == 7);
    CHECK(structuralFifth(chordType("sus2")) == 7);
    CHECK(structuralFifth(chordType("sus4")) == 7);
    CHECK(structuralFifth(chordType("dim")) == 6);
    CHECK(structuralFifth(chordType("m7b5")) == 6);
    CHECK(structuralFifth(chordType("dim7")) == 6);
    CHECK(structuralFifth(chordType("aug")) == 8);
    CHECK(structuralFifth(chordType("augMaj7")) == 8);
    CHECK(structuralFifth(chordType("7#11")) == 7);  // its #11 is a tritone, but not its fifth
    CHECK(structuralFifth(chordType("maj7#11")) == 7);
    CHECK(structuralFifth(kNoFifth) == std::nullopt);
    for (const ChordType& type : allChordTypes()) {
        CAPTURE(type.id);
        CHECK(structuralFifth(type).has_value());  // every dictionary type has one
    }
}

TEST_CASE("bassAccent: chord or bar start 100, other pulse 90, other subdivision 80") {
    CHECK(bassAccent({4, 4}, 720, 720) == 100);
    CHECK(bassAccent({4, 4}, 0, 3840) == 100);
    CHECK(bassAccent({4, 4}, 0, 960) == 90);
    CHECK(bassAccent({4, 4}, 0, 480) == 80);
    CHECK(bassAccent({6, 8}, 0, 1440) == 90);
    CHECK(bassAccent({6, 8}, 0, 480) == 80);
    CHECK(bassAccent({6, 8}, 0, 2880) == 100);
    CHECK(bassAccent({7, 8}, 0, 480) == 90);
    CHECK(bassAccent({7, 8}, 0, 240) == 80);
    CHECK(bassAccent({7, 8}, 0, 3360) == 100);
}

// --- the common hit builder --------------------------------------------------------------

TEST_CASE("bassGridHits: D16's pinned spans [959, 1920) and [0, 961) keep the chord-start attack") {
    // [959, 1920): hits 959 and 960; the first would last 0, so the hit after it goes; 961 * 9 / 10 = 864.
    CHECK(bassGridHits({4, 4}, 959, 1920, 960, false) == std::vector<BassHit>{{959, 864, 100}});
    // [0, 961): hits 0 and 960; the second would last 0 and goes.
    CHECK(bassGridHits({4, 4}, 0, 961, 960, false) == std::vector<BassHit>{{0, 864, 100}});
    // Full gap: the same removals, lengths to the end.
    CHECK(bassGridHits({4, 4}, 959, 1920, 960, true) == std::vector<BassHit>{{959, 961, 100}});
}

TEST_CASE("bassGridHits: gates, accents, full gaps; removal repeats with the gaps recomputed") {
    CHECK(bassGridHits({4, 4}, 0, 3840, 960, false) ==
          std::vector<BassHit>{{0, 864, 100}, {960, 864, 90}, {1920, 864, 90}, {2880, 864, 90}});
    CHECK(bassGridHits({4, 4}, 0, 3840, 960, true) ==
          std::vector<BassHit>{{0, 960, 100}, {960, 960, 90}, {1920, 960, 90}, {2880, 960, 90}});
    CHECK(bassGridHits({4, 4}, 720, 1920, 480, false) ==
          std::vector<BassHit>{{720, 216, 100}, {960, 432, 90}, {1440, 432, 80}});
    // A short middle hit: [0, 990) on the 4/4 subdivision is 0, 480, 960; 960 lasts 27 and goes, 480 then lasts 459.
    CHECK(bassGridHits({4, 4}, 0, 990, 480, false) == std::vector<BassHit>{{0, 432, 100}, {480, 459, 80}});
    // Gated 9/10 the cut is at 34 ticks: [0, 994) keeps 960 at 30, [0, 993) loses it at 29.
    CHECK(bassGridHits({4, 4}, 0, 994, 480, false).size() == 3);
    CHECK(bassGridHits({4, 4}, 0, 993, 480, false).size() == 2);
    // Full gap: the cut is at 30 ticks.
    CHECK(bassGridHits({4, 4}, 0, 990, 480, true).size() == 3);
    CHECK(bassGridHits({4, 4}, 0, 989, 480, true).size() == 2);
    // Repeated removal: [935, 1440) on the 7/8 half step (120): 935, 960, 1080, ...; 935 lasts 22, so 960 goes, then
    // 935 lasts 130 and stays.
    const std::vector<BassHit> hits = bassGridHits({7, 8}, 935, 1440, 120, false);
    REQUIRE(hits.size() == 4);
    CHECK(hits[0] == BassHit{935, 130, 100});
    CHECK(hits[1].start == 1080);
    CHECK(bassGridHits({4, 4}, 100, 100, 960, false).empty());
}

// --- Roots ------------------------------------------------------------------------------

TEST_CASE("Roots: one note over the whole span at B, velocity 100, whatever the chord velocity") {
    CHECK(generated(docOf({ev(1, kC, 4.0, 60)}), BassPattern::Roots) == Pins{{0, 3840, 36, 100}});
    CHECK(generated(docOf({ev(1, kC, 4.0), ev(2, kC, 4.0, 100, 1)}), BassPattern::Roots) ==
          Pins{{0, 3840, 36, 100}, {3840, 3840, 28, 100}});  // C/E sounds E
    // An off-grid span: from its start to its end.
    CHECK(generated(docOf({evTicks(1, kC, 959), evTicks(2, kG, 961)}), BassPattern::Roots, {2}) ==
          Pins{{959, 961, 31, 100}});
}

// --- Root pulse -------------------------------------------------------------------------

TEST_CASE("Root pulse: pulse hits, gated, B, accents; D16's [959, 1920) and [0, 961) through a document") {
    CHECK(generated(docOf({ev(1, kC, 4.0)}), BassPattern::RootPulse) ==
          Pins{{0, 864, 36, 100}, {960, 864, 36, 90}, {1920, 864, 36, 90}, {2880, 864, 36, 90}});
    const Document offGrid = docOf({evTicks(1, kC, 959), evTicks(2, kC, 961)});
    REQUIRE(chordBoundaryTicks(offGrid) == std::vector<std::int32_t>{0, 959, 1920});
    CHECK(generated(offGrid, BassPattern::RootPulse, {2}) == Pins{{959, 864, 36, 100}});
    const Document shortTail = docOf({evTicks(1, kC, 961)});
    CHECK(generated(shortTail, BassPattern::RootPulse) == Pins{{0, 864, 36, 100}});
    // A bar start inside a chord is accented as one.
    CHECK(velocitiesOf(generated(docOf({ev(1, kC, 8.0)}), BassPattern::RootPulse)) == Ints{100, 90, 90, 90, 100, 90, 90, 90});
}

// --- Root–fifth -------------------------------------------------------------------------

TEST_CASE("Root-fifth: D16's seven chords on a 4-beat 4/4 chord") {
    const auto check = [](Chord chord, int inversion, const Ints& expected) {
        const Pins pins = generated(docOf({ev(1, chord, 4.0, 100, inversion)}), BassPattern::RootFifth);
        CHECK(startsOf(pins) == Ints{0, 960, 1920, 2880});
        CHECK(lengthsOf(pins) == repeated(4, 864));
        CHECK(velocitiesOf(pins) == Ints{100, 90, 90, 90});
        CHECK(pitchesOf(pins) == expected);
    };
    SUBCASE("C") { check(kC, 0, {36, 43, 36, 43}); }
    SUBCASE("Cdim") { check(chordOf(0, "dim"), 0, {36, 42, 36, 42}); }
    SUBCASE("Caug") { check(chordOf(0, "aug"), 0, {36, 44, 36, 44}); }
    SUBCASE("C7#11 takes its perfect fifth") { check(chordOf(0, "7#11"), 0, {36, 43, 36, 43}); }
    SUBCASE("C/E: the bass and the fifth above it") { check(kC, 1, {28, 31, 28, 31}); }
    SUBCASE("C/G: the fifth in the bass repeats B") { check(kC, 2, {31, 31, 31, 31}); }
    SUBCASE("a type without a fifth repeats B") { check(Chord{0, &kNoFifth}, 0, {36, 36, 36, 36}); }
}

TEST_CASE("Root-fifth: 6/8, and the alternation restarts at each chord") {
    const Pins pins = generated(docOf({ev(1, kC, 3.0)}, {6, 8}), BassPattern::RootFifth);
    CHECK(pins == Pins{{0, 1296, 36, 100}, {1440, 1296, 43, 90}});
    // Three beats of C then G: C's third hit is even (B); G restarts on its own B.
    CHECK(pitchesOf(generated(docOf({ev(1, kC, 3.0), ev(2, kG, 2.0)}), BassPattern::RootFifth)) ==
          Ints{36, 43, 36, 31, 38});
}

// --- Octaves ----------------------------------------------------------------------------

TEST_CASE("Octaves: subdivision hits alternating B and B + 12 in 4/4, 6/8 and 7/8") {
    SUBCASE("4/4 [0, 3840)") {
        const Pins pins = generated(docOf({ev(1, kC, 4.0)}), BassPattern::Octaves);
        CHECK(startsOf(pins) == Ints{0, 480, 960, 1440, 1920, 2400, 2880, 3360});
        CHECK(lengthsOf(pins) == repeated(8, 432));
        CHECK(pitchesOf(pins) == Ints{36, 48, 36, 48, 36, 48, 36, 48});
        CHECK(velocitiesOf(pins) == Ints{100, 80, 90, 80, 90, 80, 90, 80});
    }
    SUBCASE("6/8 [0, 2880)") {
        const Pins pins = generated(docOf({ev(1, kC, 3.0)}, {6, 8}), BassPattern::Octaves);
        CHECK(startsOf(pins) == Ints{0, 480, 960, 1440, 1920, 2400});
        CHECK(lengthsOf(pins) == repeated(6, 432));
        CHECK(pitchesOf(pins) == Ints{36, 48, 36, 48, 36, 48});
        CHECK(velocitiesOf(pins) == Ints{100, 80, 80, 90, 80, 80});
    }
    SUBCASE("7/8 [0, 3360)") {
        const Pins pins = generated(docOf({ev(1, kC, 3.5)}, {7, 8}), BassPattern::Octaves);
        Ints starts;
        Ints pitches;
        Ints velocities;
        for (int i = 0; i < 14; ++i) {
            starts.push_back(240 * i);
            pitches.push_back(i % 2 == 0 ? 36 : 48);
            velocities.push_back(i == 0 ? 100 : (i % 2 == 0 ? 90 : 80));
        }
        CHECK(startsOf(pins) == starts);
        CHECK(lengthsOf(pins) == repeated(14, 216));
        CHECK(pitchesOf(pins) == pitches);
        CHECK(velocitiesOf(pins) == velocities);
    }
    SUBCASE("off-grid [720, 1920)") {
        const Document doc = docOf({ev(1, kG, 0.75), ev(2, kC, 1.25)});
        CHECK(generated(doc, BassPattern::Octaves, {2}) == Pins{{720, 216, 36, 100}, {960, 432, 48, 90}, {1440, 432, 36, 80}});
    }
}

// --- Follow feel ------------------------------------------------------------------------

TEST_CASE("Follow feel: the arpeggio's bass-class hits at B, raw starts, ends and velocities") {
    SUBCASE("C triad 4/4 [0, 1920), half rate, Up") {
        const Document doc = docOf({ev(1, kC, 2.0)}, {4, 4}, arp(ArpPattern::Up, ArpRate::Half));
        CHECK(generated(doc, BassPattern::FollowFeel) == Pins{{0, 216, 36, 100}, {720, 216, 36, 80}, {1440, 216, 36, 80}});
    }
    SUBCASE("Down") {
        const Document doc = docOf({ev(1, kC, 2.0)}, {4, 4}, arp(ArpPattern::Down, ArpRate::Half));
        CHECK(generated(doc, BassPattern::FollowFeel) == Pins{{480, 216, 36, 80}, {1200, 216, 36, 80}});
    }
    SUBCASE("Down [0, 480): no bass-class hit, no note") {
        const Document doc = docOf({ev(1, kC, 0.5)}, {4, 4}, arp(ArpPattern::Down, ArpRate::Half));
        CHECK(generated(doc, BassPattern::FollowFeel).empty());
    }
    SUBCASE("Root First on C/E [64, 67, 72]: the list's order counts, B is E") {
        const Document doc = docOf({ev(1, kC, 2.0, 100, 1)}, {4, 4}, arp(ArpPattern::RootFirst, ArpRate::Half));
        REQUIRE(voicings(doc).front() == Ints{64, 67, 72});
        CHECK(generated(doc, BassPattern::FollowFeel) == Pins{{240, 216, 28, 80}, {960, 216, 28, 90}, {1680, 216, 28, 80}});
    }
    SUBCASE("two-octave Down [0, 1440) keeps both bass-class hits, the upper octave's included") {
        const Document doc = docOf({ev(1, kC, 1.5)}, {4, 4}, arp(ArpPattern::Down, ArpRate::Half, ArpRange::Two));
        CHECK(generated(doc, BassPattern::FollowFeel) == Pins{{480, 216, 36, 80}, {1200, 216, 36, 80}});
    }
    SUBCASE("Random follows the chord's id: id 1 plays 1 0 2 0 2 0 1 2") {
        const Document doc = docOf({ev(1, kC, 4.0)}, {4, 4}, arp(ArpPattern::Random));
        CHECK(generated(doc, BassPattern::FollowFeel) == Pins{{480, 432, 36, 80}, {1440, 432, 36, 80}, {2400, 432, 36, 80}});
        // Another id, another order: id 2 plays 0 2 0 2 1 2 1 0, so the C sounds on hits 0, 2 and 7.
        const Document other = docOf({ev(2, kC, 4.0)}, {4, 4}, arp(ArpPattern::Random));
        const Pins expected{{0, 432, 36, 100}, {960, 432, 36, 90}, {3360, 432, 36, 80}};
        CHECK(generated(other, BassPattern::FollowFeel) == expected);
        int previous = -1;
        for (int k = 0; k < 8; ++k) {  // exactly the hits whose arpRandomIndex is 0
            previous = arpRandomIndex(2, k, 3, previous);
            CHECK((previous == 0) == (k == 0 || k == 2 || k == 7));
        }
    }
}

TEST_CASE("Follow feel: low-then-upper, a pulse with a short first hit, block") {
    SUBCASE("LowThenUpper C [0, 7680): only the bar starts play the low note") {
        const Document doc = docOf({ev(1, kC, 8.0)}, {4, 4}, Feel{Rhythm::LowThenUpper});
        CHECK(generated(doc, BassPattern::FollowFeel) == Pins{{0, 864, 36, 100}, {3840, 864, 36, 100}});
    }
    SUBCASE("pulse [959, 1200): the 5-tick first hit is dropped, the rest is not bridged") {
        const Document doc = docOf({evTicks(1, kG, 959), evTicks(2, kC, 241)}, {4, 4}, Feel{Rhythm::Pulse});
        REQUIRE(chordBoundaryTicks(doc) == std::vector<std::int32_t>{0, 959, 1200});
        CHECK(generated(doc, BassPattern::FollowFeel, {2}) == Pins{{960, 216, 36, 90}});
    }
    SUBCASE("Block at velocity 73: one note over the chord at its velocity") {
        const Document doc = docOf({ev(1, kC, 4.0, 73)});
        CHECK(generated(doc, BassPattern::FollowFeel) == Pins{{0, 3840, 36, 73}});
    }
}

TEST_CASE("Follow feel: the over-budget 33-bar Cmaj7 document falls back to Roots") {
    // test_feel.cpp's budget boundary (4/4, half rate, Chord, two octaves, one-bar sevenths: 128 events a bar, so
    // 32 bars are exactly the chord budget and 33 over it), with every bar Cmaj7 as D16 pins it.
    const Feel dense = arp(ArpPattern::Chord, ArpRate::Half, ArpRange::Two);
    std::vector<ChordEvent> bars;
    for (int i = 0; i < 33; ++i) {
        bars.push_back(ev(i + 1, chordOf(0, "maj7"), 4.0));
    }
    const Document over = docOf(bars, {4, 4}, dense);
    REQUIRE(compile(over).feelLimited);
    Pins expected;
    for (int i = 0; i < 33; ++i) {
        expected.push_back(Pin{3840 * i, 3840, 36, 100});
    }
    CHECK(generated(over, BassPattern::FollowFeel) == expected);
    // The budget is the whole document's: 32 bars are within it and follow the feel (16 hits a bar, all with C).
    bars.pop_back();
    const Document within = docOf(bars, {4, 4}, dense);
    REQUIRE_FALSE(compile(within).feelLimited);
    const Pins followed = generated(within, BassPattern::FollowFeel, {1});
    CHECK(followed.size() == 16);
    CHECK(followed.front() == Pin{0, 216, 36, 100});
}

// --- Walking ----------------------------------------------------------------------------

TEST_CASE("Walking: C then G in four meters, full-gap lengths, a chromatic approach to G's bass") {
    SUBCASE("4/4") {
        const Pins pins = generated(docOf({ev(1, kC, 4.0), ev(2, kG, 4.0)}), BassPattern::Walking, {1});
        CHECK(pins == Pins{{0, 960, 36, 100}, {960, 960, 40, 90}, {1920, 960, 43, 90}, {2880, 960, 30, 90}});
    }
    SUBCASE("3/4") {
        const Pins pins = generated(docOf({ev(1, kC, 3.0), ev(2, kG, 3.0)}, {3, 4}), BassPattern::Walking, {1});
        CHECK(pitchesOf(pins) == Ints{36, 40, 30});
        CHECK(lengthsOf(pins) == repeated(3, 960));
    }
    SUBCASE("6/8") {
        const Pins pins = generated(docOf({ev(1, kC, 3.0), ev(2, kG, 3.0)}, {6, 8}), BassPattern::Walking, {1});
        CHECK(pins == Pins{{0, 1440, 36, 100}, {1440, 1440, 30, 90}});
    }
    SUBCASE("7/8: up to 52, reversing there") {
        const Pins pins = generated(docOf({ev(1, kC, 3.5), ev(2, kG, 3.5)}, {7, 8}), BassPattern::Walking, {1});
        CHECK(pitchesOf(pins) == Ints{36, 40, 43, 48, 52, 48, 30});
        CHECK(startsOf(pins) == Ints{0, 480, 960, 1440, 1920, 2400, 2880});
        CHECK(lengthsOf(pins) == repeated(7, 480));
    }
}

TEST_CASE("Walking: one hit, an inversion, a repeated pitch, an approach to 28") {
    SUBCASE("one-hit C [0, 240)") {
        CHECK(generated(docOf({ev(1, kC, 0.25), ev(2, kG, 4.0)}), BassPattern::Walking, {1}) == Pins{{0, 240, 36, 100}});
    }
    SUBCASE("C/E then G: from E") {
        const Pins pins = generated(docOf({ev(1, kC, 4.0, 100, 1), ev(2, kG, 4.0)}), BassPattern::Walking, {1});
        CHECK(pitchesOf(pins) == Ints{28, 31, 36, 30});
    }
    SUBCASE("two-hit C then C#: the approach repeats C, two attacks") {
        const Pins pins = generated(docOf({ev(1, kC, 2.0), ev(2, chordOf(1, "maj"), 4.0)}), BassPattern::Walking, {1});
        CHECK(pins == Pins{{0, 960, 36, 100}, {960, 960, 36, 90}});
    }
    SUBCASE("next bass E (28): the approach is 29, from above") {
        const Pins pins = generated(docOf({ev(1, kC, 4.0), ev(2, chordOf(4, "maj"), 4.0)}), BassPattern::Walking, {1});
        CHECK(pitchesOf(pins) == Ints{36, 40, 43, 29});
    }
}

TEST_CASE("Walking: after the last chord the Loop decides; the next chord counts outside the scope") {
    SUBCASE("final C, Loop off: the walk continues") {
        CHECK(pitchesOf(generated(docOf({ev(1, kC, 4.0)}, {4, 4}, {}, false), BassPattern::Walking)) ==
              Ints{36, 40, 43, 48});
    }
    SUBCASE("final C, Loop on: an approach to the first chord, C") {
        CHECK(pitchesOf(generated(docOf({ev(1, kC, 4.0)}, {4, 4}, {}, true), BassPattern::Walking)) ==
              Ints{36, 40, 43, 35});
        // G then C with Loop on: C approaches G again.
        const Pins both = generated(docOf({ev(1, kG, 4.0), ev(2, kC, 4.0)}, {4, 4}, {}, true), BassPattern::Walking);
        CHECK(pitchesOf(both) == Ints{31, 35, 38, 35, 36, 40, 43, 30});
    }
    SUBCASE("generating only C in C then G still approaches G") {
        const Document doc = docOf({ev(1, kC, 4.0), ev(2, kG, 4.0)});
        CHECK(pitchesOf(generated(doc, BassPattern::Walking, {1})) == Ints{36, 40, 43, 30});
        // And all of it: G is last, Loop off, so it walks on from its own bass.
        CHECK(pitchesOf(generated(doc, BassPattern::Walking)) == Ints{36, 40, 43, 30, 31, 35, 38, 43});
    }
}

TEST_CASE("Walking: a longer C reverses at 52 and again at its lowest chord tone") {
    const Pins pins = generated(docOf({ev(1, kC, 16.0)}), BassPattern::Walking);
    CHECK(pitchesOf(pins) == Ints{36, 40, 43, 48, 52, 48, 43, 40, 36, 31, 28, 31, 36, 40, 43, 48});
    CHECK(lengthsOf(pins) == repeated(16, 960));
    CHECK(velocitiesOf(pins) == Ints{100, 90, 90, 90, 100, 90, 90, 90, 100, 90, 90, 90, 100, 90, 90, 90});
}

// --- scope and anchoring ----------------------------------------------------------------

TEST_CASE("generateBass: an empty scope is every chord; a non-contiguous scope, repeats and unknown ids") {
    const Document doc = docOf({ev(1, kC, 4.0), ev(2, kG, 4.0), ev(3, chordOf(9, "min"), 4.0)});
    CHECK(generated(doc, BassPattern::Roots) == Pins{{0, 3840, 36, 100}, {3840, 3840, 31, 100}, {7680, 3840, 33, 100}});
    CHECK(generated(doc, BassPattern::Roots, {3, 1}) == Pins{{0, 3840, 36, 100}, {7680, 3840, 33, 100}});
    CHECK(generated(doc, BassPattern::Roots, {3, 3, 99}) == Pins{{7680, 3840, 33, 100}});
    // Anchored to their chords, offsets from each chord's boundary.
    const std::vector<GeneratedNote> notes = generateBass(doc, BassPattern::RootPulse, {2});
    REQUIRE(notes.size() == 4);
    CHECK(notes[0] == GeneratedNote{2, 0, 864, 31, 100});
    CHECK(notes[3] == GeneratedNote{2, 2880, 864, 31, 90});
    CHECK(generateBass(Document{}, BassPattern::Walking, {}).empty());
}

TEST_CASE("generateBass: auto voice-leading decides the sounding bass") {
    Document doc = docOf({ev(1, kC, 4.0), ev(2, chordOf(5, "maj"), 4.0)});
    doc.autoVoiceLead = true;
    const std::vector<std::vector<int>> voiced = voicings(doc);
    const Pins pins = generated(doc, BassPattern::Roots, {2});
    REQUIRE(pins.size() == 1);
    CHECK(pins[0].pitch == placeBass(voiced[1].front()));
    CHECK(voiced[1] == Ints{60, 65, 69});  // F voice-led after C is F/C
    CHECK(pins[0].pitch == 36);             // so its bass is C, not F's 29
}
