import CoreGraphics
import XCTest
@testable import ChordLabUICore

/// The guide band layout the roll draws (UI/Core/GuideBands.swift, tracks design D14, D9, D6): one band per chord
/// over its playback span with a 12-entry role and opacity table, the degree labels, and the cache key.
final class GuideBandsTests: XCTestCase {
  private func fixture() throws -> Snapshot {
    let url = try SnapshotTests.fixturesDirectory().appendingPathComponent("snapshot-tracks.json")
    return try JSONDecoder().decode(Snapshot.self, from: Data(contentsOf: url))
  }

  private func bands(_ snapshot: Snapshot, role: TrackRole = .bass, geometry: RollGeometry = RollGeometry()) -> GuideBands {
    GuideBands(snapshot: snapshot, geometry: geometry, trackRole: role)
  }

  // MARK: - Spans

  func testOneBandPerChordOverItsPlaybackSpanInContentX() throws {
    let snapshot = try fixture()
    let layout = bands(snapshot)
    XCTAssertEqual(layout.bands.map(\.eventIndex), [0, 1, 2, 3])
    XCTAssertEqual(layout.bands.map(\.ticks), [0..<3840, 3840..<7680, 7680..<8640, 8640..<14400])
    XCTAssertEqual(layout.bands.map(\.minX), [0, 192, 384, 432])
    XCTAssertEqual(layout.bands.map(\.maxX), [192, 384, 432, 720])
    let zoomed = bands(snapshot, geometry: RollGeometry(pxPerBeat: 96))
    XCTAssertEqual(zoomed.bands.map(\.minX), [0, 384, 768, 864], "x follows the zoom")
  }

  func testSpansComeFromPlayStartAndPlayEndNotTheAuthoredStart() throws {
    let snapshot = try fixture()
    var events = snapshot.events
    // A feel that performs: the second chord sounds from beat 4.25 (its authored start stays 4).
    events[0].playEnd = 4.25
    events[1].playStart = 4.25
    let layout = GuideBands(events: events, ticksPerBeat: 960, revision: 1, endBeat: 15, geometry: RollGeometry(),
                            trackRole: .bass)
    XCTAssertEqual(layout.bands[0].ticks, 0..<4080)
    XCTAssertEqual(layout.bands[1].ticks, 4080..<7680)
    XCTAssertEqual(layout.bands[1].minX, 204)
  }

  func testAChordWithoutASpanOrAGuideHasNoBand() throws {
    let snapshot = try fixture()
    var events = snapshot.events
    events[1].guide = nil
    events[2].playStart = nil
    let layout = GuideBands(events: events, ticksPerBeat: 960, revision: 1, endBeat: 15, geometry: RollGeometry(),
                            trackRole: .lead)
    XCTAssertEqual(layout.bands.map(\.eventIndex), [0, 3])
  }

  func testIntersectingIndicesFollowPlaySpansRule() throws {
    let snapshot = try fixture()
    let layout = bands(snapshot)
    for range in [0..<1, 0..<3840, 3839..<3841, 3840..<7680, 7000..<9000, 8640..<8641, 14400..<20000, 100..<100] {
      XCTAssertEqual(layout.indices(intersecting: range),
                     PlaySpans.eventIndices(intersecting: range, in: snapshot.events, ticksPerBeat: 960), "\(range)")
    }
    XCTAssertEqual(layout.indices(intersecting: 3840..<7680), 1..<2, "a range starting on a boundary leaves the earlier chord")
    XCTAssertEqual(layout.indices(intersecting: 14400..<20000), 4..<4, "past the end: none")
  }

  // MARK: - Roles and opacities

  func testOpacitiesAreGuideStylesForEveryRoleAndTrackRole() throws {
    let snapshot = try fixture()
    for role in TrackRole.allCases {
      let layout = bands(snapshot, role: role)
      for band in layout.bands {
        let guide = try XCTUnwrap(snapshot.events[band.eventIndex].guide)
        XCTAssertEqual(band.roles.count, 12)
        XCTAssertEqual(band.opacities.count, 12)
        for pc in 0..<12 {
          XCTAssertEqual(band.roles[pc], guide.role(pc: pc))
          XCTAssertEqual(band.opacities[pc], GuideStyle.opacity(role: guide.role(pc: pc), track: role), "\(role) pc \(pc)")
        }
      }
    }
    // C over C major for a bass track: the root strongest, then the chord tones, the tensions, the avoid F.
    let c = bands(snapshot, role: .bass).bands[0]
    XCTAssertEqual(c.opacities[0], 0.36)
    XCTAssertEqual(c.opacities[4], 0.24)
    XCTAssertEqual(c.opacities[2], 0.12)
    XCTAssertEqual(c.opacities[5], 0.06)
  }

  func testOutsideDrawsNothing() throws {
    let snapshot = try fixture()
    let c = bands(snapshot, role: .other).bands[0]
    // C major's chord scale: C D E F G A B; the five black keys are Outside.
    XCTAssertEqual(c.drawnPitchClasses, [0, 2, 4, 5, 7, 9, 11])
    for pc in [1, 3, 6, 8, 10] {
      XCTAssertEqual(c.roles[pc], .outside)
      XCTAssertEqual(c.opacities[pc], 0)
    }
  }

  // MARK: - Degree labels

