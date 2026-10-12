#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/harmony.h"
#include "chordlab/pitch.h"
#include "chordlab/relations.h"
#include "chordlab/scale.h"
#include "chordlab/voicing.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

using namespace chordlab;

namespace {

Key key(char letter, int alter, Mode mode) {
    return Key{Tonic{letter, alter}, mode};
}

Chord chord(PitchClass root, std::string_view typeId) {
    return Chord{root, &chordType(typeId)};
}

using Notes = std::vector<int>;
using Pcs = std::vector<PitchClass>;

const Key kCIonian = key('C', 0, Mode::Ionian);
const Key kCAeolian = key('C', 0, Mode::Aeolian);
const Key kAAeolian = key('A', 0, Mode::Aeolian);

constexpr std::array<RootMotion, 10> kAllMotions{
    RootMotion::Same,    RootMotion::DownFifth, RootMotion::UpFifth, RootMotion::StepUp,     RootMotion::StepDown,
    RootMotion::ThirdUp, RootMotion::ThirdDown, RootMotion::Tritone, RootMotion::SemitoneUp, RootMotion::SemitoneDown,
};

/// The search order of `borrowedFrom`, mirrored here so the property test does not depend on the table
/// under test: minor family first, then the other modes, the parallel major last.
constexpr std::array<Mode, kNumModes> kBorrowOrder{
    Mode::Aeolian,       Mode::Dorian,       Mode::Mixolydian, Mode::Lydian, Mode::Phrygian,
    Mode::HarmonicMinor, Mode::MelodicMinor, Mode::Locrian,    Mode::Ionian,
};

/// The motion of the reversed pair: up becomes down; Same and Tritone are their own reverse.
RootMotion reversed(RootMotion m) {
    switch (m) {
        case RootMotion::Same: return RootMotion::Same;
        case RootMotion::DownFifth: return RootMotion::UpFifth;
        case RootMotion::UpFifth: return RootMotion::DownFifth;
        case RootMotion::StepUp: return RootMotion::StepDown;
        case RootMotion::StepDown: return RootMotion::StepUp;
        case RootMotion::ThirdUp: return RootMotion::ThirdDown;
        case RootMotion::ThirdDown: return RootMotion::ThirdUp;
        case RootMotion::Tritone: return RootMotion::Tritone;
        case RootMotion::SemitoneUp: return RootMotion::SemitoneDown;
        case RootMotion::SemitoneDown: return RootMotion::SemitoneUp;
    }
    return RootMotion::Same;
}

/// Every dictionary chord on every root.
std::vector<Chord> allChords() {
    std::vector<Chord> chords;
    for (const ChordType& type : allChordTypes()) {
        for (PitchClass root = 0; root < 12; ++root) {
            chords.push_back(Chord{root, &type});
        }
    }
    return chords;
}

/// Every dictionary chord on C.
std::vector<Chord> allChordsOnC() {
    std::vector<Chord> chords;
    for (const ChordType& type : allChordTypes()) {
        chords.push_back(Chord{0, &type});
    }
    return chords;
}

/// Total displacement after sorting both voicings. For two same-size voicings on a line, the
/// monotone (sorted-to-sorted) matching is an optimal bijection: uncrossing any two crossed pairs never
/// increases |a1-b1| + |a2-b2|. So this is an independent oracle for the brute-force search.
int sortedMatchingCost(Notes a, Notes b) {
    REQUIRE(a.size() == b.size());
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    int cost = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        cost += std::abs(a[i] - b[i]);
    }
    return cost;
}

std::set<PitchClass> pitchClassSet(const Chord& c) {
    const Pcs pcs = pitchClasses(c);
    return std::set<PitchClass>(pcs.begin(), pcs.end());
}

}  // namespace

// --- rootMotion ------------------------------------------------------------------------------

TEST_CASE("rootMotion: every interval up from C maps to its motion class") {
    const std::array<RootMotion, 12> expected{
        RootMotion::Same,         // 0
        RootMotion::SemitoneUp,   // 1
        RootMotion::StepUp,       // 2
        RootMotion::ThirdUp,      // 3  minor third up
        RootMotion::ThirdUp,      // 4  major third up
        RootMotion::DownFifth,    // 5  up a fourth = down a fifth
        RootMotion::Tritone,      // 6
        RootMotion::UpFifth,      // 7
        RootMotion::ThirdDown,    // 8  major third down
        RootMotion::ThirdDown,    // 9  minor third down
        RootMotion::StepDown,     // 10
        RootMotion::SemitoneDown  // 11
    };
    for (int interval = 0; interval < 12; ++interval) {
        CAPTURE(interval);
        CHECK(rootMotion(0, interval) == expected[static_cast<std::size_t>(interval)]);
    }
}

