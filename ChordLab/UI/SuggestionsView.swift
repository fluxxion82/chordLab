import SwiftUI

/// The next-chord suggestions (P2-D3) as a row of up to 8 chips under the sequence bar, best first: the
/// symbol, the numeral and the function dot, numbered 1–8. The engine ranks them after the selected
/// block's chord, else the last chord, and recomputes on every snapshot, so the strip follows the
/// selection; with no chords it offers opening chords under "Start with". Clicking a chip (or pressing
/// its digit) appends that chord (`appendChord`); resting the mouse on it shows the engine's reasons
/// (`reasonTexts`). Nothing is shown for a snapshot without `suggestions` (an older plug-in build).
///
/// The chips never truncate. `ViewThatFits` takes the first of three rows whose ideal width fits: the
/// one-line chips (digit, symbol, numeral, dot), else two-line chips (the symbol over the digit, numeral and
/// dot), which keep all 8 of the longest realistic chords in view at the 700-pt minimum width, else, for
/// a list longer still, the two-line row in a sideways scroll view. Only the chosen row exists, and the
/// digits always follow the engine's order.
///
/// The digits 1–8 append the chips through the key routes (`KeyCommandMap`, `EditorKeyRouter`), never
/// through this view.
struct SuggestionsView: View {
  @EnvironmentObject var state: ChordLabState

  static let maxChips = 8
  static let height: CGFloat = 40

  private var suggestions: [SuggestionJSON] {
    Array((state.snapshot?.suggestions ?? []).prefix(Self.maxChips))
  }

  private var heading: String {
    (state.snapshot?.events.isEmpty ?? true) ? "Start with" : "Next"
  }

  var body: some View {
    HStack(spacing: 8) {
      Text(heading)
        .foregroundStyle(.secondary)
        .frame(minWidth: 64, alignment: .leading)
        .accessibilityHidden(true)
      if suggestions.isEmpty {
        Text("—").foregroundStyle(.gray)
      } else {
        ViewThatFits(in: .horizontal) {
          chipRow(compact: false)
          chipRow(compact: true)
          ScrollView(.horizontal, showsIndicators: false) { chipRow(compact: true) }
        }
      }
    }
    .font(.body)
    .frame(maxWidth: .infinity, alignment: .leading)   // no trailing Spacer: its 8-pt gap would cost the chips
    .frame(height: Self.height)
    .accessibilityElement(children: .contain)
    .accessibilityLabel(heading == "Next" ? "Next chord suggestions" : "Opening chord suggestions")
  }

  private func chipRow(compact: Bool) -> some View {
    HStack(spacing: compact ? 5 : 8) {
      ForEach(Array(suggestions.enumerated()), id: \.element.id) { index, suggestion in
        SuggestionChip(suggestion: suggestion, number: index + 1, compact: compact) {
          state.send(command: ["cmd": "appendChord", "root": suggestion.root, "type": suggestion.type])
        }
      }
    }
  }
}

/// One chip: a button that appends the chord on click, showing its digit `number`. `compact` stacks the
/// symbol over the digit, numeral and dot, for a strip too narrow for one line per chip.
struct SuggestionChip: View {
  let suggestion: SuggestionJSON
  let number: Int
  var compact = false
  let append: () -> Void

  @State private var hovering = false

  private var digitText: some View {
    Text("\(number)")
      .font(.system(size: 9).monospacedDigit())
      .foregroundStyle(.white.opacity(0.45))
  }

  private var symbolText: some View {
    Text(ChordSymbol.glyphs(suggestion.symbol)).font(.system(size: 13, weight: .medium))
  }

  private var numeralText: some View {
    Text(ChordSymbol.glyphs(suggestion.roman)).font(.system(size: 10)).opacity(0.8)
  }

  var body: some View {
    Button(action: append) {
      Group {
        if compact {
          VStack(alignment: .leading, spacing: 1) {
            symbolText
            HStack(spacing: 4) { digitText; numeralText; FunctionDot(function: suggestion.function) }
          }
          .padding(.horizontal, 6).padding(.vertical, 3)
        } else {
          HStack(spacing: 5) {
            digitText
            symbolText
            numeralText
            FunctionDot(function: suggestion.function)
          }
          .padding(.horizontal, 9).padding(.vertical, 6)
        }
      }
      .foregroundStyle(.white)
      .lineLimit(1)
      .background(RoundedRectangle(cornerRadius: 8).fill(Color.white.opacity(hovering ? 0.16 : 0.08)))
      .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(FunctionStyle.color(suggestion.function).opacity(0.5), lineWidth: 1))
      .contentShape(RoundedRectangle(cornerRadius: 8))
    }
    .buttonStyle(.plain)
    .onHover { hovering = $0 }
    .hoverPopover(arrowEdge: .bottom) {
      SuggestionDetailView(suggestion: suggestion).padding(12)
    }
    .accessibilityLabel("Append " + ChordSymbol.spokenName(symbol: suggestion.symbol, type: suggestion.type) +
                        ", suggestion \(number)")
  }
}

/// The chip's hover popover: symbol, numeral and function word, then the reasons on one line.
struct SuggestionDetailView: View {
  let suggestion: SuggestionJSON

  var body: some View {
    VStack(alignment: .leading, spacing: 6) {
      HStack(alignment: .firstTextBaseline, spacing: 10) {
        Text(ChordSymbol.glyphs(suggestion.symbol)).font(.title2.weight(.semibold))
        Text(ChordSymbol.glyphs(suggestion.roman)).font(.title3).foregroundStyle(.secondary)
      }
      if let word = HarmonyText.functionWord(suggestion.function) {
        HStack(spacing: 6) {
          FunctionDot(function: suggestion.function)
          Text(word).font(.callout).foregroundStyle(.secondary)
        }
      }
      Text(HarmonyText.reasonsLine(suggestion.reasonTexts))
        .font(.callout)
        .fixedSize(horizontal: false, vertical: true)
    }
    .frame(minWidth: 160, maxWidth: 320, alignment: .leading)
  }
}

struct SuggestionsView_Previews: PreviewProvider {
  static var previews: some View {
    SuggestionsView().environmentObject(ChordLabState(numParams: kNumParams))
      .padding().background(PaletteView.background).frame(width: 900)
  }
}
