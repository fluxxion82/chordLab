// Allocation check for the real-time surface, as a process of its own: this translation unit replaces the
// global operator new / delete with counting versions (memory still comes from malloc/free, so every
// variant stays consistent), and each test measures the calls made inside a scope. Nothing in the
// measured scopes may use doctest's assertion macros, which may allocate; results are tallied in plain
// integers and checked afterwards. The suite proper (engine_tests) is unaffected: a replaced allocator
// belongs to one executable.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include "chordlab/chord.h"
#include "chordlab/compiler.h"
#include "chordlab/document.h"
#include "chordlab/limits.h"
#include "chordlab/note_event.h"
#include "chordlab/scheduler.h"
#include "chordlab/triple_buffer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <random>
#include <vector>

namespace {

std::size_t gNewCalls = 0;  // every operator new of this process; the tests are single-threaded

void* counted(std::size_t size) noexcept {
    ++gNewCalls;
    return std::malloc(size ? size : 1);
}

void* countedAligned(std::size_t size, std::align_val_t alignment) noexcept {
    ++gNewCalls;
    void* p = nullptr;
    const std::size_t align = std::max(static_cast<std::size_t>(alignment), sizeof(void*));
    return posix_memalign(&p, align, size ? size : 1) == 0 ? p : nullptr;
}

}  // namespace

void* operator new(std::size_t size) {
    if (void* p = counted(size)) {
        return p;
    }
    throw std::bad_alloc();
}

void* operator new[](std::size_t size) {
    if (void* p = counted(size)) {
        return p;
    }
    throw std::bad_alloc();
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    return counted(size);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    return counted(size);
}

void* operator new(std::size_t size, std::align_val_t alignment) {
    if (void* p = countedAligned(size, alignment)) {
        return p;
    }
    throw std::bad_alloc();
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
    if (void* p = countedAligned(size, alignment)) {
        return p;
    }
    throw std::bad_alloc();
}

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return countedAligned(size, alignment);
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    return countedAligned(size, alignment);
}

void operator delete(void* p) noexcept {
    std::free(p);
}

void operator delete[](void* p) noexcept {
    std::free(p);
}

void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}

void operator delete[](void* p, std::size_t) noexcept {
    std::free(p);
}

void operator delete(void* p, std::align_val_t) noexcept {
    std::free(p);
}

void operator delete[](void* p, std::align_val_t) noexcept {
    std::free(p);
}

void operator delete(void* p, std::size_t, std::align_val_t) noexcept {
    std::free(p);
}

void operator delete[](void* p, std::size_t, std::align_val_t) noexcept {
    std::free(p);
}

using namespace chordlab;

namespace {

/// Allocations made since construction.
struct AllocationProbe {
    std::size_t start = gNewCalls;
    std::size_t count() const noexcept { return gNewCalls - start; }
};

Chord chord(PitchClass root, const char* typeId) {
    return Chord{root, &chordType(typeId)};
}

Compiled compiled(const std::vector<Chord>& chords, double lengthBeats) {
    DocumentStore store;
    for (const Chord& c : chords) {
        store.appendChord(c, lengthBeats, 100);
    }
    return compile(store.doc());
}

Snapshot snapshotOf(const Compiled& c, std::uint32_t generation) noexcept {
    return Snapshot{c.events.data(), c.events.size(), c.endBeat, true, generation};
}

constexpr double kSr = 48000.0;

}  // namespace

TEST_CASE("rt alloc: the probe sees an allocation") {
    AllocationProbe probe;
    {
        std::vector<int> v(64);
        v[0] = 1;
    }
    CHECK(probe.count() >= 1);
}

