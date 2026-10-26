import SwiftUI

/// The function dot: a `size`-pt disc in the function's colour; nothing at all when the snapshot has no
/// function (an older plug-in build).
struct FunctionDot: View {
  let function: String?
  var size: CGFloat = 6

  var body: some View {
    if let code = function {
      Circle()
        .fill(FunctionStyle.color(code))
        .frame(width: size, height: size)
        .accessibilityLabel(HarmonyText.functionWord(code) ?? "")
    }
  }
}

/// A thin tension bar: a dim track `width` wide with the function-coloured fill `tension`/100 of it (0 is
/// an empty track), and the number beside it.
struct TensionBar: View {
  let tension: Int
  let function: String?
  var width: CGFloat = 100

  var body: some View {
    HStack(spacing: 6) {
      Text("tension").font(.caption).foregroundStyle(.secondary)
      ZStack(alignment: .leading) {
        Capsule().fill(Color.primary.opacity(0.12))
        Capsule().fill(FunctionStyle.color(function))
          .frame(width: width * HarmonyText.tensionFraction(tension))
      }
      .frame(width: width, height: 4)
      Text("\(tension)").font(.caption.monospacedDigit()).foregroundStyle(.secondary)
    }
    .accessibilityElement(children: .ignore)
    .accessibilityLabel("tension \(tension) of 100")
  }
}

/// A row of small rounded chips (the "why" reasons under a tip's sentence), wrapping onto new lines
/// when the row would be wider than `maxWidth`.
struct ChipRow: View {
  let chips: [String]
  var maxWidth: CGFloat = 260

  var body: some View {
    // Chips are short, so a greedy line break on a character estimate is close enough; the tip is
    // not a layout anyone resizes.
    let lines = Self.lines(chips, maxWidth: maxWidth)
    VStack(alignment: .leading, spacing: 4) {
      ForEach(lines.indices, id: \.self) { row in
        HStack(spacing: 4) {
          ForEach(lines[row].indices, id: \.self) { i in
            Text(lines[row][i])
              .font(.caption)
              .padding(.horizontal, 6).padding(.vertical, 2)
              .background(Capsule().fill(Color.primary.opacity(0.1)))
          }
        }
      }
    }
  }

  /// Greedy line breaking at ~6.5 pt per character plus the chip padding.
  static func lines(_ chips: [String], maxWidth: CGFloat) -> [[String]] {
    var lines: [[String]] = []
    var current: [String] = []
    var width: CGFloat = 0
    for chip in chips {
      let chipWidth = CGFloat(chip.count) * 6.5 + 16
      if !current.isEmpty && width + chipWidth > maxWidth {
        lines.append(current)
        current = []
        width = 0
      }
      current.append(chip)
      width += chipWidth
    }
    if !current.isEmpty { lines.append(current) }
    return lines
  }
}
