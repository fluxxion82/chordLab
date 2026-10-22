import SwiftUI

/// Colours of the harmonic functions (P2-2.1): the dot on cells, blocks and suggestion chips, and the
/// tension bars. Tonic teal-green, Predominant amber, Dominant red, Other (and an unknown or missing
/// code) grey.
enum FunctionStyle {
  static let tonic = Color(red: 0.239, green: 0.702, blue: 0.541)        // #3DB38A
  static let predominant = Color(red: 0.878, green: 0.659, blue: 0.227)  // #E0A83A
  static let dominant = Color(red: 0.878, green: 0.353, blue: 0.353)     // #E05A5A
  static let other = Color(red: 0.420, green: 0.439, blue: 0.537)        // #6B7089

  static func color(_ code: String?) -> Color {
    switch code {
    case "T": return tonic
    case "PD": return predominant
    case "D": return dominant
    default: return other
    }
  }
}
