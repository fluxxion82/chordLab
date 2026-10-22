import UniformTypeIdentifiers
import XCTest
@testable import ChordLabUICore

/// The palette's drag payload (UI/Core/ChordRef.swift).
final class ChordRefTests: XCTestCase {
  func testCodableRoundTrip() throws {
    for ref in [ChordRef(root: 0, type: "maj"), ChordRef(root: 11, type: "m7b5"), ChordRef(root: 6, type: "7sus4")] {
      let data = try JSONEncoder().encode(ref)
      XCTAssertEqual(try JSONDecoder().decode(ChordRef.self, from: data), ref)
    }
  }

  func testWireFormIsRootAndType() throws {
    let ref = try JSONDecoder().decode(ChordRef.self, from: Data(#"{"root":7,"type":"7"}"#.utf8))
    XCTAssertEqual(ref, ChordRef(root: 7, type: "7"))
    let object = try JSONSerialization.jsonObject(with: JSONEncoder().encode(ref)) as? [String: Any]
    XCTAssertEqual(Set(object?.keys ?? [:].keys), ["root", "type"])
  }

  /// The identifier the APP and VST3 Info.plists declare.
  func testDragTypeIdentifier() {
    XCTAssertEqual(UTType.chordLabChord.identifier, "com.sterlingalbury.chordlab.chord")
  }
}
