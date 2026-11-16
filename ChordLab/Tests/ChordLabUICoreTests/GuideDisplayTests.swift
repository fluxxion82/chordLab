import XCTest
@testable import ChordLabUICore

/// Which parts of the harmonic guide the roll draws (UI/Core/GuideDisplay.swift): the calmer defaults, the View
/// menu's model, and the raw form remembered in UserDefaults. The storage key and the raw form are persisted, so
/// changing either would silently reset every user's choice: they are pinned here.
final class GuideDisplayTests: XCTestCase {
  func testDefaultsShowRootChordTonesAndLabelsOnly() {
    let display = GuideDisplay.defaultDisplay
    XCTAssertEqual(display, GuideDisplay())
    XCTAssertTrue(display.root)
    XCTAssertTrue(display.chordTones)
    XCTAssertFalse(display.tensions)
    XCTAssertFalse(display.avoid)
    XCTAssertTrue(display.labels)
  }

  func testWhichGuideRolesDrawABand() {
    let display = GuideDisplay.defaultDisplay
    XCTAssertTrue(display.shows(.root))
    XCTAssertTrue(display.shows(.chord))
    XCTAssertFalse(display.shows(.tension))
    XCTAssertFalse(display.shows(.avoid))
    XCTAssertFalse(display.shows(.outside), "Outside never draws")
    XCTAssertFalse(GuideDisplay.everything.shows(.outside))
    for role in [GuideRole.root, .chord, .tension, .avoid] {
      XCTAssertTrue(GuideDisplay.everything.shows(role), "\(role)")
    }
  }

  func testTheMenuEntriesInOrderWithTitlesAndState() {
    let entries = GuideDisplay.defaultDisplay.entries
    XCTAssertEqual(entries.map(\.part), [.root, .chordTones, .tensions, .avoid, .labels])
    XCTAssertEqual(entries.map(\.title), ["Root", "Chord tones", "Tensions", "Avoid notes", "Degree labels"])
    XCTAssertEqual(entries.map(\.isOn), [true, true, false, false, true])
    XCTAssertEqual(Set(entries.map(\.id)).count, 5, "unique ids")
    XCTAssertEqual(GuideDisplay.Part.allCases, entries.map(\.part))
  }

  func testToggledFlipsOnePartOnly() {
    let display = GuideDisplay.defaultDisplay
    let withTensions = display.toggled(.tensions)
    XCTAssertTrue(withTensions.tensions)
    XCTAssertEqual(withTensions.entries.map(\.isOn), [true, true, true, false, true])
    XCTAssertEqual(withTensions.toggled(.tensions), display, "toggling twice is the identity")
    for part in GuideDisplay.Part.allCases {
      let toggled = display.toggled(part)
      XCTAssertNotEqual(toggled[part], display[part], "\(part)")
      for other in GuideDisplay.Part.allCases where other != part {
        XCTAssertEqual(toggled[other], display[other], "\(part) leaves \(other)")
      }
    }
  }

  func testTheSummaryNamesWhatIsShown() {
    XCTAssertEqual(GuideDisplay.defaultDisplay.summary, "Root, Chord tones, Degree labels")
    XCTAssertEqual(GuideDisplay.nothing.summary, "No guides")
    XCTAssertEqual(GuideDisplay.everything.summary, "Root, Chord tones, Tensions, Avoid notes, Degree labels")
  }

  // MARK: - Storage

  func testStorageKeyIsPinned() {
    XCTAssertEqual(GuideDisplay.storageKey, "chordlab.guideDisplay")
  }

  func testRawFormIsPinned() {
    XCTAssertEqual(GuideDisplay.defaultDisplay.rawValue, "root=1,chord=1,tension=0,avoid=0,labels=1")
    XCTAssertEqual(GuideDisplay.everything.rawValue, "root=1,chord=1,tension=1,avoid=1,labels=1")
    XCTAssertEqual(GuideDisplay.nothing.rawValue, "root=0,chord=0,tension=0,avoid=0,labels=0")
  }

  func testEveryValueThisBuildWritesRoundTrips() {
    // All 32 combinations.
    for bits in 0..<32 {
      var display = GuideDisplay.nothing
      for (index, part) in GuideDisplay.Part.allCases.enumerated() where bits & (1 << index) != 0 {
        display = display.toggled(part)
      }
      XCTAssertEqual(GuideDisplay(rawValue: display.rawValue), display, display.rawValue)
    }
  }

  func testAMissingOrUnknownValueReadsAsTheDefault() {
    XCTAssertEqual(GuideDisplay(rawValue: nil), .defaultDisplay)
    XCTAssertEqual(GuideDisplay(rawValue: ""), .defaultDisplay)
    XCTAssertEqual(GuideDisplay(rawValue: "garbage"), .defaultDisplay)
    XCTAssertEqual(GuideDisplay(rawValue: "{\"root\":true}"), .defaultDisplay)
  }

  func testAPartialOrLaterValueKeepsWhatItKnows() {
    // A part a value does not mention keeps its default; unknown parts (a later build's) and malformed entries are
    // skipped.
    let partial = GuideDisplay(rawValue: "tension=1")
    XCTAssertEqual(partial, GuideDisplay.defaultDisplay.toggled(.tensions))
    let later = GuideDisplay(rawValue: "root=0,chord=1,bassline=1,tension=1,avoid=1,labels=0,colour=blue")
    XCTAssertEqual(later.entries.map(\.isOn), [false, true, true, true, false])
    let malformed = GuideDisplay(rawValue: "root=yes,chord,=1,avoid=1, labels = 0 ")
    XCTAssertEqual(malformed.entries.map(\.isOn), [true, true, false, true, false],
                   "only avoid=1 and the trimmed labels = 0 are understood")
  }
}
