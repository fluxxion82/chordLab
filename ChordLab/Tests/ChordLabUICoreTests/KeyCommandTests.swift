import XCTest
@testable import ChordLabUICore

final class KeyCommandTests: XCTestCase {
  private func input(_ key: KeyInput.Key, command: Bool = false, shift: Bool = false,
                     option: Bool = false, control: Bool = false) -> KeyInput {
    KeyInput(key: key, command: command, shift: shift, option: option, control: control)
  }

  /// The map with the chords targeted (every chord shortcut before tracks existed goes through here).
  private func chords(_ input: KeyInput) -> KeyCommand? {
    KeyCommandMap.command(for: input, target: .chords)
  }

  /// The map with track 3's notes targeted.
  private func notes(_ input: KeyInput) -> KeyCommand? {
    KeyCommandMap.command(for: input, target: .notes(trackId: 3))
  }

  /// The VST3 adapter with the given text and virtual key, no modifiers unless given.
  private func vst3(_ characters: String, _ virtualKey: Int, shift: Bool = false, command: Bool = false,
                    option: Bool = false, control: Bool = false) -> KeyInput {
    KeyInput(characters: characters, virtualKey: virtualKey, shift: shift, command: command, option: option,
             control: control)
  }

  private let arrows: [ArrowDirection] = [.left, .right, .up, .down]

  // MARK: - The map (chords targeted)

  func testBareDigits1To8AppendThatSuggestion() {
    for n in 1...8 {
      XCTAssertEqual(chords(input(.character(Character(String(n))))),
                     .appendSuggestion(n), "digit \(n)")
    }
  }

  func testDigits9And0AreNotShortcuts() {
    XCTAssertNil(chords(input(.character("9"))))
    XCTAssertNil(chords(input(.character("0"))))
  }

  func testOnlyASCIIDigitsAreShortcuts() {
    XCTAssertNil(chords(input(.character("٢"))), "Arabic-Indic two")
    XCTAssertNil(chords(input(.character("²"))), "superscript two")
  }

  func testDigitsWithAnyModifierAreNotShortcuts() {
    XCTAssertNil(chords(input(.character("2"), command: true)))
    XCTAssertNil(chords(input(.character("2"), shift: true)))
    XCTAssertNil(chords(input(.character("2"), option: true)))
    XCTAssertNil(chords(input(.character("2"), control: true)))
  }

  func testDeleteAndForwardDeleteRemoveTheSelection() {
    XCTAssertEqual(chords(input(.delete)), .deleteSelection)
    XCTAssertEqual(chords(input(.forwardDelete)), .deleteSelection)
    XCTAssertNil(chords(input(.delete, command: true)), "⌘Delete is not ours")
    XCTAssertNil(chords(input(.delete, control: true)))
    XCTAssertNil(chords(input(.forwardDelete, option: true)), "⌥⌦ is not ours")
  }

  func testCommandZUndoesAndShiftCommandZRedoes() {
    XCTAssertEqual(chords(input(.character("z"), command: true)), .undo)
    XCTAssertEqual(chords(input(.character("Z"), command: true, shift: true)), .redo)
    XCTAssertEqual(chords(input(.character("z"), command: true, shift: true)), .redo,
                   "the character may arrive lower-case with shift held")
    XCTAssertNil(chords(input(.character("z"))), "bare z is nothing")
    XCTAssertNil(chords(input(.character("z"), command: true, option: true)))
    XCTAssertNil(chords(input(.character("z"), command: true, control: true)))
  }

  func testCommandLettersEditTheStrip() {
    let expected: [(Character, KeyCommand)] = [("c", .copy), ("x", .cut), ("v", .paste), ("d", .duplicate), ("a", .selectAll)]
    for (letter, command) in expected {
      XCTAssertEqual(chords(input(.character(letter), command: true)), command, "⌘\(letter)")
      XCTAssertEqual(chords(input(.character(Character(letter.uppercased())), command: true)), command,
                     "⌘\(letter) reported upper-case")
      XCTAssertNil(chords(input(.character(letter), command: true, shift: true)), "⇧⌘\(letter) is the host's")
      XCTAssertNil(chords(input(.character(letter), command: true, option: true)), "⌥⌘\(letter)")
      XCTAssertNil(chords(input(.character(letter), command: true, control: true)), "⌃⌘\(letter)")
      XCTAssertNil(chords(input(.character(letter))), "bare \(letter)")
    }
    XCTAssertNil(chords(input(.character("b"), command: true)), "⌘B is not ours")
  }

