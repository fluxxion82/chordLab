import Foundation
import XCTest
@testable import ChordLabUICore

/// Decoding the engine's snapshot JSON (UI/Core/Snapshot.swift): inline Phase 1 / Phase 2 / homeTriads cases,
/// the small message payloads, and every snapshot fixture the engine tests write to
/// engine/tests/fixtures (skipped when that directory is absent).
final class SnapshotTests: XCTestCase {
  // MARK: - Inline JSON

  /// A Phase 1 snapshot: none of the Phase 2 fields.
  static let phase1 = """
    {"schema":1,"projectKey":{"tonic":{"letter":"C","alter":0},"mode":"Ionian"},"browsedKey":{"tonic":{"letter":"C","alter":0},"mode":"Ionian"},
     "projectKeyName":"C Ionian","browsedKeyName":"C Ionian","progressionLoop":true,"followHost":true,"endBeat":4.0,
     "events":[{"id":1,"root":0,"type":"maj","symbol":"C","roman":"I","startBeat":0.0,"lengthBeats":4.0,"velocity":100,"voicing":[60,64,67]}],
     "palette":{"tiers":1,"cells":[{"degree":0,"tier":0,"root":0,"type":"5","symbol":"C5","roman":"I","family":"Other","filled":true,"voicing":[48,55]}]}}
    """

  /// A Phase 2 snapshot without `homeTriads` (before P2-M3).
  static let phase2 = """
    {"schema":1,"projectKey":{"tonic":{"letter":"C","alter":0},"mode":"Ionian"},"browsedKey":{"tonic":{"letter":"C","alter":0},"mode":"Ionian"},
     "projectKeyName":"C Ionian","browsedKeyName":"C Ionian","progressionLoop":true,"followHost":true,"autoVoiceLead":true,"endBeat":8.0,
     "selectedEventId":null,
     "events":[{"id":1,"root":7,"type":"7","symbol":"G7","roman":"V7","family":"Major","function":"D","tension":30,"startBeat":0.0,"lengthBeats":4.0,
                "velocity":100,"inversion":0,"octave":0,"manualVoicing":false,"voicing":[55,59,62,65],"outOfKey":[],"why":null},
               {"id":2,"root":0,"type":"maj","symbol":"C","roman":"I","family":"Major","function":"T","tension":3,"startBeat":4.0,"lengthBeats":4.0,
                "velocity":100,"inversion":1,"octave":0,"manualVoicing":true,"voicing":[64,67,72],"outOfKey":[],
                "why":{"commonTones":1,"commonPcs":[7],"commonNames":["G"],"rootMotion":"down a fifth","voiceLeadingCost":4,
                       "borrowedFrom":null,"borrowedFromName":null,"text":"G7 → C: down a fifth"}}],
     "sections":[{"id":1,"name":"Verse","colourIndex":2,"firstEventIndex":0}],
     "suggestions":[{"root":5,"type":"maj","symbol":"F","roman":"IV","function":"PD","score":2.35,"reasons":["commonTones"],"reasonTexts":["1 common tone (C)"]}],
     "palette":{"tiers":1,"cells":[{"degree":0,"tier":0,"root":0,"type":"5","symbol":"C5","roman":"I","family":"Other","function":"T","tension":0,"filled":true,"voicing":[48,55],"outOfKey":[]}]}}
    """

  /// Seven `homeTriads` entries as the engine writes them while another key is browsed.
  static let homeEntries: String = {
    let symbols = ["C", "Dm", "Em", "F", "G", "Am", "Bmb5"]
    let roots = [0, 2, 4, 5, 7, 9, 11]
    let types = ["maj", "min", "min", "maj", "maj", "min", "dim"]
    return (0..<7).map { i in
      "{\"id\":\"home-\(i)\",\"degree\":\(i),\"tier\":1,\"root\":\(roots[i]),\"type\":\"\(types[i])\",\"symbol\":\"\(symbols[i])\"," +
        "\"roman\":\"I\",\"family\":\"Major\",\"function\":\"T\",\"tension\":3,\"filled\":true,\"voicing\":[60,64,67],\"outOfKey\":[]}"
    }.joined(separator: ",")
  }()

