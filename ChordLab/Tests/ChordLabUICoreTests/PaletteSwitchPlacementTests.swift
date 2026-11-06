import Foundation
import XCTest
@testable import ChordLabUICore

/// Where the Diatonic / Applied switch sits (UI/Core/PaletteSwitchPlacement.swift): one place at most, and
/// somewhere while the palette has an applied view, except in the one case the compact key hint creates.
final class PaletteSwitchPlacementTests: XCTestCase {
  private static let bools = [false, true]

  /// The expected placement for one combination, written out case by case rather than from the rule's code.
  private func expected(heardActive: Bool, hasApplied: Bool, homeRowEmpty: Bool,
                        switchRowAllowed: Bool) -> PaletteSwitchPlacement {
    switch (heardActive, hasApplied, homeRowEmpty, switchRowAllowed) {
    case (_, false, _, _): return .none                 // no applied view: no switch
    case (true, true, _, _): return .paletteRow         // the Heard group's row holds it
    case (false, true, false, _): return .homeRow       // the home row shows: at its end
    case (false, true, true, true): return .paletteRow  // nothing else shows: above the grid
    case (false, true, true, false): return .none       // the compact key hint took the row
    }
  }

  func testTruthTable() {
    for heardActive in Self.bools {
      for hasApplied in Self.bools {
        for homeRowEmpty in Self.bools {
          for allowed in Self.bools {
            XCTAssertEqual(
              PaletteSwitchPlacement.resolve(heardActive: heardActive, hasApplied: hasApplied,
                                             homeRowEmpty: homeRowEmpty, switchRowAllowed: allowed),
              expected(heardActive: heardActive, hasApplied: hasApplied, homeRowEmpty: homeRowEmpty,
                       switchRowAllowed: allowed),
              "heard \(heardActive), applied \(hasApplied), home row empty \(homeRowEmpty), row allowed \(allowed)")
          }
        }
      }
    }
  }

  func testNoneOnlyWithoutAppliedOrUnderTheCompactHint() {
    for heardActive in Self.bools {
      for hasApplied in Self.bools {
        for homeRowEmpty in Self.bools {
          for allowed in Self.bools {
            let placement = PaletteSwitchPlacement.resolve(heardActive: heardActive, hasApplied: hasApplied,
                                                           homeRowEmpty: homeRowEmpty, switchRowAllowed: allowed)
            let compactHint = !heardActive && homeRowEmpty && !allowed
            if hasApplied && !compactHint {
              XCTAssertNotEqual(placement, .none,
                                "heard \(heardActive), home row empty \(homeRowEmpty), row allowed \(allowed)")
            }
            if placement == .none {
              XCTAssertTrue(!hasApplied || compactHint,
                            "heard \(heardActive), home row empty \(homeRowEmpty), row allowed \(allowed)")
            }
          }
        }
      }
    }
  }

  func testListeningAlwaysUsesThePaletteRow() {
    for homeRowEmpty in Self.bools {
      for allowed in Self.bools {
        XCTAssertEqual(PaletteSwitchPlacement.resolve(heardActive: true, hasApplied: true,
                                                      homeRowEmpty: homeRowEmpty, switchRowAllowed: allowed),
                       .paletteRow)
      }
    }
  }

  func testTheHomeRowIgnoresTheCompactHint() {
    XCTAssertEqual(PaletteSwitchPlacement.resolve(heardActive: false, hasApplied: true, homeRowEmpty: false,
                                                  switchRowAllowed: false), .homeRow)
  }
}
