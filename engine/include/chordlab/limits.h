#pragma once

#include <cstddef>

namespace chordlab {

/// The chord budget (tracks design D8): the most events the chords lane alone may compile to. document.h sizes
/// `kMaxChords` against it (a full document of eight-note block chords fits exactly), and `performDocument`'s
/// D11 budget compares against it, so a feel over it falls back to block chords. Tracks never count against it.
constexpr std::size_t kMaxChordEvents = 4096;

/// The most events one scheduler snapshot may hold (`Scheduler::kMaxSnapshotEvents` is this value); a larger
/// snapshot is adopted as empty. It is the chord budget plus every track note's on and off: `kMaxChordEvents +
/// kMaxTracks x kMaxNotesPerTrack x 2` = 4096 + 4 x 1024 x 2 = 12288, which bounds `compilePlayback` (the merge
/// only shortens, merges or drops notes). Written as a literal so that this header, which the real-time side
/// includes (scheduler.h), stays free of tracks.h; tracks.h asserts the sum. It lives here, apart from
/// scheduler.h, so that document.h can size the document without including the real-time headers.
constexpr std::size_t kMaxSnapshotEvents = 12288;

}  // namespace chordlab
