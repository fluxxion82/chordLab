#include "chordlab/generate.h"

#include "chordlab/compiler.h"
#include "chordlab/document.h"
#include "chordlab/feel.h"
#include "chordlab/guide.h"
#include "chordlab/symbol.h"
#include "chordlab/tracks.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace chordlab {

static_assert(kMinBassHitTicks == kMinTrackNoteTicks, "every generated note fits the track domain's shortest note");
static_assert(kBassHighest - kBassLowest == 11, "the bass register holds each pitch class exactly once");
static_assert(static_cast<int>(BassPattern::Walking) == kBassPatternCount - 1, "the name table covers the enum");
static_assert(static_cast<int>(LeadPattern::FollowFeel) == kLeadPatternCount - 1, "the lead name table covers the enum");
static_assert(kLeadLowest <= kLeadSeed && kLeadSeed <= kLeadHighest, "the seed lies in the lead register");
static_assert(kLeadHighest - kLeadLowest >= 11, "the lead register holds every pitch class");

namespace {

constexpr const char* kPatternNames[] = {"roots", "rootPulse", "rootFifth", "octaves", "followFeel", "walking"};
static_assert(sizeof(kPatternNames) / sizeof(kPatternNames[0]) == static_cast<std::size_t>(kBassPatternCount),
              "one name per pattern");

constexpr const char* kLeadPatternNames[] = {"heldTone", "guideTones", "arpeggio", "stepwise", "motif", "followFeel"};
static_assert(sizeof(kLeadPatternNames) / sizeof(kLeadPatternNames[0]) == static_cast<std::size_t>(kLeadPatternCount),
              "one name per lead pattern");

/// Motif's contour table (D22), indexed by `variation / 8`.
constexpr std::array<std::array<int, 4>, 8> kMotifContours{{
    {{0, 1, 2, 1}},
    {{0, -1, -2, -1}},
    {{0, 1, 0, -1}},
    {{0, 2, 1, 0}},
    {{0, -1, 1, 0}},
    {{0, 0, 1, 0}},
    {{0, 1, -1, 0}},
    {{0, 2, 0, 1}},
}};

/// The longest gated length an Avoid passing note of Stepwise may have (D22).
constexpr int kLongestPassingNote = 480;

/// The accent velocities of D16 (absolute, not scaled by the chord's velocity).
constexpr int kAccentStrong = 100;
constexpr int kAccentPulse = 90;
constexpr int kAccentOther = 80;

/// The pitch a walking line approaches a next bass `next` with: a semitone below, or above when below would leave
/// the register (`next` is the lowest bass, 28: 29).
constexpr int kApproachFromAbove = kBassLowest + 1;

int mod12(int value) noexcept {
    return ((value % 12) + 12) % 12;
}

/// The notes of one chord, collected by the patterns.
struct ChordNotes {
    int eventId;
    int start;  // the chord's boundary: offsets are taken from it
    std::vector<GeneratedNote>& out;

    void add(int tick, int length, int pitch, int velocity) const {
        assert(tick >= start && length >= kMinBassHitTicks);
        out.push_back(GeneratedNote{eventId, static_cast<std::int32_t>(tick - start), static_cast<std::int32_t>(length),
                                    pitch, velocity});
    }
};

/// The next walking pitch after `from` in direction `direction` (+1 up, -1 down): the nearest pitch strictly beyond
/// `from` that way in kBassLowest..kWalkingHighest whose pitch class is a chord tone. With none that way the line
/// reverses (`direction` flips and stays flipped) and takes the nearest the other way; with none at all (never with a
/// chord of two or more pitch classes over this range) it repeats `from`.
int walkStep(int from, int& direction, const std::vector<bool>& tone) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        for (int p = from + direction; p >= kBassLowest && p <= kWalkingHighest; p += direction) {
            if (tone[static_cast<std::size_t>(mod12(p))]) {
                return p;
            }
        }
        direction = -direction;
    }
    return from;  // both ways empty: two flips left the direction as it was
}

}  // namespace

const char* bassPatternName(BassPattern pattern) noexcept {
    const int i = static_cast<int>(pattern);
    return i >= 0 && i < kBassPatternCount ? kPatternNames[i] : "roots";
}

