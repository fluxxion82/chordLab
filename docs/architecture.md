# ChordLab architecture

A map of how ChordLab works inside, for someone opening the repository for the first time. It gives the mental model and names the
files to read next; it does not repeat what the code and headers already say.

What the controls do is in the [user guide](user-guide.md). Build and test commands are in [development.md](development.md). Paths
are relative to the repository root.

## 1. The three layers

ChordLab is a VST3 instrument for Ableton Live that outputs silence and MIDI, an Audio Unit MIDI effect (`aumi`) for Logic Pro that
outputs MIDI only, and a standalone app. All three are built from the same code in three layers:

- **Engine** (`engine/`). A host-independent C++17 static library with no iPlug2, Objective-C or UI in it. It owns the music
  theory (keys, spelling, chords, palette, function, tension, suggestions, voicing), the document and its undo history, the compiler
  from document to timed MIDI events, the real-time scheduler, the generators, the listener, the MIDI file writer and the JSON
  surface. `engine/README.md` covers its layout, error handling and real-time rules.
- **Shell** (`ChordLab/ChordLab.h`, `ChordLab/ChordLab.mm`). The iPlug2 `Plugin` subclass `ChordLab`, in Objective-C++. It owns
  one `chordlab::DocumentStore`, runs the audio callback, routes messages between editor and engine, saves and restores state, and
  writes exported MIDI files. Its pure helpers (message parsing and validation, export file naming) are header-only in
  `ChordLab/bridge/`, so they are testable without Xcode.
- **Editor** (`ChordLab/UI/`). A SwiftUI view tree hosted in an iPlug2 Cocoa view controller.

**The one rule: the UI computes no theory.** It draws the engine's JSON snapshot and sends JSON commands. Every chord symbol,
numeral, colour family, function label, tension number, voicing, "why" sentence, suggestion and relatedness score arrives in the
snapshot. A drag from the palette carries only a root and a chord type; the engine decides the rest when the drop arrives. What the
plug-in hears from incoming MIDI reaches the editor the same way, as an engine-built payload.

```
  SwiftUI editor                      shell (main thread)                      engine
  --------------                      -------------------                      ------
  ChordLabState --command JSON--> OnMessage --applyCommandJson--> DocumentStore (undo, revision)
       ^                              |                               |
       +<---snapshot JSON-------------+<--snapshotJson (whole doc)----+
                                                                      | compilePlayback, fillSlot
  host MIDI out <-- ProcessBlock <-- Scheduler <-- triple buffer <----+
                    (audio thread)
```

## 2. Threads and ownership

Everything runs inside the host's process. The ownership comment at the top of `ChordLab/ChordLab.mm` is the authoritative
description; this is the summary.

**The audio thread never touches the document.** It plays a compiled copy. Per block, `ChordLab::ProcessBlock` zeroes the audio outputs (Live requires an audio bus), applies resets and panic requests that other threads flagged with
atomics, acquires the newest published progression and hands it to `Scheduler::adopt`, combines the progression's "follow host"
flag with the host transport, runs `Scheduler::process` into a preallocated array, forwards the events, and counts a block that
could not complete. Nothing is logged here.

After construction this code does not allocate, lock, throw or log. `engine/README.md` lists exactly which calls may run on the
audio thread, and `engine_rt_alloc_test` checks it from outside by counting allocations over random blocks.

**How a new progression reaches it.** After an accepted command, the shell runs `chordlab::fillSlot`
(`engine/include/chordlab/publication.h`), which compiles the document into a `SnapshotSlot` (a fixed array of `NoteEvent`s plus end
beat, loop, follow-host and Thru flags) in the back slot of a `TripleBuffer` (`engine/include/chordlab/triple_buffer.h`), and
publishes it only if something changed. The audio thread acquires the latest slot once per block, and a slot it holds is never
rewritten before its next acquire, which is the lifetime `Scheduler::adopt` needs. Rapid edits coalesce. A generation number moves
only when the playable content changes, so browsing a key or toggling a flag does not make the scheduler flush and chase. Slots are
preallocated and sized from the chord and track limits (`engine/include/chordlab/limits.h`).

