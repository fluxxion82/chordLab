import XCTest
@testable import ChordLabUICore

final class KeyTransposeHintTests: XCTestCase {
  // MARK: - shows

  private func shows(from: Int = 0, to: Int = 2, switchOn: Bool = false, count: Int = 4, seen: Bool = false) -> Bool {
    KeyTransposeHint.shows(fromPc: from, toPc: to, transposeWithKey: switchOn, eventCount: count, hintSeen: seen)
  }

  func testShowsOnceWhenTheTonicMovedTheSwitchIsOffChordsExistAndItWasNotSeen() {
    XCTAssertTrue(shows())
  }

  func testDoesNotShowWhenEachConditionFails() {
    XCTAssertFalse(shows(from: 2, to: 2), "same tonic (a mode-only change)")
    XCTAssertFalse(shows(switchOn: true), "the switch is on")
    XCTAssertFalse(shows(count: 0), "no chords")
    XCTAssertFalse(shows(seen: true), "already shown in this project")
  }

  // MARK: - stillValid

  /// A snapshot of `phase2` (C Ionian, G7 then C) with the given tonic letter, events and switch; the browsed
  /// key is the project key unless `browsedTonic` / `browsedMode` say otherwise.
  private func snapshot(tonic: String = "C", events: [(id: Int, root: Int, type: String, length: Double, velocity: Int)] =
    [(1, 7, "7", 4, 100), (2, 0, "maj", 4, 100)], selected: Int? = nil, switchOn: Bool? = nil,
    browsedTonic: String? = nil, browsedMode: String = "Ionian") throws -> Snapshot {
    let list = events.map { e in
      "{\"id\":\(e.id),\"root\":\(e.root),\"type\":\"\(e.type)\",\"symbol\":\"X\",\"roman\":\"I\",\"startBeat\":0.0,"
        + "\"lengthBeats\":\(e.length),\"velocity\":\(e.velocity),\"voicing\":[60,64,67]}"
    }.joined(separator: ",")
    let key = "{\"tonic\":{\"letter\":\"\(tonic)\",\"alter\":0},\"mode\":\"Ionian\"}"
    let browsed = "{\"tonic\":{\"letter\":\"\(browsedTonic ?? tonic)\",\"alter\":0},\"mode\":\"\(browsedMode)\"}"
    let flag = switchOn.map { "\"transposeWithKey\":\($0)," } ?? ""
    let sel = selected.map { "\"selectedEventId\":\($0)," } ?? ""
    let json = """
      {"schema":1,"projectKey":\(key),"browsedKey":\(browsed),"projectKeyName":"x","browsedKeyName":"x",
       "progressionLoop":true,"followHost":true,"endBeat":8.0,\(flag)\(sel)"events":[\(list)],
       "palette":{"tiers":1,"cells":[]}}
      """
    return try SnapshotTests.decode(Snapshot.self, json)
  }

  func testBasisReadsTheTonicAndTheChords() throws {
    let basis = KeyTransposeHint.basis(of: try snapshot(tonic: "D"))
    XCTAssertEqual(basis.tonicPc, 2)
    XCTAssertEqual(basis.chords, [.init(id: 1, root: 7, type: "7"), .init(id: 2, root: 0, type: "maj")])
  }

  func testStillValidForTheSnapshotItWasTakenFrom() throws {
    let s = try snapshot()
    XCTAssertTrue(KeyTransposeHint.stillValid(KeyTransposeHint.basis(of: s), in: s))
  }

  func testStillValidSurvivesLengthVelocityAndSelectionChanges() throws {
    let basis = KeyTransposeHint.basis(of: try snapshot())
    XCTAssertTrue(KeyTransposeHint.stillValid(
      basis, in: try snapshot(events: [(1, 7, "7", 2, 100), (2, 0, "maj", 4, 100)])), "length")
    XCTAssertTrue(KeyTransposeHint.stillValid(
      basis, in: try snapshot(events: [(1, 7, "7", 4, 60), (2, 0, "maj", 4, 100)])), "velocity")
    XCTAssertTrue(KeyTransposeHint.stillValid(basis, in: try snapshot(selected: 2)), "selection")
    XCTAssertTrue(KeyTransposeHint.stillValid(basis, in: try snapshot(switchOn: false)), "switch explicitly off")
  }

  func testStillValidFailsWhenTheSequenceTonicOrSwitchChanged() throws {
    let basis = KeyTransposeHint.basis(of: try snapshot())
    XCTAssertFalse(KeyTransposeHint.stillValid(
      basis, in: try snapshot(events: [(1, 7, "7", 4, 100), (2, 0, "maj", 4, 100), (3, 5, "maj", 4, 100)])), "appended")
    XCTAssertFalse(KeyTransposeHint.stillValid(basis, in: try snapshot(events: [(1, 7, "7", 4, 100)])), "removed")
    XCTAssertFalse(KeyTransposeHint.stillValid(
      basis, in: try snapshot(events: [(1, 8, "7", 4, 100), (2, 0, "maj", 4, 100)])), "moved root")
    XCTAssertFalse(KeyTransposeHint.stillValid(
      basis, in: try snapshot(events: [(1, 7, "maj", 4, 100), (2, 0, "maj", 4, 100)])), "changed type")
    XCTAssertFalse(KeyTransposeHint.stillValid(basis, in: try snapshot(tonic: "D")), "tonic change")
    XCTAssertFalse(KeyTransposeHint.stillValid(basis, in: try snapshot(switchOn: true)), "switch on")
  }

  // MARK: - isHome

  func testIsHomeWhenTheBrowsedKeyIsTheProjectKey() throws {
    XCTAssertTrue(KeyTransposeHint.isHome(try snapshot()))
    XCTAssertTrue(KeyTransposeHint.isHome(try snapshot(tonic: "D")))
  }

  func testIsNotHomeWhenTheBrowsedTonicOrModeDiffers() throws {
    XCTAssertFalse(KeyTransposeHint.isHome(try snapshot(browsedTonic: "G")), "tonic differs")
    XCTAssertFalse(KeyTransposeHint.isHome(try snapshot(browsedMode: "Dorian")), "mode differs")
    XCTAssertFalse(KeyTransposeHint.isHome(try snapshot(browsedTonic: "G", browsedMode: "Aeolian")), "both differ")
  }

  func testStillValidFailsWhenAnotherKeyIsBrowsed() throws {
    let basis = KeyTransposeHint.basis(of: try snapshot())
    XCTAssertFalse(KeyTransposeHint.stillValid(basis, in: try snapshot(browsedTonic: "G")), "browsed a fifth up")
    XCTAssertFalse(KeyTransposeHint.stillValid(basis, in: try snapshot(browsedMode: "Aeolian")), "browsed mode")
    XCTAssertTrue(KeyTransposeHint.stillValid(basis, in: try snapshot(browsedTonic: "C")), "browsed home again")
  }
}
