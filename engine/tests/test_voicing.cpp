#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/scale.h"
#include "chordlab/voicing.h"

#include <algorithm>
#include <climits>
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

constexpr int kDefaultLowest = 60;
constexpr int kDefaultHighest = 96;

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
    CHECK(closeVoicing(chord(0, "maj")) == Notes{60, 64, 67});
    CHECK(closeVoicing(chord(9, "min")) == Notes{69, 72, 76});
    CHECK(closeVoicing(chord(0, "9")) == Notes{60, 64, 67, 70, 74});
}

TEST_CASE("closeVoicing: the default register is 60..96, the root in the middle-C octave") {
    for (const Chord& c : {chord(0, "maj"), chord(9, "min"), chord(11, "9"), chord(1, "69")}) {
        CAPTURE(c.root);
        const Notes notes = closeVoicing(c);
        CHECK(notes == closeVoicing(c, kDefaultLowest, kDefaultHighest));
        REQUIRE_FALSE(notes.empty());
        CHECK(notes.front() >= 60);
        CHECK(notes.front() <= 71);
    }
    CHECK(closeVoicing(chord(0, "maj")) != closeVoicing(chord(0, "maj"), 48, 84));
}

TEST_CASE("closeVoicing: a high root stays within the range, sorted and unique") {
    const Notes notes = closeVoicing(chord(11, "maj7"), 48, 84);
    CHECK(notes == Notes{59, 63, 66, 70});
    CHECK(sortedUniqueWithin(notes, 48, 84));
}

TEST_CASE("closeVoicing: a range narrower than two octaves falls back to the defaults") {
    CHECK(closeVoicing(chord(0, "maj"), 48, 60) == closeVoicing(chord(0, "maj"), 60, 96));
    CHECK(closeVoicing(chord(7, "9"), 48, 60) == closeVoicing(chord(7, "9"), 60, 96));  // 12 < 24
    CHECK(closeVoicing(chord(0, "maj"), 48, 71) == closeVoicing(chord(0, "maj"), 60, 96));  // 23 < 24
}

TEST_CASE("closeVoicing: a range outside MIDI falls back to the defaults") {
    CHECK(closeVoicing(chord(0, "maj"), -1, 48) == closeVoicing(chord(0, "maj"), 60, 96));
    CHECK(closeVoicing(chord(0, "maj"), 100, 128) == closeVoicing(chord(0, "maj"), 60, 96));
}

