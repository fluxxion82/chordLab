#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/identify.h"
#include "chordlab/listener.h"
#include "chordlab/pitch.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

using namespace chordlab;

namespace {

// 120 bpm at 48 kHz: one beat is 24000 samples.
constexpr double kBpm = 120.0;
constexpr double kRate = 48000.0;
constexpr double kSamplesPerBeat = 24000.0;

std::uint64_t at(double beat) { return static_cast<std::uint64_t>(beat * kSamplesPerBeat); }

void onAt(Listener& l, int pitch, double beat) {
    l.feed({HeardEvent::NoteOn, static_cast<std::uint8_t>(pitch), 100, at(beat)}, kBpm, kRate);
}
void offAt(Listener& l, int pitch, double beat) {
    l.feed({HeardEvent::NoteOff, static_cast<std::uint8_t>(pitch), 0, at(beat)}, kBpm, kRate);
}
void pedal(Listener& l, int value, double beat) {
    l.feed({HeardEvent::Sustain, 64, static_cast<std::uint8_t>(value), at(beat)}, kBpm, kRate);
}
void advanceTo(Listener& l, double beat) { l.advance(at(beat), kBpm, kRate); }

/// One note of a schedule: a pitch and its length in beats.
struct Step {
    int pitch;
    double beats;
};

/// Plays `steps` back to back from `start` (on at its start, off at its end). With `tick`, `advance`s
/// to each note-off. Returns the beat after the last note.
double play(Listener& l, const std::vector<Step>& steps, double start, bool tick = true) {
    double t = start;
    for (const Step& s : steps) {
        onAt(l, s.pitch, t);
        t += s.beats;
        offAt(l, s.pitch, t);
        if (tick) advanceTo(l, t);
    }
    return t;
}

/// The C major scale 60...72 as quarter notes, one beat each.
const std::vector<Step> kCMajorScale{{60, 1}, {62, 1}, {64, 1}, {65, 1}, {67, 1}, {69, 1}, {71, 1}, {72, 1}};

/// The A natural minor phrase: A3 for 2 beats, A B C D E F G A, E, then A for 2 beats.
const std::vector<Step> kAMinorPhrase{{57, 2}, {57, 1}, {59, 1}, {60, 1}, {62, 1}, {64, 1},
                                      {65, 1}, {67, 1}, {69, 1}, {64, 1}, {69, 2}};

Key cIonian() { return Key{{'C', 0}, Mode::Ionian}; }
Key aAeolian() { return Key{{'A', 0}, Mode::Aeolian}; }

Chord chord(PitchClass root, std::string_view typeId) { return Chord{root, &chordType(typeId)}; }

/// `profile` rotated to `tonic`: element `(tonic + i) % 12` is `profile[i]`.
std::array<double, 12> rotated(const std::array<double, 12>& profile, int tonic) {
    std::array<double, 12> out{};
    for (int i = 0; i < 12; ++i) out[(tonic + i) % 12] = profile[i];
    return out;
}

}  // namespace

TEST_CASE("listener: starts empty and inactive") {
    Listener l;
    CHECK(l.held().empty());
    CHECK_FALSE(l.active());
    CHECK(l.nowBeats() == doctest::Approx(0.0));
    CHECK(l.onsetsInWindow() == 0);
    for (double v : l.histogram()) CHECK(v == doctest::Approx(0.0));
}

TEST_CASE("listener: note-on holds, note-off releases, a repeated note-on keeps one entry") {
    Listener l;
    onAt(l, 60, 0);
    onAt(l, 64, 0);
    CHECK(l.held() == std::vector<int>{60, 64});
    CHECK(l.active());
    offAt(l, 60, 1);
    CHECK(l.held() == std::vector<int>{64});
    onAt(l, 64, 2);
    CHECK(l.held() == std::vector<int>{64});
}

TEST_CASE("listener: held() is ascending whatever the arrival order") {
    Listener l;
    onAt(l, 67, 0);
    onAt(l, 48, 0);
    onAt(l, 60, 0);
    CHECK(l.held() == std::vector<int>{48, 60, 67});
}

TEST_CASE("listener: the sustain pedal holds a released note until it lifts") {
    Listener l;
    onAt(l, 60, 0);
    pedal(l, 127, 1);
    offAt(l, 60, 2);
    CHECK(l.held() == std::vector<int>{60});
    pedal(l, 0, 3);
    CHECK(l.held().empty());
}

