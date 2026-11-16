#pragma once

#include "chordlab/chord.h"
#include "chordlab/meter.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
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

/// The accent of a generated grid hit at `tick` of a chord starting at `chordStart` (D16, D22): 100 on the chord's
/// start or a bar start of the whole progression (bars counted from tick 0 in `meter`), 90 on any other pulse, 80
/// elsewhere (a subdivision). Absolute velocities, never scaled by the chord's. Shared by the bass and lead
/// generators. `meter` must satisfy `validMeter`.
int gridAccent(Meter meter, int chordStart, int tick) noexcept;

/// The bass generator's name for `gridAccent` (D16), kept as a forwarding wrapper.
int bassAccent(Meter meter, int chordStart, int tick) noexcept;

/// One generated hit, before a pitch is given to it: its absolute start tick, its length and its accent. Shared by
/// the bass and lead generators.
struct GridHit {
    int start;
    int length;
    int velocity;
};

constexpr bool operator==(const GridHit& a, const GridHit& b) noexcept {
    return a.start == b.start && a.length == b.length && a.velocity == b.velocity;
}

constexpr bool operator!=(const GridHit& a, const GridHit& b) noexcept {
    return !(a == b);
}

/// The bass generator's name for a hit (D16).
using BassHit = GridHit;

/// How a finalized hit's length follows from the gap to the next kept start (or the span's end): nine tenths of it,
/// integer division (`Gated`), or all of it (`FullGap`).
enum class HitGate { Gated, FullGap };

/// The common hit finalizer (D16, D22): turns the candidate `starts` of one chord ending at `end` into hits. `starts`
/// must be ascending and distinct, its first element the chord's start (the attack that always stays) and every
/// element below `end`; empty `starts`, or `end <= starts.front()`, gives no hits. Each hit's gap runs to the next
/// kept start, the last one's to `end`, and is first capped at `gapCap` when one is given (Motif's one-pulse cap,
/// D22); its length is that gap under `gate`. **Short hits:** while some hit would last under `kMinBassHitTicks`, the
/// earliest such hit is removed, except that when it is the first hit the hit after it is removed instead; the lengths
/// are recomputed after every removal, the capped length being the one tested. A lone first hit shorter than the
/// minimum is returned as it is (the bass behaviour; `omitShortLoneHit` is the lead's terminal rule). Velocities are
/// `gridAccent` with the chord starting at `starts.front()`. `meter` must satisfy `validMeter`; `gapCap`, when
/// given, must be at least 1.
std::vector<GridHit> finalizeHits(Meter meter, std::vector<int> starts, int end, HitGate gate,
                                  std::optional<int> gapCap = std::nullopt);

/// The candidate starts of a chord spanning [s, e) on a grid of `step` ticks: `s` plus the multiples of `step`
/// strictly inside the span (`gridPointsInside`: counted from tick 0 of the progression, so from its bar starts),
/// ascending and distinct; empty when `e <= s`. What `finalizeHits` takes for the grid patterns.
std::vector<int> gridStarts(int s, int e, int step);

/// The common hits of a chord spanning ticks [s, e) of the whole progression on a grid of `step` ticks (D16):
/// `finalizeHits(meter, gridStarts(s, e, step), e, fullGap ? FullGap : Gated)`. Each hit lasts nine tenths of the gap
/// to the next kept hit (integer division), the last gap ending at `e`; with `fullGap` the whole gap. **Short hits:**
/// while some hit would last under `kMinBassHitTicks`, the earliest such hit is removed, except that when it is the
/// first hit the hit after it is removed instead (the chord-start attack always stays); the lengths are recomputed
/// after every removal, so every hit lasts at least `kMinBassHitTicks` unless the span itself is too short for its one
/// hit (under 34 ticks, 30 with `fullGap`: that hit is returned short; a store's chords are never that short).
/// Velocities are `bassAccent`. Empty when `e <= s`. `[959, 1920)` on the 4/4 pulse is one hit (959, 864, 100);
/// `[0, 961)` is (0, 864, 100). `meter` must satisfy `validMeter` and `step` be at least 1.
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

// --- the lead generator (tracks design D22) ------------------------------------------------------------------------

/// The lead generator's patterns (tracks design D22): one chord tone held over each chord (`HeldTone`); the chord's
/// third and seventh (or their stand-ins), one per half of the chord (`GuideTones`); the chord tones up and down on the
/// subdivisions (`Arpeggio`); a line stepping through the chord scale between chord tones on the pulses
/// (`Stepwise`); a short rhythmic and melodic figure restated in every bar, one of 64 takes (`Motif`, the default);
/// the top note's pitch class wherever the document's feel plays it (`FollowFeel`). `generateLead` says exactly what
/// each writes. A separate enum from `BassPattern` on purpose: the two families are offered and validated apart.
enum class LeadPattern { HeldTone, GuideTones, Arpeggio, Stepwise, Motif, FollowFeel };

/// How many lead patterns there are: the enum's values are 0..kLeadPatternCount - 1.
constexpr int kLeadPatternCount = 6;

