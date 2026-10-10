#include "doctest.h"

#include "chordlab/pitch.h"
#include "chordlab/relations.h"
#include "chordlab/scale.h"

#include <array>
#include <climits>
#include <cstddef>
#include <vector>

using namespace chordlab;

namespace {

Key key(char letter, int alter, Mode mode) {
    return Key{Tonic{letter, alter}, mode};
}

constexpr std::array<Mode, kNumModes> kAllModes{
    Mode::Ionian,     Mode::Dorian,  Mode::Phrygian,      Mode::Lydian,       Mode::Mixolydian,
    Mode::Aeolian,    Mode::Locrian, Mode::HarmonicMinor, Mode::MelodicMinor,
};

const Key kCIonian = key('C', 0, Mode::Ionian);

/// Every canonical key: the canonical tonic of each pitch class for each mode.
std::vector<Key> allCanonicalKeys() {
    std::vector<Key> keys;
    for (PitchClass pc = 0; pc < 12; ++pc) {
        for (const Mode mode : kAllModes) {
            keys.push_back(Key{canonicalTonic(pc, mode), mode});
        }
    }
    return keys;
}

}  // namespace

// --- fifthsNeighbour ---------------------------------------------------------------------

TEST_CASE("fifthsNeighbour: plan cases") {
    const Tonic fSharp{'F', +1};
    CHECK(fifthsNeighbour(kCIonian, +1) == key('G', 0, Mode::Ionian));
    CHECK(fifthsNeighbour(kCIonian, -1) == key('F', 0, Mode::Ionian));
    CHECK(fifthsNeighbour(key('B', 0, Mode::Ionian), +1).tonic == fSharp);  // F#, not Gb
    CHECK(fifthsNeighbour(key('A', 0, Mode::Aeolian), +1) == key('E', 0, Mode::Aeolian));
}

TEST_CASE("fifthsNeighbour: keeps the mode") {
    CHECK(fifthsNeighbour(key('D', 0, Mode::Dorian), +1) == key('A', 0, Mode::Dorian));
    CHECK(fifthsNeighbour(key('A', 0, Mode::HarmonicMinor), -1) == key('D', 0, Mode::HarmonicMinor));
    CHECK(fifthsNeighbour(key('B', 0, Mode::Locrian), +1).mode == Mode::Locrian);
}

TEST_CASE("fifthsNeighbour: steps wrap around the circle") {
    CHECK(fifthsNeighbour(kCIonian, 12) == kCIonian);
    CHECK(fifthsNeighbour(kCIonian, -12) == kCIonian);
    CHECK(fifthsNeighbour(kCIonian, 13) == key('G', 0, Mode::Ionian));
    CHECK(fifthsNeighbour(kCIonian, 6) == key('F', +1, Mode::Ionian));
    CHECK(fifthsNeighbour(kCIonian, -6) == key('F', +1, Mode::Ionian));  // the tritone's canonical spelling
    CHECK(fifthsNeighbour(kCIonian, 0) == kCIonian);
}

TEST_CASE("fifthsNeighbour: respells a non-canonical tonic canonically") {
    CHECK(fifthsNeighbour(key('G', -1, Mode::Ionian), 0) == key('F', +1, Mode::Ionian));
    CHECK(fifthsNeighbour(key('C', -1, Mode::Ionian), +1) == key('F', +1, Mode::Ionian));  // Cb -> Gb, spelled F#
}

TEST_CASE("fifthsNeighbour: every canonical key round-trips a step up and back") {
    for (const Key& k : allCanonicalKeys()) {
        CAPTURE(k.tonic.letter);
        CAPTURE(k.tonic.alter);
        CAPTURE(static_cast<int>(k.mode));
        CHECK(fifthsNeighbour(fifthsNeighbour(k, +1), -1) == k);
        CHECK(fifthsNeighbour(fifthsNeighbour(k, -1), +1) == k);
        CHECK(fifthsNeighbour(k, +1).tonicPc() == pitchClassOf(k.tonicPc() + 7));
    }
}