TEST_CASE("rootMotion: plan cases") {
    CHECK(rootMotion(0, 7) == RootMotion::UpFifth);      // C -> G
    CHECK(rootMotion(7, 0) == RootMotion::DownFifth);    // G -> C
    CHECK(rootMotion(0, 5) == RootMotion::DownFifth);    // C -> F (up a fourth)
    CHECK(rootMotion(0, 2) == RootMotion::StepUp);       // C -> D
    CHECK(rootMotion(0, 10) == RootMotion::StepDown);    // C -> Bb
    CHECK(rootMotion(0, 4) == RootMotion::ThirdUp);      // C -> E
    CHECK(rootMotion(0, 3) == RootMotion::ThirdUp);      // C -> Eb
    CHECK(rootMotion(0, 9) == RootMotion::ThirdDown);    // C -> Am
    CHECK(rootMotion(0, 8) == RootMotion::ThirdDown);    // C -> Ab
    CHECK(rootMotion(0, 6) == RootMotion::Tritone);      // C -> F#
    CHECK(rootMotion(0, 1) == RootMotion::SemitoneUp);   // C -> Db
    CHECK(rootMotion(0, 11) == RootMotion::SemitoneDown); // C -> B
    CHECK(rootMotion(0, 0) == RootMotion::Same);
    CHECK(rootMotion(9, 2) == RootMotion::DownFifth);    // A -> D
    CHECK(rootMotion(2, 7) == RootMotion::DownFifth);    // D -> G
}

TEST_CASE("rootMotion: depends only on the interval between the roots") {
    for (PitchClass from = 0; from < 12; ++from) {
        for (PitchClass to = 0; to < 12; ++to) {
            CAPTURE(from);
            CAPTURE(to);
            CHECK(rootMotion(from, to) == rootMotion(0, pitchClassOf(to - from)));
        }
    }
}

TEST_CASE("rootMotion: swapping the chords reverses the direction") {
    for (PitchClass from = 0; from < 12; ++from) {
        for (PitchClass to = 0; to < 12; ++to) {
            CAPTURE(from);
            CAPTURE(to);
            CHECK(rootMotion(to, from) == reversed(rootMotion(from, to)));
        }
    }
}

TEST_CASE("rootMotion: reduces out-of-range roots modulo 12") {
    CHECK(rootMotion(12, 19) == RootMotion::UpFifth);
    CHECK(rootMotion(-1, 0) == RootMotion::SemitoneUp);
    CHECK(rootMotion(0, -5) == RootMotion::UpFifth);  // -5 is G
}

// --- rootMotionName --------------------------------------------------------------------------

TEST_CASE("rootMotionName: one lowercase phrase per motion") {
    CHECK(std::string_view(rootMotionName(RootMotion::Same)) == "same root");
    CHECK(std::string_view(rootMotionName(RootMotion::DownFifth)) == "down a fifth");
    CHECK(std::string_view(rootMotionName(RootMotion::UpFifth)) == "up a fifth");
    CHECK(std::string_view(rootMotionName(RootMotion::StepUp)) == "up a step");
    CHECK(std::string_view(rootMotionName(RootMotion::StepDown)) == "down a step");
    CHECK(std::string_view(rootMotionName(RootMotion::ThirdUp)) == "up a third");
    CHECK(std::string_view(rootMotionName(RootMotion::ThirdDown)) == "down a third");
    CHECK(std::string_view(rootMotionName(RootMotion::Tritone)) == "tritone");
    CHECK(std::string_view(rootMotionName(RootMotion::SemitoneUp)) == "up a semitone");
    CHECK(std::string_view(rootMotionName(RootMotion::SemitoneDown)) == "down a semitone");
}

TEST_CASE("rootMotionName: every motion has a distinct non-empty name") {
    std::set<std::string> names;
    for (const RootMotion m : kAllMotions) {
        CAPTURE(static_cast<int>(m));
        const std::string name = rootMotionName(m);
        CHECK_FALSE(name.empty());
        names.insert(name);
    }
    CHECK(names.size() == kAllMotions.size());
}

// --- voiceLeadingCost ------------------------------------------------------------------------

