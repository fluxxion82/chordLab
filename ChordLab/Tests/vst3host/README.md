# vst3host

A command-line VST3 host that loads a built `ChordLab.vst3` into its own process and runs the shell's runtime scenarios: life cycle, state, playback of one instance, two instances linked through their states, a copy, and a state load while playing. No DAW, window, audio or MIDI device; it writes nothing.

Every expected note comes from a lone engine scheduler playing the document as the plug-in should (followed lane muted on the leader, soloed on the follower), never from the code under test.

    cmake -S ChordLab/Tests/vst3host -B <build dir> && cmake --build <build dir>
    <build dir>/vst3host <path to ChordLab.vst3>

The VST3 SDK is iPlug2's (`IPLUG2_ROOT` from `ChordLab/config/ChordLab-mac.xcconfig`; override with `-DIPLUG2_ROOT=<path>`). `scripts/check.sh --shell` builds it and runs it against the scratch build. One line per check ("--" lines are steps, not counted); exit 1 on the first failure, 70 on a crash.

What it cannot see: `OnIdle` (no run loop, so iPlug2's timer never fires); `HandleLink`, the snapshot request and `OnUIOpen` (no editor, and a single-component VST3 offers a host no message path); `RestorePreset`; anything that needs a second thread, such as the destructor retiring the lifeline before `mLink.reset()` or a hook racing destruction (the engine's lifeline tests pin those under TSan); and the AU, which is not built here.
