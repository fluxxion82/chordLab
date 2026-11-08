#include "doctest.h"

#include "chordlab/midi_in.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <type_traits>
#include <vector>

using namespace chordlab;

TEST_CASE("midi_in: a note-on is a NoteOn with its pitch, velocity and the given time") {
    const auto e = heardEventOf(0x90, 60, 100, 12345);
    REQUIRE(e.has_value());
    CHECK(e->kind == HeardEvent::NoteOn);
    CHECK(e->pitch == 60);
    CHECK(e->velocity == 100);
    CHECK(e->sampleTime == 12345);
}

TEST_CASE("midi_in: a note-on with velocity 0 is a NoteOff") {
    const auto e = heardEventOf(0x90, 60, 0, 7);
    REQUIRE(e.has_value());
    CHECK(e->kind == HeardEvent::NoteOff);
    CHECK(e->pitch == 60);
    CHECK(e->velocity == 0);
    CHECK(e->sampleTime == 7);
}

TEST_CASE("midi_in: a note-off is a NoteOff and keeps its release velocity") {
    const auto e = heardEventOf(0x80, 60, 64, 9);
    REQUIRE(e.has_value());
    CHECK(e->kind == HeardEvent::NoteOff);
    CHECK(e->pitch == 60);
    CHECK(e->velocity == 64);
    CHECK(e->sampleTime == 9);
}

TEST_CASE("midi_in: CC 64 is Sustain with the value as velocity") {
    const auto down = heardEventOf(0xB0, 64, 127, 100);
    REQUIRE(down.has_value());
    CHECK(down->kind == HeardEvent::Sustain);
    CHECK(down->velocity == 127);
    CHECK(down->sampleTime == 100);
    const auto up = heardEventOf(0xB0, 64, 0, 200);
    REQUIRE(up.has_value());
    CHECK(up->kind == HeardEvent::Sustain);
    CHECK(up->velocity == 0);
    CHECK(up->sampleTime == 200);
}

TEST_CASE("midi_in: every other message is nothing") {
    CHECK_FALSE(heardEventOf(0xB0, 1, 77, 0).has_value());    // modulation wheel
    CHECK_FALSE(heardEventOf(0xB0, 66, 127, 0).has_value());  // sostenuto, not the sustain pedal
    CHECK_FALSE(heardEventOf(0xE0, 0, 64, 0).has_value());    // pitch bend
    CHECK_FALSE(heardEventOf(0xA0, 60, 50, 0).has_value());   // polyphonic aftertouch
    CHECK_FALSE(heardEventOf(0xC0, 5, 0, 0).has_value());     // program change
    CHECK_FALSE(heardEventOf(0xD0, 50, 0, 0).has_value());    // channel pressure
    CHECK_FALSE(heardEventOf(0xF8, 0, 0, 0).has_value());     // clock
    CHECK_FALSE(heardEventOf(0x3C, 64, 0, 0).has_value());    // a data byte where a status belongs
    CHECK_FALSE(heardEventOf(0xF0, 0x7E, 0x7F, 0).has_value());  // SysEx start
}

TEST_CASE("midi_in: channels are merged") {
    for (std::uint8_t channel = 0; channel < 16; ++channel) {
        const auto on = heardEventOf(static_cast<std::uint8_t>(0x90 | channel), 60, 100, 5);
        REQUIRE(on.has_value());
        CHECK(on->kind == HeardEvent::NoteOn);
        CHECK(on->pitch == 60);
        const auto off = heardEventOf(static_cast<std::uint8_t>(0x80 | channel), 60, 0, 5);
        REQUIRE(off.has_value());
        CHECK(off->kind == HeardEvent::NoteOff);
        const auto pedal = heardEventOf(static_cast<std::uint8_t>(0xB0 | channel), 64, 90, 5);
        REQUIRE(pedal.has_value());
        CHECK(pedal->kind == HeardEvent::Sustain);
        CHECK(pedal->velocity == 90);
    }
    const auto five = heardEventOf(0x95, 60, 100, 5);
    const auto zero = heardEventOf(0x90, 60, 100, 5);
    REQUIRE(five.has_value());
    REQUIRE(zero.has_value());
    CHECK(five->kind == zero->kind);
    CHECK(five->pitch == zero->pitch);
    CHECK(five->velocity == zero->velocity);
}

TEST_CASE("midi_in: thru carries on and off with pitch, velocity and the offset") {
    const auto on = thruEventOf(0x90, 64, 90, 33);
    REQUIRE(on.has_value());
    CHECK(on->on);
    CHECK(on->pitch == 64);
    CHECK(on->velocity == 90);
    CHECK(on->sampleOffset == 33);

    const auto zero = thruEventOf(0x93, 64, 0, 4);  // note-on with velocity 0 on channel 3
    REQUIRE(zero.has_value());
    CHECK_FALSE(zero->on);
    CHECK(zero->pitch == 64);
    CHECK(zero->sampleOffset == 4);

    const auto off = thruEventOf(0x80, 64, 40, 511);
    REQUIRE(off.has_value());
    CHECK_FALSE(off->on);
    CHECK(off->pitch == 64);
    CHECK(off->velocity == 40);
    CHECK(off->sampleOffset == 511);
}

