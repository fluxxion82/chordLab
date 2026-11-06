#pragma once

#include "chordlab/limits.h"
#include "chordlab/note_event.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace chordlab {

/// What the host tells us about one audio block. `ppqStart` is the host position in beats at the first
/// sample; `bpm` the tempo in force for the block; `playing` whether the transport runs; `thru` whether
/// incoming notes pass through this block (the document's flag, read from the slot acquired for this very
/// block; see `Scheduler::pushThru`).
struct BlockInfo {
    double ppqStart;
    int nFrames;
    double sampleRate;
    double bpm;
    bool playing;
    bool thru;
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
/// `(beat, off before on, pitch)` with every beat in `[0, endBeat]` and at most
/// `Scheduler::kMaxSnapshotEvents` of them, as `compile` produces. Lifetime: the array must stay valid
/// and unchanged from its `adopt` through every `process` call until the next `adopt`. That next `adopt`
/// never reads the previous array (everything it needs from the old timeline is kept in scheduler state
/// that `process` maintains), so the memory may be reused as soon as the next `adopt` is entered: the
/// plug-in's triple buffer relies on this, since `acquire` hands the old slot back to the producer before
/// `adopt` runs. `generation` changes whenever the content changes.
struct Snapshot {
    const NoteEvent* events;
    std::size_t count;
    double endBeat;
    bool loop;
    std::uint32_t generation;
};

/// A preview request from the UI: `Start` sounds `pitches[0..count)` at `velocity` until a `Stop` with
/// the same `id` arrives or four seconds of processed audio pass; `Stop` releases the preview with that
/// `id`; `Extend` gives the sounding preview with that `id` another four seconds of processed audio from
/// the block it is applied in and touches no note. A preview therefore ends four seconds of processed
/// audio after its Start or its last applied Extend. The UI sends an Extend every second while a cell is
/// held, so this ends a preview whose release never arrived, or a held one whose extends stopped coming
/// for that long (a main thread stalled for several seconds); while the host processes no audio nothing
/// expires. An `Extend` for an id that is not sounding does nothing, and one applied in a block with no
/// usable sample rate leaves the deadline as it is.
struct PreviewCmd {
    enum Kind : std::uint8_t { Start, Stop, Extend };
    Kind kind;
    int id;
    int velocity;
    std::uint8_t count;
    std::uint8_t pitches[8];
};

/// One incoming note the plug-in passes through (design D2): `on` with `velocity`, or a release,
/// `sampleOffset` samples into the block it arrived in. A `pitch` of 128 or more is ignored (`pushThru`
/// reports it as queued).
struct ThruEvent {
    bool on;
    std::uint8_t pitch;
    std::uint8_t velocity;
    int sampleOffset;
};

/// Owner bits of a pitch, as `Scheduler::ownersOf` reports them.
constexpr std::uint8_t kOwnerProgression = 1;
constexpr std::uint8_t kOwnerPreview = 2;
constexpr std::uint8_t kOwnerThru = 4;

/// Turns the host's transport position, a compiled progression, preview requests and incoming notes into
/// sample-accurate note-ons and note-offs, with one physical note per pitch no matter how many sources
/// hold it.
///
/// Ownership: a pitch has up to three owners, the progression, the previews and the thru notes (incoming
/// MIDI passed to the output). The first owner to acquire a pitch produces the physical note-on; the last
/// to release it produces the note-off. The progression or a thru note attacking a pitch that already
/// sounds retriggers it (off then on); a preview joining a sounding pitch is silent. Previews release
/// themselves four seconds after they start, or after their last applied `Extend`, counted in processed
/// samples. A thru note is released by its own note-off (a note-off with no thru note-on is ignored), by
/// a block with `BlockInfo::thru` false, by an overflow of its ring, by `reset` (on the next block), by a
/// bypass end (`releaseThru`, on the next block) or by `panic`; the progression's releases, flushes and
/// chases never touch it.
///
/// Position: progression beat 0 sits at host ppq 0. With `Snapshot::loop` the progression repeats every
/// `endBeat` beats; without it, it plays once and nothing sounds at or after `endBeat`. The progression
/// never sounds before host ppq 0 (previews are independent of the transport).
///
/// Timing: playback is locked to an integer sample clock anchored at the host position where the
/// transport started, sought or changed tempo. Every event sits at `round((beat - anchor) * samplesPerBeat)`
/// on that clock, so the output does not depend on how the host cuts its blocks, and host positions that
/// differ from the expected one by less than `kSeekThresholdBeats` are ignored. Beyond that threshold
/// the block is a seek: the progression's notes are released and the notes sounding at the new position
/// are chased (attacked at offset 0). Start, stop, `reset` and a changed `adopt` are discontinuities too.
///
/// Output order: events are written in time order. At one sample the phases run note-offs owed from
/// earlier blocks, preview commands in arrival order, the discontinuity's releases (by pitch, and the thru
/// release-all), the chase (by pitch), the thru notes in arrival order, then the timeline in the
/// snapshot's own order, which `compile` makes `(beat, off before on, pitch)`; the final offs of one wrap
/// precede the first attacks of the next. Timeline events that did not fit an earlier block are written
/// first, at offset 0, still in their order. Releases are never lost: one that does not fit the output is
/// owed and paid at the start of the next block before anything else, and a capacity of at least
/// `kCapacityForImmediateRelease` always pays every owed release in the block that owes it. Attacks that do
/// not fit are dropped and left unowned, and `process` then returns false.
///
/// Bounded work: a block of unusable timing (`nFrames < 0`, `sampleRate` or `bpm` not positive and finite,
/// a non-finite `ppqStart`, samples per beat outside `(0, kMaxSamplesPerBeat]`, or a preview timer that
/// does not fit) is treated as a stopped block (previews served, progression released, nothing played);
/// a block spanning more than `kMaxWrapsPerBlock` wraps of the progression is skipped the same way. A
/// timeline too dense for the scratch is carried over to the following blocks, late but complete and in
/// order, each block making progress; a backlog that falls more than `kMaxLagBeats` behind the transport
/// is dropped instead, with every progression note released and the current position chased. Every
/// block that carries over, drops or skips anything returns false. An empty block (`nFrames == 0`) does
/// nothing at all and changes nothing.
///
/// Real time: after construction nothing allocates, throws, locks or logs. `process`, `reset`, `adopt`,
/// `panic`, `pushThru`, `releaseThru`, `ownersOf` and `sounding` belong to one thread, the audio thread; `pushPreview`
/// may be called from one other thread. `reset` and `adopt` may also be called from another thread while the
/// host guarantees that no `process` runs concurrently (iPlug2's `OnReset` before processing starts, for
/// instance); without such a guarantee the plugin must marshal them to the audio callback.
class Scheduler {
public:
    static constexpr std::size_t kMaxEventsPerBlock = 512;
    /// Scratch entries kept back for the block's own releases (the discontinuity's release-all, the thru
    /// notes' release-all and the preview expiries), so that no number of attacks can displace them.
    static constexpr std::size_t kReservedReleaseEvents = 32;
    static constexpr std::size_t kPreviewQueueCapacity = 64;
    static constexpr std::size_t kMaxPreviews = 8;
    static constexpr double kPreviewSeconds = 4.0;
    /// A host position this far from the expected one counts as a seek.
    static constexpr double kSeekThresholdBeats = 1.0 / 1024.0;
    /// The most progression wraps one block may span and still be played (16 beats of a 0.25-beat
    /// progression, far beyond any real block); beyond it the block only releases.
    static constexpr std::size_t kMaxWrapsPerBlock = 64;
    /// The most events a snapshot may hold; a larger one is adopted as empty. The value is
    /// `chordlab::kMaxSnapshotEvents` of limits.h, which document.h sizes `kMaxChords` against.
    static constexpr std::size_t kMaxSnapshotEvents = ::chordlab::kMaxSnapshotEvents;
    /// An output capacity that always pays every owed release in one block.
    static constexpr std::size_t kCapacityForImmediateRelease = 128;
    /// The incoming notes `pushThru` can hold between two blocks.
    static constexpr std::size_t kThruQueueCapacity = 256;
    /// The most queued incoming notes one block turns into events; the rest wait for the next block.
    static constexpr std::size_t kMaxThruPerBlock = 64;
    /// The largest sane sample-rate-to-tempo ratio; beyond it the block's timing is unusable.
    static constexpr double kMaxSamplesPerBeat = 1e9;
    /// How far a carried-over timeline may fall behind the transport before it is dropped.
    static constexpr double kMaxLagBeats = 1.0;

