import XCTest
@testable import ChordLabUICore

final class KeyCommandTests: XCTestCase {
  private func input(_ key: KeyInput.Key, command: Bool = false, shift: Bool = false,
                     option: Bool = false, control: Bool = false) -> KeyInput {
    KeyInput(key: key, command: command, shift: shift, option: option, control: control)
  }

  // MARK: - The map

  func testBareDigits1To8AppendThatSuggestion() {
    for n in 1...8 {
      XCTAssertEqual(KeyCommandMap.command(for: input(.character(Character(String(n))))),
                     .appendSuggestion(n), "digit \(n)")
    }
  }

  func testDigits9And0AreNotShortcuts() {
    XCTAssertNil(KeyCommandMap.command(for: input(.character("9"))))
    XCTAssertNil(KeyCommandMap.command(for: input(.character("0"))))
  }

  func testOnlyASCIIDigitsAreShortcuts() {
    XCTAssertNil(KeyCommandMap.command(for: input(.character("٢"))), "Arabic-Indic two")
    XCTAssertNil(KeyCommandMap.command(for: input(.character("²"))), "superscript two")
  }

  func testDigitsWithAnyModifierAreNotShortcuts() {
    XCTAssertNil(KeyCommandMap.command(for: input(.character("2"), command: true)))
    XCTAssertNil(KeyCommandMap.command(for: input(.character("2"), shift: true)))
    XCTAssertNil(KeyCommandMap.command(for: input(.character("2"), option: true)))
    XCTAssertNil(KeyCommandMap.command(for: input(.character("2"), control: true)))
  }

  func testDeleteAndForwardDeleteRemoveTheSelection() {
    XCTAssertEqual(KeyCommandMap.command(for: input(.delete)), .deleteSelection)
    XCTAssertEqual(KeyCommandMap.command(for: input(.forwardDelete)), .deleteSelection)
    XCTAssertNil(KeyCommandMap.command(for: input(.delete, command: true)), "⌘Delete is not ours")
    XCTAssertNil(KeyCommandMap.command(for: input(.delete, control: true)))
    XCTAssertNil(KeyCommandMap.command(for: input(.forwardDelete, option: true)), "⌥⌦ is not ours")
  }

  func testCommandZUndoesAndShiftCommandZRedoes() {
    XCTAssertEqual(KeyCommandMap.command(for: input(.character("z"), command: true)), .undo)
    XCTAssertEqual(KeyCommandMap.command(for: input(.character("Z"), command: true, shift: true)), .redo)
    XCTAssertEqual(KeyCommandMap.command(for: input(.character("z"), command: true, shift: true)), .redo,
                   "the character may arrive lower-case with shift held")
    XCTAssertNil(KeyCommandMap.command(for: input(.character("z"))), "bare z is nothing")
    XCTAssertNil(KeyCommandMap.command(for: input(.character("z"), command: true, option: true)))
    XCTAssertNil(KeyCommandMap.command(for: input(.character("z"), command: true, control: true)))
  }

  func testOtherKeysAreNotShortcuts() {
    XCTAssertNil(KeyCommandMap.command(for: input(.character(" "))), "Space stays the host's")
    XCTAssertNil(KeyCommandMap.command(for: input(.character("a"))))
    XCTAssertNil(KeyCommandMap.command(for: input(.other)))
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

  func testAHoldWithoutAKeyUpExpiresAfterTwoSeconds() {
    var filter = KeyRepeatFilter()
    XCTAssertTrue(filter.shouldAct(input(.character("2")), at: 10.0, source: .host))
    XCTAssertTrue(filter.shouldAct(input(.character("2")), at: 12.5, source: .host), "a host that never sends key-ups")
  }

  func testRouteANeverHoldsAKey() {
    var filter = KeyRepeatFilter()
    XCTAssertTrue(filter.shouldAct(input(.character("2")), at: 10.0, source: press))
    XCTAssertTrue(filter.shouldAct(input(.character("2")), at: 10.3, source: press), "not a repeat: a second press")
  }

  func testADuplicateOnTheHostRouteHoldsTheKeyUntilItsKeyUp() {
    var filter = KeyRepeatFilter()
    XCTAssertTrue(filter.shouldAct(input(.character("2")), at: 10.0, source: press))
    XCTAssertFalse(filter.shouldAct(input(.character("2")), at: 10.02, source: .host), "the same press on route B")
    XCTAssertFalse(filter.shouldAct(input(.character("2")), at: 10.5, source: .host), "held: route B's repeat")
    filter.keyUp(input(.character("2")), at: 10.6)
    XCTAssertTrue(filter.shouldAct(input(.character("2")), at: 10.8, source: .host))
  }

  func testAKeyUpIgnoresModifiersAndCase() {
    var filter = KeyRepeatFilter()
    XCTAssertTrue(filter.shouldAct(input(.character("z"), command: true), at: 10.0, source: .host))
    filter.keyUp(input(.character("Z")), at: 10.3)   // ⌘ released before the key
    XCTAssertTrue(filter.shouldAct(input(.character("z"), command: true), at: 10.5, source: .host))
  }
}