**Main thread.** Messages, `OnIdle`, window open and close, and the editor all run here. Every model access takes one mutex,
`mModelMutex`, and no lock is held while calling into iPlug2 or the UI. Host state callbacks take the same mutex, because they may
run elsewhere. Delivery to Swift is hopped to the main thread under a lifeline object that makes a late hop harmless after
destruction. `OnIdle` runs on a 20 ms timer with or without an editor: it drains incoming-note events, ticks the listener and pushes
playhead and status to an open editor. A message sent from the SwiftUI view reaches `OnMessage` synchronously, and the reply
(snapshot, error, export-ready) is delivered before the send returns; the editor relies on this to tell which reply answers which
request.

**Previews.** A held palette cell, roll note or roll keyboard key sounds through `Scheduler::pushPreview`, a single-producer ring
and the scheduler's one entry from another thread. Start and Stop are paired by id, a keep-alive extends a held preview and a
watchdog ends one whose Stop never arrives. iPlug2's own MIDI-from-UI queue is not used: it is small and drops silently. Panic makes
the next block release everything the scheduler owns.

**MIDI in.** The shell overrides `ProcessMidiMsg` and never calls iPlug2's default, which would forward input to the output outside
the scheduler. Per incoming event on the audio thread:

- `heardEventOf` normalises note on/off and the sustain pedal into a `HeardEvent` stamped with the sample clock, pushed to
  `HeardRing`, a single-producer single-consumer ring the main thread drains into the `Listener`. If it fills, the main thread
  clears the listener rather than guess which note-offs were lost.
- `thruEventOf` turns note on/off into a Thru event for `Scheduler::pushThru`. The scheduler tracks who owns each sounding pitch
  (progression, preview, Thru), so one physical note exists per pitch and retrigger, release and Panic cover all three.

**The Audio Unit's different MIDI thread.** In the Audio Unit build, iPlug2 delivers incoming MIDI on the host's MIDI event thread,
not the render thread, so `ProcessMidiMsg` cannot touch the scheduler or the heard ring. It pushes raw bytes, tagged with an epoch,
into `RawMidiRing` (`engine/include/chordlab/spsc_ring.h`, `midi_in.h`). The render thread drains it at the top of `ProcessBlock`
with `drainRawMidi`, which orders host resets against queued messages by epoch, so a note pressed around a reset is neither dropped
nor cut. The VST3 and the app call the same per-message code directly.

## 3. The engine

One public header per module in `engine/include/chordlab/`, implementation in `engine/src/`. The theory modules (`pitch`, `scale`,
`chord`, `symbol`, `function`, `harmony`, `tension`, `suggest`, `relatedness`, `palette`, `voicing`, `presets`) are pure functions
over small value types. The parts below have structure worth knowing.

### The document and undo

`Document` (`engine/include/chordlab/document.h`) holds the chord events, sections, project key, auto voice-leading flag, feel,
meter and tracks. `DocumentStore` wraps it with undo and redo, id counters, selection and the edit operations. It is
single-threaded, and every mutation gives the strong exception guarantee: a refused edit leaves document, history and counters
exactly as they were.

State splits two ways. **Undoable content** goes through the history: events, sections, project key, feel, meter, tracks and notes.
**Live state** has no history and survives undo and redo as it is: browsed key, loop, follow host, Thru, relation mode, mute and
solo, selections. A content `revision` moves on every successful commit, undo, redo and replace; note edits can carry the revision
they were computed from, and the engine refuses a stale one, so a slow drag cannot overwrite a newer edit.

### The compile pipeline

`compile` (`engine/include/chordlab/compiler.h`) turns a document into a sorted array of `NoteEvent`s, the only type the real-time
side shares with the compiler. It has two paths:

- **Block path.** If the feel does not perform (block rhythm, strum off, humanise off), each note is one on at the chord's start
  and one off at its end. A default document takes this path.
