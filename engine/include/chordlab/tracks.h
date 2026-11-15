#pragma once

#include "chordlab/meter.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace chordlab {

struct Document;

/// What a track is for (tracks design D2). A role sets defaults only (the default name, and in the UI the
/// register, colour and guide emphasis), never what the user may write; it is editable (`patchTrack`).
enum class TrackRole { Bass, Lead, Other };

/// One note of a track (D1). Its position is derived, not stored (D3): the note belongs to the chord it starts
/// in, `anchorEventId`, and starts `offsetTick` ticks (960 per beat) after that chord's canonical boundary
/// (`chordBoundaryTicks`), so it follows the chord when the strip changes. `id` is unique across all tracks of
/// a `DocumentStore` and never reused. Domain: `pitch` 0..127, `velocity` 1..127, `offsetTick` >= 0, the
/// derived start in [0, kMaxNoteTick - kMinTrackNoteTicks], `lengthTicks` in [kMinTrackNoteTicks,
/// kMaxNoteTick - start]. Named `TrackNote`, not `Note`: feel.cpp has a file-local `Note`.
struct TrackNote {
    int id;
    int pitch;
    int anchorEventId;
    std::int32_t offsetTick;
    std::int32_t lengthTicks;
    int velocity;
};

constexpr bool operator==(const TrackNote& a, const TrackNote& b) noexcept {
    return a.id == b.id && a.pitch == b.pitch && a.anchorEventId == b.anchorEventId && a.offsetTick == b.offsetTick &&
           a.lengthTicks == b.lengthTicks && a.velocity == b.velocity;
}

constexpr bool operator!=(const TrackNote& a, const TrackNote& b) noexcept {
    return !(a == b);
}

/// A lane of notes over the progression (D1). `id` is in 1..INT_MAX-1, unique within a `DocumentStore` and
/// never reused. `name` is any UTF-8 string of at most `kMaxTrackNameBytes` bytes (it may be empty, see
/// `DocumentStore::patchTrack`). `notes` are sorted by (derived start, pitch, id) after every store edit.
/// `mute` and `solo` are live state (set by later tasks' setters), the rest is undoable content.
struct Track {
    int id;
    std::string name;
    TrackRole role;
    bool mute = false;
    bool solo = false;
    std::vector<TrackNote> notes;
};

inline bool operator==(const Track& a, const Track& b) noexcept {
    return a.id == b.id && a.name == b.name && a.role == b.role && a.mute == b.mute && a.solo == b.solo &&
           a.notes == b.notes;
}

inline bool operator!=(const Track& a, const Track& b) noexcept {
    return !(a == b);
}

/// The most tracks a document holds: `DocumentStore::addTrack` refuses the next one.
constexpr int kMaxTracks = 4;

/// The most notes one track holds, inactive ones included: `DocumentStore::insertNotes` refuses past it.
constexpr int kMaxNotesPerTrack = 1024;

/// The shortest track note, in ticks: a 1/32 beat. Unrelated to the feel's `kMinNoteTicks` (5).
constexpr std::int32_t kMinTrackNoteTicks = 30;

/// The end of the note domain, in ticks: kMaxChords x kMaxLengthBeats x ticks per beat (document.cpp checks
/// the product), so the longest progression ends exactly here.
constexpr std::int32_t kMaxNoteTick = 256 * 64 * kFeelTicksPerBeat;

/// The longest track name, in UTF-8 bytes, as `kMaxSectionNameBytes`: the store cuts a longer one down at a
/// character boundary.
constexpr int kMaxTrackNameBytes = 64;

/// The id `DocumentStore::addTrack` returns when it added nothing. No track ever has this id.
constexpr int kNoTrackId = -1;

/// The canonical chord boundaries in ticks (D3): size `doc.events.size() + 1` ({0} for an empty progression),
/// entry i the start of chord i and the last entry the end. The cumulative authored beats, accumulated exactly
/// as `Document::startBeatOf` does, each rounded once (`lround(beat * 960)`), so adjacent chords share their
/// boundary: three 0.2506-beat chords give {0, 241, 481, 722}. `performDocument`'s `chordTicks` is this.
std::vector<std::int32_t> chordBoundaryTicks(const Document& doc);

/// The chord boundaries and an id -> index lookup, computed once so that resolving or sorting many notes is not
/// quadratic. A copy of the document's timing: rebuild it after the document changes.
struct ChordTimeline {
    std::vector<std::int32_t> boundaries;     ///< `chordBoundaryTicks(doc)`
    std::vector<int> eventIds;                ///< the id of the chord at each index
    std::unordered_map<int, int> indexById;   ///< the inverse of `eventIds`

    /// Position of the chord with `eventId`, or -1 when there is none.
    int indexOf(int eventId) const;
};

ChordTimeline chordTimeline(const Document& doc);

/// Where a tick falls: the owning chord (its index and id) and the offset into it.
struct NoteAnchor {
    int eventIndex;
    int eventId;
    std::int32_t offsetTick;
};

/// The chord that owns `tick` (D3), half-open: chord i owns [boundaries[i], boundaries[i + 1]), so a tick
/// exactly on a boundary belongs to the following chord. nullopt before 0, at or after the end, and always on
/// an empty progression.
std::optional<NoteAnchor> resolveStart(const ChordTimeline& timeline, std::int64_t tick);
std::optional<NoteAnchor> resolveStart(const Document& doc, std::int64_t tick);

/// A note's derived start: its anchor's boundary plus its offset (64-bit, so an out-of-domain offset read from
/// outside cannot overflow). The anchor must be a chord of the document (asserted).
std::int64_t noteStart(const ChordTimeline& timeline, const TrackNote& note);
std::int64_t noteStart(const Document& doc, const TrackNote& note);

/// Whether a note is active (D3): its offset lies inside its chord, `offsetTick < boundaries[i + 1] -
/// boundaries[i]`. An inactive note (its chord got shorter) is kept but neither played nor exported. The anchor
/// must be a chord of the document (asserted).
bool noteActive(const ChordTimeline& timeline, const TrackNote& note);
bool noteActive(const Document& doc, const TrackNote& note);

/// Sorts `notes` by (derived start, pitch, id), the order every track keeps (D4); each start is computed once.
/// Every anchor must be a chord of `timeline` (asserted).
void sortTrackNotes(std::vector<TrackNote>& notes, const ChordTimeline& timeline);

/// The clamps of D1, applied in this order by the store: pitch and velocity on their own, the start into
/// [0, kMaxNoteTick - kMinTrackNoteTicks] first, then the length into [kMinTrackNoteTicks, kMaxNoteTick -
/// start] against the space the clamped start leaves. 64-bit inputs, so any value clamps without overflow.
int clampNotePitch(int pitch);
int clampNoteVelocity(int velocity);
std::int32_t clampNoteStart(std::int64_t tick);
/// `start` must already be clamped (`clampNoteStart`).
std::int32_t clampNoteLength(std::int64_t length, std::int32_t start);

/// The name `DocumentStore::addTrack` gives a track added without one (D2): "Bass", "Lead", or "Track" for
/// `Other`; when a track of `doc` already has it, " 2", " 3", ... is appended, the first that no track has (as
/// `defaultSectionName` steps over taken names). Only names count, not roles.
std::string defaultTrackName(const Document& doc, TrackRole role);

}  // namespace chordlab