    Scheduler() noexcept;

    /// Records the sample rate the preview timers use and marks a pending flush: the next `process`
    /// releases every progression note and chases the current position. Ownership state is kept, so the
    /// notes that sound are released properly; previews are untouched, and so are the queued preview
    /// commands, which the next block serves as usual (only `panic` discards them). Thru notes are released
    /// too, by a release-all on the next block whatever its `BlockInfo::thru` (a host that deactivates the
    /// plug-in while a key is held never delivers that key's note-off), and the incoming notes queued
    /// before the call are discarded. `sampleRate` must be positive and finite (an unusable value is
    /// ignored).
    void reset(double sampleRate) noexcept;

    /// Replaces the progression. A changed `generation` marks a pending flush (release all, then chase on
    /// the next block), and if that abandons occurrences of the old timeline that were overdue, the next
    /// block reports it (returns false); that verdict comes from state `process` keeps, never from the
    /// previous array (see `Snapshot`). The same generation means the same content: playback goes on
    /// exactly as if `adopt` had not been called. A snapshot with more than `kMaxSnapshotEvents` events is
    /// adopted as an empty one (and asserts in a debug build).
    void adopt(const Snapshot& snapshot) noexcept;

    /// Queues a preview command for the next block. Returns false, and queues nothing, when the queue is
    /// full. `count` must be at most 8 and every pitch below 128. An `Extend` whose id matches the last
    /// command still queued, itself an `Extend`, is merged into it and reported as queued, so a host that
    /// is not processing cannot fill the ring with them.
    bool pushPreview(const PreviewCmd& cmd) noexcept;