TEST_CASE("closeVoicing: an extreme or reversed range falls back to the defaults without overflowing") {
    for (const Chord& c : {chord(0, "maj"), chord(11, "9")}) {
        CAPTURE(c.root);
        const Notes expected = closeVoicing(c, 60, 96);
        CHECK(closeVoicing(c, 1, INT_MIN) == expected);  // `highest - lowest` must not be formed
        CHECK(closeVoicing(c, INT_MIN, 84) == expected);
        CHECK(closeVoicing(c, 48, INT_MAX) == expected);
        CHECK(closeVoicing(c, INT_MAX, INT_MIN) == expected);
        CHECK(closeVoicing(c, 84, 48) == expected);  // reversed
    }
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
    // B9 on 48..72: 59 63 66 69 73 -> 73 drops to 61 and sits a whole tone above the root.
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
            // The highest root is 71 and every interval is below 24, so no note exceeds 94 <= 96: nothing
            // in the dictionary folds at the default range.
            CHECK(notes.size() == type.intervals.size());
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

// --- inversionCount --------------------------------------------------------------------------

TEST_CASE("inversionCount: the number of distinct pitch classes") {
    CHECK(inversionCount(chord(0, "5")) == 2);
    CHECK(inversionCount(chord(0, "maj")) == 3);
    CHECK(inversionCount(chord(9, "min")) == 3);
    CHECK(inversionCount(chord(0, "7")) == 4);
    CHECK(inversionCount(chord(0, "add9")) == 4);  // the ninth is its own pitch class
    CHECK(inversionCount(chord(0, "9")) == 5);
}

TEST_CASE("inversionCount: every dictionary type has one chord tone per interval, on every root") {
    for (const ChordType& type : allChordTypes()) {
        for (PitchClass root = 0; root < 12; ++root) {
            CAPTURE(type.id);
            CAPTURE(root);
            CHECK(inversionCount(Chord{root, &type}) == static_cast<int>(type.intervals.size()));
            CHECK(inversionCount(Chord{root, &type}) == static_cast<int>(pitchClassSet(Chord{root, &type}).size()));
        }
    }
}

// --- voicing: plan cases ---------------------------------------------------------------------

TEST_CASE("voicing: C major in root position, first and second inversion") {
    CHECK(voicing(chord(0, "maj"), 0, 0) == Notes{60, 64, 67});
    CHECK(voicing(chord(0, "maj"), 1, 0) == Notes{64, 67, 72});
    CHECK(voicing(chord(0, "maj"), 2, 0) == Notes{67, 72, 76});
}

TEST_CASE("voicing: the octave shifts the whole voicing by twelve semitones") {
    CHECK(voicing(chord(0, "maj"), 0, -1) == Notes{48, 52, 55});
    CHECK(voicing(chord(0, "maj"), 0, 1) == Notes{72, 76, 79});
    CHECK(voicing(chord(0, "maj"), 1, -1) == Notes{52, 55, 60});
    CHECK(voicing(chord(0, "maj"), 2, 1) == Notes{79, 84, 88});
}

TEST_CASE("voicing: C7 in third inversion puts the seventh in the bass") {
    CHECK(voicing(chord(0, "7"), 3, 0) == Notes{70, 72, 76, 79});
    CHECK(voicing(chord(0, "7"), 1, 0) == Notes{64, 67, 70, 72});
    CHECK(voicing(chord(0, "7"), 2, 0) == Notes{67, 70, 72, 76});
}

TEST_CASE("voicing: a ninth chord rotates through all five chord tones, the ninth in the bass last") {
    CHECK(voicing(chord(0, "9"), 0, 0) == Notes{60, 64, 67, 70, 74});
    CHECK(voicing(chord(0, "9"), 1, 0) == Notes{64, 67, 70, 72, 74});  // the root lands under the ninth
    CHECK(voicing(chord(0, "9"), 3, 0) == Notes{70, 72, 74, 76, 79});
    // One octave would leave the root (72) under the ninth (74): it is lifted a second time.
    CHECK(voicing(chord(0, "9"), 4, 0) == Notes{74, 76, 79, 82, 84});
    CHECK(voicing(chord(0, "add9"), 3, 0) == Notes{74, 76, 79, 84});
    CHECK(voicing(chord(11, "maj9"), 4, 0) == Notes{85, 87, 90, 94, 95});  // the highest default-range voicing
    CHECK(voicing(chord(11, "maj9"), 4, 1) == Notes{97, 99, 102, 106, 107});
}

TEST_CASE("voicing: root position at octave 0 is exactly closeVoicing, for every type on every root") {
    for (const ChordType& type : allChordTypes()) {
        for (PitchClass root = 0; root < 12; ++root) {
            const Chord c{root, &type};
            CAPTURE(type.id);
            CAPTURE(root);
            CHECK(voicing(c, 0, 0) == closeVoicing(c));
            CHECK(voicing(c, 0, 0, 48, 72) == closeVoicing(c, 48, 72));
            CHECK(voicing(c, 0, 0, 12, 103) == closeVoicing(c, 12, 103));
        }
    }
}

// --- voicing: clamping -------------------------------------------------------------------------

TEST_CASE("voicing: the inversion is clamped to 0..inversionCount-1") {
    CHECK(voicing(chord(0, "maj"), 3, 0) == voicing(chord(0, "maj"), 2, 0));
    CHECK(voicing(chord(0, "maj"), 99, 0) == voicing(chord(0, "maj"), 2, 0));
    CHECK(voicing(chord(0, "maj"), -1, 0) == voicing(chord(0, "maj"), 0, 0));
    CHECK(voicing(chord(0, "maj"), INT_MIN, 0) == voicing(chord(0, "maj"), 0, 0));
    CHECK(voicing(chord(0, "maj"), INT_MAX, 0) == voicing(chord(0, "maj"), 2, 0));
    CHECK(voicing(chord(0, "7"), 4, 0) == voicing(chord(0, "7"), 3, 0));
    CHECK(voicing(chord(0, "5"), 2, 0) == voicing(chord(0, "5"), 1, 0));
    CHECK(voicing(chord(0, "5"), 1, 0) == Notes{67, 72});
}

TEST_CASE("voicing: the octave is clamped to -1..1") {
    CHECK(voicing(chord(0, "maj"), 0, 2) == voicing(chord(0, "maj"), 0, 1));
    CHECK(voicing(chord(0, "maj"), 0, -2) == voicing(chord(0, "maj"), 0, -1));
    CHECK(voicing(chord(0, "maj"), 0, INT_MAX) == Notes{72, 76, 79});
    CHECK(voicing(chord(0, "maj"), 0, INT_MIN) == Notes{48, 52, 55});
}

TEST_CASE("voicing: a range without an octave of MIDI below it or two above it falls back to the defaults") {
    // 12..103 is the widest range every octave and inversion of every chord fits in MIDI.
    CHECK(voicing(chord(0, "maj"), 0, 0, 12, 103) == Notes{12, 16, 19});
    CHECK(voicing(chord(0, "maj"), 0, -1, 12, 103) == Notes{0, 4, 7});
    CHECK(voicing(chord(0, "maj"), 2, 1, 12, 103) == Notes{31, 36, 40});
    CHECK(voicing(chord(0, "maj"), 0, 0, 11, 103) == voicing(chord(0, "maj"), 0, 0));   // 11 - 12 < 0
    CHECK(voicing(chord(0, "maj"), 0, 0, 12, 104) == voicing(chord(0, "maj"), 0, 0));   // 104 + 24 > 127
    CHECK(voicing(chord(0, "maj"), 0, 0, 0, 127) == voicing(chord(0, "maj"), 0, 0));    // valid for closeVoicing only
    CHECK(voicing(chord(11, "9"), 4, 1, 90, 115) == voicing(chord(11, "9"), 4, 1));     // 115 + 24 > 127
}

// --- voicing: range handling --------------------------------------------------------------------

TEST_CASE("voicing: honours a valid custom range and falls back to the defaults for an invalid one") {
    CHECK(voicing(chord(0, "maj"), 1, 0, 48, 84) == Notes{52, 55, 60});
    CHECK(voicing(chord(0, "maj"), 2, -1, 48, 84) == Notes{43, 48, 52});
    CHECK(voicing(chord(0, "maj"), 1, 0, 48, 60) == voicing(chord(0, "maj"), 1, 0));  // too narrow
    CHECK(voicing(chord(0, "maj"), 1, 1, 84, 48) == voicing(chord(0, "maj"), 1, 1));  // reversed
    CHECK(voicing(chord(0, "maj"), 1, -1, -1, 48) == voicing(chord(0, "maj"), 1, -1));  // outside MIDI
    CHECK(voicing(chord(11, "9"), 4, 1, INT_MIN, INT_MAX) == voicing(chord(11, "9"), 4, 1));
}

TEST_CASE("voicing: a rotated note that would leave a narrow register is kept rather than folded under the bass") {
    // B9 on 48..72 closes to 59 61 63 66 69; the fourth inversion moves the four lowest notes up an octave
    // and the bass (69) stays the bass: folding 73, 75 or 78 back down would put them under it.
    CHECK(voicing(chord(11, "9"), 4, 0, 48, 72) == Notes{69, 71, 73, 75, 78});
}

// --- voicing: properties ----------------------------------------------------------------------

TEST_CASE("voicing: every type x root x inversion x octave is sorted, unique, in register and keeps the chord") {
    for (const ChordType& type : allChordTypes()) {
        for (PitchClass root = 0; root < 12; ++root) {
            const Chord c{root, &type};
            const Notes base = closeVoicing(c);
            const int count = inversionCount(c);
            REQUIRE(count == static_cast<int>(base.size()));
            for (int inversion = 0; inversion < count; ++inversion) {
                for (int octave = -1; octave <= 1; ++octave) {
                    CAPTURE(type.id);
                    CAPTURE(root);
                    CAPTURE(inversion);
                    CAPTURE(octave);
                    const Notes notes = voicing(c, inversion, octave);
                    // The register for an octave is the default one shifted by that octave; at the
                    // default range no rotated note ever leaves it, so the union over octaves is 48..108.
                    CHECK(sortedUniqueWithin(notes, kDefaultLowest + 12 * octave, kDefaultHighest + 12 * octave));
                    CHECK(sortedUniqueWithin(notes, kDefaultLowest - 12, kDefaultHighest + 12));
                    CHECK(notes.size() == base.size());
                    CHECK(pitchClassSet(notes) == pitchClassSet(c));
                    REQUIRE_FALSE(notes.empty());
                    // The bass is chord tone `inversion`, counted up the close voicing.
                    CHECK(pitchClassOf(notes.front()) == pitchClassOf(base[static_cast<std::size_t>(inversion)]));
                }
            }
        }
    }
}

TEST_CASE("voicing: distinct inversions of one chord are distinct voicings") {
    for (const ChordType& type : allChordTypes()) {
        for (PitchClass root = 0; root < 12; ++root) {
            const Chord c{root, &type};
            const int count = inversionCount(c);
            for (int a = 0; a < count; ++a) {
                for (int b = a + 1; b < count; ++b) {
                    CAPTURE(type.id);
                    CAPTURE(root);
                    CAPTURE(a);
                    CAPTURE(b);
                    CHECK(voicing(c, a, 0) != voicing(c, b, 0));
                }
            }
        }
    }
}