/// How many takes Motif has (D22): a variation is reduced modulo this, 8 rhythms times 8 contours.
constexpr int kMotifVariationCount = 64;

/// The wire and menu name of a lead pattern: "heldTone", "guideTones", "arpeggio", "stepwise", "motif" or
/// "followFeel". The one table of lead pattern names: the `generateLead` command and its messages use it.
const char* leadPatternName(LeadPattern pattern) noexcept;

/// The lead pattern named `name` exactly as `leadPatternName` spells it, or empty (case matters, no trimming).
std::optional<LeadPattern> leadPatternFromName(std::string_view name) noexcept;

/// The inclusive register every generated lead pitch lies in (D22): C4 (60) .. C6 (84).
constexpr int kLeadLowest = 60;
constexpr int kLeadHighest = 84;

/// The pitch a lead line starts from (D22): the previous pitch at progression tick 0, C5. Loop never changes it.
constexpr int kLeadSeed = 72;

/// The member of `candidates` nearest `pitch` (D22's `nearest`): the smallest `abs(candidate - pitch)`, an equal
/// distance going to the higher pitch. `candidates` need not be sorted but must not be empty.
int nearestPitch(const std::vector<int>& candidates, int pitch);

/// The two structural pitch classes Guide tones plays over `chord` (D22), absolute (C = 0), `A` first:
/// - `A`, a structural third: the smallest interval of the type whose `chordToneDegree` is 2; without one the
///   interval 5 when the type lists it (sus4, 7sus4), else the interval 2 (sus2), else the root (a power chord).
/// - `B`, a structural seventh: the smallest interval whose `chordToneDegree` is 6 (dim7's diminished seventh
///   included); without one the `structuralFifth`; without either the root; failing that (each of these equal to
///   `A`) the lowest absolute pitch class of the chord other than `A`. A candidate equal to `A` is passed over.
/// `B` is empty when the chord has no second pitch class. Reads `chord.type->intervals` only, so a test may pass a
/// type of its own; `chord.type` must be non-null.
std::pair<int, std::optional<int>> guideToneClasses(const Chord& chord);

/// The tick at which Guide tones moves from `A` to `B` in a chord spanning [s, e) (D22): among the absolute pulse
/// ticks `t` of `meter` strictly inside the span with `t - s >= 30` and `e - t >= 30`, the one minimizing
/// `abs(2t - s - e)`, a tie going to the later tick; empty when there is none. 4/4 [0, 3840): 1920; 7/8 [0, 3360):
/// 1920 (1440 and 1920 tie); 3/4 [0, 2880): 1920. `meter` must satisfy `validMeter`.
std::optional<int> guideToneSplit(Meter meter, int s, int e);

/// The lead side's terminal rule (D22): `hits` without its one hit when it is a lone hit still shorter than
/// `kMinBassHitTicks` (never lengthened past the span); any other `hits` unchanged. With it, gated spans under 34
/// ticks and full-duration spans under 30 emit nothing.
std::vector<GridHit> omitShortLoneHit(std::vector<GridHit> hits);

/// The lead's gated grid hits of a chord spanning [s, e) on a grid of `step` ticks (Arpeggio and Stepwise on the
/// subdivision, D22): `omitShortLoneHit(finalizeHits(meter, gridStarts(s, e, step), e, Gated))`. 4/4 subdivision:
/// [0, 33) none, [0, 34) one hit (0, 30, 100). `meter` must satisfy `validMeter`, `step` be at least 1.
std::vector<GridHit> leadGridHits(Meter meter, int s, int e, int step);

/// The lead's one full-duration hit over [s, e) (Held tone, and Guide tones without a split, D22):
/// `omitShortLoneHit(finalizeHits(meter, {s}, e, FullGap))`, so (s, e - s, 100), or none when the span is shorter than
/// `kMinBassHitTicks` or empty: [0, 29) none, [0, 30) (0, 30, 100). `meter` must satisfy `validMeter`.
std::vector<GridHit> leadSpanHit(Meter meter, int s, int e);

/// Motif's rhythm table (D22): the bar-relative slots, eighths of a bar, of rhythm `index` 0..7. Index 6 has three
/// slots, index 7 five, the others four.
const std::vector<int>& motifRhythm(int index);

/// Motif's contour table (D22): the four chord-tone index offsets, from the fragment's base, of contour `index`
/// 0..7, used by hit `k` of a fragment as entry `k % 4`.
const std::array<int, 4>& motifContour(int index);

/// The tick at which Motif's rhythm slot `slot` (0..7) falls in the bar starting at `barStart` (D22):
/// `barStart + floor(slot * M / 8) * subdivisionTicks(meter)`, `M = barTicks(meter) / subdivisionTicks(meter)`.
/// 4/4: slot j at 480j; 6/8: slots 0, 2, 4, 6 at 0, 480, 1440, 1920; 7/8: at 0, 720, 1680, 2400. `meter` must
/// satisfy `validMeter`.
int motifSlotTick(Meter meter, int barStart, int slot);