  func testCommandLettersArriveThroughTheVST3Route() {
    let paste = KeyInput(characters: "v", virtualKey: 0x56, shift: false, command: true, option: false, control: false)
    XCTAssertEqual(paste, input(.character("v"), command: true))
    XCTAssertEqual(chords(paste), .paste)
  }

  func testOtherKeysAreNotShortcuts() {
    XCTAssertNil(chords(input(.character(" "))), "Space stays the host's")
    XCTAssertNil(chords(input(.character("a"))))
    XCTAssertNil(chords(input(.other)))
  }

  func testArrowsAreNotShortcutsWithTheChordsTargeted() {
    // Not consumed: the host keeps them (Live moves its own selection).
    for direction in arrows {
      XCTAssertNil(chords(input(.arrow(direction))), "\(direction)")
      XCTAssertNil(chords(input(.arrow(direction), shift: true)), "⇧\(direction)")
      XCTAssertNil(chords(input(.arrow(direction), command: true)), "⌘\(direction)")
      XCTAssertNil(chords(input(.arrow(direction), option: true)), "⌥\(direction)")
      XCTAssertNil(chords(input(.arrow(direction), control: true)), "⌃\(direction)")
    }
  }

  // MARK: - The map (notes targeted)

  func testBareArrowsNudgeTheNotes() {
    for direction in arrows {
      XCTAssertEqual(notes(input(.arrow(direction))), .nudge(direction, octave: false), "\(direction)")
    }
  }

  func testShiftArrowsNudgeWithTheOctaveFlag() {
    // ⇧↑/⇧↓ an octave; ⇧←/⇧→ carry the flag too and the nudge ignores it horizontally (RollGroupMath.nudge).
    for direction in arrows {
      XCTAssertEqual(notes(input(.arrow(direction), shift: true)), .nudge(direction, octave: true), "⇧\(direction)")
    }
  }

  func testArrowsWithCommandOptionOrControlAreNotShortcutsForNotes() {
    for direction in arrows {
      XCTAssertNil(notes(input(.arrow(direction), command: true)), "⌘\(direction)")
      XCTAssertNil(notes(input(.arrow(direction), option: true)), "⌥\(direction)")
      XCTAssertNil(notes(input(.arrow(direction), control: true)), "⌃\(direction)")
      XCTAssertNil(notes(input(.arrow(direction), command: true, shift: true)), "⇧⌘\(direction)")
      XCTAssertNil(notes(input(.arrow(direction), shift: true, option: true)), "⇧⌥\(direction)")
    }
  }

  func testDeleteKeysMapForNotesAsForChords() {
    XCTAssertEqual(notes(input(.delete)), .deleteSelection)
    XCTAssertEqual(notes(input(.forwardDelete)), .deleteSelection)
    XCTAssertNil(notes(input(.delete, command: true)))
    XCTAssertNil(notes(input(.forwardDelete, option: true)))
  }

  func testCommandShortcutsMapForNotesAsForChords() {
    let expected: [(Character, KeyCommand)] = [("c", .copy), ("x", .cut), ("v", .paste), ("d", .duplicate), ("a", .selectAll)]
    for (letter, command) in expected {
      XCTAssertEqual(notes(input(.character(letter), command: true)), command, "⌘\(letter)")
      XCTAssertNil(notes(input(.character(letter), command: true, shift: true)), "⇧⌘\(letter)")
      XCTAssertNil(notes(input(.character(letter))), "bare \(letter)")
    }
    XCTAssertEqual(notes(input(.character("z"), command: true)), .undo)
    XCTAssertEqual(notes(input(.character("Z"), command: true, shift: true)), .redo)
  }

  func testDigitsAreNotShortcutsForNotes() {
    // The suggestion chips are hidden while a track is open (design D13).
    for n in 1...8 {
      XCTAssertNil(notes(input(.character(Character(String(n))))), "digit \(n)")
      XCTAssertEqual(chords(input(.character(Character(String(n))))), .appendSuggestion(n), "digit \(n), chords")
    }
  }

  func testOtherKeysAreNotShortcutsForNotes() {
    XCTAssertNil(notes(input(.character(" "))))
    XCTAssertNil(notes(input(.character("a"))))
    XCTAssertNil(notes(input(.other)))
  }

