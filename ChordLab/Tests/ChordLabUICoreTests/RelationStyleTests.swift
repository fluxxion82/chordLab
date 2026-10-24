import SwiftUI
import XCTest
@testable import ChordLabUICore

/// The chord-relation chooser's modes and the palette cells' relatedness look (UI/Core/RelationStyle.swift,
/// plan R2 and R7).
final class RelationStyleTests: XCTestCase {
  // MARK: - RelationMode

  /// The raw values are the engine's wire names (`setRelationMode {mode}`, snapshot `relationMode`).
  func testRawValuesArePinned() {
    XCTAssertEqual(RelationMode.off.rawValue, "off")
    XCTAssertEqual(RelationMode.commonTones.rawValue, "commonTones")
    XCTAssertEqual(RelationMode.voiceLeading.rawValue, "voiceLeading")
    XCTAssertEqual(RelationMode.function.rawValue, "function")
    XCTAssertEqual(RelationMode.blend.rawValue, "blend")
    XCTAssertEqual(RelationMode(rawValue: "blend"), .blend)
    XCTAssertNil(RelationMode(rawValue: "Blend"))
  }

  func testCasesInPickerOrderWithTitlesAndIds() {
    XCTAssertEqual(RelationMode.allCases, [.off, .commonTones, .voiceLeading, .function, .blend])
    XCTAssertEqual(RelationMode.allCases.map(\.title), ["Off", "Common tones", "Voice leading", "Function", "Blend"])
    XCTAssertEqual(RelationMode.allCases.map(\.id), RelationMode.allCases.map(\.rawValue))
  }

  // MARK: - Scale: 0.70 + 0.40 · r / 100

  func testScaleWithoutARelationIsOne() {
    XCTAssertEqual(RelationStyle.scale(relation: nil), 1.0, accuracy: 1e-9)
  }

  func testScaleAcrossTheRange() {
    XCTAssertEqual(RelationStyle.scale(relation: 0), 0.70, accuracy: 1e-9)
    XCTAssertEqual(RelationStyle.scale(relation: 50), 0.90, accuracy: 1e-9)
    XCTAssertEqual(RelationStyle.scale(relation: 100), 1.10, accuracy: 1e-9)
  }

  func testScaleClampsOutOfRangeRelations() {
    XCTAssertEqual(RelationStyle.scale(relation: -5), 0.70, accuracy: 1e-9)
    XCTAssertEqual(RelationStyle.scale(relation: 140), 1.10, accuracy: 1e-9)
  }

  // MARK: - Opacity: 0.55 + 0.45 · r / 100

  func testOpacityWithoutARelationIsOne() {
    XCTAssertEqual(RelationStyle.opacity(relation: nil), 1.0, accuracy: 1e-9)
  }

  func testOpacityAcrossTheRange() {
    XCTAssertEqual(RelationStyle.opacity(relation: 0), 0.55, accuracy: 1e-9)
    XCTAssertEqual(RelationStyle.opacity(relation: 50), 0.775, accuracy: 1e-9)
    XCTAssertEqual(RelationStyle.opacity(relation: 100), 1.0, accuracy: 1e-9)
  }

  func testOpacityClampsOutOfRangeRelations() {
    XCTAssertEqual(RelationStyle.opacity(relation: -5), 0.55, accuracy: 1e-9)
    XCTAssertEqual(RelationStyle.opacity(relation: 140), 1.0, accuracy: 1e-9)
  }

  // MARK: - Text

  func testHeadlineNamesTheReferenceWithGlyphsAndTheScore() {
    let reference = RelationReferenceJSON(eventId: 3, symbol: "Bbmaj7")
    XCTAssertEqual(RelationStyle.headline(reference: reference, relation: 72), "Relation to B♭maj7 · 72")
  }

  func testHeadlineWithoutAReferenceSymbolStillShowsTheScore() {
    XCTAssertEqual(RelationStyle.headline(reference: nil, relation: 40), "Relation · 40")
  }

  func testNoHeadlineWithoutARelation() {
    XCTAssertNil(RelationStyle.headline(reference: RelationReferenceJSON(eventId: 1, symbol: "G7"), relation: nil))
  }

  func testCaptionNamesTheReferenceWithGlyphs() {
    XCTAssertEqual(RelationStyle.caption(reference: RelationReferenceJSON(eventId: 1, symbol: "F#m7")), "to F♯m7")
    XCTAssertNil(RelationStyle.caption(reference: nil))
  }
}
