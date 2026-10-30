import Foundation

/// Whether a chord has anything for "Auto voicing" (the block menu) and the popover's "Auto" to reset.
/// A transpose moves the stored octave of chords that are not manual, so a non-manual chord can sit off
/// inversion 0 / octave 0 too. Fields an older snapshot lacks count as false / 0.
enum VoicingReset {
  static func isAvailable(manual: Bool?, inversion: Int?, octave: Int?) -> Bool {
    (manual ?? false) || (inversion ?? 0) != 0 || (octave ?? 0) != 0
  }
}