  // MARK: - The key target

  private func track(_ id: Int) -> TrackJSON {
    TrackJSON(id: id, name: "T\(id)", role: .bass, mute: false, solo: false, audible: true,
              notes: TrackNotesJSON(id: [], pitch: [], start: [], length: [], velocity: [], anchor: [], offset: [],
                                    active: []))
  }

  func testNotesStayTargetedWhileTheirTrackExists() {
    XCTAssertEqual(KeyTarget.notes(trackId: 2).resolved(in: [track(1), track(2)]), .notes(trackId: 2))
    XCTAssertEqual(KeyTarget.chords.resolved(in: [track(1)]), .chords)
  }

  func testARemovedTracksNotesFallBackToChords() {
    XCTAssertEqual(KeyTarget.notes(trackId: 2).resolved(in: [track(1)]), .chords)
    XCTAssertEqual(KeyTarget.notes(trackId: 2).resolved(in: []), .chords)
  }

  func testNoTracksTargetChords() {
    XCTAssertEqual(KeyTarget.notes(trackId: 1).resolved(in: nil), .chords)
    XCTAssertEqual(KeyTarget.chords.resolved(in: nil), .chords)
  }

  func testChangingTheOpenLaneTargetsChords() {
    XCTAssertEqual(KeyTarget.notes(trackId: 1).afterOpenLaneChange(from: .track(1), to: .chords), .chords)
    XCTAssertEqual(KeyTarget.notes(trackId: 1).afterOpenLaneChange(from: .track(1), to: .track(2)), .chords)
    XCTAssertEqual(KeyTarget.chords.afterOpenLaneChange(from: .chords, to: .track(2)), .chords,
                   "opening a track does not target its notes; the roll's mouse-down does")
  }

  func testReopeningTheSameLaneKeepsTheTarget() {
    XCTAssertEqual(KeyTarget.notes(trackId: 1).afterOpenLaneChange(from: .track(1), to: .track(1)), .notes(trackId: 1))
  }

  // MARK: - What perform may run before notes get their own dispatch (plan Task 20)

  private let everyCommand: [KeyCommand] = [
    .appendSuggestion(1), .deleteSelection, .undo, .redo, .copy, .cut, .paste, .duplicate, .selectAll,
    .nudge(.up, octave: false), .nudge(.left, octave: true),
  ]

  func testEveryCommandRunsWithTheChordsTargeted() {
    for command in everyCommand {
      XCTAssertTrue(command.runsBeforeNoteDispatch(target: .chords), "\(command)")
    }
  }

  func testOnlyUndoAndRedoRunWithNotesTargeted() {
    for command in everyCommand {
      let expected = command == .undo || command == .redo
      XCTAssertEqual(command.runsBeforeNoteDispatch(target: .notes(trackId: 3)), expected,
                     "\(command) must never act on the hidden chords")
    }
  }

  // MARK: - The AppKit adapter's key (route A: macOS key code and charactersIgnoringModifiers)

  private func mac(_ keyCode: UInt16, _ text: String?) -> KeyInput.Key {
    KeyInput.Key(macKeyCode: keyCode, charactersIgnoringModifiers: text)
  }

  func testMacKeyCodesForTheDeleteKeys() {
    XCTAssertEqual(mac(51, "\u{7F}"), .delete, "delete")
    XCTAssertEqual(mac(51, ""), .delete)
    XCTAssertEqual(mac(117, "\u{F728}"), .forwardDelete, "NSDeleteFunctionKey")
    XCTAssertEqual(mac(117, nil), .forwardDelete)
  }

  func testMacKeyCodesForTheArrowsWhateverTheirText() {
    // Live 12 sends arrows with empty text (S0); AppKit elsewhere gives the private-use function-key characters.
    XCTAssertEqual(mac(123, ""), .arrow(.left))
    XCTAssertEqual(mac(124, ""), .arrow(.right))
    XCTAssertEqual(mac(125, ""), .arrow(.down))
    XCTAssertEqual(mac(126, ""), .arrow(.up))
    XCTAssertEqual(mac(123, "\u{F702}"), .arrow(.left), "NSLeftArrowFunctionKey")
    XCTAssertEqual(mac(124, "\u{F703}"), .arrow(.right), "NSRightArrowFunctionKey")
    XCTAssertEqual(mac(125, "\u{F701}"), .arrow(.down), "NSDownArrowFunctionKey")
    XCTAssertEqual(mac(126, "\u{F700}"), .arrow(.up), "NSUpArrowFunctionKey")
    XCTAssertEqual(mac(126, nil), .arrow(.up))
    XCTAssertEqual(mac(123, "1"), .arrow(.left), "a digit's text must not append a chip")
  }

