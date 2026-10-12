#pragma once

#include "chordlab/compiler.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace chordlab {

/// What the host tells us about one audio block. `ppqStart` is the host position in beats at the first
/// sample; `bpm` the tempo in force for the block; `playing` whether the transport runs.
struct BlockInfo {
    double ppqStart;
    int nFrames;
    double sampleRate;
    double bpm;
    bool playing;
};

/// One output MIDI event: a note-on (`0x90`, velocity in `data2`) or a note-off (`0x80`, `data2` 0),
/// always on channel 0, `sampleOffset` samples into the block.
struct MidiOut {
    int sampleOffset;
    std::uint8_t status;
    std::uint8_t data1;
    std::uint8_t data2;
};

/// The compiled progression as the scheduler plays it. `events` must be sorted by
/// `(beat, off before on, pitch)` with every beat in `[0, endBeat]`, as `compile` produces; the array must
/// stay valid and unchanged for as long as the scheduler may read it (until a later `adopt` replaces it
/// and `process` has run once more). `generation` changes whenever the content changes.
struct Snapshot {
    const NoteEvent* events;
    std::size_t count;
    double endBeat;
    bool loop;
    std::uint32_t generation;
};

/// A preview request from the UI: `Start` sounds `pitches[0..count)` at `velocity` until a `Stop` with
/// the same `id` arrives or four seconds pass; `Stop` releases the preview with that `id`.
struct PreviewCmd {
    enum Kind : std::uint8_t { Start, Stop };
    Kind kind;
    int id;
    int velocity;
    std::uint8_t count;
    std::uint8_t pitches[8];
};

/// Owner bits of a pitch, as `Scheduler::ownersOf` reports them.
constexpr std::uint8_t kOwnerProgression = 1;
constexpr std::uint8_t kOwnerPreview = 2;

/// Turns the host's transport position, a compiled progression and preview requests into sample-accurate
/// note-ons and note-offs, with one physical note per pitch no matter how many sources hold it.
///
/// Ownership: a pitch has up to two owners, the progression and the previews. The first owner to acquire
/// a pitch produces the physical note-on; the last to release it produces the note-off. The progression
/// attacking a pitch that already sounds retriggers it (off then on); a preview joining a sounding pitch
/// is silent. Previews release themselves four seconds after they start, counted in samples.
///
/// Position: progression beat 0 sits at host ppq 0. With `Snapshot::loop` the progression repeats every
/// `endBeat` beats; without it, it plays once and nothing sounds at or after `endBeat`. Nothing ever
/// sounds before host ppq 0.
///
/// Each block: preview commands are drained, discontinuities (start, stop, seek, a pending flush from
/// `reset` or `adopt`) release the progression's notes and chase the notes sounding at the new position,
/// the block's own events are enumerated across every wrap it spans, and everything is written in
/// `(sampleOffset, off before on, pitch)` order. Releases are never dropped for lack of output capacity;
/// attacks that do not fit are dropped and left unowned, and `process` then returns false.
///
/// Bounded work: a block with unusable timing (`nFrames < 0`, `sampleRate` or `bpm` not positive, a
/// non-finite `ppqStart`) is treated as a stopped block (previews served, progression released, nothing
/// played), and a block that would span more than `kMaxWrapsPerBlock` wraps of the progression is skipped
/// the same way, so no host value and no snapshot can make a block loop unboundedly.
///
/// Real time: after construction nothing allocates, throws, locks or logs. `process`, `reset` and `adopt`
/// belong to one thread (the audio thread); `pushPreview` may be called from one other thread.
class Scheduler {
public:
    static constexpr std::size_t kMaxEventsPerBlock = 512;
    static constexpr std::size_t kPreviewQueueCapacity = 64;
    static constexpr std::size_t kMaxPreviews = 8;
    static constexpr double kPreviewSeconds = 4.0;
    /// A host position this far from the one we expected counts as a seek.
    static constexpr double kSeekThresholdBeats = 1.0 / 1024.0;
    /// The most progression wraps one block may span and still be played (16 beats of a 0.25-beat
    /// progression, far beyond any real block); beyond it the block only releases.
    static constexpr std::size_t kMaxWrapsPerBlock = 64;