TEST_CASE("midi_in: thru ignores CCs and everything else") {
    CHECK_FALSE(thruEventOf(0xB0, 64, 127, 0).has_value());
    CHECK_FALSE(thruEventOf(0xB0, 1, 77, 0).has_value());
    CHECK_FALSE(thruEventOf(0xE0, 0, 64, 0).has_value());
    CHECK_FALSE(thruEventOf(0xA0, 60, 50, 0).has_value());
    CHECK_FALSE(thruEventOf(0xC0, 5, 0, 0).has_value());
    CHECK_FALSE(thruEventOf(0xD0, 50, 0, 0).has_value());
    CHECK_FALSE(thruEventOf(0xF8, 0, 0, 0).has_value());
    CHECK_FALSE(thruEventOf(0x3C, 64, 0, 0).has_value());  // a data byte where a status belongs
    CHECK_FALSE(thruEventOf(0xF0, 0x7E, 0x7F, 0).has_value());  // SysEx start
}

namespace {

HeardEvent noteOn(std::uint64_t i) {
    return HeardEvent{HeardEvent::NoteOn, static_cast<std::uint8_t>(i % 128), 100, i};
}

}  // namespace

TEST_CASE("HeardRing: events come out in the order they went in") {
    HeardRing ring;
    for (std::uint64_t i = 0; i < 10; ++i) {
        CHECK(ring.push(noteOn(i)));
    }
    for (std::uint64_t i = 0; i < 10; ++i) {
        HeardEvent e{};
        REQUIRE(ring.pop(e));
        CHECK(e.sampleTime == i);
        CHECK(e.pitch == i);
        CHECK(e.kind == HeardEvent::NoteOn);
        CHECK(e.velocity == 100);
    }
}

TEST_CASE("HeardRing: a Reset event round-trips unchanged") {
    HeardRing ring;
    REQUIRE(ring.push(noteOn(1)));
    REQUIRE(ring.push(HeardEvent{HeardEvent::Reset, 0, 0, 4242}));
    REQUIRE(ring.push(noteOn(2)));
    HeardEvent e{};
    REQUIRE(ring.pop(e));
    CHECK(e.kind == HeardEvent::NoteOn);
    REQUIRE(ring.pop(e));
    CHECK(e.kind == HeardEvent::Reset);
    CHECK(e.pitch == 0);
    CHECK(e.velocity == 0);
    CHECK(e.sampleTime == 4242);
    REQUIRE(ring.pop(e));
    CHECK(e.kind == HeardEvent::NoteOn);
    CHECK(e.sampleTime == 2);
}

TEST_CASE("HeardRing: pop on an empty ring returns false and leaves the output alone") {
    HeardRing ring;
    HeardEvent e{HeardEvent::Sustain, 1, 2, 3};
    CHECK_FALSE(ring.pop(e));
    CHECK(e.pitch == 1);
    CHECK(e.sampleTime == 3);
    CHECK_FALSE(ring.takeOverflow());
}

TEST_CASE("HeardRing: 256 fit, the 257th is refused and the overflow reads true once") {
    HeardRing ring;
    for (std::uint64_t i = 0; i < HeardRing::kCapacity; ++i) {
        REQUIRE(ring.push(noteOn(i)));
    }
    CHECK_FALSE(ring.takeOverflow());
    CHECK_FALSE(ring.push(noteOn(999)));
    CHECK(ring.takeOverflow());
    CHECK_FALSE(ring.takeOverflow());
    CHECK_FALSE(ring.push(noteOn(1000)));  // still full: the flag is set again
    CHECK(ring.takeOverflow());
    // The refused events are lost; the 256 that fit are intact and in order.
    for (std::uint64_t i = 0; i < HeardRing::kCapacity; ++i) {
        HeardEvent e{};
        REQUIRE(ring.pop(e));
        CHECK(e.sampleTime == i);
    }
    HeardEvent e{};
    CHECK_FALSE(ring.pop(e));
    CHECK(ring.push(noteOn(5)));  // space again
    CHECK_FALSE(ring.takeOverflow());
}

