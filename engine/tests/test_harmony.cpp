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

using Pcs = std::vector<PitchClass>;

const Key kCIonian = key('C', 0, Mode::Ionian);
const Key kCAeolian = key('C', 0, Mode::Aeolian);
const Key kAAeolian = key('A', 0, Mode::Aeolian);

constexpr std::array<RootMotion, 10> kAllMotions{
    RootMotion::Same,    RootMotion::DownFifth, RootMotion::UpFifth, RootMotion::StepUp,     RootMotion::StepDown,
    RootMotion::ThirdUp, RootMotion::ThirdDown, RootMotion::Tritone, RootMotion::SemitoneUp, RootMotion::SemitoneDown,
};

/// The search orders of `borrowedFrom`, mirrored here so the property test does not depend on the tables
/// under test. A major-family project key (Ionian, Lydian, Mixolydian) tries the parallel minor first;
/// every other project key tries the minor scales, then the parallel major.
constexpr std::array<Mode, kNumModes> kMajorFamilyBorrowOrder{
    Mode::Aeolian,  Mode::Mixolydian,    Mode::Lydian,       Mode::Dorian,  Mode::Phrygian,
    Mode::HarmonicMinor, Mode::MelodicMinor, Mode::Locrian, Mode::Ionian,
};
constexpr std::array<Mode, kNumModes> kMinorFamilyBorrowOrder{
    Mode::HarmonicMinor, Mode::MelodicMinor, Mode::Ionian,   Mode::Aeolian, Mode::Dorian,
    Mode::Mixolydian,    Mode::Phrygian,     Mode::Lydian,   Mode::Locrian,
};

const std::array<Mode, kNumModes>& borrowOrderFor(Mode projectMode) {
    const bool majorFamily =
        projectMode == Mode::Ionian || projectMode == Mode::Lydian || projectMode == Mode::Mixolydian;
    return majorFamily ? kMajorFamilyBorrowOrder : kMinorFamilyBorrowOrder;
}

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

// --- pcVoiceLeadingCost ----------------------------------------------------------------------

TEST_CASE("pcVoiceLeadingCost: plan cases") {
    CHECK(pcVoiceLeadingCost({0, 4, 7}, {5, 9, 0}) == 3);      // C -> F: C stays, E->F 1, G->A 2
    CHECK(pcVoiceLeadingCost({0, 4, 7}, {7, 11, 2}) == 3);     // C -> G: G stays, C->B 1, E->D 2
    CHECK(pcVoiceLeadingCost({0, 4, 7}, {9, 0, 4}) == 2);      // C -> Am: C and E stay, G->A 2
    CHECK(pcVoiceLeadingCost({0, 4, 7}, {8, 0, 3}) == 2);      // C -> Ab: C stays, E->Eb 1, G->Ab 1
    CHECK(pcVoiceLeadingCost({0, 4, 7}, {1, 5, 8}) == 3);      // C -> Db: every voice up a semitone
    CHECK(pcVoiceLeadingCost({0, 4, 7}, {7, 11, 2, 5}) == 4);  // C -> G7: {0 4 7 0}; G stays, C->B 1, E->F 1, C->D 2
}

TEST_CASE("pcVoiceLeadingCost: identical sets cost nothing, whatever the order") {
    CHECK(pcVoiceLeadingCost({0, 4, 7}, {0, 4, 7}) == 0);
    CHECK(pcVoiceLeadingCost({0, 4, 7}, {7, 0, 4}) == 0);
    for (const Chord& c : allChords()) {
        CAPTURE(c.type->id);
        CAPTURE(c.root);
        CHECK(pcVoiceLeadingCost(pitchClasses(c), pitchClasses(c)) == 0);
    }
}

TEST_CASE("pcVoiceLeadingCost: distances are circular") {
    CHECK(pcVoiceLeadingCost({11}, {0}) == 1);
    CHECK(pcVoiceLeadingCost({0}, {11}) == 1);
    CHECK(pcVoiceLeadingCost({0}, {6}) == 6);
    CHECK(pcVoiceLeadingCost({0}, {7}) == 5);
    CHECK(pcVoiceLeadingCost({1}, {10}) == 3);
    CHECK(pcVoiceLeadingCost({0, 4, 7}, {11, 3, 6}) == 3);  // C -> B: every voice down a semitone
    CHECK(pcVoiceLeadingCost({12, 16, 19}, {0, 4, 7}) == 0);  // reduced modulo 12
}