std::optional<BassPattern> bassPatternFromName(std::string_view name) noexcept {
    for (int i = 0; i < kBassPatternCount; ++i) {
        if (name == kPatternNames[i]) {
            return static_cast<BassPattern>(i);
        }
    }
    return std::nullopt;
}

int placeBass(int pitch) noexcept {
    return kBassLowest + mod12(pitch - kBassLowest);
}

std::optional<int> structuralFifth(const ChordType& type) {
    std::optional<int> altered;
    for (int interval : type.intervals) {
        const int pc = mod12(interval);
        if (pc == 7) {
            return 7;  // the perfect fifth wins whatever else the type holds
        }
        // The chord-tone structure's fifth: the tritone of a type without a perfect fifth is its diminished fifth,
        // eight semitones its augmented fifth; with a perfect fifth neither is (7#11's tritone is its #11).
        if (!altered && chordToneDegree(type, interval) == 4) {
            altered = pc;
        }
    }
    return altered;
}

int gridAccent(Meter meter, int chordStart, int tick) noexcept {
    if (tick == chordStart || tick % barTicks(meter) == 0) {
        return kAccentStrong;
    }
    return tick % pulseTicks(meter) == 0 ? kAccentPulse : kAccentOther;
}

int bassAccent(Meter meter, int chordStart, int tick) noexcept {
    return gridAccent(meter, chordStart, tick);
}

std::vector<GridHit> finalizeHits(Meter meter, std::vector<int> starts, int end, HitGate gate,
                                  std::optional<int> gapCap) {
    std::vector<GridHit> hits;
    if (starts.empty() || end <= starts.front()) {
        return hits;
    }
    assert(std::is_sorted(starts.begin(), starts.end()) &&
           std::adjacent_find(starts.begin(), starts.end()) == starts.end() && starts.back() < end);
    assert(!gapCap || *gapCap >= 1);
    const int chordStart = starts.front();
    const auto lengthAt = [&](std::size_t i) {
        int gap = (i + 1 < starts.size() ? starts[i + 1] : end) - starts[i];
        if (gapCap) {
            gap = std::min(gap, *gapCap);  // Motif's cap: the capped length is also the one the short-hit rule tests
        }
        return gate == HitGate::FullGap ? gap : gap * 9 / 10;
    };
    // Short hits: the earliest too-short hit goes, or the hit after it when it is the chord's first; then again with
    // the gaps the removal changed, until none is left (each pass removes one start, so this ends).
    for (;;) {
        std::size_t shortest = starts.size();
        for (std::size_t i = 0; i < starts.size(); ++i) {
            if (lengthAt(i) < kMinBassHitTicks) {
                shortest = i;
                break;
            }
        }
        if (shortest == starts.size()) {
            break;
        }
        const std::size_t removed = shortest == 0 ? 1 : shortest;
        if (removed >= starts.size()) {
            break;  // a lone chord-start hit under the minimum: returned short (the lead omits it, omitShortLoneHit)
        }
        starts.erase(starts.begin() + static_cast<std::ptrdiff_t>(removed));
    }
    hits.reserve(starts.size());
    for (std::size_t i = 0; i < starts.size(); ++i) {
        hits.push_back(GridHit{starts[i], lengthAt(i), gridAccent(meter, chordStart, starts[i])});
    }
    return hits;
}

std::vector<int> gridStarts(int s, int e, int step) {
    std::vector<int> starts;
    if (e <= s) {
        return starts;
    }
    // The span start plus the grid points strictly inside: ascending and distinct, since every point is > s.
    starts.push_back(s);
    for (int t : gridPointsInside(s, e, step)) {
        starts.push_back(t);
    }
    return starts;
}

std::vector<BassHit> bassGridHits(Meter meter, int s, int e, int step, bool fullGap) {
    if (e <= s) {
        return {};
    }
    return finalizeHits(meter, gridStarts(s, e, step), e, fullGap ? HitGate::FullGap : HitGate::Gated);
}