  func testMacKeypadDigitIsItsCharacter() {
    XCTAssertEqual(mac(84, "2"), .character("2"), "keypad 2")
  }

  func testAnArrowCharacterWithAnotherKeyCodeIsACharacter() {
    // Only the key code makes an arrow; the text never does.
    XCTAssertEqual(mac(0, "\u{F702}"), .character("\u{F702}"))
  }

  func testMacTextThatIsNotOneCharacterIsOther() {
    XCTAssertEqual(mac(0, ""), .other)
    XCTAssertEqual(mac(0, nil), .other)
    XCTAssertEqual(mac(0, "ab"), .other)
  }

  func testMacLetterIsItsCharacter() {
    XCTAssertEqual(mac(6, "z"), .character("z"))
    XCTAssertEqual(chords(input(mac(6, "z"), command: true)), .undo)
  }

  // MARK: - The VST3 adapter (iPlug2 IKeyPress: utf8 text, Windows virtual key, S / C=command / A)

  func testVirtualKeyNormalisation() {
    XCTAssertEqual(KeyInput(characters: "", virtualKey: 0x08, shift: false, command: false, option: false, control: false),
                   input(.delete), "kVK_BACK is the Mac delete key")
    XCTAssertEqual(KeyInput(characters: "", virtualKey: 0x2E, shift: false, command: false, option: false, control: false),
                   input(.forwardDelete), "kVK_DELETE is forward delete")
    XCTAssertEqual(KeyInput(characters: "3", virtualKey: 0x33, shift: false, command: false, option: false, control: false),
                   input(.character("3")))
    XCTAssertEqual(KeyInput(characters: "z", virtualKey: 0x5A, shift: true, command: true, option: false, control: false),
                   input(.character("z"), command: true, shift: true))
    XCTAssertEqual(KeyInput(characters: "2", virtualKey: 0x32, shift: false, command: false, option: false, control: true),
                   input(.character("2"), control: true), "Control comes from the caller (iPlug2 drops it)")
    XCTAssertEqual(KeyInput(characters: "", virtualKey: 0x41, shift: false, command: false, option: false, control: false),
                   input(.other), "no character and not a delete key")
  }

  func testKeypadDigitsWithoutTextAreThatDigit() {
    // VST3 keypad events may arrive with empty text and only the keypad virtual key (kVK_NUMPAD0..9).
    XCTAssertEqual(KeyInput(characters: "", virtualKey: 0x62, shift: false, command: false, option: false, control: false),
                   input(.character("2")), "keypad 2")
    XCTAssertEqual(KeyInput(characters: "", virtualKey: 0x60, shift: false, command: false, option: false, control: false),
                   input(.character("0")), "keypad 0")
    XCTAssertEqual(KeyInput(characters: "", virtualKey: 0x69, shift: false, command: false, option: false, control: false),
                   input(.character("9")), "keypad 9")
    XCTAssertEqual(KeyInput(characters: "2", virtualKey: 0x62, shift: false, command: false, option: false, control: false),
                   input(.character("2")), "keypad 2 with its text")
  }

  func testArrowVirtualKeysAreArrows() {
    // kVK_LEFT 0x25, kVK_UP 0x26, kVK_RIGHT 0x27, kVK_DOWN 0x28; iPlug2's VST3 translation leaves the text empty.
    XCTAssertEqual(vst3("", 0x25), input(.arrow(.left)))
    XCTAssertEqual(vst3("", 0x26), input(.arrow(.up)))
    XCTAssertEqual(vst3("", 0x27), input(.arrow(.right)))
    XCTAssertEqual(vst3("", 0x28), input(.arrow(.down)))
    XCTAssertEqual(vst3("", 0x26, shift: true), input(.arrow(.up), shift: true))
    XCTAssertEqual(vst3("", 0x25, command: true, option: true, control: true),
                   input(.arrow(.left), command: true, option: true, control: true))
  }

