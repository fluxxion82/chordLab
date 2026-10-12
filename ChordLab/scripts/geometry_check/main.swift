// Checks of SequenceGeometry (ChordLab has no XCTest target): `run.sh` compiles this with plain swiftc
// against ../../UI/SequenceGeometry.swift and runs it. Prints one line per expectation and exits non-zero
// if any failed. Add a case here whenever the sequence bar's geometry rules change.
import CoreGraphics
import Foundation

var failures = 0
func expect(_ condition: Bool, _ what: String, file: String = #file, line: Int = #line) {
  if condition {
    print("ok    \(what)")
  } else {
    failures += 1
    print("FAIL  \(what)  (line \(line))")
  }
}

let px: CGFloat = 24

// --- 32 quarter-beat blocks: the case Codex raised ---
let quarters = SequenceGeometry.layout(lengths: Array(repeating: 0.25, count: 32), pxPerBeat: px)
expect(quarters.frames.count == 32, "32 frames")
expect(quarters.endX == 192, "32 quarter-beat blocks span 192 pt (not 320): endX == \(quarters.endX)")
expect(quarters.frames.allSatisfy { $0.width == 6 }, "every quarter-beat block is 6 pt wide")
expect(quarters.frames.enumerated().allSatisfy { CGFloat($0.offset) * 6 == $0.element.x }, "blocks abut at multiples of 6")
expect(quarters.frames[0].centre == 3 && quarters.frames[1].centre == 9, "centres at 3, 9, ...")
expect(SequenceGeometry.x(ofBeat: 8, pxPerBeat: px) == quarters.endX, "the end marker x equals the ruler's beat 8")
expect(SequenceGeometry.targetIndex(from: 0, droppedBy: 2, in: quarters) == 0, "dragging block 0 by 2 pt: stays at 0 (centre 5 < 9)")
expect(SequenceGeometry.targetIndex(from: 0, droppedBy: 5.9, in: quarters) == 0, "by 5.9 pt: still 0 (centre 8.9 < 9)")
expect(SequenceGeometry.targetIndex(from: 0, droppedBy: 7, in: quarters) == 1, "by 7 pt: index 1, and the block is drawn 7 pt right, past block 1's centre")
expect(SequenceGeometry.targetIndex(from: 0, droppedBy: 12.5, in: quarters) == 2, "by 12.5 pt: index 2 (centre 15.5 > 9, 15)")
expect(SequenceGeometry.targetIndex(from: 31, droppedBy: -1000, in: quarters) == 0, "the last block dragged far left: 0")
expect(SequenceGeometry.targetIndex(from: 5, droppedBy: 0, in: quarters) == 5, "no drag: itself")
expect(SequenceGeometry.targetIndex(from: 40, droppedBy: 10, in: quarters) == 40, "index outside the layout: itself")

// --- mixed lengths: 4, 0.25, 2 beats ---
let mixed = SequenceGeometry.layout(lengths: [4, 0.25, 2], pxPerBeat: px)
expect(mixed.frames.map(\.x) == [0, 96, 102], "x: 0, 96, 102")
expect(mixed.frames.map(\.width) == [96, 6, 48], "widths: 96, 6, 48")
expect(mixed.endX == 150, "endX 150")
expect(mixed.frames.map(\.centre) == [48, 99, 126], "centres 48, 99, 126")
expect(SequenceGeometry.targetIndex(from: 2, droppedBy: -100, in: mixed) == 0, "block 2 dropped at 26: before everything")
expect(SequenceGeometry.targetIndex(from: 2, droppedBy: -30, in: mixed) == 1, "block 2 dropped at 96: after block 0, before block 1 (centre 99)")
expect(SequenceGeometry.targetIndex(from: 0, droppedBy: 60, in: mixed) == 1, "block 0 dropped at 108: after block 1 (99), before block 2 (126)")
expect(SequenceGeometry.targetIndex(from: 0, droppedBy: 100, in: mixed) == 2, "block 0 dropped at 148: last")
expect(SequenceGeometry.targetIndex(from: 1, droppedBy: -60, in: mixed) == 0, "the quarter-beat block dropped at 39: first")
expect(SequenceGeometry.targetIndex(from: 1, droppedBy: -40, in: mixed) == 1, "the quarter-beat block dropped at 59: after block 0's centre, still 1")

// --- rendering and the ruler agree: the x of beat b equals the frame of the block starting there ---
let starts: [Double] = [0, 4, 4.25]
expect(zip(starts, mixed.frames).allSatisfy { SequenceGeometry.x(ofBeat: $0, pxPerBeat: px) == $1.x }, "beat -> x matches every block start")

// --- degenerate input ---
let empty = SequenceGeometry.layout(lengths: [], pxPerBeat: px)
expect(empty.frames.isEmpty && empty.endX == 0, "empty layout")
let odd = SequenceGeometry.layout(lengths: [1, .nan, -2, .infinity], pxPerBeat: px)
expect(odd.frames.map(\.width) == [24, 0, 0, 0] && odd.endX == 24, "non-finite or negative lengths render as 0 wide")

// --- resize snapping and clamping ---
let range = 0.25...64.0
expect(SequenceGeometry.resizedLength(4, by: 0, pxPerBeat: px, snap: 0.25, range: range) == 4, "no drag: unchanged")
expect(SequenceGeometry.resizedLength(4, by: 24, pxPerBeat: px, snap: 0.25, range: range) == 5, "+24 pt: +1 beat")
expect(SequenceGeometry.resizedLength(4, by: 5, pxPerBeat: px, snap: 0.25, range: range) == 4.25, "+5 pt (0.21 beat) snaps to 4.25")
expect(SequenceGeometry.resizedLength(4, by: 2, pxPerBeat: px, snap: 0.25, range: range) == 4, "+2 pt (0.08 beat) snaps back to 4")
expect(SequenceGeometry.resizedLength(0.5, by: -1000, pxPerBeat: px, snap: 0.25, range: range) == 0.25, "clamped to 0.25")
expect(SequenceGeometry.resizedLength(60, by: 1000, pxPerBeat: px, snap: 0.25, range: range) == 64, "clamped to 64")

print(failures == 0 ? "ALL OK" : "\(failures) FAILED")
exit(failures == 0 ? 0 : 1)