TEST_CASE("pcVoiceLeadingCost: is register-free where the MIDI cost is not") {
    const Chord c = chord(0, "maj");
    const Chord am = chord(9, "min");
    CHECK(pcVoiceLeadingCost(pitchClasses(c), pitchClasses(am)) == 2);
    CHECK(voiceLeadingCost(closeVoicing(c), closeVoicing(am)) == 26);  // Am sits a sixth above C
}

TEST_CASE("pcVoiceLeadingCost: the shorter set is padded by cycling through its pitch classes in order") {
    // C5 {0 7} vs C9 {0 4 7 10 2}: C5 becomes {0 7 0 7 0}; C and G stay, G->E 3, C->Bb 2, C->D 2.
    CHECK(pcVoiceLeadingCost({0, 7}, {0, 4, 7, 10, 2}) == 7);
    CHECK(pcVoiceLeadingCost({0, 4, 7, 10, 2}, {0, 7}) == 7);  // whichever side is shorter
    // Given as {7 0} it becomes {7 0 7 0 7}: one more G to move (G->E 3, G->Bb 3, C->D 2), so 8.
    CHECK(pcVoiceLeadingCost({7, 0}, {0, 4, 7, 10, 2}) == 8);
    // A single pitch class is repeated as often as needed.
    CHECK(pcVoiceLeadingCost({0}, {0, 0, 0}) == 0);
    CHECK(pcVoiceLeadingCost({0}, {11, 0, 1}) == 2);
}

TEST_CASE("pcVoiceLeadingCost: is symmetric and bounded for every pair of dictionary chords") {
    const std::vector<Chord> onC = allChordsOnC();
    const std::vector<Chord> all = allChords();
    for (const Chord& a : onC) {
        for (const Chord& b : all) {
            CAPTURE(a.type->id);
            CAPTURE(b.type->id);
            CAPTURE(b.root);
            const int cost = pcVoiceLeadingCost(pitchClasses(a), pitchClasses(b));
            CHECK(cost >= 0);
            CHECK(cost <= 6 * 5);  // at most five voices, each at most a tritone away
            CHECK(pcVoiceLeadingCost(pitchClasses(b), pitchClasses(a)) == cost);
        }
    }
}

TEST_CASE("pcVoiceLeadingCost: is invariant under transposition") {
    const std::vector<Chord> onC = allChordsOnC();
    const std::vector<Chord> all = allChords();
    for (const Chord& a : onC) {
        for (const Chord& b : all) {
            for (const int t : {1, 5, 7}) {
                CAPTURE(a.type->id);
                CAPTURE(b.type->id);
                CAPTURE(b.root);
                CAPTURE(t);
                const Chord at{pitchClassOf(a.root + t), a.type};
                const Chord bt{pitchClassOf(b.root + t), b.type};
                CHECK(pcVoiceLeadingCost(pitchClasses(at), pitchClasses(bt)) ==
                      pcVoiceLeadingCost(pitchClasses(a), pitchClasses(b)));
            }
        }
    }
}

