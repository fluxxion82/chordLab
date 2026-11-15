import XCTest
@testable import ChordLabUICore

/// Snap pitch (UI/Core/PitchSnap.swift, tracks design D14, D15).
final class PitchSnapTests: XCTestCase {
  /// E7 in C major, from engine/tests/test_guide.cpp ("Rt.tca.ct.c." read from the root E): E root; G#, B, D chord
  /// tones; F, G, C tensions (G by the dominant exception); A avoid; C#, D#, F#, A# outside. The chord scale is
  /// E F G# A B C D (G# replaced G), mask bits {0, 2, 4, 5, 8, 9, 11} = 2869.
  private let e7: GuideJSON = {
    let roles: [GuideRole] = [
      .tension,   // 0  C
      .outside,   // 1  C#
      .chord,     // 2  D
      .outside,   // 3  D#
      .root,      // 4  E
      .tension,   // 5  F
      .outside,   // 6  F#
      .tension,   // 7  G
      .chord,     // 8  G#
      .avoid,     // 9  A
      .outside,   // 10 A#
      .chord,     // 11 B
    ]
    return GuideJSON(roles: roles, scale: 2869, degrees: Array(repeating: "", count: 12), names: Array(repeating: "", count: 12))
  }()

  // MARK: - Titles and default

  func testTitlesOrderAndDefault() {
    XCTAssertEqual(PitchSnap.allCases, [.off, .scale, .chordAndTensions, .chordTones])
    XCTAssertEqual(PitchSnap.allCases.map(\.title), ["Off", "Scale", "Chord + tensions", "Chord tones"])
    XCTAssertEqual(PitchSnap.default, .off)
  }

  // MARK: - The E7 guide as literal data

  func testTheLiteralGuideIsTheEnginesE7InC() {
    XCTAssertEqual(e7.scale, (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 8) | (1 << 9) | (1 << 11))
    XCTAssertFalse(e7.inScale(pc: 7), "G is not in the chord scale (G# replaced it)")
    XCTAssertEqual(e7.role(pc: 7), .tension, "yet G is a tension by the dominant exception")
    XCTAssertEqual(e7.role(pc: 4), .root)
  }

  // MARK: - allows

  func testEveryPitchClassIsAllowedWhenOff() {
    for pc in 0..<12 { XCTAssertTrue(PitchSnap.off.allows(pc: pc, guide: e7)) }
  }

  func testScaleAllowsTheMaskPitchClassesAndNotG() {
    let allowed = (0..<12).filter { PitchSnap.scale.allows(pc: $0, guide: e7) }
    XCTAssertEqual(allowed, [0, 2, 4, 5, 8, 9, 11])
    XCTAssertFalse(PitchSnap.scale.allows(pc: 7, guide: e7))
    XCTAssertTrue(PitchSnap.scale.allows(pc: 9, guide: e7), "the avoid note A is in the scale")
  }

  func testChordAndTensionsAllowRootChordAndTensionRoles() {
    let allowed = (0..<12).filter { PitchSnap.chordAndTensions.allows(pc: $0, guide: e7) }
    XCTAssertEqual(allowed, [0, 2, 4, 5, 7, 8, 11])
    XCTAssertTrue(PitchSnap.chordAndTensions.allows(pc: 7, guide: e7), "G is a tension")
    XCTAssertFalse(PitchSnap.chordAndTensions.allows(pc: 9, guide: e7), "A is avoid")
  }

  func testChordTonesAllowRootAndChordRoles() {
    let allowed = (0..<12).filter { PitchSnap.chordTones.allows(pc: $0, guide: e7) }
    XCTAssertEqual(allowed, [2, 4, 8, 11])
  }

  func testAllowsTakesAnyIntegerAsAPitchClass() {
    XCTAssertTrue(PitchSnap.chordTones.allows(pc: 64, guide: e7))
    XCTAssertTrue(PitchSnap.chordTones.allows(pc: -8, guide: e7))
    XCTAssertFalse(PitchSnap.chordTones.allows(pc: 67, guide: e7))
  }

