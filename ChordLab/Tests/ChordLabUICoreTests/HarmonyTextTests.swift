import XCTest
@testable import ChordLabUICore

/// Display wording of the Phase 2 harmony fields (UI/HarmonyText.swift).
final class HarmonyTextTests: XCTestCase {
  // MARK: - Function words and tension

  func testFunctionWords() {
    XCTAssertEqual(HarmonyText.functionWord("T"), "Tonic")
    XCTAssertEqual(HarmonyText.functionWord("PD"), "Predominant")
    XCTAssertEqual(HarmonyText.functionWord("D"), "Dominant")
    XCTAssertEqual(HarmonyText.functionWord("O"), "Other")
  }

  func testUnknownFunctionIsOtherAndNilIsNil() {
    XCTAssertEqual(HarmonyText.functionWord("X"), "Other")
    XCTAssertNil(HarmonyText.functionWord(nil))
  }

  func testTensionFraction() {
    XCTAssertEqual(HarmonyText.tensionFraction(42), 0.42)
    XCTAssertEqual(HarmonyText.tensionFraction(0), 0)
    XCTAssertEqual(HarmonyText.tensionFraction(100), 1)
  }

  func testTensionFractionClampsAndNilIsZero() {
    XCTAssertEqual(HarmonyText.tensionFraction(-5), 0)
    XCTAssertEqual(HarmonyText.tensionFraction(250), 1)
    XCTAssertEqual(HarmonyText.tensionFraction(nil), 0)
  }

  // MARK: - "Why" headline and chips

  private let why = WhyJSON(
    commonTones: 2, commonPcs: [0, 4], commonNames: ["C", "E"], rootMotion: "down a fifth",
    voiceLeadingCost: 4, borrowedFrom: nil, borrowedFromName: nil,
    text: "G7 → C: down a fifth, resolves (D→T), 2 common tones (C, E), smooth voice leading (cost 4)")

  private let borrowed = WhyJSON(
    commonTones: 0, commonPcs: [], commonNames: [], rootMotion: "up a third", voiceLeadingCost: 6,
    borrowedFrom: KeyJSON(tonic: TonicJSON(letter: "C", alter: 0), mode: "Aeolian"),
    borrowedFromName: "C Aeolian", text: "C → Eb: up a third, borrowed from C Aeolian")

  func testWhyHeadlineIsTheEnginesSentence() {
    XCTAssertEqual(HarmonyText.whyHeadline(why), why.text)
  }

  func testWhyHeadlineOfTheFirstBlockSaysFirstChord() {
    XCTAssertEqual(HarmonyText.whyHeadline(nil), "first chord")
  }

  func testWhyChipsListCommonTonesMotionAndCost() {
    XCTAssertEqual(HarmonyText.whyChips(why), ["common C", "common E", "down a fifth", "cost 4"])
  }

  func testWhyChipsPutBorrowedFromLast() {
    XCTAssertEqual(HarmonyText.whyChips(borrowed), ["up a third", "cost 6", "borrowed from C Aeolian"])
  }

  func testNoWhyChipsForTheFirstBlock() {
    XCTAssertTrue(HarmonyText.whyChips(nil).isEmpty)
  }

  // MARK: - Voicings

  func testVoicingTextWithMiddleCAsC4() {
    XCTAssertEqual(HarmonyText.voicingText([60, 64, 67]), "C4  E4  G4")
    XCTAssertEqual(HarmonyText.voicingText([59, 62, 67, 70]), "B3  D4  G4  A♯4")
    XCTAssertEqual(HarmonyText.voicingText([]), "")
  }

  func testInversionAndOctaveLabels() {
    XCTAssertEqual(HarmonyText.inversionLabel(1, bass: 64), "Inversion 1 · bass E4")
    XCTAssertEqual(HarmonyText.inversionLabel(0, bass: nil), "Inversion 0")
    XCTAssertEqual(HarmonyText.octaveLabel(-1), "Octave −1")
    XCTAssertEqual(HarmonyText.octaveLabel(0), "Octave 0")
    XCTAssertEqual(HarmonyText.octaveLabel(1), "Octave +1")
  }