std::vector<GeneratedNote> generateBass(const Document& doc, BassPattern pattern,
                                        const std::vector<int>& scopeEventIds) {
    std::vector<GeneratedNote> out;
    const std::size_t count = doc.events.size();
    if (count == 0) {
        return out;
    }
    const ChordTimeline timeline = chordTimeline(doc);
    std::vector<bool> inScope(count, scopeEventIds.empty());
    for (int id : scopeEventIds) {
        const int index = timeline.indexOf(id);
        if (index >= 0) {
            inScope[static_cast<std::size_t>(index)] = true;
        }
    }
    const std::vector<std::vector<int>> voiced = voicings(doc);
    const auto bassOf = [&](std::size_t i) { return placeBass(voiced[i].front()); };
    // Follow feel reads the whole document's budget verdict: over it, the chords play block and the bass plays Roots.
    if (pattern == BassPattern::FollowFeel && feelPerforms(doc.feel) && compile(doc).feelLimited) {
        pattern = BassPattern::Roots;
    }
    const int pulse = pulseTicks(doc.meter);
    const int subdivision = subdivisionTicks(doc.meter);

    for (std::size_t i = 0; i < count; ++i) {
        if (!inScope[i]) {
            continue;
        }
        const ChordEvent& event = doc.events[i];
        const int s = timeline.boundaries[i];
        const int e = timeline.boundaries[i + 1];
        const int bass = bassOf(i);
        const ChordNotes notes{event.id, s, out};
        switch (pattern) {
            case BassPattern::Roots:
                notes.add(s, e - s, bass, kAccentStrong);
                break;
            case BassPattern::RootPulse:
                for (const BassHit& h : bassGridHits(doc.meter, s, e, pulse, false)) {
                    notes.add(h.start, h.length, bass, h.velocity);
                }
                break;
            case BassPattern::RootFifth: {
                const std::optional<int> fifth = structuralFifth(*event.chord.type);
                // The fifth above the sounding bass; 0 (the fifth is the bass) and no fifth repeat it.
                const int upper = fifth ? bass + mod12(event.chord.root + *fifth - bass) : bass;
                const std::vector<BassHit> hits = bassGridHits(doc.meter, s, e, pulse, false);
                for (std::size_t k = 0; k < hits.size(); ++k) {
                    notes.add(hits[k].start, hits[k].length, k % 2 == 0 ? bass : upper, hits[k].velocity);
                }
                break;
            }
            case BassPattern::Octaves: {
                const std::vector<BassHit> hits = bassGridHits(doc.meter, s, e, subdivision, false);
                for (std::size_t k = 0; k < hits.size(); ++k) {
                    notes.add(hits[k].start, hits[k].length, k % 2 == 0 ? bass : bass + 12, hits[k].velocity);
                }
                break;
            }
            case BassPattern::FollowFeel: {
                const std::vector<int>& voicing = voiced[i];
                const bool arpeggio = doc.feel.rhythm == Rhythm::Arpeggio;
                const std::vector<int> list =
                    arpeggio ? arpeggioNotes(event.chord, voicing, doc.feel.pattern, doc.feel.range) : voicing;
                const int bassClass = mod12(bass);
                for (const Hit& h : rhythmHits(doc.feel, doc.meter, s, e, static_cast<int>(voicing.size()),
                                               event.velocity, event.id)) {
                    const bool plays = std::any_of(h.notes.begin(), h.notes.end(), [&](int index) {
                        return index >= 0 && static_cast<std::size_t>(index) < list.size() &&
                               mod12(list[static_cast<std::size_t>(index)]) == bassClass;
                    });
                    const int start = std::max(h.start, s);
                    const int end = std::min(h.end, e);
                    // Raw hits: a short one is dropped, never stretched over the rest after it.
                    if (plays && end - start >= kMinBassHitTicks) {
                        notes.add(start, end - start, bass, h.velocity);
                    }
                }
                break;
            }
            case BassPattern::Walking: {
                const std::vector<BassHit> hits = bassGridHits(doc.meter, s, e, pulse, true);
                // The next chord in the document, in the scope or not; past the last, the first only under Loop
                // (read now: the line is baked).
                std::optional<int> next;
                if (i + 1 < count) {
                    next = bassOf(i + 1);
                } else if (doc.progressionLoop) {
                    next = bassOf(0);
                }
                std::vector<bool> tone(12, false);
                for (PitchClass pc : pitchClasses(event.chord)) {
                    tone[static_cast<std::size_t>(mod12(pc))] = true;
                }
                int pitch = bass;
                int direction = 1;
                for (std::size_t k = 0; k < hits.size(); ++k) {
                    if (k > 0) {
                        const bool approach = next && hits.size() >= 2 && k + 1 == hits.size();
                        if (approach) {
                            pitch = *next == kBassLowest ? kApproachFromAbove : *next - 1;
                        } else {
                            pitch = walkStep(pitch, direction, tone);
                        }
                    }
                    notes.add(hits[k].start, hits[k].length, pitch, hits[k].velocity);
                }
                break;
            }
        }
    }
    return out;
}

