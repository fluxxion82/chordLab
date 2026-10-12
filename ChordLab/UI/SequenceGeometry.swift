import CoreGraphics
import Foundation

/// The sequence bar's geometry, pure and in one place so that block rendering, the ruler, the playhead and
/// the reorder arithmetic cannot disagree. Everything is linear: `x = beat × pxPerBeat`, with no minimum
/// block width and no spacing between blocks (a hairline is drawn *inside* a block's exact frame), so a
/// quarter-beat block is 6 pt wide at the fixed zoom. That is narrow, but it keeps one mapping for the
/// ruler ticks and the playhead too; a piecewise mapping with a minimum width would have made beats
/// uneven along the ruler. The rendered width of block `i` is `frames[i].width`, nothing else.
///
/// Depends on nothing but CoreGraphics and Foundation, so it also compiles under plain `swiftc` for the
/// scratchpad checks (ChordLab has no XCTest target).
enum SequenceGeometry {
  /// One block's horizontal extent, in points from the strip's origin.
  struct BlockFrame: Equatable {
    let x: CGFloat
    let width: CGFloat
    var centre: CGFloat { x + width / 2 }
  }

  /// The blocks in order plus the width the progression ends at (`endX`, the end marker and the content
  /// width before any slack).
  struct Layout: Equatable {
    let frames: [BlockFrame]
    let endX: CGFloat
  }

  /// Prefix sums of the rendered widths, so that abutting blocks in an HStack land exactly here.
  static func layout(lengths: [Double], pxPerBeat: CGFloat) -> Layout {
    var frames: [BlockFrame] = []
    frames.reserveCapacity(lengths.count)
    var x: CGFloat = 0
    for length in lengths {
      let width = max(0, CGFloat(length.isFinite ? length : 0)) * pxPerBeat
      frames.append(BlockFrame(x: x, width: width))
      x += width
    }
    return Layout(frames: frames, endX: x)
  }

  /// The x of a beat under the same linear mapping (ruler ticks, the playhead).
  static func x(ofBeat beat: Double, pxPerBeat: CGFloat) -> CGFloat {
    CGFloat(beat) * pxPerBeat
  }

  /// Where the block at `from` lands after being dragged `dx` points: its new index is the number of
  /// other blocks whose centre lies left of the dragged block's dropped centre, measured in the undragged
  /// layout (which is what the other blocks still show during the drag). `from` outside the layout: itself.
  static func targetIndex(from: Int, droppedBy dx: CGFloat, in layout: Layout) -> Int {
    guard layout.frames.indices.contains(from) else { return from }
    let dropped = layout.frames[from].centre + dx
    return layout.frames.enumerated().filter { $0.offset != from && $0.element.centre < dropped }.count
  }

  /// A section's horizontal extent in the header row: from its first block's x to the next section's first
  /// block's x, or `endX` for the last.
  struct SpanFrame: Equatable {
    let x: CGFloat
    let width: CGFloat
  }

  /// The spans of sections starting at the block indices `starts` (in order, as the snapshot lists them):
  /// each runs from `frames[start].x` to the next start's x, the last to `endX`. A start outside the
  /// layout, or not after the previous one, gets a zero-width span at the layout's end (drawn as nothing),
  /// so a stale snapshot cannot put a span anywhere the blocks are not.
  static func sectionSpans(starts: [Int], in layout: Layout) -> [SpanFrame] {
    var spans: [SpanFrame] = []
    spans.reserveCapacity(starts.count)
    for (i, start) in starts.enumerated() {
      guard layout.frames.indices.contains(start), i == 0 || start > starts[i - 1] else {
        spans.append(SpanFrame(x: layout.endX, width: 0))
        continue
      }
      let x = layout.frames[start].x
      let next = i + 1 < starts.count ? starts[i + 1] : nil
      let end = next.flatMap { layout.frames.indices.contains($0) && $0 > start ? layout.frames[$0].x : nil } ?? layout.endX
      spans.append(SpanFrame(x: x, width: max(0, end - x)))
    }
    return spans
  }

  /// The block whose start is nearest to `x` (a section edge dropped there): the index in
  /// 0...frames.count-1 minimising |frames[i].x - x|, the lower index on a tie; 0 without blocks.
  static func boundaryIndex(nearestTo x: CGFloat, in layout: Layout) -> Int {
    var best = 0
    var bestDistance = CGFloat.infinity
    for (i, frame) in layout.frames.enumerated() {
      let distance = abs(frame.x - x)
      if distance < bestDistance {
        best = i
        bestDistance = distance
      }
    }
    return best
  }

  /// `lengthBeats` after dragging its handle by `dx` points: snapped to `snap` and clamped to `range`.
  static func resizedLength(_ lengthBeats: Double, by dx: CGFloat, pxPerBeat: CGFloat,
                            snap: Double, range: ClosedRange<Double>) -> Double {
    let beats = lengthBeats + Double(dx / pxPerBeat)
    let snapped = (beats / snap).rounded() * snap
    return min(range.upperBound, max(range.lowerBound, snapped))
  }
}
