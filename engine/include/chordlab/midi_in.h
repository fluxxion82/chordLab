#pragma once

#include "chordlab/listener.h"
#include "chordlab/scheduler.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace chordlab {

/// Normalises one incoming MIDI message (status byte with channel, two data bytes) into what the listener
/// and the scheduler take (design D1): note-on with velocity 0 is a note-off; CC 64 is `Sustain` with the
/// value as velocity; every other message is nothing. Channels are merged (the channel nibble is
/// ignored). `sampleTime` is stamped on the result unchanged; the result is never `Reset`, which only the
/// shell pushes. Data bytes are not range-checked (the listener and the scheduler ignore pitches >= 128
/// and clamp velocities). Audio thread; no allocation.
std::optional<HeardEvent> heardEventOf(std::uint8_t status, std::uint8_t data1, std::uint8_t data2,
                                       std::uint64_t sampleTime) noexcept;

/// The thru counterpart: note-on (velocity 0 is an off) and note-off only, with the pitch, the velocity
/// and `sampleOffset` carried over; CCs and everything else are nothing. Channels are merged. Audio
/// thread; no allocation.
std::optional<ThruEvent> thruEventOf(std::uint8_t status, std::uint8_t data1, std::uint8_t data2,
                                     int sampleOffset) noexcept;

/// A single-producer single-consumer ring of `HeardEvent` (design D1): the audio thread pushes, the main
/// thread pops. Lock-free (two atomic counters, acquire/release), allocation-free after construction.
///
/// Capacity: exactly `kCapacity` (256) events fit; the 257th push is refused. The two counters run freely
/// (they are never reduced modulo the capacity, only their low bits index the storage), so the ring is
/// full when `head - tail == kCapacity` and empty when `head == tail`, and no slot is wasted. Unsigned
/// wrap-around of the 32-bit counters is harmless because the capacity divides 2^32.
///
/// Overflow: `push` on a full ring returns false, drops the event and sets a sticky flag; `takeOverflow`
/// returns the flag and clears it, so the consumer can resynchronise (the listener forgets, design D1).
/// The consumer must call `takeOverflow()` *before* draining and, when it returns true, pop and discard
/// everything then resynchronise. The flag's release store and the exchange's acquire make every event
/// pushed before the refusal visible to those pops; draining first and taking the flag afterwards could
/// feed the listener a pre-drop note-on whose note-off was the refused event.
///
/// Threads: `push` belongs to the one producer thread, `pop` and `takeOverflow` to the one consumer
/// thread.
class HeardRing {
public:
    /// How many events fit.
    static constexpr std::size_t kCapacity = 256;

    HeardRing() noexcept = default;
    HeardRing(const HeardRing&) = delete;
    HeardRing& operator=(const HeardRing&) = delete;

    /// Producer. Appends `event`; returns false (and sets the overflow flag) when the ring already holds
    /// `kCapacity` events, dropping `event`.
    bool push(const HeardEvent& event) noexcept;

    /// Consumer. Moves the oldest event into `out` and returns true, or returns false (leaving `out`
    /// untouched) when the ring is empty.
    bool pop(HeardEvent& out) noexcept;

    /// Consumer. Whether any push was refused since the last call; clears the flag.
    bool takeOverflow() noexcept;

private:
    static_assert((kCapacity & (kCapacity - 1)) == 0 && 0x100000000ull % kCapacity == 0,
                  "the capacity must be a power of two so the 32-bit counters wrap cleanly");
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "the ring must never lock");
    static_assert(std::atomic<bool>::is_always_lock_free, "the ring must never lock");
    static_assert(std::is_trivially_copyable_v<HeardEvent>, "slots are copied while the other thread may read others");

    std::array<HeardEvent, kCapacity> slots_{};
    alignas(64) std::atomic<std::uint32_t> head_{0};  // next slot to write; written by the producer
    alignas(64) std::atomic<std::uint32_t> tail_{0};  // next slot to read; written by the consumer
    std::atomic<bool> overflow_{false};
};

}  // namespace chordlab