// --- the lead generator (D22) --------------------------------------------------------------------------------------

namespace {

/// One chord's pitch sets for the lead (D22), from its harmonic guide in the project key: `tones` (T), the register's
/// pitches whose role is Root or ChordTone; `scale` (S), those whose class is in the chord scale and whose role is not
/// Outside; both ascending. `role` is the guide's, by absolute pitch class.
struct LeadSets {
    std::vector<int> tones;
    std::vector<int> scale;
    std::array<GuideRole, 12> role;

    GuideRole roleOf(int pitch) const {
        return role[static_cast<std::size_t>(mod12(pitch))];
    }
};

LeadSets leadSets(const Chord& chord, const Key& key) {
    const Guide guide = harmonicGuide(chord, key);
    LeadSets sets{{}, {}, guide.role};
    for (int p = kLeadLowest; p <= kLeadHighest; ++p) {
        const int pc = mod12(p);
        const GuideRole role = guide.role[static_cast<std::size_t>(pc)];
        if (role == GuideRole::Root || role == GuideRole::ChordTone) {
            sets.tones.push_back(p);
        }
        if (((guide.scaleMask >> pc) & 1U) != 0 && role != GuideRole::Outside) {
            sets.scale.push_back(p);
        }
    }
    return sets;
}

/// The register's pitches of pitch class `pc`, ascending.
std::vector<int> leadPitchesOf(int pc) {
    std::vector<int> out;
    for (int p = kLeadLowest; p <= kLeadHighest; ++p) {
        if (mod12(p) == mod12(pc)) {
            out.push_back(p);
        }
    }
    return out;
}

/// Arpeggio's (and Stepwise's structural) step (D22): the nearest member of `tones` (ascending) strictly beyond `from`
/// in `direction` (+1 up, -1 down); with none that way the direction reverses, and stays reversed, and the nearest the
/// other way is taken; with none either way (two flips leave the direction as it was) `from` repeats.
int stepThrough(const std::vector<int>& tones, int from, int& direction) {
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (direction > 0) {
            const auto above = std::upper_bound(tones.begin(), tones.end(), from);
            if (above != tones.end()) {
                return *above;
            }
        } else {
            const auto below = std::lower_bound(tones.begin(), tones.end(), from);
            if (below != tones.begin()) {
                return *(below - 1);
            }
        }
        direction = -direction;
    }
    return from;
}

/// A lead note of one chord before ownership filtering, start absolute.
struct LeadNote {
    int start;
    int length;
    int pitch;
    int velocity;
};

