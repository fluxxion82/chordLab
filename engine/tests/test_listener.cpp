#include "doctest.h"

#include "chordlab/listener.h"

#include <cstdint>
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
