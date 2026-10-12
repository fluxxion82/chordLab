import SwiftUI

/// The next-chord suggestions (P2-D3) as a row of up to 8 chips under the sequence bar, best first: the
/// symbol, the numeral and the function dot, numbered 1–8. The engine ranks them after the selected
/// block's chord, else the last chord, and recomputes on every snapshot, so the strip follows the
/// selection; with no chords it offers opening chords under "Start with". Clicking a chip (or pressing
/// its digit) appends that chord (`appendChord`); resting the mouse on it shows the engine's reasons
/// (`reasonTexts`). Nothing is shown for a snapshot without `suggestions` (an older plug-in build).
///
/// The digit shortcuts are bare keys (no modifier) on the chip buttons themselves. Live may swallow bare
/// digits before the plug-in window sees them (a Live check); in the standalone APP they work from any
/// focus.
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
      ForEach(Array(suggestions.enumerated()), id: \.element.id) { index, suggestion in
        SuggestionChip(suggestion: suggestion, number: index + 1) {
          state.send(command: ["cmd": "appendChord", "root": suggestion.root, "type": suggestion.type])
        }
      }
      if suggestions.isEmpty {
        Text("—").foregroundStyle(.gray)
      }
      Spacer(minLength: 0)
    }
    .font(.body)
    .frame(height: Self.height)
    .accessibilityElement(children: .contain)
    .accessibilityLabel(heading == "Next" ? "Next chord suggestions" : "Opening chord suggestions")
  }
}

/// One chip: a button that appends the chord, with the digit `number` as its bare-key shortcut.
struct SuggestionChip: View {
  let suggestion: SuggestionJSON
  let number: Int
  let append: () -> Void

  @State private var hovering = false

  var body: some View {
    Button(action: append) {
      HStack(spacing: 5) {
        Text("\(number)")
          .font(.system(size: 9).monospacedDigit())
          .foregroundStyle(.white.opacity(0.45))
        Text(ChordSymbol.glyphs(suggestion.symbol))
          .font(.system(size: 13, weight: .medium))
        Text(ChordSymbol.glyphs(suggestion.roman))
          .font(.system(size: 10))
          .opacity(0.8)
        FunctionDot(function: suggestion.function)
      }
      .foregroundStyle(.white)
      .lineLimit(1)
      .padding(.horizontal, 9).padding(.vertical, 6)
      .background(RoundedRectangle(cornerRadius: 8).fill(Color.white.opacity(hovering ? 0.16 : 0.08)))
      .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(FunctionStyle.color(suggestion.function).opacity(0.5), lineWidth: 1))
      .contentShape(RoundedRectangle(cornerRadius: 8))
    }
    .buttonStyle(.plain)
    .keyboardShortcut(KeyEquivalent(Character(String(number))), modifiers: [])
    .onHover { hovering = $0 }
    .hoverPopover(arrowEdge: .bottom) {
      SuggestionDetailView(suggestion: suggestion).padding(12)
    }
    .help("Append \(ChordSymbol.glyphs(suggestion.symbol)) (key \(number))")
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
