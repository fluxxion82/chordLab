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

  // MARK: - The repeat / duplicate filter

  func testAFirstPressActs() {
    var filter = KeyRepeatFilter()
    XCTAssertTrue(filter.shouldAct(input(.character("2")), at: 10.0, isRepeat: false))
  }

  func testARouteFlaggedRepeatNeverActs() {
    var filter = KeyRepeatFilter()
    XCTAssertTrue(filter.shouldAct(input(.delete), at: 10.0, isRepeat: false))
    XCTAssertFalse(filter.shouldAct(input(.delete), at: 10.5, isRepeat: true))
  }

  func testTheSameKeyWithinTheWindowIsADuplicateAndAChainOfRepeatsStaysSuppressed() {
    var filter = KeyRepeatFilter()
    XCTAssertTrue(filter.shouldAct(input(.character("2")), at: 10.0, isRepeat: false))
    XCTAssertFalse(filter.shouldAct(input(.character("2")), at: 10.03, isRepeat: false), "both routes, one press")
    XCTAssertFalse(filter.shouldAct(input(.character("2")), at: 10.06, isRepeat: false), "autorepeat at 33 ms")
    XCTAssertFalse(filter.shouldAct(input(.character("2")), at: 10.12, isRepeat: false),
                   "measured from the previous delivery, not the first press")
  }

  func testADeliberateSecondPressActs() {
    var filter = KeyRepeatFilter()
    XCTAssertTrue(filter.shouldAct(input(.character("2")), at: 10.0, isRepeat: false))
    XCTAssertTrue(filter.shouldAct(input(.character("2")), at: 10.15, isRepeat: false), "a double tap")
  }

  func testTheSameKeyInEitherCaseIsADuplicate() {
    // ⇧⌘Z may arrive as "Z" on one route and "z" on the other; it is still one press.
    var filter = KeyRepeatFilter()
    XCTAssertTrue(filter.shouldAct(input(.character("Z"), command: true, shift: true), at: 10.0, isRepeat: false))
    XCTAssertFalse(filter.shouldAct(input(.character("z"), command: true, shift: true), at: 10.03, isRepeat: false))
  }

  func testADifferentKeyActsAtOnce() {
    var filter = KeyRepeatFilter()
    XCTAssertTrue(filter.shouldAct(input(.character("2")), at: 10.0, isRepeat: false))
    XCTAssertTrue(filter.shouldAct(input(.character("3")), at: 10.01, isRepeat: false))
    XCTAssertTrue(filter.shouldAct(input(.delete), at: 10.02, isRepeat: false))
  }
}
