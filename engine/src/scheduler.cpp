#include "chordlab/scheduler.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace chordlab {

namespace {

constexpr std::uint8_t kNoteOn = 0x90;
constexpr std::uint8_t kNoteOff = 0x80;
constexpr int kPitchCount = 128;
/// The most progression wraps one block may span (`Scheduler::kMaxWrapsPerBlock`), as a double for the
/// comparison that decides it.
constexpr double kMaxWraps = static_cast<double>(Scheduler::kMaxWrapsPerBlock);

std::uint8_t clampVelocity(int velocity) noexcept {
    return static_cast<std::uint8_t>(std::clamp(velocity, 1, 127));
}

bool validPitch(int pitch) noexcept {
    return pitch >= 0 && pitch < kPitchCount;
}

}  // namespace

/// Takes the physical events ownership produces and writes them out. In a dry run nothing is written and
/// every attack is accepted, which counts the most note-offs the block can need; the real run then keeps
/// that many slots free for them and spends the rest on attacks. A note-off is refused only when the
/// output is already full, which cannot happen when the dry run's count fit.
struct Scheduler::Writer {
    MidiOut* out;
    std::size_t capacity;
    std::size_t written = 0;
    std::size_t attackBudget;
    bool dryRun;
    std::size_t releases = 0;
    bool dropped = false;

    Writer(MidiOut* out_, std::size_t capacity_, std::size_t attackBudget_, bool dryRun_) noexcept
        : out(out_), capacity(capacity_), attackBudget(attackBudget_), dryRun(dryRun_) {}

    void put(int offset, bool on, std::uint8_t pitch, std::uint8_t velocity) noexcept {
        out[written] = MidiOut{offset, on ? kNoteOn : kNoteOff, pitch, on ? velocity : std::uint8_t{0}};
        ++written;
    }

    bool off(int offset, std::uint8_t pitch) noexcept {
        ++releases;
        if (dryRun) {
            return true;
        }
        if (written < capacity) {
            put(offset, false, pitch, 0);
            return true;
        }
        dropped = true;
        return false;
    }

    bool on(int offset, std::uint8_t pitch, std::uint8_t velocity) noexcept {
        if (dryRun) {
            return true;
        }
        if (attackBudget > 0 && written < capacity) {
            put(offset, true, pitch, velocity);
            --attackBudget;
            return true;
        }
        dropped = true;
        return false;
    }

    /// Off then on for a pitch that already sounds; all or nothing.
    bool retrigger(int offset, std::uint8_t pitch, std::uint8_t velocity) noexcept {
        if (dryRun) {
            ++releases;
            return true;
        }
        if (attackBudget > 0 && written + 2 <= capacity) {
            put(offset, false, pitch, 0);
            put(offset, true, pitch, velocity);
            ++releases;
            --attackBudget;
            return true;
        }
        dropped = true;
        return false;
    }
};

Scheduler::Scheduler() noexcept
    : mState{},
      mSnapshot{nullptr, 0, 0.0, false, 0},
      mSampleRate(0.0),
      mWasPlaying(false),
      mPendingFlush(false),
      mHasSnapshot(false),
      mHasFrontier(false),
      mFrontier(0.0),
      mScratch{},
      mScratchCount(0),
      mDropped(false),
      mQueue{},
      mQueueHead(0),
      mQueueTail(0) {
    mState.owners.fill(0);
    for (Preview& preview : mState.previews) {
        preview = Preview{};
    }
}

void Scheduler::reset(double sampleRate) noexcept {
    assert(sampleRate > 0.0 && std::isfinite(sampleRate));
    if (sampleRate > 0.0 && std::isfinite(sampleRate)) {
        mSampleRate = sampleRate;
    }
    mPendingFlush = true;
}

void Scheduler::adopt(const Snapshot& snapshot) noexcept {
    assert(snapshot.events != nullptr || snapshot.count == 0);
    assert(std::isfinite(snapshot.endBeat));
#ifndef NDEBUG
    for (std::size_t i = 1; i < snapshot.count; ++i) {
        assert(snapshot.events[i - 1].beat <= snapshot.events[i].beat);
    }
#endif
    if (!mHasSnapshot || snapshot.generation != mSnapshot.generation) {
        mPendingFlush = true;
    }
    mSnapshot = snapshot;
    mHasSnapshot = true;
}

