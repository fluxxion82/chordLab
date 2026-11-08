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
#include "chordlab/midi_in.h"
#include "chordlab/note_event.h"
#include "chordlab/publication.h"
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

TEST_CASE("rt alloc: the scheduler allocates nothing across 10k random blocks with previews, thru notes and resets") {
    // Everything that allocates is prepared first: the progressions, the random engine, the output.
    const Compiled a = compiled({chord(0, "maj"), chord(9, "min"), chord(5, "maj"), chord(7, "7")}, 4.0);
    const Compiled b = compiled({chord(2, "m7"), chord(7, "7"), chord(0, "maj9")}, 1.5);
    std::mt19937 rng(20261011);
    std::uniform_int_distribution<int> frames(0, 1024);
    std::uniform_int_distribution<int> dice(0, 99);
    std::uniform_int_distribution<int> pitch(36, 96);
    std::uniform_int_distribution<int> fine(0, 999);
    std::uniform_int_distribution<int> offset(0, 1023);
    std::array<MidiOut, 512> out{};  // ChordLab.h: kMidiOutCapacity

    AllocationProbe probe;
    Scheduler scheduler;  // construction included: the contract is "after construction", this is stricter
    scheduler.reset(kSr);
    scheduler.adopt(snapshotOf(a, 1));
    const Compiled* current = &a;
    std::uint32_t generation = 1;
    double ppq = 0.0;
    double bpm = 120.0;
    bool playing = true;
    bool thru = true;
    int previewId = 0;
    int incompleteBlocks = 0;
    int refusedPreviews = 0;
    int thruPushes = 0;
    int refusedThru = 0;
    int thruFlips = 0;
    int forcedOverflows = 0;
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
            cmd.kind = roll == 16 ? PreviewCmd::Extend : PreviewCmd::Stop;  // one roll in five an extend
            cmd.id = previewId;
            refusedPreviews += scheduler.pushPreview(cmd) ? 0 : 1;
        } else if (roll == 17) {
            scheduler.panic();
        } else if (roll == 18) {
            current = current == &a ? &b : &a;
            scheduler.adopt(snapshotOf(*current, ++generation));  // changed content
        } else if (roll == 19) {
            scheduler.adopt(snapshotOf(*current, generation));  // same generation: a no-op by contract
        } else if (roll < 22) {
            scheduler.reset(roll == 20 ? kSr : 44100.0);  // repeated resets, with and without a rate change
        } else if (roll < 25) {
            // An incoming note somewhere in the block, and its release anywhere in 0..1023: often beyond a
            // smaller block, where it is clamped into it.
            const int p = pitch(rng);
            const int span = std::max(nFrames, 1);
            refusedThru += scheduler.pushThru(ThruEvent{true, static_cast<std::uint8_t>(p), 90, offset(rng) % span}) ? 0 : 1;
            refusedThru += scheduler.pushThru(ThruEvent{false, static_cast<std::uint8_t>(p), 0, offset(rng)}) ? 0 : 1;
            thruPushes += 2;
        } else if (roll == 25) {
            thru = !thru;  // the on -> off edge releases every thru note; disabled blocks discard the ring
            ++thruFlips;
        }
        if (fine(rng) == 0) {
            // A burst beyond the thru ring: the next block releases every thru note and discards the ring.
            for (int k = 0; k < 300; ++k) {
                refusedThru += scheduler.pushThru(ThruEvent{k % 2 == 0, static_cast<std::uint8_t>(pitch(rng)), 90, 0}) ? 0 : 1;
            }
            thruPushes += 300;
            ++forcedOverflows;
        }
        std::size_t written = 0;
        const BlockInfo info{ppq, nFrames, kSr, bpm, playing, thru};
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
    CHECK(thruPushes > 0);   // ...and thru notes
    CHECK(thruFlips > 0);    // ...and the thru switch
    CHECK(forcedOverflows > 0);
    CHECK(refusedThru > 0);  // ...and the overflow path
    (void) incompleteBlocks;  // seeks and stops may make blocks incomplete; that is not what is measured
    (void) refusedPreviews;
}

TEST_CASE("rt alloc: the triple buffer allocates nothing on the producer or the consumer side, empty acquires included") {
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
    int unexpectedFresh = 0;
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
            // Empty acquires: nothing new, so nullptr and nothing changes.
            for (int i = 0; i < 3; ++i) {
                if (buffer->acquire() != nullptr || buffer->current() != acquired || buffer->currentGeneration() != generation) {
                    ++unexpectedFresh;
                }
            }
        } else if (buffer->acquire() != nullptr && buffer->currentGeneration() != generation) {
            ++wrongGeneration;  // an acquire between the scheduled ones still hands over the latest
        }
    }
    CHECK(probe.count() == 0);
    CHECK(unexpectedNull == 0);
    CHECK(unexpectedFresh == 0);
    CHECK(wrongCurrent == 0);
    CHECK(wrongGeneration == 0);
}

TEST_CASE("rt alloc: the raw MIDI ring allocates nothing on push, pop or an overflow") {
    const auto ring = std::make_unique<chordlab::RawMidiRing>();  // off the stack, before the probe
    AllocationProbe probe;
    for (int round = 0; round < 100; ++round) {
        for (int i = 0; i < 300; ++i) ring->push(chordlab::RawMidi{0x90, std::uint8_t(i % 128), 100, 0, i});  // 44 refusals per round
        (void) ring->takeOverflow();
        chordlab::RawMidi m{};
        while (ring->pop(m)) {}
    }
    CHECK(probe.count() == 0);
}

