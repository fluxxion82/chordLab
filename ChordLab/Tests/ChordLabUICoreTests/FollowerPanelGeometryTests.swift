import CoreGraphics
import XCTest
@testable import ChordLabUICore

/// The follower panel's card in the window (UI/Core/FollowerPanelGeometry.swift).
final class FollowerPanelGeometryTests: XCTestCase {
  func testTheCardTakesItsCapInAWideWindow() {
    XCTAssertEqual(FollowerPanelGeometry.cardWidth(panelWidth: 900), 520)
    XCTAssertEqual(FollowerPanelGeometry.cardWidth(panelWidth: 700), 520)
    XCTAssertEqual(FollowerPanelGeometry.cardWidth(panelWidth: 544), 520)
  }

  func testTheCardStaysInsideTheMarginsOfANarrowPanel() {
    XCTAssertEqual(FollowerPanelGeometry.cardWidth(panelWidth: 500), 476)
    XCTAssertEqual(FollowerPanelGeometry.cardWidth(panelWidth: 24), 0)
    XCTAssertEqual(FollowerPanelGeometry.cardWidth(panelWidth: 10), 0)
    XCTAssertEqual(FollowerPanelGeometry.cardWidth(panelWidth: 0), 0)
  }

  func testAnUnspecifiedOrUnboundedWidthGivesTheCap() {
    XCTAssertEqual(FollowerPanelGeometry.cardWidth(panelWidth: nil), 520)
    XCTAssertEqual(FollowerPanelGeometry.cardWidth(panelWidth: .infinity), 520)
    XCTAssertEqual(FollowerPanelGeometry.cardWidth(panelWidth: .nan), 520)
  }

  func testTheCardIsOfferedTheHeightInsideTheMargins() {
    XCTAssertEqual(FollowerPanelGeometry.cardHeight(panelHeight: 800), 776)
    XCTAssertEqual(FollowerPanelGeometry.cardHeight(panelHeight: 10), 0)
    XCTAssertNil(FollowerPanelGeometry.cardHeight(panelHeight: nil))
    XCTAssertNil(FollowerPanelGeometry.cardHeight(panelHeight: .infinity))
  }

  func testThePanelTakesAProposedSize() {
    let card = CGSize(width: 520, height: 281)
    XCTAssertEqual(FollowerPanelGeometry.panelSize(proposedWidth: 900, proposedHeight: 800, card: card),
                   CGSize(width: 900, height: 800))
  }

  func testWithoutAProposalThePanelIsTheCardAndItsMargins() {
    let card = CGSize(width: 520, height: 281)
    XCTAssertEqual(FollowerPanelGeometry.panelSize(proposedWidth: nil, proposedHeight: nil, card: card),
                   CGSize(width: 544, height: 305))
    XCTAssertEqual(FollowerPanelGeometry.panelSize(proposedWidth: .infinity, proposedHeight: 800, card: card),
                   CGSize(width: 544, height: 800))
    XCTAssertEqual(FollowerPanelGeometry.panelSize(proposedWidth: 900, proposedHeight: nil, card: card),
                   CGSize(width: 900, height: 305))
  }
}
