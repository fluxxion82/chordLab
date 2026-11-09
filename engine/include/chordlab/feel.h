#pragma once

#include "chordlab/meter.h"

#include <cstdint>
#include <vector>

namespace chordlab {

struct Document;

/// How a chord's notes are spread over its span (see the feel plan, D4-D7, and the arpeggio design, D1).
enum class Rhythm { Block, Pulse, Subdivided, Offbeats, LowThenUpper, Arpeggio };

/// The order an arpeggio visits its notes (arpeggio design D2). Live's Arpeggiator styles, plus Alberti; Live's
/// "Play Order" slot is `RootFirst` (chord tones from the root), its "Chord Trigger" is `Chord`.
enum class ArpPattern {
    Up, Down, UpDown, DownUp, UpAndDown, DownAndUp, Converge, Diverge, ConAndDiverge,
    PinkyUp, PinkyUpDown, ThumbUp, ThumbUpDown, Alberti, RootFirst, Chord, Random
};
constexpr int kArpPatternCount = 17;

/// The grid an arpeggio steps on (D3): the meter's pulse, its subdivision, or half the subdivision.
enum class ArpRate { Pulse, Subdivision, Half };

/// How many octaves an arpeggio spans (D4): the voicing, or the voicing and the same an octave up.
enum class ArpRange { One, Two };

/// Direction of the strum that staggers a hit's note onsets.
enum class Strum { Off, Down, Up, DownUp };

/// How far apart the strummed notes start.
enum class StrumSpeed { Tight, Medium, Loose };

/// Amount of random timing and velocity variation.
enum class Humanise { Off, Light, Medium, Heavy };

/// The playing feel of the whole progression. Defaults: block chords, no strum, medium speed, no humanise;
/// the arpeggio fields (heard only while `rhythm == Arpeggio`) default to up, the subdivision, one octave.
struct Feel {
    Rhythm rhythm = Rhythm::Block;
    Strum strum = Strum::Off;
    StrumSpeed speed = StrumSpeed::Medium;
    Humanise humanise = Humanise::Off;
    ArpPattern pattern = ArpPattern::Up;
    ArpRate rate = ArpRate::Subdivision;
    ArpRange range = ArpRange::One;
};

inline bool operator==(const Feel& a, const Feel& b) {
    return a.rhythm == b.rhythm && a.strum == b.strum && a.speed == b.speed && a.humanise == b.humanise &&
           a.pattern == b.pattern && a.rate == b.rate && a.range == b.range;
}

inline bool operator!=(const Feel& a, const Feel& b) { return !(a == b); }

/// Shortest gated note, in ticks (D6).
constexpr int kMinNoteTicks = 5;

/// One hit: tick span after the gate, the indices it plays into the chord's note list (the voicing, ascending;
/// for an arpeggio the arpeggio note list), its velocity
/// before humanise, and whether it is on a pulse or the chord's start (for the down-up strum).
struct Hit {
    int start;
    int end;
    std::vector<int> notes;
    int velocity;
    bool strong;
};

/// Which index of an `n`-note arpeggio list the `k`-th hit plays (arpeggio design D2), for the index
/// patterns: everything but `Chord` (every note plays) and `Random` (`arpRandomIndex`). `k >= 0`, `n >= 1`;
/// `n == 1` is always 0. `RootFirst` is `Up` (the list itself is ordered by chord tone, `arpeggioNotes`).
int arpIndex(ArpPattern pattern, int k, int n);

/// Ticks between arpeggio hits at `rate` in `meter` (D3): the pulse, the subdivision, or half of it (4/4: 960, 480, 240; 6/8: 1440, 480, 240; 7/8: 480, 240, 120).
int arpStepTicks(ArpRate rate, Meter meter);

/// The `Random` pattern's index for hit `k` of an `n`-note list (D2): hit 0 (`previous < 0`) plays
/// `feelKey(id, 0, 0, kFeelSaltArp) % n`; a later hit plays the r-th of the n - 1 indices other than `previous`,
/// `r = feelKey(id, k, 0, kFeelSaltArp) % (n - 1)`, so no note repeats immediately in the generated hit order
/// (humanise may still move two hits' notes around each other). `n == 1` is always 0. Pure in the chord's event
/// id: playback equals export, undo keeps the order, moving the chord keeps it, re-adding a preset re-rolls.
/// Pinned sequences (independent Python splitmix64, test_feel.cpp): id 1, n 4: 2 1 3 0 3 0 3 0; id 1, n 3:
/// 1 0 2 0 2 0 1 2.
int arpRandomIndex(int id, int k, int n, int previous);

/// The hits of a chord spanning ticks [s, e) of the whole progression (D4-D7 of the feel plan). Bars count
/// from tick 0 of the progression. Empty when the span or `noteCount` is empty. For an arpeggio, `noteCount`
/// is the size of the chord's arpeggio note list (`arpeggioNotes`) and each hit's `notes` index that list;
/// `id` is the chord's event id, read by `ArpPattern::Random` only.
std::vector<Hit> rhythmHits(const Feel& feel, Meter meter, int s, int e, int noteCount, int velocity, int id = 0);

/// `rhythmHits(Feel{rhythm}, ...)`: the rhythm with the default feel (an arpeggio plays up, on the
/// subdivision, over one octave).
inline std::vector<Hit> rhythmHits(Rhythm rhythm, Meter meter, int s, int e, int noteCount, int velocity) {
    return rhythmHits(Feel{rhythm}, meter, s, e, noteCount, velocity, 0);
}

inline bool operator==(const Hit& a, const Hit& b) {
    return a.start == b.start && a.end == b.end && a.notes == b.notes && a.velocity == b.velocity && a.strong == b.strong;
}

// --- Humanise hash (D9) ----------------------------------------------------------------------------------
//
// Every humanise offset is a pure function of the chord's event id and the position of what it moves, so a
// chord keeps its feel when it is moved, edited or restored by undo (its id survives), and a preset that
// replaces the progression (fresh ids) gets a new one. The offsets of one chord never depend on another
// chord's id or position.

/// Salts of `feelKey`: which quantity an offset moves.
constexpr int kFeelSaltHitTiming = 1;   // a whole hit, in ticks; `note` is 0
constexpr int kFeelSaltNoteTiming = 2;  // one note of a hit, in ticks (strum off only); `note` is its voicing index
constexpr int kFeelSaltVelocity = 3;    // one note's velocity; `note` is its voicing index
constexpr int kFeelSaltArp = 4;         // the Random pattern's note choice; `note` is 0

/// splitmix64's output function for state `x`: `x += 0x9E3779B97F4A7C15; x = (x ^ x >> 30) *
/// 0xBF58476D1CE4E5B9; x = (x ^ x >> 27) * 0x94D049BB133111EB; return x ^ x >> 31;`, all modulo 2^64.
/// `feelMix(0)` is 0xE220A8397B1DCDAF.
std::uint64_t feelMix(std::uint64_t x);

/// `mix(mix(mix(mix((uint64_t)(uint32_t)id) ^ hit) ^ note) ^ salt)`. `id` is the chord's event id
/// (`ChordEvent::id`, taken as its 32-bit pattern), `hit` the hit's index within its chord (0 for the chord's
/// first hit, in the order `rhythmHits` returns them), `note` the note's index in the chord's voicing (0 =
/// lowest pitch) for salts 2 and 3 and 0 for salt 1, `salt` one of the `kFeelSalt` values. `hit` and `note`
/// must be non-negative.
std::uint64_t feelKey(int id, int hit, int note, int salt);

/// `(int)(key % (2m + 1)) - m`: an offset in [-m, m]. `m` must be in 0..2^30.
int feelOffset(std::uint64_t key, int m);

/// `(int)(key % (m + 1))`: an offset in [0, m], for a chord's first hit, which may only move later. `m` must
/// be in 0..2^30.
int feelLateOffset(std::uint64_t key, int m);

// --- Performance (D8-D12) --------------------------------------------------------------------------------

/// The most events a performed document may hold in any window of `kFeelTicksPerBeat` ticks (D11). With the
/// supported host range (up to 300 bpm, blocks up to 4096 frames at 44.1 kHz or more: at most 0.47 beat per
/// block), one audio block never carries more than this many performed events.
constexpr int kMaxFeelEventsPerBeat = 96;

/// One note-on or note-off of a performed progression, on the integer tick grid of the whole progression.
/// `velocity` is 0 for an off. `chord` is the index in `Document::events` of the chord it belongs to and
/// `hit` the index of its hit within that chord (as in `feelKey`).
struct PerformedEvent {
    int tick;
    bool on;
    int pitch;
    int velocity;
    int chord;
    int hit;
};

/// The whole progression as the feel plays it.
struct Performance {
    /// Sorted strictly by (tick, off before on, pitch): no two events share that key. Empty when
    /// `feelLimited`.
    std::vector<PerformedEvent> events;
    /// Chord `i` spans ticks [chordTicks[i], chordTicks[i + 1]); `lround(startBeat * 960)` of the cumulative
    /// beats, so adjacent chords share their boundary tick exactly. Size `doc.events.size() + 1`
    /// (just {0} for an empty one); the last entry is the end tick.
    std::vector<int> chordTicks;
    /// The feel would exceed a budget (D11): more than `kMaxSnapshotEvents` events, or more than
    /// `kMaxFeelEventsPerBeat` in some beat. The whole document then plays and exports as block chords.
    bool feelLimited = false;