    /// Queues an incoming note for the block about to be processed. Audio thread only, the same thread as
    /// `process`, so the ring needs no atomics. The event is stamped with the preview clock (which runs on
    /// processed samples whether or not the transport does) plus its offset, so one that waits for a later
    /// block keeps its place in time. Returns false, and queues nothing, when the ring is full; the next
    /// `process` then releases every thru-owned pitch, discards the ring and reports the block incomplete.
    /// An event with a pitch of 128 or more is ignored (and reported as queued).
    ///
    /// The next `process` applies the event only if its `BlockInfo::thru` is true: it takes up to
    /// `kMaxThruPerBlock` events in arrival order, each at its offset on the preview clock (an overdue one,
    /// or one beyond the block, at the nearest offset inside it), and leaves the rest for the following
    /// blocks, reporting each block that leaves some behind as incomplete. A block with `thru` false
    /// discards everything queued, and the first such block after one with `thru` true releases every thru
    /// note.
    bool pushThru(const ThruEvent& event) noexcept;

    /// Releases every thru note on the next block with `nFrames > 0` (an empty block leaves the release
    /// pending), by a release-all whatever that block's `BlockInfo::thru`. Audio thread only, the same
    /// thread as `process`. The shell calls it when a host bypass ends, so that a note-off swallowed while
    /// bypassed cannot leave a thru note sounding. Unlike `reset`, it keeps the queued incoming notes: with
    /// `thru` on, that block applies them after the release-all as usual, so an attack on a released pitch
    /// at the same sample comes out as its off then its on.
    void releaseThru() noexcept;