TEST_CASE("pcVoiceLeadingCost: agrees with the best cyclic shift of the sorted sets for same-size sets") {
    // On a circle an optimal bijection between two sorted sequences of equal size is one of the n cyclic
    // shifts (Werman, Peleg, Melter and Kong, 1986), which gives an independent oracle for the search.
    const std::vector<Chord> onC = allChordsOnC();
    const std::vector<Chord> all = allChords();
    for (const Chord& a : onC) {
        for (const Chord& b : all) {
            Pcs pa = pitchClasses(a);
            Pcs pb = pitchClasses(b);
            if (pa.size() != pb.size()) {
                continue;
            }
            CAPTURE(a.type->id);
            CAPTURE(b.type->id);
            CAPTURE(b.root);
            std::sort(pa.begin(), pa.end());
            std::sort(pb.begin(), pb.end());
            const std::size_t n = pa.size();
            int best = 6 * 6;
            for (std::size_t shift = 0; shift < n; ++shift) {
                int cost = 0;
                for (std::size_t i = 0; i < n; ++i) {
                    const int d = std::abs(pa[i] - pb[(i + shift) % n]);
                    cost += std::min(d, 12 - d);
                }
                best = std::min(best, cost);
            }
            CHECK(pcVoiceLeadingCost(pa, pb) == best);
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
    // Bb D F is in both C Aeolian and C Mixolydian; the parallel minor is tried first.
    CHECK(borrowedFrom(chord(10, "maj"), kCIonian) == kCAeolian);
    // Bb with a major seventh (Bb D F A) is not in Aeolian (no A); Mixolydian has it, before Dorian.
    CHECK(borrowedFrom(chord(10, "maj7"), kCIonian) == key('C', 0, Mode::Mixolydian));
    // C7 (C E G Bb) is not in Aeolian (no E); Mixolydian has it.
    CHECK(borrowedFrom(chord(0, "7"), kCIonian) == key('C', 0, Mode::Mixolydian));
    // Dm7b5 (D F Ab C) is in Aeolian.
    CHECK(borrowedFrom(chord(2, "m7b5"), kCIonian) == kCAeolian);
}

TEST_CASE("borrowedFrom: a chord in no parallel mode is not attributed") {
    CHECK_FALSE(borrowedFrom(chord(1, "dim7"), kCIonian).has_value());  // C#°7: C# E G Bb
    CHECK_FALSE(borrowedFrom(chord(4, "maj"), kCIonian).has_value());   // E: E G# B
    CHECK_FALSE(borrowedFrom(chord(0, "aug"), kCIonian).has_value());   // C+: C E G#
    CHECK_FALSE(borrowedFrom(chord(6, "7"), kCIonian).has_value());     // F#7: F# A# C# E
}

TEST_CASE("borrowedFrom: in a major key the minor scales and Locrian come after the diatonic modes") {
    // G#°7 (G# B D F): no diatonic mode on C has both G# and B; harmonic minor does.
    CHECK(borrowedFrom(chord(8, "dim7"), kCIonian) == key('C', 0, Mode::HarmonicMinor));
    // Cm(maj7) (C Eb G B): harmonic minor, which precedes melodic minor.
    CHECK(borrowedFrom(chord(0, "mMaj7"), kCIonian) == key('C', 0, Mode::HarmonicMinor));
    // F# major (F# A# C#, i.e. Gb Bb Db): only C Locrian holds all three.
    CHECK(borrowedFrom(chord(6, "maj"), kCIonian) == key('C', 0, Mode::Locrian));
}

TEST_CASE("borrowedFrom: a minor-family key tries harmonic and melodic minor, then the parallel major") {
    // C Aeolian: the dominant comes from harmonic minor, I from the parallel major (Ionian precedes
    // Mixolydian, which also holds C E G), and ii (D F A) from melodic minor, which precedes Dorian.
    CHECK(borrowedFrom(chord(7, "maj"), kCAeolian) == key('C', 0, Mode::HarmonicMinor));
    CHECK(borrowedFrom(chord(7, "7"), kCAeolian) == key('C', 0, Mode::HarmonicMinor));
    CHECK(borrowedFrom(chord(0, "maj"), kCAeolian) == key('C', 0, Mode::Ionian));
    CHECK(borrowedFrom(chord(2, "min"), kCAeolian) == key('C', 0, Mode::MelodicMinor));
    CHECK(borrowedFrom(chord(11, "m7b5"), kCAeolian) == key('C', 0, Mode::MelodicMinor));  // B D F A
    CHECK(borrowedFrom(chord(7, "9"), kCAeolian) == key('C', 0, Mode::MelodicMinor));      // G B D F A
    // A Aeolian: V is from A harmonic minor; i is diatonic.
    CHECK(borrowedFrom(chord(4, "maj"), kAAeolian) == key('A', 0, Mode::HarmonicMinor));
    CHECK_FALSE(borrowedFrom(chord(9, "min"), kAAeolian).has_value());
    // The other minor-family modes and Locrian use the same order.
    const Key dDorian = key('D', 0, Mode::Dorian);
    CHECK(borrowedFrom(chord(9, "maj"), dDorian) == key('D', 0, Mode::HarmonicMinor));  // A C# E
    CHECK(borrowedFrom(chord(2, "maj"), dDorian) == key('D', 0, Mode::Ionian));         // D F# A
    CHECK(borrowedFrom(chord(11, "maj"), key('E', 0, Mode::Phrygian)) == key('E', 0, Mode::HarmonicMinor));
    CHECK(borrowedFrom(chord(6, "maj"), key('B', 0, Mode::Locrian)) == key('B', 0, Mode::HarmonicMinor));
}

TEST_CASE("borrowedFrom: Lydian and Mixolydian keys use the major-family order") {
    // F Lydian: Db (Db F Ab) is in F Aeolian, tried first; Bb (Bb D F) is not, and Mixolydian precedes
    // Dorian and Ionian.
    const Key fLydian = key('F', 0, Mode::Lydian);
    CHECK(borrowedFrom(chord(1, "maj"), fLydian) == key('F', 0, Mode::Aeolian));
    CHECK(borrowedFrom(chord(10, "maj"), fLydian) == key('F', 0, Mode::Mixolydian));
    // G Mixolydian: C#° (C# E G) is in G Lydian, tried before Dorian; Bb (Bb D F) is in G Aeolian.
    const Key gMixolydian = key('G', 0, Mode::Mixolydian);
    CHECK(borrowedFrom(chord(1, "dim"), gMixolydian) == key('G', 0, Mode::Lydian));
    CHECK(borrowedFrom(chord(10, "maj"), gMixolydian) == key('G', 0, Mode::Aeolian));
    CHECK_FALSE(borrowedFrom(chord(5, "maj"), gMixolydian).has_value());  // F is diatonic
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

TEST_CASE("borrowedFrom: every answer is the first containing mode in the family's order") {
    const std::array<Key, 9> projectKeys{kCIonian,
                                         kAAeolian,
                                         key('D', 0, Mode::Dorian),
                                         key('E', 0, Mode::Phrygian),
                                         key('F', 0, Mode::Lydian),
                                         key('E', -1, Mode::Ionian),
                                         key('F', +1, Mode::Mixolydian),
                                         key('A', 0, Mode::HarmonicMinor),
                                         key('B', 0, Mode::Locrian)};
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
            for (const Mode mode : borrowOrderFor(projectKey.mode)) {
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
    CHECK(r.voiceLeadingCost == 4);  // {0 4 7 0} vs {7 11 2 5}: G stays, C->B 1, E->F 1, C->D 2
    CHECK_FALSE(r.borrowedFrom.has_value());
}

TEST_CASE("relate: G7 to C in C major") {
    const Relation r = relate(chord(7, "7"), chord(0, "maj"), kCIonian);
    CHECK(r.commonTones == 1);
    CHECK(r.commonPcs == Pcs{7});
    CHECK(r.motion == RootMotion::DownFifth);
    CHECK(r.voiceLeadingCost == 4);
    CHECK_FALSE(r.borrowedFrom.has_value());
}

TEST_CASE("relate: C to Am shares two tones") {
    const Relation r = relate(chord(0, "maj"), chord(9, "min"), kCIonian);
    CHECK(r.commonTones == 2);
    CHECK(r.commonPcs == Pcs{0, 4});
    CHECK(r.motion == RootMotion::ThirdDown);
    CHECK(r.voiceLeadingCost == 2);  // C and E stay, G -> A
    CHECK_FALSE(r.borrowedFrom.has_value());
}

TEST_CASE("relate: C to Ab in C major is borrowed from C Aeolian") {
    const Relation r = relate(chord(0, "maj"), chord(8, "maj"), kCIonian);
    CHECK(r.commonTones == 1);
    CHECK(r.commonPcs == Pcs{0});
    CHECK(r.motion == RootMotion::ThirdDown);
    CHECK(r.voiceLeadingCost == 2);  // C stays, E -> Eb, G -> Ab
    CHECK(r.borrowedFrom == kCAeolian);
}

TEST_CASE("relate: borrowedFrom describes the destination chord only") {
    CHECK_FALSE(relate(chord(8, "maj"), chord(0, "maj"), kCIonian).borrowedFrom.has_value());
    CHECK(relate(chord(8, "maj"), chord(1, "maj"), kCIonian).borrowedFrom == key('C', 0, Mode::Phrygian));
    CHECK(relate(chord(0, "maj"), chord(6, "dim"), kCIonian).borrowedFrom == key('C', 0, Mode::Lydian));
}

TEST_CASE("relate: the voice-leading cost is the octave-free pitch-class cost, root doubled first") {
    const std::array<Chord, 4> chords{chord(0, "maj"), chord(7, "7"), chord(2, "m7"), chord(10, "9")};
    for (const Chord& a : chords) {
        for (const Chord& b : chords) {
            CAPTURE(a.type->id);
            CAPTURE(b.type->id);
            CHECK(relate(a, b, kCIonian).voiceLeadingCost == pcVoiceLeadingCost(pitchClasses(a), pitchClasses(b)));
        }
    }
    // The same move transposed costs the same, whatever register the close voicings would land in.
    CHECK(relate(chord(11, "maj"), chord(0, "maj"), kCIonian).voiceLeadingCost == 3);  // B -> C
    CHECK(relate(chord(0, "maj"), chord(1, "maj"), kCIonian).voiceLeadingCost == 3);   // C -> Db
    CHECK(relate(chord(0, "maj"), chord(5, "maj"), kCIonian).voiceLeadingCost == 3);   // C -> F
    CHECK(relate(chord(0, "maj"), chord(7, "maj"), kCIonian).voiceLeadingCost == 3);   // C -> G
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
            CHECK(ab.voiceLeadingCost == pcVoiceLeadingCost(pitchClasses(a), pitchClasses(b)));
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
