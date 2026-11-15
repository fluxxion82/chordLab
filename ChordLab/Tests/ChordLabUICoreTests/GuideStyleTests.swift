import SwiftUI
import XCTest
@testable import ChordLabUICore

/// How a guide band is drawn (UI/Core/GuideStyle.swift, tracks design D14).
final class GuideStyleTests: XCTestCase {
  private let emphasisedRoles: [GuideRole] = [.root, .chord, .tension]
  private let allRoles: [GuideRole] = [.root, .chord, .tension, .avoid, .outside]

  private func opacity(_ role: GuideRole, _ track: TrackRole) -> Double { GuideStyle.opacity(role: role, track: track) }

  func testBassEmphasisesTheRootThenTheChordTones() {
    XCTAssertGreaterThan(opacity(.root, .bass), opacity(.chord, .bass))
    XCTAssertGreaterThan(opacity(.chord, .bass), opacity(.tension, .bass))
    XCTAssertGreaterThan(opacity(.tension, .bass), opacity(.avoid, .bass))
    XCTAssertGreaterThan(opacity(.avoid, .bass), 0)
  }

  func testLeadEmphasisesChordTonesAndTensionsEqually() {
    XCTAssertEqual(opacity(.chord, .lead), opacity(.tension, .lead))
    XCTAssertGreaterThan(opacity(.chord, .lead), opacity(.root, .lead))
    XCTAssertGreaterThan(opacity(.root, .lead), opacity(.avoid, .lead))
    XCTAssertGreaterThan(opacity(.avoid, .lead), 0)
  }

  func testOtherIsEven() {
    XCTAssertEqual(opacity(.root, .other), opacity(.chord, .other))
    XCTAssertEqual(opacity(.chord, .other), opacity(.tension, .other))
    XCTAssertGreaterThan(opacity(.tension, .other), opacity(.avoid, .other))
    XCTAssertGreaterThan(opacity(.avoid, .other), 0)
  }

  func testOutsideIsNeverDrawn() {
    for track in TrackRole.allCases { XCTAssertEqual(opacity(.outside, track), 0, "\(track)") }
  }

  func testAvoidIsAFaintTintOnEveryTrackRoleBelowEveryOtherRole() {
    for track in TrackRole.allCases {
      let avoid = opacity(.avoid, track)
      XCTAssertGreaterThan(avoid, 0, "\(track)")
      for role in emphasisedRoles { XCTAssertLessThan(avoid, opacity(role, track), "\(role) on \(track)") }
    }
    XCTAssertEqual(Set(TrackRole.allCases.map { opacity(.avoid, $0) }).count, 1, "one avoid tint for every role")
  }

  func testOpacitiesAreFractionsAndTheTableIsPinned() {
    let table: [(TrackRole, [Double])] = [   // root, chord, tension, avoid, outside
      (.bass, [0.36, 0.24, 0.12, 0.06, 0]),
      (.lead, [0.18, 0.30, 0.30, 0.06, 0]),
      (.other, [0.20, 0.20, 0.20, 0.06, 0]),
    ]
    for (track, values) in table {
      for (role, value) in zip(allRoles, values) {
        XCTAssertEqual(opacity(role, track), value, accuracy: 1e-12, "\(role) on \(track)")
        XCTAssertTrue((0...1).contains(opacity(role, track)))
      }
    }
  }

  func testColoursAreCoolForTheStableRolesAndWarmForAvoid() {
    XCTAssertEqual(hex(GuideStyle.color(role: .root)), "#6E8FD8")
    XCTAssertEqual(hex(GuideStyle.color(role: .chord)), "#6E8FD8")
    XCTAssertEqual(hex(GuideStyle.color(role: .tension)), "#6E8FD8")
    XCTAssertEqual(hex(GuideStyle.color(role: .avoid)), "#D9904A")
    XCTAssertEqual(GuideStyle.color(role: .outside), Color.clear)
  }

  func testDegreeLabelsOnRootAndChordRowsFromTwelvePoints() {
    XCTAssertTrue(GuideStyle.showsDegreeLabel(role: .root, rowHeight: 12))
    XCTAssertTrue(GuideStyle.showsDegreeLabel(role: .chord, rowHeight: 14))
    XCTAssertTrue(GuideStyle.showsDegreeLabel(role: .root, rowHeight: 24))
    XCTAssertFalse(GuideStyle.showsDegreeLabel(role: .root, rowHeight: 11.99))
    XCTAssertFalse(GuideStyle.showsDegreeLabel(role: .chord, rowHeight: 8))
  }

  func testNoDegreeLabelOnTensionAvoidOrOutsideRows() {
    for role in [GuideRole.tension, .avoid, .outside] {
      XCTAssertFalse(GuideStyle.showsDegreeLabel(role: role, rowHeight: 24), "\(role)")
    }
  }

  func testTheDefaultRowHeightShowsLabels() {
    XCTAssertTrue(GuideStyle.showsDegreeLabel(role: .root, rowHeight: RollGeometry.defaultRowHeight))
    XCTAssertFalse(GuideStyle.showsDegreeLabel(role: .root, rowHeight: RollGeometry.rowHeightRange.lowerBound))
  }
}