TEST_CASE("listener: the pedal threshold is 64") {
    Listener l;
    onAt(l, 60, 0);
    pedal(l, 64, 0);
    offAt(l, 60, 1);
    CHECK(l.held() == std::vector<int>{60});
    pedal(l, 63, 2);
    CHECK(l.held().empty());

    onAt(l, 62, 3);
    pedal(l, 63, 3);
    offAt(l, 62, 4);  // pedal below 64 at the note-off: released at once
    CHECK(l.held().empty());
}

TEST_CASE("listener: pedal up with nothing pedal-held changes nothing") {
    Listener l;
    onAt(l, 60, 0);
    pedal(l, 0, 1);
    CHECK(l.held() == std::vector<int>{60});
}

TEST_CASE("listener: a note-on after the pedal went up is held by its key, not by the pedal") {
    Listener l;
    pedal(l, 127, 0);
    pedal(l, 0, 1);
    onAt(l, 60, 2);
    pedal(l, 127, 3);
    CHECK(l.held() == std::vector<int>{60});
    pedal(l, 0, 4);
    CHECK(l.held() == std::vector<int>{60});  // the key is still down
    offAt(l, 60, 5);
    CHECK(l.held().empty());
}

TEST_CASE("listener: a note-on on a pedal-held pitch is held by its key again") {
    Listener l;
    onAt(l, 60, 0);
    pedal(l, 127, 0);
    offAt(l, 60, 1);
    onAt(l, 60, 2);
    pedal(l, 0, 3);
    CHECK(l.held() == std::vector<int>{60});
    offAt(l, 60, 4);
    CHECK(l.held().empty());
}

TEST_CASE("listener: time accumulates from the sample stamps") {
    Listener l;
    onAt(l, 60, 0);
    CHECK(l.nowBeats() == doctest::Approx(0.0));
    onAt(l, 62, 1);
    CHECK(l.nowBeats() == doctest::Approx(1.0));
    l.feed({HeardEvent::NoteOff, 62, 0, 36000}, kBpm, kRate);
    CHECK(l.nowBeats() == doctest::Approx(1.5));
    l.advance(48000, kBpm, kRate);
    CHECK(l.nowBeats() == doctest::Approx(2.0));
}

TEST_CASE("listener: the first stamp is only a baseline") {
    Listener l;
    l.advance(10'000'000, kBpm, kRate);
    CHECK(l.nowBeats() == doctest::Approx(0.0));
    l.advance(10'024'000, kBpm, kRate);
    CHECK(l.nowBeats() == doctest::Approx(1.0));
}

TEST_CASE("listener: each call uses the tempo passed with it") {
    Listener l;
    l.feed({HeardEvent::NoteOn, 60, 100, 0}, kBpm, kRate);
    l.feed({HeardEvent::NoteOff, 60, 0, 36000}, kBpm, kRate);
    CHECK(l.nowBeats() == doctest::Approx(1.5));
    l.feed({HeardEvent::NoteOn, 62, 100, 60000}, 60.0, kRate);  // 24000 samples at 60 bpm: half a beat
    CHECK(l.nowBeats() == doctest::Approx(2.0));
}

TEST_CASE("listener: time is monotonic") {
    Listener l;
    l.feed({HeardEvent::NoteOn, 60, 100, 0}, kBpm, kRate);
    l.feed({HeardEvent::NoteOn, 62, 100, 36000}, kBpm, kRate);
    CHECK(l.nowBeats() == doctest::Approx(1.5));
    // An event stamped before the last one is simultaneous with it.
    l.feed({HeardEvent::NoteOff, 62, 0, 12000}, kBpm, kRate);
    CHECK(l.nowBeats() == doctest::Approx(1.5));
    // `advance` with an earlier clock never moves time backwards.
    l.advance(12000, kBpm, kRate);
    CHECK(l.nowBeats() == doctest::Approx(1.5));
    // The late stamps did not lower the reference: 48000 is half a beat after 36000, not 1.5 after 12000.
    l.advance(48000, kBpm, kRate);
    CHECK(l.nowBeats() == doctest::Approx(2.0));
}