  func testAnArrowsTextIsNeverReadAsACharacter() {
    // Whatever text a host attaches (a digit, a letter, a function-key character, several characters), the
    // virtual key decides.
    XCTAssertEqual(vst3("1", 0x25), input(.arrow(.left)), "a digit's text must not append a chip")
    XCTAssertEqual(vst3("a", 0x26, command: true), input(.arrow(.up), command: true), "not ⌘A")
    XCTAssertEqual(vst3("\u{F703}", 0x27), input(.arrow(.right)), "NSRightArrowFunctionKey")
    XCTAssertEqual(vst3("xy", 0x28), input(.arrow(.down)))
    XCTAssertEqual(notes(vst3("2", 0x28)), .nudge(.down, octave: false))
    XCTAssertNil(chords(vst3("2", 0x28)), "not chip 2, and not consumed with the chords targeted")
  }

  func testNeighbouringVirtualKeysAreNotArrows() {
    XCTAssertEqual(vst3("", 0x24), input(.other), "kVK_HOME")
    XCTAssertEqual(vst3("", 0x29), input(.other), "kVK_SELECT")
  }

  // MARK: - The repeat / duplicate filter

  private let press = KeySource.appKit(isRepeat: false)

  func testAFirstPressActs() {
    var filter = KeyRepeatFilter()
    XCTAssertTrue(filter.shouldAct(input(.character("2")), at: 10.0, source: press))
  }

  func testARouteFlaggedRepeatNeverActs() {
    var filter = KeyRepeatFilter()
    XCTAssertTrue(filter.shouldAct(input(.delete), at: 10.0, source: press))
    XCTAssertFalse(filter.shouldAct(input(.delete), at: 10.5, source: .appKit(isRepeat: true)))
  }

  func testTheSameKeyWithinTheWindowIsADuplicateAndAChainOfRepeatsStaysSuppressed() {
    var filter = KeyRepeatFilter()
    XCTAssertTrue(filter.shouldAct(input(.character("2")), at: 10.0, source: press))
    XCTAssertFalse(filter.shouldAct(input(.character("2")), at: 10.03, source: press), "both routes, one press")
    XCTAssertFalse(filter.shouldAct(input(.character("2")), at: 10.06, source: press), "autorepeat at 33 ms")
    XCTAssertFalse(filter.shouldAct(input(.character("2")), at: 10.12, source: press),
                   "measured from the previous delivery, not the first press")
  }

  func testADeliberateSecondPressActs() {
    var filter = KeyRepeatFilter()
    XCTAssertTrue(filter.shouldAct(input(.character("2")), at: 10.0, source: press))
    XCTAssertTrue(filter.shouldAct(input(.character("2")), at: 10.15, source: press), "a double tap")
  }

  func testTheSameKeyInEitherCaseIsADuplicate() {
    // ⇧⌘Z may arrive as "Z" on one route and "z" on the other; it is still one press.
    var filter = KeyRepeatFilter()
    XCTAssertTrue(filter.shouldAct(input(.character("Z"), command: true, shift: true), at: 10.0, source: press))
    XCTAssertFalse(filter.shouldAct(input(.character("z"), command: true, shift: true), at: 10.03, source: .host))
  }

  func testADifferentKeyActsAtOnce() {
    var filter = KeyRepeatFilter()
    XCTAssertTrue(filter.shouldAct(input(.character("2")), at: 10.0, source: press))
    XCTAssertTrue(filter.shouldAct(input(.character("3")), at: 10.01, source: press))
    XCTAssertTrue(filter.shouldAct(input(.delete), at: 10.02, source: press))
  }

  func testInterleavedKeysOnBothRoutesActOncePerKey() {
    var filter = KeyRepeatFilter()
    let acted = [
      filter.shouldAct(input(.character("2")), at: 10.00, source: press),
      filter.shouldAct(input(.character("3")), at: 10.01, source: press),
      filter.shouldAct(input(.character("2")), at: 10.02, source: .host),
      filter.shouldAct(input(.character("3")), at: 10.03, source: .host),
    ]
    XCTAssertEqual(acted, [true, true, false, false], "the history is per key, not just the last delivery")
  }

  func testAKeypadDigitWithoutTextIsTheSameKeyAsRouteAsDigit() {
    var filter = KeyRepeatFilter()
    let keypad = KeyInput(characters: "", virtualKey: 0x62, shift: false, command: false, option: false, control: false)
    XCTAssertTrue(filter.shouldAct(input(.character("2")), at: 10.0, source: press))
    XCTAssertFalse(filter.shouldAct(keypad, at: 10.02, source: .host))
  }

