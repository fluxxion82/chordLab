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

/// Epochs compare modulo 2^32: `a` is newer than `b` when `int32_t(a - b) > 0`.
inline bool newerEpoch(std::uint32_t a, std::uint32_t b) noexcept { return static_cast<std::int32_t>(a - b) > 0; }

/// The render thread's policy for the raw ring, once per block, before anything else touches the scheduler.
/// `applied` is the render thread's record of the last epoch whose reset it performed; `current` is the
/// shell's epoch loaded once at the top of the block. Pops at most `RawMidiRing::kCapacity` messages (a
/// producer that keeps pushing cannot make the loop endless; what it adds meanwhile waits for the next
/// block). For each popped message, in order: a message whose epoch is older than `current` was queued
/// before the host's last reset and is skipped (the reset forgets it); otherwise, if its epoch is not
/// `applied`, `onReset()` runs first and `applied` becomes that epoch (the reset lands in FIFO position
/// before the first message that followed it, even when the bump happened after `current` was loaded);
/// then `handle(const RawMidi&)`. Epochs are monotone in push order (the one producer loads the epoch, then
/// pushes), so a message that is neither older than `current` nor `applied` carries `current` or newer.
/// After the loop, if `current` is newer than `applied`, `onReset()` runs and `applied` becomes `current`
/// (a reset with no message after it still lands this block). The overflow flag is taken first, as the
/// heard ring's rule says: when it reads true, messages were lost, so everything still queued is discarded
/// unread (bounded the same way), `applied` becomes `current`, `onReset` is NOT called (the caller performs
/// its own, larger forget: heard `Reset`, `dropThru`, and counts the loss) and `Forgot` is returned.
/// `onReset` and `handle` must not allocate, block or throw (the drain is `noexcept`); they run on the
/// render thread.
template <typename OnReset, typename Handle>
RawDrain drainRawMidi(RawMidiRing& ring, std::uint32_t& applied, std::uint32_t current, OnReset&& onReset,
                      Handle&& handle) noexcept {
    RawMidi m{};
    if (ring.takeOverflow()) {
        for (std::size_t n = 0; n < RawMidiRing::kCapacity && ring.pop(m); ++n) {
        }
        applied = current;
        return RawDrain::Forgot;
    }
    for (std::size_t n = 0; n < RawMidiRing::kCapacity && ring.pop(m); ++n) {
        if (newerEpoch(current, m.epoch)) {
            continue;  // queued before the last reset: forgotten
        }
        if (m.epoch != applied) {
            onReset();
            applied = m.epoch;
        }
        handle(m);
    }
    if (newerEpoch(current, applied)) {
        onReset();
        applied = current;
    }
    return RawDrain::Handled;
}

}  // namespace chordlab
