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
constexpr std::int64_t kWrapLimitInt = static_cast<std::int64_t>(kWrapLimit);
/// The most samples a preview timer may count (four seconds of any sane sample rate).
constexpr double kMaxPreviewSamples = 1e12;
/// A hard stop for the enumeration loop, far beyond what the scratch and the lag bound allow.
constexpr std::size_t kMaxEnumerationSteps = 8 * Scheduler::kMaxSnapshotEvents + Scheduler::kMaxEventsPerBlock;

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
      mPreviewClock(0),
      mHasCursor(false),
      mCursorWrap(0),
      mCursorIndex(0),
      mHasNext(false),
      mNextBeat(0.0),
      mBacklogAbandoned(false),
      mScratch{},
      mScratchCount(0),
      mDropped(false),
      mBatch{},
      mBatchCount(0),
      mQueue{},
      mQueueHead(0),
      mQueueTail(0),
      mCancelQueued(false),
      mCancelTail(0) {
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
    const bool changed = !mHasSnapshot || snapshot.generation != mSnapshot.generation;
    if (changed) {
        // Occurrences of the old timeline that are overdue but not yet emitted are abandoned with it;
        // the next block says so. Judged from `mNextBeat`, kept by `process`: the old array may already
        // belong to the producer again (Snapshot, scheduler.h) and is not read here.
        if (mWasPlaying && mHasAnchor && mHasNext && sampleOf(mNextBeat, mSamplesPerBeat) < mBlockSample) {
            mBacklogAbandoned = true;
        }
        mPendingFlush = true;
        mHasCursor = false;  // the pending flush re-seeks it
        mHasNext = false;
    }
    // With the generation unchanged the content is the same by contract: the cursor and everything else
    // stay as they are, and playback does not notice.
    mSnapshot = snapshot;
    mHasSnapshot = true;
    if (mSnapshot.events == nullptr || mSnapshot.count > kMaxSnapshotEvents || !std::isfinite(mSnapshot.endBeat)) {
        mSnapshot.events = nullptr;
        mSnapshot.count = 0;
    }
    // The final offs sit at endBeat (a sorted suffix); they are placed on the next wrap's start.
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
    // Everything in the ring right now is stale: remember where it ends and let the next drain skip it.
    // Only the tail is read, so the producer is never waited for; commands it pushes from here on sit
    // past the cut and are served.
    mCancelTail = mQueueTail.load(std::memory_order_acquire);
    mCancelQueued = true;
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

/// The beat of occurrence (`wrap`, `index`): the wrap's start plus the event's beat, except that the final
/// offs (at endBeat) sit exactly on the next wrap's start, so that `k * endBeat + endBeat` and
/// `(k + 1) * endBeat` cannot disagree by a rounding error.
double Scheduler::canonicalBeat(std::int64_t wrap, std::size_t index) const noexcept {
    const double w = static_cast<double>(wrap);
    if (index >= mEndIndex) {
        return (w + 1.0) * mSnapshot.endBeat;
    }
    return w * mSnapshot.endBeat + mSnapshot.events[index].beat;
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
    if (mCancelQueued) {
        // The cut lies in [head, tail]: `panic` and this drain run on the same thread, head moves only
        // here, and the tail only grows. Indices are free-running, so the check is on differences.
        assert(mCancelTail - head <= tail - head);
        head = mCancelTail;
        mCancelQueued = false;
    }
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
                pushLogical(Logical{mBlockSample + running, PreviewExpiry, PhaseTimeline, 0, 0, slot, 0}, true);
            }
        }
        if (previewSamples < nFrames && previewSamples != running) {
            pushLogical(Logical{mBlockSample + previewSamples, PreviewExpiry, PhaseTimeline, 0, 0, slot, 0}, true);
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
    const std::int64_t k = static_cast<std::int64_t>(wrap);
    bool sounding[kPitchCount] = {};
    std::uint8_t velocity[kPitchCount] = {};
    for (std::size_t i = 0; i < mEndIndex; ++i) {
        const NoteEvent& e = mSnapshot.events[i];
        const std::int64_t sample = sampleOf(canonicalBeat(k, i), samplesPerBeat);
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
    if (mEndIndex < mSnapshot.count && sampleOf(canonicalBeat(k, mEndIndex), samplesPerBeat) <= mBlockSample) {
        for (std::size_t i = mEndIndex; i < mSnapshot.count; ++i) {
            const NoteEvent& e = mSnapshot.events[i];
            if (!e.on && validPitch(e.pitch)) {
                sounding[e.pitch] = false;
            }
        }
    }
    for (int pitch = 0; pitch < kPitchCount; ++pitch) {
        if (sounding[pitch] &&
            !pushLogical(Logical{mBlockSample, ProgressionAttack, PhaseChase, static_cast<std::uint8_t>(pitch),
                                 velocity[pitch], 0, 0},
                         false)) {
            mDropped = true;
        }
    }
}

/// Points the cursor at the first occurrence on or after clock sample `targetSample`.
void Scheduler::seekCursor(std::int64_t targetSample, double samplesPerBeat) noexcept {
    placeCursor(targetSample, samplesPerBeat);
    refreshNext();
}

/// `seekCursor` without the refresh: every return path leaves the cursor placed, and the one caller
/// resolves `mNextBeat` afterwards.
void Scheduler::placeCursor(std::int64_t targetSample, double samplesPerBeat) noexcept {
    mHasCursor = true;
    mCursorWrap = 0;
    mCursorIndex = 0;
    if (!snapshotUsable()) {
        return;
    }
    const double targetBeat = mAnchorPpq + static_cast<double>(targetSample) / samplesPerBeat;
    double first = 0.0;
    if (mSnapshot.loop) {
        first = std::max(std::floor(targetBeat / mSnapshot.endBeat) - 1.0, 0.0);
        if (!(first < kWrapLimit)) {
            mHasCursor = false;  // an unreachable position: nothing is ever due there
            return;
        }
    }
    // The wrap after the one containing the target starts past it, so three wraps always suffice.
    const std::int64_t startWrap = static_cast<std::int64_t>(first);
    const std::int64_t lastWrap = mSnapshot.loop ? startWrap + 2 : 0;
    for (std::int64_t k = startWrap; k <= lastWrap; ++k) {
        for (std::size_t i = 0; i < mSnapshot.count; ++i) {
            if (sampleOf(canonicalBeat(k, i), samplesPerBeat) >= targetSample) {
                mCursorWrap = k;
                mCursorIndex = i;
                return;
            }
        }
    }
    mCursorWrap = lastWrap + 1;  // past the end of a one-shot progression, or unexpectedly far
    mCursorIndex = 0;
}

/// The occurrence the cursor points at, normalised; false when there is none (past a one-shot's end).
bool Scheduler::cursorNext(std::int64_t& wrap, std::size_t& index) const noexcept {
    if (!mHasCursor || !snapshotUsable()) {
        return false;
    }
    wrap = mCursorWrap;
    index = mCursorIndex;
    if (index >= mSnapshot.count) {
        wrap = wrap < kWrapLimitInt ? wrap + 1 : wrap;
        index = 0;
    }
    return mSnapshot.loop || wrap == 0;
}

/// Resolves the cursor's next occurrence to `mHasNext` / `mNextBeat`. Called whenever the cursor moves,
/// while the array is valid, so that the backlog checks never need the array (and `adopt`, which may run
/// after the array has been reclaimed, can still judge the old timeline).
void Scheduler::refreshNext() noexcept {
    std::int64_t wrap = 0;
    std::size_t index = 0;
    mHasNext = cursorNext(wrap, index);
    mNextBeat = mHasNext ? canonicalBeat(wrap, index) : 0.0;
}

/// Emits occurrences from the cursor on, in playback order, up to the first one at or past `windowHi`
/// (deferred to a later block) or until the scratch is full (the rest is carried over, in order; the
/// block is then incomplete). Occurrences behind the block start are emitted late, at offset 0.
void Scheduler::enumerate(std::int64_t windowHi, double samplesPerBeat) noexcept {
    std::int64_t wrap = 0;
    std::size_t index = 0;
    if (!cursorNext(wrap, index)) {
        return;
    }
    for (std::size_t steps = 0; steps < kMaxEnumerationSteps; ++steps) {
        if (index >= mSnapshot.count) {
            if (wrap >= kWrapLimitInt || !mSnapshot.loop) {
                break;
            }
            ++wrap;
            index = 0;
        }
        const NoteEvent& e = mSnapshot.events[index];
        const std::int64_t sample = sampleOf(canonicalBeat(wrap, index), samplesPerBeat);
        if (sample >= windowHi) {
            break;
        }
        const bool silent = !validPitch(e.pitch) || (!mSnapshot.loop && e.on && index >= mEndIndex);
        if (!silent && !pushLogical(Logical{sample, e.on ? ProgressionAttack : ProgressionRelease, PhaseTimeline,
                                            static_cast<std::uint8_t>(e.pitch),
                                            e.on ? clampVelocity(e.velocity) : std::uint8_t{0}, 0, 0},
                                    false)) {
            mDropped = true;  // carried over, not lost; still an incomplete block
            break;
        }
        ++index;
    }
    mCursorWrap = wrap;
    mCursorIndex = index;
    refreshNext();
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

void Scheduler::apply(State& state, const Logical& event, int offset, std::int64_t clock, Writer& writer) noexcept {
    switch (event.kind) {
        case ProgressionAttack:
            attack(state, kOwnerProgression, offset, event.pitch, event.velocity, writer);
            break;
        case ProgressionRelease:
            releaseProgression(state, offset, event.pitch, writer);
            break;
        case ProgressionReleaseAll:
            for (int pitch = 0; pitch < kPitchCount; ++pitch) {
                releaseProgression(state, offset, static_cast<std::uint8_t>(pitch), writer);
            }
            break;
        case PreviewExpiry: {
            const Preview& preview = state.previews[event.slot];
            if (preview.active && preview.expireAt <= clock + offset) {
                releasePreview(state, event.slot, offset, writer);
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
    // A rejected block is not the block the host asked for, and an `adopt` that abandoned overdue
    // occurrences has left a hole in the timeline: both make this block incomplete.
    mDropped = !timingValid || mBacklogAbandoned;
    mBacklogAbandoned = false;
    drainQueue();

    // Discontinuities, and the clock. Whether a carried-over timeline is pending is judged on the clock
    // as it was, before any re-anchoring, from the cursor's next occurrence as the last block left it.
    const bool backlog = mWasPlaying && mHasAnchor && mHasNext && sampleOf(mNextBeat, mSamplesPerBeat) < mBlockSample;
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
    } else if (continuous && samplesPerBeat != mSamplesPerBeat) {
        // Tempo or sample rate changed: re-anchor the clock where this block was expected to start. The
        // cursor is in beats, so what has been emitted stays emitted and what has not stays due.
        mAnchorPpq = expected;
        mSamplesPerBeat = samplesPerBeat;
        mBlockSample = 0;
    }
    const std::int64_t windowHi = mBlockSample + nFrames;

    // The block's events: mandatory releases first, so that nothing can displace them.
    if (flush) {
        pushLogical(Logical{mBlockSample, ProgressionReleaseAll, PhaseFlush, 0, 0, 0, 0}, true);
        if (backlog) {
            mDropped = true;  // the carried-over timeline is abandoned with the position
        }
    }
    pushExpiries(nFrames, previewSamples);
    if (playing) {
        const bool spansTooMuch = snapshotUsable() && mSnapshot.loop &&
                                  static_cast<double>(nFrames) / samplesPerBeat / mSnapshot.endBeat >
                                      static_cast<double>(kMaxWrapsPerBlock);
        bool overloaded = false;
        if (!flush && !spansTooMuch && backlog) {
            const double position = mAnchorPpq + static_cast<double>(mBlockSample) / samplesPerBeat;
            overloaded = position - mNextBeat > kMaxLagBeats;
        }
        if (spansTooMuch || overloaded) {
            if (!flush) {
                pushLogical(Logical{mBlockSample, ProgressionReleaseAll, PhaseFlush, 0, 0, 0, 0}, true);
            }
            mDropped = true;  // this block's timeline, or a stale backlog, is dropped
        }
        if (spansTooMuch) {
            seekCursor(windowHi, samplesPerBeat);  // skip the block
        } else {
            if (flush || overloaded) {
                seekCursor(mBlockSample, samplesPerBeat);
                chase(samplesPerBeat);
            }
            enumerate(windowHi, samplesPerBeat);
        }
    }

    // Order, then apply ownership twice: a dry run to count the note-offs, the real run with the rest of
    // the capacity for attacks. Owed note-offs and the preview commands come before the sorted events.
    // Within one sample, earlier phases first, then arrival order: the snapshot's own `(off before on,
    // pitch)` at one beat, one wrap's final offs before the next wrap's attacks, releases and chase by pitch.
    std::sort(mScratch.begin(), mScratch.begin() + static_cast<std::ptrdiff_t>(mScratchCount),
              [](const Logical& a, const Logical& b) noexcept {
                  if (a.sample != b.sample) {
                      return a.sample < b.sample;
                  }
                  if (a.phase != b.phase) {
                      return a.phase < b.phase;
                  }
                  return a.seq < b.seq;
              });
    const int lastOffset = nFrames > 0 ? nFrames - 1 : 0;
    auto offsetOf = [&](const Logical& event) noexcept -> int {
        return static_cast<int>(std::clamp<std::int64_t>(event.sample - mBlockSample, 0, lastOffset));
    };
    Writer dry(nullptr, 0, 0, true);
    {
        State shadow = mState;
        payOwed(shadow, dry);
        applyBatch(shadow, mBatch.data(), mBatchCount, mPreviewClock, previewSamples, dry);
        for (std::size_t i = 0; i < mScratchCount; ++i) {
            apply(shadow, mScratch[i], offsetOf(mScratch[i]), mPreviewClock, dry);
        }
    }
    Writer writer(out, capacity, capacity > dry.releases ? capacity - dry.releases : 0, false);
    payOwed(mState, writer);
    applyBatch(mState, mBatch.data(), mBatchCount, mPreviewClock, previewSamples, writer);
    for (std::size_t i = 0; i < mScratchCount; ++i) {
        apply(mState, mScratch[i], offsetOf(mScratch[i]), mPreviewClock, writer);
    }
    written = writer.written;

    // Advance.
    mPreviewClock += nFrames;
    if (playing) {
        mBlockSample += nFrames;
    } else {
        mHasAnchor = false;
        mHasCursor = false;
        mHasNext = false;
    }
    mWasPlaying = playing;
    mPendingFlush = false;
    return !mDropped && !writer.dropped;
}

}  // namespace chordlab