  // MARK: - Held keys on the host route (no repeat flag; key-ups from OnKeyUp)

  func testAKeyHeldOnTheHostRouteActsOnce() {
    var filter = KeyRepeatFilter()
    XCTAssertTrue(filter.shouldAct(input(.character("2")), at: 10.0, source: .host))
    XCTAssertFalse(filter.shouldAct(input(.character("2")), at: 10.5, source: .host), "the initial repeat delay")
    XCTAssertFalse(filter.shouldAct(input(.character("2")), at: 10.6, source: .host), "a slow repeat")
    XCTAssertFalse(filter.shouldAct(input(.character("2")), at: 10.7, source: .host))
  }

  func testAKeyUpOnTheHostRouteEndsTheHold() {
    var filter = KeyRepeatFilter()
    XCTAssertTrue(filter.shouldAct(input(.character("2")), at: 10.0, source: .host))
    XCTAssertFalse(filter.shouldAct(input(.character("2")), at: 10.5, source: .host))
    filter.keyUp(input(.character("2")), at: 10.8)
    XCTAssertTrue(filter.shouldAct(input(.character("2")), at: 10.9, source: .host))
  }

  func testAHoldWhoseReleaseWasLostExpiresAfterTwoSeconds() {
    // Models a release that never arrived (a host that sends no key-ups): the next delivery 2.5 s later
    // acts. The expiry cannot tell this from a key still held with a repeat slower than 2 s.
    var filter = KeyRepeatFilter()
    XCTAssertTrue(filter.shouldAct(input(.character("2")), at: 10.0, source: .host))
    XCTAssertTrue(filter.shouldAct(input(.character("2")), at: 12.5, source: .host))
  }

  func testRouteANeverHoldsAKey() {
    var filter = KeyRepeatFilter()
    XCTAssertTrue(filter.shouldAct(input(.character("2")), at: 10.0, source: press))
    XCTAssertTrue(filter.shouldAct(input(.character("2")), at: 10.3, source: .host),
                  "a host press outside the window: the route-A press left no hold")
  }

  func testUndoReleaseThenRedoAreTwoCommands() {
    // Holds are per physical key, duplicates per key and modifiers: ⌘Z released, then ⇧⌘Z 50 ms later.
    var filter = KeyRepeatFilter()
    XCTAssertTrue(filter.shouldAct(input(.character("z"), command: true), at: 10.00, source: .host), "undo")
    filter.keyUp(input(.character("z"), command: true), at: 10.03)
    XCTAssertTrue(filter.shouldAct(input(.character("Z"), command: true, shift: true), at: 10.08, source: .host), "redo")
  }

  func testClearingHoldsLetsTheNextHostPressAct() {
    var filter = KeyRepeatFilter()
    XCTAssertTrue(filter.shouldAct(input(.character("2")), at: 10.0, source: .host))
    filter.clearHolds()   // the editor lost the keyboard; the release may go elsewhere
    XCTAssertTrue(filter.shouldAct(input(.character("2")), at: 10.4, source: .host))
  }

  // MARK: - Key-ups answer what their key-down answered

  func testAKeyUpAnswersWhatItsKeyDownAnswered() {
    var filter = KeyRepeatFilter()
    filter.noteHostKeyDown(input(.character("2"), option: true), consumed: false)
    XCTAssertFalse(filter.keyUp(input(.character("2")), at: 10.1), "⌥2 went to the host; Option released first")
    filter.noteHostKeyDown(input(.character("z"), command: true), consumed: true)
    XCTAssertTrue(filter.keyUp(input(.character("z")), at: 10.2), "⌘Z was ours; ⌘ released first")
    XCTAssertFalse(filter.keyUp(input(.character("z")), at: 10.3), "the decision is forgotten after its release")
    XCTAssertFalse(filter.keyUp(input(.character("3")), at: 10.4), "a release without a key-down is not ours")
  }

  func testClearingHoldsForgetsKeyDownDecisions() {
    var filter = KeyRepeatFilter()
    filter.noteHostKeyDown(input(.character("2")), consumed: true)
    filter.clearHolds()
    XCTAssertFalse(filter.keyUp(input(.character("2")), at: 10.1))
  }

