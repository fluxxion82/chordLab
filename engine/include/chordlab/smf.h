#pragma once

#include "chordlab/compiler.h"

#include <cstdint>
#include <string>
#include <vector>

namespace chordlab {

/// MIDI variable-length quantity of `value`: big-endian groups of seven bits, every byte but the last
/// with its top bit set, so 0 is `00`, 127 is `7F`, 128 is `81 00` and 0x0FFFFFFF is `FF FF FF 7F`.
/// The SMF specification allows at most four bytes (values up to 0x0FFFFFFF); a larger value is encoded
/// faithfully in five, which no reader accepts, so `writeSmf0` never emits one.
std::vector<std::uint8_t> vlq(std::uint32_t value);

/// Serialises `compiled` as a format 0 Standard MIDI File: an `MThd` chunk (format 0, one track,
/// `tpqn` ticks per quarter note) and one `MTrk` chunk holding, in order, a tempo meta event
/// (`FF 51 03`, `lround(60e6 / bpm)` microseconds per quarter), a time signature meta event
/// (`FF 58 04 num log2(den) 18 08`), every note of `compiled` as a note-on (`9n`) or note-off (`8n`) on
/// channel `channel` with an explicit status byte (no running status), and an end-of-track meta event
/// (`FF 2F 00`).
///
/// Timing: each event's absolute tick is `lround(beat * tpqn)`; the deltas are taken between those
/// rounded ticks, so rounding never accumulates. Events are then re-sorted by (tick, off before on,
/// pitch) because two events distinct in beats may collapse onto one tick; the sort is stable, so an
/// exact tie keeps its input order. The end-of-track sits at `lround(endBeat * tpqn)` so trailing
/// silence survives, or at the last event's tick if that is later.
///
/// Returns an empty vector, writing nothing, when the file cannot be well formed: `bpm` not finite and
/// positive, or so slow or fast that the tempo does not fit its three bytes (about 3.6 bpm to 1.2e8
/// bpm); `num` outside 1..255; `den` not a power of two in 1..128; `tpqn` outside 1..32767; `channel`
/// outside 0..15; an event whose pitch or velocity is outside 0..127; a beat or `endBeat` that is
/// negative, NaN or infinite; or a tick beyond 0x0FFFFFFF, the largest four-byte delta.
std::vector<std::uint8_t> writeSmf0(const Compiled& compiled, double bpm, int num = 4, int den = 4,
                                    int tpqn = 480, int channel = 0);

/// Writes `bytes` to `path`, replacing any existing file there. Returns false on any error: the file
/// cannot be created or written, or `bytes` is empty (there is no empty MIDI file, and an empty vector
/// is how `writeSmf0` reports a failure), in which case nothing is written.
bool writeFile(const std::string& path, const std::vector<std::uint8_t>& bytes);

}  // namespace chordlab
