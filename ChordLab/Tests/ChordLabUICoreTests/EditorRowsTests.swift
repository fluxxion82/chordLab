import Foundation
import XCTest
@testable import ChordLabUICore

/// Which of the editor's optional rows show (UI/Core/EditorRows.swift).
final class EditorRowsTests: XCTestCase {
  private func rows(offer: Bool = true, keyRow: Bool = false, heard: Bool = false, compact: Bool = true,
                    notice: Bool = false) -> EditorRows {
    EditorRows.visible(offer: offer, keyRow: keyRow, heardActive: heard, compact: compact, notice: notice)
  }

  func testInAShortWindowTheKeyRowWinsOverTheBanner() {
    XCTAssertEqual(rows(keyRow: true), EditorRows(banner: false, keyRow: true, paletteSwitchRow: false, noticeInline: false))
    XCTAssertEqual(rows(keyRow: true, heard: true, notice: true),
                   EditorRows(banner: false, keyRow: true, paletteSwitchRow: true, noticeInline: false))
    XCTAssertEqual(rows(), EditorRows(banner: true, keyRow: false, paletteSwitchRow: true, noticeInline: false))
  }

  func testInATallWindowBothShow() {
    XCTAssertEqual(rows(keyRow: true, compact: false),
                   EditorRows(banner: true, keyRow: true, paletteSwitchRow: true, noticeInline: false))
  }

  /// As before the banner existed: the switch row hides only for the key row in a short window, unless the listener
  /// holds it.
  func testTheSwitchRowRuleIsUnchanged() {
    for offer in [false, true] {
      XCTAssertTrue(rows(offer: offer).paletteSwitchRow)
      XCTAssertFalse(rows(offer: offer, keyRow: true).paletteSwitchRow)
      XCTAssertTrue(rows(offer: offer, keyRow: true, heard: true).paletteSwitchRow)
      XCTAssertTrue(rows(offer: offer, keyRow: true, compact: false).paletteSwitchRow)
    }
  }

  func testTheRefusalSentenceTakesTheBannersLineInAShortWindow() {
    XCTAssertTrue(rows(notice: true).noticeInline)
    XCTAssertFalse(rows(compact: false, notice: true).noticeInline)
    XCTAssertFalse(rows(offer: false, notice: true).noticeInline)
    XCTAssertFalse(rows(keyRow: true, notice: true).noticeInline)              // no banner to put it in
  }

  func testNoOfferNoBanner() {
    XCTAssertFalse(rows(offer: false).banner)
    XCTAssertFalse(rows(offer: false, compact: false).banner)
  }

  func testTheStatusLinesShowOnlyWithSomethingToSay() {
    XCTAssertFalse(EditorRows.showsStatusLines(incompleteBlocks: 0, lastError: nil))
    XCTAssertTrue(EditorRows.showsStatusLines(incompleteBlocks: 2, lastError: nil))
    XCTAssertTrue(EditorRows.showsStatusLines(incompleteBlocks: 0, lastError: "x"))
  }
}