  // MARK: - snap

  func testOffAndNoGuideLeaveThePitchAlone() {
    for pitch in [0, 1, 67, 127] {
      XCTAssertEqual(PitchSnap.off.snap(pitch: pitch, guide: e7), pitch)
      for option in PitchSnap.allCases { XCTAssertEqual(option.snap(pitch: pitch, guide: nil), pitch) }
    }
  }

  func testAnAllowedPitchStaysPut() {
    for option in PitchSnap.allCases {
      for pitch in 0...127 where option.allows(pc: pitch, guide: e7) {
        XCTAssertEqual(option.snap(pitch: pitch, guide: e7), pitch, "\(option) \(pitch)")
      }
    }
  }

  func testScaleSnapsGToTheNearestScaleNoteWhichIsGSharp() {
    // 67 (G): G# (68) is one semitone up, F# (66) is not in the scale and F (65) is two down.
    XCTAssertEqual(PitchSnap.scale.snap(pitch: 67, guide: e7), 68)
  }

  func testChordAndTensionsKeepG() {
    XCTAssertEqual(PitchSnap.chordAndTensions.snap(pitch: 67, guide: e7), 67)
  }

  func testChordTonesSnapGToGSharp() {
    XCTAssertEqual(PitchSnap.chordTones.snap(pitch: 67, guide: e7), 68)
  }

  func testChordAndTensionsSnapTheAvoidNoteAndTheOutsideNotes() {
    // A (69): G# (68) is one below, G (67) and B (71) two away, so 68.
    XCTAssertEqual(PitchSnap.chordAndTensions.snap(pitch: 69, guide: e7), 68)
    // A# (70): B (71) one up; A (69) is avoid.
    XCTAssertEqual(PitchSnap.chordAndTensions.snap(pitch: 70, guide: e7), 71)
    // C# (61): D (62) one up and C (60) one down, both allowed: the tie goes up.
    XCTAssertEqual(PitchSnap.chordAndTensions.snap(pitch: 61, guide: e7), 62)
  }

  func testATieGoesUp() {
    // Scale, A# (70): A (69) and B (71) are both allowed and one semitone away.
    XCTAssertEqual(PitchSnap.scale.snap(pitch: 70, guide: e7), 71)
    // Scale, C# (61): C (60) and D (62).
    XCTAssertEqual(PitchSnap.scale.snap(pitch: 61, guide: e7), 62)
    // Scale, D# (63): D (62) and E (64).
    XCTAssertEqual(PitchSnap.scale.snap(pitch: 63, guide: e7), 64)
    // Chord tones, F# (66): E (64) and G# (68) are both two away.
    XCTAssertEqual(PitchSnap.chordTones.snap(pitch: 66, guide: e7), 68)
    // Chord tones, C (60): B (59) is one down, D (62) two up: the nearer wins, not the upper.
    XCTAssertEqual(PitchSnap.chordTones.snap(pitch: 60, guide: e7), 59)
    // Chord tones, C# (61): D (62) one up.
    XCTAssertEqual(PitchSnap.chordTones.snap(pitch: 61, guide: e7), 62)
    // Chord tones, A (69): G# (68) one down, B (71) two up.
    XCTAssertEqual(PitchSnap.chordTones.snap(pitch: 69, guide: e7), 68)
  }

  func testATieGoesUpAcrossAWiderGap() {
    let c = GuideJSON(roles: [.root] + Array(repeating: .outside, count: 11), scale: 1,
                      degrees: Array(repeating: "", count: 12), names: Array(repeating: "", count: 12))
    XCTAssertEqual(PitchSnap.chordTones.snap(pitch: 66, guide: c), 72, "F# is six from both C: up")
    XCTAssertEqual(PitchSnap.chordTones.snap(pitch: 65, guide: c), 60)
    XCTAssertEqual(PitchSnap.chordTones.snap(pitch: 67, guide: c), 72)
    XCTAssertEqual(PitchSnap.scale.snap(pitch: 66, guide: c), 72)
  }

