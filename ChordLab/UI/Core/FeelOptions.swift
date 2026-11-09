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

  /// The rhythm menu for a meter, in the engine's order. The arpeggio's own grid is the Rate row.
  static func rhythms(num: Int, den: Int) -> [FeelOption] {
    [
      FeelOption(id: "block", title: "Block"),
      FeelOption(id: "pulse", title: "Pulse"),
      FeelOption(id: "subdivided", title: "Subdivided (\(subdivisionName(num: num, den: den)))"),
      FeelOption(id: "offbeats", title: "Offbeats"),
      FeelOption(id: "lowThenUpper", title: "Low note, then chord"),
      FeelOption(id: "arpeggio", title: "Arpeggio"),
    ]
  }

  /// The arpeggio patterns (arpeggio design D2), in the engine's order, grouped as the menu shows them:
  /// directions, the converge family, Live's pinky and thumb styles, Alberti / Root first / Chord, Random.
  /// Aliases for the help text: Thumb up is a pedal bass, Converge is outside-in.
  static let arpPatterns: [[FeelOption]] = [
    [FeelOption(id: "up", title: "Up"), FeelOption(id: "down", title: "Down"),
     FeelOption(id: "upDown", title: "Up-down"), FeelOption(id: "downUp", title: "Down-up"),
     FeelOption(id: "upAndDown", title: "Up & down"), FeelOption(id: "downAndUp", title: "Down & up")],
    [FeelOption(id: "converge", title: "Converge"), FeelOption(id: "diverge", title: "Diverge"),
     FeelOption(id: "conAndDiverge", title: "Con & diverge")],
    [FeelOption(id: "pinkyUp", title: "Pinky up"), FeelOption(id: "pinkyUpDown", title: "Pinky up-down"),
     FeelOption(id: "thumbUp", title: "Thumb up"), FeelOption(id: "thumbUpDown", title: "Thumb up-down")],
    [FeelOption(id: "alberti", title: "Alberti"), FeelOption(id: "rootFirst", title: "Root first"),
     FeelOption(id: "chord", title: "Chord")],
    [FeelOption(id: "random", title: "Random")],
  ]

  static let arpRanges: [FeelOption] = [FeelOption(id: "one", title: "1 oct"), FeelOption(id: "two", title: "2 oct")]

  static let defaultArpPattern = "up"
  static let defaultArpRate = "subdivision"
  static let defaultArpRange = "one"

  /// Ticks in one pulse of the meter, the engine's `pulseTicks`: 1440 when compound, else 3840 / den.
  static func pulseTicks(num: Int, den: Int) -> Int {
    guard den > 0 else { return 0 }
    return RulerMarks.isCompound(num: num, den: den) ? 1440 : 3840 / den
  }

  /// The note value of a grid step, by its length in ticks (960 a beat).
  static func noteValueName(ticks: Int) -> String {
    switch ticks {
    case 1920: return "Half notes"
    case 1440: return "Dotted quarters"
    case 960: return "Quarters"
    case 480: return "Eighths"
    case 240: return "Sixteenths"
    case 120: return "Thirty-seconds"
    default: return "\(ticks) ticks"
    }
  }

  /// The arpeggio rate menu for a meter (D3): the pulse, the subdivision and half of it, each named for its
  /// note value in that meter (4/4: quarters, eighths, sixteenths; 6/8: dotted quarters, eighths, sixteenths;
  /// 7/8: eighths, sixteenths, thirty-seconds).
  static func arpRates(num: Int, den: Int) -> [FeelOption] {
    let sub = subdivisionTicks(num: num, den: den)
    return [
      FeelOption(id: "pulse", title: noteValueName(ticks: pulseTicks(num: num, den: den))),
      FeelOption(id: "subdivision", title: noteValueName(ticks: sub)),
      FeelOption(id: "half", title: noteValueName(ticks: sub / 2)),
    ]
  }

  /// Whether the rhythm is the arpeggio (the Pattern and Rate rows show, Strum and Speed do not).
  static func isArpeggio(_ feel: FeelJSON?) -> Bool { (feel ?? defaultFeel).rhythm == "arpeggio" }

  /// The arpeggio fields, the engine's defaults when the snapshot lacks them (a plug-in built before them).
  static func arpPattern(_ feel: FeelJSON?) -> String { feel?.arpPattern ?? defaultArpPattern }
  static func arpRate(_ feel: FeelJSON?) -> String { feel?.arpRate ?? defaultArpRate }
  static func arpRange(_ feel: FeelJSON?) -> String { feel?.arpRange ?? defaultArpRange }

  /// `setFeel`: all seven strings, as the engine requires (the arpeggio fields default when the feel lacks them).
  static func command(_ feel: FeelJSON) -> [String: Any] {
    ["cmd": "setFeel", "rhythm": feel.rhythm, "strum": feel.strum,
     "strumSpeed": feel.strumSpeed, "humanise": feel.humanise,
     "arpPattern": arpPattern(feel), "arpRate": arpRate(feel), "arpRange": arpRange(feel)]
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

  /// The engine's default feel (`Feel{}`): block, strum off at medium speed, humanise off.
  static let defaultFeel = FeelJSON(rhythm: "block", strum: "off", strumSpeed: "medium", humanise: "off",
                                    arpPattern: "up", arpRate: "subdivision", arpRange: "one")

  /// `feel` (the default when the snapshot has none) with the given fields replaced: what one control of
  /// the popover sends, all seven fields (the arpeggio ones defaulted) as `setFeel` requires.
  static func changed(_ feel: FeelJSON?, rhythm: String? = nil, strum: String? = nil, strumSpeed: String? = nil,
                      humanise: String? = nil, arpPattern: String? = nil, arpRate: String? = nil,
                      arpRange: String? = nil) -> FeelJSON {
    let base = feel ?? defaultFeel
    return FeelJSON(rhythm: rhythm ?? base.rhythm, strum: strum ?? base.strum,
                    strumSpeed: strumSpeed ?? base.strumSpeed, humanise: humanise ?? base.humanise,
                    arpPattern: arpPattern ?? Self.arpPattern(base), arpRate: arpRate ?? Self.arpRate(base),
                    arpRange: arpRange ?? Self.arpRange(base))
  }

  /// Whether the strum is heard: the Pattern row replaces the Strum row under the arpeggio; the engine
  /// still ignores a strum there (feel plan D8).
  static func strumApplies(_ feel: FeelJSON?) -> Bool { !isArpeggio(feel) }

  /// Whether the strum speed is heard: only while a strum plays (not off, not under an arpeggio).
  static func speedApplies(_ feel: FeelJSON?) -> Bool {
    strumApplies(feel) && (feel ?? defaultFeel).strum != "off"
  }

  /// The snapshot's meter as a menu entry; 4/4 when the snapshot has none (an older plug-in build).
  static func currentMeter(_ meter: MeterJSON?) -> MeterOption {
    meter.map { MeterOption(num: $0.num, den: $0.den) } ?? MeterOption(num: 4, den: 4)
  }

  /// The meter menu: the fixed list, plus the current meter at the end when the list lacks it (a valid
  /// meter the engine accepts but the menu does not offer, such as 3/8), so the menu always shows it.
  static func meterMenu(current: MeterJSON?) -> [MeterOption] {
    let option = currentMeter(current)
    return meters.contains(option) ? meters : meters + [option]
  }
}
