#pragma once

#include <cstddef>

namespace chordlab {

/// The most events one scheduler snapshot may hold (`Scheduler::kMaxSnapshotEvents` is this value); a
/// larger snapshot is adopted as empty. It lives here, apart from scheduler.h, so that document.h can
/// size the document against it (`kMaxChords`) without including the real-time headers.
constexpr std::size_t kMaxSnapshotEvents = 4096;

}  // namespace chordlab
