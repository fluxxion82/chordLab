#pragma once

#include "chordlab/limits.h"
#include "chordlab/meter.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace chordlab {

struct Document;

/// What a track is for (tracks design D2). A role sets defaults only (the default name, and in the UI the
/// register, colour and guide emphasis), never what the user may write; it is editable (`patchTrack`).
enum class TrackRole { Bass, Lead, Other };

/// The wire and message name of a role, lowercase: "bass", "lead" or "other". The one table of role names:
/// `addTrack`, `patchTrack`, a clipboard lane's `role`, refusal messages and the snapshot all use it.
const char* trackRoleName(TrackRole role) noexcept;

/// The role named `name` exactly as `trackRoleName` spells it, or empty (case matters, no trimming).
std::optional<TrackRole> trackRoleFromName(std::string_view name) noexcept;

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
/// `mute` and `solo` are live state (`DocumentStore::setTrackMute` / `setTrackSolo`: no history, kept by undo and
/// redo by track id), the rest is undoable content.
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

// A snapshot slot holds the chord budget plus an on and an off for every note of every track (limits.h).
static_assert(kMaxSnapshotEvents == kMaxChordEvents + static_cast<std::size_t>(kMaxTracks) *
                                                          static_cast<std::size_t>(kMaxNotesPerTrack) * 2,
              "kMaxSnapshotEvents is the chord budget plus every track note's on and off");

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

/// The most track notes that may sound at once, over the active notes of all tracks, muted ones included (D10):
/// authored intervals [start, start + length), so a note ending where another starts does not overlap it.
/// `validateTracks` enforces it. Provisional: Task 8's capacity proof fixes the value.
constexpr int kMaxTrackPolyphony = 8;

/// The most track-note starts any 960-tick window [t, t + 960) may hold, over the active notes of all tracks,
/// muted ones included (D10). `validateTracks` enforces it. Provisional: Task 8's capacity proof fixes the value.
constexpr int kMaxTrackOnsetsPerBeat = 24;

/// The note selection (D5): live state of `DocumentStore`, one track at a time. `ids` are notes of track
/// `trackId`, in that track's note order, without repeats; empty exactly when `trackId` is `kNoTrackId`.
struct NoteSelection {
    int trackId = kNoTrackId;
    std::vector<int> ids;
};

inline bool operator==(const NoteSelection& a, const NoteSelection& b) noexcept {
    return a.trackId == b.trackId && a.ids == b.ids;
}

inline bool operator!=(const NoteSelection& a, const NoteSelection& b) noexcept {
    return !(a == b);
}

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

/// What `validateTracks` found wrong (D17a); see its `fault` parameter.
enum class TrackFault { None, Structure, Domain, Polyphony, Onsets };

/// The track invariant (D7), checked on a whole document with the id counters that go with it (`nextTrackId`,
/// `nextNoteId`: the candidate counters an edit will publish, or a loaded state's). True when all of these hold,
/// else false with a short reason in `error` (cleared on entry, so empty exactly when true), the first failure
/// in this order:
/// - both counters >= 1; at most `kMaxTracks` tracks; track ids >= 1, unique and below `nextTrackId`; roles
///   declared `TrackRole`s; names of at most `kMaxTrackNameBytes` bytes; at most `kMaxNotesPerTrack` notes per
///   track, inactive ones included;
/// - note by note, track by track: the id >= 1 and below `nextNoteId`; the anchor an existing chord (checked
///   before any start is derived); D1's domain: pitch 0..127, velocity 1..127, offset >= 0, derived start in
///   [0, kMaxNoteTick - kMinTrackNoteTicks], length in [kMinTrackNoteTicks, kMaxNoteTick - start]; each track's
///   notes sorted by (derived start, pitch, id);
/// - note ids unique across all tracks (the smallest repeated id is reported);
/// - D10's limits over the active notes of all tracks, muted ones included: `trackPolyphony` at most
///   `kMaxTrackPolyphony` ("At most 8 track notes can sound at once") and `densestOnsetWindow` at most
///   `kMaxTrackOnsetsPerBeat` ("At most 24 notes can start within one beat across all tracks"), the numbers
///   being the constants'.
/// The chords must satisfy the store's chord invariants (lengths in [kMinLengthBeats, kMaxLengthBeats], at most
/// `kMaxChords`, distinct ids); the tracks may be anything (dangling anchors, extreme offsets and lengths): the
/// tick arithmetic is 64-bit, so a malformed track is reported, never undefined. Every track and note edit and
/// every chord timing edit of `DocumentStore` runs it on its candidate, and a state loader runs it before
/// `DocumentStore::replace`.
/// O(n log n) in the number of notes.
///
/// `fault`, when given, classifies the failure so a caller can word it for its own edit (D17a): `None` on success;
/// `Domain` for a derived start past `kMaxNoteTick - kMinTrackNoteTicks` or a length past `kMaxNoteTick - start` (a
/// length below `kMinTrackNoteTicks` is `Structure`);
/// `Polyphony` and `Onsets` for the two D10 limits; `Structure` for everything else. `error` is the validator's
/// precise text whatever the class.
bool validateTracks(const Document& doc, int nextTrackId, int nextNoteId, std::string& error,
                    TrackFault* fault = nullptr);

/// The most active track notes of all tracks (muted ones included) sounding at one tick: authored intervals
/// [start, start + length), so touching notes (one ends where the next starts) do not overlap. Notes whose anchor
/// is not a chord of `doc` are skipped. O(n log n).
int trackPolyphony(const Document& doc);

/// The most active track-note starts of all tracks (muted ones included) within any 960-tick window [t, t + 960);
/// coincident starts count separately. Notes whose anchor is not a chord of `doc` are skipped. O(n log n).
int densestOnsetWindow(const Document& doc);

/// Whether a lane sounds (D8), the one rule for the chords lane and every track alike: with no solo anywhere
/// (`anySolo` false: neither the chords nor any track), every unmuted lane; with any solo, only the soloed lanes,
/// mute being ignored on a soloed lane.
constexpr bool laneAudible(bool muted, bool soloed, bool anySolo) noexcept {
    return anySolo ? soloed : !muted;
}

/// Whether the chords lane or any track is soloed: the `anySolo` of `laneAudible`.
bool anyLaneSoloed(const Document& doc) noexcept;

/// `laneAudible` for the chords lane (`chordsMuted`, `chordsSolo`).
bool chordsAudible(const Document& doc) noexcept;

/// `laneAudible` for the track at `trackIndex`, which must be a position of `doc.tracks` (asserted).
bool trackAudible(const Document& doc, std::size_t trackIndex) noexcept;

}  // namespace chordlab