  func testTheEdgesAreNeverLeft() {
    // Pitch 0 (C) is a tension: chord tones go up to D (2).
    XCTAssertEqual(PitchSnap.chordTones.snap(pitch: 0, guide: e7), 2)
    XCTAssertEqual(PitchSnap.scale.snap(pitch: 0, guide: e7), 0)
    XCTAssertEqual(PitchSnap.scale.snap(pitch: 1, guide: e7), 2, "C# at the bottom: C and D tie, up")
    // Pitch 127 (G): chord tones have nothing above (G# would be 128): E (124) is the nearest below.
    XCTAssertEqual(PitchSnap.chordTones.snap(pitch: 127, guide: e7), 124)
    XCTAssertEqual(PitchSnap.scale.snap(pitch: 127, guide: e7), 125, "G# would be 128, so F (125)")
    XCTAssertEqual(PitchSnap.chordAndTensions.snap(pitch: 127, guide: e7), 127)
    for option in PitchSnap.allCases {
      for pitch in 0...127 {
        let snapped = option.snap(pitch: pitch, guide: e7)
        XCTAssertTrue((0...127).contains(snapped), "\(option) \(pitch)")
        XCTAssertTrue(option.allows(pc: snapped, guide: e7), "\(option) \(pitch)")
      }
    }
  }

  func testTheResultIsTheNearestAllowedPitchAndNeverFartherThanAnAllowedOne() {
    for option in PitchSnap.allCases {
      for pitch in 0...127 {
        let snapped = option.snap(pitch: pitch, guide: e7)
        let nearest = (0...127).filter { option.allows(pc: $0, guide: e7) }.map { abs($0 - pitch) }.min()
        XCTAssertEqual(abs(snapped - pitch), nearest, "\(option) \(pitch)")
        if let distance = nearest, distance > 0, pitch + distance <= 127, option.allows(pc: pitch + distance, guide: e7) {
          XCTAssertEqual(snapped, pitch + distance, "a tie goes up: \(option) \(pitch)")
        }
      }
    }
  }

  func testAnOutOfRangePitchIsClampedBeforeSnapping() {
    XCTAssertEqual(PitchSnap.chordTones.snap(pitch: 300, guide: e7), 124)
    XCTAssertEqual(PitchSnap.chordTones.snap(pitch: -40, guide: e7), 2)
    XCTAssertEqual(PitchSnap.off.snap(pitch: 300, guide: e7), 300, "off changes nothing, even out of range")
  }

  func testAGuideWithNothingAllowedLeavesThePitchAlone() {
    let nothing = GuideJSON(roles: Array(repeating: .outside, count: 12), scale: 0, degrees: [], names: [])
    for option in [PitchSnap.scale, .chordAndTensions, .chordTones] {
      XCTAssertEqual(option.snap(pitch: 67, guide: nothing), 67)
    }
    let short = GuideJSON(roles: [], scale: 0, degrees: [], names: [])
    XCTAssertEqual(PitchSnap.chordTones.snap(pitch: 67, guide: short), 67)
  }

  // MARK: - The chord at a position

  private func fixtureEvents() throws -> [EventJSON] {
    let url = try SnapshotTests.fixturesDirectory().appendingPathComponent("snapshot-tracks.json")
    return try JSONDecoder().decode(Snapshot.self, from: Data(contentsOf: url)).events
  }

  func testTheLookupFindsTheGuideForSnapping() throws {
    let events = try fixtureEvents()
    // In the G7 (beats 9-15) the guide is the fixture's own: chord tones D, F, B, root G.
    let guide = try XCTUnwrap(PlaySpans.event(atTick: 10 * 960, in: events, ticksPerBeat: 960)?.guide)
    XCTAssertEqual(PitchSnap.chordTones.snap(pitch: 71, guide: guide), 71)
    XCTAssertEqual(PitchSnap.chordTones.snap(pitch: 69, guide: guide), 71, "A: G (67) and B (71) tie, up")
    XCTAssertEqual(PitchSnap.chordTones.snap(pitch: 64, guide: guide), 65, "E is a tension of G7: F (65) is one up")
  }
}