    /// Produces this block's events into `out[0..capacity)`, setting `written`. Returns false if the block
    /// could not emit everything it was due to: an attack was dropped (output capacity, scratch space or the
    /// preview table ran out), a release had to be owed to a later block, part of the timeline was carried
    /// over, dropped or skipped, an `adopt` abandoned overdue occurrences, queued incoming notes were left
    /// for a later block or lost to a full ring, or the block's timing was unusable. Only an empty block
    /// (`nFrames == 0`) is a true no-op. `out` may be null only when `capacity` is 0.
    bool process(const BlockInfo& info, MidiOut* out, std::size_t capacity, std::size_t& written) noexcept;

    /// Silences everything: every sounding pitch, progression, preview and thru note alike, is released
    /// (its note-off is owed and paid by the next block), every preview is forgotten and every queued
    /// incoming note is discarded (`pushThru` and `panic` share a thread, so the cut is exact). The
    /// progression is not chased afterwards: it stays silent until its next attack.
    /// The preview commands queued before the call are discarded too, at the next block's drain: a start
    /// that has not sounded yet never will, and a stop has nothing left to stop. Commands pushed after
    /// `panic` returns are served as usual. The cut is the queue's tail as `panic` saw it (recorded, not
    /// drained, so the pushing thread needs no lock); the last of several panics before one block wins.
    void panic() noexcept;

    /// The owners currently holding `pitch` (a combination of `kOwnerProgression`, `kOwnerPreview` and
    /// `kOwnerThru`).
    /// `pitch` must be in 0..127.
    std::uint8_t ownersOf(int pitch) const noexcept;

    /// Whether `pitch` physically sounds: it has an owner, or its note-off is owed. `pitch` must be in
    /// 0..127.
    bool sounding(int pitch) const noexcept;

private:
    enum Kind : std::uint8_t {
        ProgressionAttack,
        ProgressionRelease,
        ProgressionReleaseAll,
        PreviewExpiry,
        ThruAttack,
        ThruRelease,
        ThruReleaseAll,
    };

    /// Where in the block an event came from; earlier phases write first at equal offsets.
    enum Phase : std::uint8_t { PhaseFlush, PhaseChase, PhaseThru, PhaseTimeline };

    /// One owner-level event of the block, before ownership turns it into physical MIDI (or into nothing).
    /// `sample` is on the clock (`mBlockSample` is this block's first sample); a timeline event carried
    /// over from an earlier block sits before it and is written at offset 0.
    struct Logical {
        std::int64_t sample;
        Kind kind;
        Phase phase;
        std::uint8_t pitch;
        std::uint8_t velocity;
        std::uint8_t slot;  // PreviewExpiry: the preview slot
        std::uint16_t seq;  // insertion order, the final tie-break
    };

    /// One queued incoming note: `due` is its sample on the preview clock.
    struct ThruQueued {
        std::int64_t due;
        bool on;
        std::uint8_t pitch;
        std::uint8_t velocity;
    };

    /// One running preview.
    struct Preview {
        bool active;
        int id;
        std::int64_t expireAt;  // on the preview clock
        std::uint8_t count;
        std::uint8_t held;  // bit i set while pitches[i] is acquired by this preview
        std::uint8_t velocity;
        std::uint8_t pitches[8];
    };

    /// The ownership state: everything a block mutates while applying its events. Copied once per block
    /// for the dry run that counts releases.
    struct State {
        std::array<std::uint8_t, 128> owners;
        std::array<bool, 128> owed;  // physically sounding with no owner: a note-off is due
        std::array<Preview, kMaxPreviews> previews;
    };

    struct Writer;

    void drainQueue() noexcept;
    bool pushLogical(const Logical& event, bool mandatory) noexcept;
    void pushExpiries(int nFrames, std::int64_t previewSamples) noexcept;
    void collectThru(bool thru, int lastOffset) noexcept;
    void chase(double samplesPerBeat) noexcept;
    void seekCursor(std::int64_t targetSample, double samplesPerBeat) noexcept;
    void placeCursor(std::int64_t targetSample, double samplesPerBeat) noexcept;
    bool cursorNext(std::int64_t& wrap, std::size_t& index) const noexcept;
    void refreshNext() noexcept;
    void enumerate(std::int64_t windowHi, double samplesPerBeat) noexcept;
    bool snapshotUsable() const noexcept;
    double canonicalBeat(std::int64_t wrap, std::size_t index) const noexcept;
    std::int64_t sampleOf(double beat, double samplesPerBeat) const noexcept;