    int endTick() const { return chordTicks.back(); }
};

/// The most events any half-open window of `kFeelTicksPerBeat` ticks holds when the progression of
/// `endTick` ticks loops (the window may span the wrap, and several wraps of a short progression); 0 when
/// `events` is empty. `events` must be sorted by tick, every tick in [0, endTick], `endTick` > 0 unless
/// `events` is empty. The budget counts across the wrap whether or not the transport loops, so the
/// decision never depends on the loop switch.
int densestBeat(const std::vector<PerformedEvent>& events, int endTick);

/// True when `feel` changes anything about block chords: a rhythm other than block, a strum or humanise.
/// (The strum speed alone changes nothing while the strum is off.) `compile` takes today's block path
/// whenever this is false, so its output stays exactly what it was before feels existed.
bool feelPerforms(const Feel& feel);

/// Performs the whole of `doc` with `doc.feel` and `doc.meter`:
/// - each chord `i` spans ticks [s, e) = [chordTicks[i], chordTicks[i + 1]) and is played with the voicing
///   `voicings(doc)` gives it (auto voice-leading respected) and its velocity, as `rhythmHits` says (D5-D7) and, for an arpeggio, the arpeggio design D2-D4;
/// - strum (D8): `down` plays the hit's notes lowest first, `up` highest first, `downUp` down on a strong hit
///   (a pulse or the chord's start) and up otherwise, `step` ticks apart (tight 10, medium 20, loose 40),
///   `step = min(step, (len / 2) / (n - 1))` for a hit of gated length `len` and `n` notes; arpeggios ignore
///   it (they play as with the strum off);
/// - humanise (D9), amounts (hit, note, velocity) light (5, 1, 4), medium (10, 2, 8), heavy (20, 4, 12):
///   hit `k` moves as a whole by `H = feelOffset(feelKey(id, k, 0, 1), hit)`, and the chord's first hit by
///   `feelLateOffset(feelKey(id, 0, 0, 1), hit)`; with the (effective) strum off each note's on moves by a
///   further `feelOffset(feelKey(id, k, note, 2), note)`; every note's velocity becomes
///   `clamp(hitVelocity + feelOffset(feelKey(id, k, note, 3), velocity), 1, 127)`. A note's off is the moved
///   hit start plus the gated length (offs are not humanised individually);
/// - safety (D10): ons are clamped into [s, e - kMinNoteTicks] and offs to at most e; then, per pitch over
///   the whole document in on order, an off later than the next on of that pitch is moved to that on; a
///   note left shorter than `kMinNoteTicks` is dropped. Clamping is monotone, so a strum's order survives
///   (notes may only come to share a tick);
/// - budgets (D11), decided once for the whole document: when the result has more than
///   `kMaxSnapshotEvents` events or `densestBeat` above `kMaxFeelEventsPerBeat`, `events` is cleared and
///   `feelLimited` set.
/// Same preconditions as `compile`; `doc.meter` must satisfy `validMeter`.
Performance performDocument(const Document& doc);

}  // namespace chordlab
