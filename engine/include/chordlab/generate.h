#pragma once

#include "chordlab/chord.h"
#include "chordlab/meter.h"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace chordlab {

struct Document;

/// The bass generator's patterns (tracks design D16, Q3): one note per chord lasting it (`Roots`); the sounding bass
/// on every pulse (`RootPulse`); the bass and the chord's structural fifth alternating on the pulses (`RootFifth`);
/// the bass and its octave alternating on the subdivisions (`Octaves`); the bass wherever the document's feel plays
/// a note of the bass's pitch class (`FollowFeel`); a walking line through the chord tones with a chromatic approach
/// to the next chord (`Walking`). `generateBass` says exactly what each writes.
enum class BassPattern { Roots, RootPulse, RootFifth, Octaves, FollowFeel, Walking };

/// How many patterns there are: the enum's values are 0..kBassPatternCount - 1.
constexpr int kBassPatternCount = 6;

/// The wire and menu name of a pattern: "roots", "rootPulse", "rootFifth", "octaves", "followFeel" or "walking". The
/// one table of pattern names: the `generateBass` command and its refusal messages use it.
const char* bassPatternName(BassPattern pattern) noexcept;

/// The pattern named `name` exactly as `bassPatternName` spells it, or empty (case matters, no trimming).
std::optional<BassPattern> bassPatternFromName(std::string_view name) noexcept;

/// The register the sounding bass is placed in (D16): the octave 28 (E1) .. 39 (D#2), so every pitch class has
/// exactly one place in it.
constexpr int kBassLowest = 28;
constexpr int kBassHighest = 39;

/// The top of the range a walking line moves in, `kBassLowest`..`kWalkingHighest` (E1..E3).
constexpr int kWalkingHighest = 52;

/// The shortest hit the generator writes, in ticks: a grid hit gated shorter, or a feel hit shorter, is removed
/// (D16). Equal to `kMinTrackNoteTicks` (tracks.h), so every generated note is long enough for the track domain.
constexpr int kMinBassHitTicks = 30;

/// `pitch` moved by octaves to the lowest pitch of its pitch class in `kBassLowest`..`kBassHighest` (D16's `B`):
/// 60 (C4) is 36, 64 (E4) is 28, 67 (G4) is 31, 63 (D#4) is 39. Any `pitch` is accepted, negative ones included.
int placeBass(int pitch) noexcept;

/// The structural fifth of a chord of `type`, as semitones 0..11 above its root (D16): the perfect fifth (7) when the
/// type has one, else the interval the chord-tone structure letters as its fifth (`chordToneDegree`, symbol.h,
/// degree 4: the diminished fifth of dim, dim7 and m7b5, the augmented fifth of aug and augMaj7), else empty (a power
/// chord has one; sus chords keep their perfect fifth). Found by structure, not by interval order, so `7#11` takes its
/// perfect fifth and never its sharp eleventh (18, a tritone above the root). Reads `type.intervals` only, so a test
/// may pass a type of its own.
std::optional<int> structuralFifth(const ChordType& type);

/// The accent of a generated grid hit at `tick` of a chord starting at `chordStart` (D16): 100 on the chord's start or
/// a bar start of the whole progression (bars counted from tick 0 in `meter`), 90 on any other pulse, 80 elsewhere
/// (a subdivision). `meter` must satisfy `validMeter`.
int bassAccent(Meter meter, int chordStart, int tick) noexcept;

/// One hit of the common hit builder: its absolute start tick, its length and its accent.
struct BassHit {
    int start;
    int length;
    int velocity;
};

constexpr bool operator==(const BassHit& a, const BassHit& b) noexcept {
    return a.start == b.start && a.length == b.length && a.velocity == b.velocity;
}

constexpr bool operator!=(const BassHit& a, const BassHit& b) noexcept {
    return !(a == b);
}

/// The common hits of a chord spanning ticks [s, e) of the whole progression on a grid of `step` ticks (D16): the
/// span start plus the multiples of `step` strictly inside it (`gridPointsInside`: pulses and subdivisions counted
/// from tick 0 of the progression, so from its bar starts). Each hit lasts nine tenths of the gap to the next kept
/// hit (integer division), the last gap ending at `e`; with `fullGap` the whole gap (each note ends where the next
/// starts, or at `e`). **Short hits:** while some hit would last under `kMinBassHitTicks`, the earliest such hit is
/// removed, except that when it is the first hit the hit after it is removed instead (the chord-start attack always
/// stays); the lengths are recomputed after every removal. Velocities are `bassAccent`. Empty when `e <= s`.
/// `[959, 1920)` on the 4/4 pulse is one hit (959, 864, 100); `[0, 961)` is (0, 864, 100). `meter` must satisfy
/// `validMeter` and `step` be at least 1.
std::vector<BassHit> bassGridHits(Meter meter, int s, int e, int step, bool fullGap);

