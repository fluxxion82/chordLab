#include "doctest.h"

#include "chordlab/triple_buffer.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>

using namespace chordlab;

namespace {

/// A payload big enough that a torn read would show: every element carries the generation it was
/// published with.
using Block = std::array<std::uint32_t, 16>;

void fill(Block& block, std::uint32_t value) {
    block.fill(value);
}

bool holds(const Block& block, std::uint32_t value) {
    for (std::uint32_t v : block) {
        if (v != value) {
            return false;
        }
    }
    return true;
}

void publishBlock(TripleBuffer<Block>& buffer, std::uint32_t generation) {
    fill(buffer.beginWrite(), generation);
    buffer.publish(generation);
}

}  // namespace

TEST_CASE("triple buffer: nothing to acquire before the first publish") {
    TripleBuffer<Block> buffer;
    CHECK(buffer.current() == nullptr);
    CHECK(buffer.currentGeneration() == 0);
    CHECK(buffer.acquire() == nullptr);
    CHECK(buffer.current() == nullptr);
    CHECK(buffer.currentGeneration() == 0);
}

TEST_CASE("triple buffer: publish then acquire hands over the content and its generation") {
    TripleBuffer<Block> buffer;
    publishBlock(buffer, 7);
    const Block* acquired = buffer.acquire();
    REQUIRE(acquired != nullptr);
    CHECK(holds(*acquired, 7));
    CHECK(buffer.currentGeneration() == 7);
    CHECK(buffer.current() == acquired);
}

TEST_CASE("triple buffer: acquire returns nullptr when nothing new was published and keeps the front") {
    TripleBuffer<Block> buffer;
    publishBlock(buffer, 1);
    const Block* first = buffer.acquire();
    REQUIRE(first != nullptr);
    CHECK(buffer.acquire() == nullptr);
    CHECK(buffer.acquire() == nullptr);
    CHECK(buffer.current() == first);
    CHECK(buffer.currentGeneration() == 1);
    CHECK(holds(*first, 1));
}

TEST_CASE("triple buffer: publishes coalesce, one acquire after three publishes sees the last") {
    TripleBuffer<Block> buffer;
    publishBlock(buffer, 1);
    publishBlock(buffer, 2);
    publishBlock(buffer, 3);
    const Block* acquired = buffer.acquire();
    REQUIRE(acquired != nullptr);
    CHECK(holds(*acquired, 3));
    CHECK(buffer.currentGeneration() == 3);
    CHECK(buffer.acquire() == nullptr);  // 1 and 2 were replaced, not queued
}

TEST_CASE("triple buffer: the acquired slot is never the one being written") {
    TripleBuffer<Block> buffer;
    publishBlock(buffer, 1);
    const Block* held = buffer.acquire();
    REQUIRE(held != nullptr);

    // Publish twice more and then start a third write without publishing it: the producer has now
    // touched every slot it may ever touch while the consumer holds `held`.
    publishBlock(buffer, 2);
    publishBlock(buffer, 3);
    Block& writing = buffer.beginWrite();
    fill(writing, 99);
    CHECK(&writing != held);
    CHECK(holds(*held, 1));
    CHECK(buffer.current() == held);
    CHECK(buffer.currentGeneration() == 1);

    // The consumer's next acquire sees the last published content (3), never the unpublished write.
    const Block* next = buffer.acquire();
    REQUIRE(next != nullptr);
    CHECK(next != held);
    CHECK(next != &writing);
    CHECK(holds(*next, 3));
    CHECK(buffer.currentGeneration() == 3);

    // Whatever the interleaving, beginWrite never returns the consumer's front slot.
    for (std::uint32_t generation = 4; generation < 40; ++generation) {
        Block& slot = buffer.beginWrite();
        CHECK(&slot != buffer.current());
        fill(slot, generation);
        buffer.publish(generation);
        if (generation % 3 == 0) {
            const Block* acquired = buffer.acquire();
            REQUIRE(acquired != nullptr);
            CHECK(holds(*acquired, generation));
        }
    }
}

TEST_CASE("triple buffer: generations seen by the consumer never go backwards") {
    TripleBuffer<Block> buffer;
    std::uint32_t generation = 0;
    std::uint32_t lastSeen = 0;
    // A fixed interleaving: bursts of 1..5 publishes, each followed by one or two acquires.
    for (int round = 0; round < 50; ++round) {
        const int burst = 1 + round % 5;
        for (int i = 0; i < burst; ++i) {
            publishBlock(buffer, ++generation);
        }
        for (int i = 0; i < 1 + round % 2; ++i) {
            if (const Block* acquired = buffer.acquire()) {
                const std::uint32_t seen = buffer.currentGeneration();
                CHECK(seen > lastSeen);
                CHECK(seen == generation);  // always the latest publish, never a stale middle
                CHECK(holds(*acquired, seen));
                lastSeen = seen;
            } else {
                CHECK(buffer.currentGeneration() == lastSeen);
            }
        }
    }
    CHECK(lastSeen == generation);
}

TEST_CASE("triple buffer: two threads, 100k generations, every acquired slot consistent and in order") {
    constexpr std::uint32_t kGenerations = 100000;
    TripleBuffer<Block> buffer;

    std::thread producer([&buffer] {
        for (std::uint32_t generation = 1; generation <= kGenerations; ++generation) {
            fill(buffer.beginWrite(), generation);
            buffer.publish(generation);
        }
    });

    // The consumer runs on the test thread until it has acquired the final generation, which the
    // producer leaves fresh in the middle slot, so this always terminates.
    std::uint32_t lastSeen = 0;
    std::uint32_t acquires = 0;
    std::uint32_t torn = 0;          // an acquired slot whose elements disagree with its generation
    std::uint32_t backwards = 0;     // a generation not above the previous one
    std::uint32_t overwritten = 0;   // the front slot changed while the consumer held it
    const Block* front = nullptr;
    while (lastSeen < kGenerations) {
        if (const Block* acquired = buffer.acquire()) {
            const std::uint32_t generation = buffer.currentGeneration();
            if (generation <= lastSeen) {
                ++backwards;
            }
            if (!holds(*acquired, generation)) {
                ++torn;
            }
            lastSeen = generation;
            front = acquired;
            ++acquires;
        } else if (front != nullptr && !holds(*front, lastSeen)) {
            ++overwritten;
        }
    }
    producer.join();

    CHECK(acquires >= 1);
    CHECK(acquires <= kGenerations);
    CHECK(torn == 0);
    CHECK(backwards == 0);
    CHECK(overwritten == 0);
    CHECK(lastSeen == kGenerations);
    CHECK(buffer.acquire() == nullptr);
}