- **Feel path.** Otherwise `performDocument` (`engine/include/chordlab/feel.h`) performs the whole progression on an integer grid
  of 960 ticks per beat. Chord boundaries are rounded once from cumulative beats, so adjacent chords share a tick exactly and
  nothing needs an epsilon. Per chord it picks the voicing (auto voice-leading respected), generates hits from the rhythm (block,
  pulse, subdivided, or an arpeggio pattern with rate and range), staggers notes by the strum, then applies humanise.

Humanise is a deterministic hash of the chord's event id, so a progression plays and exports identically every time and a chord
keeps its feel through moves and undo. A safety stage keeps every note inside its own chord, which is what lets a section be sliced
from the whole. The feel is used only if the result fits two budgets (total events, and events in any one-beat window); otherwise
everything plays and exports as block chords, and the snapshot's `feelLimited` flag says so.

`compilePlayback` is the playback-only variant. It merges the **audible lanes**, the chords plus every track. Audibility is one
decision over the whole document, `laneAudible`: with no solo anywhere every unmuted lane plays; with any solo, only soloed lanes
do. A merge works per pitch before any event exists: notes starting on the same tick become one, and a note still sounding when the
next of its pitch starts ends there. Export shares this merge. With no track contributing, `compilePlayback` returns exactly what
`compile` does.

### The scheduler

`Scheduler` (`engine/include/chordlab/scheduler.h`) is the audio-thread player. It runs on an integer sample clock anchored where
the transport started, sought or changed tempo, with progression beat 0 at host position 0. A jump beyond a threshold is a seek:
release everything, then chase the notes that should be sounding. With loop on, the progression repeats every `endBeat` beats.
Output is note-on and note-off on one channel. The shell's output array has a fixed capacity (the VST3 wrapper would otherwise grow
its queue on the audio thread), and the worst block must fit it. A test proves that, and it depends on two track limits
(simultaneous notes, onsets per beat) that the document invariant below enforces.

### Tracks and how notes follow their chords

