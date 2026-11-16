#include "doctest.h"

#include "chordlab/generate.h"

#include "chordlab/chord.h"
#include "chordlab/compiler.h"
#include "chordlab/document.h"
#include "chordlab/feel.h"
#include "chordlab/meter.h"
#include "chordlab/tracks.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

using namespace chordlab;

// The lead generator, tracks design D22. Every pinned case of D22's "Pinned cases" appears below as its tuples
// (startTick, lengthTicks, pitch, velocity), starts relative to the progression. Unless a case says otherwise the
// document is D22's default: key C major, sequential ids from 1, root position, octave 0, auto voice-leading off,
// chord velocity 100, block feel without strum or humanise, Loop explicitly off, variation 0, the whole scope.

namespace {

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

std::ostream& operator<<(std::ostream& os, const GridHit& h) {
    return os << "(" << h.start << ", " << h.length << ", " << h.velocity << ")";
}

using Pins = std::vector<Pin>;
using Ints = std::vector<int>;
using Hits = std::vector<GridHit>;

Chord chordOf(PitchClass root, const char* typeId) {
    return Chord{root, &chordType(typeId)};
}

const Chord kC = chordOf(0, "maj");
const Chord kG = chordOf(7, "maj");

ChordEvent ev(int id, Chord chord, double beats, int velocity = 100, int inversion = 0) {
    ChordEvent e{id, chord, beats, velocity};
    e.inversion = inversion;
    e.manualVoicing = inversion != 0;
    return e;
}

ChordEvent evTicks(int id, Chord chord, int ticks) {
    return ev(id, chord, ticks / static_cast<double>(kFeelTicksPerBeat));
}

/// D22's default document: key C major (Document's default), Loop explicitly off.
Document docOf(std::vector<ChordEvent> events, Meter meter = {4, 4}, Feel feel = {}) {
    Document doc;
    doc.events = std::move(events);
    doc.meter = meter;
    doc.feel = feel;
    doc.progressionLoop = false;
    return doc;
}

Feel arp(ArpPattern pattern, ArpRate rate = ArpRate::Subdivision, ArpRange range = ArpRange::One) {
    Feel feel;
    feel.rhythm = Rhythm::Arpeggio;
    feel.pattern = pattern;
    feel.rate = rate;
    feel.range = range;
    return feel;
}

/// `generateLead` as pins, each start made absolute through its anchor's boundary.
Pins rawPins(const Document& doc, LeadPattern pattern, const std::vector<int>& scope, int variation) {
    const ChordTimeline timeline = chordTimeline(doc);
    Pins pins;
    for (const GeneratedNote& n : generateLead(doc, pattern, scope, variation)) {
        const int index = timeline.indexOf(n.anchorEventId);
        REQUIRE(index >= 0);
        pins.push_back(Pin{timeline.boundaries[static_cast<std::size_t>(index)] + n.offsetTick, n.lengthTicks, n.pitch,
                           n.velocity});
    }
    return pins;
}

/// `generateLead` as pins, checking on the way two of D22's invariants for every fixture: turning Loop on changes
/// nothing, and neither do notes already in a track (the generator never reads them). Every pitch lies in 60..84.
Pins lead(const Document& doc, LeadPattern pattern, const std::vector<int>& scope = {}, int variation = 0) {
    const Pins pins = rawPins(doc, pattern, scope, variation);
    Document looped = doc;
    looped.progressionLoop = !doc.progressionLoop;
    CHECK_MESSAGE(rawPins(looped, pattern, scope, variation) == pins, "Loop changed the line");
    if (!doc.events.empty()) {
        Document withNotes = doc;
        Track track{1, "Lead", TrackRole::Lead};
        track.notes.push_back(TrackNote{1, 61, doc.events.front().id, 0, 480, 100});
        track.notes.push_back(TrackNote{2, 83, doc.events.back().id, 30, 960, 64});
        withNotes.tracks.push_back(track);
        CHECK_MESSAGE(rawPins(withNotes, pattern, scope, variation) == pins, "track notes changed the line");
    }
    for (const Pin& p : pins) {
        CHECK(p.pitch >= kLeadLowest);
        CHECK(p.pitch <= kLeadHighest);
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

/// The pins starting at or after `tick`.
Pins from(const Pins& pins, int tick) {
    Pins out;
    for (const Pin& p : pins) {
        if (p.start >= tick) {
            out.push_back(p);
        }
    }
    return out;
}

/// D22's main fixture: C then G, one 4/4 bar each, boundaries 0, 3840, 7680.
Document cThenG() {
    return docOf({ev(1, kC, 4.0), ev(2, kG, 4.0)});
}

/// Am7, Dm7, G7, Cmaj7, four beats each in 4/4.
Document sixTwoFiveOne() {
    return docOf({ev(1, chordOf(9, "m7"), 4.0), ev(2, chordOf(2, "m7"), 4.0), ev(3, chordOf(7, "7"), 4.0),
                  ev(4, chordOf(0, "maj7"), 4.0)});
}

const LeadPattern kAllLead[] = {LeadPattern::HeldTone, LeadPattern::GuideTones, LeadPattern::Arpeggio,
                                LeadPattern::Stepwise, LeadPattern::Motif,      LeadPattern::FollowFeel};

}  // namespace

// --- names, constants and helpers ----------------------------------------------------------------

TEST_CASE("leadPatternName / leadPatternFromName: one table, six names, exact spelling") {
    const char* const names[] = {"heldTone", "guideTones", "arpeggio", "stepwise", "motif", "followFeel"};
    static_assert(sizeof(names) / sizeof(names[0]) == kLeadPatternCount, "one name per pattern");
    for (int i = 0; i < kLeadPatternCount; ++i) {
        const auto pattern = static_cast<LeadPattern>(i);
        CHECK(std::string(leadPatternName(pattern)) == names[i]);
        CHECK(leadPatternFromName(names[i]) == pattern);
    }
    CHECK(leadPatternFromName("Motif") == std::nullopt);
    CHECK(leadPatternFromName("motif ") == std::nullopt);
    CHECK(leadPatternFromName("") == std::nullopt);
    CHECK(leadPatternFromName("held-tone") == std::nullopt);
    CHECK(leadPatternFromName("roots") == std::nullopt);  // the bass family is a different table
    CHECK(bassPatternFromName("motif") == std::nullopt);
    // followFeel is spelled the same in both families, but each table answers for its own enum.
    CHECK(leadPatternFromName("followFeel") == LeadPattern::FollowFeel);
    CHECK(bassPatternFromName("followFeel") == BassPattern::FollowFeel);
}

TEST_CASE("lead register and seed constants") {
    CHECK(kLeadLowest == 60);
    CHECK(kLeadHighest == 84);
    CHECK(kLeadSeed == 72);
    CHECK(kMotifVariationCount == 64);
}

TEST_CASE("gridAccent is bassAccent: chord or bar start 100, other pulse 90, other subdivision 80") {
    for (int tick : {0, 240, 480, 720, 960, 1440, 1920, 2880, 3360, 3840}) {
        for (Meter m : {Meter{4, 4}, Meter{3, 4}, Meter{6, 8}, Meter{7, 8}, Meter{2, 2}}) {
            CHECK(gridAccent(m, 0, tick) == bassAccent(m, 0, tick));
        }
    }
    CHECK(gridAccent({4, 4}, 720, 720) == 100);
    CHECK(gridAccent({2, 2}, 0, 1920) == 90);
    CHECK(gridAccent({2, 2}, 0, 960) == 80);
}

TEST_CASE("nearestPitch: the smallest distance, a tie going up") {
    CHECK(nearestPitch({60, 64, 67, 72}, 72) == 72);
    CHECK(nearestPitch({62, 67, 71, 74, 79, 83}, 72) == 71);
    CHECK(nearestPitch({62, 74}, 68) == 74);  // 6 either way: up
    CHECK(nearestPitch({74, 62}, 68) == 74);  // whatever the order
    CHECK(nearestPitch({62, 67, 71, 74, 79, 83}, 84) == 83);
    CHECK(nearestPitch({65}, 0) == 65);
}

TEST_CASE("guideToneClasses: the structural third and seventh, and their stand-ins") {
    using Classes = std::pair<int, std::optional<int>>;
    CHECK(guideToneClasses(kC) == Classes{4, 7});
    CHECK(guideToneClasses(kG) == Classes{11, 2});
    CHECK(guideToneClasses(chordOf(9, "m7")) == Classes{0, 7});
    CHECK(guideToneClasses(chordOf(2, "m7")) == Classes{5, 0});
    CHECK(guideToneClasses(chordOf(7, "7")) == Classes{11, 5});
    CHECK(guideToneClasses(chordOf(0, "maj7")) == Classes{4, 11});
    CHECK(guideToneClasses(chordOf(0, "sus2")) == Classes{2, 7});
    CHECK(guideToneClasses(chordOf(0, "sus4")) == Classes{5, 7});
    CHECK(guideToneClasses(chordOf(0, "7sus4")) == Classes{5, 10});
    CHECK(guideToneClasses(chordOf(0, "5")) == Classes{0, 7});
    CHECK(guideToneClasses(chordOf(0, "dim")) == Classes{3, 6});
    CHECK(guideToneClasses(chordOf(0, "dim7")) == Classes{3, 9});  // the diminished seventh is structural
    CHECK(guideToneClasses(chordOf(0, "6")) == Classes{4, 7});     // the sixth is no seventh
    CHECK(guideToneClasses(chordOf(0, "7#11")) == Classes{4, 10});
    CHECK(guideToneClasses(chordOf(0, "aug")) == Classes{4, 8});
    // Test-only types: a lone root has no second class; without third, fifth and seventh, B is the root when it differs
    // from A, else the chord's lowest other pitch class.
    const ChordType lone{"test-lone", "1", {0}, Family::Other};
    CHECK(guideToneClasses(Chord{2, &lone}) == Classes{2, std::nullopt});
    const ChordType sixth{"test-sixth", "6only", {0, 9}, Family::Other};  // A = root (no third, 5 or 2)
    CHECK(guideToneClasses(Chord{0, &sixth}) == Classes{0, 9});
    CHECK(guideToneClasses(Chord{5, &sixth}) == Classes{5, 2});            // lowest absolute pitch class other than A
    const ChordType thirdOnly{"test-third", "3", {0, 4}, Family::Other};    // B is the root
    CHECK(guideToneClasses(Chord{0, &thirdOnly}) == Classes{4, 0});
}

TEST_CASE("guideToneSplit: the pulse nearest the middle, the later on a tie, 30 ticks from each end") {
    CHECK(guideToneSplit({4, 4}, 0, 3840) == 1920);
    CHECK(guideToneSplit({6, 8}, 0, 2880) == 1440);
    CHECK(guideToneSplit({7, 8}, 0, 3360) == 1920);
    CHECK(guideToneSplit({3, 4}, 0, 2880) == 1920);
    CHECK(guideToneSplit({4, 4}, 0, 240) == std::nullopt);
    CHECK(guideToneSplit({4, 4}, 0, 989) == std::nullopt);  // 960 is 29 from the end
    CHECK(guideToneSplit({4, 4}, 0, 990) == 960);
    CHECK(guideToneSplit({4, 4}, 931, 2000) == 1920);  // 960 is 29 after the start; 1920 is left
    CHECK(guideToneSplit({4, 4}, 930, 1500) == 960);
    CHECK(guideToneSplit({4, 4}, 100, 100) == std::nullopt);
}

TEST_CASE("timing helpers: D22's boundary pins and the lead's terminal rule") {
    // Gated: [0, 33) is a lone 29-tick hit, omitted; [0, 34) lasts 30.
    CHECK(leadGridHits({4, 4}, 0, 33, 480).empty());
    CHECK(leadGridHits({4, 4}, 0, 34, 480) == Hits{{0, 30, 100}});
    // Full duration: [0, 29) none, [0, 30) the whole span.
    CHECK(leadSpanHit({4, 4}, 0, 29).empty());
    CHECK(leadSpanHit({4, 4}, 0, 30) == Hits{{0, 30, 100}});
    CHECK(leadSpanHit({4, 4}, 500, 3840) == Hits{{500, 3340, 100}});
    // Empty and reversed spans.
    CHECK(leadGridHits({4, 4}, 0, 0, 480).empty());
    CHECK(leadGridHits({4, 4}, 50, 10, 480).empty());
    CHECK(leadSpanHit({4, 4}, 0, 0).empty());
    CHECK(leadSpanHit({4, 4}, 50, 10).empty());
    // The bass keeps its short singleton; the lead rule removes exactly that case.
    CHECK(bassGridHits({4, 4}, 0, 33, 480, false) == Hits{{0, 29, 100}});
    CHECK(omitShortLoneHit(Hits{{0, 29, 100}}).empty());
    CHECK(omitShortLoneHit(Hits{{0, 30, 100}}) == Hits{{0, 30, 100}});
    CHECK(omitShortLoneHit(Hits{}).empty());
    CHECK(omitShortLoneHit(Hits{{0, 432, 100}, {480, 432, 80}}) == Hits{{0, 432, 100}, {480, 432, 80}});
}

TEST_CASE("finalizeHits: explicit starts, gate modes and a gap cap; gridStarts feeds it") {
    CHECK(gridStarts(0, 1920, 480) == Ints{0, 480, 960, 1440});
    CHECK(gridStarts(720, 1920, 480) == Ints{720, 960, 1440});
    CHECK(gridStarts(5, 5, 480).empty());
    CHECK(finalizeHits({4, 4}, gridStarts(0, 3840, 960), 3840, HitGate::Gated) ==
          bassGridHits({4, 4}, 0, 3840, 960, false));
    CHECK(finalizeHits({4, 4}, {0, 1440, 1920}, 3840, HitGate::FullGap) == Hits{{0, 1440, 100}, {1440, 480, 80}, {1920, 1920, 90}});
    // Capped at one pulse: a gap of 1920 lasts 864.
    CHECK(finalizeHits({4, 4}, {0, 1920}, 3840, HitGate::Gated, 960) == Hits{{0, 864, 100}, {1920, 864, 90}});
    // The cap applies to the short-hit test too: a 20-tick gap capped is still short.
    CHECK(finalizeHits({4, 4}, {0, 20, 960}, 3840, HitGate::Gated, 960) == Hits{{0, 864, 100}, {960, 864, 90}});
    CHECK(finalizeHits({4, 4}, {}, 3840, HitGate::Gated).empty());
    CHECK(finalizeHits({4, 4}, {100}, 100, HitGate::Gated).empty());
}

TEST_CASE("Motif tables, slot mapping and reflection") {
    const std::vector<int> rhythms[] = {{0, 2, 4, 6}, {0, 3, 4, 6}, {0, 2, 5, 6}, {0, 2, 4, 7},
                                        {0, 1, 4, 6}, {0, 3, 5, 7}, {0, 4, 6},    {0, 2, 3, 6, 7}};
    const std::array<int, 4> contours[] = {{0, 1, 2, 1}, {0, -1, -2, -1}, {0, 1, 0, -1}, {0, 2, 1, 0},
                                           {0, -1, 1, 0}, {0, 0, 1, 0},    {0, 1, -1, 0}, {0, 2, 0, 1}};
    for (int i = 0; i < 8; ++i) {
        CAPTURE(i);
        CHECK(motifRhythm(i) == rhythms[i]);
        CHECK(motifContour(i) == contours[i]);
    }
    // Slot j at b + floor(j * M / 8) * subdivision.
    for (int j = 0; j < 8; ++j) {
        CHECK(motifSlotTick({4, 4}, 3840, j) == 3840 + 480 * j);
    }
    CHECK(motifSlotTick({6, 8}, 0, 2) == 480);
    CHECK(motifSlotTick({6, 8}, 0, 4) == 1440);
    CHECK(motifSlotTick({6, 8}, 0, 6) == 1920);
    CHECK(motifSlotTick({7, 8}, 0, 2) == 720);
    CHECK(motifSlotTick({7, 8}, 0, 4) == 1680);
    CHECK(motifSlotTick({7, 8}, 0, 6) == 2400);
    CHECK(motifSlotTick({3, 4}, 0, 1) == 0);   // M = 6: slot 1 falls on slot 0
    CHECK(motifSlotTick({2, 2}, 0, 3) == 960);  // M = 4, subdivision 960
    // Reflection at both ends.
    CHECK(motifReflectIndex(6, 1, -1) == 0);
    CHECK(motifReflectIndex(6, 1, -2) == 1);  // G's reflected index in the variation-9 pin
    CHECK(motifReflectIndex(7, 6, 1) == 5);
    CHECK(motifReflectIndex(7, 6, 2) == 4);
    CHECK(motifReflectIndex(7, 3, 2) == 5);
    CHECK(motifReflectIndex(3, 0, -5) == 1);
    CHECK(motifReflectIndex(1, 0, 2) == 0);
    CHECK(motifReflectIndex(1, 0, -1) == 0);
}

// --- C -> G, one 4/4 bar each ------------------------------------------------------------------

TEST_CASE("C -> G: Held tone") {
    REQUIRE(chordBoundaryTicks(cThenG()) == std::vector<std::int32_t>{0, 3840, 7680});
    CHECK(lead(cThenG(), LeadPattern::HeldTone) == Pins{{0, 3840, 72, 100}, {3840, 3840, 71, 100}});
}

TEST_CASE("C -> G: Guide tones") {
    CHECK(lead(cThenG(), LeadPattern::GuideTones) ==
          Pins{{0, 1920, 76, 100}, {1920, 1920, 79, 90}, {3840, 1920, 74, 100}, {5760, 1920, 71, 90}});
}

TEST_CASE("C -> G: Arpeggio") {
    CHECK(lead(cThenG(), LeadPattern::Arpeggio) ==
          Pins{{0, 432, 72, 100},    {480, 432, 76, 80},   {960, 432, 79, 90},   {1440, 432, 84, 80},
               {1920, 432, 79, 90},  {2400, 432, 76, 80},  {2880, 432, 72, 90},  {3360, 432, 67, 80},
               {3840, 432, 67, 100}, {4320, 432, 71, 80},  {4800, 432, 74, 90},  {5280, 432, 79, 80},
               {5760, 432, 83, 90},  {6240, 432, 79, 80},  {6720, 432, 74, 90},  {7200, 432, 71, 80}});
}

TEST_CASE("C -> G: Stepwise, F5 and C5 accepted as Avoid passing notes") {
    CHECK(lead(cThenG(), LeadPattern::Stepwise) ==
          Pins{{0, 432, 72, 100},    {480, 432, 74, 80},   {960, 432, 76, 90},   {1440, 432, 77, 80},
               {1920, 432, 79, 90},  {2400, 432, 81, 80},  {2880, 432, 84, 90},  {3360, 432, 84, 80},
               {3840, 432, 83, 100}, {4320, 432, 81, 80},  {4800, 432, 79, 90},  {5280, 432, 76, 80},
               {5760, 432, 74, 90},  {6240, 432, 72, 80},  {6720, 432, 71, 90},  {7200, 432, 71, 80}});
}

TEST_CASE("C -> G: Motif, variation 0") {
    CHECK(lead(cThenG(), LeadPattern::Motif) ==
          Pins{{0, 864, 72, 100},    {960, 864, 76, 90},  {1920, 864, 79, 90}, {2880, 864, 76, 90},
               {3840, 864, 74, 100}, {4800, 864, 79, 90}, {5760, 864, 83, 90}, {6720, 864, 79, 90}});
}

TEST_CASE("C -> G: Follow feel, Block follows the top note's pitch class, not its octave") {
    CHECK(lead(cThenG(), LeadPattern::FollowFeel) == Pins{{0, 3840, 67, 100}, {3840, 3840, 62, 100}});
}

TEST_CASE("C -> G: Motif, variation 9 (rhythm 1, contour 1); 73 equals 9") {
    const Pins nine{{0, 864, 72, 100},    {1440, 432, 67, 80}, {1920, 864, 64, 90}, {2880, 864, 67, 90},
                    {3840, 864, 67, 100}, {5280, 432, 62, 80}, {5760, 864, 67, 90}, {6720, 864, 62, 90}};
    CHECK(lead(cThenG(), LeadPattern::Motif, {}, 9) == nine);
    CHECK(lead(cThenG(), LeadPattern::Motif, {}, 73) == nine);
    CHECK(lead(cThenG(), LeadPattern::Motif, {}, 64) == lead(cThenG(), LeadPattern::Motif, {}, 0));
}

TEST_CASE("variation is read by Motif only") {
    for (LeadPattern pattern : kAllLead) {
        if (pattern != LeadPattern::Motif) {
            CHECK(rawPins(cThenG(), pattern, {}, 9) == rawPins(cThenG(), pattern, {}, 0));
        }
    }
}

TEST_CASE("Motif: every one of the 64 takes stays in the register, starts each chord at its start, and differs") {
    const Document doc = sixTwoFiveOne();
    std::vector<Pins> takes;
    for (int v = 0; v < kMotifVariationCount; ++v) {
        CAPTURE(v);
        const Pins pins = lead(doc, LeadPattern::Motif, {}, v);
        REQUIRE_FALSE(pins.empty());
        for (int chordStart : {0, 3840, 7680, 11520}) {
            CHECK(from(pins, chordStart).front().start == chordStart);
        }
        for (const Pins& other : takes) {
            CHECK_FALSE(other == pins);
        }
        takes.push_back(pins);
    }
}

// --- compound and odd meter, one C bar each ----------------------------------------------------

TEST_CASE("6/8 C: Guide tones, Arpeggio, Stepwise, Motif") {
    const Document doc = docOf({ev(1, kC, 3.0)}, {6, 8});
    REQUIRE(chordBoundaryTicks(doc) == std::vector<std::int32_t>{0, 2880});
    CHECK(lead(doc, LeadPattern::GuideTones) == Pins{{0, 1440, 76, 100}, {1440, 1440, 79, 90}});
    CHECK(lead(doc, LeadPattern::Arpeggio) == Pins{{0, 432, 72, 100},   {480, 432, 76, 80},  {960, 432, 79, 80},
                                                   {1440, 432, 84, 90}, {1920, 432, 79, 80}, {2400, 432, 76, 80}});
    CHECK(lead(doc, LeadPattern::Stepwise) == Pins{{0, 432, 72, 100},   {480, 432, 74, 80},  {960, 432, 74, 80},
                                                   {1440, 432, 76, 90}, {1920, 432, 76, 80}, {2400, 432, 76, 80}});
    CHECK(lead(doc, LeadPattern::Motif) ==
          Pins{{0, 432, 72, 100}, {480, 864, 76, 80}, {1440, 432, 79, 90}, {1920, 864, 76, 80}});
}

TEST_CASE("7/8 C: Guide tones split at 1920 (the midpoint tie), Stepwise, Motif") {
    const Document doc = docOf({ev(1, kC, 3.5)}, {7, 8});
    REQUIRE(chordBoundaryTicks(doc) == std::vector<std::int32_t>{0, 3360});
    CHECK(lead(doc, LeadPattern::GuideTones) == Pins{{0, 1920, 76, 100}, {1920, 1440, 79, 90}});
    CHECK(lead(doc, LeadPattern::Stepwise) ==
          Pins{{0, 216, 72, 100},    {240, 216, 74, 80},   {480, 216, 76, 90},   {720, 216, 77, 80},
               {960, 216, 79, 90},   {1200, 216, 81, 80},  {1440, 216, 84, 90},  {1680, 216, 81, 80},
               {1920, 216, 79, 90},  {2160, 216, 77, 80},  {2400, 216, 76, 90},  {2640, 216, 74, 80},
               {2880, 216, 72, 90},  {3120, 216, 72, 80}});
    CHECK(lead(doc, LeadPattern::Motif) ==
          Pins{{0, 432, 72, 100}, {720, 432, 76, 80}, {1680, 432, 79, 80}, {2400, 432, 76, 90}});
}

TEST_CASE("3/4 C: Guide tones") {
    CHECK(lead(docOf({ev(1, kC, 3.0)}, {3, 4}), LeadPattern::GuideTones) ==
          Pins{{0, 1920, 76, 100}, {1920, 960, 79, 90}});
}

// --- Am7 -> Dm7 -> G7 -> Cmaj7 -----------------------------------------------------------------

TEST_CASE("Am7 -> Dm7 -> G7 -> Cmaj7: Guide tones") {
    CHECK(lead(sixTwoFiveOne(), LeadPattern::GuideTones) ==
          Pins{{0, 1920, 72, 100},    {1920, 1920, 67, 90},  {3840, 1920, 65, 100}, {5760, 1920, 60, 90},
               {7680, 1920, 65, 100}, {9600, 1920, 71, 90},  {11520, 1920, 71, 100}, {13440, 1920, 76, 90}});
}

TEST_CASE("Am7 -> Dm7 -> G7 -> Cmaj7: Stepwise") {
    CHECK(lead(sixTwoFiveOne(), LeadPattern::Stepwise) ==
          Pins{{0, 432, 72, 100},     {480, 432, 74, 80},    {960, 432, 76, 90},    {1440, 432, 77, 80},
               {1920, 432, 79, 90},   {2400, 432, 79, 80},   {2880, 432, 81, 90},   {3360, 432, 81, 80},
               {3840, 432, 81, 100},  {4320, 432, 83, 80},   {4800, 432, 84, 90},   {5280, 432, 83, 80},
               {5760, 432, 81, 90},   {6240, 432, 79, 80},   {6720, 432, 77, 90},   {7200, 432, 77, 80},
               {7680, 432, 77, 100},  {8160, 432, 76, 80},   {8640, 432, 74, 90},   {9120, 432, 72, 80},
               {9600, 432, 71, 90},   {10080, 432, 69, 80},  {10560, 432, 67, 90},  {11040, 432, 67, 80},
               {11520, 432, 67, 100}, {12000, 432, 65, 80},  {12480, 432, 64, 90},  {12960, 432, 62, 80},
               {13440, 432, 60, 90},  {13920, 432, 62, 80},  {14400, 432, 64, 90},  {14880, 432, 64, 80}});
}

// --- guide-tone structures, each one bar of 4/4 from 72 -----------------------------------------

TEST_CASE("Guide tones: sus, power, diminished structures") {
    const auto guide = [](const char* type) { return lead(docOf({ev(1, chordOf(0, type), 4.0)}), LeadPattern::GuideTones); };
    CHECK(guide("sus2") == Pins{{0, 1920, 74, 100}, {1920, 1920, 79, 90}});
    CHECK(guide("sus4") == Pins{{0, 1920, 77, 100}, {1920, 1920, 79, 90}});
    CHECK(guide("7sus4") == Pins{{0, 1920, 70, 100}, {1920, 1920, 65, 90}});
    CHECK(guide("5") == Pins{{0, 1920, 72, 100}, {1920, 1920, 67, 90}});
    CHECK(guide("dim") == Pins{{0, 1920, 75, 100}, {1920, 1920, 78, 90}});
    CHECK(guide("dim7") == Pins{{0, 1920, 75, 100}, {1920, 1920, 81, 90}});  // the upward tie-break
}

TEST_CASE("Guide tones: a chord with one pitch class holds it; no split holds the nearer of A and B") {
    const ChordType lone{"test-lone", "1", {0}, Family::Other};
    CHECK(lead(docOf({ev(1, Chord{2, &lone}, 4.0)}), LeadPattern::GuideTones) == Pins{{0, 3840, 74, 100}});
    // A one-beat C has no pulse strictly inside: one note, E5 being nearer 72 than G4.
    CHECK(lead(docOf({ev(1, kC, 1.0)}), LeadPattern::GuideTones) == Pins{{0, 960, 76, 100}});
}

// --- Arpeggio reversals --------------------------------------------------------------------------

TEST_CASE("Arpeggio: eight beats of C reverse after 84 and after 60, no turning note doubled") {
    const Ints p{72, 76, 79, 84, 79, 76, 72, 67, 64, 60, 64, 67, 72, 76, 79, 84};
    const Ints v{100, 80, 90, 80, 90, 80, 90, 80, 100, 80, 90, 80, 90, 80, 90, 80};
    Pins expected;
    for (std::size_t k = 0; k < 16; ++k) {
        expected.push_back(Pin{480 * static_cast<int>(k), 432, p[k], v[k]});
    }
    CHECK(lead(docOf({ev(1, kC, 8.0)}), LeadPattern::Arpeggio) == expected);
}

// --- Follow feel -------------------------------------------------------------------------------

TEST_CASE("Follow feel: Up / Half / One arpeggio, and Block at chord velocity 73") {
    const Document up = docOf({ev(1, kC, 4.0), ev(2, kG, 4.0)}, {4, 4}, arp(ArpPattern::Up, ArpRate::Half));
    CHECK(lead(up, LeadPattern::FollowFeel) ==
          Pins{{480, 216, 67, 80},  {1200, 216, 67, 80}, {1920, 216, 67, 90}, {2640, 216, 67, 80}, {3360, 216, 67, 80},
               {4320, 216, 62, 80}, {5040, 216, 62, 80}, {5760, 216, 62, 90}, {6480, 216, 62, 80}, {7200, 216, 62, 80}});
    const Document soft = docOf({ev(1, kC, 4.0, 73), ev(2, kG, 4.0, 73)});
    CHECK(lead(soft, LeadPattern::FollowFeel) == Pins{{0, 3840, 67, 73}, {3840, 3840, 62, 73}});
}

TEST_CASE("Follow feel: the voicing decides the top pitch class") {
    // C/E, manual first inversion: [64, 67, 72], top C.
    const Document inverted = docOf({ev(1, kC, 4.0, 100, 1)});
    REQUIRE(voicings(inverted).front() == Ints{64, 67, 72});
    CHECK(lead(inverted, LeadPattern::FollowFeel) == Pins{{0, 3840, 72, 100}});
    // C -> F with auto voice-leading: F is voiced [60, 65, 69], top A.
    Document led = docOf({ev(1, kC, 4.0), ev(2, chordOf(5, "maj"), 4.0)});
    led.autoVoiceLead = true;
    REQUIRE(voicings(led)[1] == Ints{60, 65, 69});
    CHECK(lead(led, LeadPattern::FollowFeel) == Pins{{0, 3840, 67, 100}, {3840, 3840, 69, 100}});
    led.autoVoiceLead = false;
    CHECK(lead(led, LeadPattern::FollowFeel) == Pins{{0, 3840, 67, 100}, {3840, 3840, 72, 100}});
    // Only Follow feel reads the voicing: the other five patterns ignore inversion and auto voice-leading.
    Document plain = docOf({ev(1, kC, 4.0), ev(2, chordOf(5, "maj"), 4.0)});
    Document voiced = docOf({ev(1, kC, 4.0, 100, 2), ev(2, chordOf(5, "maj"), 4.0, 100, 1)});
    voiced.autoVoiceLead = true;
    for (LeadPattern pattern : kAllLead) {
        if (pattern != LeadPattern::FollowFeel) {
            CHECK(lead(voiced, pattern) == lead(plain, pattern));
        }
    }
}

TEST_CASE("Follow feel: the over-budget 33-bar Cmaj7 document falls back to Held tone") {
    const Feel dense = arp(ArpPattern::Chord, ArpRate::Half, ArpRange::Two);
    std::vector<ChordEvent> bars;
    for (int i = 0; i < 33; ++i) {
        bars.push_back(ev(i + 1, chordOf(0, "maj7"), 4.0));
    }
    const Document over = docOf(bars, {4, 4}, dense);
    REQUIRE(compile(over).feelLimited);
    Pins expected;
    for (int i = 0; i < 33; ++i) {
        expected.push_back(Pin{3840 * i, 3840, 72, 100});
    }
    CHECK(lead(over, LeadPattern::FollowFeel) == expected);
    CHECK(lead(over, LeadPattern::FollowFeel, {5, 9}) == Pins{{3840 * 4, 3840, 72, 100}, {3840 * 8, 3840, 72, 100}});
}

TEST_CASE("Follow feel: the capacity pin, 32 C power chords of 64 beats under LowThenUpper, 1,536 notes") {
    std::vector<ChordEvent> chords;
    for (int i = 0; i < 32; ++i) {
        chords.push_back(ev(i + 1, chordOf(0, "5"), kMaxLengthBeats));
    }
    const Document doc = docOf(chords, {4, 4}, Feel{Rhythm::LowThenUpper});
    REQUIRE_FALSE(compile(doc).feelLimited);
    const std::vector<GeneratedNote> notes = generateLead(doc, LeadPattern::FollowFeel, {});
    CHECK(notes.size() == 1536);
    // The bar start plays the low C; the top note, G, plays on the other 48 pulses of each chord.
    CHECK(notes.front().anchorEventId == 1);
    CHECK(notes.front().offsetTick == 960);
    CHECK(notes.front().pitch == 67);
}

// --- one-hit chords ------------------------------------------------------------------------------

TEST_CASE("One-hit chords: a C of a quarter beat, [0, 240)") {
    const Document doc = docOf({ev(1, kC, 0.25)});
    REQUIRE(chordBoundaryTicks(doc) == std::vector<std::int32_t>{0, 240});
    CHECK(lead(doc, LeadPattern::HeldTone) == Pins{{0, 240, 72, 100}});
    CHECK(lead(doc, LeadPattern::GuideTones) == Pins{{0, 240, 76, 100}});
    CHECK(lead(doc, LeadPattern::Arpeggio) == Pins{{0, 216, 72, 100}});
    CHECK(lead(doc, LeadPattern::Stepwise) == Pins{{0, 216, 72, 100}});
    CHECK(lead(doc, LeadPattern::Motif) == Pins{{0, 216, 72, 100}});
    CHECK(lead(doc, LeadPattern::FollowFeel) == Pins{{0, 240, 67, 100}});
    CHECK(lead(docOf({ev(1, kC, 0.25)}, {4, 4}, arp(ArpPattern::Up, ArpRate::Half)), LeadPattern::FollowFeel).empty());
    CHECK(lead(docOf({ev(1, kC, 0.25)}, {4, 4}, arp(ArpPattern::Down, ArpRate::Half)), LeadPattern::FollowFeel) ==
          Pins{{0, 216, 67, 100}});
}

// --- scope, pruning, Avoid rejection --------------------------------------------------------------

TEST_CASE("Scope {2} of C -> G: exactly G's notes of the whole line, for every pattern") {
    for (LeadPattern pattern : kAllLead) {
        CAPTURE(leadPatternName(pattern));
        const Pins whole = lead(cThenG(), pattern);
        CHECK(lead(cThenG(), pattern, {2}) == from(whole, 3840));
    }
    CHECK(lead(cThenG(), LeadPattern::Arpeggio, {2}).front().pitch == 67);
    CHECK(lead(cThenG(), LeadPattern::Stepwise, {2}).front().pitch == 83);
    CHECK(lead(cThenG(), LeadPattern::FollowFeel, {2}).front().pitch == 62);
}

TEST_CASE("Scope {3, 1, 3} of C -> G -> Am, Held tone: G advances the line out of scope") {
    const Document doc = docOf({ev(1, kC, 4.0), ev(2, kG, 4.0), ev(3, chordOf(9, "min"), 4.0)});
    CHECK(lead(doc, LeadPattern::HeldTone, {3, 1, 3}) == Pins{{0, 3840, 72, 100}, {7680, 3840, 72, 100}});
    CHECK(lead(doc, LeadPattern::HeldTone, {3, 99}) == Pins{{7680, 3840, 72, 100}});  // an unknown id is skipped
    // Anchored to their chords, offsets from each chord's boundary.
    const std::vector<GeneratedNote> notes = generateLead(doc, LeadPattern::Motif, {2});
    REQUIRE(notes.size() == 4);
    CHECK(notes[0].anchorEventId == 2);
    CHECK(notes[0].offsetTick == 0);
    CHECK(notes[3].offsetTick == 2880);
    CHECK(generateLead(Document{}, LeadPattern::Stepwise, {}).empty());
}

TEST_CASE("Scope: Stepwise's direction is carried through chords outside the scope") {
    // The line's direction turns inside Dm7 and Cmaj7; generating any one chord alone must give its notes of the whole.
    const Pins whole = lead(sixTwoFiveOne(), LeadPattern::Stepwise);
    for (int id = 1; id <= 4; ++id) {
        CAPTURE(id);
        Pins expected;
        for (const Pin& p : whole) {
            if (p.start >= 3840 * (id - 1) && p.start < 3840 * id) {
                expected.push_back(p);
            }
        }
        CHECK(lead(sixTwoFiveOne(), LeadPattern::Stepwise, {id}) == expected);
    }
}

TEST_CASE("Pruning: a first C spanning [0, 961), Arpeggio, loses the 960 attack before pitches are given") {
    const Document doc = docOf({evTicks(1, kC, 961), evTicks(2, kG, 959)});
    REQUIRE(chordBoundaryTicks(doc) == std::vector<std::int32_t>{0, 961, 1920});
    CHECK(lead(doc, LeadPattern::Arpeggio, {1}) == Pins{{0, 432, 72, 100}, {480, 432, 76, 80}});
}

TEST_CASE("Stepwise: C in 2/2, subdivision 960, gated 864") {
    const Document doc = docOf({ev(1, kC, 4.0)}, {2, 2});
    CHECK(lead(doc, LeadPattern::Stepwise) ==
          Pins{{0, 864, 72, 100}, {960, 864, 74, 80}, {1920, 864, 76, 90}, {2880, 864, 76, 80}});
}

TEST_CASE("Stepwise: Avoid rejection - too long, and a repeated F ornament") {
    // Two bars of C in 2/2: F between E and G lasts 864 (> 480), so it becomes the preceding E.
    CHECK(pitchesOf(lead(docOf({ev(1, kC, 8.0)}, {2, 2}), LeadPattern::Stepwise)) == Ints{72, 74, 76, 76, 79, 81, 84, 84});
    // Two bars of C in 6/8: E to G has two intervening subdivisions, both F; each has an Avoid neighbour (and one
    // a repeated pitch), so both become E.
    CHECK(pitchesOf(lead(docOf({ev(1, kC, 6.0)}, {6, 8}), LeadPattern::Stepwise)) ==
          Ints{72, 74, 74, 76, 76, 76, 79, 81, 83, 84, 84, 84});
}
