import SwiftUI

/// The piano roll's keyboard column (tracks design D14): one key per pitch row, row-aligned with the grid at the same
/// vertical scroll and row height, drawn in one `Canvas` for the visible rows only. White keys are light rows; a
/// black key is a dark bar over the left `blackKeyFraction` of its row, the rest of the row light like the white key
/// it sits in; a thin line marks the two places white keys meet (B|C and E|F). Each C carries its label ("C4" for
/// MIDI 60, middle C; `RollGeometry.cLabel`) at the right. Clicking a key does nothing yet (plan Task 19 previews
/// the pitch); the column is hidden from accessibility until then.
struct PianoKeyboardView: View {
  let geometry: RollGeometry
  /// The grid's vertical scroll (content y at the top), shared with the canvas.
  let scrollY: CGFloat

  /// How much of a black key's row its dark bar covers.
  static let blackKeyFraction: CGFloat = 0.62

  var body: some View {
    Canvas { context, size in
      let scroll = CGPoint(x: 0, y: scrollY)
      let pitches = geometry.visiblePitches(viewport: size, scroll: scroll)
      context.fill(Path(CGRect(origin: .zero, size: size)), with: .color(RollColors.whiteKey))
      var black = Path()
      var seams = Path()
      for pitch in pitches {
        let y = geometry.y(pitch: pitch) - scrollY
        if RollGeometry.isBlackKey(pitch: pitch) {
          black.addRoundedRect(in: CGRect(x: 0, y: y + 0.5, width: size.width * Self.blackKeyFraction,
                                          height: geometry.rowHeight - 1),
                               cornerSize: CGSize(width: 1.5, height: 1.5))
        } else if pitch % 12 == 0 || pitch % 12 == 5 {
          // Under C and F a white key meets the white key below it (B, E).
          let line = (y + geometry.rowHeight).rounded(.down) + 0.5
          seams.move(to: CGPoint(x: 0, y: line))
          seams.addLine(to: CGPoint(x: size.width, y: line))
        }
      }
      context.stroke(seams, with: .color(RollColors.keySeam), lineWidth: 1)
      context.fill(black, with: .color(RollColors.blackKey))
      for pitch in pitches {
        guard let label = RollGeometry.cLabel(pitch: pitch) else { continue }
        let y = geometry.y(pitch: pitch) - scrollY + geometry.rowHeight / 2
        context.draw(Text(label).font(RollColors.keyLabelFont).foregroundColor(RollColors.keyLabel),
                     at: CGPoint(x: size.width - 3, y: y), anchor: .trailing)
      }
      // The column's right edge against the grid.
      context.fill(Path(CGRect(x: size.width - 1, y: 0, width: 1, height: size.height)), with: .color(RollColors.keyEdge))
    }
    .accessibilityHidden(true)
  }
}
