import AppKit
import SwiftUI

/// "#RRGGBB" of a SwiftUI colour in sRGB, for pinning the palettes' documented hex values.
func hex(_ color: Color) -> String? {
  guard let srgb = NSColor(color).usingColorSpace(.sRGB) else { return nil }
  func byte(_ component: CGFloat) -> Int { Int((component * 255).rounded()) }
  return String(format: "#%02X%02X%02X", byte(srgb.redComponent), byte(srgb.greenComponent), byte(srgb.blueComponent))
}