  func testStoredVoicingSoundsWhenManualOrWhenAutoIsOff() {
    XCTAssertTrue(HarmonyText.storedVoicingSounds(manual: true, autoVoiceLead: true))
    XCTAssertTrue(HarmonyText.storedVoicingSounds(manual: true, autoVoiceLead: false))
    XCTAssertTrue(HarmonyText.storedVoicingSounds(manual: false, autoVoiceLead: false))
    XCTAssertFalse(HarmonyText.storedVoicingSounds(manual: false, autoVoiceLead: true))
  }

  /// Auto on, not manual: the stored inversion/octave are not shown, since the chord may sound as
  /// [60,65,69] (second inversion, octave −1) whatever they say.
  func testVoicingStatusUnderAutoOmitsTheStoredFields() {
    XCTAssertEqual(HarmonyText.voicingStatus(inversion: 0, octave: 0, manual: false, autoVoiceLead: true, bass: 60),
                   "Automatic voicing · bass C4")
  }

  func testVoicingStatusOfAManualChordUnderAutoShowsTheStoredFields() {
    XCTAssertEqual(HarmonyText.voicingStatus(inversion: 2, octave: -1, manual: true, autoVoiceLead: true, bass: 53),
                   "Manual voicing · Inversion 2 · bass F3 · Octave −1")
  }

  func testVoicingStatusWithAutoOffShowsTheStoredFields() {
    XCTAssertEqual(HarmonyText.voicingStatus(inversion: 1, octave: 1, manual: false, autoVoiceLead: false, bass: 76),
                   "Default voicing · Inversion 1 · bass E5 · Octave +1")
  }

  func testVoicingStatusWithoutNotesHasNoBass() {
    XCTAssertEqual(HarmonyText.voicingStatus(inversion: 0, octave: 0, manual: false, autoVoiceLead: true, bass: nil),
                   "Automatic voicing")
  }

  // MARK: - Suggestion reasons

  func testReasonsLineJoinsWithMiddleDots() {
    XCTAssertEqual(HarmonyText.reasonsLine(["resolves (D→T)", "1 common tone (G)"]),
                   "resolves (D→T) · 1 common tone (G)")
    XCTAssertEqual(HarmonyText.reasonsLine([]), "")
  }

  // MARK: - Section names: the engine's 64-byte bound at a character boundary

  func testSectionNameBoundMatchesTheEngine() {
    XCTAssertEqual(HarmonyText.maxSectionNameBytes, 64, "kMaxSectionNameBytes in engine/include/chordlab/document.h")
  }

  func testShortSectionNameIsUntouched() {
    XCTAssertEqual(HarmonyText.boundedSectionName("Verse"), "Verse")
  }

  func testSixtyFourAsciiBytesPass() {
    let name = String(repeating: "a", count: 64)
    XCTAssertEqual(HarmonyText.boundedSectionName(name), name)
  }

  func testSixtyFiveAsciiBytesAreCutToSixtyFour() {
    XCTAssertEqual(HarmonyText.boundedSectionName(String(repeating: "a", count: 65)), String(repeating: "a", count: 64))
  }

  func testTwoByteCharactersAreCutWithinTheBound() {
    let name = String(repeating: "é", count: 33)   // 66 bytes
    XCTAssertEqual(HarmonyText.boundedSectionName(name), String(repeating: "é", count: 32))
  }

  func testCharacterStraddlingTheBoundIsDroppedWhole() {
    XCTAssertEqual(HarmonyText.boundedSectionName(String(repeating: "a", count: 63) + "é"),
                   String(repeating: "a", count: 63))
  }

  func testEmptySectionNameStaysEmpty() {
    XCTAssertEqual(HarmonyText.boundedSectionName(""), "")
  }
}
