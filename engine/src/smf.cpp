#include "chordlab/smf.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <ios>
#include <optional>
#include <tuple>

namespace chordlab {

namespace {

using Bytes = std::vector<std::uint8_t>;

/// The largest absolute tick: a delta from tick 0 must fit a four-byte variable-length quantity.
constexpr long kMaxTick = 0x0FFFFFFF;

/// The largest tempo: microseconds per quarter note occupy three bytes.
constexpr long kMaxTempo = 0xFFFFFF;

/// Status bytes of the two channel messages written, before the channel is or'd into the low nibble.
constexpr std::uint8_t kNoteOff = 0x80;
constexpr std::uint8_t kNoteOn = 0x90;

/// An event with its beat already rounded to a tick.
struct TickEvent {
    long tick;
    bool on;
    int pitch;
    int velocity;
};

/// The sort key after quantisation: tick, then offs before ons, then ascending pitch.
bool keyLess(const TickEvent& a, const TickEvent& b) {
    return std::make_tuple(a.tick, a.on ? 1 : 0, a.pitch) < std::make_tuple(b.tick, b.on ? 1 : 0, b.pitch);
}

/// `lround(beat * tpqn)`, or nothing if the beat is negative, NaN or infinite, or the tick would not
/// fit a four-byte delta. The range check runs on the double so that `lround` is never handed a value
/// outside `long`.
std::optional<long> tickOf(double beat, int tpqn) {
    const double scaled = beat * tpqn;
    if (!(scaled >= 0.0 && scaled <= static_cast<double>(kMaxTick))) {
        return std::nullopt;
    }
    return std::lround(scaled);
}

/// Microseconds per quarter note at `bpm`, or nothing if it is not finite and positive or does not fit
/// three bytes. Checked on the double for the same reason as `tickOf`.
std::optional<long> tempoOf(double bpm) {
    if (!(std::isfinite(bpm) && bpm > 0.0)) {
        return std::nullopt;
    }
    const double micros = 60e6 / bpm;
    if (!(micros >= 0.5 && micros <= static_cast<double>(kMaxTempo))) {
        return std::nullopt;
    }
    return std::lround(micros);
}

/// log2 of `den` if it is a power of two in 1..128, else nothing.
std::optional<int> denominatorPower(int den) {
    if (den < 1 || den > 128 || (den & (den - 1)) != 0) {
        return std::nullopt;
    }
    int power = 0;
    while ((1 << power) != den) {
        ++power;
    }
    return power;
}

void appendBe16(Bytes& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

void appendBe32(Bytes& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
}

void append(Bytes& out, const Bytes& more) {
    out.insert(out.end(), more.begin(), more.end());
}

}  // namespace

Bytes vlq(std::uint32_t value) {
    Bytes bytes;
    bytes.push_back(static_cast<std::uint8_t>(value & 0x7F));
    value >>= 7;
    while (value != 0) {
        bytes.push_back(static_cast<std::uint8_t>((value & 0x7F) | 0x80));
        value >>= 7;
    }
    std::reverse(bytes.begin(), bytes.end());
    return bytes;
}

Bytes writeSmf0(const Compiled& compiled, double bpm, int num, int den, int tpqn, int channel) {
    const std::optional<long> tempo = tempoOf(bpm);
    const std::optional<int> denPower = denominatorPower(den);
    if (!tempo || !denPower || num < 1 || num > 255 || tpqn < 1 || tpqn > 32767 || channel < 0 || channel > 15) {
        return {};
    }
    const std::optional<long> endTick = tickOf(compiled.endBeat, tpqn);
    if (!endTick) {
        return {};
    }

    // Quantise every beat first, then re-sort: two events distinct in beats may share a tick, and at a
    // shared tick an off must still precede an on. The stable sort keeps input order for exact ties.
    std::vector<TickEvent> events;
    events.reserve(compiled.events.size());
    for (const NoteEvent& e : compiled.events) {
        const std::optional<long> tick = tickOf(e.beat, tpqn);
        if (!tick || e.pitch < 0 || e.pitch > 127 || e.velocity < 0 || e.velocity > 127) {
            return {};
        }
        events.push_back(TickEvent{*tick, e.on, e.pitch, e.velocity});
    }
    std::stable_sort(events.begin(), events.end(), keyLess);

    Bytes track;
    // Tempo: FF 51 03 tt tt tt, microseconds per quarter note, big-endian.
    append(track, {0x00, 0xFF, 0x51, 0x03});
    track.push_back(static_cast<std::uint8_t>((*tempo >> 16) & 0xFF));
    track.push_back(static_cast<std::uint8_t>((*tempo >> 8) & 0xFF));
    track.push_back(static_cast<std::uint8_t>(*tempo & 0xFF));
    // Time signature: FF 58 04 nn dd cc bb, with 24 MIDI clocks per metronome click and eight
    // thirty-second notes per quarter, the conventional values.
    append(track, {0x00, 0xFF, 0x58, 0x04, static_cast<std::uint8_t>(num), static_cast<std::uint8_t>(*denPower),
                   0x18, 0x08});

    long previous = 0;
    for (const TickEvent& e : events) {
        append(track, vlq(static_cast<std::uint32_t>(e.tick - previous)));
        previous = e.tick;
        track.push_back(static_cast<std::uint8_t>((e.on ? kNoteOn : kNoteOff) | channel));
        track.push_back(static_cast<std::uint8_t>(e.pitch));
        track.push_back(static_cast<std::uint8_t>(e.velocity));
    }

    // End of track at endBeat so trailing silence survives, never before the last event.
    const long eotTick = std::max(*endTick, previous);
    append(track, vlq(static_cast<std::uint32_t>(eotTick - previous)));
    append(track, {0xFF, 0x2F, 0x00});

    Bytes file;
    file.reserve(14 + 8 + track.size());
    append(file, {'M', 'T', 'h', 'd'});
    appendBe32(file, 6);
    appendBe16(file, 0);  // format 0
    appendBe16(file, 1);  // one track
    appendBe16(file, static_cast<std::uint16_t>(tpqn));
    append(file, {'M', 'T', 'r', 'k'});
    appendBe32(file, static_cast<std::uint32_t>(track.size()));
    append(file, track);
    return file;
}

bool writeFile(const std::string& path, const Bytes& bytes) {
    if (bytes.empty()) {
        return false;
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!out) {
        return false;
    }
    out.close();
    return !out.fail();
}

}  // namespace chordlab