/// Guide tones over one chord (D22), from `previous`.
void guideTonesOf(const Chord& chord, Meter meter, int s, int e, int previous, std::vector<LeadNote>& notes) {
    const std::pair<int, std::optional<int>> classes = guideToneClasses(chord);
    const std::vector<int> as = leadPitchesOf(classes.first);
    if (!classes.second) {
        for (const GridHit& h : leadSpanHit(meter, s, e)) {
            notes.push_back(LeadNote{h.start, h.length, nearestPitch(as, previous), h.velocity});
        }
        return;
    }
    const std::vector<int> bs = leadPitchesOf(*classes.second);
    const std::optional<int> split = guideToneSplit(meter, s, e);
    if (!split) {
        std::vector<int> both = as;
        both.insert(both.end(), bs.begin(), bs.end());
        for (const GridHit& h : leadSpanHit(meter, s, e)) {
            notes.push_back(LeadNote{h.start, h.length, nearestPitch(both, previous), h.velocity});
        }
        return;
    }
    // Every ordered pair with one pitch of each class, either first; within seven semitones when any pair is.
    struct Pair {
        int a;
        int b;
    };
    std::vector<Pair> pairs;
    for (int x : as) {
        for (int y : bs) {
            pairs.push_back(Pair{x, y});
            pairs.push_back(Pair{y, x});
        }
    }
    const bool anyClose = std::any_of(pairs.begin(), pairs.end(), [](const Pair& p) { return std::abs(p.b - p.a) <= 7; });
    if (anyClose) {
        pairs.erase(std::remove_if(pairs.begin(), pairs.end(), [](const Pair& p) { return std::abs(p.b - p.a) > 7; }),
                    pairs.end());
    }
    // Lexicographically least (|a - previous|, |b - a|, -a, -b).
    const auto key = [previous](const Pair& p) {
        return std::array<int, 4>{std::abs(p.a - previous), std::abs(p.b - p.a), -p.a, -p.b};
    };
    const Pair best = *std::min_element(pairs.begin(), pairs.end(),
                                        [&](const Pair& x, const Pair& y) { return key(x) < key(y); });
    const int t = *split;
    notes.push_back(LeadNote{s, t - s, best.a, kAccentStrong});
    notes.push_back(LeadNote{t, e - t, best.b, gridAccent(meter, s, t)});
}

/// Stepwise over one chord (D22): `previous` is the incoming pitch, `direction` the progression's direction state,
/// updated by the structural steps.
void stepwiseOf(const LeadSets& sets, Meter meter, int s, int e, int previous, int& direction,
                std::vector<LeadNote>& notes) {
    const std::vector<GridHit> hits = leadGridHits(meter, s, e, subdivisionTicks(meter));
    const std::size_t n = hits.size();
    if (n == 0) {
        return;
    }
    const int pulse = pulseTicks(meter);
    // The chord-tone skeleton: the first hit and every later one on an absolute pulse.
    std::vector<std::size_t> structural;
    for (std::size_t k = 0; k < n; ++k) {
        if (k == 0 || hits[k].start % pulse == 0) {
            structural.push_back(k);
        }
    }
    std::vector<int> provisional(n, 0);
    for (std::size_t j = 0; j < structural.size(); ++j) {
        const std::size_t k = structural[j];
        provisional[k] = j == 0 ? nearestPitch(sets.tones, previous)
                                : stepThrough(sets.tones, provisional[structural[j - 1]], direction);
    }
    // The preceding structural hit of every hit (itself for a structural one).
    std::vector<std::size_t> before(n, 0);
    for (std::size_t j = 0; j < structural.size(); ++j) {
        const std::size_t last = j + 1 < structural.size() ? structural[j + 1] : n;
        for (std::size_t k = structural[j]; k < last; ++k) {
            before[k] = structural[j];
        }
    }
    // The hits between two structural notes: the member of S strictly between them nearest the linear interpolation
    // in time (exact integer comparison), a tie going to the pitch nearer b; none repeats a. Hits after the last
    // structural one repeat it.
    for (std::size_t j = 0; j < structural.size(); ++j) {
        const std::size_t ia = structural[j];
        const bool hasNext = j + 1 < structural.size();
        const std::size_t ib = hasNext ? structural[j + 1] : n;
        for (std::size_t k = ia + 1; k < ib; ++k) {
            const int a = provisional[ia];
            if (!hasNext) {
                provisional[k] = a;
                continue;
            }
            const int b = provisional[ib];
            const std::int64_t ta = hits[ia].start;
            const std::int64_t tb = hits[ib].start;
            const std::int64_t t = hits[k].start;
            const int lo = std::min(a, b);
            const int hi = std::max(a, b);
            std::optional<int> chosen;
            std::int64_t chosenScore = 0;
            for (int p : sets.scale) {
                if (p <= lo || p >= hi) {
                    continue;
                }
                const std::int64_t score = std::llabs(p * (tb - ta) - (a * (tb - t) + b * (t - ta)));
                if (!chosen || score < chosenScore ||
                    (score == chosenScore && std::abs(p - b) < std::abs(*chosen - b))) {
                    chosen = p;
                    chosenScore = score;
                }
            }
            provisional[k] = chosen ? *chosen : a;
        }
    }
    // Avoid notes survive only as checked passing notes, judged against the completed provisional sequence; anything
    // else becomes its preceding structural pitch. Non-Avoid pitches never change.
    std::vector<int> pitches = provisional;
    for (std::size_t k = 0; k < n; ++k) {
        if (sets.roleOf(provisional[k]) != GuideRole::Avoid) {
            continue;
        }
        bool passing = hits[k].velocity == kAccentOther && hits[k].length <= kLongestPassingNote && k > 0 && k + 1 < n;
        if (passing) {
            const int prev = provisional[k - 1];
            const int next = provisional[k + 1];
            const int in = provisional[k] - prev;
            const int out = next - provisional[k];
            passing = sets.roleOf(prev) != GuideRole::Avoid && sets.roleOf(next) != GuideRole::Avoid &&
                      ((in > 0 && out > 0) || (in < 0 && out < 0)) && std::abs(in) <= 2 && std::abs(out) <= 2;
        }
        if (!passing) {
            pitches[k] = provisional[before[k]];
        }
    }
    for (std::size_t k = 0; k < n; ++k) {
        notes.push_back(LeadNote{hits[k].start, hits[k].length, pitches[k], hits[k].velocity});
    }
}

}  // namespace

