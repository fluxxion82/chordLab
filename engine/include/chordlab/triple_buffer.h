#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace chordlab {

/// Hands a value from one producer thread to one consumer thread without locks, allocation or waiting:
/// the classic triple buffer. The plug-in uses it to publish compiled progressions from the main thread
/// to the audio thread, but it carries any `T` that is nothrow default constructible.
///
/// Three slots: the producer owns the back slot, the consumer owns the front slot, and the middle slot
/// is the one in transit. `beginWrite` hands out the back slot; `publish` swaps it with the middle slot
/// and marks the middle fresh; `acquire` swaps a fresh middle with the front. Each swap is one atomic
/// exchange on `middle_`, which packs the middle slot's index (two bits) with the fresh bit; the
/// generation travels inside the slot. Because the producer only ever writes the slot it got back from
/// an exchange, and the consumer only ever gives up its *previous* front, the slot `acquire` or `current`
/// returned is never written until the consumer's next successful `acquire`.
///
/// Coalescing: the producer may `beginWrite` / `publish` any number of times between acquires; an
/// unread middle is simply replaced, so the consumer always gets the latest publish, never a backlog.
/// Generations: the producer passes a generation to each `publish`; if it passes increasing values the
/// consumer sees them increase (it may skip some, never go back). The consumer learns a generation only
/// through `currentGeneration`, so the content it belongs to is always the one in `current`.
///
/// Threads: `beginWrite` and `publish` belong to one producer thread at a time (callers that switch
/// producer threads must order the calls themselves, with a mutex for instance); `acquire`, `current`
/// and `currentGeneration` belong to the consumer thread. Nothing here allocates, throws, locks or
/// waits after construction. `T` must not throw from its default constructor (asserted), and the slots
/// are value-initialized once, at construction.
template <class T>
class TripleBuffer {
    static_assert(std::is_nothrow_default_constructible_v<T>, "a slot must construct without throwing");
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free, "the handoff must never lock");

public:
    TripleBuffer() noexcept : middle_(kInitialMiddle), back_(kInitialBack), front_(kInitialFront), acquired_(false) {}

    /// Producer. The back slot, for the producer to fill before `publish`. It is never a slot the consumer
    /// may read, and it keeps whatever it held before (the content of some earlier publish, or the initial
    /// value), so the producer must write every field it cares about. Calling it twice without a `publish`
    /// returns the same slot.
    T& beginWrite() noexcept { return slots_[back_].value; }

    /// Producer. Stamps the back slot with `generation` and makes it the fresh middle; the slot that was
    /// in the middle (read or not) becomes the new back. Publishing without a `beginWrite` publishes the
    /// back slot as it is.
    void publish(std::uint32_t generation) noexcept {
        slots_[back_].generation = generation;
        // Release: the slot's content happens-before any consumer acquire that sees it. Acquire: the
        // consumer's reads of the slot we are about to take as the new back happen-before our writes to it.
        const std::uint32_t previous = middle_.exchange(back_ | kFresh, std::memory_order_acq_rel);
        back_ = previous & kIndexMask;
    }

    /// Consumer. If a publish is waiting, takes it as the new front and returns it; otherwise returns
    /// nullptr and changes nothing. The pointer, like `current`, stays valid and unmodified until the next
    /// call to `acquire` that returns non-null.
    const T* acquire() noexcept {
        if ((middle_.load(std::memory_order_acquire) & kFresh) == 0) {
            return nullptr;
        }
        // Only the producer writes `middle_` between the check and the exchange, and it always writes a
        // fresh index, so what comes back is a fresh slot. Release: our reads of the old front happen-before
        // the producer's writes to it once it takes it as its back. Acquire: the published content is visible.
        const std::uint32_t previous = middle_.exchange(front_, std::memory_order_acq_rel);
        front_ = previous & kIndexMask;
        acquired_ = true;
        return &slots_[front_].value;
    }

    /// Consumer. The front slot: what the last successful `acquire` returned, or nullptr before the first.
    const T* current() const noexcept { return acquired_ ? &slots_[front_].value : nullptr; }

    /// Consumer. The generation the front slot was published with, or 0 before the first `acquire`.
    std::uint32_t currentGeneration() const noexcept { return acquired_ ? slots_[front_].generation : 0; }

private:
    struct Slot {
        T value{};
        std::uint32_t generation = 0;
    };

    static constexpr std::uint32_t kIndexMask = 0x3;  // bits 0-1: the middle slot's index
    static constexpr std::uint32_t kFresh = 0x4;      // bit 2: the middle holds an unread publish
    static constexpr std::uint32_t kInitialBack = 0;
    static constexpr std::uint32_t kInitialMiddle = 1;  // not fresh: nothing to acquire yet
    static constexpr std::uint32_t kInitialFront = 2;

    std::array<Slot, 3> slots_;
    std::atomic<std::uint32_t> middle_;  // index | kFresh, the only word both threads touch
    std::uint32_t back_;                 // producer-private
    std::uint32_t front_;                // consumer-private
    bool acquired_;                      // consumer-private: front_ holds a published value
};

}  // namespace chordlab