bool Scheduler::pushPreview(const PreviewCmd& cmd) noexcept {
    assert(cmd.count <= 8);
    const std::uint32_t tail = mQueueTail.load(std::memory_order_relaxed);
    const std::uint32_t head = mQueueHead.load(std::memory_order_acquire);
    if (tail - head >= kPreviewQueueCapacity) {
        return false;
    }
    mQueue[tail % kPreviewQueueCapacity] = cmd;
    mQueueTail.store(tail + 1, std::memory_order_release);
    return true;
}

std::uint8_t Scheduler::ownersOf(int pitch) const noexcept {
    assert(validPitch(pitch));
    return validPitch(pitch) ? mState.owners[static_cast<std::size_t>(pitch)] : std::uint8_t{0};
}

bool Scheduler::snapshotUsable() const noexcept {
    return mSnapshot.events != nullptr && mSnapshot.count > 0 && mSnapshot.endBeat > 0.0 &&
           std::isfinite(mSnapshot.endBeat);
}

bool Scheduler::pushLogical(const Logical& event) noexcept {
    if (mScratchCount >= kMaxEventsPerBlock) {
        mDropped = true;
        return false;
    }
    mScratch[mScratchCount] = event;
    mScratch[mScratchCount].seq = static_cast<std::uint16_t>(mScratchCount);
    ++mScratchCount;
    return true;
}

// --- Block steps -------------------------------------------------------------------------------

void Scheduler::drainPreviews(double sampleRate) noexcept {
    std::uint32_t head = mQueueHead.load(std::memory_order_relaxed);
    const std::uint32_t tail = mQueueTail.load(std::memory_order_acquire);
    while (head != tail) {
        const PreviewCmd cmd = mQueue[head % kPreviewQueueCapacity];
        ++head;
        mQueueHead.store(head, std::memory_order_release);

        // A Stop, or a Start that restarts an id, releases every running preview with that id.
        for (std::size_t s = 0; s < kMaxPreviews; ++s) {
            if (mState.previews[s].active && mState.previews[s].id == cmd.id) {
                pushLogical(Logical{0, PreviewRelease, PhasePreview, 0, 0, static_cast<std::uint8_t>(s), 0, 0});
            }
        }
        if (cmd.kind != PreviewCmd::Start) {
            continue;
        }

        std::size_t slot = kMaxPreviews;
        for (std::size_t s = 0; s < kMaxPreviews; ++s) {
            if (!mState.previews[s].active) {
                slot = s;
                break;
            }
        }
        if (slot == kMaxPreviews) {
            mDropped = true;  // more concurrent previews than the table holds
            continue;
        }
        Preview& preview = mState.previews[slot];
        preview.active = true;
        preview.id = cmd.id;
        preview.remainingSamples = std::lround(kPreviewSeconds * sampleRate);
        preview.count = static_cast<std::uint8_t>(std::min<unsigned>(cmd.count, 8u));
        preview.held = 0;
        preview.velocity = clampVelocity(cmd.velocity);
        for (std::size_t i = 0; i < 8; ++i) {
            preview.pitches[i] = cmd.pitches[i];
        }
        for (std::uint8_t i = 0; i < preview.count; ++i) {
            if (!validPitch(preview.pitches[i])) {
                continue;
            }
            pushLogical(Logical{0, PreviewAttack, PhasePreview, preview.pitches[i], preview.velocity,
                                static_cast<std::uint8_t>(slot), i, 0});
        }
    }
}

void Scheduler::expirePreviews(int nFrames) noexcept {
    for (std::size_t s = 0; s < kMaxPreviews; ++s) {
        Preview& preview = mState.previews[s];
        if (!preview.active) {
            continue;
        }
        if (preview.remainingSamples < nFrames) {
            pushLogical(Logical{static_cast<int>(preview.remainingSamples), PreviewRelease, PhaseTimeline, 0, 0,
                                static_cast<std::uint8_t>(s), 0, 0});
            preview.remainingSamples = 0;
        } else {
            preview.remainingSamples -= nFrames;
        }
    }
}

