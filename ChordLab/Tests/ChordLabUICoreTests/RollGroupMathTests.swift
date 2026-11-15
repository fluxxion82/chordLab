import XCTest
@testable import ChordLabUICore

/// The shared-delta clamps of a group edit (UI/Core/RollGroupMath.swift, tracks design D15 "Group geometry").
final class RollGroupMathTests: XCTestCase {
  /// The engine's domain with a 16-beat progression (15,360 ticks at 960).
  private let limits = RollLimits(minNoteTicks: 30, endTick: 15_360)

  private func note(_ id: Int, pitch: Int = 60, start: Int = 0, length: Int = 480, velocity: Int = 100) -> RollNote {
    RollNote(id: id, pitch: pitch, start: start, length: length, velocity: velocity, anchor: 1, offset: start, active: true)
  }

  // MARK: - Limits

  func testTheLimitsMirrorTheEnginesDomain() {
    XCTAssertEqual(RollLimits.engineMaxNoteTick, 15_728_640, "kMaxNoteTick = 256 × 64 × 960 (engine/include/chordlab/tracks.h)")
    XCTAssertEqual(RollLimits.engineMinNoteTicks, 30, "kMinTrackNoteTicks")
    XCTAssertEqual(limits.maxNoteTick, RollLimits.engineMaxNoteTick)
    let timing = RollTiming(ticksPerBeat: 960, barTicks: 3840, endTick: 7680, minNoteTicks: 40)
    XCTAssertEqual(RollLimits(timing: timing), RollLimits(minNoteTicks: 40, endTick: 7680))
  }

  // MARK: - Move

  func testAGroupAtTheTopRowCannotMoveUpAndKeepsItsInterval() {
    let notes = [note(1, pitch: 126), note(2, pitch: 127)]
    XCTAssertEqual(RollGroupMath.move(notes, dPitch: 1, dTick: 0, limits: limits).dPitch, 0)
    XCTAssertEqual(RollGroupMath.move(notes, dPitch: -3, dTick: 0, limits: limits).dPitch, -3)
    let low = [note(1, pitch: 0), note(2, pitch: 5)]
    XCTAssertEqual(RollGroupMath.move(low, dPitch: -4, dTick: 0, limits: limits).dPitch, 0)
    XCTAssertEqual(RollGroupMath.move([note(1, pitch: 120), note(2, pitch: 124)], dPitch: 12, dTick: 0, limits: limits).dPitch, 3,
                   "the shared delta stops where the highest note reaches 127")
  }

  func testAGroupAtTickZeroCannotMoveLeftAndKeepsItsSpacing() {
    let notes = [note(1, start: 0), note(2, start: 480)]
    XCTAssertEqual(RollGroupMath.move(notes, dPitch: 0, dTick: -240, limits: limits).dTick, 0)
    let later = [note(1, start: 100), note(2, start: 580)]
    XCTAssertEqual(RollGroupMath.move(later, dPitch: 0, dTick: -480, limits: limits).dTick, -100)
  }

  func testNoStartMayReachTheProgressionsEnd() {
    let notes = [note(1, start: 14_000), note(2, start: 15_000)]
    XCTAssertEqual(RollGroupMath.move(notes, dPitch: 0, dTick: 1000, limits: limits).dTick, 359, "the latest start stops at 15,359")
  }

  func testNoEndMayPassTheDomain() {
    let wide = RollLimits(minNoteTicks: 30, endTick: RollLimits.engineMaxNoteTick)
    let notes = [note(1, start: 15_000_000, length: 700_000)]
    XCTAssertEqual(RollGroupMath.move(notes, dPitch: 0, dTick: 100_000, limits: wide).dTick, 28_640)
  }