TEST_CASE("voiceLeadingCost: plan cases") {
    CHECK(voiceLeadingCost({60, 64, 67}, {60, 65, 69}) == 3);  // C -> F: 60->60, 64->65, 67->69
    CHECK(voiceLeadingCost({60, 64, 67}, {59, 62, 67}) == 3);  // C -> G/B: 60->59, 64->62, 67->67
}

TEST_CASE("voiceLeadingCost: identical voicings cost nothing") {
    CHECK(voiceLeadingCost({60, 64, 67}, {60, 64, 67}) == 0);
    CHECK(voiceLeadingCost({60}, {60}) == 0);
    for (const Chord& c : allChords()) {
        CAPTURE(c.type->id);
        CAPTURE(c.root);
        CHECK(voiceLeadingCost(closeVoicing(c), closeVoicing(c)) == 0);
    }
}

TEST_CASE("voiceLeadingCost: finds the cheapest bijection, not the index-wise pairing") {
    CHECK(voiceLeadingCost({60, 64, 67}, {67, 60, 64}) == 0);  // a permutation of the same notes
    CHECK(voiceLeadingCost({60, 64, 67}, {69, 65, 60}) == 3);  // index-wise would be 9 + 1 + 7
    CHECK(voiceLeadingCost({67, 64, 60}, {60, 65, 69}) == 3);
}

TEST_CASE("voiceLeadingCost: the order of the notes within a voicing does not matter") {
    const Notes c{60, 64, 67};
    const Notes f{60, 65, 69};
    CHECK(voiceLeadingCost({67, 60, 64}, f) == voiceLeadingCost(c, f));
    CHECK(voiceLeadingCost(c, {69, 60, 65}) == voiceLeadingCost(c, f));
}

TEST_CASE("voiceLeadingCost: a shorter voicing is padded with its lowest note an octave up") {
    // C {60 64 67} vs G7 {67 71 74 77}: C becomes {60 64 67 72}; the cheapest bijection is
    // 60->67 (7), 64->71 (7), 67->74 (7), 72->77 (5) = 26.
    CHECK(voiceLeadingCost({60, 64, 67}, {67, 71, 74, 77}) == 26);
    CHECK(voiceLeadingCost({67, 71, 74, 77}, {60, 64, 67}) == 26);  // whichever side is shorter
    // C vs C7 {60 64 67 70}: the padded 72 moves to 70.
    CHECK(voiceLeadingCost({60, 64, 67}, {60, 64, 67, 70}) == 2);
    // C vs Cmaj7 {60 64 67 71}: 72 moves to 71.
    CHECK(voiceLeadingCost({60, 64, 67}, {60, 64, 67, 71}) == 1);
    // The pad is the lowest note's octave whatever the input order.
    CHECK(voiceLeadingCost({67, 60, 64}, {60, 64, 67, 71}) == 1);
    // A voicing whose lowest note is not the root still pads from its lowest note: {64 67 72} + 76.
    CHECK(voiceLeadingCost({64, 67, 72}, {64, 67, 72, 76}) == 0);
}

TEST_CASE("voiceLeadingCost: pads with as many octave doublings as the size difference") {
    // C5 {60 67} vs C9 {60 64 67 70 74}: C5 becomes {60 67 72 72 72};
    // 60->60 (0), 67->67 (0), 72->64 (8), 72->70 (2), 72->74 (2) = 12.
    CHECK(voiceLeadingCost({60, 67}, {60, 64, 67, 70, 74}) == 12);
    CHECK(voiceLeadingCost({60, 64, 67, 70, 74}, {60, 67}) == 12);
    CHECK(voiceLeadingCost({60}, {60, 72, 72}) == 0);
}

TEST_CASE("voiceLeadingCost: is symmetric for every pair of dictionary voicings") {
    const std::vector<Chord> onC = allChordsOnC();
    const std::vector<Chord> all = allChords();
    for (const Chord& a : onC) {
        for (const Chord& b : all) {
            CAPTURE(a.type->id);
            CAPTURE(b.type->id);
            CAPTURE(b.root);
            const Notes va = closeVoicing(a);
            const Notes vb = closeVoicing(b);
            const int cost = voiceLeadingCost(va, vb);
            CHECK(cost >= 0);
            CHECK(voiceLeadingCost(vb, va) == cost);
        }
    }
}

