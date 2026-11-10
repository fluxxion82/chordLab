import XCTest
@testable import ChordLabUICore

/// The Length menu (UI/Core/LengthOptions.swift, strip editing design D8).
final class LengthOptionsTests: XCTestCase {
  private let fourFour = MeterJSON(num: 4, den: 4)
  private let threeFour = MeterJSON(num: 3, den: 4)
  private let sixEight = MeterJSON(num: 6, den: 8)
  private let sevenEight = MeterJSON(num: 7, den: 8)

  func testTheNoteValuesAreMeterIndependentBeats() {
    XCTAssertEqual(LengthOptions.noteEntries.map(\.title),
                   ["1/16", "1/8", "dotted 1/8", "1/4", "dotted 1/4", "1/2", "dotted 1/2", "whole"])
    XCTAssertEqual(LengthOptions.noteEntries.map(\.beats), [0.25, 0.5, 0.75, 1, 1.5, 2, 3, 4])
  }

  func testTheBarsFollowTheMeter() {
    XCTAssertEqual(LengthOptions.barEntries(meter: fourFour), [
      LengthEntry(id: "1 bar", title: "1 bar", beats: 4), LengthEntry(id: "2 bars", title: "2 bars", beats: 8),
      LengthEntry(id: "4 bars", title: "4 bars", beats: 16)])
    XCTAssertEqual(LengthOptions.barEntries(meter: threeFour).map(\.beats), [3, 6, 12])
    XCTAssertEqual(LengthOptions.barEntries(meter: sixEight).map(\.beats), [3, 6, 12])
    XCTAssertEqual(LengthOptions.barEntries(meter: sevenEight).map(\.beats), [3.5, 7, 14])
    XCTAssertEqual(LengthOptions.barEntries(meter: MeterJSON(num: 12, den: 8)).map(\.beats), [6, 12, 24])
    XCTAssertEqual(LengthOptions.barEntries(meter: nil).map(\.beats), [4, 8, 16], "an older plug-in: 4/4")
    XCTAssertEqual(LengthOptions.barEntries(meter: MeterJSON(num: 16, den: 2)).map(\.beats), [32, 64],
                   "longer than 64 beats: left out")
    XCTAssertEqual(LengthOptions.groups(meter: fourFour).map(\.count), [8, 3])
  }

  func testNamesPreferABarThenANoteValueThenTheBeats() {
    XCTAssertEqual(LengthOptions.name(4, meter: fourFour), "1 bar")
    XCTAssertEqual(LengthOptions.name(4, meter: threeFour), "whole")
    XCTAssertEqual(LengthOptions.name(3, meter: threeFour), "1 bar")
    XCTAssertEqual(LengthOptions.name(3, meter: fourFour), "dotted 1/2")
    XCTAssertEqual(LengthOptions.name(3.5, meter: sevenEight), "1 bar")
    XCTAssertEqual(LengthOptions.name(0.5, meter: fourFour), "1/8")
    XCTAssertEqual(LengthOptions.name(1.5, meter: fourFour), "dotted 1/4")
    XCTAssertEqual(LengthOptions.name(2.75, meter: fourFour), "2.75 beats")
    XCTAssertEqual(LengthOptions.name(5, meter: fourFour), "5 beats")
    XCTAssertEqual(LengthOptions.name(63.75, meter: fourFour), "63.75 beats")
  }

  func testTheTitleForASelectionAndForNewChords() {
    XCTAssertEqual(LengthOptions.title(selectionLengths: [0.5, 0.5], newChordBeats: nil, meter: fourFour), "1/8")
    XCTAssertEqual(LengthOptions.title(selectionLengths: [0.5, 1], newChordBeats: nil, meter: fourFour), "Mixed")
    XCTAssertEqual(LengthOptions.title(selectionLengths: [2.75], newChordBeats: 1, meter: fourFour), "2.75 beats")
    XCTAssertEqual(LengthOptions.title(selectionLengths: [], newChordBeats: nil, meter: fourFour), "New: 1 bar")
    XCTAssertEqual(LengthOptions.title(selectionLengths: [], newChordBeats: nil, meter: sevenEight), "New: 1 bar")
    XCTAssertEqual(LengthOptions.title(selectionLengths: [], newChordBeats: 0.5, meter: fourFour), "New: 1/8")
    XCTAssertEqual(LengthOptions.title(selectionLengths: [], newChordBeats: 1.5, meter: fourFour), "New: dotted 1/4")
  }

