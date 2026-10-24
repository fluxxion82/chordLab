import XCTest
@testable import ChordLabUICore

/// Key display names and tonic pitch classes (UI/Core/KeyNames.swift).
final class KeyNamesTests: XCTestCase {
  private func key(_ letter: String, _ alter: Int, _ mode: String) -> KeyJSON {
    KeyJSON(tonic: TonicJSON(letter: letter, alter: alter), mode: mode)
  }

  func testModeDisplayNames() {
    XCTAssertEqual(KeyNames.modeDisplayName("Ionian"), "Major")
    XCTAssertEqual(KeyNames.modeDisplayName("Aeolian"), "Minor")
    XCTAssertEqual(KeyNames.modeDisplayName("HarmonicMinor"), "Harmonic Minor")
    XCTAssertEqual(KeyNames.modeDisplayName("MelodicMinor"), "Melodic Minor")
    XCTAssertEqual(KeyNames.modeDisplayName("Dorian"), "Dorian")
    XCTAssertEqual(KeyNames.modeDisplayName("Locrian"), "Locrian")
  }

  func testDisplayNamesOfSpelledKeys() {
    XCTAssertEqual(KeyNames.displayName(key("C", 0, "Ionian")), "C Major")
    XCTAssertEqual(KeyNames.displayName(key("A", 0, "Aeolian")), "A Minor")
    XCTAssertEqual(KeyNames.displayName(key("B", -1, "HarmonicMinor")), "B♭ Harmonic Minor")
    XCTAssertEqual(KeyNames.displayName(key("F", 1, "Ionian")), "F♯ Major")
    XCTAssertEqual(KeyNames.displayName(key("D", 0, "Dorian")), "D Dorian")
    XCTAssertEqual(KeyNames.displayName(key("E", -1, "MelodicMinor")), "E♭ Melodic Minor")
  }

  func testTonicTextIsTheEnginesAscii() {
    XCTAssertEqual(KeyNames.text(TonicJSON(letter: "C", alter: 0)), "C")
    XCTAssertEqual(KeyNames.text(TonicJSON(letter: "F", alter: 1)), "F#")
    XCTAssertEqual(KeyNames.text(TonicJSON(letter: "B", alter: -1)), "Bb")
  }

  func testTonicNameUsesGlyphs() {
    XCTAssertEqual(KeyNames.tonicName(TonicJSON(letter: "C", alter: 0)), "C")
    XCTAssertEqual(KeyNames.tonicName(TonicJSON(letter: "F", alter: 1)), "F♯")
    XCTAssertEqual(KeyNames.tonicName(TonicJSON(letter: "B", alter: -1)), "B♭")
  }

  func testPitchClassOfNaturalTonics() {
    let pcs = ["C", "D", "E", "F", "G", "A", "B"].map { KeyNames.pitchClass(of: TonicJSON(letter: $0, alter: 0)) }
    XCTAssertEqual(pcs, [0, 2, 4, 5, 7, 9, 11])
  }

  func testPitchClassOfAlteredTonicsWraps() {
    XCTAssertEqual(KeyNames.pitchClass(of: TonicJSON(letter: "F", alter: 1)), 6)
    XCTAssertEqual(KeyNames.pitchClass(of: TonicJSON(letter: "B", alter: -1)), 10)
    XCTAssertEqual(KeyNames.pitchClass(of: TonicJSON(letter: "C", alter: -1)), 11, "C♭ wraps down to 11")
    XCTAssertEqual(KeyNames.pitchClass(of: TonicJSON(letter: "B", alter: 1)), 0, "B♯ wraps up to 0")
    XCTAssertEqual(KeyNames.pitchClass(of: TonicJSON(letter: "E", alter: 1)), 5, "E♯ is F")
  }
}