void Scheduler::chase(double ppq) noexcept {
    assert(ppq >= 0.0);
    if (!snapshotUsable()) {
        return;
    }
    const double endBeat = mSnapshot.endBeat;
    if (!mSnapshot.loop && ppq >= endBeat) {
        return;
    }
    // Only the wrap containing `ppq` can hold a sounding note: offs never pass endBeat, so a note of the
    // previous wrap is released by `ppq` at the latest. An on exactly at `ppq` is left to the timeline,
    // which enumerates it in this very block; an off exactly at `ppq` has already released its note.
    const double base = mSnapshot.loop ? std::floor(ppq / endBeat) * endBeat : 0.0;
    bool sounding[kPitchCount] = {};
    std::uint8_t velocity[kPitchCount] = {};
    for (std::size_t i = 0; i < mSnapshot.count; ++i) {
        const NoteEvent& e = mSnapshot.events[i];
        const double abs = base + e.beat;
        if (abs > ppq) {
            break;
        }
        if (!validPitch(e.pitch)) {
            continue;
        }
        if (e.on) {
            if (abs < ppq) {
                sounding[e.pitch] = true;
                velocity[e.pitch] = clampVelocity(e.velocity);
            }
        } else {
            sounding[e.pitch] = false;
        }
    }
    for (int pitch = 0; pitch < kPitchCount; ++pitch) {
        if (sounding[pitch]) {
            pushLogical(Logical{0, ProgressionAttack, PhaseChase, static_cast<std::uint8_t>(pitch), velocity[pitch],
                                0, 0, 0});
        }
    }
}

double Scheduler::enumerate(double lo, double start, double end, int nFrames, double samplesPerBeat) noexcept {
    if (!snapshotUsable() || !(lo < end)) {
        return end;
    }
    const double endBeat = mSnapshot.endBeat;
    long long kLo = 0;
    long long kHi = 0;
    if (mSnapshot.loop) {
        // One wrap earlier than `lo` as well: its events at beat endBeat sit exactly on the next wrap's
        // start, which may be `lo` itself.
        const double first = std::floor(lo / endBeat) - 1.0;
        const double last = std::floor(end / endBeat);
        // `process` already skips a block that spans more than kMaxWraps wraps; this keeps the casts
        // defined should the span check ever be bypassed.
        if (!(first < 1e15 && last < 1e15) || last - first > kMaxWraps + 2.0) {
            return end;
        }
        kLo = first < 0.0 ? 0 : static_cast<long long>(first);
        kHi = static_cast<long long>(last);
    }
    const int lastOffset = nFrames > 0 ? nFrames - 1 : 0;
    for (long long k = kLo; k <= kHi; ++k) {
        const double base = static_cast<double>(k) * endBeat;
        for (std::size_t i = 0; i < mSnapshot.count; ++i) {
            const NoteEvent& e = mSnapshot.events[i];
            const double abs = base + e.beat;
            if (abs < lo) {
                continue;
            }
            if (abs >= end) {
                break;
            }
            if (!validPitch(e.pitch) || (!mSnapshot.loop && e.on && abs >= endBeat)) {
                continue;
            }
            const long offset = std::lround((abs - start) * samplesPerBeat);
            if (offset >= nFrames) {
                // Rounding put it on the first sample of the next block: leave it there. Everything after
                // it is later still.
                return abs;
            }
            const Logical event{static_cast<int>(std::clamp<long>(offset, 0, lastOffset)),
                                e.on ? ProgressionAttack : ProgressionRelease,
                                PhaseTimeline,
                                static_cast<std::uint8_t>(e.pitch),
                                e.on ? clampVelocity(e.velocity) : std::uint8_t{0},
                                0,
                                0,
                                0};
            if (!pushLogical(event)) {
                return abs;
            }
        }
    }
    return end;
}

// --- Ownership ---------------------------------------------------------------------------------

