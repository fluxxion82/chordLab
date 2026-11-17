# vst3host

A command-line VST3 host that loads a built `ChordLab.vst3` into its own process and runs the shell's runtime scenarios: life cycle, state, playback of one instance, two instances linked through their states, a copy, a state load while playing, and previews a leader's editor sends for a lane its follower plays. No DAW, window on screen, audio or MIDI device; it writes nothing.

Every expected note comes from a lone engine scheduler playing the document as the plug-in should (followed lane muted on the leader, soloed on the follower), never from the code under test.

    cmake -S ChordLab/Tests/vst3host -B <build dir> && cmake --build <build dir>
    <build dir>/vst3host <path to ChordLab.vst3>

The VST3 SDK is iPlug2's (`IPLUG2_ROOT` from `ChordLab/config/ChordLab-mac.xcconfig`; override with `-DIPLUG2_ROOT=<path>`). `scripts/check.sh --shell` builds it and runs it against the scratch build. One line per check ("--" lines are steps, not counted); exit 1 on the first failure, 70 on a crash.

Editor messages: a single-component VST3 offers a host no message path, so the previews scenario opens the leader's editor as a host does (`createView`, `attached` to a view in no window, which runs `OpenWindow`; `removed` runs `CloseWindow`) and sends each message the way the Swift editor does, through `sendArbitraryMsgFromUI` on the editor's real view controller (`editor.mm`). What the plug-in sends back as an error is recorded on its way to the controller.

What it cannot see: `OnIdle` (no run loop, so iPlug2's timer never fires); `HandleLink` and the snapshot request (not sent), and what the editor draws; `RestorePreset`; anything that needs a second thread, such as the destructor retiring the lifeline before `mLink.reset()` or a hook racing destruction (the engine's lifeline tests pin those under TSan); and the AU, which is not built here.
