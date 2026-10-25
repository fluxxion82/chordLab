import Foundation
import XCTest
@testable import ChordLabUICore

/// The palette's Diatonic / Applied view (UI/Core/PaletteViewMode.swift): the persisted names, the view
/// shown for a saved choice and a snapshot, the applied grid's layout, and the one numeral rule.
final class PaletteViewModeTests: XCTestCase {
  /// A cell as the engine writes it; `extra` is spliced in before the closing brace (`,"label":"V7/ii"`).
  private static func cell(degree: Int, tier: Int, symbol: String = "A7", roman: String = "VI7",
                           extra: String = "") throws -> CellJSON {
    let json = "{\"degree\":\(degree),\"tier\":\(tier),\"root\":9,\"type\":\"7\",\"symbol\":\"\(symbol)\"," +
      "\"roman\":\"\(roman)\",\"family\":\"Major\",\"filled\":false,\"voicing\":[57,61,64,67]\(extra)}"
    return try JSONDecoder().decode(CellJSON.self, from: Data(json.utf8))
  }

  /// An applied entry: engine id, label and target as in plan A3.
  private static func applied(degree: Int, tier: Int, symbol: String, label: String, target: String) throws -> CellJSON {
    try cell(degree: degree, tier: tier, symbol: symbol,
             extra: ",\"id\":\"applied-\(degree)-\(tier)\",\"label\":\"\(label)\",\"target\":\"\(target)\"")
  }

  // MARK: - Persisted names

  func testRawValuesArePinned() {
    XCTAssertEqual(PaletteViewMode.diatonic.rawValue, "diatonic")
    XCTAssertEqual(PaletteViewMode.applied.rawValue, "applied")
    XCTAssertNil(PaletteViewMode(rawValue: "Applied"))
  }

  func testStorageKeyIsPinned() {
    XCTAssertEqual(PaletteViewMode.storageKey, "chordlab.paletteView")
  }

  func testDefaultIsDiatonic() {
    XCTAssertEqual(PaletteViewMode.defaultMode, .diatonic)
  }

  func testCasesInSwitchOrderWithTitlesAndIds() {
    XCTAssertEqual(PaletteViewMode.allCases, [.diatonic, .applied])
    XCTAssertEqual(PaletteViewMode.allCases.map(\.title), ["Diatonic", "Applied"])
    XCTAssertEqual(PaletteViewMode.allCases.map(\.id), PaletteViewMode.allCases.map(\.rawValue))
  }

  // MARK: - Effective view

  func testSavedViewIsShownWhenTheSnapshotHasApplied() {
    XCTAssertEqual(PaletteViewMode.effective(saved: .applied, snapshotHasApplied: true), .applied)
    XCTAssertEqual(PaletteViewMode.effective(saved: .diatonic, snapshotHasApplied: true), .diatonic)
  }

  /// An older plug-in build (no `applied`) always shows the diatonic grid; the saved value is an input
  /// only, so it survives for the next build that has the field.
  func testWithoutAppliedTheDiatonicGridIsShown() {
    XCTAssertEqual(PaletteViewMode.effective(saved: .applied, snapshotHasApplied: false), .diatonic)
    XCTAssertEqual(PaletteViewMode.effective(saved: .diatonic, snapshotHasApplied: false), .diatonic)
  }

  // MARK: - Applied layout

  func testLayoutIsTwoRowsOfSevenColumns() {
    let rows = PaletteViewMode.appliedLayout([])
    XCTAssertEqual(rows.count, 2)
    XCTAssertEqual(rows.map(\.count), [7, 7])
    XCTAssertTrue(rows.allSatisfy { $0.allSatisfy { $0 == nil } })
    XCTAssertEqual(PaletteViewMode.columns, 7)
    XCTAssertEqual(PaletteViewMode.appliedTiers, 2)
  }

  /// C Ionian: V7/x in tier 1 and vii°7/x in tier 0 over columns ii..vi; the tonic and vii° columns
  /// stay empty.
  func testCIonianFillsColumnsOneToFiveInBothRows() throws {
    let symbols = [(1, "A7", "C#dim7", "Dm", "ii"), (2, "B7", "D#dim7", "Em", "iii"), (3, "C7", "Edim7", "F", "IV"),
                   (4, "D7", "F#dim7", "G", "V"), (5, "E7", "G#dim7", "Am", "vi")]
    var cells: [CellJSON] = []
    for (degree, dominant, leading, target, numeral) in symbols {
      cells.append(try Self.applied(degree: degree, tier: 1, symbol: dominant, label: "V7/" + numeral, target: target))
      cells.append(try Self.applied(degree: degree, tier: 0, symbol: leading, label: "vii°7/" + numeral, target: target))
    }
    let rows = PaletteViewMode.appliedLayout(cells)
    XCTAssertEqual(rows[1].map { $0?.symbol }, [nil, "A7", "B7", "C7", "D7", "E7", nil])
    XCTAssertEqual(rows[0].map { $0?.symbol }, [nil, "C#dim7", "D#dim7", "Edim7", "F#dim7", "G#dim7", nil])
    XCTAssertEqual(rows[1][2]?.id, "applied-2-1")
    XCTAssertEqual(rows[0][4]?.label, "vii°7/V")
  }

  func testCellsOutsideTheGridAreIgnored() throws {
    let cells = [try Self.cell(degree: -1, tier: 0), try Self.cell(degree: 7, tier: 1),
                 try Self.cell(degree: 3, tier: 2), try Self.cell(degree: 3, tier: -1),
                 try Self.cell(degree: 6, tier: 0, symbol: "F#dim7")]
    let rows = PaletteViewMode.appliedLayout(cells)
    XCTAssertEqual(rows.count, 2)
    XCTAssertEqual(rows.map(\.count), [7, 7])
    XCTAssertEqual(rows.flatMap { $0 }.compactMap { $0?.symbol }, ["F#dim7"])
    XCTAssertEqual(rows[0][6]?.symbol, "F#dim7")
  }

  // MARK: - Numeral line

  func testNumeralTextIsTheLabelWhenPresent() throws {
    let cell = try Self.applied(degree: 1, tier: 1, symbol: "A7", label: "V7/ii", target: "Dm")
    XCTAssertEqual(PaletteViewMode.numeralText(cell), "V7/ii")
  }

  func testNumeralTextIsTheRomanWithoutALabel() throws {
    XCTAssertEqual(PaletteViewMode.numeralText(try Self.cell(degree: 1, tier: 1, roman: "ii")), "ii")
  }
}
