#include "chordlab/generate.h"

#include "chordlab/compiler.h"
#include "chordlab/document.h"
#include "chordlab/feel.h"
#include "chordlab/symbol.h"
#include "chordlab/tracks.h"

#include <algorithm>
#include <cassert>
#include <cstddef>

namespace chordlab {

static_assert(kMinBassHitTicks == kMinTrackNoteTicks, "every generated note fits the track domain's shortest note");
static_assert(kBassHighest - kBassLowest == 11, "the bass register holds each pitch class exactly once");
static_assert(static_cast<int>(BassPattern::Walking) == kBassPatternCount - 1, "the name table covers the enum");

namespace {

constexpr const char* kPatternNames[] = {"roots", "rootPulse", "rootFifth", "octaves", "followFeel", "walking"};
static_assert(sizeof(kPatternNames) / sizeof(kPatternNames[0]) == static_cast<std::size_t>(kBassPatternCount),
              "one name per pattern");

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

int bassAccent(Meter meter, int chordStart, int tick) noexcept {
    if (tick == chordStart || tick % barTicks(meter) == 0) {
        return kAccentStrong;
    }
    return tick % pulseTicks(meter) == 0 ? kAccentPulse : kAccentOther;
}

std::vector<BassHit> bassGridHits(Meter meter, int s, int e, int step, bool fullGap) {
    std::vector<BassHit> hits;
    if (e <= s) {
        return hits;
    }
    // The span start plus the grid points strictly inside: ascending and distinct, since every point is > s.
    std::vector<int> starts{s};
    for (int t : gridPointsInside(s, e, step)) {
        starts.push_back(t);
    }
    const auto lengthAt = [&](std::size_t i) {
        const int gap = (i + 1 < starts.size() ? starts[i + 1] : e) - starts[i];
        return fullGap ? gap : gap * 9 / 10;
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
            break;  // a lone chord-start hit under the minimum: a span the store's chord lengths never make
        }
        starts.erase(starts.begin() + static_cast<std::ptrdiff_t>(removed));
    }
    hits.reserve(starts.size());
    for (std::size_t i = 0; i < starts.size(); ++i) {
        hits.push_back(BassHit{starts[i], lengthAt(i), bassAccent(meter, s, starts[i])});
    }
    return hits;
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

}  // namespace chordlab
