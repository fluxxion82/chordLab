#include "chordlab/listener.h"

#include "chordlab/chord.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace chordlab {

const std::array<double, 12> kMajorProfile{6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
const std::array<double, 12> kMinorProfile{6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};

double pearson(const std::array<double, 12>& a, const std::array<double, 12>& b) noexcept {
    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    const auto [aMin, aMax] = std::minmax_element(a.begin(), a.end());
    const auto [bMin, bMax] = std::minmax_element(b.begin(), b.end());
    if (*aMin == *aMax || *bMin == *bMax) return kNaN;
    double ma = 0.0, mb = 0.0;
    for (int i = 0; i < 12; ++i) {
        ma += a[i];
        mb += b[i];
    }
    ma /= 12.0;
    mb /= 12.0;
    double sab = 0.0, saa = 0.0, sbb = 0.0;
    for (int i = 0; i < 12; ++i) {
        const double da = a[i] - ma, db = b[i] - mb;
        sab += da * db;
        saa += da * da;
        sbb += db * db;
    }
    if (!(saa > 0.0 && sbb > 0.0)) return kNaN;
    return sab / std::sqrt(saa * sbb);
}

namespace {
constexpr int kPedalThreshold = 64;

bool usable(double v) noexcept { return std::isfinite(v) && v > 0.0; }

/// The correlation of `h` with `profile` rotated to `tonic`: `h[(tonic + i) % 12]` against `profile[i]`.
double correlate(const std::array<double, 12>& h, const std::array<double, 12>& profile, int tonic) noexcept {
    std::array<double, 12> aligned{};
    for (int i = 0; i < 12; ++i) aligned[i] = h[(tonic + i) % 12];
    return pearson(aligned, profile);
}

const std::array<double, 12>& profileOf(Mode mode) noexcept {
    return mode == Mode::Aeolian ? kMinorProfile : kMajorProfile;
}

/// An undefined correlation (NaN) is reported as 0.
double reported(double r) noexcept { return std::isnan(r) ? 0.0 : r; }
}  // namespace

Listener::Listener() { openSeq_.fill(kNoNote); }

Listener::Visible Listener::visible() const {
    Visible v;
    v.active = active_;
    for (int p = 0; p < 128; ++p) v.held[p] = keyDown_[p] || pedalHeld_[p];
    v.onsets = onsetsInWindow();
    if (key_) {
        v.key = key_->key;
        v.keyPercent = static_cast<int>(std::lround(100.0 * key_->r));
    }
    if (chord_) {
        v.hasChord = true;
        v.chord = chord_->chord;
        v.chordBass = chord_->bass;
        v.chordInversion = chord_->inversion;
    }
    v.chordHeld = chordHeld_;
    return v;
}

void Listener::moveTo(std::uint64_t sampleTime, double bpm, double sampleRate) {
    if (!hasStamp_) {
        hasStamp_ = true;
        lastStamp_ = sampleTime;
        return;
    }
    if (sampleTime <= lastStamp_) return;  // monotonic: earlier or equal stamps are simultaneous
    const std::uint64_t delta = sampleTime - lastStamp_;
    lastStamp_ = sampleTime;
    if (!usable(bpm) || !usable(sampleRate)) return;
    now_ += static_cast<double>(delta) * bpm / (60.0 * sampleRate);
}

void Listener::closeNote(int pitch) {
    const std::int64_t seq = openSeq_[pitch];
    openSeq_[pitch] = kNoNote;
    if (seq == kNoNote) return;
    // The ring may have wrapped over this note while it sounded; then there is nothing to end.
    if (noteCount_ - static_cast<std::uint64_t>(seq) > kNotes) return;
    Note& n = noteAt(static_cast<std::uint64_t>(seq));
    n.open = false;
    n.end = now_;
}

void Listener::release(int pitch) {
    keyDown_[pitch] = false;
    pedalHeld_[pitch] = false;
    closeNote(pitch);
}

void Listener::feed(const HeardEvent& e, double bpm, double sampleRate) {
    moveTo(e.sampleTime, bpm, sampleRate);
    active_ = true;
    changed_ = true;
    bool noteOn = false;
    switch (e.kind) {
        case HeardEvent::NoteOn: {
            if (e.pitch >= 128) break;
            noteOn = true;
            const int p = e.pitch;
            closeNote(p);  // a repeated note-on restarts the pitch
            keyDown_[p] = true;
            pedalHeld_[p] = false;
            Note& n = noteAt(noteCount_);
            n = Note{static_cast<std::uint8_t>(p % 12), now_, now_, true};
            openSeq_[p] = static_cast<std::int64_t>(noteCount_);
            ++noteCount_;
            break;
        }
        case HeardEvent::NoteOff: {
            if (e.pitch >= 128) break;
            const int p = e.pitch;
            if (!keyDown_[p]) break;  // nothing to release (a pedal-held pitch is already released)
            keyDown_[p] = false;
            if (pedalDown_) {
                pedalHeld_[p] = true;
            } else {
                release(p);
            }
            break;
        }
        case HeardEvent::Sustain: {
            const bool down = e.velocity >= kPedalThreshold;
            if (pedalDown_ && !down) {
                for (int p = 0; p < 128; ++p)
                    if (pedalHeld_[p]) release(p);
            }
            pedalDown_ = down;
            break;
        }
    }
    if (noteOn) latchChord();
    updateChordHeld();
}

void Listener::latchChord() {
    const std::vector<int> pitches = held();
    std::array<bool, 12> classes{};
    int distinct = 0;
    for (const int p : pitches) {
        if (!classes[p % 12]) ++distinct;
        classes[p % 12] = true;
    }
    if (distinct < 2) return;
    if (auto heard = identifyChord(pitches)) chord_ = *heard;  // no match keeps the latched chord
}

void Listener::updateChordHeld() {
    chordHeld_ = false;
    if (!chord_) return;
    std::array<bool, 12> classes{};
    for (int p = 0; p < 128; ++p)
        if (keyDown_[p] || pedalHeld_[p]) classes[p % 12] = true;
    for (const PitchClass pc : pitchClasses(chord_->chord))
        if (!classes[pc]) return;
    chordHeld_ = true;
}

void Listener::evaluateKey() {
    const std::array<double, 12> h = histogram();
    const int onsets = onsetsInWindow();

    struct Candidate {
        double r;
        int tonic;
        Mode mode;
    };
    std::optional<Candidate> best, second;
    for (int t = 0; t < 12; ++t) {
        for (const Mode mode : {Mode::Ionian, Mode::Aeolian}) {
            const double r = correlate(h, profileOf(mode), t);
            if (std::isnan(r)) continue;
            const Candidate c{r, t, mode};
            if (!best || r > best->r) {
                second = best;
                best = c;
            } else if (!second || r > second->r) {
                second = c;
            }
        }
    }
    const double gap = best && second ? best->r - second->r : 0.0;
    const bool acquires = best && best->r >= kMinR && onsets >= kMinOnsets && gap >= kMinGap;
    const auto keyOf = [](const Candidate& c) { return Key{canonicalTonic(c.tonic, c.mode), c.mode}; };

    if (!key_) {
        if (acquires) {
            key_ = HeardKey{keyOf(*best), best->r, gap};
            keyMisses_ = 0;
        }
        return;
    }
    const double rs = correlate(h, profileOf(key_->key.mode), key_->key.tonicPc());
    const bool bestIsShown = best && best->tonic == key_->key.tonicPc() && best->mode == key_->key.mode;
    if (acquires && !bestIsShown && best->r >= rs + kHysteresis) {  // false while rs is NaN
        key_ = HeardKey{keyOf(*best), best->r, gap};
        keyMisses_ = 0;
        return;
    }
    const bool retains = !std::isnan(rs) && rs >= kMinR && onsets >= kMinOnsets;
    if (retains) {
        keyMisses_ = 0;
    } else if (++keyMisses_ >= 2) {
        key_.reset();
        keyMisses_ = 0;
        return;
    }
    key_->r = reported(rs);
    key_->gap = gap;
}

void Listener::advance(std::uint64_t nowSampleTime, double bpm, double sampleRate) {
    moveTo(nowSampleTime, bpm, sampleRate);
    evaluateKey();
    if (visible() != acknowledged_) changed_ = true;
}

void Listener::clear() {
    notes_.fill(Note{});
    noteCount_ = 0;
    openSeq_.fill(kNoNote);
    keyDown_.fill(false);
    pedalHeld_.fill(false);
    pedalDown_ = false;
    active_ = false;
    hasStamp_ = false;
    lastStamp_ = 0;
    now_ = 0.0;
    // The tick on an empty histogram can only drop the key: drop it, and the latched chord.
    key_.reset();
    keyMisses_ = 0;
    chord_.reset();
    chordHeld_ = false;
    changed_ = true;
}

bool Listener::active() const noexcept { return active_; }

std::vector<int> Listener::held() const {
    std::vector<int> out;
    for (int p = 0; p < 128; ++p)
        if (keyDown_[p] || pedalHeld_[p]) out.push_back(p);
    return out;
}

double Listener::nowBeats() const noexcept { return now_; }

std::array<double, 12> Listener::histogram() const {
    std::array<double, 12> h{};
    const double lo = now_ - kWindowBeats;
    const std::uint64_t count = std::min<std::uint64_t>(noteCount_, kNotes);
    for (std::uint64_t i = 0; i < count; ++i) {
        const Note& n = notes_[i];
        const double end = n.open ? now_ : n.end;
        const double overlap = std::min(end, now_) - std::max(n.start, lo);
        double w = 0.0;
        if (overlap > 0.0) w += std::min(overlap, kNoteCapBeats);
        if (n.start >= lo) w += kOnsetWeight;
        h[n.pc] += w;
    }
    return h;
}

int Listener::onsetsInWindow() const {
    const double lo = now_ - kWindowBeats;
    const std::uint64_t count = std::min<std::uint64_t>(noteCount_, kNotes);
    int onsets = 0;
    for (std::uint64_t i = 0; i < count; ++i)
        if (notes_[i].start >= lo) ++onsets;
    return onsets;
}

bool Listener::changed() const noexcept { return changed_; }

std::optional<HeardKey> Listener::key() const noexcept { return key_; }

std::optional<HeardChord> Listener::chord() const noexcept { return chord_; }

bool Listener::chordHeld() const noexcept { return chordHeld_; }

void Listener::acknowledge() {
    acknowledged_ = visible();
    changed_ = false;
}

}  // namespace chordlab
