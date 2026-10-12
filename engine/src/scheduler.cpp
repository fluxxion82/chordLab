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
/// Sample positions are clamped into +-this before they become integers (well inside int64).
constexpr double kSampleLimit = 4e18;
/// Wrap indices are computed in doubles and must fit an integer with room to spare.
constexpr double kWrapLimit = 1e15;
/// The most samples a preview timer may count (four seconds of any sane sample rate).
constexpr double kMaxPreviewSamples = 1e12;

std::uint8_t clampVelocity(int velocity) noexcept {
    return static_cast<std::uint8_t>(std::clamp(velocity, 1, 127));
}

bool validPitch(int pitch) noexcept {
    return pitch >= 0 && pitch < kPitchCount;
}

bool usableSampleRate(double sampleRate) noexcept {
    return std::isfinite(sampleRate) && sampleRate > 0.0 &&
           Scheduler::kPreviewSeconds * sampleRate <= kMaxPreviewSamples;
}

}  // namespace

static_assert(Scheduler::kReservedReleaseEvents >= 1 + 2 * Scheduler::kMaxPreviews,
              "the reserve must hold a release-all and two expiry candidates per preview");
static_assert(Scheduler::kReservedReleaseEvents < Scheduler::kMaxEventsPerBlock, "no room left for attacks");
static_assert(Scheduler::kCapacityForImmediateRelease >= 128, "one note-off per pitch may be owed");

/// Takes the physical events ownership produces and writes them out. In a dry run nothing is written and
/// every attack is accepted, which counts the most note-offs the block can need; the real run then keeps
/// that many slots free for them and spends the rest on attacks. A note-off is refused only when the
/// output is already full, and its caller then records it as owed.
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
        dropped = true;  // owed instead: the block is incomplete
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
      mEndIndex(0),
      mHasSnapshot(false),
      mSampleRate(0.0),
      mWasPlaying(false),
      mPendingFlush(false),
      mHasAnchor(false),
      mAnchorPpq(0.0),
      mSamplesPerBeat(0.0),
      mBlockSample(0),
      mFrontierSample(0),
      mHasLag(false),
      mLagBeat(0.0),
      mPreviewClock(0),
      mScratch{},
      mScratchCount(0),
      mDropped(false),
      mNextFrontier(0),
      mNextHasLag(false),
      mNextLagBeat(0.0),
      mBatch{},
      mBatchCount(0),
      mQueue{},
      mQueueHead(0),
      mQueueTail(0) {
    mState.owners.fill(0);
    mState.owed.fill(false);
    for (Preview& preview : mState.previews) {
        preview = Preview{};
    }
}

void Scheduler::reset(double sampleRate) noexcept {
    assert(usableSampleRate(sampleRate));
    if (usableSampleRate(sampleRate)) {
        mSampleRate = sampleRate;
    }
    mPendingFlush = true;
}