TEST_CASE("HeardRing: interleaved pushes and pops across the wrap point keep the order") {
    HeardRing ring;
    std::uint64_t pushed = 0;
    std::uint64_t popped = 0;
    std::vector<std::uint64_t> seen;
    const auto pushN = [&](int n) {
        for (int i = 0; i < n; ++i) {
            REQUIRE(ring.push(noteOn(pushed++)));
        }
    };
    const auto popN = [&](int n) {
        for (int i = 0; i < n; ++i) {
            HeardEvent e{};
            REQUIRE(ring.pop(e));
            seen.push_back(e.sampleTime);
            ++popped;
        }
    };
    pushN(200);
    popN(100);
    pushN(150);  // 250 in flight: indices now wrap past 256
    HeardEvent e{};
    while (ring.pop(e)) {
        seen.push_back(e.sampleTime);
        ++popped;
    }
    REQUIRE(popped == pushed);
    REQUIRE(seen.size() == 350);
    for (std::uint64_t i = 0; i < seen.size(); ++i) {
        CHECK(seen[i] == i);
    }
    CHECK_FALSE(ring.takeOverflow());
}

TEST_CASE("HeardRing: many wraps with a ring kept nearly full stay in order") {
    HeardRing ring;
    std::uint64_t next = 0;
    std::uint64_t expected = 0;
    for (int i = 0; i < 200; ++i) {
        REQUIRE(ring.push(noteOn(next++)));
    }
    for (int round = 0; round < 5000; ++round) {
        REQUIRE(ring.push(noteOn(next++)));
        HeardEvent e{};
        REQUIRE(ring.pop(e));
        REQUIRE(e.sampleTime == expected++);
    }
    CHECK_FALSE(ring.takeOverflow());
}

namespace {

/// The deadline every cross-thread ring test stays inside, so a lost wake-up fails instead of hanging.
constexpr auto kThreadedBudget = std::chrono::seconds(5);

/// Stops and joins its producer on every exit, including a throwing REQUIRE: a joinable std::thread destroyed
/// during unwinding calls std::terminate and would abort the whole binary instead of failing one test.
struct ProducerJoiner {
    std::thread& thread;
    std::atomic<bool>& stop;
    void join() {
        stop = true;  // a no-op after a normal finish; on a timeout it frees a producer blocked on a full ring
        if (thread.joinable()) thread.join();
    }
    ~ProducerJoiner() { join(); }
};

bool intact(const HeardEvent& e, std::uint64_t seq) {
    return e.kind == HeardEvent::NoteOn && e.sampleTime == seq && e.pitch == seq % 128 && e.velocity == 100;
}

}  // namespace

TEST_CASE("HeardRing: a producer thread's 10 000 events arrive in order and intact through many wraps") {
    constexpr std::uint64_t kEvents = 10000;  // 39 wraps of the 256-slot ring
    HeardRing ring;
    const auto deadline = std::chrono::steady_clock::now() + kThreadedBudget;
    std::atomic<bool> stop{false};
    std::thread producer([&] {
        for (std::uint64_t seq = 0; seq < kEvents;) {
            if (ring.push(noteOn(seq))) {
                ++seq;
            } else if (stop) {
                return;
            } else {
                std::this_thread::yield();  // full: let the consumer catch up
            }
        }
    });
    ProducerJoiner joiner{producer, stop};
    std::uint64_t expected = 0;
    std::uint64_t broken = 0;  // events out of order or damaged; must stay 0
    while (expected < kEvents && std::chrono::steady_clock::now() <= deadline) {
        HeardEvent e{};
        if (!ring.pop(e)) {
            std::this_thread::yield();
            continue;
        }
        if (!intact(e, expected)) ++broken;
        ++expected;
    }
    joiner.join();  // join first: no assertion below may throw while the producer is still running
    CHECK(expected == kEvents);
    CHECK(broken == 0);
    // A full ring refuses, and every refusal sets the flag: the producer retried each one, so nothing was lost.
    ring.takeOverflow();
    HeardEvent e{};
    CHECK_FALSE(ring.pop(e));
}

