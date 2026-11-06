import Foundation

/// Where the Diatonic / Applied switch is shown (design D15 and the home row, P2-M3): the one rule
/// `PaletteView` and `HomeRowView` both consult, so the switch is never shown twice and, while the palette
/// has an applied view, never missing (but for the compact key hint, below).
enum PaletteSwitchPlacement: Equatable {
  /// In the switch row above the grid (`PaletteView`).
  case paletteRow
  /// At the trailing end of the home row (`HomeRowView`).
  case homeRow
  /// Not shown.
  case none

  /// The placement for the current state. Without an applied view (`hasApplied` false: an older plug-in
  /// build, or no snapshot) there is no switch. While the listener is active (`heardActive`) the palette's
  /// switch row always shows, for the Heard group, and holds the switch. Otherwise the home row, when it has
  /// cells (`homeRowEmpty` false), holds it, whatever `switchRowAllowed` says; with no home row the switch
  /// takes the palette row, unless `switchRowAllowed` is false (ContentView clears it while the key
  /// transpose hint shows in a short window), when it is not shown at all.
  static func resolve(heardActive: Bool, hasApplied: Bool, homeRowEmpty: Bool,
                      switchRowAllowed: Bool) -> PaletteSwitchPlacement {
    guard hasApplied else { return .none }
    if heardActive { return .paletteRow }
    if !homeRowEmpty { return .homeRow }
    return switchRowAllowed ? .paletteRow : .none
  }
}