  /// `json` with a `homeTriads` array of `entries` inserted before the palette.
  static func withHome(_ json: String, _ entries: String) -> String {
    json.replacingOccurrences(of: "\"palette\":", with: "\"homeTriads\":[\(entries)],\"palette\":")
  }

  static func decode<T: Decodable>(_ type: T.Type = Snapshot.self, _ json: String) throws -> T {
    try JSONDecoder().decode(T.self, from: Data(json.utf8))
  }

  // MARK: - Phase 1

  func testPhase1SnapshotDecodes() throws {
    let snapshot = try Self.decode(Snapshot.self, Self.phase1)
    XCTAssertEqual(snapshot.schema, 1)
    XCTAssertEqual(snapshot.events.map(\.symbol), ["C"])
    XCTAssertEqual(snapshot.palette.cells.map(\.symbol), ["C5"])
  }

  func testPhase1SnapshotHasNilPhase2Fields() throws {
    let snapshot = try Self.decode(Snapshot.self, Self.phase1)
    XCTAssertNil(snapshot.events[0].function)
    XCTAssertNil(snapshot.events[0].why)
    XCTAssertNil(snapshot.suggestions)
    XCTAssertNil(snapshot.sections)
    XCTAssertNil(snapshot.selectedEventId)
    XCTAssertNil(snapshot.autoVoiceLead)
  }

  func testPhase1SnapshotHasNoHomeTriadsAndAnEmptyHomeRow() throws {
    let snapshot = try Self.decode(Snapshot.self, Self.phase1)
    XCTAssertNil(snapshot.homeTriads)
    XCTAssertTrue(snapshot.homeRow.isEmpty)
  }

  // MARK: - Phase 2

  func testPhase2SnapshotDecodes() throws {
    XCTAssertNoThrow(try Self.decode(Snapshot.self, Self.phase2))
  }

  func testNullSelectionIsNilAndAutoVoiceLeadIsRead() throws {
    let snapshot = try Self.decode(Snapshot.self, Self.phase2)
    XCTAssertNil(snapshot.selectedEventId)
    XCTAssertEqual(snapshot.autoVoiceLead, true)
  }

  func testWhyIsNullOnTheFirstEventAndDataOnTheSecond() throws {
    let snapshot = try Self.decode(Snapshot.self, Self.phase2)
    XCTAssertNil(snapshot.events[0].why)
    XCTAssertEqual(snapshot.events[1].why?.commonNames, ["G"])
  }

  func testVoicingFields() throws {
    let snapshot = try Self.decode(Snapshot.self, Self.phase2)
    XCTAssertEqual(snapshot.events[1].manualVoicing, true)
    XCTAssertEqual(snapshot.events[1].inversion, 1)
  }

  func testSections() throws {
    let snapshot = try Self.decode(Snapshot.self, Self.phase2)
    XCTAssertEqual(snapshot.sections?.first?.name, "Verse")
    XCTAssertEqual(snapshot.sections?.first?.colourIndex, 2)
  }

  func testSuggestions() throws {
    let snapshot = try Self.decode(Snapshot.self, Self.phase2)
    XCTAssertEqual(snapshot.suggestions?.first?.symbol, "F")
    XCTAssertEqual(snapshot.suggestions?.first?.reasonTexts, ["1 common tone (C)"])
    XCTAssertEqual(snapshot.suggestions?.first?.id, "5/maj", "a suggestion's id is its chord")
  }

  func testCellFunctionAndTension() throws {
    let snapshot = try Self.decode(Snapshot.self, Self.phase2)
    XCTAssertEqual(snapshot.palette.cells[0].function, "T")
    XCTAssertEqual(snapshot.palette.cells[0].tension, 0)
  }

  // MARK: - homeTriads (P2-M3)

