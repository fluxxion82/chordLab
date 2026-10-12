#pragma once

namespace chordlab {

/// One note-on or note-off of the compiled progression, in beats from the start of the progression.
/// `velocity` is the chord's velocity for an on and 0 for an off. Plain data with no constructors so
/// that the scheduler can read an array of these from the audio thread. This header holds nothing else:
/// it is the one type the real-time side (scheduler.h) shares with the compiler, so the scheduler can
/// include it without pulling the document, the chord dictionary or anything that allocates into the
/// audio-thread translation units.
struct NoteEvent {
    double beat;
    bool on;
    int pitch;
    int velocity;
};

constexpr bool operator==(const NoteEvent& a, const NoteEvent& b) noexcept {
    return a.beat == b.beat && a.on == b.on && a.pitch == b.pitch && a.velocity == b.velocity;
}

constexpr bool operator!=(const NoteEvent& a, const NoteEvent& b) noexcept {
    return !(a == b);
}

}  // namespace chordlab
