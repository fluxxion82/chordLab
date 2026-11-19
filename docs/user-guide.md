# ChordLab user guide

ChordLab is a chord-progression plug-in for Ableton Live and Logic Pro in the style of Klimper 2. You
pick a key, audition chords from a palette, drag them into a sequence, and the plug-in plays the
progression as MIDI in time with the host or hands it to a track as a clip. It also listens to the MIDI
that reaches its track and shows the key and the chord it hears. It makes no sound of its own: it comes
as a VST3 instrument for Live that outputs MIDI to a second track with a real instrument, an Audio Unit
MIDI FX for Logic that plays the instrument on its own track, and a standalone app.

This guide covers setting it up, every control in the window, keyboard shortcuts, what is saved with
your set, and the current limits. For a guided first session, see [tutorial.md](tutorial.md). Building it is
covered in [development.md](development.md); how it works inside is in [architecture.md](architecture.md).

## Contents

- [Before you start](#before-you-start)
- [Setting up in Ableton Live](#setting-up-in-ableton-live)
- [Setting up in Logic Pro](#setting-up-in-logic-pro)
- [The idea in one minute](#the-idea-in-one-minute)
- [The window, top to bottom](#the-window-top-to-bottom)
  - [Key bar](#key-bar)
  - [Transport row](#transport-row)
  - [Sequence bar](#sequence-bar)
  - [Lane tab row](#lane-tab-row)
  - [Suggestions strip](#suggestions-strip)
  - [Palette](#palette)
  - [Home row](#home-row)
  - [Bottom line](#bottom-line)
- [Reading a chord](#reading-a-chord)
- [Chord relations](#chord-relations)
- [Presets](#presets)
- [Feel](#feel)
- [Tracks and the piano roll](#tracks-and-the-piano-roll)
  - [Lanes](#lanes)
  - [The piano roll](#the-piano-roll)
  - [Editing notes](#editing-notes)
  - [Harmonic guides](#harmonic-guides)
  - [Notes follow their chords](#notes-follow-their-chords)
  - [Generate a line](#generate-a-line)
  - [Getting tracks into Live](#getting-tracks-into-live)
  - [Track limits](#track-limits)
- [Give a track its own instrument](#give-a-track-its-own-instrument)
  - [Set-up in Live with an Instrument Rack (recommended)](#set-up-in-live-with-an-instrument-rack-recommended)
  - [Set-up in Live with separate tracks](#set-up-in-live-with-separate-tracks)
  - [What a second ChordLab's window shows](#what-a-second-chordlabs-window-shows)
  - [Changing or stopping](#changing-or-stopping)
  - [In the main ChordLab](#in-the-main-chordlab)
  - [Duplicate, delete, undo, save](#duplicate-delete-undo-save)
  - [Limits and cautions](#limits-and-cautions)
- [Playing it back](#playing-it-back)
- [Listening](#listening)
- [Getting the MIDI into Live](#getting-the-midi-into-live)
- [Keyboard shortcuts](#keyboard-shortcuts)
- [What is saved](#what-is-saved)
- [Limits and troubleshooting](#limits-and-troubleshooting)

## Before you start

- macOS 15 or later and Ableton Live or Logic Pro. Install the pkg from the latest GitHub release (see the
  [README](../README.md#install)), or build from source as [development.md](development.md) describes. In Live, use the VST3; Live cannot route MIDI out of an AU instrument.
- The pkg installs the VST3 to `/Library/Audio/Plug-Ins/VST3/ChordLab.vst3`, the Audio Unit to
  `/Library/Audio/Plug-Ins/Components/ChordLab.component` and the app to `/Applications/ChordLab.app`. A
  build from source installs development copies to `~/Library/Audio/Plug-Ins/VST3/ChordLab.vst3` and
  `~/Applications/ChordLab.app` instead; keep one or the other. Rescan plug-ins in Live's Preferences (or
  restart Live) after installing or after a new build.
- The standalone app shows the same window without a host. It is handy for exploring the controls; the
  Live or Logic setup below is the way to hear it.

## Setting up in Ableton Live

1. Create a MIDI track (track A) and drop **ChordLab** (Plug-Ins → VST3) on it.
2. Create a second MIDI track (track B) holding the instrument you want to hear.
3. On track B, set **MIDI From** to track A. In the second chooser directly below it, pick
   **ChordLab** (the MIDI coming out of the plug-in) instead of the default *Pre FX*.
4. Set track B's **Monitor** to **In** (or arm the track).
5. Press a palette cell in ChordLab. Track B's instrument should sound, whether Live's transport is
   stopped or running.

Live merges all MIDI channels on internal track-to-track routing, so one ChordLab instance drives one
receiving track. For a second part, add a second instance, or give a track of this ChordLab its own instrument
(see [Give a track its own instrument](#give-a-track-its-own-instrument)).

## Setting up in Logic Pro

1. Create a software-instrument track with the instrument you want to hear.
2. In the track's channel strip, click the **MIDI FX** slot and choose **Audio Units → SterlingAlbury → ChordLab**.
   (If it is not listed: Logic Pro → Settings → Plug-in Manager → **Full Audio Unit Reset**, then quit and reopen Logic.)
3. Press a palette cell: the instrument sounds, with the transport stopped or running. Follow Live is "follow the host" here.
4. To record what ChordLab plays into a region, open the MIDI FX slot's menu and choose **Record MIDI to Track Here**,
   then record-arm the track and record as usual; the region holds the progression's notes.

Logic places ChordLab before the instrument on the same track, so one instance drives that track's instrument; add
another instance on another track for a second part. A MIDI keyboard playing the track reaches ChordLab (the Heard
group and Thru work as in Live); regions on the track do too.

> The Audio Unit passes Apple's `auval` validation, but this setup has not yet been tested end to end in Logic Pro.

## The idea in one minute

- The **project key** is the key your track is in. The **browsed key** is the key whose chords the
  palette shows; it starts equal to the project key and can be stepped around the circle of fifths to
  borrow chords. Chords that leave the project key are drawn as rings instead of filled circles.
- The **palette** holds the browsed key's chords, one column per scale degree, triads at the bottom,
  richer chords above them and the two-note power chords in the top row.
- The **sequence bar** is your progression: chord blocks over a beat ruler.
- With **Follow Live** on, the progression plays whenever Live's transport runs, locked to Live's beat
  position. The **♫** control drops the progression onto a track as a MIDI clip.
- **Feel** (transport row) plays the progression as a rhythm, a strum or a humanised performance instead of
  block chords; the clip you drag out matches.
- **Listening**: play into the ChordLab track and the **Heard** group above the palette names the key and
  the chord you play; **Use** makes that key the project key, **Add** appends the chord. With **Thru** on,
  the notes you play also come out of ChordLab. See [Listening](#listening).
- **Tracks**: add a Bass, Lead or Other track under the sequence bar and write its notes in a piano roll whose
  rows are shaded by how each pitch sits over the chord playing at that moment. Notes belong to the chord they
  start in, so they move with it. See [Tracks and the piano roll](#tracks-and-the-piano-roll).
- Everything else explains the harmony: a function and tension for every chord, "why" tips,
  ranked suggestions for the next chord, and palette cells that grow or shrink by how related they are
  to the chord you selected.

## The window, top to bottom

A new ChordLab window opens at 900 × 800 points; Live can resize it down to 700 × 632, and the size is saved
with the set. Top to bottom: the key bar, the transport row, the sequence bar, the lane tab row (**Chords** and
your tracks, see [Tracks and the piano roll](#tracks-and-the-piano-roll)), then, with Chords open, the suggestions
strip, the palette and the home row, or, with a track open, that track's piano roll in their place, and the bottom
line.

**Tips.** Resting the mouse on a palette cell, a sequence block or a suggestion chip shows its details in
a tip (after about 0.6 s). The tip is drawn inside the window, never takes a click, never opens
while you hold a mouse button, goes away when you press, and stays away until the mouse leaves and comes
back.

### Key bar

- **Key**: tonic and mode pickers for the project key. 12 tonics; the seven church modes plus
  harmonic minor and melodic minor. Choosing a key also brings the browsed key home.
- **Transpose with key** (a switch right after the Mode picker, off by default; in a window narrower
  than 880 points it moves to the transport row, right after Auto voice-lead): when on, changing the
  key's tonic transposes every chord in the sequence, not only the selected ones, by the same interval,
  and keeps each chord's type: C – Am – F – G in C becomes D – Bm – G – A in D. Length, velocity,
  inversion, sections and any manual voicing stay. The move goes up or down, whichever direction keeps
  the progression nearer the middle register (C to G moves every chord down a fourth, so C – Am – F – G
  becomes G – Em – C – D). With Auto voice-lead off, every chord moves by exactly the same interval (a
  chord already an octave up or down can land an octave off). With it on, the first chord and the chords
  you voiced yourself move exactly, and the others are re-voiced from them as usual, so they may change
  inversion. Changing only the mode, or respelling the
  tonic (C♯ to D♭), moves nothing and relabels the chords, as it does with the switch off. To hear the
  same chords as the relative minor, turn the switch off before choosing the key: with it on, tonic A
  followed by mode Minor moves the chords down a minor third to A, then relabels them. A key change that transposes
  is one undo step (⌘Z restores key and chords), and a note under the key bar says "Moved 4 chords to D.
  ⌘Z undoes it." for four seconds (browsing to another key closes it sooner). The switch is saved with
  the set.
- **Browse**: `<` and `>` step the browsed key a fifth down or up; the mode menu between them, beside
  the browsed tonic, switches to a parallel mode (same tonic, another of the nine modes); **Home**
  returns to the project key.
- **Undo / redo** buttons (also ⌘Z / ⇧⌘Z). Edits to the progression and to the tracks and their notes are
  undoable, in one history; the live settings below (browsed key, Follow Live, Loop, Thru, Relations,
  Transpose with key) and the lanes' **M** and **S** toggles are not.
- **Labels** (*Names / Numerals / Both*): how chords are labelled in palette cells, the home row,
  sequence blocks and drag previews. *Both* (the default) shows the symbol over the numeral and
  function, e.g. `C` over `I · T`.
- **Relations**: which relation to the reference chord sizes the palette cells. See
  [Chord relations](#chord-relations) below. A caption names the reference ("to G7").

### Transport row

- **Follow Live**: play the progression in time with Live's transport. Off, the progression is silent
  and cell previews still work.
- **Loop**: repeat the progression from its start; off, it plays once and is silent past its end.
- **Thru** (on by default): pass the notes ChordLab hears on its track to its output, alongside the
  progression and the previews, so the instrument on the receiving track plays them. Off, ChordLab still
  listens but passes nothing on, and turning it off releases any note you are holding. See
  [Listening](#listening).
- **Panic**: silence every sounding note, the notes passed through included.
- **Auto voice-lead**: let ChordLab pick each chord's inversion and octave for the smoothest motion
  from the previous chord. A chord with its own manual voicing keeps it.
- **Transpose with key**: here, right after Auto voice-lead, only while the window is narrower than 880
  points; wider, it sits in the key bar after the Mode picker. Same switch, same setting: see
  [Key bar](#key-bar).
- **+ Section**: start a section on the selected chord, else the last one.
- **Record**: while on, pressing a palette or home cell adds its chord to the strip, after the selected block
  (else at the end), as long as you held it at the host's tempo, on the quarter-beat grid. A press under
  0.2 s is a tap and adds a chord of the new-chord length. The preview still sounds. A press you drag to
  the strip just drops, and adds nothing more. Record is off whenever the window opens.
- **Presets**: open the list of well-known progressions and load one. See [Presets](#presets) below.
- **Feel**: after Presets. Opens a popover with the rhythm, strum (or, under Arpeggio, its pattern, rate and range), humanise and the document's meter. A dot
  beside the title means the progression plays with a feel (a rhythm other than Block, a strum or humanise;
  the strum speed alone does not count). See [Feel](#feel) below.
- **Length**: after Feel. The menu's title is the selected blocks' common length, "Mixed" when they differ,
  or "New: …" with nothing selected. Its entries are 1/16 to whole (a quarter is one beat in every meter),
  then 1, 2 and 4 bars of the meter, and with nothing selected "Default (one bar)"; **Custom…** takes
  0.25–64 beats in quarter beats (Return applies, Escape closes). A pick sets the selected blocks' length in
  one undo step; with nothing selected it sets the length of every chord you add from then on (drops,
  chips, number keys, the Heard group's Add, Record taps). A dot beside the title shows while that length is
  not the one-bar default.

### Sequence bar

A sections header, the tension curve, the chord blocks, and a ruler drawn by the document's meter: a tick per
pulse (a beat in 4/4 and 3/4, a dotted quarter in 6/8, 9/8 and 12/8, an eighth in 7/8), taller on each
bar line, with the bar number beside it. The bars count from the start of the progression, whatever the
chords' lengths.

- Drop a palette or home cell anywhere on the bar to append the chord. A new chord lasts the new-chord
  length: one bar of the meter (4 beats in 4/4, 3 in 3/4 and 6/8, 3.5 in 7/8) unless you chose another
  with **Length**; the drag preview is that wide. The new chord
  is selected, so the suggestions and the palette relations read from it at once.
- Click a block to select it; ⇧-click selects the range from the anchor (the brighter-ringed block),
  ⌘-click adds or removes one block, ⌘A selects every block, and a click on the empty bar deselects. Every
  selected block has a white ring, the anchor's brighter; the suggestions strip, the palette relations and
  the large tension dot follow the anchor.
- Drag a block sideways to reorder: a drag that starts anywhere on the block but its handle moves the
  block. The handle is the 14-pt strip at the block's right edge (a light bar marks it); the cursor
  becomes ⇔ over it. Drag the handle to change the length: the block follows the mouse, and lengths snap
  to quarter beats, from 0.25 to 64 beats. Dragging a block that belongs to a selection moves only that
  block.
- Delete or Forward Delete removes every selected block, in one undo step. A progression holds up to 256 chords; undo
  remembers the last 200 edits.
- With **Transpose with key** off (the default), changing the project key relabels the chords (numerals,
  rings, functions); it does not transpose them. The first time you change the tonic in a project that has
  chords in the sequence, a row under the key bar says "Chords in the sequence kept their notes." with
  three buttons: **Move once** transposes the chords to the new tonic this time, **Move and always
  transpose** does that and turns the switch on, **Keep chords** closes the row. The row also closes when
  you change the key again, add, remove or change a chord, or browse away from the project key (`<`,
  `>` or the browsed mode), and it shows once per project, even if you close it unanswered. The row
  never pushes the bottom line or the drag-out control out of view: the palette grid gives up the height,
  and in a window shorter than 700 points the row is drawn smaller and the Diatonic / Applied switch
  steps aside while the row (or the "Moved" note) shows.
- Click the small **v** badge for the voicing popover: inversion, octave (one below, as is, one above),
  and Auto. Right-click a block for *Copy*, *Cut*, *Paste after*, *Duplicate* and *Remove* (on the whole selection when
  the block is part of it, else on that block; Paste after goes after that block), then *Voicing…*, *Inversion*, *Octave*, *Auto voicing*,
  *Transpose all chords* (*Up a semitone* or *Down a semitone*: every chord in the sequence, one undo
  step, the key unchanged) and *Remove chord*. A chord with its own voicing is marked "manual". A
  transposed chord keeps its place relative to the others, so the voicing popover may show a different
  octave afterwards; *Auto voicing* (or the popover's Auto) puts such a chord back in the default register.
- Rest the mouse on a block for a tip with its details and the "why" against the previous chord: root
  motion, common tones, voice-leading cost, a dominant-to-tonic resolution, or the parallel mode a borrowed
  chord comes from.
- A thin curve between the sections header and the blocks traces the progression's tension: a dot
  over the middle of each block, in the chord's function colour (larger for the selected chord), higher
  for a tenser chord. It shows the arc of the progression at a glance; the numbers are in each block's
  tip.
- A playhead moves along the bar while the progression plays.

**Sections** are named, coloured spans over the progression (8 colours), each exportable as its own
clip. The first section always starts at the first chord: adding your first section on a later chord
also creates one for the chords before it. A new section is named by count ("Section 1", "Section 2",
…, stepping over a name another section already has) until you rename it. Removing the chord a section
starts on removes that section;
the first section instead moves to the new first chord, unless that chord already starts a section or no
chord remains. In the sections header: double-click a span to rename it (Return commits, Escape cancels,
and a click elsewhere in the ChordLab window also finishes it);
right-click for *Rename…*, *Colour* and *Remove section*; drag a span's left edge to move where it
starts (the first section always starts at the first chord); drag its **♫** to a MIDI track to export
just that section.

**Clipboard and Record.** ⌘C copies the selected chords, with their lengths, velocities and voicings, the
track notes that start in them (see [Notes follow their chords](#notes-follow-their-chords)), and their names
as text for other apps. ⌘V pastes after the anchor, else at the end, and selects what it
pasted; ⌘X cuts; ⌘D duplicates the selection after it. Copy in one ChordLab and paste in another; text
copied elsewhere does not paste as chords. A paste that would pass 256 chords is refused with a message and
changes nothing. Every paste, cut, duplicate and multi-block delete is one undo step. **Record**, as in the
transport row, chains: each recorded chord is selected, so the next one goes after it.

### Lane tab row

Under the sequence bar: **Chords**, then up to four tracks, **+** to add one, and **♫ All** at the right end.
Clicking a tab opens that lane. With Chords open the suggestions strip, the palette and the home row follow; with
a track open its piano roll takes their place. See [Tracks and the piano roll](#tracks-and-the-piano-roll).

### Suggestions strip

"Start with" while the progression is empty, otherwise "Next": up to 8 chips numbered 1–8, ranked
for the selected chord (else the last one). Click a chip, or press its digit, to append that chord; it
becomes the selection, so the strip re-ranks for it.
Rest the mouse on a chip for the reasons behind it. When the window is too narrow for the chips on one
line each, every chip puts its digit, numeral and function dot under the symbol, so nothing is cut
short; a list too long even for that scrolls sideways.

### Palette

The browsed key's chords: one column per scale degree. From the bottom: triads, sixth and seventh
chords, seventh and add9 chords, colour chords, and the power chords (root and fifth, two notes) in the
top row. Every row is named on its left: *triads*, *6 / 7*, *7 / add9*, *colour*, *power*.

- The cells shrink to show every row when the window is tall enough (at the default 900 × 800 all five
  rows show). In a shorter window the cells keep a minimum size and the grid scrolls, opening on the
  triads at the bottom; scroll up for the richer rows.

- Press and hold a cell to hear it for as long as you hold it; release to stop. While you hold, ChordLab
  tells the engine once a second that the cell is still held. The engine ends a preview by itself after
  four seconds of processed audio without hearing that, so a release that never reaches it cannot leave
  a chord sounding. A plug-in window frozen for several seconds ends a held preview the same way; the
  cell then still shows pressed, so release it and press again. While Live processes no audio (its audio
  engine off), nothing ends and nothing sounds. A hold made by touch (a Sidecar screen, where macOS
  reports no mouse button) is kept alive for one minute at most; after that, release and press again.
- Drag a cell to the sequence bar to append it.
- Rest the mouse on a cell for a tip with its notes, function, tension, any notes outside the project
  key, and its relation to the reference chord.
- A filled circle is entirely in the project key; a ring borrows at least one note from outside it.
- Cells grow and brighten, or shrink and dim, by how related they are to the reference chord. The hit
  area is always the whole slot, so a small cell is as easy to press as a large one.

**Diatonic / Applied.** The switch at the top left of the palette (at the right end of the home row while that row is shown; hidden in a window shorter than 700 points while the key-change row under the key bar shows; while ChordLab is listening it always stays at the top left, with the Heard group in the same row) shows either the key's own chords
(*Diatonic*, the default) or its **applied chords** (*Applied*): the chords that lead into one chord of
the browsed key for a moment.

- Each applied chord sits in the column of the chord it resolves to: over `ii` you find the chords that
  lead to `ii`. The top row is the secondary dominant `V7/x` (in C major, `A7` leads to `Dm`); the row
  under it is the leading-tone diminished seventh `vii°7/x` (`C#dim7`, also to `Dm`). The rows are named
  `V7/x` and `vii°7/x` on their left. The tonic's column
  and the column of a diminished chord stay empty: neither is tonicised.
- The numeral is the applied name, read in the browsed key: *Numerals* shows `V7/ii`, *Both* the symbol
  over `V7/ii · D`. The tip adds "Leads to Dm (V7/ii)".
- Most applied chords are rings, because they borrow a note from outside the project key (`C#` in `A7`).
  A filled one is entirely in the key.
- Press, drag, the function dot, the tip and relation sizing work as in the diatonic grid. The cells
  are the same size as in the diatonic grid, so switching views does not resize them. Sizes are
  on the same scale as the diatonic grid and the home row, so a big applied cell is as related to the
  reference as a big home cell beside it.
- Dropping an applied chord on the sequence bar gives an ordinary block: its numeral there is the one in
  the project key (`A7` in C major is `VI7`), like every block.
- When the window is too short for both rows, the view opens on the `V7/x` row; scroll for the other.
- Your choice is remembered. A key with no applied chords shows "No applied chords in this key".

**Heard.** Once a note reaches ChordLab's track, the row of the Diatonic / Applied switch shows what
ChordLab hears, at its right end, in one line:

`Heard · A Minor · 87%  [Use]  ·  Am7 · vi7  [Add]  ×`

- The first part is the key the notes fit and how well (0–100 %). Until a key qualifies it reads
  "listening · 5 notes" (the notes counted so far) and has no **Use**. **Use** makes that key the project
  key, exactly as choosing it in the Key pickers does.
- The second part is the chord the held notes make, symbol and numeral, labelled in the project key.
  Before any chord is recognised it lists the held notes instead ("C, E, G♯") and has no **Add**. **Add**
  appends the chord as you played it, inversion included. Once you let go the chord stays shown, dimmed,
  and **Add** still appends it.
- **×** forgets everything heard; the group hides until the next note arrives.
- The palette cell with the heard chord (same root and type) gets a ring in the accent colour, at full
  strength however dimmed the cell is by the relations, in the Diatonic and the Applied view; nothing is
  ringed when the browsed palette does not hold that chord, and the home row is never ringed.

Before the first note of the session the group is not shown at all, and the switch row behaves as
described above. See [Listening](#listening) for what feeds it.

### Home row

Appears under the palette only while the browsed key differs from the project key: "Home · <project
key>" and that key's seven triads, so the home chords stay one press away while you borrow. The cells
behave exactly like palette cells, relation sizing included.

### Bottom line

A status readout (cell and event counts, end beat, follow and loop state, and any error from the
engine) and the **♫ … drag to Live** export control, which drags the open lane: the chords ("♫ 16 beats · drag
to Live") with Chords open, that track's notes ("♫ Bass · 16 beats") with a track open. When the feel is too
dense (see [Feel](#feel)), the readout is replaced by an orange message saying playback and export use block
chords.

## Reading a chord

- **Function**: Tonic, Predominant, Dominant or Other, shown as a coloured dot and a letter
  (T, PD, D) next to the numeral.
- **Tension**: a 0–100 score shown as a bar in tips and as the curve over the sequence bar: how far
  the sound is from a plain in-key triad (rough intervals, notes outside the key, density). It is a
  colour axis, separate from function; a dominant seventh is tense because of its tritone, not because
  it wants to resolve, so a high point on the curve marks a colourful chord, not one that must move on.
- **Filled vs ring**: filled is in the project key; a ring is borrowed. The tip names the
  out-of-key notes. The parallel mode a borrowed chord comes from, when one fits, appears in a sequence
  block's "why" (every block but the first) and in a palette cell's relation line (while Relations is on
  and there is a reference chord).

## Chord relations

While a chord is selected (or the progression has a last chord), every palette and home-row cell is
sized by a 0–100 relatedness score to that **reference** chord. The **Relations** picker in the key bar
chooses what "related" means:

| Mode | Big cells are chords that… |
|---|---|
| **Common tones** | share the largest part of their notes with the reference (shared notes divided by the bigger chord's size). |
| **Voice leading** | lie closest to the reference: the fewest semitones to move its notes onto theirs, counted between note names regardless of octave or voicing. |
| **Function** | make the most conventional move in pop harmony (a dominant chord after a predominant, a tonic after a dominant, and so on). |
| **Blend** (default) | score highest by the same measure that ranks the suggestions strip, so a top chip that is also in the palette is among the biggest cells (the strip can also suggest chords the palette does not show; the reference chord itself always stays full size). |
| **Off** | Every cell is the same size. |

The caption under the picker names the reference ("to G7"). Resting the mouse on a cell shows
"Relation to G7 · 72" and the reason in one line: common tones, root motion, voice-leading cost, or
the key a borrowed chord comes from. Deselecting makes the last chord of the progression the reference;
an empty progression has none, and the cells return to uniform size. The picker setting is saved with
the set.

## Presets

**Presets** in the transport row opens a list of well-known progressions (pop, rock, blues, jazz, folk
and country, EDM, R&B and neo-soul, film, gospel, classical), grouped by genre. Each row shows the
preset's name, its roman numerals and a badge saying whether it was written for a *major* or *minor* key
or works in *any*.

The numerals are read against the project key: the Axis progression, I – V – vi – IV, loads as
C – G – Am – F in C major and as D – A – Bm – G in D major. Change the key first, then load. A preset
written for the other kind of key (a major-key preset while the project key is minor, or the reverse) is
greyed, with a line saying "Written for a major key" or "Written for a minor key". It still loads: you
get the chords the numerals name, and those outside the key wear the usual out-of-key rings.

The choice at the top of the list decides what a click does, and is remembered:

- **Add to end** (the default): the preset's chords go after the last chord.
- **Replace progression**: the chords and sections are cleared and the preset takes their place. Keys,
  Follow Live, Loop and the other live settings are untouched.

Either way, loading is one undo step (⌘Z takes the whole preset back out), and a section named after the
preset starts at its first chord, so it can be renamed, recoloured or dragged out on its own. Adding to
the end of a progression that has no sections yet also puts the chords before it in a section of their
own. The list closes once the preset is in; if ChordLab refuses it (the progression would grow past its
chord limit), the list stays open and the status line says why. Escape closes the list.

## Feel

**Feel** in the transport row sets how the whole progression is played: a rhythm, a strum, an amount of
humanise, and the meter the rhythms follow. The defaults are block chords, no strum, no humanise and 4/4,
which plays plain block chords. One feel and one meter apply to the whole
progression, not to a section or a chord, and both are undoable (⌘Z) and saved with the set.

**Playback and export match.** The progression plays in Live with the feel, and the clip you drag out (the
whole progression or one section) holds the same notes at the same times, the humanise included. Previews
stay block chords: a palette cell, a chip or a held cell always sounds as one chord, whatever the feel.

**Meter.** The Meter menu offers 2/4, 3/4, 4/4, 5/4, 6/8, 7/8, 9/8 and 12/8 (a meter loaded from a set
that is not in the menu, such as 3/8, is shown too). It sets the ruler's bars and pulses, what the rhythms
do, and the time signature written into the clip. It does not read Live's time signature: set both to
match. Changing the meter keeps every chord's length, so chords made in 4/4 will not line up with 6/8 bar
lines; chords added afterwards last one bar, and presets keep their bar proportions: a four-beat step
becomes one bar of the meter, a two-beat step (the jazz turnarounds) half a bar.

The pulse is the beat of the meter: a quarter note in 2/4, 3/4, 4/4 and 5/4, an eighth in 7/8, and a
dotted quarter in 6/8, 9/8 and 12/8 (compound meters, where the eighths come in threes). The subdivision is
half a pulse (an eighth in 4/4, a sixteenth in 7/8), or a third of a pulse in the compound meters (an
eighth in 6/8). The Rhythm menu names it for the meter: "Subdivided (eighths)" or "(sixteenths)".

**Rhythm.** Each chord plays from its own start; a hit is a moment when notes sound.

| Rhythm | What plays |
|---|---|
| **Block** | One hit for the whole chord, as before. |
| **Pulse** | The whole chord on every pulse. |
| **Subdivided** | The whole chord on every subdivision. |
| **Offbeats** | The whole chord on the chord's own start (so a change is always heard) and on every subdivision that is not a pulse. In 4/4 that is the start and the "and" of each beat. |
| **Low note, then chord** | On every pulse. The first hit of a chord and the first pulse of each bar play the lowest note alone; every other pulse plays the rest of the chord. A one-note chord plays its note everywhere. |
| **Arpeggio** | One note per step; the Pattern, Rate and Range rows below say which note, how fast and over how many octaves. |

The grid is the progression's, counted from its start, so a chord that begins off the grid plays at its
own start, then on the next grid points. A chord too short for the next grid point plays one hit.

**Arpeggio pattern, rate and range.** Choosing Arpeggio swaps the Strum and Speed rows for **Pattern**, **Rate**
and **Range**. The defaults (Up, the subdivision, 1 oct) are the old "Arpeggio up".

The **Pattern** menu has seventeen entries: Live's Arpeggiator styles plus Alberti. The table shows one round
for a four-note chord, 1 the lowest note and 4 the highest; the pattern then repeats from its first note,
counting from the chord's start. With three notes some patterns coincide (Pinky up-down is Pinky up, Thumb up-down
is Thumb up, and Alberti is Pinky up); with two notes Alberti is 1 2 1 2; with one note every pattern plays that note.

| Pattern | One round, four notes |
|---|---|
| **Up** | 1 2 3 4 |
| **Down** | 4 3 2 1 |
| **Up-down** | 1 2 3 4 3 2 (the top and the bottom play once per round) |
| **Down-up** | 4 3 2 1 2 3 |
| **Up & down** | 1 2 3 4 4 3 2 1 (the top and the bottom play twice) |
| **Down & up** | 4 3 2 1 1 2 3 4 |
| **Converge** | 1 4 2 3 (from the outside in) |
| **Diverge** | 3 2 4 1 (from the middle outwards, the note below first; three notes play 2 1 3, five play 3 2 4 1 5) |
| **Con & diverge** | 1 4 2 3 2 4 |
| **Pinky up** | 1 4 2 4 3 4 (the top note between each of the others) |
| **Pinky up-down** | 1 4 2 4 3 4 2 4 |
| **Thumb up** | 1 2 1 3 1 4 (the bottom note between each of the others) |
| **Thumb up-down** | 1 2 1 3 1 4 1 3 |
| **Alberti** | 1 4 2 4 (lowest, highest, middle, highest) |
| **Root first** | The notes in chord order from the root (root, third, fifth, seventh...) whatever the inversion, then round again. |
| **Chord** | Every note on every step. At one octave and the subdivision that is the Subdivided rhythm; over two octaves the steps alternate between the chord and the chord an octave up. |
| **Random** | A fixed random note per step; see below. |

Two entries have a second name: Thumb up is a pedal bass, and Converge is outside-in (the Pattern menu's tooltip
says so).

**Rate** is the length of a step: the meter's pulse, its subdivision (the default) or half the subdivision. The
buttons carry the note values for the meter:

| Meter | Pulse | Subdivision | Half the subdivision |
|---|---|---|---|
| 4/4 (and 2/4, 3/4, 5/4) | Quarters | Eighths | Sixteenths |
| 6/8 (and 9/8, 12/8) | Dotted quarters | Eighths | Sixteenths |
| 7/8 | Eighths | Sixteenths | Thirty-seconds |

**Range** is **1 oct** (the chord's voicing) or **2 oct**: the pattern plays one round on the voicing, the next
round an octave up, and so on, as Live's Arpeggiator does with Steps 1 and Distance +12 st. C major over two
octaves: Up plays C E G, then C E G an octave higher; Down plays G E C, then G E C an octave higher; Up-down
plays C E G E, then the same an octave higher. A round is the pattern's full length from the table above, so
Up & down over four notes climbs and falls once in each octave. Chord alternates octaves on every step, and Random
picks from the notes of both octaves.

**Differences from Live's Arpeggiator.** ChordLab's patterns follow Live's style list, with these changes:

- **Random** is fixed per chord. Live's Random picks again on every pass; ChordLab's order is worked out from the
  chord alone, so playback, export and every loop pass agree, undo and moving the chord keep its order, and a
  preset that adds new chords gives new orders. It never plays the same note twice in a row (a chord with
  at least two notes). Live's Random Other and Random Once are not offered.
- **Root first** takes the place of Live's Play Order. ChordLab's chords are not played in, so there is no
  played order; Root first orders the notes by chord tone, starting from the root.
- Live's gate, retrigger, repeats, pattern offset and velocity settings have no counterpart: the gate and
  accents are the ones under **Gate and accents** below. A new chord always starts its pattern from the
  beginning, in the lower octave (Live's Retrigger set to Note).
- **2 oct** is Live's transposition with Steps 1 and Distance +12 st, with the Shift mode; other step counts,
  distances and the Scale mode are not offered.

**Gate and accents.** In every rhythm but Block, a hit's notes last nine tenths of the time to the next
hit (or the end of the chord), so repeated notes are separate and a sustained sound restarts instead of
blurring; Block keeps the full length. The hit at the start of a chord or a bar plays at the chord's
velocity; another pulse plays at nine tenths of it, an off-pulse subdivision at eight tenths. Block keeps
the chord's velocity.

**Strum.** Off, **Down** (lowest note first), **Up** (highest first) or **Down and up** (hits on a pulse
or at the chord's start go down, the others up). Speed sets the gap between notes: **Tight**, **Medium**
or **Loose**, 10, 20 or 40 ticks (a tick is 1/960 of a beat; at 100 BPM that is about 6, 12 or 25 ms
per note). A strum never takes more than half the hit's length. Under the Arpeggio the Strum and Speed rows give
way to Pattern and Rate, and the engine ignores a strum there (an arpeggio hit has one note, or with the Chord
pattern the whole chord at once); the strum you set comes back when you choose another rhythm. Speed is greyed
while no strum plays.

**Humanise.** Off, **Light**, **Medium** or **Heavy**. Each hit moves as a whole by up to 5, 10 or 20
ticks either way (a chord's first hit only later, so a chord never starts before its place), each note's
velocity varies by up to 4, 8 or 12, and, with no strum, each note also moves by up to 1, 2 or 4 ticks.
With a strum on the notes keep their strum order. The variation is fixed, not random per play: the same
progression plays and exports identically every time. A chord keeps its variation when you move or edit it
or undo; a preset that replaces the progression gives new chords, so it changes.

**Every note stays inside its own chord.** Whatever the humanise or strum, a note never starts before its
chord, never starts later than 5 ticks before the chord's end, never sounds past the chord's end, and never overlaps the same
pitch's next note. A section therefore exports exactly what it plays inside the progression.

**Too dense.** If the feel would make more than 4096 note events in the progression (a note counts twice,
its start and its end; for example 65 one-bar four-note chords played Subdivided), or more than 96 events within
one beat, ChordLab drops the feel for the whole progression: it plays and exports as block chords, and the
status row says "This feel is too dense for the progression, so playback and export use block chords. Try a
simpler rhythm." Choose a simpler rhythm, or shorten the progression. The check counts across the loop
point whether or not Loop is on. With the chords of the palette (at most five notes), the 4096 limit is the
one you reach: for example the Arpeggio's Chord pattern at the fastest rate in 4/4 over 33 one-bar seventh chords.
The densest feel the palette's chords can make on the quarter-beat grid the sequence uses is the Chord pattern at
the fastest rate in 7/8 with five-note chords: 80 events a beat, under the per-beat limit.

**Supported range.** The 96-per-beat limit is chosen so Live never gets more than 96 progression events in
one audio block at up to 300 BPM with blocks up to 4096 frames at 44.1 kHz or higher. Outside that range
(faster tempos, bigger blocks, slower sample rates) a feel near the limit may drop notes.

## Tracks and the piano roll

Over the progression you can write up to four **tracks** of notes, such as a bass line or a lead, each in a
piano roll whose rows are shaded by how each pitch sits over the chord playing at that moment. Tracks play with
the chords and drag into Live as clips of their own.

> Not tested yet: modifier clicks, the right-click menu, pinch, and anything in Logic.

### Lanes

The lane tab row under the sequence bar holds **Chords**, then your tracks in order, then **+**, and **♫ All**
at the right end. Click a tab's name to open that lane. With **Chords** open the window is as described above:
suggestions, palette and home row. With a track open, its piano roll takes their place (the Heard group, which
lives in the palette's switch row, is hidden then; ChordLab still listens). The open lane is not saved: the
window always opens on Chords.

**+** offers **Bass**, **Lead** and **Other**. The new track is named after its role ("Bass", "Lead", or "Track"
for Other, with " 2", " 3" and so on when the name is taken) and opens at once. At four tracks **+** is greyed.

A track's **role** sets defaults, never what you may write: any note can go on any track. It decides:

- the name a new track gets;
- the register the roll shows the first time the track opens: Bass E1–G3, Lead C4–C6, Other C3–C5 (middle C is
  C4); changing the role later does not scroll the roll again;
- the colour of the tab's mark and of the notes: green for Bass, pink for Lead, lavender for Other.

Right-click a track tab for **Rename…** (an inline field: Return keeps the name, Escape cancels, a click
elsewhere keeps it; an empty name is a cancel, and names stop at 64 bytes), **Role** (Bass, Lead, Other, the
current one ticked), **Move left**, **Move right** and **Remove track**. Removing asks nothing; ⌘Z brings the
track back with its notes. Adding, removing, renaming, moving and changing a role are each one undo step.

Every tab has **M** (mute) and **S** (solo), the Chords tab too. With no solo anywhere, every lane that is not
muted plays; once any lane is soloed, only the soloed lanes play, muted or not. A lane that is not playing has
its tab dimmed. **M** and **S** change only what plays: previews (a palette press, a note click) still sound,
and every drag-out exports its lanes as written, muted or not. They are not undo steps, and they are saved with
the set.

Every lane plays on MIDI channel 1 with the chords, so in Live the bass, the lead and the chords all reach the
one receiving track and its instrument. To hear a line alone, solo it; to give it its own instrument while you
compose, see [Give a track its own instrument](#give-a-track-its-own-instrument); or drag its
clip onto another track (see [Getting tracks into Live](#getting-tracks-into-live)). The [feel](#feel) plays
the chords only: track notes play exactly as written. Where a track note and a chord note (or two notes) start on
the same pitch at the same time, they sound as one note, the longer and louder; where a note starts on a pitch
that is still sounding, that pitch restarts.

### The piano roll

Top to bottom:

- **The toolbar.** **Grid** (1/4, 1/8, 1/16, 1/32, 1/8T, 1/16T or Off; 1/8 at first) is the grid notes are
  drawn and moved on. It counts from each bar line, so in 7/8 a 1/4 grid starts again at every bar; Off still
  places notes on a 1/96-beat grid. **Snap** (pitch: Off, Scale, Chord + tensions, Chord tones; Off at first)
  keeps a drawn or moved note on the chord's notes, see [Harmonic guides](#harmonic-guides). **Notes | Velocity**
  picks what a drag does. **Generate** writes a bass or lead line (see [Generate a line](#generate-a-line)); in
  a window too narrow for its name it shows as a wand icon, with the same menu. The two magnifying glasses zoom
  time out and in. **View** chooses what the roll draws: which parts of the harmonic guides (see
  [Harmonic guides](#harmonic-guides)) and **Other tracks' notes** (on at first), the other tracks' notes as faint
  outlines in their own colours; they cannot be clicked. The four triangle buttons at the right end move the selected notes one grid
  step earlier or later, or a semitone up or down, like the arrow keys; each is greyed when the selection cannot
  move that way.
- **The chord ribbon**: bar numbers and a tick per pulse, the chord symbols over the stretch of time each chord plays,
  on their section colours, the chord boundaries, the dashed end line and the playhead.
- **The keyboard column** on the left: one key per row, each C labelled (C4 is middle C). Press a key to hear
  its pitch for as long as you hold it.
- **The grid**: the guide bands, bar, beat and grid lines, the chord boundaries, the notes, and after the end of
  the progression a darker hatched area where no note can start.

Grid, Snap, the mode, Other tracks, the zoom and the scroll are kept per track while the window is open, and not
saved. The first time a track opens, the roll shows bar 1 and the role's register.

**Scrolling and zooming.** The scroll wheel or two fingers on a trackpad scroll the grid both ways (⇧ with a
mouse wheel scrolls sideways); the ribbon and the keyboard column follow. ⌥ while scrolling zooms the rows
(taller or shorter) about the pointer, ⇧⌥ while scrolling zooms time about the pointer, and a pinch on a
trackpad zooms time too. The toolbar's magnifying glasses halve or double the zoom in time about the middle. The
grid scrolls one bar past the end of the progression, or past the last note when one runs beyond it.

### Editing notes

In **Notes** mode:

- **Draw.** Double-click on empty space to add a note, as in Live: it starts at the grid line at or before the
  click, on the row you clicked (moved to the nearest allowed pitch when Snap is on), with velocity 100, and
  plays briefly. Its length is the last length you drew or resized to, at first one grid step. The new note is
  selected. A single click on empty space adds nothing: it clears the selection and leaves the insert marker.
- **Select.** Click a note to select it; ⇧-click adds a note to the selection or takes it out. Drag on empty
  space for a rubber band: it selects every note it touches (with ⇧ held, added to the selection). Selected notes
  wear a white ring.
- **Move.** Drag a note to move it and every selected note with it: up and down by rows, sideways with the
  dragged note's start landing on the nearest grid line. The notes move by the same amount, so a chord shape
  survives, and stop together at the edges (the lowest and highest pitch, the start, the end of the progression).
  With Snap on, a single note snaps its pitch to the chord at its new place; a group moves by exact rows. The note
  sounds while you hold it, changing pitch as you move.
- **Resize.** Drag either end of a note (the cursor becomes ⇔ over an edge): the end lands on the nearest grid
  line, and every selected note changes by the same amount. A note never gets shorter than 1/32 of a beat.
- **Duplicate.** Hold ⌥ when you press on a note, then drag: copies of the selected notes go where you drop them
  and the originals stay.
- **Delete.** Double-click a note, or select notes and press Delete.

In **Velocity** mode a drag on a note changes how hard it plays instead: up is louder, one step per point, for
every selected note together (1–127). On release the note plays at its new velocity. Each note shows a light bar
along its top as long as its velocity, and a double-click on empty space adds nothing. In both modes a louder note is
drawn brighter.

**Right-click** (or ⌃-click) in the grid for **Copy**, **Cut**, **Paste here**, **Duplicate** and **Delete**.
On a note that is not selected, that note alone is selected first; otherwise the items act on the selection.
**Paste here** puts the copied notes at the grid line at or before the click.

**The insert marker** is a thin coloured line left by a click on empty space. ⌘V pastes there; a click on a note, a rubber band or a click after the end clears it.
Each ⌘V at the same marker pastes at the same place again.

**The keys**, after a click in the roll (see [Keyboard shortcuts](#keyboard-shortcuts) for which lane the keys
act on):

- Delete or Forward Delete removes the selected notes.
- ⌘C copies the selected notes (and their names, such as "C2 A2 F2", as text for other apps); ⌘X cuts them.
- ⌘V pastes at the insert marker; with no marker, right after the end of the selected notes; with neither, at
  the start of the progression. The spacing of the copied notes is kept, and notes copied from one track paste
  into any other, in this ChordLab or another.
- ⌘D duplicates the selected notes right after the latest end among them, spacing kept.
- ⌘A selects every note of the track.
- ↑ and ↓ move the selected notes a semitone; ⇧↑ and ⇧↓ an octave (a whole octave or not at all). ← and → move
  them one grid step (with Off, 1/96 of a beat); ⇧ changes nothing there. Holding an arrow moves once.

Pasted, duplicated and drawn notes become the selection. Each gesture, key and menu item is one undo step, and
the notes share the undo history with the chords: ⌘Z and ⇧⌘Z (or the key bar's buttons) undo and redo either,
in order, whichever lane is open.

### Harmonic guides

Behind the notes, each row is shaded over each chord by what that pitch is against the chord playing at that
moment. The shading follows the chords: a row can be strong over one chord and blank over the next. It is worked
out from the chord and the project key (not the browsed key), and it covers the stretch of time each chord
actually plays, as the ribbon shows it.

- **Root**: the chord's root.
- **Chord tone**: the chord's other notes.
- **Tension**: a note of the chord's scale that is not in the chord and does not rub against it, such as a 9th,
  or an 11th over a minor chord. The chord's scale is the project key's scale with the chord's own notes in
  place of the key's notes they alter: E7 in C major has G♯ where C major has G.
- **Avoid**: a scale note a semitone above a chord tone (F over C major), or a key note the chord has altered
  (G over E7 in C). Over a dominant seventh chord the key's ♭9, ♯9 and ♭13 count as tensions instead (F, G and
  C over E7 in C). Over a sus chord, the third it replaces is an avoid note (E over Csus4).
- **Outside**: everything else, the notes outside the chord's scale.

Each kind has its own colour, the same on every track: the **root** is gold, the other **chord tones** a light
ice blue, **tensions** aqua, **avoid** notes a dim grey, and outside notes are left dark. The strengths step down
in that order, so the notes that carry the chord stand out most.

At first only the root and the chord tones are coloured, with their labels: the rest stays dark so the roll is
calm. The toolbar's **View** menu switches each part on or off: **Root**, **Chord tones**, **Tensions**, **Avoid
notes** and **Degree labels** (and, below them, **Other tracks' notes**). The choice is the same for every track
and is remembered on this Mac. It changes only what is drawn: Snap still uses the whole guide.

On the root, chord-tone and tension rows each chord's stretch starts with a degree label: "R" on the root, "3",
"5" or "♭7" in bold on the chord tones, and the tension's number ("2", "6", "9", "13") a little lighter. The labels
show while the rows are at least 12 points tall (they are at first; ⌥-scroll zooms them).

Changing a chord's type, inversion or voicing, or the project key, changes the shading at once and never moves a
note.

**Snap** in the toolbar uses the same guide, judged by the chord at the note's start: **Scale** allows the
chord's scale, **Chord + tensions** the root, chord tones and tensions, **Chord tones** the root and chord tones
only. A note that is not allowed goes to the nearest allowed pitch (upwards on a tie). Snap applies when you draw
a note and when you drag a single note, not to a dragged group, the arrow keys, a paste or the generator.

### Notes follow their chords

Every note belongs to the chord it starts in (a note starting exactly on a chord's start belongs to that chord)
and keeps its distance from that chord's start. So when the progression changes, the notes go with their chords:

- **Inserting, moving or resizing a chord** moves the later chords and their notes with them. Notes keep their
  pitch and length; nothing stretches.
- **Shortening a chord** leaves the notes that now start after its end in place but silent: drawn as dashed
  outlines, neither played nor exported. Lengthen the chord again and they come back. A dashed note can be
  selected, moved (it then belongs to the chord it lands in) and deleted.
- **A note longer than its chord** keeps sounding into the next chord; playback stops it at the end of the
  progression.
- **Deleting a chord** deletes the notes that start in it, dashed ones included; ⌘Z brings everything back. A
  note that starts in an earlier chord and rings into the deleted one stays.
- **Duplicating chords (⌘D) or copying and pasting them** carries their notes, dashed ones included, onto the
  same tracks. Pasted into another ChordLab, each note goes to the track with the same name and role, or to a
  new track when there is none and fewer than four exist; when two tracks there have that name and role the
  paste is refused (`Lane "Bass" (bass) matches more than one track`).
- **Presets**: **Add to end** leaves the notes alone (the new chords have none). **Replace** removes every note
  (the tracks stay), and while any track has notes the choice reads **Replace progression and notes**.
- **Transposing**: with **Transpose with key**, **Move once**, **Move and always transpose**, or the block
  menu's **Transpose all chords**, the notes move by the same interval as the chords, in the same undo step. A
  note that would leave the MIDI range moves an octave less (it keeps its note name). A mode-only change moves
  nothing.
- Changing a chord's type, inversion or voicing, and any section edit, moves no note.

A chord edit that would push the notes it carries past the track limits is refused, and the bottom line says
why: "That change would leave more than 8 track notes sounding at once", "That change would leave more than 15
track notes starting within one beat" or, rarely, "That change would move a track note past the end of what
ChordLab can hold".

### Generate a line

**Generate** in the toolbar writes a line on the open track. What it offers follows the track's role: a **Bass**
track gets the six bass patterns, a **Lead** track the six lead patterns, and an **Other** track both, each in
its own submenu ("Lead line", "Bass line"). The menu's header says what it replaces ("Replace notes in all 4
chords", "Replace notes in the 2 selected chords", "Replace notes in the selected chord"), then the patterns. It works on the selected chords, or on every chord when none or all
are selected; with some chords selected, a second group below does the same for every chord. Choosing a pattern
replaces the track's notes that start in those chords (dashed ones too) with the new line and leaves every other
note alone. The roll then scrolls to the new notes if they are out of view. There is no confirmation: ⌘Z takes the whole line back in one step. On an empty progression the menu
is greyed ("Add chords first").

**Bass patterns.** Common to all six: the bass note is the lowest note of the chord as voiced (so its inversion, and Auto
voice-lead, count), placed between E1 and D♯2. Notes are placed on the meter's pulse (a quarter in 4/4, a dotted
quarter in 6/8, an eighth in 7/8) or subdivision (an eighth in 4/4 and 6/8, a sixteenth in 7/8), counted from the
bar lines of the whole progression: a chord that starts off that grid gets its first note at its own start. A
note on the chord's start or a bar line plays at velocity 100, another pulse at 90, a subdivision at 80. Notes
last nine tenths of the gap to the next one, unless the pattern says otherwise. Each chord starts its pattern
afresh.

| Pattern | What it writes |
|---|---|
| **Roots** | One note per chord, the bass note, lasting the whole chord, at velocity 100. |
| **Root pulse** | The bass note on every pulse. |
| **Root–fifth** | On the pulses, the bass note and the chord's fifth above it in turn, starting with the bass. A diminished chord takes its diminished fifth, an augmented one its augmented fifth. Under an inversion it alternates the actual bass and the fifth above it (C/E: E and G); when the fifth is itself in the bass (C/G), or the chord has no fifth, every note is the bass. |
| **Octaves** | On the subdivisions, the bass note and the octave above in turn. |
| **Follow feel** | The bass note wherever the progression's [feel](#feel) plays a note with the bass note's name, with that hit's timing, length and velocity (strum and humanise left out). With Block chords that is one note per chord at the chord's velocity; under an arpeggio, only the steps that reach that note. A chord where the feel never plays it gets no notes (its old ones still go). When the feel is too dense and the chords fall back to block chords, Follow feel writes Roots. |
| **Walking** | One note per pulse, each lasting until the next. It starts on the bass note and climbs through the chord's notes (extensions included) between E1 and E3, turning round at either end. When the chord has at least two pulses, the last one is a half step below the next chord's bass note (above it, F1, when that is the lowest E). After the last chord it leads into the first one if **Loop** is on when you generate, and simply walks on if it is off; the menu's last line says which. A chord one pulse long gets its bass note. |

**Lead patterns.** They write between C4 and C6 (Follow feel may go higher), use the same pulse and subdivision
grid and the same velocities (100 on a chord's start or a bar line, 90 on another pulse, 80 between), and the same
notes every time for the same chords: nothing is random. The line starts near C5, and in Held tone, Guide tones,
Arpeggio and Stepwise each chord carries on from the last note of the chord before, also when you generate for a
selection only. Loop makes no difference to a lead line.

| Pattern | What it writes |
|---|---|
| **Motif** | A short figure repeated in every bar and fitted to each chord: a rhythm of three to five notes per bar and a shape through the chord's notes, starting each bar on the chord note nearest C5. There are 64 of them, called takes. **Motif, take N of 64** writes the take named; **Motif, next take** writes the one after and moves on to it, so you can step through takes until one fits (⌘Z brings the previous notes back, not the take number). A loaded project starts again at take 1. |
| **Held tone** | One note per chord, lasting the whole chord: the chord note nearest the note before. |
| **Guide tones** | Two notes per chord, changing at the pulse nearest its middle: the chord's third and seventh (third and fifth on a triad; the 4 or 2 of a sus chord in place of the third), placed so the line moves by small steps. A chord too short to split gets one note. |
| **Arpeggio** | On the subdivisions, up through the chord's notes from where the last chord ended, turning round at the top and bottom of the range; each chord starts upwards again. |
| **Stepwise** | On the subdivisions, a scale run: chord notes on the pulses, moving in one direction until the range ends, and scale notes passing between them. A note that clashes with the chord is used only as a quick passing note between two that fit. |
| **Follow feel** | The chord's top note, one octave higher, wherever the progression's [feel](#feel) plays that note, with that hit's timing, length and velocity. With Block chords that is one note per chord. A chord where the feel never plays its top note gets no notes. When the feel is too dense and the chords fall back to block chords, Follow feel writes Held tone. |

Very short notes are left out: a pattern never writes a note under 1/32 of a beat. The result is ordinary notes:
edit them like any other. Nothing regenerates when you later change the chords, the feel or Loop; choose the
pattern again to rewrite the line. Generated notes are not selected. The track limits apply: a long progression
can pass 1024 notes per track with Octaves, Arpeggio, Stepwise or Motif, and the menu's choice is then refused
with the limit's message (a refused "next take" still moves on to that take, so the one after can be tried).

### Getting tracks into Live

- Each tab's **♫** grip drags that lane over the whole progression, whichever lane is open: the chords, or the
  track's own notes.
- **♫ All** drags one file with a lane for the chords and one for every track that has notes, muted lanes
  included.
- The bottom **♫** control drags the open lane, and a section's **♫** handle drags the open lane's part of that
  section, with the notes that cross the section's edges cut to it.

Each lane's clip holds that lane's own notes as written, so where a bass note restarted a chord note in playback
the chords clip still has the chord note at full length. Only sounding notes are exported: dashed notes are
not, and nor is anything after the end of the progression. A grip with nothing to export is dimmed and starts no
drag; resting the mouse on it says why ("Add notes to Bass to export", "Bass has no sounding notes to export").

Names: ChordLab names a track's clip after the track, and a track's section "Bass - Verse"; the chords keep
their usual name, the chord symbols (or the section's name).

What Live does (Live 12): a lane's clip takes its name from ChordLab ("Bass", "Bass -
Verse", the chord symbols); **♫ All** dropped on empty space makes one new MIDI track per lane, each with its
clip, muted lanes included; a clip's length is rounded up to whole bars. Live reads these names in an old Mac
text encoding, so a letter outside plain ASCII in a track or section name (ü, é, a dash typed as –) shows as
other characters in the clip's name; rename the clip in Live, or keep such names plain. Dropped on a track, a
file of several lanes spreads over that track and the tracks to its right (Live's status bar offers ⌘ for
separate tracks; seen with a hand-made file). In Arrangement view Live asks "Import tempo and time signature data
into Arrangement?" for every MIDI file: answer **No** (or tick "Don't show again") to keep the set's tempo.

### Track limits

| Limit | What you see | What to change |
|---|---|---|
| 4 tracks | **+** greyed; a chord paste that would need a fifth track: "At most 4 tracks" | Remove a track, or rename one so the paste matches it |
| 1024 notes per track, dashed ones included | "At most 1024 notes per track" | Delete notes, or move some to another track |
| 8 track notes sounding at once, all tracks together, muted ones included | "At most 8 track notes can sound at once" | Shorten the notes that overlap there |
| 15 note starts within any one beat, all tracks together, muted ones included | "At most 15 notes can start within one beat across all tracks" | Spread the notes out, or thin a fast passage on one of the tracks |
| The shortest note is 1/32 of a beat | Resizing stops there | — |
| A note must start inside the progression | "A note must start before the end of the progression" (a paste or duplicate that would land past the end) | Paste earlier, or lengthen the progression first |
| Notes need chords | "Add chords first"; Generate greyed | Add chords |

Dashed notes count only towards the 1024. The sounding and starting limits keep ChordLab's MIDI within what it
can send in one audio block. An edit based on notes that changed in the meantime is refused with "The notes
changed while you were dragging"; do it again. Every refusal leaves the notes as they were, and its reason shows
in the bottom line.

If the host reloads the project (a preset or state recall) at the very moment you act, ChordLab ignores that one
click or key instead of applying it to the newly loaded progression, and a drag started then offers no file and
says "The project was reloaded; drag again". The window shows the loaded progression; do it again there.

## Give a track its own instrument

Normally the chords, the bass and the lead all play out of one ChordLab, so they reach one instrument. Any other
ChordLab in the same Live set can instead play one track of that ChordLab (or its chords): the bass line on a bass
synth, the lead on a lead synth, the chords on a pad, each on its own instrument track, live and in time, while you
still write everything in one window.

> This works in the VST3 in Ableton Live only. The Audio Unit in Logic and the standalone app show none of it.
> In a set with one ChordLab nothing in this section appears.

Words used here: the **main ChordLab** is the one where you write the chords and tracks. A **second ChordLab** is
another ChordLab in the set that has been told to play one track of the main one. The main ChordLab stops playing
that track itself, so each line sounds once. To tell several main ChordLabs apart the guide and the window call
them **ChordLab A**, **ChordLab B** and so on. Only a ChordLab that others can play from gets a letter (a second
ChordLab has none), so a set with one main ChordLab always calls it **ChordLab A**. A main ChordLab keeps its
letter while the set is open, also when it is deleted and brought back with undo while a second ChordLab still
plays from it. The letters are only labels for reading (with several main ChordLabs, reopening the set can hand
them out differently), and the link does not depend on them.

### Set-up in Live with an Instrument Rack (recommended)

This needs one ChordLab track and one track per instrument: four tracks for chords, bass and lead.

1. Make a MIDI track and drop **ChordLab** on it, as in [Setting up in Ableton Live](#setting-up-in-ableton-live).
   This is the main ChordLab: write the chords and the Bass and Lead tracks in it as usual.
2. Select the ChordLab device and press ⌘G to group it into an **Instrument Rack**. Show the rack's chain list and
   name the chain **Chords**.
3. For each other line, duplicate the chain (⌘D) and name the copy after the line, for example **Bass** and
   **Lead**. A copy is a second ChordLab that starts with a copy of the main one's chords and tracks.
4. Open the ChordLab's window of each copy (from its device's title bar). It starts with a banner, "This ChordLab is a
   copy of ChordLab A. Make it play only one of its tracks:", with a button for **Chords** and one for each track.
   Click the track this chain should play. The window turns into a small panel that says what it plays, for
   example "Plays **Bass** of **ChordLab A**" (see below). In the main ChordLab the Bass tab now carries a small
   arrow and the main ChordLab stops playing the bass.
5. Make one MIDI track per instrument and put the instrument on it. On each:
   - **MIDI From**: the ChordLab track (Live lists it by number and name, for example `1-ChordLab`).
   - The chooser directly below it: the chain's entry. Live lists three entries for each chain, ending in
     Pre FX, Post FX and ChordLab; pick the one ending in **ChordLab**: `Instrument Rack | Chords | ChordLab` for
     the pad, `Instrument Rack | Bass | ChordLab` for the bass synth, `Instrument Rack | Lead | ChordLab` for the
     lead.
   - **Monitor**: **In**.
6. Press play. Each instrument plays its own line. Save the set: the chain's routing, the links and the arrows
   come back when you reopen it.

The main ChordLab's own chain, **Chords**, plays the chords and any track nobody else plays. Mute, solo, edits and
the key you change in the main ChordLab apply at once to every instrument. The second ChordLabs also follow the
main one's **Follow Live** and **Loop** switches.

### Set-up in Live with separate tracks

This works too and needs six tracks for three instruments: a ChordLab track and an instrument track for each part.

1. Chords as in [Setting up in Ableton Live](#setting-up-in-ableton-live): the main ChordLab on track 1, the pad on
   track 2 with **MIDI From** = `1-ChordLab`, the lower chooser = **ChordLab** and **Monitor** = **In**.
2. Drop a new **ChordLab** on a MIDI track of its own (track 3). Its window, empty, opens with the banner "Make this
   ChordLab play one track of ChordLab A:" and a button per track. Click **Bass**.
3. Make track 4 with the bass synth: **MIDI From** = `3-ChordLab`, lower chooser = **ChordLab**, **Monitor** =
   **In**.
4. The same for the lead on tracks 5 and 6, clicking **Lead**.

A ChordLab that already holds chords or tracks does not show the banner (apart from a fresh copy, above); use the
**Follow** menu in its transport row instead (see Changing or stopping).

### What a second ChordLab's window shows

Its window is a panel, not the editor: it plays, it does not edit. From the top it shows

- the headline: "Plays **Bass** of **ChordLab A**", or "Plays the **chords** of **ChordLab A**", with the track's
  colour mark (a track with no name shows its role's name). While the track is not coming through for a reason you
  must fix (waiting, deleted, another project) it reads "Set to play **Bass** of **ChordLab A**" instead;
- a status line, drawn in orange in those same cases, and under it the buttons **Change…** and **Stop following**;
- its own **Thru** switch and **Panic**, and the same status and error line the editor has at the bottom;
- "Edit in ChordLab A's window." and, if this ChordLab held chords or tracks of its own, "This ChordLab's own chords
  and tracks are kept. They come back when you stop following."; and a reminder of the Live routing: "On the
  instrument's track, set MIDI From to the track this ChordLab is on, choose this ChordLab below it (in a rack, by
  its chain's name), and set Monitor to In."

What the status line can say, and what to do:

| It says | What it means | What to do |
|---|---|---|
| "Playing." | The track is in the main ChordLab and audible. | Nothing. |
| "Bass is muted in ChordLab A (or another track is soloed)." | The track is silent on purpose: muted in the main ChordLab, or another lane is soloed there. | Unmute or unsolo it in the main ChordLab's window. |
| "Waiting for ChordLab A. It is not in this set, or it now plays from another ChordLab." | The main ChordLab was deleted or has not loaded yet, or it was itself set to play from another ChordLab. The link is kept and this ChordLab plays nothing meanwhile. | Undo the deletion, or wait for the set to finish loading; the track plays again by itself. Or press **Change…** (the button is highlighted) to pick another, or **Stop following**. |
| "Bass was deleted in ChordLab A. Undo there brings it back." | The track was removed in the main ChordLab. | ⌘Z in the main ChordLab's window brings the same track back and this one plays it again. Or **Change…**. |
| "ChordLab A holds a different project now." | Another project was loaded into the main ChordLab (for example a device preset saved from another set), so the old track number means nothing. | Press **Pick a track…** and choose again. |

Names in these lines come from the main ChordLab as it is now, so renaming a track there shows here at once. A
panel can also read "the other ChordLab" or "another ChordLab" where it cannot tell which one; the buttons still
work.

### Changing or stopping

- **Change…** opens a menu with each other ChordLab (by its label) and its lines: **Chords** and its tracks. A tick
  marks the line this ChordLab plays now. A line another ChordLab already plays says "(1 other ChordLab plays it)";
  you may still pick it, which puts two instruments on the one line.
- **Stop following** makes this ChordLab play its own chords and tracks again, and the main ChordLab plays that
  track itself again, at once.
- A ChordLab that is not playing another's track has a **Follow** menu in its transport row, after **Length**,
  whenever another ChordLab in the set has something to offer. It has the same entries. If other ChordLabs play
  tracks of this one, the menu's first (greyed) line says "Tracks that play from this ChordLab will stop.": a
  ChordLab that plays another's track cannot also hand out its own.
- Following is a setting like **Thru**, not an edit: it has no undo step and does not touch this ChordLab's own
  chords and tracks. **Stop following** is the way back.
- If the main ChordLab changed between the moment the menu or banner was drawn and your click, nothing happens and
  the window says "ChordLab A changed while you were choosing. Pick again." If this ChordLab's own project was
  replaced meanwhile (a preset loaded into it, or Live's undo of a big edit), the sentence is "This ChordLab changed
  while you were choosing. Pick again."; the same sentence follows a **Stop following** made from a list older than
  such a load. Pick again.
- On the banner, a long track name is shortened to 16 characters with "…" in the middle, and a track that does not
  fit on the banner's line is in **More…**, which opens the same menu as **Change…** (with the full names). Resting
  the mouse on a button says "Another ChordLab already plays it." or "2 other ChordLabs already play it." when
  another ChordLab plays that track.
- **Not now** hides the banner for this project in this ChordLab, and so does **Stop following**. It stays hidden
  when you close and reopen the window, until Live quits; loading another project into that ChordLab lets it offer
  again.

### In the main ChordLab

A track that another ChordLab plays carries a small arrow after its name on the lane tab (the Chords tab too). Rest
the mouse on it: "Not played here: another ChordLab plays it on its own instrument." (with more than one: "Not
played here: 2 other ChordLabs play it on their own instruments."). Mute, solo, editing and the drag grips work as
before; a muted track stays silent on its own instrument too.

### Duplicate, delete, undo, save

- **Duplicate a second ChordLab** (its chain or its track): the copy plays the same track, so two instruments can
  share one line.
- **Duplicate the main ChordLab's chain or track**: a new, independent main ChordLab holding a copy of everything and
  no second ChordLabs. Its window offers the banner to play one track of the original. This is how you add a second
  ChordLab in the rack.
- **Duplicate the whole rack or track group** with the main and its second ChordLabs: the copied second ChordLabs
  still play from the original main ChordLab. Each one's panel says "ChordLab C was copied together with this one,
  but this one still plays from ChordLab A." with a button "Play Bass of ChordLab C instead" (it names the copy's
  track, shortened to 16 characters if long; when the copy has no matching track it reads "Play a track of ChordLab
  C instead" and opens a small menu of the copy's tracks). ChordLab offers this only when the copies were made
  within about two seconds of each other, as one paste; if they were made apart, use **Change…**.
- **Delete the main ChordLab** (its chain or track): the second ChordLabs show "Waiting for ChordLab A…" and play
  nothing; undo brings the main ChordLab back and they play again. **Delete a second ChordLab**: the main ChordLab
  plays that track itself again.
- **Undo in the main ChordLab** changes the main ChordLab's chords and tracks as always; the second ChordLabs play
  whatever it holds now.
- **Save and reopen the set**: each second ChordLab saves which ChordLab and which track it plays, so the set reopens
  linked, whichever of the two comes first in track order. Following marks the set as modified, so ⌘S saves it. The
  instrument tracks' MIDI From choices are Live's own and come back with the set. Reordering the chains of the rack
  does not disturb them (the choice follows the chain). Deleting a chain sets the
  instrument track that listened to it to **No Input**; Live's undo brings the chain, the choice and the line back.
- The banner is for a ChordLab you have just added or duplicated. After the set has been saved and reopened, a
  duplicated ChordLab that still plays nothing does not show it again (it is no longer a copy); use the **Follow**
  menu. In a window shorter than 700 pt the banner waits while the key-change hint shows.

### Limits and cautions

- **VST3 in Ableton Live only.** Not the Audio Unit in Logic Pro, not the standalone app. All the ChordLabs must be
  in the one Live set you have open; a ChordLab in another program, or another plug-in format, is not seen.
- **Changing a link while the song plays** (picking a track, **Stop following**, a deletion) can leave the track
  silent, or sounding on both instruments, for a moment: the few milliseconds the main ChordLab needs to recompute,
  plus one audio block, before it is exact. Nothing is left hanging. With the transport stopped the change is exact.
- **Notes can hang if a ChordLab device is switched off while a note sounds.** Live stops calling the plug-in, so the
  note is never ended; it sounds on its instrument until you switch the device on again. Press **Panic** in that ChordLab's window if it still rings.
- **Switching the main ChordLab's device off, or bypassing it, does not silence the second ChordLabs.** Each
  device's switch governs its own output (with the main ChordLab's device off, a
  second ChordLab's line plays on without a break). Deleting the main ChordLab does stop
  them.
- **Auditions sound on the track's own instrument** (not tested by hand yet: a held note and Panic). A note pressed in a
  track's piano roll (a key of its keyboard column, a note you press, draw or move) sounds on the instrument of the
  second ChordLab that plays that track, and not on the main ChordLab's. A palette cell sounds where the chords play:
  on the main ChordLab's instrument, or on a second ChordLab's if one plays the chords. A track nobody else plays
  sounds on the main ChordLab's instrument as before. **Panic** in the main ChordLab also silences what it sent to
  the others (it silences everything those ChordLabs sound for a moment; their lines go on with the next notes).
  Closing the main ChordLab's window ends any audition it started.
- **Keyboard shortcuts in a ChordLab that plays another's track go to Live.** While the panel shows, ChordLab's keys
  (⌘Z, ⌘C, ⌘V, ⌘D, ⌘A, Delete, the arrows and the digits) do nothing in ChordLab, since there is nothing in the panel
  for them to change; Live gets them. For the same reason nothing from the hidden editor can change or sound its
  chords either (a menu that was still open, a rename in progress): only **Thru**, **Panic** and the end of a note
  already sounding get through.
- **Freezing.** Live refuses to freeze either kind of track in this set-up (Live 12). The
  ChordLab track, because other tracks take MIDI from it: "Track … cannot be frozen because of the routing". An
  instrument track, because it listens to another track: "Track … cannot be frozen because its monitoring state is
  set to In." To commit a line to its instrument track, arm that track, record a pass into a clip (the clip holds
  exactly the line's notes), set the track's **Monitor** to **Auto**, and freeze it then. Set **Monitor** back to
  **In** to hear ChordLab's line live again.
- **Exporting audio** (File > Export Audio/Video) works: the export has every instrument's line from the first bar,
  also straight after opening the set with no ChordLab window opened, equal to a real-time
  recording.
- **Timing between instruments.** Measured in Live 12: the main ChordLab and a second
  ChordLab, and two second ChordLabs, feeding two instruments with the same line sounded identical to the sample at
  buffer sizes 64, 256 and 512, also with plug-in latency on one of the tracks.
- A saved rack preset or track group dragged into another set has not been checked; if it brings a second copy of
  the main ChordLab along, the copied second ChordLabs offer the switch described above.

## Playing it back

- Turn on **Follow Live** and start Live's transport. The progression's first beat sits at the start
  of the arrangement; the playhead shows where you are.
- **Loop** repeats from the start; otherwise the progression plays once.
- The tracks play with the chords, by the lanes' **M** and **S** toggles, all on MIDI channel 1 (see
  [Lanes](#lanes)). They never make the progression longer: a note running past the end stops there.
- **Panic** stops every note at once, for example after a stuck note on the receiving instrument.
- Cell previews work whether or not Live is running and whether or not Follow Live is on.

## Listening

ChordLab listens to the MIDI that reaches its own track: a keyboard you play, the notes of another track,
or a clip on the ChordLab track. It shows what it hears in the **Heard** group above the palette (see
[Palette](#palette)): the key the notes fit, with **Use** to make it the project key, and the chord you
hold, with **Add** to append it to the progression. Nothing changes until you press one of them. With
**Thru** on (the default) the notes also go out of ChordLab, with the progression and the previews, so the
instrument on track B plays what you play.

Three ways to feed it (track A holds ChordLab, track B the instrument, set up as in
[Setting up in Ableton Live](#setting-up-in-ableton-live)):

1. **Play your keyboard.** Arm track A (or set its **Monitor** to **In**), with **MIDI From** on your
   keyboard or *All Ins*. Play: the Heard group appears with the first note, and with Thru on track B
   plays along.
2. **Listen to another track.** On track A set **MIDI From** to the track whose notes ChordLab should hear
   (in the second chooser, *Pre FX* takes that track's clips and input before its devices) and set track A's
   **Monitor** to **In**. Play that track's clip or instrument.
3. **Play a clip on the ChordLab track.** Put a MIDI clip on track A itself and play it. ChordLab hears the
   clip's notes; with Thru on track B plays them too, together with the progression when Follow Live is on.

Each ChordLab instance hears only its own track.

**The key.** ChordLab weighs the notes of the last 32 beats at Live's tempo (also while the transport is
stopped): the longer a note sounds, the more it counts, up to 4 beats, and every note started in that time
adds a little more. It compares the result with the major and minor key profiles of Krumhansl and Kessler
and shows a key once about six notes have started in those 32 beats and one key fits clearly better than
any other (a fit of at least 60 %). In practice that is a few bars of playing; a scale or a few chords in
one key is enough. Until then the group reads "listening · 5 notes". The key keeps updating as you play:
another key replaces it only when it fits clearly better (so it does not flicker between a major key and
its relative minor), and it goes away once fewer than six notes are left in the last 32 beats or the fit
drops below 60 %. Only major and minor keys are recognised, named as the Key picker names them (Major,
Minor). **Use** does exactly what choosing that key in the Key pickers does: the browsed key comes home,
Transpose with key (on: the chords move; off: its one-time hint) behaves as after a pick, and ⌘Z undoes
it. The note and chord names in the group follow the project key, so after **Use** they are respelled in
the new key: an A♭ triad played while the project key is E Major reads G♯, and A♭ once the key is A♭
Major.

**The chord.** When you press a key and at least two different notes are held, ChordLab looks for the chord
in its chord list made of exactly those notes, whatever their octaves or doublings: nothing missing, nothing
extra. When the same notes have several names, the one whose root is the lowest note wins, then the one with
fewer notes: A–C–E–G with A lowest reads Am7, with C lowest C6. A fifth alone reads as a power chord (C5).
Notes that make no chord in the list leave the last chord shown. Lifting a finger never changes the chord
(Am7 without its A still reads Am7), and the chord stays after you let go of all the keys, until you play the
next chord or press ×; once released it is drawn dimmed. **Add** works whether the chord is still held or
already released, so you can play a chord, let go and then click **Add**. The chord is appended one bar
long, selected, in the inversion your lowest note gave it (not the octave or the spacing), and marked
manual so Auto voice-lead keeps that inversion; ⌘Z removes it.

**The sustain pedal** (CC64, down from 64): a note released while the pedal is down stays held until the
pedal comes up, for the chord and for the key.

**Thru.** Note-ons and note-offs pass with their velocity; the pedal, pitch bend and other controllers do
not. A note you play on a pitch the progression (or another note you pass through) is already sounding
restarts it, and the pitch stops only when everything holding it has let go, so nothing is cut short and
nothing hangs. Turning Thru off releases the notes you are holding; **Panic** silences them with everything
else. Bypassing the device in Live while you hold a note releases it when the device comes back on.

**With the window closed** ChordLab keeps listening; open the window and the group shows the key and the last
chord it heard. When Live deactivates and reactivates the plug-in (for example after an audio device or
sample-rate change), ChordLab forgets what it heard and the group hides until the next note.

## Getting the MIDI into Live

Drag the **♫ … drag to Live** control (bottom right) onto a MIDI track. It lands as a MIDI clip at
Live's tempo, with the voicings and the [feel](#feel) you hear. The file's time signature is the
document's meter (6/8 for a 6/8 progression). The **♫** at the right end of a section span exports that
section alone, with the same feel and meter, as the section plays inside the progression. The clip is a
plain `.mid` file, so it also drops into any other DAW or the Finder.

Both controls drag the **open lane**: the chords with Chords open, the track's own notes with a track open. Each
lane tab's **♫** drags that lane whichever is open, and **♫ All** drags the chords and every track with notes as
one file, one clip per lane. See [Getting tracks into Live](#getting-tracks-into-live), which also covers the
question Live asks about tempo when you drop a file in Arrangement view.

## Keyboard shortcuts

| Keys | Chords targeted | A track's notes targeted |
|---|---|---|
| ⌘Z / ⇧⌘Z | Undo / redo (one history for chords and notes) | The same |
| 1 – 8 (top row or keypad) | Append suggestion chip 1–8 (nothing while a track is open) | Nothing |
| ⌘C / ⌘X / ⌘V | Copy / cut / paste the selected chords (paste goes after the anchor) | Copy / cut / paste notes (paste at the insert marker, else after the selected notes, else at the start) |
| ⌘D | Duplicate the selected chords after them | Duplicate the selected notes after them |
| ⌘A | Select every chord | Select every note of the track |
| Delete, Forward Delete | Remove the selected chords | Remove the selected notes |
| ↑ / ↓ | Left to Live | Move the selected notes a semitone up / down |
| ⇧↑ / ⇧↓ | Left to Live | Move the selected notes an octave up / down |
| ← / → (with or without ⇧) | Left to Live | Move the selected notes one grid step earlier / later |
| Return / Escape | Commit / cancel a section or track rename | The same |

**Which lane the keys act on.** A click in a track's piano roll (a right-click too) makes the keys act on that
track's notes. A click on the sequence bar, a palette or home-row cell, or a suggestion chip makes them act on the
chords again, and so does opening another lane: after clicking a track's tab, Delete still removes the selected
chords until you click in the roll. While the chords are targeted ChordLab leaves the arrow keys to the host.
While a track is open the suggestion chips are not on screen, so the digits do nothing, whichever is targeted.

These keys belong to ChordLab whenever its window has the keyboard and your last click in that window
was inside ChordLab — click anywhere in it first — and no section or track name is being edited. A key ChordLab
knows is used by ChordLab even when there is nothing to do (Delete with no chord selected does nothing);
holding a key down does not repeat it (the one exception is a key whose repeats reach ChordLab more
than two seconds apart, which it cannot tell from a new press); every other key goes to the host, so Space should still start
Live. If a key does nothing in Live after clicking in the window, use the chips and buttons and tell us
which key. If ⌘C, ⌘V, ⌘D or ⌘A does nothing in Live, use the right-click menus. Live 12 hands the arrow keys to the
plug-in window; if they do not move notes in the roll, use the triangle buttons at the right end of the roll's toolbar.

## What is saved

Saved with the Live set, per instance: the progression (chords, lengths, velocities, voicings), sections,
the project key, the browsed key, Follow Live, Loop, Thru, Auto voice-lead, the Relations mode, the feel and the
meter, the new-chord length (the **Length** menu with nothing selected), the tracks (their names, roles, order
and notes) and every lane's **M** and **S**, the Chords lane's included, and the window size. Not saved: the selection (of chords and of notes), Record (off whenever the window opens) and the undo history, which start empty when the
set loads, and what ChordLab heard: the Heard group starts hidden and fills again from the next note. Nor are the
open lane (the window opens on Chords), the insert marker, or each roll's Grid, Snap, mode, Other tracks, zoom
and scroll, which last while the window is open. A second ChordLab (see
[Give a track its own instrument](#give-a-track-its-own-instrument)) also saves which ChordLab and which track it
plays; the main ChordLab saves nothing about it.

Saved per user, shared by every instance and every set: the **Labels** choice and the palette's
**Diatonic / Applied** view.

In Live, any change to what is saved (a chord, a length, the new-chord length, the key, a toggle, the feel, a
track, a note, a lane's M or S) marks the set as modified, so ⌘S writes it and Live asks before closing or opening
another set, even when nothing else in the set changed. Selecting blocks or notes, scrolling or zooming a roll and
resizing the window do not. Logic Pro gets no such signal from an Audio Unit (see
[Limits and troubleshooting](#limits-and-troubleshooting)).

## Limits and troubleshooting

- **No sound.** ChordLab makes none itself. Check the receiving track: MIDI From = the ChordLab track,
  second chooser = ChordLab (not Pre FX), Monitor = In or the track armed. Check the instrument on
  that track plays when you click its own keys.
- **Digit keys or Delete do nothing.** Click inside the ChordLab window first. If they still do nothing,
  Live may be keeping them: use the chips and buttons, and tell us which key.
- **A power chord sounds like one note.** The top palette row holds power chords: root and fifth only,
  two notes that blend into one sound on many instruments. Press a cell in the bottom row (the triads)
  for a full chord.
- **Nothing plays with the transport.** Follow Live must be on, and the progression must not be
  empty. Without Loop, the progression is silent once Live's position passes its end.
- **The Heard group never shows.** No MIDI reaches the ChordLab track: arm it or set its Monitor to In, and
  check its MIDI From (see [Listening](#listening)).
- **Logic does not ask to save after ChordLab edits alone.** Audio Units have no way to tell Logic that their
  state changed, and ChordLab has no parameters Logic could watch. Save with ⌘S before closing the project; if
  Logic treats the project as unchanged, change anything in it first (move a region, touch a fader). The VST3 in
  Live has no such limit.
- **The sustain pedal is ignored, or lands early.** In the VST3 the pedal arrives only on MIDI channel 1,
  and Live hands it over once per audio block, ahead of that block's notes: a pedal change applies before
  every note of its block (at most one block early, about 12 ms at a 512-sample buffer and 44.1 kHz), and
  several changes within one block count as the last of them. The standalone app delivers the pedal in
  order.
- **The Heard group vanished while I played.** ChordLab takes up to 256 incoming notes and pedal changes
  between two reads of its input (normally every 20 ms); a burst larger than that makes it forget everything
  it heard, so that no note can stay held by a lost note-off. The group hides and comes back with the next
  note.
- **Thru notes are slightly late in the standalone app.** The app sends every note passed through at the
  start of its audio block, not at the exact sample. In Live they keep their timing; only more than 64
  incoming notes in one audio block wait for the next block.
- **Logic: ChordLab is missing from MIDI FX.** Full Audio Unit Reset in the Plug-in Manager, then reopen
  Logic; `auval -v aumi pozR Ster` in Terminal should print AU VALIDATION SUCCEEDED.
- **Logic: bypass forgets.** Bypassing the plug-in in Logic resets it: while it is bypassed nothing is heard
  or passed through and the window holds its last state; when the bypass ends the Heard group clears and the
  progression chases back in (Live's Device Off behaves differently, see **Thru** under [Listening](#listening)).
- **Logic: the window can get smaller than 700 × 632.** The minimum size holds in Live but is not enforced
  for the Audio Unit; if Logic lets you shrink the window further, part of the palette may not fit.
- **The bass plays on the chords' instrument.** Unless another ChordLab plays it (see [Give a track its own
  instrument](#give-a-track-its-own-instrument)), every lane plays on MIDI channel 1 into the one receiving track.
  Solo the track to hear it alone, or drag its clip to a track of its own (see [Lanes](#lanes)).
- **A note edit, a paste, a generated line or a chord change is refused.** A track limit would be passed; the
  bottom line names it. See [Track limits](#track-limits) for each message and what to change.
- **Notes went dashed and silent.** Their chord got shorter than their place in it; lengthen the chord and they
  play again, or move them into the chord (see [Notes follow their chords](#notes-follow-their-chords)).
- **A click in the roll adds no note.** Notes are drawn with a double-click; a single click on empty space
  clears the selection and sets the insert marker.
- **Delete removed chords, not notes.** The keys act on the chords until you click in the roll after opening a
  track (see [Keyboard shortcuts](#keyboard-shortcuts)). ⌘Z brings the chords back.
- **Not yet in this version.** Tracks play on one MIDI channel with the chords unless a second ChordLab plays them
  (an instrument per track while composing works in the VST3 in Live only, see [Give a track its own
  instrument](#give-a-track-its-own-instrument)), the feel and humanise do not apply to them, there is no melody generator beyond the bass and lead patterns, no fitting
  chords to notes, and no importing a MIDI file or recording incoming MIDI into a track; four tracks at most.
  Listening recognises major and minor keys
  only, never changes the key by itself (only **Use** does), does not turn a clip into a progression, passes
  notes only (no pedal, pitch bend or other controllers), and merges every MIDI channel (no MPE). Chords are contiguous,
  with no gaps or rests between them. No swing, per-section or per-chord feel, custom patterns or
additive meter groupings (7/8 is seven eighth pulses), and Live's own time signature is not read. One
suggestion style (pop). The Applied
  view has secondary dominants and leading-tone sevenths only (no tritone substitutes or ii–V pairs).
  macOS only (VST3, Audio Unit, app).
