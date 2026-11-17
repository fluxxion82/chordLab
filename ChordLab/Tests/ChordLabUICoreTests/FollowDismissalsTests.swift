import Foundation
import XCTest
@testable import ChordLabUICore

/// The banner's dismissals for the life of the process (UI/Core/FollowDismissals.swift).
final class FollowDismissalsTests: XCTestCase {
  func testADismissedDocumentStaysDismissedAndANewOneMayOffer() {
    let d = FollowDismissals()
    XCTAssertFalse(d.isDismissed("doc-1"))
    d.dismiss("doc-1")
    XCTAssertTrue(d.isDismissed("doc-1"))
    XCTAssertFalse(d.isDismissed("doc-2"))
    d.dismiss("doc-1")
    XCTAssertEqual(d.count, 1)
  }

  func testTheOldestGoesFirstBeyondTheCapacity() {
    let d = FollowDismissals(capacity: 3)
    for token in ["a", "b", "c"] { d.dismiss(token) }
    d.dismiss("a")                                                   // dismissed again: the newest now
    d.dismiss("d")                                                   // drops b, the oldest
    XCTAssertFalse(d.isDismissed("b"))
    XCTAssertTrue(d.isDismissed("a"))
    XCTAssertTrue(d.isDismissed("c"))
    XCTAssertTrue(d.isDismissed("d"))
    XCTAssertEqual(d.count, 3)
  }

  func testTheSharedOneHoldsTheDefaultCapacity() {
    XCTAssertEqual(FollowDismissals.defaultCapacity, 256)
    XCTAssertEqual(FollowDismissals.shared.capacity, 256)
    XCTAssertTrue(FollowDismissals.shared === FollowDismissals.shared)
    XCTAssertEqual(FollowDismissals(capacity: 0).capacity, 1)
  }

  /// Two editors of one instance (the window closed and opened again) share the process's dismissals: the banner
  /// dismissed in the first does not come back in the second.
  func testASecondEditorOfTheSameDocumentDoesNotOffer() throws {
    let url = try SnapshotTests.fixturesDirectory().appendingPathComponent("snapshot-home-empty.json")
    let fresh = try JSONDecoder().decode(Snapshot.self, from: Data(contentsOf: url))
    var link = try LinkStateTests.fixture("link-state-copy.json")
    link = LinkStateJSON(schema: link.schema, instance: link.instance, following: nil, leaders: link.leaders, routed: [])
    let shared = FollowDismissals()
    XCTAssertNotNil(FollowOffer.make(link: link, snapshot: fresh, lineage: "doc-1", dismissals: shared))
    shared.dismiss("doc-1")
    XCTAssertNil(FollowOffer.make(link: link, snapshot: fresh, lineage: "doc-1", dismissals: shared))
    XCTAssertNotNil(FollowOffer.make(link: link, snapshot: fresh, lineage: "doc-2", dismissals: shared))
  }
}