const char* leadPatternName(LeadPattern pattern) noexcept {
    const int i = static_cast<int>(pattern);
    return i >= 0 && i < kLeadPatternCount ? kLeadPatternNames[i] : "motif";
}

std::optional<LeadPattern> leadPatternFromName(std::string_view name) noexcept {
    for (int i = 0; i < kLeadPatternCount; ++i) {
        if (name == kLeadPatternNames[i]) {
            return static_cast<LeadPattern>(i);
        }
    }
    return std::nullopt;
}

int nearestPitch(const std::vector<int>& candidates, int pitch) {
    assert(!candidates.empty());
    int best = candidates.front();
    for (int c : candidates) {
        const int d = std::abs(c - pitch);
        const int bestD = std::abs(best - pitch);
        if (d < bestD || (d == bestD && c > best)) {
            best = c;
        }
    }
    return best;
}

std::pair<int, std::optional<int>> guideToneClasses(const Chord& chord) {
    assert(chord.type != nullptr);
    const ChordType& type = *chord.type;
    const auto smallestOfDegree = [&](int degree) {
        std::optional<int> best;
        for (int interval : type.intervals) {
            if (chordToneDegree(type, interval) == degree && (!best || interval < *best)) {
                best = interval;
            }
        }
        return best;
    };
    const auto lists = [&](int interval) {
        return std::find(type.intervals.begin(), type.intervals.end(), interval) != type.intervals.end();
    };
    // A: the structural third; else the literal 4th (5), else the literal 2nd (2), else the root.
    const std::optional<int> third = smallestOfDegree(2);
    const int aInterval = third ? *third : (lists(5) ? 5 : (lists(2) ? 2 : 0));
    const int a = mod12(chord.root + aInterval);
    // B: the structural seventh, else the structural fifth, else the root; a candidate equal to A is passed over.
    for (const std::optional<int>& interval : {smallestOfDegree(6), structuralFifth(type), std::optional<int>{0}}) {
        if (interval && mod12(chord.root + *interval) != a) {
            return {a, mod12(chord.root + *interval)};
        }
    }
    // Failing those: the chord's lowest absolute pitch class other than A.
    std::optional<int> lowest;
    for (int interval : type.intervals) {
        const int pc = mod12(chord.root + interval);
        if (pc != a && (!lowest || pc < *lowest)) {
            lowest = pc;
        }
    }
    return {a, lowest};
}

std::optional<int> guideToneSplit(Meter meter, int s, int e) {
    std::optional<int> best;
    std::int64_t bestScore = 0;
    for (int t : gridPointsInside(s, e, pulseTicks(meter))) {
        if (t - s < kMinBassHitTicks || e - t < kMinBassHitTicks) {
            continue;
        }
        const std::int64_t score =
            std::llabs(2 * static_cast<std::int64_t>(t) - static_cast<std::int64_t>(s) - static_cast<std::int64_t>(e));
        if (!best || score <= bestScore) {  // ascending ticks: an equal score is a later tick, which wins the tie
            best = t;
            bestScore = score;
        }
    }
    return best;
}

