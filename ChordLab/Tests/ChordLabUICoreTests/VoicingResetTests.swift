import XCTest
@testable import ChordLabUICore

/// When "Auto voicing" (the context menu) and the popover's "Auto" are available (UI/Core/VoicingReset.swift):
/// for a manual chord, and for any chord a transpose left off inversion 0 / octave 0.
final class VoicingResetTests: XCTestCase {
  func testManualChordCanBeReset() {
    XCTAssertTrue(VoicingReset.isAvailable(manual: true, inversion: 0, octave: 0))
  }

  func testNonManualChordAtOctaveZeroRootPositionHasNothingToReset() {
    XCTAssertFalse(VoicingReset.isAvailable(manual: false, inversion: 0, octave: 0))
  }

  func testNonManualChordLeftAtAnotherOctaveCanBeReset() {
    XCTAssertTrue(VoicingReset.isAvailable(manual: false, inversion: 0, octave: -1))
    XCTAssertTrue(VoicingReset.isAvailable(manual: false, inversion: 0, octave: 1))
  }

  func testNonManualChordLeftInAnInversionCanBeReset() {
    XCTAssertTrue(VoicingReset.isAvailable(manual: false, inversion: 2, octave: 0))
  }

  func testMissingFieldsFromOlderSnapshotsCountAsNotManualAndZero() {
    XCTAssertFalse(VoicingReset.isAvailable(manual: nil, inversion: nil, octave: nil))
    XCTAssertTrue(VoicingReset.isAvailable(manual: nil, inversion: nil, octave: -1))
    XCTAssertTrue(VoicingReset.isAvailable(manual: true, inversion: nil, octave: nil))
  }
}
