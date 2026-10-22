import SwiftUI

/// The 8-colour section palette that `colourIndex` indexes (P2-D6; the engine's `kNumSectionColours`).
enum SectionStyle {
  static let colors: [Color] = [
    Color(red: 0.541, green: 0.561, blue: 0.227),   // #8A8F3A olive
    Color(red: 0.180, green: 0.494, blue: 0.651),   // #2E7EA6 blue
    Color(red: 0.420, green: 0.439, blue: 0.537),   // #6B7089 grey
    Color(red: 0.651, green: 0.302, blue: 0.541),   // #A64D8A magenta
    Color(red: 0.239, green: 0.702, blue: 0.541),   // #3DB38A teal
    Color(red: 0.878, green: 0.659, blue: 0.227),   // #E0A83A amber
    Color(red: 0.878, green: 0.353, blue: 0.353),   // #E05A5A red
    Color(red: 0.482, green: 0.361, blue: 0.788),   // #7B5CC9 violet
  ]
  static let names = ["Olive", "Blue", "Grey", "Magenta", "Teal", "Amber", "Red", "Violet"]

  static func color(_ index: Int) -> Color {
    colors[((index % colors.count) + colors.count) % colors.count]
  }
}