    Scheduler() noexcept;

    /// Records the sample rate the preview timers use and marks a pending flush: the next `process`
    /// releases every progression note and chases the current position. Ownership state is kept, so the
    /// notes that sound are released properly.
    void reset(double sampleRate) noexcept;

    /// Replaces the progression. A changed `generation` marks a pending flush (release all, then chase on
    /// the next block); the same generation is adopted silently.
    void adopt(const Snapshot& snapshot) noexcept;

    /// Queues a preview command for the next block. Returns false, and queues nothing, when the queue is
    /// full. `count` must be at most 8 and every pitch below 128.
    bool pushPreview(const PreviewCmd& cmd) noexcept;

    /// Produces this block's events into `out[0..capacity)`, setting `written`. Returns false if any
    /// event was dropped (capacity, scratch space or the preview table ran out). `nFrames` must be at
    /// least 0, `sampleRate` and `bpm` positive and `ppqStart` finite; `out` may be null only when
    /// `capacity` is 0.
    bool process(const BlockInfo& info, MidiOut* out, std::size_t capacity, std::size_t& written) noexcept;

    /// The owners currently holding `pitch` (a combination of `kOwnerProgression` and `kOwnerPreview`);
    /// non-zero exactly when the pitch physically sounds. `pitch` must be in 0..127.
    std::uint8_t ownersOf(int pitch) const noexcept;

private:
    enum Kind : std::uint8_t {
        ProgressionAttack,
        ProgressionRelease,
        ProgressionReleaseAll,
        PreviewAttack,
        PreviewRelease,
    };

    /// Where in the block's processing an event came from; earlier phases write first at equal offsets.
    enum Phase : std::uint8_t { PhasePreview, PhaseFlush, PhaseChase, PhaseTimeline };

    /// One owner-level event of the block, before ownership turns it into physical MIDI (or into nothing).
    struct Logical {
        int offset;
        Kind kind;
        Phase phase;
        std::uint8_t pitch;
        std::uint8_t velocity;
        std::uint8_t slot;   // PreviewAttack / PreviewRelease: the preview slot
        std::uint8_t index;  // PreviewAttack: the pitch's index within the slot
        std::uint16_t seq;   // insertion order, the final tie-break
    };

    /// One running preview.
    struct Preview {
        bool active;
        int id;
        long remainingSamples;
        std::uint8_t count;
        std::uint8_t held;  // bit i set while pitches[i] is acquired by this preview
        std::uint8_t velocity;
        std::uint8_t pitches[8];
    };

    /// The ownership state: everything a block mutates while applying its events. Copied once per block
    /// for the dry run that counts releases.
    struct State {
        std::array<std::uint8_t, 128> owners;
        std::array<Preview, kMaxPreviews> previews;
    };

    struct Writer;

    bool pushLogical(const Logical& event) noexcept;
    void drainPreviews(double sampleRate) noexcept;
    void expirePreviews(int nFrames) noexcept;
    void chase(double ppq) noexcept;
    double enumerate(double lo, double start, double end, int nFrames, double samplesPerBeat) noexcept;
    bool snapshotUsable() const noexcept;
    static bool previewHeldElsewhere(const State& state, std::size_t slot, std::uint8_t pitch) noexcept;
    static void apply(State& state, const Logical& event, Writer& writer) noexcept;
    static void releaseProgression(State& state, int offset, std::uint8_t pitch, Writer& writer) noexcept;
    static void releasePreview(State& state, std::size_t slot, int offset, Writer& writer) noexcept;

    State mState;
    Snapshot mSnapshot;
    double mSampleRate;
    bool mWasPlaying;
    bool mPendingFlush;
    bool mHasSnapshot;
    bool mHasFrontier;
    double mFrontier;  // the beat up to which the progression has been enumerated (the previous ppqEnd)

    std::array<Logical, kMaxEventsPerBlock> mScratch;
    std::size_t mScratchCount;
    bool mDropped;

    std::array<PreviewCmd, kPreviewQueueCapacity> mQueue;
    std::atomic<std::uint32_t> mQueueHead;
    std::atomic<std::uint32_t> mQueueTail;
};

}  // namespace chordlab