TEST_CASE("voiceLeadingCost: agrees with the sorted matching for same-size voicings") {
    const std::vector<Chord> onC = allChordsOnC();
    const std::vector<Chord> all = allChords();
    for (const Chord& a : onC) {
        for (const Chord& b : all) {
            const Notes va = closeVoicing(a);
            const Notes vb = closeVoicing(b);
            if (va.size() != vb.size()) {
                continue;
            }
            CAPTURE(a.type->id);
            CAPTURE(b.type->id);
            CAPTURE(b.root);
            CHECK(voiceLeadingCost(va, vb) == sortedMatchingCost(va, vb));
        }
    }
}

// --- borrowedFrom -----------------------------------------------------------------------------

TEST_CASE("borrowedFrom: a diatonic chord is not borrowed") {
    CHECK_FALSE(borrowedFrom(chord(7, "maj"), kCIonian).has_value());   // G
    CHECK_FALSE(borrowedFrom(chord(7, "7"), kCIonian).has_value());     // G7
    CHECK_FALSE(borrowedFrom(chord(0, "maj"), kCIonian).has_value());   // C
    CHECK_FALSE(borrowedFrom(chord(2, "m7"), kCIonian).has_value());    // Dm7
    CHECK_FALSE(borrowedFrom(chord(11, "dim"), kCIonian).has_value());  // B°
    CHECK_FALSE(borrowedFrom(chord(0, "maj"), kAAeolian).has_value());  // C in A minor
    CHECK_FALSE(borrowedFrom(chord(4, "min"), kAAeolian).has_value());  // Em in A minor
}

TEST_CASE("borrowedFrom: plan cases in C major") {
    CHECK(borrowedFrom(chord(8, "maj"), kCIonian) == kCAeolian);                    // Ab: bVI
    CHECK(borrowedFrom(chord(6, "dim"), kCIonian) == key('C', 0, Mode::Lydian));    // F#°: #iv°
    CHECK(borrowedFrom(chord(1, "maj"), kCIonian) == key('C', 0, Mode::Phrygian));  // Db: bII
    CHECK(borrowedFrom(chord(5, "min"), kCIonian) == kCAeolian);                    // Fm: iv
    CHECK(borrowedFrom(chord(3, "maj"), kCIonian) == kCAeolian);                    // Eb: bIII
}

TEST_CASE("borrowedFrom: Bb in C major is Aeolian, which precedes Mixolydian in the search") {
    // Bb D F is in both C Aeolian and C Mixolydian; the minor family is tried first.
    CHECK(borrowedFrom(chord(10, "maj"), kCIonian) == kCAeolian);
    // Bb with a major seventh (Bb D F A) is not in Aeolian (no A); Dorian has it.
    CHECK(borrowedFrom(chord(10, "maj7"), kCIonian) == key('C', 0, Mode::Dorian));
    // C7 (C E G Bb) is in neither Aeolian nor Dorian (no E); Mixolydian has it.
    CHECK(borrowedFrom(chord(0, "7"), kCIonian) == key('C', 0, Mode::Mixolydian));
}

TEST_CASE("borrowedFrom: a chord in no parallel mode is not attributed") {
    CHECK_FALSE(borrowedFrom(chord(1, "dim7"), kCIonian).has_value());  // C#°7: C# E G Bb
    CHECK_FALSE(borrowedFrom(chord(4, "maj"), kCIonian).has_value());   // E: E G# B
    CHECK_FALSE(borrowedFrom(chord(0, "aug"), kCIonian).has_value());   // C+: C E G#
    CHECK_FALSE(borrowedFrom(chord(6, "7"), kCIonian).has_value());     // F#7: F# A# C# E
}

TEST_CASE("borrowedFrom: the minor scales and Locrian are reached after the diatonic modes") {
    // G#°7 (G# B D F): no diatonic mode on C has both G# and B; harmonic minor does.
    CHECK(borrowedFrom(chord(8, "dim7"), kCIonian) == key('C', 0, Mode::HarmonicMinor));
    // Cm(maj7) (C Eb G B): harmonic minor, which precedes melodic minor.
    CHECK(borrowedFrom(chord(0, "mMaj7"), kCIonian) == key('C', 0, Mode::HarmonicMinor));
    // F# major (F# A# C#, i.e. Gb Bb Db): only C Locrian holds all three.
    CHECK(borrowedFrom(chord(6, "maj"), kCIonian) == key('C', 0, Mode::Locrian));
    // In C minor, G7 (G B D F) comes from harmonic minor and Bm7b5 (B D F A) from melodic minor.
    CHECK(borrowedFrom(chord(7, "7"), kCAeolian) == key('C', 0, Mode::HarmonicMinor));
    CHECK(borrowedFrom(chord(11, "m7b5"), kCAeolian) == key('C', 0, Mode::MelodicMinor));
    CHECK(borrowedFrom(chord(7, "9"), kCAeolian) == key('C', 0, Mode::MelodicMinor));
}