  func testADuplicateOnTheHostRouteHoldsTheKeyUntilItsKeyUp() {
    var filter = KeyRepeatFilter()
    XCTAssertTrue(filter.shouldAct(input(.character("2")), at: 10.0, source: press))
    XCTAssertFalse(filter.shouldAct(input(.character("2")), at: 10.02, source: .host), "the same press on route B")
    XCTAssertFalse(filter.shouldAct(input(.character("2")), at: 10.5, source: .host), "held: route B's repeat")
    filter.keyUp(input(.character("2")), at: 10.6)
    XCTAssertTrue(filter.shouldAct(input(.character("2")), at: 10.8, source: .host))
  }

  // MARK: - Arrows through the filter (one step per press, like every command)

  func testAnArrowActsOncePerPressOnRouteA() {
    var filter = KeyRepeatFilter()
    XCTAssertTrue(filter.shouldAct(input(.arrow(.up)), at: 10.0, source: press))
    XCTAssertFalse(filter.shouldAct(input(.arrow(.up)), at: 10.5, source: .appKit(isRepeat: true)), "AppKit repeat")
    XCTAssertFalse(filter.shouldAct(input(.arrow(.up)), at: 10.55, source: .appKit(isRepeat: true)))
    XCTAssertTrue(filter.shouldAct(input(.arrow(.up)), at: 11.0, source: press), "the next press")
  }

  func testAHeldArrowOnRouteBActsOnceUntilItsKeyUp() {
    var filter = KeyRepeatFilter()
    let up = vst3("", 0x26)
    XCTAssertTrue(filter.shouldAct(up, at: 10.0, source: .host))
    XCTAssertFalse(filter.shouldAct(up, at: 10.5, source: .host), "route B's redelivery of a held key")
    XCTAssertFalse(filter.shouldAct(up, at: 10.6, source: .host))
    filter.keyUp(up, at: 10.7)
    XCTAssertTrue(filter.shouldAct(up, at: 10.9, source: .host))
  }

  func testAHeldArrowOnRouteBActsAgainAfterTheBackstop() {
    var filter = KeyRepeatFilter()
    XCTAssertTrue(filter.shouldAct(input(.arrow(.left)), at: 10.0, source: .host))
    XCTAssertFalse(filter.shouldAct(input(.arrow(.left)), at: 11.9, source: .host))
    XCTAssertTrue(filter.shouldAct(input(.arrow(.left)), at: 14.0, source: .host), "2 s after its last delivery")
  }

  func testAnArrowArrivingOnBothRoutesActsOnce() {
    var filter = KeyRepeatFilter()
    XCTAssertTrue(filter.shouldAct(input(.arrow(.down), shift: true), at: 10.0, source: press))
    XCTAssertFalse(filter.shouldAct(vst3("", 0x28, shift: true), at: 10.02, source: .host))
  }

  func testLeftThenRightAreSeparateKeys() {
    var filter = KeyRepeatFilter()
    XCTAssertTrue(filter.shouldAct(input(.arrow(.left)), at: 10.0, source: .host))
    XCTAssertTrue(filter.shouldAct(input(.arrow(.right)), at: 10.02, source: .host), "not a duplicate, not held")
    filter.keyUp(input(.arrow(.left)), at: 10.1)
    XCTAssertFalse(filter.shouldAct(input(.arrow(.right)), at: 10.4, source: .host), "→ is still held")
  }

  func testAnArrowsKeyUpAnswersWhatItsKeyDownAnswered() {
    var filter = KeyRepeatFilter()
    filter.noteHostKeyDown(input(.arrow(.up)), consumed: false)   // chords targeted: the host's
    XCTAssertFalse(filter.keyUp(input(.arrow(.up)), at: 10.1))
    filter.noteHostKeyDown(input(.arrow(.up), shift: true), consumed: true)   // notes targeted: ours
    XCTAssertTrue(filter.keyUp(input(.arrow(.up)), at: 10.2), "⇧ released first")
  }

  func testAKeyUpIgnoresModifiersAndCase() {
    var filter = KeyRepeatFilter()
    XCTAssertTrue(filter.shouldAct(input(.character("z"), command: true), at: 10.0, source: .host))
    filter.keyUp(input(.character("Z")), at: 10.3)   // ⌘ released before the key
    XCTAssertTrue(filter.shouldAct(input(.character("z"), command: true), at: 10.5, source: .host))
  }
}
