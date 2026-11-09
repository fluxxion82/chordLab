#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace chordlab {

/// A single-producer single-consumer ring of trivially copyable `T` (Listening design D1, generalised for
/// the Audio Unit's raw MIDI input, Release design D13): one thread pushes, one other thread pops.
/// Lock-free (two atomic counters, acquire/release), allocation-free after construction.
///
/// Capacity: exactly `N` items fit; the next push is refused. The two counters run freely (they are never
/// reduced modulo the capacity, only their low bits index the storage), so the ring is full when
/// `head - tail == N` and empty when `head == tail`, and no slot is wasted. Unsigned wrap-around of the
/// 32-bit counters is harmless because `N` divides 2^32.
///
/// Overflow: `push` on a full ring returns false, drops the item and sets a sticky flag; `takeOverflow`
/// returns the flag and clears it, so the consumer can resynchronise. The consumer must call
/// `takeOverflow()` *before* draining and, when it returns true, pop and discard everything, then
/// resynchronise (the listener forgets; the AU shell also drops its thru notes). The flag's release
/// store and the exchange's acquire make every item pushed before the refusal visible to those pops.
/// Draining first and taking the flag afterwards could hand the consumer a pre-drop note-on whose note-off
/// was the refused item.
///
/// Threads: `push` belongs to the one producer thread, `pop` and `takeOverflow` to the one consumer thread.
template <typename T, std::size_t N>
class SpscRing {
public:
    static constexpr std::size_t kCapacity = N;

    SpscRing() noexcept = default;
    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;

    /// Producer. Appends `item`; returns false (and sets the overflow flag) when the ring already holds
    /// `kCapacity` items, dropping `item`.
    bool push(const T& item) noexcept {
        // Relaxed: only the producer writes head_, so it reads its own last value.
        const std::uint32_t head = head_.load(std::memory_order_relaxed);
        // Acquire: pairs with the consumer's release store of tail_; the consumer's read of a slot
        // happens-before this push overwrites that slot.
        const std::uint32_t tail = tail_.load(std::memory_order_acquire);
        if (head - tail >= kCapacity) {
            // Release: every push before this refusal happens-before the consumer's acquire in
            // takeOverflow, so a consumer that sees the flag also sees all earlier items.
            overflow_.store(true, std::memory_order_release);
            return false;
        }
        slots_[head % kCapacity] = item;
        // Release: the slot's content happens-before the consumer's acquire load of head_ that sees it.
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    /// Consumer. Copies the oldest item into `out` and returns true, or returns false (leaving `out`
    /// untouched) when the ring is empty.
    bool pop(T& out) noexcept {
        // Relaxed: only the consumer writes tail_, so it reads its own last value.
        const std::uint32_t tail = tail_.load(std::memory_order_relaxed);
        // Acquire: pairs with the producer's release store of head_; the slot's content happens-before this read.
        const std::uint32_t head = head_.load(std::memory_order_acquire);
        if (head == tail) {
            return false;
        }
        out = slots_[tail % kCapacity];
        // Release: the read of the slot happens-before the producer's acquire load of tail_ that sees the
        // slot as free, so the producer never overwrites a slot still being read.
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    /// Consumer. Whether any push was refused since the last call; clears the flag.
    bool takeOverflow() noexcept {
        // Acquire half: pairs with the release store in push. Release half: orders the clear after this
        // thread's earlier pops.
        return overflow_.exchange(false, std::memory_order_acq_rel);
    }

private:
    static_assert(N > 0 && (N & (N - 1)) == 0 && N <= (std::size_t{1} << 31),
                  "the capacity must be a power of two (below 2^32) so the 32-bit counters wrap cleanly");
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "the ring must never lock");
    static_assert(std::atomic<bool>::is_always_lock_free, "the ring must never lock");
    static_assert(std::is_trivially_copyable_v<T>, "slots are copied while the other thread may read others");

    std::array<T, N> slots_{};
    alignas(64) std::atomic<std::uint32_t> head_{0};  // next slot to write; written by the producer
    alignas(64) std::atomic<std::uint32_t> tail_{0};  // next slot to read; written by the consumer
    std::atomic<bool> overflow_{false};
};

}  // namespace chordlab