  func testABoundANoteAlreadyBreaksNeverForcesAMove() {
    // An inactive note of the last chord can start past the end (D3): the group stays where it is, may move left,
    // never further right.
    let notes = [note(1, start: 1000), note(2, start: 16_000)]
    XCTAssertEqual(RollGroupMath.move(notes, dPitch: 0, dTick: 0, limits: limits).dTick, 0)
    XCTAssertEqual(RollGroupMath.move(notes, dPitch: 0, dTick: 480, limits: limits).dTick, 0)
    XCTAssertEqual(RollGroupMath.move(notes, dPitch: 0, dTick: -480, limits: limits).dTick, -480)
  }

  func testAnEmptyGroupMovesNowhere() {
    XCTAssertTrue(RollGroupMath.move([], dPitch: 3, dTick: 480, limits: limits) == (0, 0))
  }

  // MARK: - Resize

  func testAGroupResizeStopsWhenTheShortestNoteReachesTheMinimum() {
    let notes = [note(1, start: 0, length: 100), note(2, start: 960, length: 40)]
    XCTAssertEqual(RollGroupMath.resize(notes, edge: .end, dTick: -50, limits: limits), -10)
    XCTAssertEqual(RollGroupMath.resize(notes, edge: .end, dTick: 200, limits: limits), 200)
    XCTAssertEqual(RollGroupMath.resize(notes, edge: .start, dTick: 50, limits: limits), 10)
  }

  func testAStartEdgeStopsAtTickZeroAndBeforeTheEnd() {
    let notes = [note(1, start: 20, length: 500), note(2, start: 500, length: 500)]
    XCTAssertEqual(RollGroupMath.resize(notes, edge: .start, dTick: -100, limits: limits), -20)
    let late = [note(1, start: 15_300, length: 960)]
    XCTAssertEqual(RollGroupMath.resize(late, edge: .start, dTick: 200, limits: limits), 59, "the start stays before 15,360")
  }

  func testAnEndEdgeStopsAtTheDomainsEnd() {
    let notes = [note(1, start: RollLimits.engineMaxNoteTick - 100, length: 60)]
    XCTAssertEqual(RollGroupMath.resize(notes, edge: .end, dTick: 500, limits: limits), 40)
  }

  // MARK: - Velocity

  func testAVelocityDeltaKeepsEveryNoteIn1To127() {
    let notes = [note(1, velocity: 100), note(2, velocity: 120)]
    XCTAssertEqual(RollGroupMath.velocity(notes, dVelocity: 20), 7)
    XCTAssertEqual(RollGroupMath.velocity([note(1, velocity: 5), note(2, velocity: 50)], dVelocity: -10), -4)
    XCTAssertEqual(RollGroupMath.velocity(notes, dVelocity: -30), -30)
    XCTAssertEqual(RollGroupMath.velocity([], dVelocity: 9), 0)
  }

  // MARK: - Nudge

  func testNudgeInEachDirection() {
    let notes = [note(1, pitch: 60, start: 960), note(2, pitch: 64, start: 1440)]
    XCTAssertEqual(RollGroupMath.nudge(notes, direction: .up, octave: false, gridTicks: 480, limits: limits),
                   [RollNoteEdit(id: 1, pitch: 61), RollNoteEdit(id: 2, pitch: 65)])
    XCTAssertEqual(RollGroupMath.nudge(notes, direction: .down, octave: false, gridTicks: 480, limits: limits),
                   [RollNoteEdit(id: 1, pitch: 59), RollNoteEdit(id: 2, pitch: 63)])
    XCTAssertEqual(RollGroupMath.nudge(notes, direction: .up, octave: true, gridTicks: 480, limits: limits),
                   [RollNoteEdit(id: 1, pitch: 72), RollNoteEdit(id: 2, pitch: 76)])
    XCTAssertEqual(RollGroupMath.nudge(notes, direction: .down, octave: true, gridTicks: 480, limits: limits),
                   [RollNoteEdit(id: 1, pitch: 48), RollNoteEdit(id: 2, pitch: 52)])
    XCTAssertEqual(RollGroupMath.nudge(notes, direction: .left, octave: false, gridTicks: 480, limits: limits),
                   [RollNoteEdit(id: 1, start: 480), RollNoteEdit(id: 2, start: 960)])
    XCTAssertEqual(RollGroupMath.nudge(notes, direction: .right, octave: false, gridTicks: 240, limits: limits),
                   [RollNoteEdit(id: 1, start: 1200), RollNoteEdit(id: 2, start: 1680)])
    XCTAssertEqual(RollGroupMath.nudge(notes, direction: .right, octave: true, gridTicks: 240, limits: limits),
                   [RollNoteEdit(id: 1, start: 1200), RollNoteEdit(id: 2, start: 1680)], "⇧ means an octave only vertically")
  }

