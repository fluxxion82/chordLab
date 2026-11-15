import XCTest
@testable import ChordLabUICore

/// The playback-span lookups (UI/Core/PlaySpans.swift, tracks design D6, D14, D15): the chord at a tick, and the
/// chords a tick range touches.
final class PlaySpansTests: XCTestCase {
  private func fixtureEvents() throws -> [EventJSON] {
    let url = try SnapshotTests.fixturesDirectory().appendingPathComponent("snapshot-tracks.json")
    return try JSONDecoder().decode(Snapshot.self, from: Data(contentsOf: url)).events
  }

  private func legacyEvents() throws -> [EventJSON] {
    let url = try SnapshotTests.fixturesDirectory().appendingPathComponent("snapshot-legacy-phase1.json")
    return try JSONDecoder().decode(Snapshot.self, from: Data(contentsOf: url)).events
  }

  /// `event` with its playback span replaced.
  private func withSpan(_ event: EventJSON, start: Double?, end: Double?) -> EventJSON {
    var copy = event
    copy.playStart = start
    copy.playEnd = end
    return copy
  }

  func testTheFixtureSpansAreWhatTheseTestsAssume() throws {
    let events = try fixtureEvents()
    XCTAssertEqual(events.map(\.symbol), ["C", "Am", "F", "G7"])
    XCTAssertEqual(events.map { $0.playStart }, [0, 4, 8, 9])
    XCTAssertEqual(events.map { $0.playEnd }, [4, 8, 9, 15])
  }

  // MARK: - EventJSON.playTicks

  func testPlayTicksAreTheSpanInBeatsTimesTicksPerBeatRounded() throws {
    let events = try fixtureEvents()
    XCTAssertEqual(events[0].playTicks(ticksPerBeat: 960), 0..<3840)
    XCTAssertEqual(events[3].playTicks(ticksPerBeat: 960), 8640..<14400)
    XCTAssertEqual(events[1].playTicks(ticksPerBeat: 480), 1920..<3840)
    let fractional = withSpan(events[0], start: 0.3333333, end: 3.9999999)
    XCTAssertEqual(fractional.playTicks(ticksPerBeat: 960), 320..<3840, "rounded to the nearest tick")
    let half = withSpan(events[0], start: 0.25, end: 1.25)   // exactly 0.5 and 2.5 ticks at 2 per beat
    XCTAssertEqual(half.playTicks(ticksPerBeat: 2), 1..<3, "a half rounds away from zero, as the engine's lround")
  }

  func testPlayTicksAreNilWhenTheSpanIsAbsentOrNotANumber() throws {
    let events = try fixtureEvents()
    XCTAssertNil(withSpan(events[0], start: nil, end: nil).playTicks(ticksPerBeat: 960))
    XCTAssertNil(withSpan(events[0], start: 0, end: nil).playTicks(ticksPerBeat: 960))
    XCTAssertNil(withSpan(events[0], start: nil, end: 4).playTicks(ticksPerBeat: 960))
    XCTAssertNil(withSpan(events[0], start: .nan, end: 4).playTicks(ticksPerBeat: 960))
    XCTAssertNil(withSpan(events[0], start: 0, end: .infinity).playTicks(ticksPerBeat: 960))
    XCTAssertNil(try legacyEvents()[0].playTicks(ticksPerBeat: 960))
  }

  func testPlayTicksNeverTrapOnAnInvertedOrAbsurdSpan() throws {
    let events = try fixtureEvents()
    XCTAssertEqual(withSpan(events[0], start: 4, end: 2).playTicks(ticksPerBeat: 960), 3840..<3840, "an empty range, not a trap")
    XCTAssertNil(withSpan(events[0], start: 0, end: 1e300).playTicks(ticksPerBeat: 960))
    XCTAssertEqual(events[0].playTicks(ticksPerBeat: 0), 0..<4, "ticksPerBeat counts as at least 1")
  }

  // MARK: - The chord at a tick

  func testEventAtATickInsideEachSpan() throws {
    let events = try fixtureEvents()
    func symbol(_ tick: Int) -> String? { PlaySpans.event(atTick: tick, in: events, ticksPerBeat: 960)?.symbol }
    XCTAssertEqual(symbol(0), "C")
    XCTAssertEqual(symbol(1), "C")
    XCTAssertEqual(symbol(3839), "C")
    XCTAssertEqual(symbol(5000), "Am")
    XCTAssertEqual(symbol(8 * 960 + 100), "F")
    XCTAssertEqual(symbol(9 * 960 + 100), "G7")
    XCTAssertEqual(symbol(15 * 960 - 1), "G7")
  }

  func testASpanIsHalfOpenSoABoundaryTickBelongsToTheNextChord() throws {
    let events = try fixtureEvents()
    func symbol(_ tick: Int) -> String? { PlaySpans.event(atTick: tick, in: events, ticksPerBeat: 960)?.symbol }
    XCTAssertEqual(symbol(3840), "Am")
    XCTAssertEqual(symbol(3840 - 1), "C")
    XCTAssertEqual(symbol(7680), "F")
    XCTAssertEqual(symbol(8 * 960 + 960), "G7")
    XCTAssertEqual(symbol(8 * 960 + 959), "F")
  }

