import XCTest
@testable import ChordLabUICore

/// The Presets popover's pure logic (UI/Core/PresetStyle.swift, plan P7): the load mode, grouping by genre,
/// the tonality badge and footnote, the row's accessibility label, the `loadPreset` command, and when the
/// popover closes after a click.
final class PresetStyleTests: XCTestCase {
  private static func preset(_ id: String, genre: String, tonality: String = "Major", fits: Bool = true,
                             name: String? = nil, numerals: String = "I – IV – V – I") -> PresetJSON {
    PresetJSON(id: id, name: name ?? id, genre: genre, tonality: tonality, numerals: numerals, fits: fits)
  }

  // MARK: - PresetLoadMode

  /// The raw values are the engine's wire names (`loadPreset {id, mode}`).
  func testLoadModeRawValuesArePinned() {
    XCTAssertEqual(PresetLoadMode.append.rawValue, "append")
    XCTAssertEqual(PresetLoadMode.replace.rawValue, "replace")
    XCTAssertNil(PresetLoadMode(rawValue: "Append"))
  }

  func testLoadModesInPickerOrderWithTitles() {
    XCTAssertEqual(PresetLoadMode.allCases, [.append, .replace])
    XCTAssertEqual(PresetLoadMode.allCases.map(\.title), ["Add to end", "Replace progression"])
    XCTAssertEqual(PresetLoadMode.allCases.map(\.id), ["append", "replace"])
  }

  func testLoadModeDefaultAndStorageKey() {
    XCTAssertEqual(PresetLoadMode.defaultMode, .append)
    XCTAssertEqual(PresetLoadMode.storageKey, "chordlab.presetLoadMode")
  }

  // MARK: - Grouping

  func testGroupsByGenreInFirstAppearanceOrder() {
    let presets = [Self.preset("axis", genre: "Pop"), Self.preset("sensitive", genre: "Pop"),
                   Self.preset("mixolydian-rock", genre: "Rock"), Self.preset("blues-12", genre: "Blues", tonality: "Any")]
    let groups = PresetStyle.groups(presets)
    XCTAssertEqual(groups.map(\.genre), ["Pop", "Rock", "Blues"])
    XCTAssertEqual(groups.map { $0.presets.map(\.id) }, [["axis", "sensitive"], ["mixolydian-rock"], ["blues-12"]])
    XCTAssertEqual(groups.map(\.id), ["Pop", "Rock", "Blues"])
  }

  /// A genre that reappears later joins its first group, keeping the table order within it.
  func testNonContiguousGenreJoinsItsFirstGroupInOrder() {
    let presets = [Self.preset("a", genre: "Pop"), Self.preset("b", genre: "Jazz"),
                   Self.preset("c", genre: "Pop"), Self.preset("d", genre: "Jazz"), Self.preset("e", genre: "Pop")]
    let groups = PresetStyle.groups(presets)
    XCTAssertEqual(groups.map(\.genre), ["Pop", "Jazz"])
    XCTAssertEqual(groups.map { $0.presets.map(\.id) }, [["a", "c", "e"], ["b", "d"]])
  }

  func testNoPresetsNoGroups() {
    XCTAssertEqual(PresetStyle.groups([]), [])
  }

  /// Genres are compared exactly: "Folk / Country" is one genre, "pop" is not "Pop".
  func testGenresAreComparedExactly() {
    let presets = [Self.preset("a", genre: "Folk / Country"), Self.preset("b", genre: "Folk / Country"),
                   Self.preset("c", genre: "pop"), Self.preset("d", genre: "Pop")]
    XCTAssertEqual(PresetStyle.groups(presets).map(\.genre), ["Folk / Country", "pop", "Pop"])
  }

  // MARK: - Badge and footnote

  func testBadge() {
    XCTAssertEqual(PresetStyle.badge(tonality: "Major"), "major")
    XCTAssertEqual(PresetStyle.badge(tonality: "Minor"), "minor")
    XCTAssertEqual(PresetStyle.badge(tonality: "Any"), "any")
    XCTAssertEqual(PresetStyle.badge(tonality: "Modal"), "modal")
  }

  func testFootnoteOnlyForAPresetThatDoesNotFit() {
    XCTAssertEqual(PresetStyle.footnote(fits: false, tonality: "Major"), "Written for a major key")
    XCTAssertEqual(PresetStyle.footnote(fits: false, tonality: "Minor"), "Written for a minor key")
    XCTAssertNil(PresetStyle.footnote(fits: true, tonality: "Major"))
    XCTAssertNil(PresetStyle.footnote(fits: true, tonality: "Minor"))
  }

