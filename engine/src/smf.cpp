#include "chordlab/smf.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <ios>
#include <limits>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>

namespace chordlab {

namespace {

using Bytes = std::vector<std::uint8_t>;

/// The largest absolute tick: a delta from tick 0 must fit a four-byte variable-length quantity.
constexpr long kMaxTick = 0x0FFFFFFF;

/// The largest tempo: microseconds per quarter note occupy three bytes.
constexpr long kMaxTempo = 0xFFFFFF;

/// The longest meta event payload: its length is a variable-length quantity of at most four bytes.
constexpr std::size_t kMaxMetaLength = 0x0FFFFFFF;

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

namespace {

/// The meta events every file opens its first track with: tempo, `FF 51 03 tt tt tt` (microseconds per quarter
/// note, big-endian), then the time signature, `FF 58 04 nn dd cc bb` with 24 MIDI clocks per metronome click and
/// eight thirty-second notes per quarter, the conventional values.
void appendTempoAndMeter(Bytes& track, long tempo, int num, int denPower) {
    append(track, {0x00, 0xFF, 0x51, 0x03});
    track.push_back(static_cast<std::uint8_t>((tempo >> 16) & 0xFF));
    track.push_back(static_cast<std::uint8_t>((tempo >> 8) & 0xFF));
    track.push_back(static_cast<std::uint8_t>(tempo & 0xFF));
    append(track, {0x00, 0xFF, 0x58, 0x04, static_cast<std::uint8_t>(num), static_cast<std::uint8_t>(denPower), 0x18,
                   0x08});
}

/// Every event of `events` quantised to a tick and re-sorted, or nothing when one cannot be encoded. Two events
/// distinct in beats may share a tick, and at a shared tick an off must still precede an on; the stable sort keeps
/// input order for exact ties.
std::optional<std::vector<TickEvent>> quantise(const std::vector<NoteEvent>& source, int tpqn) {
    std::vector<TickEvent> events;
    events.reserve(source.size());
    for (const NoteEvent& e : source) {
        const std::optional<long> tick = tickOf(e.beat, tpqn);
        if (!tick || e.pitch < 0 || e.pitch > 127 || e.velocity < 0 || e.velocity > 127) {
            return std::nullopt;
        }
        events.push_back(TickEvent{*tick, e.on, e.pitch, e.velocity});
    }
    std::stable_sort(events.begin(), events.end(), keyLess);
    return events;
}

/// The tick of the last event, 0 for none.
long lastTick(const std::vector<TickEvent>& events) {
    return events.empty() ? 0 : events.back().tick;
}

/// One track's body after any tempo and meter: the name event when `name` is not empty (`FF 03 len bytes`), every
/// event as a channel message with an explicit status byte, and the end-of-track at `endTick`, which must not be
/// before the last event. Appended to `track`.
void appendNameEventsAndEnd(Bytes& track, std::string_view name, const std::vector<TickEvent>& events, int channel,
                            long endTick) {
    if (!name.empty()) {
        append(track, {0x00, 0xFF, 0x03});
        append(track, vlq(static_cast<std::uint32_t>(name.size())));
        track.insert(track.end(), name.begin(), name.end());
    }
    long previous = 0;
    for (const TickEvent& e : events) {
        append(track, vlq(static_cast<std::uint32_t>(e.tick - previous)));
        previous = e.tick;
        track.push_back(static_cast<std::uint8_t>((e.on ? kNoteOn : kNoteOff) | channel));
        track.push_back(static_cast<std::uint8_t>(e.pitch));
        track.push_back(static_cast<std::uint8_t>(e.velocity));
    }
    assert(endTick >= previous);
    append(track, vlq(static_cast<std::uint32_t>(endTick - previous)));
    append(track, {0xFF, 0x2F, 0x00});
}

/// An `MTrk` chunk around `body`, appended to `file`.
void appendTrackChunk(Bytes& file, const Bytes& body) {
    append(file, {'M', 'T', 'r', 'k'});
    appendBe32(file, static_cast<std::uint32_t>(body.size()));
    append(file, body);
}

/// The MTrk length is four bytes. Reaching this needs some 600 million events (each at most seven bytes), which no
/// document produces in practice; it is a guard, not a path the tests exercise.
bool fitsChunk(const Bytes& body) {
    return body.size() <= std::numeric_limits<std::uint32_t>::max();
}

}  // namespace

Bytes writeSmf0(const Compiled& compiled, double bpm, int num, int den, int tpqn, int channel,
                std::string_view trackName) {
    const std::optional<long> tempo = tempoOf(bpm);
    const std::optional<int> denPower = denominatorPower(den);
    if (!tempo || !denPower || num < 1 || num > 255 || tpqn < 1 || tpqn > 32767 || channel < 0 || channel > 15 ||
        trackName.size() > kMaxMetaLength) {
        return {};
    }
    const std::optional<long> endTick = tickOf(compiled.endBeat, tpqn);
    if (!endTick) {
        return {};
    }
    const std::optional<std::vector<TickEvent>> events = quantise(compiled.events, tpqn);
    if (!events) {
        return {};
    }

    Bytes track;
    appendTempoAndMeter(track, *tempo, num, *denPower);
    // End of track at endBeat so trailing silence survives, never before the last event.
    appendNameEventsAndEnd(track, trackName, *events, channel, std::max(*endTick, lastTick(*events)));
    if (!fitsChunk(track)) {
        return {};
    }

    Bytes file;
    file.reserve(14 + 8 + track.size());
    append(file, {'M', 'T', 'h', 'd'});
    appendBe32(file, 6);
    appendBe16(file, 0);  // format 0
    appendBe16(file, 1);  // one track
    appendBe16(file, static_cast<std::uint16_t>(tpqn));
    appendTrackChunk(file, track);
    return file;
}

Bytes writeSmf1(const std::vector<SmfLane>& lanes, double bpm, int num, int den, int ppq, double endBeat) {
    const std::optional<long> tempo = tempoOf(bpm);
    const std::optional<int> denPower = denominatorPower(den);
    // The track count (lanes + the conductor) is two bytes.
    if (lanes.empty() || lanes.size() > 0xFFFE || !tempo || !denPower || num < 1 || num > 255 || ppq < 1 ||
        ppq > 32767) {
        return {};
    }
    const std::optional<long> endTick = tickOf(endBeat, ppq);
    if (!endTick) {
        return {};
    }
    // Quantise every lane first: the one end tick every MTrk shares is known only after the last lane's last event.
    std::vector<std::vector<TickEvent>> quantised;
    quantised.reserve(lanes.size());
    long sharedEnd = *endTick;
    for (const SmfLane& lane : lanes) {
        std::optional<std::vector<TickEvent>> events = quantise(lane.events, ppq);
        if (!events || lane.name.size() > kMaxMetaLength) {
            return {};
        }
        sharedEnd = std::max(sharedEnd, lastTick(*events));
        quantised.push_back(std::move(*events));
    }

    // The conductor: tempo, meter and the end-of-track, at the shared end tick like every lane.
    Bytes conductor;
    appendTempoAndMeter(conductor, *tempo, num, *denPower);
    appendNameEventsAndEnd(conductor, {}, {}, 0, sharedEnd);
    if (!fitsChunk(conductor)) {
        return {};
    }

    Bytes file;
    append(file, {'M', 'T', 'h', 'd'});
    appendBe32(file, 6);
    appendBe16(file, 1);  // format 1
    appendBe16(file, static_cast<std::uint16_t>(lanes.size() + 1));
    appendBe16(file, static_cast<std::uint16_t>(ppq));
    appendTrackChunk(file, conductor);
    for (std::size_t i = 0; i < lanes.size(); ++i) {
        Bytes body;
        appendNameEventsAndEnd(body, lanes[i].name, quantised[i], 0, sharedEnd);
        if (!fitsChunk(body)) {
            return {};
        }
        appendTrackChunk(file, body);
    }
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