TEST_CASE("fifthsNeighbour: extreme step counts reduce modulo 12 without overflowing") {
    // Only `steps` modulo 12 matters, so `7 * steps` must never be formed before reducing.
    static_assert(INT_MAX % 12 == 7, "INT_MAX reduces to +7 steps");
    static_assert(INT_MIN % 12 == -8, "INT_MIN reduces to -8 steps, i.e. +4 around the circle");
    const Key bIonian = key('B', 0, Mode::Ionian);
    CHECK(fifthsNeighbour(bIonian, INT_MAX) == fifthsNeighbour(bIonian, INT_MAX % 12));
    CHECK(fifthsNeighbour(bIonian, INT_MAX) == kCIonian);  // B + 7 fifths = C
    CHECK(fifthsNeighbour(bIonian, -INT_MAX) == fifthsNeighbour(bIonian, -(INT_MAX % 12)));
    CHECK(fifthsNeighbour(bIonian, INT_MIN) == fifthsNeighbour(bIonian, INT_MIN % 12));
    CHECK(fifthsNeighbour(bIonian, INT_MIN) == fifthsNeighbour(bIonian, 4));
    CHECK(fifthsNeighbour(bIonian, INT_MIN) == key('E', -1, Mode::Ionian));  // B + 4 fifths = D#, spelled Eb
}

TEST_CASE("fifthsNeighbour: extreme step counts agree with their reduced step on every canonical key") {
    for (const Key& k : allCanonicalKeys()) {
        CAPTURE(k.tonic.letter);
        CAPTURE(k.tonic.alter);
        CAPTURE(static_cast<int>(k.mode));
        CHECK(fifthsNeighbour(k, INT_MAX) == fifthsNeighbour(k, INT_MAX % 12));
        CHECK(fifthsNeighbour(k, -INT_MAX) == fifthsNeighbour(k, -(INT_MAX % 12)));
        CHECK(fifthsNeighbour(k, INT_MIN) == fifthsNeighbour(k, INT_MIN % 12));
        CHECK(fifthsNeighbour(k, INT_MIN) == fifthsNeighbour(k, 4));
        CHECK(fifthsDistance(k, fifthsNeighbour(k, INT_MAX)) == -5);  // +7 fifths is reported as -5
        CHECK(fifthsDistance(k, fifthsNeighbour(k, INT_MIN)) == 4);
    }
}

// --- relativeKey ---------------------------------------------------------------------------

TEST_CASE("relativeKey: plan cases") {
    CHECK(relativeKey(kCIonian) == key('A', 0, Mode::Aeolian));
    CHECK(relativeKey(key('A', 0, Mode::Aeolian)) == kCIonian);
    CHECK(relativeKey(key('E', -1, Mode::Ionian)) == key('C', 0, Mode::Aeolian));
}

TEST_CASE("relativeKey: the relative minor of a sharp-side major is spelled with sharps") {
    CHECK(relativeKey(key('F', +1, Mode::Ionian)) == key('D', +1, Mode::Aeolian));
    CHECK(relativeKey(key('D', +1, Mode::Aeolian)) == key('F', +1, Mode::Ionian));
    CHECK(relativeKey(key('D', -1, Mode::Ionian)) == key('B', -1, Mode::Aeolian));
}

TEST_CASE("relativeKey: modes other than Ionian and Aeolian are returned unchanged") {
    CHECK(relativeKey(key('D', 0, Mode::Dorian)) == key('D', 0, Mode::Dorian));
    CHECK(relativeKey(key('A', 0, Mode::HarmonicMinor)) == key('A', 0, Mode::HarmonicMinor));
    CHECK(relativeKey(key('B', 0, Mode::Locrian)) == key('B', 0, Mode::Locrian));
}

TEST_CASE("relativeKey: Ionian and Aeolian round-trip across every canonical key") {
    for (const Key& k : allCanonicalKeys()) {
        if (k.mode != Mode::Ionian && k.mode != Mode::Aeolian) {
            continue;
        }
        CAPTURE(k.tonic.letter);
        CAPTURE(k.tonic.alter);
        CAPTURE(static_cast<int>(k.mode));
        const Key r = relativeKey(k);
        CHECK(r.mode != k.mode);
        CHECK(relativeKey(r) == k);
        CHECK(signatureOf(r) == signatureOf(k));  // relative keys share a signature
    }
}

// --- fifthsDistance -------------------------------------------------------------------------

TEST_CASE("fifthsDistance: plan cases") {
    CHECK(fifthsDistance(kCIonian, key('E', 0, Mode::Ionian)) == 4);
    CHECK(fifthsDistance(kCIonian, key('F', 0, Mode::Ionian)) == -1);
    CHECK(fifthsDistance(kCIonian, key('F', +1, Mode::Ionian)) == 6);
    CHECK(fifthsDistance(kCIonian, key('G', -1, Mode::Ionian)) == 6);  // the tritone is always +6
}