TEST_CASE("listener: histogram weighs duration plus an onset") {
    Listener l;
    onAt(l, 60, 0);
    offAt(l, 60, 2);
    CHECK(l.histogram()[0] == doctest::Approx(2.25));
    for (int pc = 1; pc < 12; ++pc) CHECK(l.histogram()[pc] == doctest::Approx(0.0));
}

TEST_CASE("listener: a held note's end is now") {
    Listener l;
    onAt(l, 60, 0);
    advanceTo(l, 2);
    CHECK(l.histogram()[0] == doctest::Approx(2.25));
    advanceTo(l, 3);
    CHECK(l.histogram()[0] == doctest::Approx(3.25));
}

TEST_CASE("listener: one note contributes at most four beats") {
    Listener l;
    onAt(l, 60, 0);
    offAt(l, 60, 10);
    CHECK(l.histogram()[0] == doctest::Approx(4.25));
}

TEST_CASE("listener: notes use their pitch class") {
    Listener l;
    onAt(l, 61, 0);
    offAt(l, 61, 1);
    onAt(l, 73, 1);
    offAt(l, 73, 2);
    CHECK(l.histogram()[1] == doctest::Approx(2.5));
}

TEST_CASE("listener: a note entirely before the window contributes nothing") {
    Listener l;
    onAt(l, 60, 0);
    offAt(l, 60, 2);
    advanceTo(l, 40);
    CHECK(l.histogram()[0] == doctest::Approx(0.0));
}

TEST_CASE("listener: a note straddling the window start counts its inside part and no onset") {
    Listener l;
    onAt(l, 60, 6);
    offAt(l, 60, 10);
    advanceTo(l, 40);
    CHECK(l.histogram()[0] == doctest::Approx(2.0));
    CHECK(l.onsetsInWindow() == 0);
}

TEST_CASE("listener: an onset exactly at the window start counts") {
    Listener l;
    onAt(l, 60, 8);
    offAt(l, 60, 10);
    advanceTo(l, 40);
    CHECK(l.histogram()[0] == doctest::Approx(2.25));
    CHECK(l.onsetsInWindow() == 1);
}

TEST_CASE("listener: onsetsInWindow counts note starts inside the window") {
    Listener l;
    onAt(l, 60, 0);
    offAt(l, 60, 0.5);
    onAt(l, 62, 1);
    offAt(l, 62, 1.5);
    onAt(l, 64, 2);
    offAt(l, 64, 2.5);
    advanceTo(l, 2);
    CHECK(l.onsetsInWindow() == 3);
    advanceTo(l, 40);
    CHECK(l.onsetsInWindow() == 0);
}

TEST_CASE("listener: a restarted note closes the old one and opens a new one") {
    Listener l;
    onAt(l, 60, 0);
    onAt(l, 60, 1);
    offAt(l, 60, 2);
    // two notes: 0-1 and 1-2, each 1 beat plus an onset
    CHECK(l.histogram()[0] == doctest::Approx(2.5));
    CHECK(l.onsetsInWindow() == 2);
}

TEST_CASE("listener: a pedal-held note keeps sounding in the histogram until the pedal lifts") {
    Listener l;
    onAt(l, 60, 0);
    pedal(l, 127, 0);
    offAt(l, 60, 1);
    advanceTo(l, 2);
    CHECK(l.histogram()[0] == doctest::Approx(2.25));
    pedal(l, 0, 3);
    advanceTo(l, 5);
    CHECK(l.histogram()[0] == doctest::Approx(3.25));
}

TEST_CASE("listener: changed() follows feed, acknowledge and advance") {
    Listener l;
    CHECK_FALSE(l.changed());
    onAt(l, 60, 0);
    CHECK(l.changed());
    l.acknowledge();
    CHECK_FALSE(l.changed());
    offAt(l, 60, 1);
    CHECK(l.changed());
    l.acknowledge();
    CHECK_FALSE(l.changed());

    // Time passes inside the window with nothing visible changing.
    advanceTo(l, 5);
    CHECK_FALSE(l.changed());
    // The note's onset leaves the window: the visible count changes.
    advanceTo(l, 40);
    CHECK(l.changed());
    l.acknowledge();
    CHECK_FALSE(l.changed());
}

TEST_CASE("listener: advance on an idle listener stays unchanged") {
    Listener l;
    advanceTo(l, 1);
    advanceTo(l, 50);
    CHECK_FALSE(l.changed());
}