/// One note the generator writes, already anchored (D3): it belongs to the chord `anchorEventId` and starts
/// `offsetTick` ticks after its canonical boundary (`chordBoundaryTicks`), inside it, lasting `lengthTicks` (at least
/// `kMinBassHitTicks`, never past the chord's end). No id: the store hands those out.
struct GeneratedNote {
    int anchorEventId;
    std::int32_t offsetTick;
    std::int32_t lengthTicks;
    int pitch;
    int velocity;
};

constexpr bool operator==(const GeneratedNote& a, const GeneratedNote& b) noexcept {
    return a.anchorEventId == b.anchorEventId && a.offsetTick == b.offsetTick && a.lengthTicks == b.lengthTicks &&
           a.pitch == b.pitch && a.velocity == b.velocity;
}

constexpr bool operator!=(const GeneratedNote& a, const GeneratedNote& b) noexcept {
    return !(a == b);
}

/// The bass notes `pattern` writes over the chords of `scopeEventIds` (tracks design D16), in progression order,
/// each chord's notes by start. An empty scope means every chord; ids no chord has are skipped and a repeat counts
/// once (the store refuses an unknown id before it calls this). Each chord is handled on its own, so a non-contiguous
/// scope works. Pure: it reads the document (chords, voicings, meter, feel, Loop), never a track, so the store can
/// apply the result to a track of any role.
///
/// Common to every pattern: a chord spans its canonical boundaries [s, e) (`chordBoundaryTicks`, D3); `B`, the
/// sounding bass, is the lowest note of the voicing `voicings(doc)` gives the chord (auto voice-leading and inversions
/// respected) placed by `placeBass`; every note is anchored to its chord.
/// - `Roots`: one note (s, e - s), `B`, velocity 100.
/// - `RootPulse`: `bassGridHits` on the pulse (`pulseTicks`), every hit `B`.
/// - `RootFifth`: the same hits; even hits (0, 2, ...) `B`, odd hits the chord's `structuralFifth` placed at
///   `B + mod12(root + fifth - bassPitchClass)`, so under an inversion it is the bass and the fifth above it (0, the
///   fifth itself in the bass, repeats `B`, and so does a type without a fifth). The alternation restarts at each
///   chord.
/// - `Octaves`: `bassGridHits` on the subdivision (`subdivisionTicks`); even hits `B`, odd hits `B + 12`, restarting
///   at each chord.
/// - `FollowFeel`: the hits `rhythmHits` gives the chord with the document's feel and meter, its span, its voicing's
///   size, its velocity and its id; a hit's indices read the arpeggio list (`arpeggioNotes`, so Root First's order
///   and the upper octave count) under an arpeggio, else the voicing. A hit is kept when any of its pitches has `B`'s
///   pitch class; it becomes one note at `B` with the hit's raw start, end and velocity (no accents), clipped to the
///   span; strum and humanise are ignored. Hits under `kMinBassHitTicks` are dropped and rests are never bridged (the
///   exception to the short-hit rule); no qualifying hit means no note. When the whole document's feel is over budget
///   (`compile(doc).feelLimited`) every chord gets `Roots` instead (the chords then play as block chords too).
/// - `Walking`: `bassGridHits` on the pulse with `fullGap`; pulse accents. The first hit is `B`; the line starts
///   going up and each following hit takes the nearest pitch strictly above the previous one (below, going down) in
///   `kBassLowest`..`kWalkingHighest` whose pitch class is a chord tone (`pitchClasses`); with none that way it
///   reverses and keeps the new direction. With at least two hits and a next chord, the last hit is instead a
///   chromatic approach to the next chord's placed bass `N`: `N - 1`, or 29 when `N` is `kBassLowest` (28). The next
///   chord is the document's next chord, in the scope or not; after the last chord, the first chord when the
///   document's Loop (`progressionLoop`) is on, else none (the walk continues to the end). A one-hit chord plays `B`;
///   repeated pitches stay separate notes.
/// The result is baked: a later chord, feel or Loop change does not regenerate it. The document must satisfy the
/// store's invariants (as for `compile`).
std::vector<GeneratedNote> generateBass(const Document& doc, BassPattern pattern,
                                        const std::vector<int>& scopeEventIds);

}  // namespace chordlab
