import CoreGraphics
import XCTest
@testable import ChordLabUICore

/// What the roll's canvas, ribbon and keyboard draw, decided in Core (UI/Core/RollDrawing.swift, tracks design
/// D14): the time lines, the visible notes, the ribbon's section spans, the playhead and the note opacity.
final class RollDrawingTests: XCTestCase {
  private func fixture() throws -> Snapshot {
    let url = try SnapshotTests.fixturesDirectory().appendingPathComponent("snapshot-tracks.json")
    return try JSONDecoder().decode(Snapshot.self, from: Data(contentsOf: url))
  }

  // MARK: - Lines

  func testThePulseIsTheMetersBeatOrADottedQuarterWhenCompound() {
    XCTAssertEqual(RollLines.pulseTicks(meter: nil, ticksPerBeat: 960), 960)
    XCTAssertEqual(RollLines.pulseTicks(meter: MeterJSON(num: 3, den: 4), ticksPerBeat: 960), 960)
    XCTAssertEqual(RollLines.pulseTicks(meter: MeterJSON(num: 7, den: 8), ticksPerBeat: 960), 480)
    XCTAssertEqual(RollLines.pulseTicks(meter: MeterJSON(num: 6, den: 8), ticksPerBeat: 960), 1440)
    XCTAssertEqual(RollLines.pulseTicks(meter: MeterJSON(num: 2, den: 2), ticksPerBeat: 960), 1920)
    XCTAssertEqual(RollLines.pulseTicks(meter: MeterJSON(num: 0, den: 4), ticksPerBeat: 960), 960, "invalid: 4/4")
  }

  func testBarBeatAndGridLinesInOneBarAtTheDefaultZoom() {
    // 4/4, an eighth grid (480) at 48 pt per beat (24 pt per eighth): every kind drawn.
    let lines = RollLines.lines(ticks: 0..<3841, barTicks: 3840, pulseTicks: 960, gridTicks: 480,
                                geometry: RollGeometry())
    XCTAssertEqual(lines.map(\.tick), [0, 480, 960, 1440, 1920, 2400, 2880, 3360, 3840])
    XCTAssertEqual(lines.map(\.kind), [.bar, .grid, .beat, .grid, .beat, .grid, .beat, .grid, .bar])
  }

  func testLinesDenserThanFourPointsAreSkipped() {
    // 1/32 (120 ticks) at 12 pt per beat is 1.5 pt apart: no grid lines; the beats (12 pt) stay.
    let coarse = RollLines.lines(ticks: 0..<3840, barTicks: 3840, pulseTicks: 960, gridTicks: 120,
                                 geometry: RollGeometry(pxPerBeat: 12))
    XCTAssertEqual(coarse.map(\.kind), [.bar, .beat, .beat, .beat])
    // Off (10 ticks) at the largest zoom is 2 pt apart: skipped too.
    let off = RollLines.lines(ticks: 0..<960, barTicks: 3840, pulseTicks: 960, gridTicks: 10,
                              geometry: RollGeometry(pxPerBeat: 192))
    XCTAssertEqual(off.map(\.kind), [.bar])
    // A 1/16 grid at 16 pt per beat is exactly 4 pt apart: drawn.
    let edge = RollLines.lines(ticks: 0..<960, barTicks: 3840, pulseTicks: 960, gridTicks: 240,
                               geometry: RollGeometry(pxPerBeat: 16))
    XCTAssertEqual(edge.map(\.tick), [0, 240, 480, 720])
  }

  func testTheGridRestartsAtEachBarInSevenEight() {
    // 7/8: a bar is 3360 ticks; a quarter grid restarts at the bar line (3360), never 3840.
    let lines = RollLines.lines(ticks: 2800..<4400, barTicks: 3360, pulseTicks: 480, gridTicks: 960,
                                geometry: RollGeometry(pxPerBeat: 96))
    XCTAssertEqual(lines.filter { $0.kind == .bar }.map(\.tick), [3360])
    XCTAssertFalse(lines.contains { $0.tick == 3840 && $0.kind == .grid }, "no grid point from tick 0")
    XCTAssertEqual(lines.filter { $0.kind != .bar }.map(\.tick), [2880, 3840, 4320], "the pulse (eighths) where no bar")
  }

  func testOnlyVisibleLinesAndNoneBelowZero() {
    let lines = RollLines.lines(ticks: 4000..<5000, barTicks: 3840, pulseTicks: 960, gridTicks: 480,
                                geometry: RollGeometry())
    XCTAssertEqual(lines.map(\.tick), [4320, 4800])
    XCTAssertEqual(RollLines.lines(ticks: 10..<10, barTicks: 3840, pulseTicks: 960, gridTicks: 480,
                                   geometry: RollGeometry()), [])
  }