TEST_CASE("listener: clear forgets everything and flips active") {
    Listener l;
    onAt(l, 60, 0);
    onAt(l, 64, 1);
    l.acknowledge();
    l.clear();
    CHECK(l.changed());
    CHECK_FALSE(l.active());
    CHECK(l.held().empty());
    CHECK(l.onsetsInWindow() == 0);
    CHECK(l.nowBeats() == doctest::Approx(0.0));
    for (double v : l.histogram()) CHECK(v == doctest::Approx(0.0));
    l.acknowledge();
    onAt(l, 60, 5);
    CHECK(l.active());
    CHECK(l.held() == std::vector<int>{60});
}

TEST_CASE("listener: clear drops the pedal state too") {
    Listener l;
    onAt(l, 60, 0);
    pedal(l, 127, 0);
    l.clear();
    onAt(l, 62, 1);
    offAt(l, 62, 2);  // the pedal was forgotten, so this releases
    CHECK(l.held().empty());
}

TEST_CASE("listener: 300 notes wrap the ring, held set and histogram stay right") {
    Listener l;
    for (int i = 0; i < 300; ++i) {
        const double beat = i / 16.0;
        const int pitch = 60 + (i % 2);
        onAt(l, pitch, beat);
        offAt(l, pitch, beat + 1.0 / 16.0);
    }
    CHECK(l.held().empty());
    // Only the newest 256 notes are kept: 128 of each class, each 1/16 beat plus the onset weight.
    CHECK(l.onsetsInWindow() == 256);
    CHECK(l.histogram()[0] == doctest::Approx(128 * (1.0 / 16.0 + 0.25)));
    CHECK(l.histogram()[1] == doctest::Approx(128 * (1.0 / 16.0 + 0.25)));
    CHECK(l.histogram()[2] == doctest::Approx(0.0));
}

TEST_CASE("listener: a held note survives the ring wrapping over its slot") {
    Listener l;
    onAt(l, 40, 0);
    for (int i = 0; i < 300; ++i) {
        const double beat = 1.0 + i / 16.0;
        onAt(l, 60, beat);
        offAt(l, 60, beat + 1.0 / 16.0);
    }
    CHECK(l.held() == std::vector<int>{40});
    offAt(l, 40, 30);
    CHECK(l.held().empty());
}

// ---- Key estimate (design D8) ----

TEST_CASE("listener: pearson of a profile with itself is 1") {
    CHECK(pearson(kMajorProfile, kMajorProfile) == doctest::Approx(1.0).epsilon(1e-12));
    CHECK(pearson(kMinorProfile, kMinorProfile) == doctest::Approx(1.0).epsilon(1e-12));
}

TEST_CASE("listener: pearson of the major profile against G major") {
    // python3: the Krumhansl-Kessler major profile against its rotation to G (tonic 7).
    CHECK(pearson(kMajorProfile, rotated(kMajorProfile, 7)) == doctest::Approx(0.590974).epsilon(1e-6));
}

TEST_CASE("listener: pearson of a constant vector is NaN") {
    std::array<double, 12> flat{};
    flat.fill(0.1);
    CHECK(std::isnan(pearson(flat, kMajorProfile)));
    CHECK(std::isnan(pearson(kMinorProfile, flat)));
    CHECK(std::isnan(pearson(std::array<double, 12>{}, kMajorProfile)));
}

TEST_CASE("listener: the profiles are the Krumhansl-Kessler values, C-rooted") {
    CHECK(kMajorProfile == std::array<double, 12>{6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66,
                                                  2.29, 2.88});
    CHECK(kMinorProfile == std::array<double, 12>{6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69,
                                                  3.34, 3.17});
}

TEST_CASE("listener: no key before anything is heard") {
    Listener l;
    CHECK_FALSE(l.key().has_value());
    CHECK_FALSE(l.chord().has_value());
    CHECK_FALSE(l.chordHeld());
}

TEST_CASE("listener: the C major scale reads C Ionian") {
    Listener l;
    play(l, kCMajorScale, 0);
    const auto k = l.key();
    REQUIRE(k.has_value());
    CHECK(k->key == cIonian());
    CHECK(k->r >= 0.85);
    CHECK(k->r == doctest::Approx(0.901352).epsilon(1e-6));
    // The runner-up is A Aeolian at 0.756305.
    CHECK(k->gap == doctest::Approx(0.901352 - 0.756305).epsilon(1e-5));
}

