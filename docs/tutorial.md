# Tutorial: a first session in Ableton Live

In this session you build a short loop in Ableton Live: a chord progression with a feel, then a bass line and a
lead line, with each part playing its own instrument, and finally clips in the arrangement. It takes about half an
hour. Each step names only what you need; the [user guide](user-guide.md) has the details behind every control.

You need Ableton Live 12 and the ChordLab VST3 installed (see the [README](../README.md#install)). In Logic Pro,
set ChordLab up as in [Setting up in Logic Pro](user-guide.md#setting-up-in-logic-pro) instead of step 1. Steps
2–8 then work much the same: **Follow Live** follows Logic's transport. Steps 9 and 10 are specific to Live.

## 1. Hear a chord

ChordLab makes no sound itself: it sends MIDI to an instrument on another track.

1. Create a MIDI track (track A) and drop **ChordLab** on it from Plug-Ins → VST3.
2. Create a second MIDI track (track B) with an instrument, for example a piano or a pad.
3. On track B, set **MIDI From** to track A. In the chooser below it, pick **ChordLab** instead of *Pre FX*.
4. Set track B's **Monitor** to **In**.
5. In ChordLab's window, press and hold a chord in the palette (the grid of circles in the lower half). Track B
   plays it for as long as you hold it.

If nothing sounds, check those three settings on track B first. More in
[Setting up in Ableton Live](user-guide.md#setting-up-in-ableton-live).

## 2. Pick a key

Set the **Key** pickers at the top left to **C** and **Major**. The palette shows C major's chords, one column per
scale degree, with triads at the bottom and richer chords above them. Each chord shows its symbol over its roman
numeral and function: `C` over `I · T`.

A filled circle is a chord made only of notes in the key; a ring borrows at least one note from outside it. The
`<` and `>` buttons in the key bar step the palette a fifth down or up while your project stays in C, and
**Home** brings it back. See [Key bar](user-guide.md#key-bar) and [Palette](user-guide.md#palette).

## 3. Start a progression

1. Click **Presets** in the transport row. At the top of the list choose **Replace progression**, then click
   **Axis** (I – V – vi – IV) under Pop. The sequence bar now holds C – G – Am – F, one bar each, in a section
   named after the preset.
2. Click the **Am** block. The **Next** strip above the palette ranks up to eight chords that could follow it,
   and the palette cells grow or shrink by how related they are to Am. Rest the mouse on a chip to see why it is
   suggested.
3. Click a chip, or press its number key, to append that chord. Drag a palette cell onto the sequence bar to append
   any other chord.
4. Rest the mouse on a block to read how it relates to the chord before it: root motion, common tones,
   voice-leading cost.
5. To tidy up, drag a block sideways to reorder it, drag its right edge to change its length, or select it and
   press Delete. ⌘Z undoes any of it.

To borrow a chord, use the mode menu between `<` and `>` to switch the palette to **Minor**. That gives the chords
of C minor, still in your C major song. Drag **A♭** onto the sequence (it shows as a ring), then press **Home**.

See [Presets](user-guide.md#presets), [Sequence bar](user-guide.md#sequence-bar) and
[Chord relations](user-guide.md#chord-relations).

## 4. Play it with Live

Turn on **Follow Live** and **Loop** in the transport row and start Live's transport. The progression plays from
the start of the arrangement, locked to Live's tempo, and a playhead moves along the sequence bar.

Turn on **Auto voice-lead** to let ChordLab pick inversions so each chord moves smoothly into the next. If a note
ever hangs on the instrument, press **Panic**.

## 5. Give it a feel

Click **Feel** in the transport row:

- set **Rhythm** to **Arpeggio**, **Pattern** to **Up-down** and **Range** to **2 oct**, or
- set **Rhythm** to **Pulse** and add some **Strum**;
- add a little **Humanise** either way.

The progression plays with the feel at once, and a clip you drag out later holds the same notes. One feel applies
to the whole progression. See [Feel](user-guide.md#feel).

## 6. Play along (optional)

ChordLab also listens to the MIDI on its own track. Set track A's **MIDI From** to your keyboard and arm the track
(or set its **Monitor** to **In**), then play a few chords.

The **Heard** group above the palette names the chord you hold and, after a few bars, the key your playing fits.
**Add** appends the chord you played to the progression, and **Use** makes the heard key the project key. With
**Thru** on, your playing also reaches track B's instrument. See [Listening](user-guide.md#listening).

## 7. Add a bass line

1. In the tab row under the sequence bar, click **+** and choose **Bass**. A **Bass** tab opens, and a piano roll
   takes the palette's place.
2. Click **Generate** in the roll's toolbar and choose **Walking**. A walking bass line fills every chord. Try
   **Root–fifth** or **Octaves** too: each choice replaces the line, and ⌘Z brings the last one back.
3. Look at the shading behind the notes. In each chord's stretch, the root's row is gold and the other chord tones'
   rows are light blue, labelled "R", "3", "5". **View** in the toolbar turns on tensions and avoid notes as well.
4. Edit by hand:
   - Double-click empty space to draw a note, and double-click a note to delete it.
   - Drag a note to move it, or drag either end to resize it.
   - Set **Snap** to **Chord tones**, and the notes you draw or drag land on the chord.

The bass plays along with the chords straight away, on the same instrument for now. See
[Tracks and the piano roll](user-guide.md#tracks-and-the-piano-roll) and
[Generate a line](user-guide.md#generate-a-line).

## 8. Add a lead

1. Click **+** again and choose **Lead**.
2. Choose **Generate → Motif, next take**: a short figure, repeated each bar and fitted to each chord. Choose
   **Motif, next take** again to step through the 64 takes until one fits. **Guide tones** and **Stepwise** are
   worth a listen too.
3. Click the **Chords** tab and drag a chord block to a new place, or make it longer. The bass and lead notes that
   start in a chord move with it. ⌘Z puts it back.
4. Every tab has **M** (mute) and **S** (solo). Solo the lead to hear it alone.

See [Notes follow their chords](user-guide.md#notes-follow-their-chords) and [Lanes](user-guide.md#lanes).

## 9. Give each part its own instrument

So far the chords, bass and lead all reach track B's instrument. To play the bass on a bass synth and the lead on a
lead synth, while you keep writing everything in one window:

1. Select the ChordLab device on track A and press ⌘G to group it into an **Instrument Rack**. Show the rack's
   chain list and name the chain **Chords**.
2. Duplicate the chain twice (⌘D) and name the copies **Bass** and **Lead**. Each copy is a second ChordLab.
3. Open each copy's window (from its device's title bar). A banner offers the tracks of the first ChordLab: click
   **Bass** in the Bass chain's copy and **Lead** in the Lead chain's copy. Each window turns into a small panel,
   for example "Plays **Bass** of **ChordLab A**".
4. Create two MIDI tracks, one with a bass synth and one with a lead synth. On each, set **MIDI From** to track A
   and **Monitor** to **In**. In the chooser below MIDI From, pick the matching chain's entry:
   `Instrument Rack | Bass | ChordLab` or `Instrument Rack | Lead | ChordLab`.
5. On track B, change the same chooser to `Instrument Rack | Chords | ChordLab`.

Press play: each instrument plays its own line. Keep editing in the first ChordLab's window, and every change
reaches all three instruments at once.

This works in the VST3 in Ableton Live only. See
[Give a track its own instrument](user-guide.md#give-a-track-its-own-instrument), which also covers a set-up
without a rack and what to do when a panel reports a problem.

## 10. Put it in the arrangement and save

- Switch Live to Arrangement view and drag **♫ All** (at the right end of the tab row) onto empty space below
  the tracks. Live makes one MIDI track per lane, each with its clip. When Live asks whether to import tempo and
  time signature, answer **No**.
- Each tab's own **♫** drags just that lane, and a section's **♫** handle drags just that section.
- Save the set. The chords, tracks, feel, keys and the links between ChordLabs are saved with it.

See [Getting tracks into Live](user-guide.md#getting-tracks-into-live) and [What is saved](user-guide.md#what-is-saved).

## Where next

- [Keyboard shortcuts](user-guide.md#keyboard-shortcuts), including copy and paste of chords and notes.
- [Sections](user-guide.md#sequence-bar): named, coloured spans you can export one by one.
- [Harmonic guides](user-guide.md#harmonic-guides): what each shade in the piano roll means.
- [Limits and troubleshooting](user-guide.md#limits-and-troubleshooting).
