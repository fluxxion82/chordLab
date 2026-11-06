#include "chordlab/listener.h"

#include <algorithm>
#include <cmath>

namespace chordlab {

namespace {
constexpr int kPedalThreshold = 64;

bool usable(double v) noexcept { return std::isfinite(v) && v > 0.0; }
}  // namespace

Listener::Listener() { openSeq_.fill(kNoNote); }

Listener::Visible Listener::visible() const {
    Visible v;
    v.active = active_;
    for (int p = 0; p < 128; ++p) v.held[p] = keyDown_[p] || pedalHeld_[p];
    v.onsets = onsetsInWindow();
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
    switch (e.kind) {
        case HeardEvent::NoteOn: {
            if (e.pitch >= 128) break;
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
    // Task 4 hook: chord latching on a NoteOn goes here, after the held set is updated.
}

void Listener::advance(std::uint64_t nowSampleTime, double bpm, double sampleRate) {
    moveTo(nowSampleTime, bpm, sampleRate);
    // Task 4 hook: the key estimate is evaluated here, before the visible outputs are compared.
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
    // Task 4 hook: drop the key and the latched chord here.
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

void Listener::acknowledge() {
    acknowledged_ = visible();
    changed_ = false;
}

}  // namespace chordlab
