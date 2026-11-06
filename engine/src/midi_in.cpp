#include "chordlab/midi_in.h"

namespace chordlab {

namespace {

constexpr std::uint8_t kNoteOff = 0x80;
constexpr std::uint8_t kNoteOn = 0x90;
constexpr std::uint8_t kControlChange = 0xB0;
constexpr std::uint8_t kSustainController = 64;

}  // namespace

std::optional<HeardEvent> heardEventOf(std::uint8_t status, std::uint8_t data1, std::uint8_t data2,
                                       std::uint64_t sampleTime) noexcept {
    switch (status & 0xF0) {
        case kNoteOn:
            return HeardEvent{data2 == 0 ? HeardEvent::NoteOff : HeardEvent::NoteOn, data1, data2, sampleTime};
        case kNoteOff:
            return HeardEvent{HeardEvent::NoteOff, data1, data2, sampleTime};
        case kControlChange:
            if (data1 == kSustainController) {
                return HeardEvent{HeardEvent::Sustain, 0, data2, sampleTime};
            }
            return std::nullopt;
        default:
            return std::nullopt;
    }
}

std::optional<ThruEvent> thruEventOf(std::uint8_t status, std::uint8_t data1, std::uint8_t data2,
                                     int sampleOffset) noexcept {
    switch (status & 0xF0) {
        case kNoteOn:
            return ThruEvent{data2 != 0, data1, data2, sampleOffset};
        case kNoteOff:
            return ThruEvent{false, data1, data2, sampleOffset};
        default:
            return std::nullopt;
    }
}

bool HeardRing::push(const HeardEvent& event) noexcept {
    // Relaxed: only the producer writes head_, so it reads its own last value.
    const std::uint32_t head = head_.load(std::memory_order_relaxed);
    // Acquire: pairs with the consumer's release store of tail_; the consumer's read of a slot
    // happens-before this push overwrites that slot.
    const std::uint32_t tail = tail_.load(std::memory_order_acquire);
    if (head - tail >= kCapacity) {
        // Release: every push before this refusal happens-before the consumer's acquire in takeOverflow,
        // so a consumer that sees the flag also sees all earlier events.
        overflow_.store(true, std::memory_order_release);
        return false;
    }
    slots_[head % kCapacity] = event;
    // Release: the slot's content happens-before the consumer's acquire load of head_ that sees it.
    head_.store(head + 1, std::memory_order_release);
    return true;
}

bool HeardRing::pop(HeardEvent& out) noexcept {
    // Relaxed: only the consumer writes tail_, so it reads its own last value.
    const std::uint32_t tail = tail_.load(std::memory_order_relaxed);
    // Acquire: pairs with the producer's release store of head_; the slot's content is visible before
    // it is read.
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

bool HeardRing::takeOverflow() noexcept {
    // Acquire half: pairs with the release store in push, making every event pushed before the refusal
    // visible to the pops that follow. Release half: orders the clear after this thread's earlier pops.
    return overflow_.exchange(false, std::memory_order_acq_rel);
}

}  // namespace chordlab