  func testNoEventPastTheLastPlayEndOrBeforeTickZero() throws {
    let events = try fixtureEvents()
    XCTAssertNil(PlaySpans.event(atTick: 15 * 960, in: events, ticksPerBeat: 960))
    XCTAssertNil(PlaySpans.event(atTick: 15 * 960 + 5000, in: events, ticksPerBeat: 960))
    XCTAssertNil(PlaySpans.event(atTick: -1, in: events, ticksPerBeat: 960))
    XCTAssertNil(PlaySpans.event(atTick: 0, in: [], ticksPerBeat: 960))
    XCTAssertNil(PlaySpans.eventIndex(atTick: 0, in: [], ticksPerBeat: 960))
  }

  func testEveryEventIsFoundAtItsOwnStartAndLastTick() throws {
    let events = try fixtureEvents()
    for (index, event) in events.enumerated() {
      let span = try XCTUnwrap(event.playTicks(ticksPerBeat: 960))
      XCTAssertEqual(PlaySpans.event(atTick: span.lowerBound, in: events, ticksPerBeat: 960)?.id, event.id)
      XCTAssertEqual(PlaySpans.event(atTick: span.upperBound - 1, in: events, ticksPerBeat: 960)?.id, event.id)
      XCTAssertEqual(PlaySpans.eventIndex(atTick: span.lowerBound, in: events, ticksPerBeat: 960), index)
      XCTAssertEqual(PlaySpans.eventIndex(atTick: span.upperBound - 1, in: events, ticksPerBeat: 960), index)
    }
  }

  func testAnOlderSnapshotWithoutSpansFindsNothing() throws {
    let events = try legacyEvents()
    XCTAssertFalse(events.isEmpty)
    XCTAssertTrue(events.allSatisfy { $0.playStart == nil && $0.playEnd == nil })
    for tick in [0, 960, 100_000] {
      XCTAssertNil(PlaySpans.event(atTick: tick, in: events, ticksPerBeat: 960))
      XCTAssertNil(PlaySpans.eventIndex(atTick: tick, in: events, ticksPerBeat: 960))
    }
  }

  func testTheLookupUsesThePlaybackSpanNotTheAuthoredOne() throws {
    // A feel moved the C's playback end to 3.9 beats, though the strip still draws it to 4.
    let events = try fixtureEvents()
    var shifted = events
    shifted[0] = withSpan(events[0], start: 0, end: 3.9)
    shifted[1] = withSpan(events[1], start: 3.9, end: 8)
    XCTAssertEqual(PlaySpans.event(atTick: 3744, in: shifted, ticksPerBeat: 960)?.symbol, "Am", "3744 = 3.9 beats")
    XCTAssertEqual(PlaySpans.event(atTick: 3743, in: shifted, ticksPerBeat: 960)?.symbol, "C")
    XCTAssertEqual(PlaySpans.event(atTick: 3800, in: events, ticksPerBeat: 960)?.symbol, "C", "authored span of the original")
  }

  func testAGapBetweenSpansFindsNothing() throws {
    var events = try fixtureEvents()
    events[1] = withSpan(events[1], start: 5, end: 8)   // beats 4..5 are in no span
    XCTAssertEqual(PlaySpans.event(atTick: 3839, in: events, ticksPerBeat: 960)?.symbol, "C")
    XCTAssertNil(PlaySpans.event(atTick: 3840, in: events, ticksPerBeat: 960))
    XCTAssertNil(PlaySpans.event(atTick: 4799, in: events, ticksPerBeat: 960))
    XCTAssertEqual(PlaySpans.event(atTick: 4800, in: events, ticksPerBeat: 960)?.symbol, "Am")
  }

  func testTheLookupFollowsTheSnapshotsTicksPerBeat() throws {
    let events = try fixtureEvents()
    XCTAssertEqual(PlaySpans.event(atTick: 1920, in: events, ticksPerBeat: 480)?.symbol, "Am", "1920 ticks = 4 beats at 480")
    XCTAssertEqual(PlaySpans.event(atTick: 1919, in: events, ticksPerBeat: 480)?.symbol, "C")
  }

  func testAMixOfEventsWithAndWithoutSpansNeverTrapsAndNeverReturnsAWrongChord() throws {
    // The engine publishes spans for all events or none; a mix is not expected. The lookups stay safe anyway.
    let events = try fixtureEvents()
    var mixed = events
    mixed[2] = withSpan(events[2], start: nil, end: nil)
    for tick in stride(from: -10, through: 15 * 960 + 10, by: 53) {
      if let found = PlaySpans.event(atTick: tick, in: mixed, ticksPerBeat: 960) {
        XCTAssertTrue(try XCTUnwrap(found.playTicks(ticksPerBeat: 960)).contains(tick), "tick \(tick)")
      }
      let range = PlaySpans.eventIndices(intersecting: tick..<(tick + 2000), in: mixed, ticksPerBeat: 960)
      XCTAssertTrue(range.lowerBound >= 0 && range.upperBound <= mixed.count, "tick \(tick)")
    }
    let allMissing = events.map { withSpan($0, start: nil, end: nil) }
    XCTAssertTrue(PlaySpans.eventIndices(intersecting: 0..<100_000, in: allMissing, ticksPerBeat: 960).isEmpty)
  }

