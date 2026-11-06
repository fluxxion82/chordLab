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
    const std::uint32_t head = head_.load(std::memory_order_relaxed);
    const std::uint32_t tail = tail_.load(std::memory_order_acquire);
    if (head - tail >= kCapacity) {
        overflow_.store(true, std::memory_order_release);
        return false;
    }
    slots_[head % kCapacity] = event;
    head_.store(head + 1, std::memory_order_release);
    return true;
}

bool HeardRing::pop(HeardEvent& out) noexcept {
    const std::uint32_t tail = tail_.load(std::memory_order_relaxed);
    const std::uint32_t head = head_.load(std::memory_order_acquire);
    if (head == tail) {
        return false;
    }
    out = slots_[tail % kCapacity];
    tail_.store(tail + 1, std::memory_order_release);
    return true;
}

bool HeardRing::takeOverflow() noexcept {
    return overflow_.exchange(false, std::memory_order_acq_rel);
}

}  // namespace chordlab
