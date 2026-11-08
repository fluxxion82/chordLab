#pragma once

#include "chordlab/listener.h"
#include "chordlab/scheduler.h"
#include "chordlab/spsc_ring.h"

#include <cstddef>
#include <cstdint>
#include <optional>

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

/// The heard ring (Listening design D1): the audio thread pushes, the main thread pops; 256 events.
using HeardRing = SpscRing<HeardEvent, 256>;

/// One MIDI message as a host hands it over, before any interpretation: status byte with channel, two data
/// bytes, the shell's reset epoch when it was queued (opaque here; see `drainRawMidi`), and the sample
/// offset within the block it belongs to (Release design D13). The Audio Unit's shell queues these from
/// the host's MIDIEvent thread and drains them on the render thread.
struct RawMidi {
    std::uint8_t status;
    std::uint8_t data1;
    std::uint8_t data2;
    std::uint32_t epoch;
    int sampleOffset;
};

/// Producer: the host's MIDI delivery thread; consumer: the render thread (the AU shell's ProcessBlock).
using RawMidiRing = SpscRing<RawMidi, 256>;

/// What the drain of the raw ring did (Release design D13).
enum class RawDrain { Handled, Forgot };

/// The render thread's policy for the raw ring, once per block, before anything else touches the scheduler:
/// pops at most `RawMidiRing::kCapacity` messages (a producer that keeps pushing cannot make the loop
/// endless; what it adds meanwhile waits for the next block), skips the messages whose `epoch` is not
/// `epoch` (they were queued before the host's last reset, which forgets everything, this included), and
/// calls `handle(const RawMidi&)` for the rest, in order. The overflow flag is taken first, as the heard
/// ring's rule says: when it reads true, messages were lost, so everything still queued is discarded unread
/// (bounded the same way) and `Forgot` is returned; the caller then forgets (heard `Reset`, `dropThru`) and
/// counts the loss. `handle` must not allocate or block; it runs on the render thread.
template <typename Handle>
RawDrain drainRawMidi(RawMidiRing& ring, std::uint32_t epoch, Handle&& handle) noexcept {
    RawMidi m{};
    if (ring.takeOverflow()) {
        for (std::size_t n = 0; n < RawMidiRing::kCapacity && ring.pop(m); ++n) {
        }
        return RawDrain::Forgot;
    }
    for (std::size_t n = 0; n < RawMidiRing::kCapacity && ring.pop(m); ++n) {
        if (m.epoch == epoch) {
            handle(m);
        }
    }
    return RawDrain::Handled;
}

}  // namespace chordlab