std::vector<GridHit> omitShortLoneHit(std::vector<GridHit> hits) {
    if (hits.size() == 1 && hits.front().length < kMinBassHitTicks) {
        hits.clear();
    }
    return hits;
}

std::vector<GridHit> leadGridHits(Meter meter, int s, int e, int step) {
    return omitShortLoneHit(finalizeHits(meter, gridStarts(s, e, step), e, HitGate::Gated));
}

std::vector<GridHit> leadSpanHit(Meter meter, int s, int e) {
    if (e <= s) {
        return {};
    }
    return omitShortLoneHit(finalizeHits(meter, {s}, e, HitGate::FullGap));
}

const std::vector<int>& motifRhythm(int index) {
    static const std::vector<int> kRhythms[8] = {{0, 2, 4, 6}, {0, 3, 4, 6}, {0, 2, 5, 6}, {0, 2, 4, 7},
                                                 {0, 1, 4, 6}, {0, 3, 5, 7}, {0, 4, 6},    {0, 2, 3, 6, 7}};
    assert(index >= 0 && index < 8);
    return kRhythms[index];
}

const std::array<int, 4>& motifContour(int index) {
    assert(index >= 0 && index < 8);
    return kMotifContours[static_cast<std::size_t>(index)];
}

int motifSlotTick(Meter meter, int barStart, int slot) {
    const int subdivision = subdivisionTicks(meter);
    const int perBar = barTicks(meter) / subdivision;  // M
    return barStart + (slot * perBar / 8) * subdivision;
}

int motifReflectIndex(int n, int base, int offset) noexcept {
    if (n <= 1) {
        return 0;
    }
    const int period = 2 * (n - 1);
    const int r = ((base + offset) % period + period) % period;
    return r < n ? r : period - r;
}