  func testLabelsOnlyOnRootAndChordToneRowsFromTwelvePoints() throws {
    let snapshot = try fixture()
    let layout = bands(snapshot, role: .lead, geometry: RollGeometry(rowHeight: 14))
    XCTAssertTrue(layout.showsLabels)
    let g7 = layout.bands[3]
    // G7: G root, B and D and F chord tones; C (avoid), A and E (tensions) unlabelled.
    let labels = layout.labels(in: g7, pitches: 60...72)
    XCTAssertEqual(labels.map(\.pitch), [62, 65, 67, 71])
    XCTAssertEqual(labels.map(\.text), ["5", "♭7", "R", "3"])
    XCTAssertEqual(labels.map(\.role), [.chord, .chord, .root, .chord])
    XCTAssertEqual(labels.map(\.x), [432, 432, 432, 432], "at the left of the span")
    for label in labels {
      XCTAssertEqual(label.text, snapshot.events[3].guide?.displayDegree(pc: label.pitch))
    }
    XCTAssertEqual(layout.labels(in: g7, pitches: 66...66).count, 0)

    let twelve = bands(snapshot, role: .lead, geometry: RollGeometry(rowHeight: 12))
    XCTAssertEqual(twelve.labels(in: twelve.bands[3], pitches: 60...72).count, 4)
    let small = bands(snapshot, role: .lead, geometry: RollGeometry(rowHeight: 11.5))
    XCTAssertFalse(small.showsLabels)
    XCTAssertEqual(small.labels(in: small.bands[3], pitches: 0...127).count, 0, "under 12 pt: no labels")
    let eight = bands(snapshot, role: .lead, geometry: RollGeometry(rowHeight: 8))
    XCTAssertEqual(eight.labels(in: eight.bands[0], pitches: 0...127).count, 0)
  }

  func testLabelsCoverEveryOctaveInTheVisibleRowsAndClampToTheKeyboard() throws {
    let snapshot = try fixture()
    let layout = bands(snapshot)
    let c = layout.bands[0]
    XCTAssertEqual(layout.labels(in: c, pitches: 0...127).count, 33, "C, E and G in 11 octaves each (127 is G9)")
    XCTAssertEqual(layout.labels(in: c, pitches: -5...2).map(\.pitch), [0])
  }

  // MARK: - Cache

  func testTheKeyChangesWithRevisionZoomAndRole() throws {
    let snapshot = try fixture()
    let base = GuideBands.key(snapshot: snapshot, geometry: RollGeometry(), trackRole: .bass)
    XCTAssertEqual(base, GuideBands.key(snapshot: snapshot, geometry: RollGeometry(), trackRole: .bass))
    var later = snapshot
    later.revision = (snapshot.revision ?? 0) + 1
    XCTAssertNotEqual(base, GuideBands.key(snapshot: later, geometry: RollGeometry(), trackRole: .bass))
    XCTAssertNotEqual(base, GuideBands.key(snapshot: snapshot, geometry: RollGeometry(pxPerBeat: 96), trackRole: .bass))
    XCTAssertNotEqual(base, GuideBands.key(snapshot: snapshot, geometry: RollGeometry(rowHeight: 12), trackRole: .bass))
    XCTAssertNotEqual(base, GuideBands.key(snapshot: snapshot, geometry: RollGeometry(), trackRole: .lead))
  }

  func testTheCacheBuildsOncePerKey() throws {
    let snapshot = try fixture()
    let cache = GuideBandsCache()
    let first = cache.bands(snapshot: snapshot, geometry: RollGeometry(), trackRole: .bass)
    _ = cache.bands(snapshot: snapshot, geometry: RollGeometry(), trackRole: .bass)
    XCTAssertEqual(cache.builds, 1, "the same revision, zoom and role reuse the layout")
    var later = snapshot
    later.revision = (snapshot.revision ?? 0) + 1
    _ = cache.bands(snapshot: later, geometry: RollGeometry(), trackRole: .bass)
    XCTAssertEqual(cache.builds, 2)
    _ = cache.bands(snapshot: later, geometry: RollGeometry(pxPerBeat: 24), trackRole: .bass)
    XCTAssertEqual(cache.builds, 3)
    let lead = cache.bands(snapshot: later, geometry: RollGeometry(pxPerBeat: 24), trackRole: .lead)
    XCTAssertEqual(cache.builds, 4)
    XCTAssertNotEqual(lead.bands[0].opacities, first.bands[0].opacities)
  }

  func testWithoutARevisionTheCacheAlwaysRebuilds() throws {
    var snapshot = try fixture()
    snapshot.revision = nil
    let cache = GuideBandsCache()
    let before = cache.bands(snapshot: snapshot, geometry: RollGeometry(), trackRole: .bass)
    _ = cache.bands(snapshot: snapshot, geometry: RollGeometry(), trackRole: .bass)
    XCTAssertEqual(cache.builds, 2, "an older build's chord edit keeps the count and the end: never trust the key")
    // A changed guide under the same (revision-less) key is laid out anew.
    var edited = try JSONSerialization.jsonObject(with: JSONEncoder().encode(snapshot)) as! [String: Any]
    var events = edited["events"] as! [[String: Any]]
    events[0]["guide"] = events[1]["guide"]
    edited["events"] = events
    let changed = try JSONDecoder().decode(Snapshot.self, from: JSONSerialization.data(withJSONObject: edited))
    XCTAssertEqual(GuideBands.key(snapshot: changed, geometry: RollGeometry(), trackRole: .bass),
                   GuideBands.key(snapshot: snapshot, geometry: RollGeometry(), trackRole: .bass))
    let layout = cache.bands(snapshot: changed, geometry: RollGeometry(), trackRole: .bass)
    XCTAssertEqual(layout, GuideBands(snapshot: changed, geometry: RollGeometry(), trackRole: .bass))
    XCTAssertNotEqual(layout.bands[0].roles, before.bands[0].roles, "the first chord's new guide is drawn")
    XCTAssertEqual(cache.builds, 3)
  }
}