  func testCheckmarks() {
    let eighth = LengthOptions.noteEntries[1]
    let whole = LengthOptions.noteEntries[7]
    let bar = LengthOptions.barEntries(meter: fourFour)[0]
    XCTAssertTrue(LengthOptions.isChecked(eighth, selectionLengths: [0.5, 0.5], newChordBeats: nil))
    XCTAssertFalse(LengthOptions.isChecked(eighth, selectionLengths: [0.5, 1], newChordBeats: nil))
    XCTAssertTrue(LengthOptions.isChecked(eighth, selectionLengths: [], newChordBeats: 0.5))
    XCTAssertFalse(LengthOptions.isChecked(bar, selectionLengths: [], newChordBeats: nil), "the default checks Default")
    XCTAssertTrue(LengthOptions.isChecked(bar, selectionLengths: [4], newChordBeats: nil))
    XCTAssertTrue(LengthOptions.isChecked(whole, selectionLengths: [4], newChordBeats: nil), "equal lengths: both checked")
  }

  func testTheSelectedLengthsInStripOrder() {
    func event(_ id: Int, _ length: Double) -> EventJSON {
      EventJSON(id: id, root: 0, type: "maj", symbol: "C", roman: "I", family: "Major", startBeat: 0, lengthBeats: length,
                velocity: 100, voicing: [60], voicingNames: nil, outOfKey: nil, outOfKeyNames: nil, function: nil,
                tension: nil, inversion: 0, octave: 0, manualVoicing: false, why: nil)
    }
    let events = [event(1, 4), event(2, 0.5), event(3, 1.5)]
    XCTAssertEqual(LengthOptions.lengths(of: [3, 1], in: events), [4, 1.5])
    XCTAssertEqual(LengthOptions.lengths(of: [], in: events), [])
  }

  func testCustomSnapsToQuarterBeatsAndClamps() {
    XCTAssertEqual(LengthOptions.custom("1.5"), 1.5)
    XCTAssertEqual(LengthOptions.custom(" 1.3 "), 1.25)
    XCTAssertEqual(LengthOptions.custom("1.375"), 1.5)
    XCTAssertEqual(LengthOptions.custom("0"), 0.25)
    XCTAssertEqual(LengthOptions.custom("-2"), 0.25)
    XCTAssertEqual(LengthOptions.custom("100"), 64)
    XCTAssertNil(LengthOptions.custom(""))
    XCTAssertNil(LengthOptions.custom("abc"))
    XCTAssertNil(LengthOptions.custom("inf"))
    XCTAssertNil(LengthOptions.custom("nan"))
  }

  func testTheNewChordLengthFallsBackToOneBar() {
    XCTAssertEqual(LengthOptions.newChordBeats(nil, meter: sevenEight), 3.5)
    XCTAssertEqual(LengthOptions.newChordBeats(1.5, meter: sevenEight), 1.5)
    XCTAssertEqual(LengthOptions.newChordBeats(nil, meter: nil), 4)
  }

  func testThePicksCommands() throws {
    func text(_ command: [String: Any]) throws -> String? {
      String(data: try JSONSerialization.data(withJSONObject: command, options: [.sortedKeys]), encoding: .utf8)
    }
    XCTAssertEqual(try text(LengthOptions.command(beats: 0.5, selection: [3, 1])),
                   #"{"cmd":"setLengths","ids":[3,1],"lengthBeats":0.5}"#)
    XCTAssertEqual(try text(LengthOptions.command(beats: 0.5, selection: [])), #"{"cmd":"setNewChordLength","lengthBeats":0.5}"#)
    XCTAssertEqual(try text(LengthOptions.command(beats: nil, selection: [])), #"{"cmd":"setNewChordLength","lengthBeats":null}"#)
  }
}
