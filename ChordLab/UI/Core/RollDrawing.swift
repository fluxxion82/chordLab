import CoreGraphics
import Foundation

/// The piano roll's vertical time lines (tracks design D14): bar lines, the meter's pulse ("beat") lines and the
/// grid's lines, for the visible ticks only, bar-relative like `RollGrid` (every bar restarts the pulse and the
/// grid, so 7/8's quarter grid lands on its bar lines). A tick that is several kinds is drawn once as the strongest
/// (bar, then beat, then grid). Pulse and grid lines that would lie closer than `minSpacing` points (4) at the
/// current zoom are left out, so the roll never turns into a solid wash (1/32 at 12 pt per beat, Off's 10 ticks).
///
/// Depends on nothing but CoreGraphics and Foundation; unit-tested by the `ChordLabUICore` package
/// (Tests/ChordLabUICoreTests/RollDrawingTests.swift).
enum RollLines {
  enum Kind: Equatable {
    case bar, beat, grid
  }

  struct Line: Equatable {
    let tick: Int
    let kind: Kind
  }

  /// The closest two lines of one kind may be, in points.
  static let minSpacing: CGFloat = 4

  /// The meter's pulse in ticks, as the sequence ruler counts it (`RulerMarks`): `4 × ticksPerBeat / den` (a quarter
  /// in x/4, an eighth in x/8, a half in x/2), or a dotted quarter (`1.5 × ticksPerBeat`) in a compound meter (6/8,
  /// 9/8, 12/8). No meter, or an invalid one, is 4/4.
  static func pulseTicks(meter: MeterJSON?, ticksPerBeat: Int) -> Int {
    let rate = max(1, ticksPerBeat)
    guard let meter, meter.num > 0, meter.den > 0 else { return rate }
    if RulerMarks.isCompound(num: meter.num, den: meter.den) { return max(1, rate * 3 / 2) }
    return max(1, 4 * rate / meter.den)
  }

  /// The lines in the half-open tick range `ticks` (never below tick 0), ascending: each bar's start, its pulses and
  /// its grid points, bar-relative, each pulse or grid kind only when its spacing at `geometry` is at least
  /// `minSpacing`.
  static func lines(ticks: Range<Int>, barTicks: Int, pulseTicks: Int, gridTicks: Int, geometry: RollGeometry,
                    minSpacing: CGFloat = RollLines.minSpacing) -> [Line] {
    let low = max(0, ticks.lowerBound)
    guard ticks.upperBound > low else { return [] }
    let bar = max(1, barTicks)
    let pulse = max(1, pulseTicks)
    let grid = max(1, gridTicks)
    let drawsPulse = geometry.x(tick: pulse) >= minSpacing
    let drawsGrid = geometry.x(tick: grid) >= minSpacing
    var result: [Line] = []
    var barStart = low / bar * bar
    while barStart < ticks.upperBound {
      let barEnd = barStart + bar
      // One merge per bar: the next pulse and the next grid point, walked together in tick order.
      var nextPulse = drawsPulse ? barStart + pulse : barEnd
      var nextGrid = drawsGrid ? barStart + grid : barEnd
      if barStart >= low { result.append(Line(tick: barStart, kind: .bar)) }
      while true {
        let tick = min(nextPulse, nextGrid)
        guard tick < barEnd, tick < ticks.upperBound else { break }
        let isPulse = tick == nextPulse
        if tick >= low { result.append(Line(tick: tick, kind: isPulse ? .beat : .grid)) }
        if tick == nextPulse { nextPulse += pulse }
        if tick == nextGrid { nextGrid += grid }
      }
      barStart = barEnd
    }
    return result
  }
}