TEST_CASE("listener: the estimate is the correlation and nothing else") {
    Listener l;
    play(l, kCMajorScale, 0);
    REQUIRE(l.key().has_value());
    CHECK(l.key()->r == doctest::Approx(pearson(l.histogram(), kMajorProfile)).epsilon(1e-6));
}

TEST_CASE("listener: five onsets are not enough for a key") {
    Listener l;
    const std::vector<Step> five(kCMajorScale.begin(), kCMajorScale.begin() + 5);
    play(l, five, 0);
    CHECK(l.onsetsInWindow() == 5);
    CHECK_FALSE(l.key().has_value());
    // The sixth onset acquires.
    play(l, {kCMajorScale[5]}, 5);
    REQUIRE(l.key().has_value());
    CHECK(l.key()->key == cIonian());
}

TEST_CASE("listener: feed never evaluates the key, advance does") {
    Listener l;
    play(l, kCMajorScale, 0, /*tick=*/false);
    CHECK(l.onsetsInWindow() == 8);
    CHECK_FALSE(l.key().has_value());
    advanceTo(l, 8);
    REQUIRE(l.key().has_value());
    CHECK(l.key()->key == cIonian());
}

TEST_CASE("listener: the A natural minor phrase reads A Aeolian") {
    Listener l;
    play(l, kAMinorPhrase, 0);
    const auto k = l.key();
    REQUIRE(k.has_value());
    CHECK(k->key == aAeolian());
    CHECK(k->r >= 0.8);
    CHECK(k->r == doctest::Approx(0.861957).epsilon(1e-6));
    // The runner-up is A Ionian at 0.730129.
    CHECK(k->gap == doctest::Approx(0.861957 - 0.730129).epsilon(1e-5));
}

TEST_CASE("listener: hysteresis: one acquisition, one replacement, never back") {
    // C E G C F A C G B D G C, then A(2) C E A E B A(2) D F E(2) A(2) G# A(2).
    const std::vector<Step> major{{60, 1}, {64, 1}, {67, 1}, {72, 1}, {65, 1}, {69, 1},
                                  {72, 1}, {67, 1}, {71, 1}, {74, 1}, {67, 1}, {72, 1}};
    const std::vector<Step> minor{{57, 2}, {60, 1}, {64, 1}, {69, 1}, {64, 1}, {71, 1}, {69, 2},
                                  {62, 1}, {65, 1}, {64, 2}, {69, 2}, {68, 1}, {69, 2}};
    std::vector<Step> all = major;
    all.insert(all.end(), minor.begin(), minor.end());

    Listener l;
    std::optional<Key> shown;
    int changes = 0;
    std::vector<int> changeTicks;
    double t = 0;
    for (std::size_t i = 0; i < all.size(); ++i) {
        onAt(l, all[i].pitch, t);
        t += all[i].beats;
        offAt(l, all[i].pitch, t);
        advanceTo(l, t);
        std::optional<Key> now;
        if (const auto k = l.key()) now = k->key;
        if (now != shown) {
            ++changes;
            changeTicks.push_back(static_cast<int>(i) + 1);
            shown = now;
        }
        if (i + 1 == 6) {
            REQUIRE(now.has_value());
            CHECK(*now == cIonian());
        }
        if (i + 1 > 6 && i + 1 < 12 + 7) {
            REQUIRE(now.has_value());
            CHECK(*now == cIonian());  // the relative minor creeps up but stays within the hysteresis
        }
        if (i + 1 >= 12 + 7) {
            REQUIRE(now.has_value());
            CHECK(*now == aAeolian());
        }
    }
    CHECK(changes == 2);
    // Acquired at the 6th onset, replaced at the 7th note of the minor phrase (tick 19).
    CHECK(changeTicks == std::vector<int>{6, 12 + 7});
}