TEST_CASE("borrowedFrom: the search order is fixed regardless of the project mode") {
    // The order is the same for every project mode (only the project mode itself is skipped), so with a
    // minor project key the parallel major is tried last and chords it shares with other modes are
    // attributed to those: C major in C Aeolian is Mixolydian, G major in C Aeolian is Lydian (not
    // harmonic minor, which comes later), and E major in A Aeolian is A Lydian.
    CHECK(borrowedFrom(chord(0, "maj"), kCAeolian) == key('C', 0, Mode::Mixolydian));
    CHECK(borrowedFrom(chord(7, "maj"), kCAeolian) == key('C', 0, Mode::Lydian));
    CHECK(borrowedFrom(chord(4, "maj"), kAAeolian) == key('A', 0, Mode::Lydian));
    CHECK_FALSE(borrowedFrom(chord(9, "min"), kAAeolian).has_value());  // diatonic: the project mode wins
}

TEST_CASE("borrowedFrom: spells the parallel mode canonically") {
    const Key ebIonian = key('E', -1, Mode::Ionian);
    // B major (Cb Eb Gb) is bVI of Eb: Eb Aeolian ties with D# Aeolian at six accidentals; sharp wins.
    CHECK(borrowedFrom(chord(11, "maj"), ebIonian) == key('D', +1, Mode::Aeolian));
    // E major (Fb Ab Cb) is bII of Eb: Eb Phrygian would need seven flats, so D# Phrygian.
    CHECK(borrowedFrom(chord(4, "maj"), ebIonian) == key('D', +1, Mode::Phrygian));
    // A non-canonical project key still yields a canonical answer: D major in Gb major is bVI.
    CHECK(borrowedFrom(chord(2, "maj"), key('G', -1, Mode::Ionian)) == key('F', +1, Mode::Aeolian));
}

TEST_CASE("borrowedFrom: every answer is the first containing mode in the fixed order") {
    const std::array<Key, 5> projectKeys{kCIonian, kAAeolian, key('D', 0, Mode::Dorian), key('E', -1, Mode::Ionian),
                                         key('F', +1, Mode::Mixolydian)};
    for (const Key& projectKey : projectKeys) {
        const std::vector<Key> modes = parallelModes(projectKey);
        for (const Chord& c : allChords()) {
            CAPTURE(projectKey.tonic.letter);
            CAPTURE(static_cast<int>(projectKey.mode));
            CAPTURE(c.type->id);
            CAPTURE(c.root);
            const std::optional<Key> result = borrowedFrom(c, projectKey);
            if (fullyInKey(c, projectKey)) {
                CHECK_FALSE(result.has_value());
                continue;
            }
            std::optional<Key> expected;
            for (const Mode mode : kBorrowOrder) {
                const Key& candidate = modes[static_cast<std::size_t>(mode)];
                if (mode != projectKey.mode && fullyInKey(c, candidate)) {
                    expected = candidate;
                    break;
                }
            }
            CHECK(result == expected);
            if (result) {
                CHECK(fullyInKey(c, *result));
                CHECK(result->tonicPc() == projectKey.tonicPc());
                CHECK(result->mode != projectKey.mode);
                CHECK(result->tonic == canonicalTonic(projectKey.tonicPc(), result->mode));
            }
        }
    }
}

// --- relate --------------------------------------------------------------------------------------

TEST_CASE("relate: C to G7 in C major") {
    const Relation r = relate(chord(0, "maj"), chord(7, "7"), kCIonian);
    CHECK(r.commonTones == 1);
    CHECK(r.commonPcs == Pcs{7});
    CHECK(r.motion == RootMotion::UpFifth);
    CHECK(r.voiceLeadingCost == 26);  // {60 64 67} padded with 72 against {67 71 74 77}
    CHECK_FALSE(r.borrowedFrom.has_value());
}

TEST_CASE("relate: G7 to C in C major") {
    const Relation r = relate(chord(7, "7"), chord(0, "maj"), kCIonian);
    CHECK(r.commonTones == 1);
    CHECK(r.commonPcs == Pcs{7});
    CHECK(r.motion == RootMotion::DownFifth);
    CHECK(r.voiceLeadingCost == 26);
    CHECK_FALSE(r.borrowedFrom.has_value());
}

