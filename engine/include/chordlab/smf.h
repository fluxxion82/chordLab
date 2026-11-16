#pragma once

#include "chordlab/compiler.h"

#include <cstdint>
#include <string>
#include <string_view>
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
/// (`FF 58 04 num log2(den) 18 08`), when `trackName` is not empty a track name meta event (`FF 03 len
/// bytes`, the bytes of `trackName` as given; Live 12 on macOS names a clip dropped from the file after it,
/// reading the bytes as Mac Roman, so only ASCII names show as written there: the engine's own names are ASCII,
/// a user's section or track name with other characters is written as UTF-8 and shows wrong in Live; "Track 0"
/// without one), every note of `compiled` as a note-on (`9n`) or note-off (`8n`) on
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
/// outside 0..15; a `trackName` longer than 0x0FFFFFFF bytes, the largest four-byte meta length; an event
/// whose pitch or velocity is outside 0..127; a beat or `endBeat` that is
/// negative, NaN or infinite; a tick beyond 0x0FFFFFFF, the largest four-byte delta; or a track body
/// longer than its four-byte chunk length can describe (4 GiB: some 600 million events, which no
/// document reaches in practice, so this is a guard rather than a path the tests exercise).
std::vector<std::uint8_t> writeSmf0(const Compiled& compiled, double bpm, int num = 4, int den = 4,
                                    int tpqn = 480, int channel = 0, std::string_view trackName = {});

/// One lane of a format 1 file: the track name (`FF 03`, as `writeSmf0`'s `trackName`: empty writes no name
/// event) and the notes, as `Compiled::events` hold them (beats from 0; the lane's own order does not matter).
struct SmfLane {
    std::string name;
    std::vector<NoteEvent> events;
};

/// Serialises `lanes` as a format 1 Standard MIDI File (tracks design D12): an `MThd` chunk (format 1, `lanes.size()
/// + 1` tracks, `ppq` ticks per quarter note), a conductor `MTrk` holding the tempo meta event and the time
/// signature meta event (the bytes `writeSmf0` writes, `FF 51 03 ...` and `FF 58 04 ...`) and its end-of-track,
/// then one `MTrk` per lane in order: its name event when `name` is not empty, every event as a note-on or note-off
/// on channel 0 with an explicit status byte, and an end-of-track. Live turns each lane `MTrk` into a clip named
/// after it and skips the conductor.
///
/// Each lane is encoded exactly as `writeSmf0` encodes its one track (the same function): absolute tick
/// `lround(beat * ppq)`, deltas between the rounded ticks, a stable re-sort by (tick, off before on, pitch).
/// **Every `MTrk` ends at one tick**, the conductor's included: `lround(endBeat * ppq)`, or the tick of the last
/// event of any lane when that is later, so every lane has the same length, trailing silence survives, and a
/// lane whose last note ends early still ends there.
///
/// Returns an empty vector, writing nothing, when `writeSmf0` would refuse the same arguments (`bpm`, `num`, `den`,
/// `ppq` as its `tpqn`, a name or an event, an `endBeat` or a beat outside the encodable range; channel 0 is
/// always valid), when `lanes` is empty (there is no conductor-only file), or when there are more than 65534 lanes
/// (the track count is two bytes). A lane with no events is written as an empty named lane of full length.
std::vector<std::uint8_t> writeSmf1(const std::vector<SmfLane>& lanes, double bpm, int num, int den, int ppq,
                                    double endBeat);

/// Writes `bytes` to `path`, replacing any existing file there. Returns false on any error: the file
/// cannot be created or written, or `bytes` is empty (there is no empty MIDI file, and an empty vector
/// is how `writeSmf0` reports a failure), in which case nothing is written.
bool writeFile(const std::string& path, const std::vector<std::uint8_t>& bytes);

}  // namespace chordlab