TEST_CASE("HeardRing: across threads an overrun is reported once and the stream after it is in order again") {
    constexpr std::uint64_t kBurst = 1000;   // pushed while the consumer sleeps: 744 are refused
    constexpr std::uint64_t kAfter = 5000;   // pushed after the producer saw the consumer resynchronise
    HeardRing ring;
    const auto deadline = std::chrono::steady_clock::now() + kThreadedBudget;
    std::atomic<bool> burstDone{false};
    std::atomic<bool> resynced{false};
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> refused{0};
    std::thread producer([&] {
        for (std::uint64_t seq = 0; seq < kBurst; ++seq) {
            if (!ring.push(noteOn(seq))) refused.fetch_add(1, std::memory_order_relaxed);
        }
        burstDone = true;
        while (!resynced) {
            if (stop) return;
            std::this_thread::yield();
        }
        for (std::uint64_t seq = kBurst; seq < kBurst + kAfter;) {
            if (ring.push(noteOn(seq))) {
                ++seq;
            } else if (stop) {
                return;
            } else {
                std::this_thread::yield();
            }
        }
    });
    // Declared right after the thread: every exit below, a throwing REQUIRE included, stops and joins it first.
    ProducerJoiner joiner{producer, stop};
    // The consumer sleeps through the burst, then resynchronises as OnIdle does: the flag first, then the
    // lost epoch discarded whole.
    while (!burstDone && std::chrono::steady_clock::now() <= deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(burstDone.load());
    CHECK(ring.takeOverflow());
    CHECK_FALSE(ring.takeOverflow());  // reported once
    std::uint64_t discarded = 0;
    for (HeardEvent e{}; ring.pop(e);) {
        if (!intact(e, discarded)) break;  // the discarded epoch is the first 256, in order
        ++discarded;
    }
    CHECK(discarded == HeardRing::kCapacity);
    CHECK(refused.load() == kBurst - HeardRing::kCapacity);
    resynced = true;

    std::uint64_t expected = kBurst;
    std::uint64_t broken = 0;
    while (expected < kBurst + kAfter && std::chrono::steady_clock::now() <= deadline) {
        HeardEvent e{};
        if (!ring.pop(e)) {
            std::this_thread::yield();
            continue;
        }
        if (!intact(e, expected)) ++broken;
        ++expected;
    }
    joiner.join();  // join first: no assertion below may throw while the producer is still running
    CHECK(expected == kBurst + kAfter);
    CHECK(broken == 0);
}

namespace {
RawMidi rawNote(std::uint64_t seq, std::uint32_t epoch = 0) {
    return RawMidi{0x90, std::uint8_t(seq % 128), 100, epoch, int(seq)};
}
bool intactRaw(const RawMidi& m, std::uint64_t seq) {
    return m.status == 0x90 && m.data1 == seq % 128 && m.data2 == 100 && m.sampleOffset == int(seq);
}
}  // namespace

TEST_CASE("RawMidiRing: messages come out in the order they went in, bytes, epoch and offset intact") {
    RawMidiRing ring;
    for (int i = 0; i < 10; ++i) {
        CHECK(ring.push(RawMidi{0x90, std::uint8_t(60 + i), 100, 3, i * 7}));
    }
    for (int i = 0; i < 10; ++i) {
        RawMidi m{};
        REQUIRE(ring.pop(m));
        CHECK(m.status == 0x90);
        CHECK(m.data1 == 60 + i);
        CHECK(m.data2 == 100);
        CHECK(m.epoch == 3);
        CHECK(m.sampleOffset == i * 7);
    }
    RawMidi m{};
    CHECK_FALSE(ring.pop(m));
}

TEST_CASE("RawMidiRing: 256 fit, the 257th is refused and the overflow reads true once") {
    RawMidiRing ring;
    static_assert(RawMidiRing::kCapacity == 256);
    for (int i = 0; i < 256; ++i) {
        REQUIRE(ring.push(rawNote(i)));
    }
    CHECK_FALSE(ring.takeOverflow());
    CHECK_FALSE(ring.push(rawNote(999)));
    CHECK(ring.takeOverflow());
    CHECK_FALSE(ring.takeOverflow());
    for (int i = 0; i < 256; ++i) {
        RawMidi m{};
        REQUIRE(ring.pop(m));
        CHECK(intactRaw(m, i));
    }
    CHECK(ring.push(rawNote(0)));  // space again
    CHECK_FALSE(ring.takeOverflow());
}

TEST_CASE("RawMidiRing: a producer thread's 10 000 messages arrive in order and intact") {
    constexpr std::uint64_t kEvents = 10000;
    RawMidiRing ring;
    const auto deadline = std::chrono::steady_clock::now() + kThreadedBudget;
    std::atomic<bool> stop{false};
    std::thread producer([&] {
        for (std::uint64_t seq = 0; seq < kEvents;) {
            if (ring.push(rawNote(seq))) {
                ++seq;
            } else if (stop) {
                return;
            } else {
                std::this_thread::yield();
            }
        }
    });
    ProducerJoiner joiner{producer, stop};
    std::uint64_t expected = 0;
    std::uint64_t broken = 0;
    while (expected < kEvents && std::chrono::steady_clock::now() <= deadline) {
        RawMidi m{};
        if (!ring.pop(m)) {
            std::this_thread::yield();
            continue;
        }
        if (!intactRaw(m, expected)) ++broken;
        ++expected;
    }
    joiner.join();
    CHECK(expected == kEvents);
    CHECK(broken == 0);
}

TEST_CASE("SpscRing: the heard ring is the same template") {
    static_assert(std::is_same_v<HeardRing, SpscRing<HeardEvent, 256>>);
    static_assert(std::is_trivially_copyable_v<RawMidi>);
    CHECK(HeardRing::kCapacity == RawMidiRing::kCapacity);
}
