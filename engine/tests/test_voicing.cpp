#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/scale.h"
#include "chordlab/voicing.h"

#include <algorithm>
#include <cstddef>
#include <set>
#include <string_view>
#include <vector>

using namespace chordlab;

namespace {

Chord chord(PitchClass root, std::string_view typeId) {
    return Chord{root, &chordType(typeId)};
}

using Notes = std::vector<int>;

constexpr int kDefaultLowest = 48;
constexpr int kDefaultHighest = 84;

bool sortedUniqueWithin(const Notes& notes, int lowest, int highest) {
    if (!std::is_sorted(notes.begin(), notes.end())) {
        return false;
    }
    if (std::adjacent_find(notes.begin(), notes.end()) != notes.end()) {
        return false;
    }
    return std::all_of(notes.begin(), notes.end(), [&](int n) { return n >= lowest && n <= highest; });
}

std::set<PitchClass> pitchClassSet(const Notes& notes) {
    std::set<PitchClass> pcs;
    for (const int n : notes) {
        pcs.insert(pitchClassOf(n));
    }
    return pcs;
}

std::set<PitchClass> pitchClassSet(const Chord& c) {
    const auto pcs = pitchClasses(c);
    return std::set<PitchClass>(pcs.begin(), pcs.end());
}

}  // namespace

// --- Plan cases ----------------------------------------------------------------------------

TEST_CASE("closeVoicing: stacks the chord from the first root at or above the default lowest note") {
    CHECK(closeVoicing(chord(0, "maj")) == Notes{48, 52, 55});
    CHECK(closeVoicing(chord(9, "min")) == Notes{57, 60, 64});
    CHECK(closeVoicing(chord(0, "9")) == Notes{48, 52, 55, 58, 62});
}

TEST_CASE("closeVoicing: a high root stays within the range, sorted and unique") {
    const Notes notes = closeVoicing(chord(11, "maj7"), 48, 84);
    CHECK(notes == Notes{59, 63, 66, 70});
    CHECK(sortedUniqueWithin(notes, 48, 84));
}

TEST_CASE("closeVoicing: a range narrower than two octaves falls back to the defaults") {
    CHECK(closeVoicing(chord(0, "maj"), 60, 70) == closeVoicing(chord(0, "maj")));
    CHECK(closeVoicing(chord(7, "9"), 48, 60) == closeVoicing(chord(7, "9")));  // 12 < 24
    CHECK(closeVoicing(chord(0, "maj"), 60, 83) == closeVoicing(chord(0, "maj")));  // 23 < 24
}

TEST_CASE("closeVoicing: a range outside MIDI falls back to the defaults") {
    CHECK(closeVoicing(chord(0, "maj"), -1, 60) == closeVoicing(chord(0, "maj")));
    CHECK(closeVoicing(chord(0, "maj"), 100, 128) == closeVoicing(chord(0, "maj")));
}

TEST_CASE("closeVoicing: honours a valid custom range") {
    CHECK(closeVoicing(chord(0, "maj"), 60, 96) == Notes{60, 64, 67});
    CHECK(closeVoicing(chord(0, "maj"), 60, 84) == Notes{60, 64, 67});  // exactly two octaves is valid
    CHECK(closeVoicing(chord(0, "maj"), 0, 127) == Notes{0, 4, 7});
    CHECK(closeVoicing(chord(11, "maj"), 0, 127) == Notes{11, 15, 18});
}

TEST_CASE("closeVoicing: the root is the first note of its pitch class at or above lowest") {
    CHECK(closeVoicing(chord(0, "maj"), 48, 84).front() == 48);  // lowest itself
    CHECK(closeVoicing(chord(11, "maj"), 48, 84).front() == 59);
    CHECK(closeVoicing(chord(1, "maj"), 48, 84).front() == 49);
    CHECK(closeVoicing(chord(0, "maj"), 49, 84).front() == 60);  // just missed 48
}

// --- Folding -------------------------------------------------------------------------------

TEST_CASE("closeVoicing: a chord that fits the range is not folded") {
    CHECK(closeVoicing(chord(10, "69"), 48, 84) == Notes{58, 62, 65, 67, 72});
    CHECK(closeVoicing(chord(7, "9"), 48, 72) == Notes{55, 59, 62, 65, 69});
}

TEST_CASE("closeVoicing: notes above highest fold down by octaves and re-sort into the chord") {
    // B9 on 48..72: 59 63 66 69 73 -> 73 drops to 61 and sits a semitone above the root.
    CHECK(closeVoicing(chord(11, "9"), 48, 72) == Notes{59, 61, 63, 66, 69});
    // Bb6/9 on 48..72: 58 62 65 67 72 -> nothing exceeds 72.
    CHECK(closeVoicing(chord(10, "69"), 48, 72) == Notes{58, 62, 65, 67, 72});
    // B6/9 on 48..72: 59 63 66 68 73 -> 73 drops to 61.
    CHECK(closeVoicing(chord(11, "69"), 48, 72) == Notes{59, 61, 63, 66, 68});
}

TEST_CASE("closeVoicing: folding drops by whole octaves until the note fits") {
    // B add9 on 0..24 (exactly two octaves): 11 15 18 25 -> 25 drops to 13.
    CHECK(closeVoicing(chord(11, "add9"), 0, 24) == Notes{11, 13, 15, 18});
}

// --- Properties ------------------------------------------------------------------------------

TEST_CASE("closeVoicing: every type on every root fits the default range and keeps the pitch classes") {
    for (const ChordType& type : allChordTypes()) {
        for (PitchClass root = 0; root < 12; ++root) {
            const Chord c{root, &type};
            const Notes notes = closeVoicing(c);
            CAPTURE(type.id);
            CAPTURE(root);
            CHECK(sortedUniqueWithin(notes, kDefaultLowest, kDefaultHighest));
            CHECK(notes.size() <= type.intervals.size());
            CHECK(notes.size() == type.intervals.size());  // nothing in the dictionary needs folding here
            CHECK(pitchClassSet(notes) == pitchClassSet(c));
            REQUIRE_FALSE(notes.empty());
            CHECK(pitchClassOf(notes.front()) == root);
            CHECK(notes.front() >= kDefaultLowest);
            CHECK(notes.front() < kDefaultLowest + 12);
        }
    }
}

TEST_CASE("closeVoicing: every type on every root fits a two-octave range and keeps the pitch classes") {
    constexpr int kLowest = 48;
    constexpr int kHighest = 72;
    for (const ChordType& type : allChordTypes()) {
        for (PitchClass root = 0; root < 12; ++root) {
            const Chord c{root, &type};
            const Notes notes = closeVoicing(c, kLowest, kHighest);
            CAPTURE(type.id);
            CAPTURE(root);
            CHECK(sortedUniqueWithin(notes, kLowest, kHighest));
            CHECK(notes.size() <= type.intervals.size());
            CHECK(pitchClassSet(notes) == pitchClassSet(c));
        }
    }
}