TEST_CASE("relate: C to Am shares two tones") {
    const Relation r = relate(chord(0, "maj"), chord(9, "min"), kCIonian);
    CHECK(r.commonTones == 2);
    CHECK(r.commonPcs == Pcs{0, 4});
    CHECK(r.motion == RootMotion::ThirdDown);
    CHECK(r.voiceLeadingCost == 26);  // {60 64 67} -> {69 72 76}: every note moves up, 217 - 191
    CHECK_FALSE(r.borrowedFrom.has_value());
}

TEST_CASE("relate: C to Ab in C major is borrowed from C Aeolian") {
    const Relation r = relate(chord(0, "maj"), chord(8, "maj"), kCIonian);
    CHECK(r.commonTones == 1);
    CHECK(r.commonPcs == Pcs{0});
    CHECK(r.motion == RootMotion::ThirdDown);
    CHECK(r.voiceLeadingCost == 24);  // {60 64 67} -> {68 72 75}: 215 - 191
    CHECK(r.borrowedFrom == kCAeolian);
}

TEST_CASE("relate: borrowedFrom describes the destination chord only") {
    CHECK_FALSE(relate(chord(8, "maj"), chord(0, "maj"), kCIonian).borrowedFrom.has_value());
    CHECK(relate(chord(8, "maj"), chord(1, "maj"), kCIonian).borrowedFrom == key('C', 0, Mode::Phrygian));
    CHECK(relate(chord(0, "maj"), chord(6, "dim"), kCIonian).borrowedFrom == key('C', 0, Mode::Lydian));
}

TEST_CASE("relate: the voice-leading cost is that of the default close voicings") {
    const std::array<Chord, 4> chords{chord(0, "maj"), chord(7, "7"), chord(2, "m7"), chord(10, "9")};
    for (const Chord& a : chords) {
        for (const Chord& b : chords) {
            CAPTURE(a.type->id);
            CAPTURE(b.type->id);
            CHECK(relate(a, b, kCIonian).voiceLeadingCost == voiceLeadingCost(closeVoicing(a), closeVoicing(b)));
        }
    }
}

TEST_CASE("relate: common pitch classes are the ascending intersection") {
    CHECK(relate(chord(7, "7"), chord(2, "m7"), kCIonian).commonPcs == Pcs{2, 5});   // G B D F vs D F A C
    CHECK(relate(chord(0, "maj"), chord(0, "maj"), kCIonian).commonPcs == Pcs{0, 4, 7});
    CHECK(relate(chord(0, "maj"), chord(6, "maj"), kCIonian).commonPcs == Pcs{});    // C E G vs F# A# C#
    CHECK(relate(chord(0, "maj"), chord(6, "maj"), kCIonian).commonTones == 0);
    CHECK(relate(chord(9, "min"), chord(0, "maj7"), kCIonian).commonPcs == Pcs{0, 4});  // A C E vs C E G B
    CHECK(relate(chord(0, "maj"), chord(0, "maj"), kCIonian).motion == RootMotion::Same);
    CHECK(relate(chord(0, "maj"), chord(0, "maj"), kCIonian).voiceLeadingCost == 0);
}

TEST_CASE("relate: common tones and cost are symmetric, and common tones match the pitch-class sets") {
    const std::vector<Chord> onC = allChordsOnC();
    const std::vector<Chord> all = allChords();
    for (const Chord& a : onC) {
        for (const Chord& b : all) {
            CAPTURE(a.type->id);
            CAPTURE(b.type->id);
            CAPTURE(b.root);
            const Relation ab = relate(a, b, kCIonian);
            const Relation ba = relate(b, a, kCIonian);
            CHECK(ab.commonTones == ba.commonTones);
            CHECK(ab.commonPcs == ba.commonPcs);
            CHECK(ab.voiceLeadingCost == ba.voiceLeadingCost);
            CHECK(ab.motion == reversed(ba.motion));
            CHECK(ab.commonTones == static_cast<int>(ab.commonPcs.size()));
            CHECK(std::is_sorted(ab.commonPcs.begin(), ab.commonPcs.end()));
            std::set<PitchClass> expected;
            const std::set<PitchClass> sa = pitchClassSet(a);
            for (const PitchClass pc : pitchClassSet(b)) {
                if (sa.count(pc) != 0) {
                    expected.insert(pc);
                }
            }
            CHECK(std::set<PitchClass>(ab.commonPcs.begin(), ab.commonPcs.end()) == expected);
            CHECK(ab.borrowedFrom == borrowedFrom(b, kCIonian));
        }
    }
}
