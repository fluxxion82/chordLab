import Foundation

extension EventJSON {
  /// The chord's playback span as ticks, `[round(playStart × tpb), round(playEnd × tpb))` with `tpb` the
  /// snapshot's `ticksPerBeat` (at least 1); a half rounds away from zero, as the engine's `lround`. The engine's
  /// boundaries are tick-rounded already, so this recovers its integers. nil when the event carries no span (an
  /// older plug-in build, or either value missing, not finite, or too large for an `Int`). A span that ends
  /// before it starts (never published) gives the empty range at its start rather than trapping.
  func playTicks(ticksPerBeat: Int) -> Range<Int>? {
    guard let playStart, let playEnd else { return nil }
    let rate = Double(max(1, ticksPerBeat))
    let start = playStart * rate
    let end = playEnd * rate
    guard start.isFinite, end.isFinite, abs(start) < 1e15, abs(end) < 1e15 else { return nil }
    let first = Int(start.rounded())
    return first..<max(first, Int(end.rounded()))
  }
}

/// Lookups over the chords' playback spans (tracks design D6, D14, D15): the one place the roll asks "which
/// chord sounds here" (the guide bands, the pitch snap, the degree labels), so they all match what plays and
/// not the authored positions.
///
/// `events` are in playback order with ordered, non-overlapping, non-empty spans (gaps allowed: a feel can leave
/// one), so every search is binary, O(log n). A span is half-open, so a tick exactly on a boundary belongs to
/// the NEXT chord. The engine publishes spans for all events or for none (an older plug-in build): with none,
/// every lookup finds nothing. A mix is never published; if one arrives, an event without a span counts as
/// lying after every tick, so no lookup traps and an index stays in bounds, a point lookup still never returns
/// an event whose own span does not contain the tick, and a range lookup may then include span-less events.
///
/// Depends on nothing but Foundation; unit-tested by the `ChordLabUICore` package
/// (Tests/ChordLabUICoreTests/PlaySpansTests.swift).
enum PlaySpans {
  /// The index of the event whose span contains `tick`; nil before the first span, in a gap, at or past the last
  /// `playEnd`, and when the events carry no spans.
  static func eventIndex(atTick tick: Int, in events: [EventJSON], ticksPerBeat: Int) -> Int? {
    // The last event whose start is at or before `tick`.
    let after = partition(events, ticksPerBeat: ticksPerBeat) { span in span.lowerBound > tick }
    guard after > 0, let span = events[after - 1].playTicks(ticksPerBeat: ticksPerBeat), tick < span.upperBound else {
      return nil
    }
    return after - 1
  }

  /// The event whose span contains `tick` (see `eventIndex(atTick:in:ticksPerBeat:)`).
  static func event(atTick tick: Int, in events: [EventJSON], ticksPerBeat: Int) -> EventJSON? {
    eventIndex(atTick: tick, in: events, ticksPerBeat: ticksPerBeat).map { events[$0] }
  }

  /// The indices of the events whose span intersects the half-open tick `range`, as one contiguous range (empty
  /// for an empty `range`, none touched, or no spans): an event touches `[a, b)` when it starts before `b` and
  /// ends after `a`, so a range that starts exactly where a span ends, or ends exactly where one starts, does
  /// not touch it. The guide bands take the visible tick range through this.
  static func eventIndices(intersecting range: Range<Int>, in events: [EventJSON], ticksPerBeat: Int) -> Range<Int> {
    guard !range.isEmpty else { return 0..<0 }
    // The first event that ends after the range starts, and the first that starts at or after its end.
    let first = partition(events, ticksPerBeat: ticksPerBeat) { span in span.upperBound > range.lowerBound }
    let last = partition(events, ticksPerBeat: ticksPerBeat) { span in span.lowerBound >= range.upperBound }
    return first..<max(first, last)
  }

  /// The first index whose event's span satisfies `condition` (events.count when none does), by binary search;
  /// the condition must hold from some index on. An event without a span satisfies it.
  private static func partition(_ events: [EventJSON], ticksPerBeat: Int,
                                _ condition: (Range<Int>) -> Bool) -> Int {
    var low = 0
    var high = events.count
    while low < high {
      let middle = (low + high) / 2
      if let span = events[middle].playTicks(ticksPerBeat: ticksPerBeat), !condition(span) { low = middle + 1 } else { high = middle }
    }
    return low
  }
}