  /// `Any` always fits; a tonality this UI does not know gets no footnote rather than a guess.
  func testNoFootnoteForAnyOrAnUnknownTonality() {
    XCTAssertNil(PresetStyle.footnote(fits: false, tonality: "Any"))
    XCTAssertNil(PresetStyle.footnote(fits: true, tonality: "Any"))
    XCTAssertNil(PresetStyle.footnote(fits: false, tonality: "Modal"))
  }

  // MARK: - Numerals

  /// The engine's ASCII accidentals become glyphs, as on every other numeral; the separators and the
  /// quality marks pass through.
  func testNumeralsTakeGlyphs() {
    let andalusian = Self.preset("andalusian", genre: "Film / Epic", tonality: "Minor", numerals: "i – bVII – bVI – V7")
    XCTAssertEqual(PresetStyle.numerals(andalusian), "i – ♭VII – ♭VI – V7")
    let rnb = Self.preset("rnb-ii-v", genre: "R&B / Neo-soul", numerals: "ii9 – V7 – IΔ9 – vi7")
    XCTAssertEqual(PresetStyle.numerals(rnb), "ii9 – V7 – IΔ9 – vi7")
  }

  // MARK: - Accessibility

  func testAccessibilityLabelOfAFittingPreset() {
    let axis = Self.preset("axis", genre: "Pop", name: "Axis", numerals: "I – V – vi – IV")
    XCTAssertEqual(PresetStyle.accessibilityLabel(axis), "Axis, Pop, I – V – vi – IV, written for a major key")
  }

  func testAccessibilityLabelSaysWhenAPresetDoesNotFit() {
    let lament = Self.preset("lament", genre: "Classical", tonality: "Minor", fits: false, name: "Lament",
                             numerals: "i – bVII – bVI – V")
    XCTAssertEqual(PresetStyle.accessibilityLabel(lament),
                   "Lament, Classical, i – ♭VII – ♭VI – V, written for a minor key, does not fit the project key")
  }

  func testAccessibilityLabelForAnyAndUnknownTonality() {
    let blues = Self.preset("blues-12", genre: "Blues", tonality: "Any", name: "12-bar blues", numerals: "I7 – IV7")
    XCTAssertEqual(PresetStyle.accessibilityLabel(blues), "12-bar blues, Blues, I7 – IV7, fits any key")
    let odd = Self.preset("x", genre: "Pop", tonality: "Modal", name: "X", numerals: "I")
    XCTAssertEqual(PresetStyle.accessibilityLabel(odd), "X, Pop, I")
  }

  // MARK: - Command

  func testLoadCommand() {
    let command = PresetStyle.loadCommand(id: "axis", mode: .replace)
    XCTAssertEqual(command.count, 3)
    XCTAssertEqual(command["cmd"] as? String, "loadPreset")
    XCTAssertEqual(command["id"] as? String, "axis")
    XCTAssertEqual(command["mode"] as? String, "replace")
    XCTAssertEqual(PresetStyle.loadCommand(id: "lament", mode: .append)["mode"] as? String, "append")
  }

  // MARK: - Closing after a click (P7)

  func testSnapshotChangeMeansLoaded() {
    let watch = PresetLoadWatch(snapshot: 1, error: nil)
    XCTAssertEqual(watch.outcome(snapshot: 2, error: nil), .loaded)
    XCTAssertEqual(PresetLoadWatch<Int>(snapshot: nil, error: nil).outcome(snapshot: 1, error: nil), .loaded)
  }

  /// A good snapshot clears an earlier error; it is still a load.
  func testSnapshotChangeWinsOverAClearedError() {
    XCTAssertEqual(PresetLoadWatch(snapshot: 1, error: "old").outcome(snapshot: 2, error: nil), .loaded)
  }

  func testNewErrorWithTheSameSnapshotMeansRefused() {
    XCTAssertEqual(PresetLoadWatch(snapshot: 1, error: nil).outcome(snapshot: 1, error: "too many chords"), .refused)
    XCTAssertEqual(PresetLoadWatch(snapshot: 1, error: "old").outcome(snapshot: 1, error: "new"), .refused)
  }

  func testNothingChangedIsPending() {
    XCTAssertEqual(PresetLoadWatch(snapshot: 1, error: nil).outcome(snapshot: 1, error: nil), .pending)
    XCTAssertEqual(PresetLoadWatch(snapshot: 1, error: "old").outcome(snapshot: 1, error: "old"), .pending)
  }
}