  // MARK: - The chords a range touches

  func testIndicesIntersectingARangeInsideOneChord() throws {
    let events = try fixtureEvents()
    func indices(_ range: Range<Int>) -> Range<Int> {
      PlaySpans.eventIndices(intersecting: range, in: events, ticksPerBeat: 960)
    }
    XCTAssertEqual(indices(100..<200), 0..<1)
    XCTAssertEqual(indices(0..<3840), 0..<1)
    XCTAssertEqual(indices(4000..<4001), 1..<2)
  }

  func testIndicesIntersectingARangeAtABoundary() throws {
    let events = try fixtureEvents()
    func indices(_ range: Range<Int>) -> Range<Int> {
      PlaySpans.eventIndices(intersecting: range, in: events, ticksPerBeat: 960)
    }
    XCTAssertEqual(indices(3839..<3841), 0..<2, "one tick either side of the C/Am boundary")
    XCTAssertEqual(indices(3840..<3841), 1..<2, "a range starting on a boundary does not touch the chord before it")
    XCTAssertEqual(indices(0..<3841), 0..<2)
    XCTAssertEqual(indices(3000..<3840), 0..<1, "the end is exclusive: 3840 is not touched")
    XCTAssertEqual(indices(3000..<3841), 0..<2)
    XCTAssertEqual(indices(7680..<8640), 2..<3, "exactly the F span")
    XCTAssertEqual(indices(7679..<8641), 1..<4, "one tick into the Am and the G7 each")
  }

  func testIndicesIntersectingAWideRangeAndTheEnds() throws {
    let events = try fixtureEvents()
    func indices(_ range: Range<Int>) -> Range<Int> {
      PlaySpans.eventIndices(intersecting: range, in: events, ticksPerBeat: 960)
    }
    XCTAssertEqual(indices(0..<100_000), 0..<4)
    XCTAssertEqual(indices(-5000..<1), 0..<1)
    XCTAssertTrue(indices(-5000..<0).isEmpty, "a range ending at tick 0 touches nothing")
    XCTAssertTrue(indices(14400..<20000).isEmpty, "at or past the last playEnd")
    XCTAssertEqual(indices(14399..<20000), 3..<4)
  }

  func testIndicesIntersectingAnEmptyRangeOrNoEventsAreEmpty() throws {
    let events = try fixtureEvents()
    XCTAssertTrue(PlaySpans.eventIndices(intersecting: 5000..<5000, in: events, ticksPerBeat: 960).isEmpty)
    XCTAssertTrue(PlaySpans.eventIndices(intersecting: 0..<100, in: [], ticksPerBeat: 960).isEmpty)
    XCTAssertTrue(PlaySpans.eventIndices(intersecting: 0..<100_000, in: try legacyEvents(), ticksPerBeat: 960).isEmpty)
  }

  func testIndicesIntersectingSkipTheGapBetweenSpans() throws {
    var events = try fixtureEvents()
    events[1] = withSpan(events[1], start: 5, end: 8)   // beats 4..5 are in no span
    func indices(_ range: Range<Int>) -> Range<Int> {
      PlaySpans.eventIndices(intersecting: range, in: events, ticksPerBeat: 960)
    }
    XCTAssertTrue(indices(3840..<4800).isEmpty, "the gap")
    XCTAssertEqual(indices(3839..<4800), 0..<1)
    XCTAssertEqual(indices(3840..<4801), 1..<2)
  }

  func testIndicesAgreeWithThePointLookupAtEveryTick() throws {
    let events = try fixtureEvents()
    for tick in stride(from: -50, through: 15 * 960 + 50, by: 37) {
      let point = PlaySpans.eventIndex(atTick: tick, in: events, ticksPerBeat: 960)
      let range = PlaySpans.eventIndices(intersecting: tick..<(tick + 1), in: events, ticksPerBeat: 960)
      XCTAssertEqual(range.count, point == nil ? 0 : 1, "tick \(tick)")
      if let point { XCTAssertEqual(range.lowerBound, point, "tick \(tick)") }
    }
  }

  func testIndicesFollowTheSnapshotsTicksPerBeat() throws {
    let events = try fixtureEvents()
    XCTAssertEqual(PlaySpans.eventIndices(intersecting: 1919..<1921, in: events, ticksPerBeat: 480), 0..<2)
    XCTAssertEqual(PlaySpans.eventIndices(intersecting: 1920..<1921, in: events, ticksPerBeat: 480), 1..<2)
  }
}