TEST_CASE("rt alloc: handing over full 4096-event performed snapshots allocates nothing on the audio thread") {
    // 64 one-bar four-note chords, Subdivided: 64 x 8 hits x 4 notes x 2 = 4096 performed events, the most a
    // snapshot holds. Two such progressions (G7 and Dm7, strummed and humanised differently) are published in
    // turn through the plug-in's slot type; everything that allocates (the store, compile inside fillSlot)
    // happens outside the measured scopes.
    const auto buffer = std::make_unique<TripleBuffer<SnapshotSlot>>();
    Publication publication;
    const auto publishDoc = [&](const Chord& c, const Feel& feel) {
        DocumentStore store;
        for (int i = 0; i < 64; ++i) {
            store.appendChord(c, 4.0, 100);
        }
        store.setFeel(feel);
        SnapshotSlot& back = buffer->beginWrite();
        const bool filled = fillSlot(store.doc(), back, publication);
        buffer->publish(publication.generation);
        return filled ? back.count : 0;
    };
    std::array<MidiOut, 256> out{};
    Scheduler scheduler;
    scheduler.reset(kSr);
    int incomplete = 0;
    int missing = 0;
    std::size_t emitted = 0;
    const auto play = [&](double& ppq) {
        const SnapshotSlot* slot = buffer->acquire();
        if (slot == nullptr) {
            ++missing;
            return;
        }
        scheduler.adopt(Snapshot{slot->events.data(), slot->count, slot->endBeat, slot->loop,
                                 buffer->currentGeneration()});
        for (int block = 0; block < 2000; ++block) {
            std::size_t written = 0;
            const BlockInfo info{ppq, 512, kSr, 120.0, true, true};
            incomplete += scheduler.process(info, out.data(), out.size(), written) ? 0 : 1;
            emitted += written;
            ppq += 512.0 / 24000.0;
        }
    };

    const std::size_t firstCount =
        publishDoc(chord(7, "7"), Feel{Rhythm::Subdivided, Strum::DownUp, StrumSpeed::Medium, Humanise::Heavy});
    double ppq = 0.0;
    AllocationProbe first;
    play(ppq);
    const std::size_t firstAllocations = first.count();

    const std::size_t secondCount =
        publishDoc(chord(2, "m7"), Feel{Rhythm::Subdivided, Strum::Up, StrumSpeed::Loose, Humanise::Light});
    AllocationProbe second;
    play(ppq);
    const std::size_t secondAllocations = second.count();

    CHECK(firstCount == kMaxSnapshotEvents);
    CHECK(secondCount == kMaxSnapshotEvents);
    CHECK(firstAllocations == 0);
    CHECK(secondAllocations == 0);
    CHECK(missing == 0);
    CHECK(emitted > 1000);
    CHECK(incomplete == 0);  // 512-frame blocks at 120 bpm: the density never outruns a block
}

TEST_CASE("rt alloc: the heard ring and the MIDI adapter allocate nothing across 10k pushes, pops and messages") {
    HeardRing ring;  // constructed before the measured scope
    std::mt19937 rng(20261105);
    std::uniform_int_distribution<int> byte(0, 255);
    std::uniform_int_distribution<int> dice(0, 99);
    std::uint64_t pushed = 0;
    std::uint64_t popped = 0;
    std::uint64_t refused = 0;
    std::uint64_t overflows = 0;
    std::uint64_t heard = 0;
    std::uint64_t thru = 0;
    std::uint64_t order = 0;  // pops not strictly after the previous one (out of order or duplicate), must stay 0
    constexpr std::uint64_t kNone = ~std::uint64_t{0};
    std::uint64_t lastPop = kNone;

    AllocationProbe probe;
    for (int i = 0; i < 10000; ++i) {
        // Every 1000 iterations the first 400 only push, so the ring fills (256) and refuses; after that
        // single pops and the odd drain keep it moving.
        const HeardEvent event{HeardEvent::NoteOn, static_cast<std::uint8_t>(i & 127), 100,
                               static_cast<std::uint64_t>(i)};
        if (ring.push(event)) {
            ++pushed;
        } else {
            ++refused;
        }
        const int roll = i % 1000 < 400 ? 99 : dice(rng);
        if (roll < 30) {
            HeardEvent out{};
            if (ring.pop(out)) {
                ++popped;
                if (lastPop != kNone && out.sampleTime <= lastPop) {
                    ++order;
                }
                lastPop = out.sampleTime;
            }
        } else if (roll < 33) {
            HeardEvent out{};
            while (ring.pop(out)) {
                ++popped;
                if (lastPop != kNone && out.sampleTime <= lastPop) {
                    ++order;
                }
                lastPop = out.sampleTime;
            }
        }
        if (ring.takeOverflow()) {
            ++overflows;
        }
        const auto status = static_cast<std::uint8_t>(byte(rng));
        const auto d1 = static_cast<std::uint8_t>(byte(rng));
        const auto d2 = static_cast<std::uint8_t>(byte(rng));
        if (heardEventOf(status, d1, d2, static_cast<std::uint64_t>(i))) {
            ++heard;
        }
        if (thruEventOf(status, d1, d2, i & 1023)) {
            ++thru;
        }
    }
    const std::size_t allocations = probe.count();

    // Drain what is left (outside the measured scope): everything accepted must come out exactly once.
    {
        HeardEvent out{};
        while (ring.pop(out)) {
            ++popped;
            if (lastPop != kNone && out.sampleTime <= lastPop) {
                ++order;
            }
            lastPop = out.sampleTime;
        }
    }

    CHECK(allocations == 0);
    CHECK(popped == pushed);  // "pushed" counts accepted pushes (attempts minus refused)
    CHECK(refused > 0);  // the ring did fill up
    CHECK(overflows > 0);
    CHECK(pushed + refused == 10000);
    CHECK(order == 0);
    CHECK(popped <= pushed);
    CHECK(heard > 0);
    CHECK(thru > 0);
}
