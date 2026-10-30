import SwiftUI

/// The row under the key bar while `state.keyRow` is set (ContentView shows it in its own row, so the rows
/// below move down and nothing is covered). The hint (Transpose with key off, plan D8): the chords kept their
/// notes, with Move once, Move and always transpose, and Keep chords. The moved note (switch on, D9): how many
/// chords moved and to which tonic, text only, for four seconds (`ChordLabState.movedNoteDuration`).
struct KeyTransposeRow: View {
  @EnvironmentObject var state: ChordLabState
  let row: ChordLabState.KeyRow
  /// In a window shorter than `ContentView.compactHeight`: callout text, small buttons, a small icon and less
  /// vertical padding, so the hint fits on one line at the 700-pt minimum width and the row is at most 24 pt
  /// tall in either state (the palette keeps 44 pt or more at 700 x 480).
  var compact = false

  static let hintText = "Chords in the sequence kept their notes."

  static func movedText(count: Int, tonic: String) -> String {
    "Moved \(count) \(count == 1 ? "chord" : "chords") to \(tonic). ⌘Z undoes it."
  }

  var body: some View {
    Group {
      switch row {
      case .hint:
        // One row: the text on one line where it fits (900 pt; compact, also the 700-pt minimum), else wrapped
        // beside the buttons; the buttons below the text only when even that does not fit.
        ViewThatFits(in: .horizontal) {
          HStack(spacing: 12) {
            hintLabel(wrapped: false)
            hintButtons
            Spacer(minLength: 0)
          }
          HStack(spacing: 12) {
            hintLabel(wrapped: true)
            hintButtons
            Spacer(minLength: 0)
          }
          VStack(alignment: .leading, spacing: 8) {
            hintLabel(wrapped: false)
            HStack(spacing: 12) {
              hintButtons
              Spacer(minLength: 0)
            }
          }
        }
      case .moved(let count, let tonic):
        HStack(spacing: 8) {
          Image(systemName: "arrow.up.arrow.down").foregroundStyle(.secondary)
            .accessibilityHidden(true)
          Text(Self.movedText(count: count, tonic: tonic)).foregroundStyle(.white)
          Spacer(minLength: 0)
        }
        .allowsHitTesting(false)
      }
    }
    .font(compact ? .callout : .body)
    .controlSize(compact ? .small : .regular)
    .imageScale(compact ? .small : .medium)
    .padding(.horizontal, 10)
    .padding(.vertical, compact ? 2 : 6)
    .background(RoundedRectangle(cornerRadius: 6).fill(Color.white.opacity(0.07)))
  }

  @ViewBuilder private func hintLabel(wrapped: Bool) -> some View {
    HStack(spacing: 8) {
      Image(systemName: "info.circle").foregroundStyle(.secondary)
        .accessibilityHidden(true)
      if wrapped {
        Text(Self.hintText).foregroundStyle(.white)
          .lineLimit(2).fixedSize(horizontal: false, vertical: true)
          .frame(width: Self.wrappedTextWidth, alignment: .leading)
      } else {
        Text(Self.hintText).foregroundStyle(.white).lineLimit(1).fixedSize()
      }
    }
  }

  /// The hint text's width when it wraps onto two lines beside the buttons.
  private static let wrappedTextWidth: CGFloat = 160

  private var hintButtons: some View {
    HStack(spacing: 8) {
      Button("Move once") { state.moveChordsWithKey(always: false) }
        .help("Transpose every chord in the sequence from the old key to the new one, this time")
        .accessibilityLabel("Move once")
      Button("Move and always transpose") { state.moveChordsWithKey(always: true) }
        .help("Transpose every chord now, and turn on Transpose with key so later key changes move them too")
        .accessibilityLabel("Move and always transpose")
      Button("Keep chords") { state.keepChords() }
        .help("Leave the chords as they are")
        .accessibilityLabel("Keep chords")
    }
    .fixedSize()
  }
}
