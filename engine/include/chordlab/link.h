#pragma once

#include "chordlab/compiler.h"
#include "chordlab/document.h"
#include "chordlab/note_event.h"
#include "chordlab/publication.h"
#include "chordlab/tracks.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chordlab {

// Linking instances (per-track instruments plan I2, I5, I6): a leader hands each follower the compiled notes of
// one lane, and a follower plays them from a buffer of its own. This header holds the pure, single-threaded
// pieces: how lanes are named across saves, what a leader publishes, and how a follower's slot is filled from it.
// Standard library only: no platform code.

/// The key the chords lane has in every link.
inline constexpr std::string_view kChordsLaneUid = "chords";

/// The most entries of vanished tracks a `LaneUids` table keeps (`LaneUids::cover`).
constexpr std::size_t kMaxVanishedLaneUids = 64;

/// One instance's table of lane uids (per-track instruments plan I2): each track number of the document it has
/// published gets a UUID once, so that a link names a lane that a reverted or replaced document cannot imitate
/// by reusing the number. A track number is `Track::id`. No entry is ever `kChordsLaneUid` or empty, and no two
/// entries share a uid (`cover` takes fresh uids from its generator, `replace` asserts it of a loaded table).
class LaneUids {
public:
    /// A fresh, never-before-returned uid (a UUID in the shell); never empty, never `kChordsLaneUid`.
    using Generator = std::function<std::string()>;

    /// Gives every track of `doc` without an entry a uid from `make`, in document order. Entries of tracks that
    /// no longer exist are kept (undo brings a track back with its number, and it must keep its uid), except that
    /// at most `kMaxVanishedLaneUids` entries of vanished tracks stay: beyond that the lowest track numbers go
    /// first. The entry of a track of `doc` is never removed. `kChordsLaneUid` (or an empty string) is never
    /// given to a track: `make` returning one is a programming error (asserted); without asserts such a value is
    /// skipped and `make` asked again. Returns whether the table changed (an entry added or removed).
    bool cover(const Document& doc, const Generator& make);

    /// The uid of track `trackId`, or empty when it has none.
    std::string uidOf(int trackId) const;

    /// The lane `uid` names in `doc`: the chords lane for `kChordsLaneUid`; the track whose number has that uid
    /// when such a track exists in `doc`; nothing otherwise (an empty or unknown uid, or the uid of a track
    /// number `doc` does not hold).
    std::optional<LaneKey> laneOf(const Document& doc, std::string_view uid) const;

    /// The table as saved (track number -> uid).
    const std::map<int, std::string>& entries() const noexcept;

    /// Replaces the whole table by a loaded one: entries not in `entries` are forgotten, so a track number the
    /// loaded table lacks gets a new uid at the next `cover`, never the one it had before. The caller passes a
    /// validated table (Task 6's loader validates a loaded one): no value empty or `kChordsLaneUid`, the values
    /// pairwise distinct (asserted). Nothing is dropped or repaired here.
    void replace(std::map<int, std::string> entries);

private:
    std::map<int, std::string> entries_;
};

/// One lane as a leader publishes it.
struct LinkLane {
    std::string uid;          // kChordsLaneUid, or the track's lane uid
    bool chords = false;
    std::string name;         // "Chords" for the chords lane, else the track's name
    TrackRole role = TrackRole::Other;   // meaningless for the chords lane
    bool audible = false;     // chordsAudible / trackAudible
    bool compiled = false;    // events were compiled for it (it is followed)
    std::vector<NoteEvent> events;   // compilePlaybackFor(doc, Only{this lane}).events when `compiled`; empty otherwise
};

/// What a leader publishes for its followers: immutable once built.
struct LinkFeed {
    std::string documentId;
    std::string lineage;
    std::string label;
    double endBeat = 0.0;     // compile(doc).endBeat
    bool loop = true;         // doc.progressionLoop
    bool followHost = true;   // doc.followHost
    std::vector<LinkLane> lanes;   // the chords lane first, then the tracks in document order
};

/// Builds the feed of `doc`. `uids` must cover `doc` (every track has a uid; asserted). The lanes whose uid is in
/// `followed` are compiled (`compiled` true, their events possibly empty: an inaudible lane compiles to nothing);
/// the others carry no events. A uid in `followed` that names no lane of `doc` is ignored. Allocates: never on
/// the audio thread.
std::shared_ptr<const LinkFeed> buildLinkFeed(const Document& doc, std::string documentId, const LaneUids& uids,
                                              std::string lineage, std::string label,
                                              const std::vector<std::string>& followed);

/// Generations of a link buffer start above this, own-buffer generations (publication.h, counting up from 1) stay
/// below it: a scheduler that switches between the two buffers can never see one generation for two contents.
constexpr std::uint32_t kLinkGenerationBase = 0x80000000u;

/// Fills a follower's slot from its leader's feed, with `fillSlot`'s contract (publication.h): returns true, having
/// filled `slot` and recorded it in `last`, when anything the slot carries differs from `last` or nothing was
/// published yet; false, touching neither, otherwise. The content is the events of the lane `laneUid` when `feed`
/// is not null, `feed->documentId == documentId`, the lane is in the feed and was compiled; in every other case
/// the content is empty (count 0). `endBeat`, `loop` and `followHost` are the feed's (0, true, true without a
/// feed). `slot.midiThru` is always true and means nothing: a follower takes Thru from its own buffer. The
/// generation is `kLinkGenerationBase + n`, n counting up from 1 with every change of content (events, count,
/// endBeat, loop), not with a change of `followHost` alone. A lane with more events than a slot holds is
/// published as empty (and asserts), as `fillSlot` does. `last` must be fresh (generation 0) or have been
/// filled only by this function (asserted). "Same generation, same content" holds only while one `Publication`
/// lives exactly as long as its link buffer: a caller must never reset or recreate a link buffer's `Publication`,
/// or a later fill would restart at `kLinkGenerationBase + 1` and a scheduler could take new content for
/// unchanged. Reaching `UINT32_MAX` would take 2^31 - 1 content changes (asserted before every increment).
/// Allocates: never on the audio thread.
bool fillSlotFromFeed(const LinkFeed* feed, std::string_view laneUid, std::string_view documentId,
                      SnapshotSlot& slot, Publication& last);

}  // namespace chordlab