A `Track` (`engine/include/chordlab/tracks.h`) has a role (Bass, Lead or Other), mute and solo flags and notes. The role only sets
defaults: the name, and in the editor the register the roll opens at and the colours. There are a small fixed number of tracks, and
limits on notes per track, polyphony and onsets per beat. `harmonicGuide` (`engine/include/chordlab/guide.h`) classifies each
pitch class over a chord as root, chord tone, tension, avoid or outside; the roll only draws it (see
[Harmonic guides](user-guide.md#harmonic-guides)).

**Notes follow their chords.** A note stores the chord it is anchored to and an offset in ticks from that chord's start; its
absolute position is derived from the chord boundaries. The strip is gapless, so a fixed-position note would end up under different
harmony after one early length change. Every chord timing edit therefore carries the notes. A chord shortened past a note makes it
**inactive**: kept, not played, not exported, and lengthening the chord reactivates it. Undo and redo restore stored documents and
re-run no transform. Removing a chord removes the notes it owns, and a transpose moves pitches only. See [Notes follow their
chords](user-guide.md#notes-follow-their-chords).

**The invariant.** `validateTracks` checks a whole candidate document: counts, unique ids, known roles, name length, anchors that
exist, value ranges, sort order, and the polyphony and onset caps across all tracks, muted ones included so that unmuting can never
break playback. Every track edit, chord timing edit and state load builds a candidate and runs it, and a failing candidate is
refused whole with no history entry. A refused chord edit explains itself in chord terms ("That change would leave more than 8 track
notes sounding at once").

### The bass and lead generators

`generateBass` and `generateLead` (`engine/include/chordlab/generate.h`) are pure functions of the document: they read chords,
voicings, meter, feel and loop, never a track, and return anchored notes for each chord in scope. Bass has six patterns (Roots, Root
pulse, Root-fifth, Octaves, Follow feel, Walking). Lead has six (Held tone, Guide tones, Arpeggio, Stepwise, Motif with a
`variation` choosing one of 64 takes, Follow feel). `DocumentStore` replaces the track's notes anchored to the scope chords with the
new ones in one validated candidate and one undo step. The output is ordinary notes and nothing regenerates afterwards. See
[Generate a line](user-guide.md#generate-a-line).

### The listener

`Listener` (`engine/include/chordlab/listener.h`) turns drained `HeardEvent`s into what the Heard group shows. It is main-thread
only and may allocate. It tracks the held set (with sustain), keeps a pitch-class histogram over a window of recent beats and
estimates the key by correlating it with the 24 rotations of the Krumhansl-Kessler major and minor profiles, with acquire, retain,
replace and remove thresholds and hysteresis so the key does not flicker. The chord is latched at a note-on: `identifyChord` finds
the dictionary chord whose pitch-class set equals the held notes, preferring the one rooted on the lowest note. Time counts in beats
from sample stamps and the host tempo, so listening works with the transport stopped. The listener suggests and never acts: the
editor offers Use for the key and Add for the chord. `heardJson` builds the payload. See [Listening](user-guide.md#listening).

## 4. The message protocol

The editor and shell talk through iPlug2's arbitrary-message channel. Tags are declared once in `ChordLab/ChordLab-Shared.h`, which
the bridging header exposes to Swift. Every payload is UTF-8 JSON carrying `"schema":1`.

**Editor to plug-in**

- **Command**: one engine command, `{"schema":1,"cmd":"...",...}`. Chord edits, keys and browsing, voicing, sections, selection,
  presets, feel and meter, tracks and notes, the generators, undo and redo are all commands. The shell does not parse them; it
  passes the bytes to `applyCommandJson` under the model mutex, and the answer is a snapshot or an error.
- **Snapshot request**, **Panic**, **Clear heard**.
- **Preview**: Start, Extend and Stop for a chord or note preview, paired by id.
- **Export clip**: which lanes, and optionally which section, to write as a `.mid`.
- **Link**: follow or unfollow another instance's lane (section 8).

**Plug-in to editor**

- **Snapshot**: the full `snapshotJson`, never a diff.
- **Error**: a rejected command, malformed preview or failed export. A rejected command changes nothing.
- **Status**, **transport** (playhead, playing flag, host tempo), **export ready** (the file's path), **heard** (the listener's
  payload) and **link state** (section 8).

Commands are transactional. The readers parse and validate the whole message into plain data, destroy the parsed JSON tree, and only
then touch the store through calls with the strong exception guarantee, so a false result means store, history and id counter are
untouched. They reject input over a size limit or nesting depth, and a raw NUL byte, before parsing. The payloads the shell reads
itself (previews, export and link requests) go through small validators in `ChordLab/bridge/`.

Commands carry a **lineage token**, a UUID the shell renews whenever a host state load replaces the document. The shell compares it
inside the locked section that would apply the command and ignores a stale one, so a command computed against the old document
cannot land on a freshly loaded one.

The field-by-field definitions of the snapshot, commands and persisted state live in one place,
`engine/include/chordlab/json_api.h`. The golden JSON files in `engine/tests/fixtures/` are the engine's own output, compared byte
for byte, and the Swift tests decode the same files, so the two sides cannot drift silently.

## 5. State and persistence

`ChordLab::SerializeState` writes an envelope: a version byte, a payload length, the payload from `stateJson`, then iPlug2's
parameter data. `UnserializeState` reads it back; a malformed envelope, or a payload the engine rejects, leaves the current document
untouched.

The payload is saved with the host project and holds:

- the document: keys, events with voicing fields, sections, auto voice-leading, feel, meter, tracks and notes;
- the persisted live state: loop, follow host, relation mode, Thru, new-chord length, mute and solo flags, key-transpose flags;
- the id counters, so ids are never reused after a reload;
- the editor window size;
- for per-track instruments, an `instance` block (section 8).

Not saved: selections, undo history, the revision, and anything the listener heard.

**Versioning is additive.** The schema number stays at 1 while fields are only added. Readers ignore members they do not know, the
Swift decoder reads every later field as optional, and missing fields in a saved state take documented defaults in `loadStateJson`,
so older sets open unchanged while a present but invalid value rejects the state. The number bumps only when an existing field
changes meaning or shape or a required one is removed. Hand-kept legacy fixtures in `engine/tests/fixtures/` pin this and are never
regenerated. A load is atomic: everything is validated first, then `DocumentStore::replace` clears history and selections. The shell
then rebuilds the palette and publishes the playback slot, so the audio thread plays the loaded set even with no editor open.

## 6. MIDI export and drag-out

The writer is `engine/include/chordlab/smf.h`: `writeSmf0` (one track, tempo and time-signature meta events, an end-of-track at the
progression's end so trailing silence survives) and `writeSmf1` (a conductor track, then one named track per lane). Both return an
empty result for anything they cannot encode.

Every drag-out takes one path. A grip in the editor (a lane tab's grip, the All grip, the bottom drag control, a section's handle)
builds a request in `ChordLab/UI/Core/ClipExport.swift`: which lanes, which section. The file is written when the drag starts, never
on hover. The editor sends the request as the export-clip message. The shell validates it, resolves lanes and section under the
model mutex, calls `chordlab::exportClip` and writes a uniquely named file in the system temp directory, answering with export-ready
and its path. The send is synchronous, so the drag knows the outcome before it hands the file to the system. Temp files are purged
by age, never while young enough for the host to still copy them.

`exportClip` (`engine/include/chordlab/export.h`) writes each lane as an independent clip of its own notes, ignoring mute and solo.
The chords lane is `compile` (or `compileSection`), so a dropped chords clip holds exactly what the chords play, feel and humanise
included. A track lane is its active notes, merged and cut to the span. One lane gives a format 0 file; several give a format 1 file
with one named track per lane. A section export slices the whole progression's performance, so voice-leading context is kept, and
re-bases the section to beat 0. See [Getting the MIDI into Live](user-guide.md#getting-the-midi-into-live).

## 7. The editor

**Hosting.** `ChordLab::OpenWindow` creates `ChordLabViewController`, a Swift subclass of iPlug2's `IPlugCocoaViewController`.
`EditorHosting.install` (`ChordLab/UI/EditorHosting.swift`) puts `ContentView` in an `NSHostingController` that is a child of that
controller, pinned to its view, with no size constraints derived from the content: the host decides the editor's size and the
content must not. The build defines `NO_IGRAPHICS`, so iPlug2's own drawing layer is unused. In the Audio Unit build a small
container view releases the controller when the host removes the view.

**State.** `ChordLabState` (`ChordLab/UI/ChordLabState.swift`) is the single `ObservableObject`. It holds the last decoded snapshot,
error, transport, heard payload and held preview, and sends commands. Controls never keep their own model: a toggle shows the
snapshot's value and sends a command, and the reply snapshot flips it.

**Views.** `ChordLab/UI/` has a SwiftUI view per area of the window, stacked by `ContentView.swift`: key bar, transport row,
sequence bar, lane tabs, then either the suggestions, palette and home row (chords lane) or the piano roll (a track), and the status
and drag-out row. Hover details are drawn in-window rather than as popovers, because a popover is a window of its own and can steal
the click. The roll (`PianoRollView.swift`) paints its grid on one `Canvas`, only the visible region, and takes mouse input through
an AppKit view over the grid (`RollInputView.swift`), because Live's plug-in window is often not key.

**Keyboard.** Keys reach the editor by two routes, a local key-down monitor and the VST3 key callbacks, both feeding one key map and
repeat filter so a key arriving twice acts once (`EditorKeyRouter.swift`).

**`ChordLab/UI/Core` is the testable pure-logic package.** It holds every decision and every sentence the user reads: geometry,
text, colours, the snapshot decoder, selection, clipboard payloads, the roll's gesture machine (`RollEditLogic`), key mapping, and
the export and link request builders. It imports only Foundation, CoreGraphics and SwiftUI value types: no AppKit, no iPlug2, no
`ChordLabState`. `ChordLab/Package.swift` also builds it as the Swift package `ChordLabUICore`, so `swift test` runs
`ChordLab/Tests/ChordLabUICoreTests` without Xcode's plug-in project or a host. The Xcode project compiles all of `ChordLab/UI/`,
`Core` included, through a file-system-synchronized group. Views stay thin and draw what Core decides. The roll's gesture machine
returns effects (send a command, start or stop a preview) and one seam in `ChordLabState` performs them, so a gesture is one command
and one undo step. A drag sends the revision it started on, so the engine refuses it if the document moved.

## 8. Per-track instruments

Any ChordLab instance in a Live set can be set to play one lane of another: its chords, or one of its tracks. The first instance
(the leader) then stops playing that lane and the second (the follower) plays it, so a track gets its own instrument. Both keep
their own document and nothing is sent between audio threads. The user side is in [Give a track its own
instrument](user-guide.md#give-a-track-its-own-instrument).

**Why instances, not MIDI buses or channels.** Live offers a VST3's MIDI output as one source whatever output buses the plug-in
declares, so extra buses are invisible to routing. Live also merges MIDI channels on track-to-track routing, so per-lane channels
cannot reach separate instruments. Instances inside one Instrument Rack are listed one by one, which gives a workable set-up: one
ChordLab track holding a rack, plus one track per instrument.

**Why VST3 in Live only.** Only the VST3 build joins the process-wide registry below. Under the Audio Unit, destructors and state
calls can run off the main thread, which breaks the single-producer assumptions the registry and preview forwarding rely on. The
Audio Unit and the app run the same endpoint code against a private registry no other instance can see, so there is one code path;
they drop a saved follow on load and ignore link messages.

**Lane selection in the compiler.** A lane is a `LaneKey` (the chords lane or a track id); a `LaneSelection` is "all except these"
or "only these". `compilePlaybackFor(doc, selection)` compiles just the selected lanes by `compilePlayback`'s own rules, and
`compilePlayback(doc)` is the all-lanes case. Mute and solo are still decided once over the whole document, then the audible lanes
are split by destination, and the same-pitch merge applies only among lanes that share an output.

**Lane uids.** A link must not name a track number, which means something only on one branch of one document's history. Each
instance keeps a table from track number to a UUID (the chords lane's key is the fixed string `"chords"`), saved in its state. A
track gets its uid when first published and keeps it while undo or redo can bring the track back.

**The registry.** `chordlab::LinkRegistry` (`engine/include/chordlab/link.h`) is one per process, a heap singleton that is never
destroyed so an instance torn down late still finds it, with one mutex taken only by non-real-time threads.

- An instance with a follow is a follower and can never be followed; one without is a leader.
- A follower binds to a live leader holding the stored leader id; otherwise it waits and binds when one appears.
- If a state load carries an id another live instance holds, the loaded instance is a **copy** (a duplicated track or rack chain).
  It takes a fresh id, and the registry remembers its origin so the editor can offer a one-click switch.
- The registry derives a follower's status (waiting, project changed, lane missing, muted, playing) and a serial number that moves
  whenever a view would change, so editors refresh from it. Labels (ChordLab A, B...) are kept per instance id, and only leaders
  take a letter.

**Leading and following.** A leader publishes a `LinkFeed` for its followers: document id, lineage, label, every lane's key, name,
role and audibility, and the compiled events of each lane someone follows. It is rebuilt only when something relevant changed. The
registry writes each bound follower's lane into that follower's own **link buffer**, a second `TripleBuffer` per instance whose
single producer is whoever holds the registry mutex and whose single consumer is that instance's audio thread. `LinkEndpoint` is the
instance's non-real-time side, which the shell delegates to under its model mutex. It fixes the order of publications: the side
giving up a lane is written before the side taking it, so the worst outcome is a brief gap and never a doubled lane. At the top of
each block, `LinkSource::select` is the only link code the audio thread runs: it acquires the own buffer, and the link buffer when
following, and adopts the selected one. A change of source always flushes and chases. It allocates, locks, throws and logs nothing.
A following instance publishes its whole own document into its own buffer, unplayed, so unfollowing returns to it.

**Hooks and lifeline.** When one instance's change affects another, the registry returns republish hooks that the caller runs
after releasing its locks. A hook holds the target's lifeline, not the target, so it does nothing once the target is gone.
Lock order is lifeline, then the instance's model mutex, then the registry's mutex; no thread holds two instances' model
mutexes.

**Persistence and messages.** A saved state carries an optional `instance` block: instance id, document id, the lane uid table and
the follow, if any. A leader stores nothing about its followers, so who follows what is derived at run time, and the leader's
document and undo history know nothing of routing. A state without the block loads with a fresh document id, an empty table and no
follow, so an unrelated older document's track cannot reach another instance's followers. The editor's link message (follow or
unfollow) is checked against lineage tokens like a command; a refused follow changes nothing and sends no error, and the link-state
message that follows is the answer. A preview started from a track's roll is forwarded to every follower bound to that lane, so a
note drawn in the Bass roll sounds on the Bass instrument.

**In the editor.** Core holds the decisions (which follow options to offer, the banner's rules, the follower panel's text) and a
gate (`LinkGate.swift`) that stops a following editor from editing or sounding its hidden document. A following editor shows a
panel instead of its usual controls.

## 9. Build products and where tests sit

`engine/` builds with CMake into the `chordlab_engine` library, the doctest suite `engine_tests` and the allocation-counting
`engine_rt_alloc_test`. The plug-in builds from `ChordLab/projects/ChordLab-macOS.xcodeproj` in three schemes (VST3, Audio Unit,
standalone app), compiling `engine/src` and `ChordLab/UI` directly. iPlug2 is an external checkout beside the repository, and
`scripts/release.sh` signs, notarizes and packages a release.

Tests sit next to what they cover: the engine suite and golden fixtures in `engine/tests/`, the message validators in
`ChordLab/bridge/tests/`, the editor's pure logic in `ChordLab/Tests/ChordLabUICoreTests/`, a headless VST3 host test in
`ChordLab/Tests/vst3host/` that loads the built plug-in and checks life cycle, state, playback and linked instances, and an
offscreen render harness for the SwiftUI views in `ChordLab/Tests/viewharness/`. `scripts/check.sh` runs them by flag. Nothing
automated runs inside a host, so Live and Logic are checked by hand. Commands are in
[development.md](development.md).

## 10. Design decisions

- **JSON between engine and UI, and the UI holds no theory.** Debuggable, testable and neutral about the UI framework; the editor
  can be replaced, and one fixture-pinned contract is all both sides read. Never on the audio thread.
- **A VST3 for Live, an Audio Unit MIDI effect for Logic.** Live cannot route MIDI out of an Audio Unit instrument and Logic
  ignores MIDI output from instruments.
- **A triple buffer for playback.** The audio thread gets the latest compiled progression with no lock, allocation or wait; edits
  coalesce; the slot being played stays unmodified. Follow-host and Thru travel in the slot, so a state load never pairs a new flag
  with an old progression.
- **Playback and export share one compile, on integer ticks.** The clip is what plays, feel and humanise included. 960 ticks per
  beat from rounded cumulative boundaries means no comparison needs an epsilon.
- **One budget decision for the whole document.** A feel is used only if the whole progression fits, otherwise everything is block
  chords, so a section never exports with a feel the progression cannot play.
- **Additive schema.** New fields keep the schema number and are absent when they do not apply, so neither side needs rewriting
  and older sets open unchanged.
- **Transpose with key moves notes, not only roots.** A key change shifts each chord's octave by the octave its root crossed, so
  every note moves by exactly the interval, in the direction that drifts least.
- **The listener runs on the main thread, fed by a ring, and only suggests.** The audio thread just normalises, stamps and queues.
  An overflow clears the listener instead of guessing which note-offs were lost, and Use and Add are explicit, so nothing edits the
  document automatically.
- **Notes follow their chords.** The strip is gapless, so a length change would strand fixed-position notes under other harmony.
  Shortening is non-destructive.
- **The snapshot stays whole, notes column-wise.** The editor's latency budget passed once notes were published as one array per
  field, so a diff protocol was not needed.
- **Instances, not buses or channels, for per-track instruments.** Live offers one MIDI source per VST3 and merges channels on
  internal routing, but lists instances separately. A process-wide registry with lane uids and ordered publications lets them find
  each other without sharing audio threads, survive undo and reverted projects, and never double a lane.