std::vector<GeneratedNote> generateLead(const Document& doc, LeadPattern pattern,
                                        const std::vector<int>& scopeEventIds, int variation) {
    std::vector<GeneratedNote> out;
    const std::size_t count = doc.events.size();
    if (count == 0) {
        return out;
    }
    const ChordTimeline timeline = chordTimeline(doc);
    std::vector<bool> inScope(count, scopeEventIds.empty());
    for (int id : scopeEventIds) {
        const int index = timeline.indexOf(id);
        if (index >= 0) {
            inScope[static_cast<std::size_t>(index)] = true;
        }
    }
    // Follow feel over a feel the whole document cannot afford: Held tone everywhere, then the scope.
    if (pattern == LeadPattern::FollowFeel && feelPerforms(doc.feel) && compile(doc).feelLimited) {
        pattern = LeadPattern::HeldTone;
    }
    std::vector<std::vector<int>> voiced;
    if (pattern == LeadPattern::FollowFeel) {
        voiced = voicings(doc);  // the only pattern that reads them
    }
    const Meter meter = doc.meter;
    const int pulse = pulseTicks(meter);
    const int subdivision = subdivisionTicks(meter);
    const int bar = barTicks(meter);
    const int take = ((variation % kMotifVariationCount) + kMotifVariationCount) % kMotifVariationCount;
    const std::vector<int>& rhythm = motifRhythm(take % 8);
    const std::array<int, 4>& contour = motifContour(take / 8);

    // The line's state over the whole progression, scope or not: the previous pitch and Stepwise's direction.
    int previous = kLeadSeed;
    int stepDirection = 1;
    std::vector<LeadNote> notes;
    for (std::size_t i = 0; i < count; ++i) {
        const ChordEvent& event = doc.events[i];
        const int s = timeline.boundaries[i];
        const int e = timeline.boundaries[i + 1];
        const LeadSets sets = leadSets(event.chord, doc.projectKey);
        notes.clear();
        switch (pattern) {
            case LeadPattern::HeldTone:
                for (const GridHit& h : leadSpanHit(meter, s, e)) {
                    notes.push_back(LeadNote{h.start, h.length, nearestPitch(sets.tones, previous), kAccentStrong});
                }
                break;
            case LeadPattern::GuideTones:
                guideTonesOf(event.chord, meter, s, e, previous, notes);
                break;
            case LeadPattern::Arpeggio: {
                int direction = 1;  // reset upward at every chord
                int pitch = previous;
                bool first = true;
                for (const GridHit& h : leadGridHits(meter, s, e, subdivision)) {
                    pitch = first ? nearestPitch(sets.tones, previous) : stepThrough(sets.tones, pitch, direction);
                    first = false;
                    notes.push_back(LeadNote{h.start, h.length, pitch, h.velocity});
                }
                break;
            }
            case LeadPattern::Stepwise:
                stepwiseOf(sets, meter, s, e, previous, stepDirection, notes);
                break;
            case LeadPattern::Motif: {
                // The chord's start plus every bar's rhythm slots strictly inside it, absolute bar phase kept.
                std::vector<int> starts{s};
                for (int b = (s / bar) * bar; b < e; b += bar) {
                    for (int slot : rhythm) {
                        const int t = motifSlotTick(meter, b, slot);
                        if (t > s && t < e) {
                            starts.push_back(t);
                        }
                    }
                }
                std::sort(starts.begin(), starts.end());
                starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
                const std::vector<GridHit> hits =
                    omitShortLoneHit(finalizeHits(meter, std::move(starts), e, HitGate::Gated, pulse));
                // One fragment per chord/bar intersection: its first kept hit sets the base, the contour restarts.
                int fragmentBar = -1;
                int baseIndex = 0;
                std::size_t k = 0;
                int last = previous;
                for (const GridHit& h : hits) {
                    if (h.start / bar != fragmentBar) {
                        fragmentBar = h.start / bar;
                        const int base = nearestPitch(sets.tones, last);
                        baseIndex = static_cast<int>(std::lower_bound(sets.tones.begin(), sets.tones.end(), base) -
                                                     sets.tones.begin());
                        k = 0;
                    }
                    const int index =
                        motifReflectIndex(static_cast<int>(sets.tones.size()), baseIndex, contour[k % contour.size()]);
                    last = sets.tones[static_cast<std::size_t>(index)];
                    notes.push_back(LeadNote{h.start, h.length, last, h.velocity});
                    ++k;
                }
                break;
            }
            case LeadPattern::FollowFeel: {
                const std::vector<int>& voicing = voiced[i];
                const bool arpeggio = doc.feel.rhythm == Rhythm::Arpeggio;
                const std::vector<int> list =
                    arpeggio ? arpeggioNotes(event.chord, voicing, doc.feel.pattern, doc.feel.range) : voicing;
                const int topClass = mod12(voicing.back());
                const std::vector<int> tops = leadPitchesOf(topClass);
                int last = previous;
                for (const Hit& h : rhythmHits(doc.feel, meter, s, e, static_cast<int>(voicing.size()), event.velocity,
                                               event.id)) {
                    const bool plays = std::any_of(h.notes.begin(), h.notes.end(), [&](int index) {
                        return index >= 0 && static_cast<std::size_t>(index) < list.size() &&
                               mod12(list[static_cast<std::size_t>(index)]) == topClass;
                    });
                    const int start = std::max(h.start, s);
                    const int end = std::min(h.end, e);
                    // Clipped first, then the minimum: a short hit is dropped, never stretched over a rest.
                    if (plays && end - start >= kMinBassHitTicks) {
                        last = nearestPitch(tops, last);
                        notes.push_back(LeadNote{start, end - start, last, h.velocity});
                    }
                }
                break;
            }
        }
        if (!notes.empty()) {
            previous = notes.back().pitch;
        }
        if (inScope[i]) {
            for (const LeadNote& n : notes) {
                assert(n.start >= s && n.length >= kMinBassHitTicks && n.start + n.length <= e);
                out.push_back(GeneratedNote{event.id, static_cast<std::int32_t>(n.start - s),
                                            static_cast<std::int32_t>(n.length), n.pitch, n.velocity});
            }
        }
    }
    return out;
}

}  // namespace chordlab