  func testSnapshotWithoutHomeTriadsHasAnEmptyHomeRow() throws {
    let snapshot = try Self.decode(Snapshot.self, Self.phase2)
    XCTAssertNil(snapshot.homeTriads)
    XCTAssertTrue(snapshot.homeRow.isEmpty)
  }

  func testPaletteCellWithoutEngineIdIsIdentifiedByTierAndDegree() throws {
    let snapshot = try Self.decode(Snapshot.self, Self.phase2)
    XCTAssertNil(snapshot.palette.cells[0].engineId)
    XCTAssertEqual(snapshot.palette.cells[0].id, "0/0")
  }

  func testEmptyHomeTriadsAtHomeDecodeToAnEmptyRow() throws {
    let snapshot = try Self.decode(Snapshot.self, Self.withHome(Self.phase2, ""))
    XCTAssertEqual(snapshot.homeTriads?.isEmpty, true)
    XCTAssertTrue(snapshot.homeRow.isEmpty)
  }

  func testSevenHomeTriadsDecode() throws {
    let away = try Self.decode(Snapshot.self, Self.withHome(Self.phase2, Self.homeEntries))
    XCTAssertEqual(away.homeRow.count, 7)
  }

  func testHomeCellsTakeTheEnginesIds() throws {
    let away = try Self.decode(Snapshot.self, Self.withHome(Self.phase2, Self.homeEntries))
    XCTAssertEqual(away.homeRow.map(\.id), (0..<7).map { "home-\($0)" })
  }

  func testHomeCellsCarryTheChordInDegreeOrder() throws {
    let away = try Self.decode(Snapshot.self, Self.withHome(Self.phase2, Self.homeEntries))
    XCTAssertEqual(away.homeRow.map(\.symbol), ["C", "Dm", "Em", "F", "G", "Am", "Bmb5"])
    XCTAssertEqual(away.homeRow.last?.type, "dim")
  }

  func testNoHomeIdIsAPaletteCellId() throws {
    let away = try Self.decode(Snapshot.self, Self.withHome(Self.phase2, Self.homeEntries))
    XCTAssertTrue(Set(away.homeRow.map(\.id)).isDisjoint(with: away.palette.cells.map(\.id)))
  }

  func testHomeTriadsLeaveTheRestOfTheSnapshotUnaffected() throws {
    let plain = try Self.decode(Snapshot.self, Self.phase2)
    let away = try Self.decode(Snapshot.self, Self.withHome(Self.phase2, Self.homeEntries))
    XCTAssertEqual(away.palette, plain.palette)
    XCTAssertEqual(away.events, plain.events)
  }

  // MARK: - Tonic table

  func testTonicTableNeedsModesAndTonics() throws {
    XCTAssertFalse(try Self.decode(Snapshot.self, Self.phase1).hasTonicTable)
    let row = (0..<12).map { "{\"letter\":\"C\",\"alter\":0,\"text\":\"C\($0)\"}" }.joined(separator: ",")
    let json = Self.phase1.replacingOccurrences(
      of: "\"events\":", with: "\"modes\":[\"Ionian\",\"Dorian\"],\"tonics\":{\"Ionian\":[\(row)],\"Dorian\":[]},\"events\":")
    let snapshot = try Self.decode(Snapshot.self, json)
    XCTAssertTrue(snapshot.hasTonicTable)
    XCTAssertEqual(snapshot.tonics(for: "Ionian")?.count, 12)
    XCTAssertNil(snapshot.tonics(for: "Dorian"), "a row without twelve entries is not used")
    XCTAssertNil(snapshot.tonics(for: "Locrian"), "a mode without a row")
  }

  // MARK: - Other payloads

