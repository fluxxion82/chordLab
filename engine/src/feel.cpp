#include "chordlab/feel.h"

#include "chordlab/compiler.h"
#include "chordlab/document.h"
#include "chordlab/limits.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <tuple>

namespace chordlab {

namespace {

int clampVelocity(int v) { return std::min(127, std::max(1, v)); }

std::vector<int> allNotes(int n) {
    std::vector<int> notes(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) notes[static_cast<size_t>(i)] = i;
    return notes;
}

}  // namespace

int arpIndex(ArpPattern pattern, int k, int n) {
    assert(k >= 0 && n >= 1);
    assert(pattern != ArpPattern::Chord && pattern != ArpPattern::Random);
    if (n == 1) return 0;
    // Up then down over `count` notes without repeating the ends: period 2 * count - 2, m in [0, period).
    const auto upDown = [](int m, int count) { return m < count ? m : 2 * count - 2 - m; };
    // The j-th note of the converge order over n notes: 0, n-1, 1, n-2, ...
    const auto converge = [n](int j) { return j % 2 == 0 ? j / 2 : n - 1 - (j - 1) / 2; };
    const int bounce = 2 * n - 2;  // UpDown's period
    switch (pattern) {
        case ArpPattern::Up:
        case ArpPattern::RootFirst: return k % n;
        case ArpPattern::Down: return n - 1 - k % n;
        case ArpPattern::UpDown: return upDown(k % bounce, n);
        case ArpPattern::DownUp: return n - 1 - upDown(k % bounce, n);
        case ArpPattern::UpAndDown: {
            const int m = k % (2 * n);
            return m < n ? m : 2 * n - 1 - m;
        }
        case ArpPattern::DownAndUp: {
            const int m = k % (2 * n);
            return n - 1 - (m < n ? m : 2 * n - 1 - m);
        }
        case ArpPattern::Converge: return converge(k % n);
        case ArpPattern::Diverge: {
            // From the middle outwards, below first: n/2, n/2 - 1, n/2 + 1, n/2 - 2, ... (Live's Diverge; for an
            // even count this is Converge backwards, for an odd one it is not).
            const int m = k % n;
            const int step = (m + 1) / 2;
            return m % 2 == 1 ? n / 2 - step : n / 2 + step;
        }
        case ArpPattern::ConAndDiverge: return converge(upDown(k % bounce, n));
        case ArpPattern::PinkyUp: return k % 2 == 1 ? n - 1 : (k / 2) % (n - 1);
        case ArpPattern::PinkyUpDown:
            if (k % 2 == 1) return n - 1;
            if (n == 2) return 0;
            return upDown((k / 2) % (2 * (n - 1) - 2), n - 1);  // the lower n-1 notes, up and down
        case ArpPattern::ThumbUp: return k % 2 == 0 ? 0 : 1 + (k / 2) % (n - 1);
        case ArpPattern::ThumbUpDown:
            if (k % 2 == 0) return 0;
            if (n == 2) return 1;
            return 1 + upDown((k / 2) % (2 * (n - 1) - 2), n - 1);  // the upper n-1 notes, up and down
        case ArpPattern::Alberti:
            switch (k % 4) {
                case 0: return 0;
                case 2: return (n - 1) / 2;
                default: return n - 1;
            }
        case ArpPattern::Chord:
        case ArpPattern::Random: break;
    }
    return 0;
}

int arpCycle(ArpPattern pattern, int n) {
    assert(n >= 1);
    assert(pattern != ArpPattern::Random);
    if (n == 1) return 1;
    switch (pattern) {
        case ArpPattern::Up:
        case ArpPattern::Down:
        case ArpPattern::RootFirst:
        case ArpPattern::Converge:
        case ArpPattern::Diverge: return n;
        case ArpPattern::UpDown:
        case ArpPattern::DownUp:
        case ArpPattern::ConAndDiverge: return 2 * n - 2;
        case ArpPattern::UpAndDown:
        case ArpPattern::DownAndUp: return 2 * n;
        case ArpPattern::PinkyUp:
        case ArpPattern::ThumbUp: return 2 * (n - 1);
        case ArpPattern::PinkyUpDown:
        case ArpPattern::ThumbUpDown: return n == 2 ? 2 : 4 * (n - 2);
        case ArpPattern::Alberti: return 4;
        case ArpPattern::Chord: return 1;
        case ArpPattern::Random: break;
    }
    return n;
}

int arpStepTicks(ArpRate rate, Meter meter) {
    switch (rate) {
        case ArpRate::Pulse: return pulseTicks(meter);
        case ArpRate::Subdivision: return subdivisionTicks(meter);
        case ArpRate::Half: return subdivisionTicks(meter) / 2;
    }
    return subdivisionTicks(meter);
}

std::vector<int> arpeggioNotes(const Chord& chord, const std::vector<int>& voicing, ArpPattern pattern,
                               ArpRange range) {
    std::vector<int> octave(voicing);
    const bool rootFirst = pattern == ArpPattern::RootFirst && chord.type != nullptr;
    if (rootFirst) {
        const std::vector<int>& intervals = chord.type->intervals;
        const auto tone = [&](int pitch) {
            const int pc = ((pitch - chord.root) % 12 + 12) % 12;
            for (std::size_t i = 0; i < intervals.size(); ++i) {
                if (intervals[i] % 12 == pc) return static_cast<int>(i);
            }
            return static_cast<int>(intervals.size());
        };
        std::stable_sort(octave.begin(), octave.end(), [&](int a, int b) { return tone(a) < tone(b); });
    }
    // Octave by octave: the voicing (in chord-tone order for root first), then the same an octave up, so index
    // i + n is note i an octave higher whatever the chord's span. Pitches above 127 stay in the list to keep
    // the indices aligned; performChord drops them.
    std::vector<int> notes;
    const int octaves = range == ArpRange::Two ? 2 : 1;
    for (int o = 0; o < octaves; ++o) {
        for (int pitch : octave) notes.push_back(pitch + 12 * o);
    }
    return notes;
}

std::vector<Hit> rhythmHits(const Feel& feel, Meter meter, int s, int e, int noteCount, int velocity, int id) {
    const Rhythm rhythm = feel.rhythm;
    std::vector<Hit> hits;
    if (e <= s || noteCount < 1) return hits;

    const int pulse = pulseTicks(meter);
    const int sub = subdivisionTicks(meter);
    const int bar = barTicks(meter);

    // Hit starts: the chord's start plus the grid points strictly inside (s, e), ascending, no duplicates.
    std::vector<int> starts{s};
    switch (rhythm) {
        case Rhythm::Block:
            break;
        case Rhythm::Pulse:
        case Rhythm::LowThenUpper:
            for (int t : gridPointsInside(s, e, pulse)) starts.push_back(t);
            break;
        case Rhythm::Subdivided:
            for (int t : gridPointsInside(s, e, sub)) starts.push_back(t);
            break;
        case Rhythm::Arpeggio:
            for (int t : gridPointsInside(s, e, arpStepTicks(feel.rate, meter))) starts.push_back(t);
            break;
        case Rhythm::Offbeats:
            for (int t : gridPointsInside(s, e, sub))
                if (t % pulse != 0) starts.push_back(t);
            break;
    }

    if (rhythm == Rhythm::Block) {
        hits.push_back(Hit{s, e, allNotes(noteCount), velocity, true});
        return hits;
    }

    const int octaves = feel.range == ArpRange::Two ? 2 : 1;
    int previousRandom = -1;
    for (size_t i = 0; i < starts.size(); ++i) {
        const int t = starts[i];
        const int next = i + 1 < starts.size() ? starts[i + 1] : e;
        const int gap = next - t;
        const int length = std::max(kMinNoteTicks, gap * 9 / 10);

        const bool chordStart = t == s;
        const bool barStart = t % bar == 0;
        const bool onPulse = t % pulse == 0;

        int v;
        if (chordStart || barStart)
            v = velocity;
        else if (onPulse)
            v = (velocity * 9 + 5) / 10;
        else
            v = (velocity * 8 + 5) / 10;

        std::vector<int> notes;
        if (rhythm == Rhythm::LowThenUpper) {
            if (noteCount == 1)
                notes = {0};
            else if (chordStart || barStart)
                notes = {0};
            else
                for (int k = 1; k < noteCount; ++k) notes.push_back(k);
        } else if (rhythm == Rhythm::Arpeggio) {
            // `noteCount` notes per octave; the octave above starts at index noteCount (arpeggioNotes). Random
            // draws from both octaves at once; every other pattern plays one cycle per octave, Live-style.
            const int k = static_cast<int>(i);
            if (feel.pattern == ArpPattern::Random) {
                previousRandom = arpRandomIndex(id, k, noteCount * octaves, previousRandom);
                notes = {previousRandom};
            } else {
                const int octave = (k / arpCycle(feel.pattern, noteCount)) % octaves;
                if (feel.pattern == ArpPattern::Chord) {
                    for (int n = 0; n < noteCount; ++n) notes.push_back(octave * noteCount + n);
                } else {
                    notes = {octave * noteCount + arpIndex(feel.pattern, k, noteCount)};
                }
            }
        } else {
            notes = allNotes(noteCount);
        }

        hits.push_back(Hit{t, t + length, std::move(notes), clampVelocity(v), chordStart || onPulse});
    }
    return hits;
}

}  // namespace chordlab

