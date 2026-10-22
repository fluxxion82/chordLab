import Foundation

/// How palette cells are labelled; UI-only, remembered across sessions in UserDefaults.
/// Every view that reads the mode declares
/// `@AppStorage(LabelMode.storageKey) private var labelMode = LabelMode.defaultMode`, so that they all
/// show the same thing before the user has ever chosen.
enum LabelMode: String, CaseIterable, Identifiable {
  case names, numerals, both
  static let storageKey = "chordlab.labelMode"
  /// What a fresh install shows: the symbol over the numeral and function ("C" / "I · T").
  static let defaultMode: LabelMode = .both
  var id: String { rawValue }
  var title: String {
    switch self {
    case .names: return "Names"
    case .numerals: return "Numerals"
    case .both: return "Both"
    }
  }
}