  func testMessagePayloadsDecode() throws {
    XCTAssertEqual(try Self.decode(ErrorJSON.self, #"{"schema":1,"error":"bad"}"#).error, "bad")
    XCTAssertEqual(try Self.decode(StatusJSON.self, #"{"schema":1,"incompleteBlocks":3}"#).incompleteBlocks, 3)
    XCTAssertEqual(try Self.decode(TransportJSON.self, #"{"schema":1,"ppq":12.5,"playing":true}"#),
                   TransportJSON(schema: 1, ppq: 12.5, playing: true))
    XCTAssertEqual(try Self.decode(ExportReadyJSON.self, #"{"schema":1,"path":"/tmp/x.mid"}"#).path, "/tmp/x.mid")
  }

  func testMissingRequiredFieldFailsToDecode() {
    let broken = Self.phase1.replacingOccurrences(of: "\"endBeat\":4.0,", with: "")
    XCTAssertThrowsError(try Self.decode(Snapshot.self, broken))
  }

  // MARK: - Engine fixtures (engine/tests/fixtures)

  /// engine/tests/fixtures, found from this file's path (ChordLab/Tests/ChordLabUICoreTests/ → repo root);
  /// `CHORDLAB_UI_FIXTURES_DIR` overrides it.
  static var fixturesDirectory: URL {
    if let override = ProcessInfo.processInfo.environment["CHORDLAB_UI_FIXTURES_DIR"], !override.isEmpty {
      return URL(fileURLWithPath: override, isDirectory: true)
    }
    return URL(fileURLWithPath: #filePath)
      .deletingLastPathComponent()   // ChordLabUICoreTests
      .deletingLastPathComponent()   // Tests
      .deletingLastPathComponent()   // ChordLab
      .deletingLastPathComponent()   // repo root
      .appendingPathComponent("engine/tests/fixtures", isDirectory: true)
  }

  /// The fixture files whose names start with `prefix` and end in .json, sorted; skips the test when the
  /// directory is missing.
  private func fixtures(prefix: String) throws -> [URL] {
    let dir = Self.fixturesDirectory
    var isDirectory: ObjCBool = false
    guard FileManager.default.fileExists(atPath: dir.path, isDirectory: &isDirectory), isDirectory.boolValue else {
      throw XCTSkip("no fixtures directory at \(dir.path)")
    }
    return try FileManager.default.contentsOfDirectory(at: dir, includingPropertiesForKeys: nil)
      .filter { $0.lastPathComponent.hasPrefix(prefix) && $0.pathExtension == "json" }
      .sorted { $0.lastPathComponent < $1.lastPathComponent }
  }

  /// The named snapshot fixture, decoded; skips the test when it does not exist.
  private func fixture(_ name: String) throws -> Snapshot {
    let url = Self.fixturesDirectory.appendingPathComponent(name)
    guard FileManager.default.fileExists(atPath: url.path) else {
      throw XCTSkip("no fixture \(url.path)")
    }
    return try JSONDecoder().decode(Snapshot.self, from: Data(contentsOf: url))
  }

  func testEverySnapshotFixtureDecodes() throws {
    let files = try fixtures(prefix: "snapshot-")
    guard !files.isEmpty else { throw XCTSkip("no snapshot-*.json in \(Self.fixturesDirectory.path)") }
    for url in files {
      let name = url.lastPathComponent
      do {
        let snapshot = try JSONDecoder().decode(Snapshot.self, from: Data(contentsOf: url))
        XCTAssertEqual(snapshot.schema, 1, name)
        XCTAssertGreaterThan(snapshot.palette.tiers, 0, name)
        XCTAssertFalse(snapshot.palette.cells.isEmpty, name)
        XCTAssertEqual(Set(snapshot.palette.cells.map(\.id)).count, snapshot.palette.cells.count, "\(name): cell ids unique")
        XCTAssertEqual(Set(snapshot.events.map(\.id)).count, snapshot.events.count, "\(name): event ids unique")
      } catch {
        XCTFail("\(name) does not decode: \(error)")
      }
    }
  }

  /// The UI has no type for the persistent state (`stateJson`, which the plug-in shell stores and loads
  /// itself), so a state fixture is only checked to be a schema-1 JSON object with events.
  func testEveryStateFixtureIsASchemaOneDocument() throws {
    let files = try fixtures(prefix: "state-")
    guard !files.isEmpty else { throw XCTSkip("no state-*.json in \(Self.fixturesDirectory.path)") }
    for url in files {
      let name = url.lastPathComponent
      let object = try JSONSerialization.jsonObject(with: Data(contentsOf: url)) as? [String: Any]
      XCTAssertNotNil(object, "\(name) is a JSON object")
      XCTAssertEqual(object?["schema"] as? Int, 1, name)
      XCTAssertNotNil(object?["events"] as? [Any], "\(name) has events")
      XCTAssertNotNil(object?["projectKey"].flatMap { try? JSONSerialization.data(withJSONObject: $0) }
        .flatMap { try? JSONDecoder().decode(KeyJSON.self, from: $0) }, "\(name): projectKey decodes as KeyJSON")
    }
  }

  func testLegacyPhase1FixtureHasNoPhase2Fields() throws {
    let snapshot = try fixture("snapshot-legacy-phase1.json")
    XCTAssertNil(snapshot.autoVoiceLead)
    XCTAssertNil(snapshot.selectedEventId)
    XCTAssertTrue(snapshot.sections?.isEmpty ?? true)
    XCTAssertTrue(snapshot.suggestions?.isEmpty ?? true)
    XCTAssertNil(snapshot.homeTriads)
    XCTAssertTrue(snapshot.homeRow.isEmpty)
    for event in snapshot.events {
      XCTAssertNil(event.function, "event \(event.id)")
      XCTAssertNil(event.tension, "event \(event.id)")
      XCTAssertNil(event.inversion, "event \(event.id)")
      XCTAssertNil(event.octave, "event \(event.id)")
      XCTAssertNil(event.manualVoicing, "event \(event.id)")
      XCTAssertNil(event.why, "event \(event.id)")
    }
    for cell in snapshot.palette.cells {
      XCTAssertNil(cell.function, "cell \(cell.id)")
      XCTAssertNil(cell.tension, "cell \(cell.id)")
      XCTAssertNil(cell.engineId, "cell \(cell.id)")
    }
  }

  func testHomeEmptyFixture() throws {
    let snapshot = try fixture("snapshot-home-empty.json")
    XCTAssertTrue(snapshot.events.isEmpty)
    XCTAssertEqual(snapshot.projectKey, snapshot.browsedKey)
    XCTAssertEqual(snapshot.homeTriads?.isEmpty, true, "home: homeTriads present and empty")
    XCTAssertTrue(snapshot.homeRow.isEmpty)
    XCTAssertTrue(snapshot.sections?.isEmpty ?? true)
  }

  func testHomeProgressionFixture() throws {
    let snapshot = try fixture("snapshot-home-progression.json")
    XCTAssertFalse(snapshot.events.isEmpty)
    XCTAssertEqual(snapshot.projectKey, snapshot.browsedKey)
    XCTAssertTrue(snapshot.homeRow.isEmpty)
    XCTAssertNotNil(snapshot.autoVoiceLead)
    XCTAssertNil(snapshot.events.first?.why, "the first event has no why")
    for event in snapshot.events.dropFirst() {
      XCTAssertNotNil(event.why, "event \(event.id) relates to the one before it")
    }
    for event in snapshot.events {
      XCTAssertNotNil(event.function, "event \(event.id)")
      XCTAssertNotNil(event.tension, "event \(event.id)")
    }
  }

  func testBrowsingProgressionFixtureCarriesTheHomeRow() throws {
    let snapshot = try fixture("snapshot-browsing-progression.json")
    XCTAssertNotEqual(snapshot.projectKey, snapshot.browsedKey)
    XCTAssertEqual(snapshot.homeRow.count, 7)
    XCTAssertEqual(snapshot.homeRow.map(\.id), (0..<7).map { "home-\($0)" })
    XCTAssertEqual(snapshot.homeRow.map(\.degree), Array(0..<7))
    XCTAssertTrue(snapshot.homeRow.allSatisfy { $0.tier == 1 })
    XCTAssertTrue(Set(snapshot.homeRow.map(\.id)).isDisjoint(with: snapshot.palette.cells.map(\.id)))
  }
}
