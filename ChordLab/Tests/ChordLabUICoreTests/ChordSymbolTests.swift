import XCTest
@testable import ChordLabUICore

/// Chord symbol glyphs, spoken names and note names (UI/Core/ChordSymbol.swift).
final class ChordSymbolTests: XCTestCase {
  func testGlyphsReplaceAsciiAccidentals() {
    XCTAssertEqual(ChordSymbol.glyphs("Bb7"), "B♭7")
    XCTAssertEqual(ChordSymbol.glyphs("F#m7b5"), "F♯m7♭5")
    XCTAssertEqual(ChordSymbol.glyphs("bVII"), "♭VII")
    XCTAssertEqual(ChordSymbol.glyphs("#iv°"), "♯iv°")
  }

  func testGlyphsPassEverythingElseThrough() {
    XCTAssertEqual(ChordSymbol.glyphs("CΔ7"), "CΔ7")
    XCTAssertEqual(ChordSymbol.glyphs("vii°"), "vii°")
    XCTAssertEqual(ChordSymbol.glyphs("Bø7/D"), "Bø7/D")
    XCTAssertEqual(ChordSymbol.glyphs(""), "")
  }

  func testSpokenNameOfNaturalRoots() {
    XCTAssertEqual(ChordSymbol.spokenName(symbol: "C", type: "maj"), "C major")
    XCTAssertEqual(ChordSymbol.spokenName(symbol: "Am", type: "min"), "A minor")
    XCTAssertEqual(ChordSymbol.spokenName(symbol: "G7", type: "7"), "G dominant seventh")
  }

  func testSpokenNameReadsTheAccidental() {
    XCTAssertEqual(ChordSymbol.spokenName(symbol: "F#m7b5", type: "m7b5"), "F sharp half-diminished seventh")
    XCTAssertEqual(ChordSymbol.spokenName(symbol: "Bbmaj7", type: "maj7"), "B flat major seventh")
  }

  func testSpokenNameOfAnUnknownTypeReadsTheSuffix() {
    XCTAssertEqual(ChordSymbol.spokenName(symbol: "C13", type: "13"), "C 13")
    XCTAssertEqual(ChordSymbol.spokenName(symbol: "Ebx", type: "x"), "E flat x")
  }

  func testEveryDictionaryTypeHasASpokenQuality() {
    let types = ["5", "maj", "min", "dim", "aug", "sus2", "sus4", "6", "m6", "maj7", "7", "m7", "m7b5", "dim7",
                 "mMaj7", "augMaj7", "add9", "madd9", "9", "maj9", "m9", "69", "7sus4"]
    for type in types {
      XCTAssertNotNil(ChordSymbol.spokenQualities[type], type)
    }
  }

  func testPitchClassNamesUseSharpsAndWrap() {
    XCTAssertEqual((0..<12).map(ChordSymbol.pitchClassName),
                   ["C", "C♯", "D", "D♯", "E", "F", "F♯", "G", "G♯", "A", "A♯", "B"])
    XCTAssertEqual(ChordSymbol.pitchClassName(13), "C♯")
    XCTAssertEqual(ChordSymbol.pitchClassName(-1), "B")
  }

  func testNoteNamesWithMiddleCAsC4() {
    XCTAssertEqual(ChordSymbol.noteName(midi: 60), "C4")
    XCTAssertEqual(ChordSymbol.noteName(midi: 61), "C♯4")
    XCTAssertEqual(ChordSymbol.noteName(midi: 59), "B3")
    XCTAssertEqual(ChordSymbol.noteName(midi: 0), "C-1")
    XCTAssertEqual(ChordSymbol.noteName(midi: 127), "G9")
  }
}