TEST_CASE("listener: removal takes two consecutive failing ticks") {
    Listener l;
    const double end = play(l, kCMajorScale, 0);
    REQUIRE(l.key().has_value());
    // 40 beats of silence: no onsets in the window, the histogram is empty, retain fails.
    advanceTo(l, end + 40);
    REQUIRE(l.key().has_value());  // one miss: still shown
    CHECK(l.key()->key == cIonian());
    CHECK(l.key()->r == doctest::Approx(0.0));  // an undefined correlation reports 0
    advanceTo(l, end + 41);
    CHECK_FALSE(l.key().has_value());  // the second consecutive miss removes it
}

TEST_CASE("listener: a retaining tick resets the miss count") {
    Listener l;
    play(l, kCMajorScale, 0);
    // At beat 36 only the onsets at 4..7 are inside the window: 4 < 6, retain fails once.
    advanceTo(l, 36);
    REQUIRE(l.onsetsInWindow() == 4);
    REQUIRE(l.key().has_value());
    // The scale again in half beats, one tick at its end: retains (8 onsets, r 0.901352).
    std::vector<Step> fast;
    for (const Step& s : kCMajorScale) fast.push_back({s.pitch, 0.5});
    const double end = play(l, fast, 36, /*tick=*/false);
    advanceTo(l, end);
    REQUIRE(l.key().has_value());
    CHECK(l.key()->r == doctest::Approx(0.901352).epsilon(1e-6));
    // One more failing tick: had the count not been reset, this would be the second miss.
    advanceTo(l, end + 40);
    REQUIRE(l.key().has_value());
    advanceTo(l, end + 41);
    CHECK_FALSE(l.key().has_value());
}

TEST_CASE("listener: best r just under 0.6 shows no key") {
    // C D# E F F# G, F# for half a beat: C Ionian at r 0.599853 (gap 0.095733, 6 onsets).
    Listener l;
    play(l, {{60, 1}, {63, 1}, {64, 1}, {65, 1}, {66, 0.5}, {67, 1}}, 0, /*tick=*/false);
    advanceTo(l, 5.5);
    CHECK(l.onsetsInWindow() == 6);
    CHECK_FALSE(l.key().has_value());

    // F# for a quarter beat: r 0.628307, gap 0.087278 → C Ionian.
    Listener m;
    play(m, {{60, 1}, {63, 1}, {64, 1}, {65, 1}, {66, 0.25}, {67, 1}}, 0, /*tick=*/false);
    advanceTo(m, 5.25);
    REQUIRE(m.key().has_value());
    CHECK(m.key()->key == cIonian());
    CHECK(m.key()->r == doctest::Approx(0.628307).epsilon(1e-6));
}

TEST_CASE("listener: a gap just under 0.05 shows no key") {
    // The C major scale without the octave, every note equal: C Ionian 0.756407, A Aeolian 0.712127,
    // gap 0.044280 (relative-key ambiguity).
    Listener l;
    play(l, {{60, 1}, {62, 1}, {64, 1}, {65, 1}, {67, 1}, {69, 1}, {71, 1}}, 0, /*tick=*/false);
    advanceTo(l, 7);
    CHECK(l.onsetsInWindow() == 7);
    CHECK_FALSE(l.key().has_value());

    // The tonic a quarter beat longer: gap 0.069729 → C Ionian.
    Listener m;
    play(m, {{60, 1.25}, {62, 1}, {64, 1}, {65, 1}, {67, 1}, {69, 1}, {71, 1}}, 0, /*tick=*/false);
    advanceTo(m, 7.25);
    REQUIRE(m.key().has_value());
    CHECK(m.key()->key == cIonian());
    CHECK(m.key()->gap == doctest::Approx(0.069729).epsilon(1e-5));
}

TEST_CASE("listener: a key change sets changed() at the tick") {
    Listener l;
    play(l, std::vector<Step>(kCMajorScale.begin(), kCMajorScale.begin() + 5), 0);
    onAt(l, kCMajorScale[5].pitch, 5);
    offAt(l, kCMajorScale[5].pitch, 6);
    l.acknowledge();
    advanceTo(l, 6);  // acquires: onsets did not change, the key did
    REQUIRE(l.key().has_value());
    CHECK(l.changed());
}

