import Foundation

/// One entry of a feel menu: `id` is the engine's string, `title` what the UI shows.
struct FeelOption: Equatable, Identifiable {
  let id: String
  let title: String
}

/// One entry of the meter menu.
struct MeterOption: Equatable, Identifiable {
  let num: Int
  let den: Int
  var id: String { "\(num)/\(den)" }
  var title: String { id }
}

/// The feel popover's menus and messages (plan D2, D4, D5): the option lists, the rhythm titles that
/// depend on the meter, the `setFeel` / `setMeter` commands in the shape engine/src/json_api.cpp accepts,
/// and whether a feel is the default. Pure; the engine does all the music.
enum FeelOptions {
  static let strums: [FeelOption] = [
    FeelOption(id: "off", title: "Off"),
    FeelOption(id: "down", title: "Down"),
    FeelOption(id: "up", title: "Up"),
    FeelOption(id: "downUp", title: "Down and up"),
  ]

  static let strumSpeeds: [FeelOption] = [
    FeelOption(id: "tight", title: "Tight"),
    FeelOption(id: "medium", title: "Medium"),
    FeelOption(id: "loose", title: "Loose"),
  ]

  static let humanises: [FeelOption] = [
    FeelOption(id: "off", title: "Off"),
    FeelOption(id: "light", title: "Light"),
    FeelOption(id: "medium", title: "Medium"),
    FeelOption(id: "heavy", title: "Heavy"),
  ]

  /// The meters the engine's menu offers (D2); every one is valid for `setMeter` (num 1..16, den 2, 4, 8).
  static let meters: [MeterOption] = [
    MeterOption(num: 2, den: 4), MeterOption(num: 3, den: 4), MeterOption(num: 4, den: 4),
    MeterOption(num: 5, den: 4), MeterOption(num: 6, den: 8), MeterOption(num: 7, den: 8),
    MeterOption(num: 9, den: 8), MeterOption(num: 12, den: 8),
  ]

  /// Ticks in one subdivision of the meter, the engine's `subdivisionTicks` (meter.cpp, D4): 480 when
  /// compound (den 8, num a multiple of 3, num >= 6), else half the pulse, 3840 / den / 2 (den 2: 960,
  /// den 4: 480, simple den 8: 240). 0 for a den that is not positive.
  static func subdivisionTicks(num: Int, den: Int) -> Int {
    guard den > 0 else { return 0 }
    if RulerMarks.isCompound(num: num, den: den) { return 480 }
    return 3840 / den / 2
  }

  /// The subdivision's name for the "Subdivided" rhythm: eighths at 480 ticks (den 4, compound /8),
  /// sixteenths at 240 (simple /8 such as 7/8 and 3/8), quarters at 960 (den 2, a half-note pulse split
  /// in two; the menu has no such meter, but the engine accepts it).
  static func subdivisionName(num: Int, den: Int) -> String {
    switch subdivisionTicks(num: num, den: den) {
    case 240: return "sixteenths"
    case 960: return "quarters"
    default: return "eighths"
    }
  }

  /// The rhythm menu for a meter, in the engine's order. Arpeggios also run on the subdivision.
  static func rhythms(num: Int, den: Int) -> [FeelOption] {
    [
      FeelOption(id: "block", title: "Block"),
      FeelOption(id: "pulse", title: "Pulse"),
      FeelOption(id: "subdivided", title: "Subdivided (\(subdivisionName(num: num, den: den)))"),
      FeelOption(id: "offbeats", title: "Offbeats"),
      FeelOption(id: "lowThenUpper", title: "Low note, then chord"),
      FeelOption(id: "arpUp", title: "Arpeggio up"),
      FeelOption(id: "arpUpDown", title: "Arpeggio up and down"),
    ]
  }

  /// `setFeel`: all four strings, as the engine requires.
  static func command(_ feel: FeelJSON) -> [String: Any] {
    ["cmd": "setFeel", "rhythm": feel.rhythm, "strum": feel.strum,
     "strumSpeed": feel.strumSpeed, "humanise": feel.humanise]
  }

  /// `setMeter`: `num` and `den` are JSON integers.
  static func meterCommand(num: Int, den: Int) -> [String: Any] {
    ["cmd": "setMeter", "num": num, "den": den]
  }

  /// Whether the feel changes nothing the engine would perform: block, strum off, humanise off, which is
  /// the engine's `feelPerforms == false`. The speed alone does not count (with the strum off it has no
  /// effect, and the engine default is medium), so the Feel button's dot stays off for it. A missing feel
  /// (an older plug-in build) is the default.
  static func isDefault(_ feel: FeelJSON?) -> Bool {
    guard let feel else { return true }
    return feel.rhythm == "block" && feel.strum == "off" && feel.humanise == "off"
  }
}