void Scheduler::adopt(const Snapshot& snapshot) noexcept {
    assert(snapshot.events != nullptr || snapshot.count == 0);
    assert(std::isfinite(snapshot.endBeat));
    assert(snapshot.count <= kMaxSnapshotEvents);
#ifndef NDEBUG
    for (std::size_t i = 1; i < snapshot.count && i < kMaxSnapshotEvents; ++i) {
        assert(snapshot.events[i - 1].beat <= snapshot.events[i].beat);
    }
#endif
    if (!mHasSnapshot || snapshot.generation != mSnapshot.generation) {
        mPendingFlush = true;
    }
    mSnapshot = snapshot;
    mHasSnapshot = true;
    if (mSnapshot.events == nullptr || mSnapshot.count > kMaxSnapshotEvents || !std::isfinite(mSnapshot.endBeat)) {
        mSnapshot.events = nullptr;
        mSnapshot.count = 0;
    }
    // The final offs sit at endBeat (a sorted suffix); `enumerate` places them on the next wrap's start.
    mEndIndex = mSnapshot.count;
    for (std::size_t i = 0; i < mSnapshot.count; ++i) {
        if (mSnapshot.events[i].beat >= mSnapshot.endBeat) {
            mEndIndex = i;
            break;
        }
    }
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

void Scheduler::panic() noexcept {
    for (int pitch = 0; pitch < kPitchCount; ++pitch) {
        if (mState.owners[static_cast<std::size_t>(pitch)] != 0) {
            mState.owners[static_cast<std::size_t>(pitch)] = 0;
            mState.owed[static_cast<std::size_t>(pitch)] = true;
        }
    }
    for (Preview& preview : mState.previews) {
        preview.active = false;
        preview.held = 0;
    }
}

std::uint8_t Scheduler::ownersOf(int pitch) const noexcept {
    assert(validPitch(pitch));
    return validPitch(pitch) ? mState.owners[static_cast<std::size_t>(pitch)] : std::uint8_t{0};
}

bool Scheduler::sounding(int pitch) const noexcept {
    assert(validPitch(pitch));
    return validPitch(pitch) &&
           (mState.owners[static_cast<std::size_t>(pitch)] != 0 || mState.owed[static_cast<std::size_t>(pitch)]);
}

bool Scheduler::snapshotUsable() const noexcept {
    return mSnapshot.events != nullptr && mSnapshot.count > 0 && mSnapshot.endBeat > 0.0 &&
           std::isfinite(mSnapshot.endBeat);
}

/// The clock sample of `beat`, rounded half up, saturated far outside any block so that comparisons
/// against a block window stay meaningful whatever the inputs.
std::int64_t Scheduler::sampleOf(double beat, double samplesPerBeat) const noexcept {
    const double x = (beat - mAnchorPpq) * samplesPerBeat;
    if (!(x > -kSampleLimit)) {
        return x < 0.0 ? -static_cast<std::int64_t>(kSampleLimit) : static_cast<std::int64_t>(kSampleLimit);
    }
    if (x > kSampleLimit) {
        return static_cast<std::int64_t>(kSampleLimit);
    }
    return static_cast<std::int64_t>(std::floor(x + 0.5));
}

bool Scheduler::pushLogical(const Logical& event, bool mandatory) noexcept {
    const std::size_t limit = mandatory ? kMaxEventsPerBlock : kMaxEventsPerBlock - kReservedReleaseEvents;
    if (mScratchCount >= limit) {
        return false;
    }
    mScratch[mScratchCount] = event;
    mScratch[mScratchCount].seq = static_cast<std::uint16_t>(mScratchCount);
    ++mScratchCount;
    return true;
}

// --- Block steps -------------------------------------------------------------------------------

void Scheduler::drainQueue() noexcept {
    mBatchCount = 0;
    std::uint32_t head = mQueueHead.load(std::memory_order_relaxed);
    const std::uint32_t tail = mQueueTail.load(std::memory_order_acquire);
    while (head != tail && mBatchCount < kPreviewQueueCapacity) {
        mBatch[mBatchCount] = mQueue[head % kPreviewQueueCapacity];
        ++mBatchCount;
        ++head;
    }
    mQueueHead.store(head, std::memory_order_release);
}

void Scheduler::pushExpiries(int nFrames, std::int64_t previewSamples) noexcept {
    // Two candidates per slot: the expiry of the preview running in it now, and the expiry a preview
    // started by this block's commands would have. `apply` checks the slot's actual deadline.
    for (std::size_t s = 0; s < kMaxPreviews; ++s) {
        const Preview& preview = mState.previews[s];
        const std::uint8_t slot = static_cast<std::uint8_t>(s);
        std::int64_t running = -1;
        if (preview.active) {
            running = std::max<std::int64_t>(preview.expireAt - mPreviewClock, 0);
            if (running < nFrames) {
                pushLogical(Logical{static_cast<int>(running), PreviewExpiry, PhaseTimeline, 0, 0, slot, 0}, true);
            }
        }
        if (previewSamples < nFrames && previewSamples != running) {
            pushLogical(Logical{static_cast<int>(previewSamples), PreviewExpiry, PhaseTimeline, 0, 0, slot, 0},
                        true);
        }
    }
}

void Scheduler::chase(double samplesPerBeat) noexcept {
    if (!snapshotUsable()) {
        return;
    }
    const double position = mAnchorPpq + static_cast<double>(mBlockSample) / samplesPerBeat;
    const double endBeat = mSnapshot.endBeat;
    if (!(position >= 0.0) || (!mSnapshot.loop && position >= endBeat)) {
        return;
    }
    // Only the wrap containing the position can hold a sounding note: a note of the previous wrap is
    // released by that wrap's end at the latest. A note sounds if its on is before the block's first
    // sample and its off after it; an on exactly on that sample is left to the timeline, which emits it in
    // this very block, and an off exactly on it has already released its note.
    const double wrap = mSnapshot.loop ? std::floor(position / endBeat) : 0.0;
    if (!(wrap < kWrapLimit)) {
        return;
    }
    const double base = wrap * endBeat;
    bool sounding[kPitchCount] = {};
    std::uint8_t velocity[kPitchCount] = {};
    for (std::size_t i = 0; i < mEndIndex; ++i) {
        const NoteEvent& e = mSnapshot.events[i];
        const std::int64_t sample = sampleOf(base + e.beat, samplesPerBeat);
        if (sample > mBlockSample) {
            break;
        }
        if (!validPitch(e.pitch)) {
            continue;
        }
        if (e.on) {
            if (sample < mBlockSample) {
                sounding[e.pitch] = true;
                velocity[e.pitch] = clampVelocity(e.velocity);
            }
        } else {
            sounding[e.pitch] = false;
        }
    }
    if (mEndIndex < mSnapshot.count && sampleOf((wrap + 1.0) * endBeat, samplesPerBeat) <= mBlockSample) {
        for (std::size_t i = mEndIndex; i < mSnapshot.count; ++i) {
            const NoteEvent& e = mSnapshot.events[i];
            if (!e.on && validPitch(e.pitch)) {
                sounding[e.pitch] = false;
            }
        }
    }
    for (int pitch = 0; pitch < kPitchCount; ++pitch) {
        if (sounding[pitch] &&
            !pushLogical(Logical{0, ProgressionAttack, PhaseChase, static_cast<std::uint8_t>(pitch), velocity[pitch],
                                 0, 0},
                         false)) {
            mDropped = true;
        }
    }
}

void Scheduler::enumerate(std::int64_t windowLo, std::int64_t windowHi, int nFrames,
                          double samplesPerBeat) noexcept {
    // Events whose clock sample falls in [windowLo, windowHi), in time order. The first event at or past
    // windowHi ends the block (everything after it is later still); running out of scratch ends it too,
    // after rolling back the partial group of events that share that sample, which the next block then
    // starts from (`mNextFrontier`, `mNextLagBeat`).
    if (!snapshotUsable()) {
        return;
    }
    const double endBeat = mSnapshot.endBeat;
    const double beatLo = std::max(mAnchorPpq + static_cast<double>(windowLo - 1) / samplesPerBeat, 0.0);
    const double beatHi = mAnchorPpq + static_cast<double>(windowHi + 1) / samplesPerBeat;
    if (!(beatLo < beatHi)) {
        return;  // the progression has not begun
    }
    double firstWrap = 0.0;
    double lastWrap = 0.0;
    if (mSnapshot.loop) {
        // One wrap earlier than the window as well: its final offs sit on the next wrap's start.
        firstWrap = std::max(std::floor(beatLo / endBeat) - 1.0, 0.0);
        lastWrap = std::floor(beatHi / endBeat);
        if (!(lastWrap < kWrapLimit) || lastWrap - firstWrap > static_cast<double>(kMaxWrapsPerBlock) + 2.0) {
            return;  // `process` has already ruled this block out; keep the casts defined regardless
        }
    }
    const int lastOffset = nFrames - 1;
    std::size_t groupStart = mScratchCount;
    std::int64_t groupSample = windowLo - 1;
    double groupBeat = 0.0;
    auto emit = [&](const NoteEvent& e, std::int64_t sample, double beat) noexcept -> bool {
        if (sample != groupSample) {
            groupStart = mScratchCount;
            groupSample = sample;
            groupBeat = beat;
        }
        const std::int64_t offset = std::clamp<std::int64_t>(sample - mBlockSample, 0, lastOffset);
        const Logical event{static_cast<int>(offset),
                            e.on ? ProgressionAttack : ProgressionRelease,
                            PhaseTimeline,
                            static_cast<std::uint8_t>(e.pitch),
                            e.on ? clampVelocity(e.velocity) : std::uint8_t{0},
                            0,
                            0};
        if (pushLogical(event, false)) {
            return true;
        }
        mScratchCount = groupStart;
        mNextFrontier = groupSample;
        mNextHasLag = true;
        mNextLagBeat = groupBeat;
        mDropped = true;  // carried over, not lost; still an incomplete block
        return false;
    };
    for (double wrap = firstWrap; wrap <= lastWrap; wrap += 1.0) {
        const double base = wrap * endBeat;
        for (std::size_t i = 0; i < mEndIndex; ++i) {
            const NoteEvent& e = mSnapshot.events[i];
            const double beat = base + e.beat;
            const std::int64_t sample = sampleOf(beat, samplesPerBeat);
            if (sample < windowLo) {
                continue;
            }
            if (sample >= windowHi) {
                return;
            }
            if (validPitch(e.pitch) && !emit(e, sample, beat)) {
                return;
            }
        }
        if (mEndIndex < mSnapshot.count) {
            const double beat = (wrap + 1.0) * endBeat;
            const std::int64_t sample = sampleOf(beat, samplesPerBeat);
            if (sample >= windowHi) {
                return;
            }
            if (sample >= windowLo) {
                for (std::size_t i = mEndIndex; i < mSnapshot.count; ++i) {
                    const NoteEvent& e = mSnapshot.events[i];
                    if (e.on && !mSnapshot.loop) {
                        continue;  // nothing attacks at or after endBeat when playing once
                    }
                    if (validPitch(e.pitch) && !emit(e, sample, beat)) {
                        return;
                    }
                }
            }
        }
    }
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

void Scheduler::physicalOff(State& state, int offset, std::uint8_t pitch, Writer& writer) noexcept {
    if (!writer.off(offset, pitch)) {
        state.owed[pitch] = true;  // paid by a later block
    }
}

bool Scheduler::attack(State& state, std::uint8_t owner, int offset, std::uint8_t pitch, std::uint8_t velocity,
                       Writer& writer) noexcept {
    std::uint8_t& owners = state.owners[pitch];
    if (owners == 0) {
        if (state.owed[pitch]) {
            if (!writer.retrigger(offset, pitch, velocity)) {
                return false;
            }
            state.owed[pitch] = false;
        } else if (!writer.on(offset, pitch, velocity)) {
            return false;
        }
    } else if (owner == kOwnerProgression) {
        if (!writer.retrigger(offset, pitch, velocity)) {
            return false;
        }
    }
    owners = static_cast<std::uint8_t>(owners | owner);
    return true;
}

void Scheduler::releaseProgression(State& state, int offset, std::uint8_t pitch, Writer& writer) noexcept {
    std::uint8_t& owners = state.owners[pitch];
    if ((owners & kOwnerProgression) == 0) {
        return;
    }
    owners = static_cast<std::uint8_t>(owners & ~kOwnerProgression);
    if (owners == 0) {
        physicalOff(state, offset, pitch, writer);
    }
}

void Scheduler::releasePreview(State& state, std::size_t slot, int offset, Writer& writer) noexcept {
    Preview& preview = state.previews[slot];
    if (!preview.active) {
        return;
    }
    for (std::uint8_t i = 0; i < preview.count; ++i) {
        if ((preview.held & (1u << i)) == 0) {
            continue;
        }
        const std::uint8_t pitch = preview.pitches[i];
        std::uint8_t& owners = state.owners[pitch];
        if ((owners & kOwnerPreview) != 0 && !previewHeldElsewhere(state, slot, pitch)) {
            owners = static_cast<std::uint8_t>(owners & ~kOwnerPreview);
            if (owners == 0) {
                physicalOff(state, offset, pitch, writer);
            }
        }
    }
    preview.held = 0;
    preview.active = false;
}

void Scheduler::payOwed(State& state, Writer& writer) noexcept {
    for (int pitch = 0; pitch < kPitchCount; ++pitch) {
        const std::size_t p = static_cast<std::size_t>(pitch);
        if (state.owed[p] && writer.off(0, static_cast<std::uint8_t>(pitch))) {
            state.owed[p] = false;
        }
    }
}

void Scheduler::applyBatch(State& state, const PreviewCmd* batch, std::size_t count, std::int64_t clock,
                           std::int64_t previewSamples, Writer& writer) noexcept {
    for (std::size_t c = 0; c < count; ++c) {
        const PreviewCmd& cmd = batch[c];
        // A stop, or a start that restarts an id, first releases every running preview with that id, so
        // the slot is free again before a start looks for one.
        for (std::size_t s = 0; s < kMaxPreviews; ++s) {
            if (state.previews[s].active && state.previews[s].id == cmd.id) {
                releasePreview(state, s, 0, writer);
            }
        }
        if (cmd.kind != PreviewCmd::Start) {
            continue;
        }
        std::size_t slot = kMaxPreviews;
        for (std::size_t s = 0; s < kMaxPreviews; ++s) {
            if (!state.previews[s].active) {
                slot = s;
                break;
            }
        }
        if (slot == kMaxPreviews) {
            writer.dropped = true;  // more concurrent previews than the table holds
            continue;
        }
        Preview& preview = state.previews[slot];
        preview.active = true;
        preview.id = cmd.id;
        preview.expireAt = clock + previewSamples;
        preview.count = static_cast<std::uint8_t>(std::min<unsigned>(cmd.count, 8u));
        preview.held = 0;
        preview.velocity = clampVelocity(cmd.velocity);
        for (std::size_t i = 0; i < 8; ++i) {
            preview.pitches[i] = cmd.pitches[i];
        }
        for (std::uint8_t i = 0; i < preview.count; ++i) {
            const std::uint8_t pitch = preview.pitches[i];
            if (!validPitch(pitch)) {
                continue;
            }
            bool repeated = false;  // the same pitch twice in one command is held once
            for (std::uint8_t j = 0; j < i; ++j) {
                repeated = repeated || ((preview.held & (1u << j)) != 0 && preview.pitches[j] == pitch);
            }
            if (!repeated && attack(state, kOwnerPreview, 0, pitch, preview.velocity, writer)) {
                preview.held = static_cast<std::uint8_t>(preview.held | (1u << i));
            }
        }
        if (preview.held == 0) {
            preview.active = false;  // nothing acquired
        }
    }
}

void Scheduler::apply(State& state, const Logical& event, std::int64_t clock, Writer& writer) noexcept {
    switch (event.kind) {
        case ProgressionAttack:
            attack(state, kOwnerProgression, event.offset, event.pitch, event.velocity, writer);
            break;
        case ProgressionRelease:
            releaseProgression(state, event.offset, event.pitch, writer);
            break;
        case ProgressionReleaseAll:
            for (int pitch = 0; pitch < kPitchCount; ++pitch) {
                releaseProgression(state, event.offset, static_cast<std::uint8_t>(pitch), writer);
            }
            break;
        case PreviewExpiry: {
            const Preview& preview = state.previews[event.slot];
            if (preview.active && preview.expireAt <= clock + event.offset) {
                releasePreview(state, event.slot, event.offset, writer);
            }
            break;
        }
    }
}

// --- The block -----------------------------------------------------------------------------------

bool Scheduler::process(const BlockInfo& info, MidiOut* out, std::size_t capacity, std::size_t& written) noexcept {
    assert(out != nullptr || capacity == 0);
    written = 0;
    if (out == nullptr) {
        capacity = 0;
    }
    if (info.nFrames == 0) {
        return true;  // an empty block: nothing happens, nothing moves
    }

    // Timing. A block whose values are unusable is a stopped block: previews are still served and the
    // progression is released, but nothing is played and no clock advances.
    double samplesPerBeat = 0.0;
    bool timingValid = info.nFrames > 0 && usableSampleRate(info.sampleRate) && std::isfinite(info.bpm) &&
                       info.bpm > 0.0 && std::isfinite(info.ppqStart);
    if (timingValid) {
        samplesPerBeat = info.sampleRate * 60.0 / info.bpm;
        timingValid = std::isfinite(samplesPerBeat) && samplesPerBeat > 0.0 && samplesPerBeat <= kMaxSamplesPerBeat;
    }
    const int nFrames = timingValid ? info.nFrames : 0;
    const bool playing = info.playing && timingValid;
    const double previewRate = mSampleRate > 0.0 ? mSampleRate : (timingValid ? info.sampleRate : 0.0);
    const std::int64_t previewSamples = static_cast<std::int64_t>(std::floor(kPreviewSeconds * previewRate + 0.5));

    mScratchCount = 0;
    mDropped = false;
    drainQueue();

    // Discontinuities, and the clock.
    const bool stopped = !playing && mWasPlaying;
    const bool started = playing && !mWasPlaying;
    const double expected = mHasAnchor ? mAnchorPpq + static_cast<double>(mBlockSample) / mSamplesPerBeat : 0.0;
    const bool continuous = playing && mWasPlaying && mHasAnchor &&
                            std::fabs(info.ppqStart - expected) <= kSeekThresholdBeats;
    const bool seek = playing && mWasPlaying && !continuous;
    const bool flush = stopped || started || seek || mPendingFlush;
    if (started || seek) {
        mHasAnchor = true;
        mAnchorPpq = info.ppqStart;
        mSamplesPerBeat = samplesPerBeat;
        mBlockSample = 0;
        mFrontierSample = 0;
        mHasLag = false;
    } else if (continuous) {
        if (samplesPerBeat != mSamplesPerBeat) {
            // Tempo or sample rate changed: re-anchor the clock where this block was expected to start.
            mAnchorPpq = expected;
            mSamplesPerBeat = samplesPerBeat;
            mBlockSample = 0;
            mFrontierSample = mHasLag ? sampleOf(mLagBeat, samplesPerBeat) : 0;
        }
        if (mPendingFlush) {
            mFrontierSample = mBlockSample;  // the chase re-acquires whatever was still to come
            mHasLag = false;
        }
    }
    const std::int64_t windowLo = mFrontierSample;
    const std::int64_t windowHi = mBlockSample + nFrames;
    mNextFrontier = windowHi;
    mNextHasLag = false;
    mNextLagBeat = 0.0;

    // The block's events: mandatory releases first, so that nothing can displace them.
    if (flush) {
        pushLogical(Logical{0, ProgressionReleaseAll, PhaseFlush, 0, 0, 0, 0}, true);
    }
    pushExpiries(nFrames, previewSamples);
    if (playing) {
        const bool spansTooMuch =
            snapshotUsable() && mSnapshot.loop &&
            static_cast<double>(windowHi - windowLo) / samplesPerBeat / mSnapshot.endBeat >
                static_cast<double>(kMaxWrapsPerBlock);
        if (spansTooMuch) {
            if (!flush) {
                pushLogical(Logical{0, ProgressionReleaseAll, PhaseFlush, 0, 0, 0, 0}, true);
            }
        } else {
            if (flush) {
                chase(samplesPerBeat);
            }
            enumerate(windowLo, windowHi, nFrames, samplesPerBeat);
        }
    }

    // Order, then apply ownership twice: a dry run to count the note-offs, the real run with the rest of
    // the capacity for attacks. Owed note-offs and the preview commands come before the sorted events.
    std::sort(mScratch.begin(), mScratch.begin() + static_cast<std::ptrdiff_t>(mScratchCount),
              [](const Logical& a, const Logical& b) noexcept {
                  if (a.offset != b.offset) {
                      return a.offset < b.offset;
                  }
                  if (a.phase != b.phase) {
                      return a.phase < b.phase;
                  }
                  const bool aAttack = a.kind == ProgressionAttack;
                  const bool bAttack = b.kind == ProgressionAttack;
                  if (aAttack != bAttack) {
                      return !aAttack;
                  }
                  if (a.pitch != b.pitch) {
                      return a.pitch < b.pitch;
                  }
                  return a.seq < b.seq;
              });
    Writer dry(nullptr, 0, 0, true);
    {
        State shadow = mState;
        payOwed(shadow, dry);
        applyBatch(shadow, mBatch.data(), mBatchCount, mPreviewClock, previewSamples, dry);
        for (std::size_t i = 0; i < mScratchCount; ++i) {
            apply(shadow, mScratch[i], mPreviewClock, dry);
        }
    }
    Writer writer(out, capacity, capacity > dry.releases ? capacity - dry.releases : 0, false);
    payOwed(mState, writer);
    applyBatch(mState, mBatch.data(), mBatchCount, mPreviewClock, previewSamples, writer);
    for (std::size_t i = 0; i < mScratchCount; ++i) {
        apply(mState, mScratch[i], mPreviewClock, writer);
    }
    written = writer.written;

    // Advance.
    mPreviewClock += nFrames;
    if (playing) {
        mBlockSample += nFrames;
        mFrontierSample = mNextFrontier;
        mHasLag = mNextHasLag;
        mLagBeat = mNextLagBeat;
    } else {
        mHasAnchor = false;
    }
    mWasPlaying = playing;
    mPendingFlush = false;
    return !mDropped && !writer.dropped;
}

}  // namespace chordlab