namespace chordlab {

std::uint64_t feelMix(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

std::uint64_t feelKey(int id, int hit, int note, int salt) {
    assert(hit >= 0 && note >= 0);
    const std::uint64_t idBits = static_cast<std::uint32_t>(id);
    return feelMix(feelMix(feelMix(feelMix(idBits) ^ static_cast<std::uint64_t>(hit)) ^
                           static_cast<std::uint64_t>(note)) ^
                   static_cast<std::uint64_t>(salt));
}

int feelOffset(std::uint64_t key, int m) {
    assert(m >= 0 && m <= (1 << 30));
    const std::uint64_t span = 2 * static_cast<std::uint64_t>(m) + 1;
    // The remainder reaches 2m (up to 2^31), which an int cannot hold: subtract in 64 bits, then narrow the
    // result, which lies in [-m, m].
    return static_cast<int>(static_cast<std::int64_t>(key % span) - m);
}

int feelLateOffset(std::uint64_t key, int m) {
    assert(m >= 0 && m <= (1 << 30));
    return static_cast<int>(key % (static_cast<std::uint64_t>(m) + 1));
}

int densestBeat(const std::vector<PerformedEvent>& events, int endTick) {
    if (events.empty()) return 0;
    assert(endTick > 0);
    // Repeat the progression until the copies reach one beat past its end, so that a window starting at any
    // event of the first copy sees everything a looping transport would put in it.
    const int copies = 2 + kFeelTicksPerBeat / endTick;
    std::vector<long> ticks;
    ticks.reserve(events.size() * static_cast<std::size_t>(copies));
    for (int k = 0; k < copies; ++k) {
        for (const PerformedEvent& e : events) {
            assert(e.tick >= 0 && e.tick <= endTick);
            ticks.push_back(static_cast<long>(k) * endTick + e.tick);
        }
    }
    // Each copy is sorted and lies in [k * endTick, (k + 1) * endTick], so the whole list is sorted. A densest
    // half-open window can always be slid right until it starts on an event.
    int densest = 0;
    std::size_t j = 0;
    for (std::size_t i = 0; i < events.size(); ++i) {
        const long limit = ticks[i] + kFeelTicksPerBeat;
        j = std::max(j, i);
        while (j < ticks.size() && ticks[j] < limit) ++j;
        densest = std::max(densest, static_cast<int>(j - i));
    }
    return densest;
}

bool feelPerforms(const Feel& feel) {
    return feel.rhythm != Rhythm::Block || feel.strum != Strum::Off || feel.humanise != Humanise::Off;
}

namespace {

/// One performed note before it becomes an on and an off.
struct Note {
    int on;
    int off;
    int pitch;
    int velocity;
    int chord;
    int hit;
};

int strumStep(StrumSpeed speed) {
    switch (speed) {
        case StrumSpeed::Tight: return 10;
        case StrumSpeed::Medium: return 20;
        case StrumSpeed::Loose: return 40;
    }
    return 20;
}

/// Humanise amounts (D9): whole-hit timing, per-note timing (strum off only), velocity.
struct Amounts {
    int hit;
    int note;
    int velocity;
};

Amounts amountsOf(Humanise humanise) {
    switch (humanise) {
        case Humanise::Off: return {0, 0, 0};
        case Humanise::Light: return {5, 1, 4};
        case Humanise::Medium: return {10, 2, 8};
        case Humanise::Heavy: return {20, 4, 12};
    }
    return {0, 0, 0};
}

/// The notes of chord `index` over [s, e), strummed and humanised, then clamped into the chord (D8, D9, D10's
/// per-chord part). Appends to `notes`.
void performChord(const Document& doc, std::size_t index, const std::vector<int>& voicing, int s, int e,
                  std::vector<Note>& notes) {
    const ChordEvent& event = doc.events[index];
    const Feel& feel = doc.feel;
    const bool arp = feel.rhythm == Rhythm::Arpeggio;
    const Strum strum = arp ? Strum::Off : feel.strum;  // arpeggios ignore the strum
    const Amounts amounts = amountsOf(feel.humanise);
    const bool humanise = feel.humanise != Humanise::Off;
    const int chord = static_cast<int>(index);

    const std::vector<int> notesList = arp ? arpeggioNotes(event.chord, voicing, feel.pattern, feel.range) : voicing;
    const std::vector<Hit> hits =
        rhythmHits(feel, doc.meter, s, e, static_cast<int>(voicing.size()), event.velocity, event.id);
    for (std::size_t k = 0; k < hits.size(); ++k) {
        const Hit& hit = hits[k];
        const int hitIndex = static_cast<int>(k);
        int moved = 0;
        if (humanise) {
            const std::uint64_t key = feelKey(event.id, hitIndex, 0, kFeelSaltHitTiming);
            moved = k == 0 ? feelLateOffset(key, amounts.hit) : feelOffset(key, amounts.hit);
        }
        const int start = hit.start + moved;
        const int length = hit.end - hit.start;
        const int off = start + length;  // offs follow the gate from the moved hit start
        const int n = static_cast<int>(hit.notes.size());
        const Strum direction = strum == Strum::DownUp ? (hit.strong ? Strum::Down : Strum::Up) : strum;
        int step = 0;
        if (direction != Strum::Off && n > 1) {
            step = std::min(strumStep(feel.speed), (length / 2) / (n - 1));
        }
        for (int p = 0; p < n; ++p) {
            const int noteIndex = direction == Strum::Up ? hit.notes[static_cast<std::size_t>(n - 1 - p)]
                                                         : hit.notes[static_cast<std::size_t>(p)];
            int on = start;
            if (direction != Strum::Off) {
                on += p * step;
            } else if (humanise) {
                on += feelOffset(feelKey(event.id, hitIndex, noteIndex, kFeelSaltNoteTiming), amounts.note);
            }
            int velocity = hit.velocity;
            if (humanise) {
                velocity += feelOffset(feelKey(event.id, hitIndex, noteIndex, kFeelSaltVelocity), amounts.velocity);
            }
            velocity = std::min(127, std::max(1, velocity));
            // D10, inside the chord: ons in [s, e - kMinNoteTicks], offs at most e. Monotone, so a strum's
            // order survives (notes may only come to share a tick).
            const int clampedOn = std::min(std::max(on, s), e - kMinNoteTicks);
            const int clampedOff = std::min(off, e);
            if (clampedOff - clampedOn < kMinNoteTicks) continue;
            const int pitch = notesList[static_cast<std::size_t>(noteIndex)];
            if (pitch > 127) continue;  // an arpeggio's upper octave of a very high voicing (not with the dictionary)
            notes.push_back(Note{clampedOn, clampedOff, pitch, velocity, chord, hitIndex});
        }
    }
}

}  // namespace

Performance performDocument(const Document& doc) {
    assert(validMeter(doc.meter));
    Performance performance;
    const std::size_t count = doc.events.size();
    performance.chordTicks.reserve(count + 1);
    // Chord boundaries: the cumulative beats, accumulated as `Document::startBeatOf` does, rounded once.
    double beat = 0.0;
    performance.chordTicks.push_back(0);
    for (const ChordEvent& event : doc.events) {
        beat += event.lengthBeats;
        performance.chordTicks.push_back(static_cast<int>(std::lround(beat * kFeelTicksPerBeat)));
    }
    if (count == 0) return performance;

    const std::vector<std::vector<int>> chosen = voicings(doc);
    std::vector<Note> notes;
    for (std::size_t i = 0; i < count; ++i) {
        const int s = performance.chordTicks[i];
        const int e = performance.chordTicks[i + 1];
        assert(e - s > kMinNoteTicks);  // kMinLengthBeats is 240 ticks, 239 after rounding at worst
        performChord(doc, i, chosen[i], s, e, notes);
    }

    // D10 across the document, per pitch in on order (ties keep generation order): an off that runs past the
    // next on of its pitch is moved to it, and a note left shorter than kMinNoteTicks is dropped. Every note
    // lies in its own chord, so this only ever meets notes of one chord, but it is applied globally.
    std::stable_sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) {
        return std::tie(a.pitch, a.on) < std::tie(b.pitch, b.on);
    });
    std::vector<bool> keep(notes.size(), true);
    for (std::size_t i = 0; i < notes.size(); ++i) {
        if (i + 1 < notes.size() && notes[i + 1].pitch == notes[i].pitch && notes[i].off > notes[i + 1].on) {
            notes[i].off = notes[i + 1].on;
        }
        keep[i] = notes[i].off - notes[i].on >= kMinNoteTicks;
    }