TEST_CASE("rt alloc: the scheduler allocates nothing across 10k random blocks with previews") {
    // Everything that allocates is prepared first: the progressions, the random engine, the output.
    const Compiled a = compiled({chord(0, "maj"), chord(9, "min"), chord(5, "maj"), chord(7, "7")}, 4.0);
    const Compiled b = compiled({chord(2, "m7"), chord(7, "7"), chord(0, "maj9")}, 1.5);
    std::mt19937 rng(20261011);
    std::uniform_int_distribution<int> frames(0, 1024);
    std::uniform_int_distribution<int> dice(0, 99);
    std::uniform_int_distribution<int> pitch(36, 96);
    std::array<MidiOut, 256> out{};

    AllocationProbe probe;
    Scheduler scheduler;  // construction included: the contract is "after construction", this is stricter
    scheduler.reset(kSr);
    scheduler.adopt(snapshotOf(a, 1));
    const Compiled* current = &a;
    std::uint32_t generation = 1;
    double ppq = 0.0;
    double bpm = 120.0;
    bool playing = true;
    int previewId = 0;
    int incompleteBlocks = 0;
    int refusedPreviews = 0;
    for (int i = 0; i < 10000; ++i) {
        const int nFrames = frames(rng);
        const int roll = dice(rng);
        if (roll < 2) {
            playing = !playing;  // start / stop
        } else if (roll < 4) {
            ppq += 7.25;  // seek forward
        } else if (roll < 5) {
            ppq = std::max(0.0, ppq - 3.0);  // seek back
        } else if (roll < 7) {
            bpm = roll == 5 ? 93.0 : 141.0;  // tempo change
        } else if (roll < 12) {
            PreviewCmd cmd{};
            cmd.kind = PreviewCmd::Start;
            cmd.id = ++previewId;
            cmd.velocity = 100;
            cmd.count = 3;
            cmd.pitches[0] = static_cast<std::uint8_t>(pitch(rng));
            cmd.pitches[1] = static_cast<std::uint8_t>(cmd.pitches[0] + 4);
            cmd.pitches[2] = static_cast<std::uint8_t>(cmd.pitches[0] + 7);
            refusedPreviews += scheduler.pushPreview(cmd) ? 0 : 1;
        } else if (roll < 17 && previewId > 0) {
            PreviewCmd cmd{};
            cmd.kind = PreviewCmd::Stop;
            cmd.id = previewId;
            refusedPreviews += scheduler.pushPreview(cmd) ? 0 : 1;
        } else if (roll == 17) {
            scheduler.panic();
        } else if (roll == 18) {
            current = current == &a ? &b : &a;
            scheduler.adopt(snapshotOf(*current, ++generation));  // changed content
        } else if (roll == 19) {
            scheduler.adopt(snapshotOf(*current, generation));  // same generation: a no-op by contract
        }
        std::size_t written = 0;
        const BlockInfo info{ppq, nFrames, kSr, bpm, playing};
        if (!scheduler.process(info, out.data(), out.size(), written)) {
            ++incompleteBlocks;
        }
        if (playing) {
            ppq += static_cast<double>(nFrames) / (kSr * 60.0 / bpm);
        }
    }
    CHECK(probe.count() == 0);
    CHECK(previewId > 0);  // the random walk did exercise previews
    CHECK(generation > 1);  // ...and adopts
    (void) incompleteBlocks;  // seeks and stops may make blocks incomplete; that is not what is measured
    (void) refusedPreviews;
}

TEST_CASE("rt alloc: the triple buffer allocates nothing on the producer or the consumer side") {
    struct Slot {
        std::array<NoteEvent, kMaxSnapshotEvents> events;
        std::size_t count;
        double endBeat;
        bool loop;
    };
    const auto buffer = std::make_unique<TripleBuffer<Slot>>();  // ~300 KB: off the stack, before the probe

    AllocationProbe probe;
    int wrongGeneration = 0;
    int wrongCurrent = 0;
    int unexpectedNull = 0;
    std::uint32_t lastSeen = 0;
    for (std::uint32_t generation = 1; generation <= 10000; ++generation) {
        Slot& back = buffer->beginWrite();
        back.count = generation % 5;
        for (std::size_t i = 0; i < back.count; ++i) {
            back.events[i] = NoteEvent{static_cast<double>(i), true, 60, 100};
        }
        back.endBeat = static_cast<double>(back.count);
        back.loop = true;
        buffer->publish(generation);
        if (generation % 3 == 0) {
            const Slot* acquired = buffer->acquire();
            if (acquired == nullptr) {
                ++unexpectedNull;
                continue;
            }
            if (buffer->current() != acquired) {
                ++wrongCurrent;
            }
            if (buffer->currentGeneration() != generation || generation <= lastSeen) {
                ++wrongGeneration;
            }
            lastSeen = generation;
        } else if (buffer->acquire() != nullptr && buffer->currentGeneration() != generation) {
            ++wrongGeneration;  // an acquire between the scheduled ones still hands over the latest
        }
    }
    CHECK(probe.count() == 0);
    CHECK(unexpectedNull == 0);
    CHECK(wrongCurrent == 0);
    CHECK(wrongGeneration == 0);
}
