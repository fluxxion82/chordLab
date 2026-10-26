import CoreGraphics
import Foundation
import XCTest
@testable import ChordLabUICore

/// The palette's row order, row labels and cell size (UI/Core/PaletteLayout.swift).
final class PaletteLayoutTests: XCTestCase {
  /// A palette cell as the engine writes it (the fields `CellJSON` requires).
  private func cell(degree: Int, tier: Int, symbol: String) -> CellJSON {
    let json = "{\"degree\":\(degree),\"tier\":\(tier),\"root\":0,\"type\":\"maj\",\"symbol\":\"\(symbol)\"," +
      "\"roman\":\"I\",\"family\":\"Major\",\"filled\":true,\"voicing\":[60,64,67]}"
    return try! JSONDecoder().decode(CellJSON.self, from: Data(json.utf8))
  }

  private func palette(tiers: Int, cells: [CellJSON]) -> PaletteJSON {
    PaletteJSON(tiers: tiers, cells: cells)
  }

  /// Five tiers with one cell each in column 0, named after their tier.
  private var fiveTierPalette: PaletteJSON {
    palette(tiers: 5, cells: (0..<5).map { cell(degree: 0, tier: $0, symbol: "t\($0)") })
  }

  func testDiatonicRowsPutThePowerRowOnTopThenTiers4To1() {
    let rows = PaletteLayout.diatonicRows(fiveTierPalette)
    XCTAssertEqual(rows.map(\.tier), [0, 4, 3, 2, 1])
    XCTAssertEqual(rows.map(\.label), ["power", "colour", "7 / add9", "6 / 7", "triads"])
    XCTAssertEqual(rows.map { $0.cells[0]?.symbol }, ["t0", "t4", "t3", "t2", "t1"], "each row holds its own tier")
  }

  func testDiatonicRowsCoverEveryTierBelowTheCount() {
    let rows = PaletteLayout.diatonicRows(palette(tiers: 2, cells: [cell(degree: 0, tier: 1, symbol: "C")]))
    XCTAssertEqual(rows.map(\.tier), [0, 1])
    XCTAssertEqual(rows[1].cells[0]?.symbol, "C")
    XCTAssertTrue(rows[0].cells.allSatisfy { $0 == nil }, "an empty tier is still a row")
  }

  func testCellsLandInTheirDegreeColumnAndOutOfRangeCellsAreIgnored() {
    let rows = PaletteLayout.diatonicRows(palette(tiers: 2, cells: [
      cell(degree: 4, tier: 1, symbol: "G"), cell(degree: 7, tier: 1, symbol: "X"), cell(degree: 0, tier: 5, symbol: "Y")]))
    XCTAssertEqual(rows[1].cells.compactMap { $0?.symbol }, ["G"])
    XCTAssertEqual(rows[1].cells[4]?.symbol, "G")
    XCTAssertEqual(rows[0].cells.compactMap { $0 }.count, 0)
  }

  func testAPaletteWithNoTiersStillHasOneRow() {
    let rows = PaletteLayout.diatonicRows(palette(tiers: 0, cells: []))
    XCTAssertEqual(rows.map(\.tier), [0])
    XCTAssertEqual(rows[0].cells.count, PaletteLayout.columns)
  }

  func testAppliedRowsAreV7OverVii7WithTheirOwnCells() {
    let rows = PaletteLayout.appliedRows([cell(degree: 1, tier: 1, symbol: "A7"), cell(degree: 1, tier: 0, symbol: "G#dim7")])
    XCTAssertEqual(rows.map(\.tier), [1, 0])
    XCTAssertEqual(rows.map(\.label), ["V7/x", "vii°7/x"])
    XCTAssertEqual(rows[0].cells[1]?.symbol, "A7")
    XCTAssertEqual(rows[1].cells[1]?.symbol, "G#dim7")
  }

  func testCellSizeIsWidthBoundWhenTheViewportIsTall() {
    // gutter 44 + 7 cells of 80 + 7 gaps of 10 (label-to-cell and six between cells) = 674
    XCTAssertEqual(PaletteLayout.cellSize(viewport: CGSize(width: 674, height: 2000), rows: 5), 80)
  }

  func testCellSizeIsHeightBoundWhenTheViewportIsShort() {
    // 5 rows: 2·10 padding + 4·10 spacing = 60; (400 − 60) / 5 = 68
    XCTAssertEqual(PaletteLayout.cellSize(viewport: CGSize(width: 900, height: 400), rows: 5), 68)
  }

  func testCellSizeClampsToMinAndMax() {
    XCTAssertEqual(PaletteLayout.cellSize(viewport: CGSize(width: 900, height: 100), rows: 5), PaletteLayout.minCell)
    XCTAssertEqual(PaletteLayout.cellSize(viewport: CGSize(width: 2000, height: 2000), rows: 5), PaletteLayout.maxCell)
  }

  func testRowWidthAtTheComputedSizeFitsTheViewport() {
    let width: CGFloat = 700
    let size = PaletteLayout.cellSize(viewport: CGSize(width: width, height: 2000), rows: 5)
    XCTAssertLessThanOrEqual(PaletteLayout.rowWidth(cellSize: size), width)
    XCTAssertGreaterThan(PaletteLayout.rowWidth(cellSize: size + 1), width, "the largest size that fits")
  }
}