  // MARK: - Visible notes

  private let notes = TrackNotesJSON(id: [10, 11, 12, 13, 14], pitch: [40, 60, 61, 90, 60],
                                     start: [0, 0, 960, 1920, 7680], length: [7000, 480, 480, 480, 960],
                                     velocity: [100, 100, 100, 100, 100], anchor: [1, 1, 1, 2, 3],
                                     offset: [0, 0, 960, 0, 0], active: [true, true, true, true, false])

  func testVisibleNotesOverlapTheTicksAndRowsInNoteOrder() {
    XCTAssertEqual(RollVisibleNotes.indices(notes, ticks: 0..<1000, pitches: 0...127), [0, 1, 2])
    XCTAssertEqual(RollVisibleNotes.indices(notes, ticks: 500..<2000, pitches: 0...127), [0, 2, 3],
                   "a note that ended at 480 is gone; a long note from 0 is still there")
    XCTAssertEqual(RollVisibleNotes.indices(notes, ticks: 500..<2000, pitches: 50...80), [2])
    XCTAssertEqual(RollVisibleNotes.indices(notes, ticks: 960..<961, pitches: 61...61), [2])
    XCTAssertEqual(RollVisibleNotes.indices(notes, ticks: 1440..<1920, pitches: 0...127), [0],
                   "half-open: a note ending at the range start or starting at its end is not visible")
    XCTAssertEqual(RollVisibleNotes.indices(notes, ticks: 7000..<9000, pitches: 0...127), [4])
    XCTAssertEqual(RollVisibleNotes.indices(notes, ticks: 0..<0, pitches: 0...127), [])
  }

  // MARK: - Ribbon

  func testSectionSpansRunFromTheirFirstChordToTheNextSection() throws {
    let spans = RollRibbon.sectionSpans(try fixture())
    XCTAssertEqual(spans.map(\.name), ["Verse", "Chorus"])
    XCTAssertEqual(spans.map(\.colourIndex), [0, 3])
    XCTAssertEqual(spans.map(\.ticks), [0..<7680, 7680..<14400], "playback spans, the last to the end")
  }

  func testNoSectionsGiveNoSpans() throws {
    let snapshot = try fixture()
    XCTAssertEqual(RollRibbon.sectionSpans(sections: [], events: snapshot.events, ticksPerBeat: 960), [])
    let outOfRange = SectionJSON(id: 9, name: "X", colourIndex: 1, firstEventIndex: 7)
    XCTAssertEqual(RollRibbon.sectionSpans(sections: [outOfRange], events: snapshot.events, ticksPerBeat: 960), [])
  }

  // MARK: - Playhead

  func testThePlayheadFoldsIntoALoopAndHidesWhenNotPlaying() {
    let playing = TransportJSON(schema: 1, ppq: 17.5, playing: true)
    XCTAssertEqual(Playhead.beat(transport: playing, endBeat: 15, loop: true), 2.5)
    XCTAssertNil(Playhead.beat(transport: playing, endBeat: 15, loop: false), "past the end without the loop")
    XCTAssertEqual(Playhead.beat(transport: TransportJSON(schema: 1, ppq: 3, playing: true), endBeat: 15, loop: false), 3)
    XCTAssertNil(Playhead.beat(transport: TransportJSON(schema: 1, ppq: 3, playing: false), endBeat: 15, loop: true))
    XCTAssertNil(Playhead.beat(transport: TransportJSON(schema: 1, ppq: -1, playing: true), endBeat: 15, loop: true))
    XCTAssertNil(Playhead.beat(transport: playing, endBeat: 0, loop: true), "no progression")
    XCTAssertNil(Playhead.beat(transport: nil, endBeat: 15, loop: true))
  }

  // MARK: - Notes

  func testNoteOpacityRisesWithVelocity() {
    XCTAssertEqual(RollNoteStyle.opacity(velocity: 127), 1, accuracy: 1e-12)
    XCTAssertEqual(RollNoteStyle.opacity(velocity: 0), 0.35, accuracy: 1e-12)
    XCTAssertEqual(RollNoteStyle.opacity(velocity: 64), 0.35 + 0.65 * 64 / 127, accuracy: 1e-12)
    XCTAssertEqual(RollNoteStyle.opacity(velocity: 500), 1, accuracy: 1e-12, "clamped")
    XCTAssertEqual(RollNoteStyle.opacity(velocity: -3), 0.35, accuracy: 1e-12)
  }
}
