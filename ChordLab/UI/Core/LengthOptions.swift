import Foundation

/// One entry of the transport row's Length menu: its title and its length in quarter-note beats.
struct LengthEntry: Equatable, Identifiable {
  let id: String
  let title: String
  let beats: Double
}

/// The Length menu (strip editing design D8): its entries, its title, which entries are checked, the Custom
/// field's value and the command a pick sends. Pure; the menu reads the snapshot and sends the result.
enum LengthOptions {
  /// The note values, the same in every meter (a quarter is one beat).
  static let noteEntries: [LengthEntry] = [
    LengthEntry(id: "1/16", title: "1/16", beats: 0.25),
    LengthEntry(id: "1/8", title: "1/8", beats: 0.5),
    LengthEntry(id: "dotted 1/8", title: "dotted 1/8", beats: 0.75),
    LengthEntry(id: "1/4", title: "1/4", beats: 1),
    LengthEntry(id: "dotted 1/4", title: "dotted 1/4", beats: 1.5),
    LengthEntry(id: "1/2", title: "1/2", beats: 2),
    LengthEntry(id: "dotted 1/2", title: "dotted 1/2", beats: 3),
    LengthEntry(id: "whole", title: "whole", beats: 4),
  ]

  /// The lengths the engine keeps (`kMinLengthBeats`...`kMaxLengthBeats`) and the strip's grid.
  static let range = 0.25...64.0
  static let step = 0.25

  /// 1, 2 and 4 bars of `meter` (`RulerMarks.barBeats`: 4/4 → 4, 3/4 and 6/8 → 3, 7/8 → 3.5; no meter is
  /// 4/4), leaving out any longer than 64 beats.
  static func barEntries(meter: MeterJSON?) -> [LengthEntry] {
    let bar = RulerMarks.barBeats(meter)
    return [(1, "1 bar"), (2, "2 bars"), (4, "4 bars")]
      .map { LengthEntry(id: $0.1, title: $0.1, beats: Double($0.0) * bar) }
      .filter { range.contains($0.beats) }
  }

  /// The menu's groups, a divider between them: the note values, then the bars.
  static func groups(meter: MeterJSON?) -> [[LengthEntry]] {
    [noteEntries, barEntries(meter: meter)]
  }

  /// The length a chord added without one gets: `newChordBeats`, else one bar of `meter`.
  static func newChordBeats(_ newChordBeats: Double?, meter: MeterJSON?) -> Double {
    newChordBeats ?? RulerMarks.barBeats(meter)
  }

  /// The name of a length: a bar entry's when one matches (4 beats in 4/4 is "1 bar", not "whole"), else a note
  /// value's, else the beats themselves ("2.75 beats", "5 beats").
  static func name(_ beats: Double, meter: MeterJSON?) -> String {
    if let entry = (barEntries(meter: meter) + noteEntries).first(where: { $0.beats == beats }) { return entry.title }
    return "\(beatsText(beats)) beats"
  }

  /// `beats` as text without a trailing ".0": "5", "2.75".
  static func beatsText(_ beats: Double) -> String {
    beats == beats.rounded() && abs(beats) < 1e9 ? String(Int(beats)) : String(format: "%g", beats)
  }

  /// The menu's title (D8): with blocks selected, the length they share by `name`, or "Mixed"; with none, "New: "
  /// and the new-chord length's name.
  static func title(selectionLengths: [Double], newChordBeats: Double?, meter: MeterJSON?) -> String {
    guard let first = selectionLengths.first else {
      return "New: " + name(Self.newChordBeats(newChordBeats, meter: meter), meter: meter)
    }
    return selectionLengths.allSatisfy { $0 == first } ? name(first, meter: meter) : "Mixed"
  }

  /// Whether `entry` wears the checkmark: every selected block has its length; with none selected, the chosen
  /// new-chord length is its length (while the default, one bar, is in use no entry is: "Default" is).
  static func isChecked(_ entry: LengthEntry, selectionLengths: [Double], newChordBeats: Double?) -> Bool {
    if selectionLengths.isEmpty { return newChordBeats == entry.beats }
    return selectionLengths.allSatisfy { $0 == entry.beats }
  }

  /// The lengths of the selected blocks, in strip order.
  static func lengths(of selection: [Int], in events: [EventJSON]) -> [Double] {
    events.filter { selection.contains($0.id) }.map(\.lengthBeats)
  }

  /// The Custom field's value (D8): `text` read as a number, snapped to the quarter-beat grid (halves up) and
  /// clamped to 0.25…64; nil when it is not a finite number.
  static func custom(_ text: String) -> Double? {
    guard let value = Double(text.trimmingCharacters(in: .whitespaces)), value.isFinite else { return nil }
    return min(max((value / step).rounded() * step, range.lowerBound), range.upperBound)
  }

  /// What a pick sends (D8): `setLengths` for the selected blocks; with none, `setNewChordLength` (`beats` nil
  /// is "Default (one bar)": JSON null).
  static func command(beats: Double?, selection: [Int]) -> [String: Any] {
    if !selection.isEmpty, let beats { return ["cmd": "setLengths", "ids": selection, "lengthBeats": beats] }
    return ["cmd": "setNewChordLength", "lengthBeats": beats.map { $0 as Any } ?? NSNull()]
  }
}