bool Scheduler::previewHeldElsewhere(const State& state, std::size_t slot, std::uint8_t pitch) noexcept {
    for (std::size_t s = 0; s < kMaxPreviews; ++s) {
        const Preview& other = state.previews[s];
        if (s == slot || !other.active) {
            continue;
        }
        for (std::uint8_t i = 0; i < other.count; ++i) {
            if ((other.held & (1u << i)) != 0 && other.pitches[i] == pitch) {
                return true;
            }
        }
    }
    return false;
}

void Scheduler::releaseProgression(State& state, int offset, std::uint8_t pitch, Writer& writer) noexcept {
    std::uint8_t& owners = state.owners[pitch];
    if ((owners & kOwnerProgression) == 0) {
        return;
    }
    if (owners == kOwnerProgression) {
        if (writer.off(offset, pitch)) {
            owners = 0;
        }
    } else {
        owners = static_cast<std::uint8_t>(owners & ~kOwnerProgression);
    }
}

void Scheduler::releasePreview(State& state, std::size_t slot, int offset, Writer& writer) noexcept {
    Preview& preview = state.previews[slot];
    if (!preview.active) {
        return;
    }
    for (std::uint8_t i = 0; i < preview.count; ++i) {
        const std::uint8_t bit = static_cast<std::uint8_t>(1u << i);
        if ((preview.held & bit) == 0) {
            continue;
        }
        const std::uint8_t pitch = preview.pitches[i];
        std::uint8_t& owners = state.owners[pitch];
        if (previewHeldElsewhere(state, slot, pitch)) {
            preview.held = static_cast<std::uint8_t>(preview.held & ~bit);  // the preview owner lives on
        } else if (owners == kOwnerPreview) {
            if (writer.off(offset, pitch)) {
                owners = 0;
                preview.held = static_cast<std::uint8_t>(preview.held & ~bit);
            }
        } else {
            owners = static_cast<std::uint8_t>(owners & ~kOwnerPreview);
            preview.held = static_cast<std::uint8_t>(preview.held & ~bit);
        }
    }
    if (preview.held == 0) {
        preview.active = false;
    }
}

void Scheduler::apply(State& state, const Logical& event, Writer& writer) noexcept {
    switch (event.kind) {
        case ProgressionAttack: {
            std::uint8_t& owners = state.owners[event.pitch];
            if (owners == 0) {
                if (writer.on(event.offset, event.pitch, event.velocity)) {
                    owners = kOwnerProgression;
                }
            } else if (writer.retrigger(event.offset, event.pitch, event.velocity)) {
                owners = static_cast<std::uint8_t>(owners | kOwnerProgression);
            }
            break;
        }
        case ProgressionRelease:
            releaseProgression(state, event.offset, event.pitch, writer);
            break;
        case ProgressionReleaseAll:
            for (int pitch = 0; pitch < kPitchCount; ++pitch) {
                releaseProgression(state, event.offset, static_cast<std::uint8_t>(pitch), writer);
            }
            break;
        case PreviewAttack: {
            Preview& preview = state.previews[event.slot];
            if (!preview.active) {
                break;
            }
            std::uint8_t& owners = state.owners[event.pitch];
            if (owners == 0 && !writer.on(event.offset, event.pitch, event.velocity)) {
                break;
            }
            owners = static_cast<std::uint8_t>(owners | kOwnerPreview);
            preview.held = static_cast<std::uint8_t>(preview.held | (1u << event.index));
            break;
        }
        case PreviewRelease:
            releasePreview(state, event.slot, event.offset, writer);
            break;
    }
}

// --- The block -----------------------------------------------------------------------------------