    std::vector<PerformedEvent>& events = performance.events;
    events.reserve(2 * notes.size());
    for (std::size_t i = 0; i < notes.size(); ++i) {
        if (!keep[i]) continue;
        const Note& n = notes[i];
        events.push_back(PerformedEvent{n.on, true, n.pitch, n.velocity, n.chord, n.hit});
        events.push_back(PerformedEvent{n.off, false, n.pitch, 0, n.chord, n.hit});
    }
    // Exact order on integers: tick, offs before ons, pitch. Per pitch the kept notes are non-overlapping and
    // at least kMinNoteTicks long, so no two events share this key.
    const auto key = [](const PerformedEvent& e) { return std::make_tuple(e.tick, e.on ? 1 : 0, e.pitch); };
    std::sort(events.begin(), events.end(),
              [&key](const PerformedEvent& a, const PerformedEvent& b) { return key(a) < key(b); });
    assert(std::adjacent_find(events.begin(), events.end(), [&key](const PerformedEvent& a, const PerformedEvent& b) {
               return !(key(a) < key(b));
           }) == events.end());

    // D11: decided once, for the whole document.
    if (events.size() > kMaxSnapshotEvents ||
        densestBeat(events, performance.endTick()) > kMaxFeelEventsPerBeat) {
        events.clear();
        performance.feelLimited = true;
    }
    return performance;
}

int arpRandomIndex(int id, int k, int n, int previous) {
    assert(k >= 0 && n >= 1);
    if (n == 1) return 0;
    const std::uint64_t key = feelKey(id, k, 0, kFeelSaltArp);
    if (previous < 0) return static_cast<int>(key % static_cast<std::uint64_t>(n));
    const int r = static_cast<int>(key % static_cast<std::uint64_t>(n - 1));
    return r < previous ? r : r + 1;
}

}  // namespace chordlab
