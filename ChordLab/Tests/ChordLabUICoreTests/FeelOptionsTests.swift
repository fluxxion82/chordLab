import XCTest
@testable import ChordLabUICore

final class FeelOptionsTests: XCTestCase {
  private func feel(_ r: String = "block", _ s: String = "off", _ sp: String = "medium", _ h: String = "off") -> FeelJSON {
    FeelJSON(rhythm: r, strum: s, strumSpeed: sp, humanise: h)
  }

  // MARK: - option lists

  func testOptionIdsAreTheEnginesStrings() {
    XCTAssertEqual(FeelOptions.rhythms(num: 4, den: 4).map(\.id),
                   ["block", "pulse", "subdivided", "offbeats", "lowThenUpper", "arpUp", "arpUpDown"])
    XCTAssertEqual(FeelOptions.strums.map(\.id), ["off", "down", "up", "downUp"])
    XCTAssertEqual(FeelOptions.strumSpeeds.map(\.id), ["tight", "medium", "loose"])
    XCTAssertEqual(FeelOptions.humanises.map(\.id), ["off", "light", "medium", "heavy"])
  }

  func testMeterMenu() {
    XCTAssertEqual(FeelOptions.meters.map { "\($0.num)/\($0.den)" },
                   ["2/4", "3/4", "4/4", "5/4", "6/8", "7/8", "9/8", "12/8"])
    XCTAssertEqual(FeelOptions.meters.map(\.title), ["2/4", "3/4", "4/4", "5/4", "6/8", "7/8", "9/8", "12/8"])
  }

  // MARK: - titles per meter

  private func titles(_ num: Int, _ den: Int) -> [String] { FeelOptions.rhythms(num: num, den: den).map(\.title) }

  func testRhythmTitlesInFourFour() {
    XCTAssertEqual(titles(4, 4), ["Block", "Pulse", "Subdivided (eighths)", "Offbeats", "Low note, then chord",
                                  "Arpeggio up", "Arpeggio up and down"])
  }

  func testSubdividedUnitFollowsTheMeter() {
    func sub(_ n: Int, _ d: Int) -> String { titles(n, d)[2] }
    XCTAssertEqual(sub(4, 4), "Subdivided (eighths)")
    XCTAssertEqual(sub(3, 4), "Subdivided (eighths)")
    XCTAssertEqual(sub(2, 4), "Subdivided (eighths)")
    XCTAssertEqual(sub(5, 4), "Subdivided (eighths)")
    XCTAssertEqual(sub(6, 8), "Subdivided (eighths)", "compound: 480 ticks")
    XCTAssertEqual(sub(12, 8), "Subdivided (eighths)")
    XCTAssertEqual(sub(9, 8), "Subdivided (eighths)")
    XCTAssertEqual(sub(7, 8), "Subdivided (sixteenths)", "simple /8: 240 ticks")
    XCTAssertEqual(sub(3, 8), "Subdivided (sixteenths)", "3/8 is simple (num < 6)")
    XCTAssertEqual(sub(4, 8), "Subdivided (sixteenths)")
    XCTAssertEqual(sub(4, 2), "Subdivided (quarters)", "den 2: pulse 1920, subdivision 960")
  }

  func testSubdivisionTicksMatchTheEngineGrid() {
    XCTAssertEqual(FeelOptions.subdivisionTicks(num: 4, den: 4), 480)
    XCTAssertEqual(FeelOptions.subdivisionTicks(num: 6, den: 8), 480)
    XCTAssertEqual(FeelOptions.subdivisionTicks(num: 7, den: 8), 240)
    XCTAssertEqual(FeelOptions.subdivisionTicks(num: 3, den: 2), 960)
  }

  func testOtherListsHaveFixedTitles() {
    XCTAssertEqual(FeelOptions.strums.map(\.title), ["Off", "Down", "Up", "Down and up"])
    XCTAssertEqual(FeelOptions.strumSpeeds.map(\.title), ["Tight", "Medium", "Loose"])
    XCTAssertEqual(FeelOptions.humanises.map(\.title), ["Off", "Light", "Medium", "Heavy"])
  }

  // MARK: - commands (the shapes json_api.cpp accepts)

  func testSetFeelCommandIsTheEnginesMessage() {
    let c = FeelOptions.command(feel("lowThenUpper", "downUp", "loose", "heavy"))
    XCTAssertEqual(c.count, 5)
    XCTAssertEqual(c["cmd"] as? String, "setFeel")
    XCTAssertEqual(c["rhythm"] as? String, "lowThenUpper")
    XCTAssertEqual(c["strum"] as? String, "downUp")
    XCTAssertEqual(c["strumSpeed"] as? String, "loose")
    XCTAssertEqual(c["humanise"] as? String, "heavy")
  }

  func testEveryOptionBuildsACommandWithTheOptionAsItsValue() {
    for o in FeelOptions.rhythms(num: 4, den: 4) {
      XCTAssertEqual(FeelOptions.command(feel(o.id))["rhythm"] as? String, o.id)
    }
    for o in FeelOptions.strums { XCTAssertEqual(FeelOptions.command(feel("block", o.id))["strum"] as? String, o.id) }
    for o in FeelOptions.strumSpeeds { XCTAssertEqual(FeelOptions.command(feel("block", "off", o.id))["strumSpeed"] as? String, o.id) }
    for o in FeelOptions.humanises { XCTAssertEqual(FeelOptions.command(feel("block", "off", "medium", o.id))["humanise"] as? String, o.id) }
  }

  func testSetMeterCommandCarriesIntegers() throws {
    let c = FeelOptions.meterCommand(num: 7, den: 8)
    XCTAssertEqual(c.count, 3)
    XCTAssertEqual(c["cmd"] as? String, "setMeter")
    XCTAssertEqual(c["num"] as? Int, 7)
    XCTAssertEqual(c["den"] as? Int, 8)
    let data = try JSONSerialization.data(withJSONObject: c, options: [.sortedKeys])
    XCTAssertEqual(String(data: data, encoding: .utf8), #"{"cmd":"setMeter","den":8,"num":7}"#)
  }

  func testEveryMenuMeterIsValidForTheEngine() {
    for m in FeelOptions.meters {
      XCTAssertTrue((1...16).contains(m.num) && [2, 4, 8].contains(m.den), m.title)
    }
  }

  // MARK: - isDefault

  func testIsDefaultMatchesTheEnginesDefaultFeel() {
    XCTAssertTrue(FeelOptions.isDefault(feel()))
    XCTAssertFalse(FeelOptions.isDefault(feel("pulse")))
    XCTAssertFalse(FeelOptions.isDefault(feel("block", "down")))
    XCTAssertFalse(FeelOptions.isDefault(feel("block", "off", "medium", "light")))
  }

  func testSpeedAloneStillCountsAsDefaultLikeTheEngine() {
    XCTAssertTrue(FeelOptions.isDefault(feel("block", "off", "tight")))
    XCTAssertTrue(FeelOptions.isDefault(feel("block", "off", "loose")))
    XCTAssertFalse(FeelOptions.isDefault(feel("block", "up", "loose")))
  }

  func testNilFeelIsDefault() {
    XCTAssertTrue(FeelOptions.isDefault(nil))
  }
}