bool Scheduler::process(const BlockInfo& info, MidiOut* out, std::size_t capacity, std::size_t& written) noexcept {
    assert(out != nullptr || capacity == 0);

    written = 0;
    mScratchCount = 0;
    mDropped = false;
    if (out == nullptr) {
        capacity = 0;
    }
    // A block with unusable timing (a host that has not set its tempo yet, say) is treated as a stopped
    // block: previews are still served and the progression's notes are released, nothing is played.
    const bool timingValid = info.nFrames >= 0 && info.sampleRate > 0.0 && std::isfinite(info.sampleRate) &&
                             info.bpm > 0.0 && std::isfinite(info.bpm) && std::isfinite(info.ppqStart);
    const int nFrames = timingValid ? info.nFrames : 0;
    const bool playing = info.playing && timingValid;
    const double start = timingValid ? info.ppqStart : 0.0;
    const double previewRate = mSampleRate > 0.0 ? mSampleRate : (timingValid ? info.sampleRate : 0.0);
    double end = start;
    double samplesPerBeat = 0.0;
    if (playing) {
        samplesPerBeat = info.sampleRate * 60.0 / info.bpm;
        end = start + static_cast<double>(nFrames) * info.bpm / (60.0 * info.sampleRate);
        if (!std::isfinite(end)) {
            end = start;
        }
    }

    // 1. Preview commands.
    drainPreviews(previewRate);

    // 2. Discontinuities.
    const bool stopped = !playing && mWasPlaying;
    const bool started = playing && !mWasPlaying;
    const bool continuous = playing && mWasPlaying && mHasFrontier &&
                            std::fabs(start - mFrontier) <= kSeekThresholdBeats;
    const bool seek = playing && mWasPlaying && !continuous;
    const bool flush = stopped || started || seek || mPendingFlush;
    // The first beat the timeline enumerates: where the previous block stopped when playback simply
    // continues (so sub-threshold host jitter never duplicates or skips a boundary event), else here.
    // Nothing sounds before ppq 0.
    const double lo = std::max(continuous && !flush ? mFrontier : start, 0.0);
    // A block spanning more wraps than kMaxWrapsPerBlock (an absurdly short progression, or an absurdly
    // long block) is not played: the progression is released and the block skipped, so the work per
    // block stays bounded whatever the host or the snapshot say.
    const bool spansTooMuch = playing && snapshotUsable() && mSnapshot.loop && lo < end &&
                              (end - lo) / mSnapshot.endBeat > kMaxWraps;
    if (flush || spansTooMuch) {
        pushLogical(Logical{0, ProgressionReleaseAll, PhaseFlush, 0, 0, 0, 0, 0});
        if (flush && playing && !spansTooMuch && start >= 0.0) {
            chase(start);
        }
    }

    // 3. The timeline and the preview timers.
    double frontier = end;
    if (playing && !spansTooMuch) {
        frontier = enumerate(lo, start, end, nFrames, samplesPerBeat);
    }
    expirePreviews(nFrames);

    // 4. Order, apply ownership, write.
    std::sort(mScratch.begin(), mScratch.begin() + static_cast<std::ptrdiff_t>(mScratchCount),
              [](const Logical& a, const Logical& b) noexcept {
                  if (a.offset != b.offset) {
                      return a.offset < b.offset;
                  }
                  if (a.phase != b.phase) {
                      return a.phase < b.phase;
                  }
                  if (a.phase != PhasePreview) {  // preview commands keep their arrival order
                      const bool aAttack = a.kind == ProgressionAttack || a.kind == PreviewAttack;
                      const bool bAttack = b.kind == ProgressionAttack || b.kind == PreviewAttack;
                      if (aAttack != bAttack) {
                          return !aAttack;
                      }
                      if (a.pitch != b.pitch) {
                          return a.pitch < b.pitch;
                      }
                  }
                  return a.seq < b.seq;
              });
    Writer dry(nullptr, 0, 0, true);
    {
        State shadow = mState;
        for (std::size_t i = 0; i < mScratchCount; ++i) {
            apply(shadow, mScratch[i], dry);
        }
    }
    Writer writer(out, capacity, capacity > dry.releases ? capacity - dry.releases : 0, false);
    for (std::size_t i = 0; i < mScratchCount; ++i) {
        apply(mState, mScratch[i], writer);
    }
    for (Preview& preview : mState.previews) {
        if (preview.active && preview.held == 0) {
            preview.active = false;  // started with nothing acquired
        }
    }
    written = writer.written;

    // 5. Remember where we are.
    mWasPlaying = playing;
    if (playing) {
        mFrontier = continuous ? std::max(mFrontier, frontier) : frontier;
        mHasFrontier = true;
    } else {
        mHasFrontier = false;
    }
    mPendingFlush = false;
    return !mDropped && !writer.dropped;
}

}  // namespace chordlab
