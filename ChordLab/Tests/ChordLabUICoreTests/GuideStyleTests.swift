import SwiftUI
import XCTest
@testable import ChordLabUICore

/// How a guide band is drawn (UI/Core/GuideStyle.swift, tracks design D14).
final class GuideStyleTests: XCTestCase {
  private let emphasisedRoles: [GuideRole] = [.root, .chord, .tension]
  private let allRoles: [GuideRole] = [.root, .chord, .tension, .avoid, .outside]

  private func opacity(_ role: GuideRole, _ track: TrackRole) -> Double { GuideStyle.opacity(role: role, track: track) }

  func testTheRolesStepDownFromTheRootToAvoid() {
    for track in TrackRole.allCases {
      XCTAssertGreaterThan(opacity(.root, track), opacity(.chord, track), "\(track)")
      XCTAssertGreaterThan(opacity(.chord, track), opacity(.tension, track), "\(track)")
      XCTAssertGreaterThan(opacity(.tension, track), opacity(.avoid, track), "\(track)")
      XCTAssertGreaterThan(opacity(.avoid, track), 0, "\(track)")
    }
  }

  func testEveryTrackRoleShowsTheSameGuide() {
    // One hue per guide role, so a colour means the same thing on a Bass, a Lead and an Other track.
    for role in allRoles {
      XCTAssertEqual(Set(TrackRole.allCases.map { opacity(role, $0) }).count, 1, "\(role)")
    }
  }

  func testOutsideIsNeverDrawn() {
    for track in TrackRole.allCases { XCTAssertEqual(opacity(.outside, track), 0, "\(track)") }
  }

  func testOpacitiesAreFractionsAndTheTableIsPinned() {
    let values: [Double] = [0.60, 0.48, 0.32, 0.16, 0]   // root, chord, tension, avoid, outside
    for track in TrackRole.allCases {
      for (role, value) in zip(allRoles, values) {
        XCTAssertEqual(opacity(role, track), value, accuracy: 1e-12, "\(role) on \(track)")
        XCTAssertTrue((0...1).contains(opacity(role, track)))
      }
    }
  }

  func testOneHuePerRole() {
    XCTAssertEqual(hex(GuideStyle.color(role: .root)), "#F7C65A", "gold")
    XCTAssertEqual(hex(GuideStyle.color(role: .chord)), "#C9DEFF", "ice")
    XCTAssertEqual(hex(GuideStyle.color(role: .tension)), "#4FD1BF", "aqua")
    XCTAssertEqual(hex(GuideStyle.color(role: .avoid)), "#A39B8C", "a neutral taupe: never red, which the chords use")
    XCTAssertEqual(GuideStyle.color(role: .outside), Color.clear)
    XCTAssertEqual(Set(emphasisedRoles.map { hex(GuideStyle.color(role: $0)) }).count, 3, "three different hues")
  }

  func testDegreeLabelsOnRootChordAndTensionRowsFromTwelvePoints() {
    for role in emphasisedRoles {
      XCTAssertTrue(GuideStyle.showsDegreeLabel(role: role, rowHeight: 12), "\(role)")
      XCTAssertTrue(GuideStyle.showsDegreeLabel(role: role, rowHeight: 24), "\(role)")
      XCTAssertFalse(GuideStyle.showsDegreeLabel(role: role, rowHeight: 11.99), "\(role)")
      XCTAssertFalse(GuideStyle.showsDegreeLabel(role: role, rowHeight: 8), "\(role)")
    }
  }

  func testNoDegreeLabelOnAvoidOrOutsideRows() {
    for role in [GuideRole.avoid, .outside] {
      XCTAssertFalse(GuideStyle.showsDegreeLabel(role: role, rowHeight: 24), "\(role)")
    }
  }

  func testLabelColoursAreBrightAndTheRootsIsItsOwn() {
    XCTAssertEqual(hex(GuideStyle.labelColor(role: .root)), "#FFE9B0", "pale gold on the gold band")
    XCTAssertEqual(hex(GuideStyle.labelColor(role: .chord)), "#F4F7FF")
    XCTAssertEqual(hex(GuideStyle.labelColor(role: .tension)), "#C3CDE8", "a step dimmer: a tension, not a chord tone")
  }

  func testTheDefaultRowHeightShowsLabels() {
    XCTAssertTrue(GuideStyle.showsDegreeLabel(role: .root, rowHeight: RollGeometry.defaultRowHeight))
    XCTAssertFalse(GuideStyle.showsDegreeLabel(role: .root, rowHeight: RollGeometry.rowHeightRange.lowerBound))
  }
}
