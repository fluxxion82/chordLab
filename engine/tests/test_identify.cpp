#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/identify.h"
#include "chordlab/voicing.h"

#include <algorithm>
#include <set>
#include <string_view>
#include <vector>

using namespace chordlab;

namespace {

Chord chord(PitchClass root, std::string_view typeId) {
    return Chord{root, &chordType(typeId)};
}

/// Identifies `held` and checks the literal expectation.
void expectHeard(const std::vector<int>& held, const Chord& expected, int bass, int inversion) {
    const auto heard = identifyChord(held);
    REQUIRE(heard.has_value());
    CHECK(heard->chord == expected);
    CHECK(heard->bass == bass);
    CHECK(heard->inversion == inversion);
}

std::set<int> classesOf(const std::vector<int>& pitches) {
    std::set<int> result;
    for (const int p : pitches) {
        result.insert(p % 12);
    }
    return result;
}

}  // namespace

TEST_CASE("identify: the plan's literal examples") {
    expectHeard({57, 60, 64, 67}, chord(9, "m7"), 57, 0);
    expectHeard({48, 57, 64, 67}, chord(0, "6"), 48, 0);
    expectHeard({48, 60, 64, 67}, chord(0, "maj"), 48, 0);
    expectHeard({60, 64, 67, 70, 74}, chord(0, "9"), 60, 0);
    expectHeard({64, 67, 70, 72, 74}, chord(0, "9"), 64, 1);
    expectHeard({60, 67}, chord(0, "5"), 60, 0);
}

TEST_CASE("identify: input order is irrelevant and the bass is the lowest pitch") {
    expectHeard({67, 57, 64, 60}, chord(9, "m7"), 57, 0);
    expectHeard({74, 70, 60, 67, 64}, chord(0, "9"), 60, 0);
}

TEST_CASE("identify: fewer than two classes, or no exact match, is nullopt") {
    CHECK_FALSE(identifyChord({}).has_value());
    CHECK_FALSE(identifyChord({60}).has_value());
    CHECK_FALSE(identifyChord({60, 72}).has_value());
    CHECK_FALSE(identifyChord({60, 61}).has_value());
    CHECK_FALSE(identifyChord({60, 61, 62}).has_value());
    CHECK_FALSE(identifyChord({60, 64, 67, 70, 73}).has_value());  // C7 plus a flat nine: no near matches
}

TEST_CASE("identify: collision family {0,4,7,9}: 6 before m7 unless the bass is the m7 root") {
    expectHeard({52, 57, 60, 67}, chord(0, "6"), 52, 1);  // E in the bass
    expectHeard({57, 60, 64, 67}, chord(9, "m7"), 57, 0);  // A in the bass
    expectHeard({48, 57, 64, 67}, chord(0, "6"), 48, 0);  // C in the bass
    expectHeard({43, 48, 52, 57}, chord(0, "6"), 43, 2);  // G in the bass
}

TEST_CASE("identify: collision family {0,3,7,9}: m6 before m7b5 unless the bass is the m7b5 root") {
    expectHeard({48, 57, 64, 66}, chord(9, "m6"), 48, 1);  // C in the bass
    expectHeard({54, 57, 60, 64}, chord(6, "m7b5"), 54, 0);  // F# in the bass
    expectHeard({57, 60, 64, 66}, chord(9, "m6"), 57, 0);  // A in the bass
    expectHeard({52, 57, 60, 66}, chord(9, "m6"), 52, 2);  // E in the bass
}

TEST_CASE("identify: collision family {0,2,7}: sus2 before sus4 unless the bass is the sus4 root") {
    expectHeard({62, 67, 72}, chord(0, "sus2"), 62, 1);  // D in the bass
    expectHeard({55, 60, 62}, chord(7, "sus4"), 55, 0);  // G in the bass
    expectHeard({60, 62, 67}, chord(0, "sus2"), 60, 0);  // C in the bass
}

TEST_CASE("identify: the augmented triad resolves to the root equal to the bass") {
    expectHeard({60, 64, 68}, chord(0, "aug"), 60, 0);
    expectHeard({64, 68, 72}, chord(4, "aug"), 64, 0);  // E in the bass
    expectHeard({56, 60, 64}, chord(8, "aug"), 56, 0);  // G# in the bass
}

TEST_CASE("identify: the diminished seventh resolves to the root equal to the bass") {
    expectHeard({51, 54, 57, 60}, chord(3, "dim7"), 51, 0);  // Eb in the bass
    expectHeard({57, 60, 63, 66}, chord(9, "dim7"), 57, 0);  // A in the bass
    expectHeard({48, 51, 54, 57}, chord(0, "dim7"), 48, 0);  // C in the bass
}

TEST_CASE("identify: every dictionary type in every inversion identifies its pitch-class set") {
    const std::set<std::string_view> colliding{"6", "m7", "m6", "m7b5", "sus2", "sus4", "aug", "dim7"};
    for (const ChordType& type : allChordTypes()) {
        CAPTURE(type.id);
        const Chord original{0, &type};
        for (int inversion = 0; inversion < inversionCount(original); ++inversion) {
            CAPTURE(inversion);
            const std::vector<int> held = voicing(original, inversion, 0);
            const auto heard = identifyChord(held);
            REQUIRE(heard.has_value());

            std::set<int> expectedClasses;
            for (const PitchClass pc : pitchClasses(original)) {
                expectedClasses.insert(pc);
            }
            std::set<int> heardClasses;
            for (const PitchClass pc : pitchClasses(heard->chord)) {
                heardClasses.insert(pc);
            }
            CHECK(heardClasses == expectedClasses);
            CHECK(classesOf(held) == expectedClasses);

            CHECK(heard->bass == *std::min_element(held.begin(), held.end()));
            const std::vector<PitchClass> pcs = pitchClasses(heard->chord);
            REQUIRE(heard->inversion >= 0);
            REQUIRE(heard->inversion < static_cast<int>(pcs.size()));
            CHECK(pcs[static_cast<std::size_t>(heard->inversion)] == heard->bass % 12);

            if (colliding.count(type.id) == 0) {
                CHECK(heard->chord == original);
                CHECK(heard->inversion == inversion);
            } else if (std::string_view(type.id) == "aug" || std::string_view(type.id) == "dim7") {
                CHECK(heard->chord.root == heard->bass % 12);
                CHECK(heard->inversion == 0);
            }
        }
    }
}