/// Motif's reflected chord-tone index (D22): the index `base + offset` folded back at both ends of an `n`-tone
/// list, `period = 2(n - 1)`, `r = (base + offset) mod period` (mathematical), `r < n ? r : period - r`; 0 when
/// `n` is 1. `n` must be at least 1.
int motifReflectIndex(int n, int base, int offset) noexcept;

/// The lead line `pattern` writes over the chords of `scopeEventIds` (tracks design D22), in progression order, each
/// chord's notes by start, anchored to their chords. An empty scope means every chord; ids no chord has are skipped
/// and a repeat counts once (the store refuses an unknown id before it calls this). Pure: it reads the document
/// (chords, key, meter, feel, and for Follow feel the voicings), never a track, so existing track notes never change
/// it and the store can apply it to a track of any role.
///
/// **Continuity.** One line is generated over the whole progression and then filtered by ownership: out-of-scope
/// chords advance the previous pitch and Stepwise's direction exactly as if their notes were kept, so a
/// non-contiguous scope never restarts the line. The previous pitch starts at `kLeadSeed` (72) and becomes each
/// emitted note's pitch; a silent chord leaves it. Loop changes nothing (no approach to the first chord).
///
/// **Pitches.** Every pitch lies in `kLeadLowest`..`kLeadHighest`. For each chord, from `harmonicGuide(chord,
/// doc.projectKey)`: `T`, the pitches whose role is Root or ChordTone; `S`, the pitches whose class is in the chord
/// scale (`scaleMask`) and whose role is not Outside. `nearest` is `nearestPitch`. Only Follow feel reads
/// `voicings(doc)`, so inversion, octave and auto voice-leading change no other pattern.
///
/// **Timing.** A chord spans its canonical boundaries [s, e) (`chordBoundaryTicks`); no note passes `e`. Grid hits
/// are `finalizeHits` (nine-tenths gate, the short-hit pruning, `gridAccent` velocities) with the lead's terminal
/// rule (`omitShortLoneHit`); pitches are assigned after pruning; repeated pitches stay separate notes.
/// - `HeldTone`: (s, e - s, nearest(T, previous), 100).
/// - `GuideTones`: `guideToneClasses` A and B and `guideToneSplit` t. With a split, of every ordered pair (a, b) in
///   the register with one pitch of each class, in either order (pairs more than 7 semitones apart discarded when any
///   pair is within 7), the least by (|a - previous|, |b - a|, -a, -b): (s, t - s, a, 100) and (t, e - t, b,
///   accent(t)). Without a split, one full-span note at velocity 100 nearest the previous pitch among the pitches of
///   classes A and B; without a B, of class A.
/// - `Arpeggio`: `leadGridHits` on the subdivision. The first pitch is nearest(T, previous); the direction starts up at
///   every chord; each later hit takes the nearest member of T strictly beyond the last pitch that way, reversing
///   (and keeping the new direction) at an edge, repeating the pitch when neither way has one.
/// - `Stepwise`: the same hits. The structural hits (the first, and every other one on an absolute pulse) take
///   nearest(T, previous), then Arpeggio's step with one direction for the whole progression (starting up, never
///   reset by a new chord). A hit between structural (ta, a) and (tb, b) takes the member of S strictly between a and
///   b minimizing |p(tb - ta) - (a(tb - t) + b(t - ta))|, a tie going to the pitch nearer b, else repeats a; a hit
///   after the chord's last structural hit repeats it. Then each Avoid pitch is kept only when its velocity is 80, its
///   length at most 480, it has a provisional neighbour before and after in the chord, both non-Avoid, and both moves
///   go the same way by one or two semitones; otherwise it becomes its preceding structural pitch.
/// - `Motif`: take v = variation mod 64, rhythm `motifRhythm(v % 8)`, contour `motifContour(v / 8)`. A chord's starts
///   are s plus every bar's `motifSlotTick`s strictly inside it, deduplicated, finalized with the gated length capped
///   at one pulse. Grouped by bar, the first kept hit of each chord/bar fragment sets the base nearest(T, previous);
///   hit k of the fragment plays T[`motifReflectIndex`(|T|, index of base, contour[k % 4])]. No hash, id or random
///   number is involved.
/// - `FollowFeel`: when the document's feel is over budget (`feelPerforms` and `compile(doc).feelLimited`) the whole
///   document is generated as `HeldTone`, then filtered. Otherwise each chord's `rhythmHits` (the feel, meter, span,
///   voicing size, chord velocity and id; indices through `arpeggioNotes` under an arpeggio, else the voicing) is kept
///   where a hit plays the pitch class of the voicing's top note, clipped to the span and dropped under
///   `kMinBassHitTicks`, at the raw start, end and velocity, at that class's pitch nearest the previous one (ties up);
///   one note per hit, strum and humanise ignored; no qualifying hit means silence.
/// `variation` is read by Motif only and must be non-negative (the store refuses anything else; a negative value is
/// reduced mathematically here). The result is baked: a later harmony, key, meter, feel, Loop or role change does not
/// regenerate it. The document must satisfy the store's invariants (as for `compile`).
std::vector<GeneratedNote> generateLead(const Document& doc, LeadPattern pattern,
                                        const std::vector<int>& scopeEventIds, int variation = 0);

}  // namespace chordlab
