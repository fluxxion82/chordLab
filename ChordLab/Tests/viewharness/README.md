# viewharness

The editor's render harness. A command-line program compiled with the editor's SwiftUI views (`ChordLab/UI`, all but `ChordLabViewController.swift`, which needs iPlug2) and Core (`ChordLab/UI/Core`); never part of the Xcode project or the plug-in. It feeds a `ChordLabState` the engine's snapshot and link-state fixtures (`engine/tests/fixtures`) through the same `apply…` calls the view controller makes, renders `ContentView` offscreen at 700 x 632 (the minimum window) and 900 x 800 (the default), writes one PNG per mode and size, prints the frames of the elements the views mark with `probeFrame` (`ChordLab/UI/FrameProbe.swift`; nothing in the plug-in, where no probe is set), and checks them. Exit 0 when every check passes, 1 when one fails, 2 for a usage error.

    ChordLab/Tests/viewharness/build.sh <build dir>
    caffeinate -d -u <build dir>/viewharness [--fixtures <dir>] <png dir> [<mode>...]
    <build dir>/viewharness --list

`scripts/check.sh --views` does both into a fresh run directory inside the validated scratch directory (`CHORDLAB_SHELL_OUT`, as `--shell`); it is not part of `--all`.

**The display must be awake**: AppKit draws nothing into the bitmap while it sleeps. Run it under `caffeinate -d -u` (check.sh does). A render with almost no colours fails with a message saying so.

What it stubs: the plug-in connection (`ShellStub`: records every message the editor sends and, in the modes that need it, answers a link message inline through `onLink` with what the shell would push, in its order: a new token, a snapshot, an error, a link state); the bridged constants of `ChordLab/ChordLab-Shared.h` (`Stubs.swift`, compared with the header at start-up); the preferences (`MemoryDefaults`, in memory, also as the views' `@AppStorage`), and the banner's dismissals (a `FollowDismissals` per mode, so modes do not see each other's). It writes nothing but `<png dir>/<mode>-<width>x<height>.png` (the directory must exist); `build.sh` writes the binary and the module cache into its build directory. The window is offscreen and never ordered in (the harness checks it was never visible); the app takes no Dock icon or menu bar.

Modes (each at both sizes):

| mode | state | checks beyond the common ones |
|---|---|---|
| `editor` | a leader with two tracks, no link state | no banner, no Follow menu; route B's ⌘Z and `perform` reach the document |
| `banner-fresh` | an empty instance that sees ChordLab A | the banner under the key bar, above the transport bar, one line |
| `banner-copy` | a copy of ChordLab A holding its progression | the copy's wording |
| `banner-compact` | the fullest banner: five lanes, More…, and the refusal sentence of a click the stub refused | the sentence shows; the request carried schema, op, A's id, lane key and lineage, and the own lineage |
| `menu` | a leader whose lanes others play | the Follow menu's warning; the transport bar's three layouts measured with Follow, the one chosen is the first that fits |
| `panel-playing`, `panel-muted`, `panel-waiting`, `panel-lane-missing`, `panel-project-changed`, `panel-switch-to` | a follower in each status | the rows each status shows; no key command reaches the hidden editor (playing) |
| `panel-crowded` | everything at once: the switch as a menu, the refusal sentence, incomplete blocks, an error line, the kept note | every row present |
| `tab-mark` | a leader whose chords and Lead others play | the arrow on Chords and Lead, apart from the name and inside the tab; none on Bass |
| `banner-long-names`, `banner-realistic-names`, `banner-wide-names` | four 64-character names; five 16-character names; a copy with wide-letter names | the banner keeps one line inside the window; the lanes that do not fit go to More… |
| `panel-long-names` | the followed lane and its copy's named with 64 characters | the headline wraps inside the panel; the switch button stays inside it |
| `rows-before-banner` | the key hint and the Heard group, no link state (what existed before the banner) | the palette keeps its row (100 pt at 700 x 632, as before) |
| `rows-banner-heard`, `rows-banner-heard-notice`, `rows-banner-key-hint`, `rows-banner-key-hint-notice`, `rows-banner-key-hint-heard`, `rows-all`, `rows-long-names-notice-heard` | the banner with every combination of the key hint, the Heard group and the refusal sentence | at 700 x 632 the key row wins over the banner and the palette keeps 76 pt or more; in a tall window both show |
| `follow-stale-entry` | an entry drawn before a token change, clicked after it | sent with its old lineage; answered stale: "This ChordLab changed…", no error, nothing applied |
| `gate-retained-callbacks` | a roll menu choice, a rename commit, an append and a cell press after the panel appeared | only the panel's Thru command and Panic leave; no preview start |
| `dismiss-reopen` | Not now, then a second state of the same document (a reopened window) | it does not offer; another document does |
| `answer-stale`, `answer-malformed-lineage`, `answer-parse-error`, `answer-none`, `answer-applied-while-held`, `answer-applied` | what the shell sends inside a follow's send, in its order (`ShellStub.onLink`) | the sentence, the error or the panel each answer gives; a held preview stops when the panel appears |
| `unfollow-stale`, `unfollow-error` | Stop following answered stale, or with an error and the link state | the panel stays with "This ChordLab changed…", or with the error and no sentence |

Common checks: the render is not empty and the window was never on screen; in the editor, the transport bar and the Follow menu lie inside the window's padding, the Follow menu sits after Length in its row, every transport row is one line, and at 700 x 632 the palette viewport keeps one full row (76 pt); in the panel, every reported part lies inside the window and the panel, no two rows overlap, the panel is centred and at most 520 pt wide, and its buttons are one line.