TEST_CASE("listener: clear drops the key and the chord") {
    Listener l;
    play(l, kCMajorScale, 0);
    onAt(l, 60, 8);
    onAt(l, 64, 8);
    onAt(l, 67, 8);
    REQUIRE(l.key().has_value());
    REQUIRE(l.chord().has_value());
    l.acknowledge();
    l.clear();
    CHECK_FALSE(l.key().has_value());
    CHECK_FALSE(l.chord().has_value());
    CHECK_FALSE(l.chordHeld());
    CHECK(l.held().empty());
    CHECK(l.onsetsInWindow() == 0);
    CHECK(l.changed());
    CHECK_FALSE(l.active());
}

// ---- Latched chord (design D10) ----

TEST_CASE("listener: Am7 latches and stays after its notes are released") {
    Listener l;
    onAt(l, 57, 0);
    onAt(l, 60, 0);
    onAt(l, 64, 0);
    onAt(l, 67, 0);
    REQUIRE(l.chord().has_value());
    CHECK(l.chord()->chord == chord(9, "m7"));
    CHECK(l.chord()->bass == 57);
    CHECK(l.chord()->inversion == 0);
    CHECK(l.chordHeld());

    offAt(l, 57, 1);  // C E G left: still Am7, no longer held
    REQUIRE(l.chord().has_value());
    CHECK(l.chord()->chord == chord(9, "m7"));
    CHECK_FALSE(l.chordHeld());

    offAt(l, 60, 2);
    offAt(l, 64, 2);
    offAt(l, 67, 2);
    REQUIRE(l.chord().has_value());
    CHECK(l.chord()->chord == chord(9, "m7"));
    CHECK_FALSE(l.chordHeld());

    onAt(l, 60, 3);
    onAt(l, 64, 3);
    onAt(l, 67, 3);
    REQUIRE(l.chord().has_value());
    CHECK(l.chord()->chord == chord(0, "maj"));
    CHECK(l.chordHeld());
}

TEST_CASE("listener: an unidentified set keeps the latched chord") {
    Listener l;
    onAt(l, 60, 0);
    onAt(l, 64, 0);
    onAt(l, 67, 0);
    REQUIRE(l.chord().has_value());
    CHECK(l.chord()->chord == chord(0, "maj"));
    offAt(l, 60, 1);
    offAt(l, 64, 1);
    offAt(l, 67, 1);

    onAt(l, 60, 2);
    onAt(l, 61, 2);  // C and C#: no match
    REQUIRE(l.chord().has_value());
    CHECK(l.chord()->chord == chord(0, "maj"));
    CHECK_FALSE(l.chordHeld());

    offAt(l, 61, 3);  // C alone
    REQUIRE(l.chord().has_value());
    CHECK(l.chord()->chord == chord(0, "maj"));
    CHECK_FALSE(l.chordHeld());

    offAt(l, 60, 4);
    onAt(l, 60, 5);  // a single note-on: one class, no identification
    REQUIRE(l.chord().has_value());
    CHECK(l.chord()->chord == chord(0, "maj"));
    CHECK_FALSE(l.chordHeld());
}

TEST_CASE("listener: the pedal keeps the chord held until it lifts") {
    Listener l;
    onAt(l, 60, 0);
    onAt(l, 64, 0);
    onAt(l, 67, 0);
    pedal(l, 127, 0);
    offAt(l, 60, 1);
    offAt(l, 64, 1);
    offAt(l, 67, 1);
    CHECK(l.chordHeld());
    pedal(l, 0, 2);
    REQUIRE(l.chord().has_value());
    CHECK(l.chord()->chord == chord(0, "maj"));
    CHECK_FALSE(l.chordHeld());
}

TEST_CASE("listener: octave doubling reads C in root position over the lowest pitch") {
    Listener l;
    onAt(l, 48, 0);
    onAt(l, 60, 0);
    onAt(l, 64, 0);
    onAt(l, 67, 0);
    REQUIRE(l.chord().has_value());
    CHECK(l.chord()->chord == chord(0, "maj"));
    CHECK(l.chord()->inversion == 0);
    CHECK(l.chord()->bass == 48);
    CHECK(l.chordHeld());
}

TEST_CASE("listener: a newly latched chord sets changed() even when acknowledged state matched") {
    Listener l;
    onAt(l, 60, 0);
    onAt(l, 64, 0);
    l.acknowledge();
    onAt(l, 67, 0);
    CHECK(l.changed());
    l.acknowledge();
    advanceTo(l, 1);  // nothing visible moves
    CHECK_FALSE(l.changed());
}