TEST_CASE("fifthsDistance: is antisymmetric except at the tritone") {
    CHECK(fifthsDistance(key('E', 0, Mode::Ionian), kCIonian) == -4);
    CHECK(fifthsDistance(key('F', 0, Mode::Ionian), kCIonian) == 1);
    CHECK(fifthsDistance(key('F', +1, Mode::Ionian), kCIonian) == 6);
}

TEST_CASE("fifthsDistance: compares tonic pitch classes only, ignoring mode and spelling") {
    CHECK(fifthsDistance(kCIonian, key('A', 0, Mode::Aeolian)) == 3);
    CHECK(fifthsDistance(key('A', 0, Mode::Aeolian), key('E', 0, Mode::Dorian)) == 1);
    CHECK(fifthsDistance(key('F', +1, Mode::Ionian), key('G', -1, Mode::Ionian)) == 0);
}

TEST_CASE("fifthsDistance: every canonical key is at distance zero from itself") {
    for (const Key& k : allCanonicalKeys()) {
        CAPTURE(k.tonic.letter);
        CAPTURE(k.tonic.alter);
        CAPTURE(static_cast<int>(k.mode));
        CHECK(fifthsDistance(k, k) == 0);
    }
}

TEST_CASE("fifthsDistance: recovers the step count of fifthsNeighbour") {
    for (const Key& k : allCanonicalKeys()) {
        for (int n = -6; n <= 6; ++n) {
            CAPTURE(k.tonic.letter);
            CAPTURE(k.tonic.alter);
            CAPTURE(static_cast<int>(k.mode));
            CAPTURE(n);
            const int expected = n == -6 ? 6 : n;  // -6 and +6 are the same key; +6 is reported
            CHECK(fifthsDistance(k, fifthsNeighbour(k, n)) == expected);
        }
    }
}

// --- parallelModes --------------------------------------------------------------------------

TEST_CASE("parallelModes: one key per mode, in enum order, on the same tonic pitch class") {
    const std::vector<Key> modes = parallelModes(kCIonian);
    REQUIRE(modes.size() == static_cast<std::size_t>(kNumModes));
    for (std::size_t i = 0; i < modes.size(); ++i) {
        CAPTURE(i);
        CHECK(modes[i].mode == kAllModes[i]);
        CHECK(modes[i].tonicPc() == 0);
        CHECK(modes[i].tonic == canonicalTonic(0, kAllModes[i]));
    }
}

TEST_CASE("parallelModes: spells each tonic canonically for its mode") {
    const std::vector<Key> modes = parallelModes(key('E', -1, Mode::Ionian));
    REQUIRE(modes.size() == static_cast<std::size_t>(kNumModes));
    CHECK(modes[static_cast<std::size_t>(Mode::Ionian)] == key('E', -1, Mode::Ionian));
    CHECK(modes[static_cast<std::size_t>(Mode::Dorian)] == key('E', -1, Mode::Dorian));
    CHECK(modes[static_cast<std::size_t>(Mode::Phrygian)] == key('D', +1, Mode::Phrygian));  // Eb Phrygian is -7
    CHECK(modes[static_cast<std::size_t>(Mode::Aeolian)] == key('D', +1, Mode::Aeolian));    // tie: sharp wins
    CHECK(modes[static_cast<std::size_t>(Mode::Locrian)] == key('D', +1, Mode::Locrian));
}

TEST_CASE("parallelModes: is the same list for any key on that tonic pitch class") {
    for (const Key& k : allCanonicalKeys()) {
        CAPTURE(k.tonic.letter);
        CAPTURE(k.tonic.alter);
        CAPTURE(static_cast<int>(k.mode));
        const std::vector<Key> modes = parallelModes(k);
        REQUIRE(modes.size() == static_cast<std::size_t>(kNumModes));
        CHECK(modes == parallelModes(Key{canonicalTonic(k.tonicPc(), Mode::Ionian), Mode::Ionian}));
        CHECK(modes[static_cast<std::size_t>(k.mode)] == k);  // a canonical key appears as itself
        for (const Key& m : modes) {
            CHECK(m.tonicPc() == k.tonicPc());
        }
    }
}