/// The notes of a track that a viewport shows (tracks design D14: the canvas draws the visible region only). The
/// engine keeps notes sorted by start (`TrackNotesJSON`), so the scan stops at the first note starting at or after
/// the range's end (binary search); a note is visible when it overlaps the half-open tick range and its pitch is
/// one of the visible rows.
///
/// Depends on nothing but Foundation; unit-tested by the `ChordLabUICore` package
/// (Tests/ChordLabUICoreTests/RollDrawingTests.swift).
enum RollVisibleNotes {
  /// The indices of the visible notes, in note order (the order the canvas draws them in, so the last drawn is the
  /// topmost and the one `RollEditLogic` hits).
  static func indices(_ notes: TrackNotesJSON, ticks: Range<Int>, pitches: ClosedRange<Int>) -> [Int] {
    guard !ticks.isEmpty else { return [] }
    let count = notes.count
    var low = 0
    var high = count
    while low < high {
      let middle = (low + high) / 2
      if notes.start[middle] < ticks.upperBound { low = middle + 1 } else { high = middle }
    }
    var result: [Int] = []
    for i in 0..<low where notes.start[i] + notes.length[i] > ticks.lowerBound && pitches.contains(notes.pitch[i]) {
      result.append(i)
    }
    return result
  }
}

/// The chord ribbon's section tints (tracks design D14): each section's span over the playback spans, from its first
/// chord's `playStart` to the next section's first chord (the last section to the last chord's `playEnd`).
///
/// Depends on nothing but Foundation; unit-tested by the `ChordLabUICore` package
/// (Tests/ChordLabUICoreTests/RollDrawingTests.swift).
enum RollRibbon {
  struct SectionSpan: Equatable {
    let id: Int
    let name: String
    let colourIndex: Int
    let ticks: Range<Int>
  }

  static func sectionSpans(_ snapshot: Snapshot) -> [SectionSpan] {
    sectionSpans(sections: snapshot.sections ?? [], events: snapshot.events, ticksPerBeat: snapshot.ticksPerBeatOrDefault)
  }

  /// The spans of `sections` (in `firstEventIndex` order); a section whose first chord does not exist or has no
  /// span is left out.
  static func sectionSpans(sections: [SectionJSON], events: [EventJSON], ticksPerBeat: Int) -> [SectionSpan] {
    let valid = sections.filter { events.indices.contains($0.firstEventIndex) }
    guard let end = events.last?.playTicks(ticksPerBeat: ticksPerBeat)?.upperBound else { return [] }
    var result: [SectionSpan] = []
    for (i, section) in valid.enumerated() {
      guard let start = events[section.firstEventIndex].playTicks(ticksPerBeat: ticksPerBeat)?.lowerBound else { continue }
      let next = i + 1 < valid.count ? events[valid[i + 1].firstEventIndex].playTicks(ticksPerBeat: ticksPerBeat)?.lowerBound : nil
      let stop = next ?? end
      result.append(SectionSpan(id: section.id, name: section.name, colourIndex: section.colourIndex,
                                ticks: start..<max(start, stop)))
    }
    return result
  }
}

/// Where the playhead is drawn (the sequence bar's rule, strip editing design D4; the roll uses the same, tracks
/// design D14): the transport's position in beats, folded into the progression when it loops; nil (hidden) while
/// not playing, before beat 0, with no progression, or past the end when the loop is off.
///
/// Depends on nothing but Foundation; unit-tested by the `ChordLabUICore` package
/// (Tests/ChordLabUICoreTests/RollDrawingTests.swift).
enum Playhead {
  static func beat(transport: TransportJSON?, endBeat: Double, loop: Bool) -> Double? {
    guard let transport, transport.playing, endBeat > 0, transport.ppq >= 0 else { return nil }
    if loop { return transport.ppq.truncatingRemainder(dividingBy: endBeat) }
    return transport.ppq < endBeat ? transport.ppq : nil
  }
}

/// How a note is drawn (tracks design D14: "notes as rounded rectangles in the track colour with opacity by
/// velocity").
///
/// Depends on nothing but Foundation; unit-tested by the `ChordLabUICore` package
/// (Tests/ChordLabUICoreTests/RollDrawingTests.swift).
enum RollNoteStyle {
  /// The fill opacity of a note of `velocity`: 0.35 + 0.65 × v / 127, v clamped to 0...127, so the softest note
  /// still reads over the strongest guide band and 127 is opaque.
  static func opacity(velocity: Int) -> Double {
    0.35 + 0.65 * Double(min(max(velocity, 0), 127)) / 127
  }
}