  func testNudgeClampsAndIsNilWhenNothingCanMove() {
    let top = [note(1, pitch: 127, start: 0)]
    XCTAssertNil(RollGroupMath.nudge(top, direction: .up, octave: false, gridTicks: 480, limits: limits))
    XCTAssertNil(RollGroupMath.nudge(top, direction: .left, octave: false, gridTicks: 480, limits: limits))
    XCTAssertEqual(RollGroupMath.nudge([note(1, start: 100)], direction: .left, octave: false, gridTicks: 480, limits: limits),
                   [RollNoteEdit(id: 1, start: 0)], "a partial step to tick 0")
    XCTAssertEqual(RollGroupMath.nudge([note(1, start: 15_200)], direction: .right, octave: false, gridTicks: 480, limits: limits),
                   [RollNoteEdit(id: 1, start: 15_359)], "a partial step stops before the end")
    XCTAssertNil(RollGroupMath.nudge([note(1, start: 15_359)], direction: .right, octave: false, gridTicks: 480, limits: limits))
    XCTAssertNil(RollGroupMath.nudge([], direction: .up, octave: false, gridTicks: 480, limits: limits))
  }

  func testAnOctaveNudgeMovesAWholeOctaveOrNothing() {
    XCTAssertNil(RollGroupMath.nudge([note(1, pitch: 120)], direction: .up, octave: true, gridTicks: 480, limits: limits),
                 "seven semitones would change the pitch classes")
    XCTAssertNil(RollGroupMath.nudge([note(1, pitch: 11), note(2, pitch: 40)], direction: .down, octave: true, gridTicks: 480,
                                     limits: limits))
    XCTAssertEqual(RollGroupMath.nudge([note(1, pitch: 12)], direction: .down, octave: true, gridTicks: 480, limits: limits),
                   [RollNoteEdit(id: 1, pitch: 0)])
  }

  // MARK: - Edits

  func testEditsCarryOnlyTheFieldsThatChanged() {
    let notes = [note(1, pitch: 60, start: 0, length: 480, velocity: 100)]
    XCTAssertEqual(RollGroupMath.moveEdits(notes, dPitch: 2, dTick: 0), [RollNoteEdit(id: 1, pitch: 62)])
    XCTAssertEqual(RollGroupMath.moveEdits(notes, dPitch: 0, dTick: 480), [RollNoteEdit(id: 1, start: 480)])
    XCTAssertEqual(RollGroupMath.moveEdits(notes, dPitch: -1, dTick: 960), [RollNoteEdit(id: 1, pitch: 59, start: 960)])
    XCTAssertEqual(RollGroupMath.moveEdits(notes, dPitch: 0, dTick: 0), [])
    XCTAssertEqual(RollGroupMath.resizeEdits(notes, edge: .end, dTick: 240), [RollNoteEdit(id: 1, length: 720)])
    XCTAssertEqual(RollGroupMath.resizeEdits([note(1, start: 960, length: 480)], edge: .start, dTick: -480),
                   [RollNoteEdit(id: 1, start: 480, length: 960)])
    XCTAssertEqual(RollGroupMath.velocityEdits(notes, dVelocity: -10), [RollNoteEdit(id: 1, velocity: 90)])
  }
}