    static bool attack(State& state, std::uint8_t owner, int offset, std::uint8_t pitch, std::uint8_t velocity,
                       Writer& writer) noexcept;
    static void physicalOff(State& state, int offset, std::uint8_t pitch, Writer& writer) noexcept;
    static void releaseProgression(State& state, int offset, std::uint8_t pitch, Writer& writer) noexcept;
    static void releaseThru(State& state, int offset, std::uint8_t pitch, Writer& writer) noexcept;
    static void releasePreview(State& state, std::size_t slot, int offset, Writer& writer) noexcept;
    static bool previewHeldElsewhere(const State& state, std::size_t slot, std::uint8_t pitch) noexcept;
    static void payOwed(State& state, Writer& writer) noexcept;
    static void applyBatch(State& state, const PreviewCmd* batch, std::size_t count, std::int64_t clock,
                           std::int64_t previewSamples, Writer& writer) noexcept;
    static void apply(State& state, const Logical& event, int offset, std::int64_t clock, Writer& writer) noexcept;

    State mState;
    Snapshot mSnapshot;
    std::size_t mEndIndex;  // first snapshot event at beat >= endBeat (the final offs)
    bool mHasSnapshot;
    double mSampleRate;
    bool mWasPlaying;
    bool mPendingFlush;

    // The sample clock the progression is played on while the transport runs.
    bool mHasAnchor;
    double mAnchorPpq;           // host beat at clock sample 0
    double mSamplesPerBeat;      // the tempo the clock was anchored with
    std::int64_t mBlockSample;   // clock sample at which the next block starts
    std::int64_t mPreviewClock;  // runs whether or not the transport does

    // How far the timeline has been consumed: the next snapshot occurrence to emit, as (wrap, index) in
    // playback order. Independent of tempo, so re-anchoring the clock can neither lose nor repeat an event.
    bool mHasCursor;
    std::int64_t mCursorWrap;
    std::size_t mCursorIndex;
    // The cursor's next unconsumed occurrence, as `cursorNext` normalises it, resolved to its canonical
    // beat whenever the cursor moves (`refreshNext`). The backlog checks in `process` and `adopt` read
    // this rather than the array, so `adopt` never touches the array it is replacing.
    bool mHasNext;
    double mNextBeat;
    bool mBacklogAbandoned;  // an `adopt` dropped overdue occurrences: the next block reports it

    // Per-block scratch.
    std::array<Logical, kMaxEventsPerBlock> mScratch;
    std::size_t mScratchCount;
    bool mDropped;
    std::array<PreviewCmd, kPreviewQueueCapacity> mBatch;
    std::size_t mBatchCount;

    std::array<PreviewCmd, kPreviewQueueCapacity> mQueue;
    std::atomic<std::uint32_t> mQueueHead;
    std::atomic<std::uint32_t> mQueueTail;
    bool mCancelQueued;           // a panic has cut the queue: the next drain discards up to mCancelTail
    std::uint32_t mCancelTail;    // the queue's tail index as that panic saw it

    // Incoming notes, pushed and consumed on the audio thread (no atomics): a FIFO of mThruCount entries
    // from mThruHead.
    std::array<ThruQueued, kThruQueueCapacity> mThru;
    std::size_t mThruHead;
    std::size_t mThruCount;
    bool mThruOverflow;  // an event was refused since the last block
    bool mThruWas;       // the last block's BlockInfo::thru; thru owners exist only while it is true
    bool mThruReleasePending;  // a `reset` or `releaseThru` since the last non-empty block: the next one
                               // releases every thru note
};

}  // namespace chordlab
