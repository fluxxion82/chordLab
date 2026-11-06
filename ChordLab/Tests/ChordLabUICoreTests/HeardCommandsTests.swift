import Foundation
import XCTest
@testable import ChordLabUICore

/// What the Heard group's Add and Use send (UI/Core/HeardCommands.swift): Add appends the heard chord as
/// played, held or released (design D10, D11); Use changes the project key to the heard one (D12).
final class HeardCommandsTests: XCTestCase {
  private func heard(chord: HeardChordJSON? = nil, chordHeld: Bool = false,
                     key: HeardKeyJSON? = nil) -> HeardJSON {
    HeardJSON(schema: 1, active: true, held: [], heldNames: [], chord: chord, chordHeld: chordHeld, key: key,
              onsets: 8)
  }

  private let am7Over_G = HeardChordJSON(root: 9, type: "m7", symbol: "Am7/G", roman: "vi7", inversion: 3, bass: 55)

  // MARK: - Add

  func testAddIsNilWithoutAHeardMessageOrAChord() {
    XCTAssertNil(HeardCommands.add(nil))
    XCTAssertNil(HeardCommands.add(heard()))
    XCTAssertNil(HeardCommands.add(heard(chordHeld: true)))
  }

  func testAddCarriesTheHeardChordsRootTypeAndInversion() {
    let add = HeardCommands.add(heard(chord: am7Over_G, chordHeld: true))
    XCTAssertEqual(add, HeardCommands.Append(root: 9, type: "m7", inversion: 3))
  }

  func testAddStaysAvailableOnceTheChordIsReleased() {
    // D10: a two-hand chord cannot be held while clicking, so the released (latched) chord is addable.
    XCTAssertEqual(HeardCommands.add(heard(chord: am7Over_G, chordHeld: false)),
                   HeardCommands.Append(root: 9, type: "m7", inversion: 3))
  }

  func testTheAppendCommandIsAppendChordWithTheInversion() throws {
    let command = try XCTUnwrap(HeardCommands.add(heard(chord: am7Over_G))?.command)
    XCTAssertEqual(Set(command.keys), ["cmd", "root", "type", "inversion"])
    XCTAssertEqual(command["cmd"] as? String, "appendChord")
    XCTAssertEqual(command["root"] as? Int, 9)
    XCTAssertEqual(command["type"] as? String, "m7")
    XCTAssertEqual(command["inversion"] as? Int, 3)
  }

  // MARK: - Use

  func testUseIsNilWithoutAHeardMessageOrAKey() {
    XCTAssertNil(HeardCommands.use(nil))
    XCTAssertNil(HeardCommands.use(heard(chord: am7Over_G, chordHeld: true)))
  }

  func testUseIsTheHeardTonicsPitchClassAndTheModeUnchanged() {
    let eFlat = HeardKeyJSON(tonic: TonicJSON(letter: "E", alter: -1), mode: "Ionian", name: "Eb Ionian",
                             percent: 71)
    XCTAssertEqual(HeardCommands.use(heard(key: eFlat)), HeardCommands.UseKey(pc: 3, mode: "Ionian"))
    let fSharp = HeardKeyJSON(tonic: TonicJSON(letter: "F", alter: 1), mode: "Aeolian", name: "F# Aeolian",
                              percent: 80)
    XCTAssertEqual(HeardCommands.use(heard(key: fSharp)), HeardCommands.UseKey(pc: 6, mode: "Aeolian"))
    // The engine's mode string goes through untouched, whatever it is.
    let odd = HeardKeyJSON(tonic: TonicJSON(letter: "B", alter: 0), mode: "HarmonicMinor", name: "", percent: 61)
    XCTAssertEqual(HeardCommands.use(heard(key: odd))?.mode, "HarmonicMinor")
    XCTAssertEqual(HeardCommands.use(heard(key: odd))?.pc, 11)
  }
}
