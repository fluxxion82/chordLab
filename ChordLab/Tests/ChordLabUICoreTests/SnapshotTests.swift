import Foundation
import XCTest
@testable import ChordLabUICore

/// Decoding the engine's snapshot JSON (UI/Core/Snapshot.swift): inline Phase 1 / Phase 2 / homeTriads
/// cases, the small message payloads, and the fixtures the engine tests write to engine/tests/fixtures.
///
/// The fixtures are required. The directory is engine/tests/fixtures (found from this file's path), or
/// `CHORDLAB_UI_FIXTURES_DIR` when that is set; a missing directory or a missing required file
/// (`requiredFixtures`) fails the fixture tests, it never skips them. The one opt-out is
/// `CHORDLAB_UI_SKIP_FIXTURES=1` (honoured only without `CHORDLAB_UI_FIXTURES_DIR`), which skips the
/// fixture tests with `XCTSkip`, for running the package on its own:
/// `CHORDLAB_UI_SKIP_FIXTURES=1 swift test`.
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

  func testPhase1SnapshotHasNoKeyTransposeFlags() throws {
    let snapshot = try Self.decode(Snapshot.self, Self.phase1)
    XCTAssertNil(snapshot.transposeWithKey)
    XCTAssertNil(snapshot.keyHintSeen)
  }

  /// `json` with the two key-transpose flags written before the palette.
  private static func withKeyFlags(_ json: String, transposeWithKey: Bool, keyHintSeen: Bool) -> String {
    json.replacingOccurrences(
      of: "\"palette\":",
      with: "\"transposeWithKey\":\(transposeWithKey),\"keyHintSeen\":\(keyHintSeen),\"palette\":")
  }

  func testKeyTransposeFlagsDecodeExplicitTrueAndFalseIndependently() throws {
    for (switchOn, seen) in [(true, true), (false, false), (true, false), (false, true)] {
      let snapshot = try Self.decode(
        Snapshot.self, Self.withKeyFlags(Self.phase2, transposeWithKey: switchOn, keyHintSeen: seen))
      XCTAssertEqual(snapshot.transposeWithKey, switchOn)
      XCTAssertEqual(snapshot.keyHintSeen, seen)
    }
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

  /// `phase1` with `modes` and/or `tonics` added: an Ionian row of twelve tonics and an empty Dorian row.
  private static func withTonicTable(modes: Bool, tonics: Bool) -> String {
    let row = (0..<12).map { "{\"letter\":\"C\",\"alter\":0,\"text\":\"C\($0)\"}" }.joined(separator: ",")
    var fields = ""
    if modes { fields += "\"modes\":[\"Ionian\",\"Dorian\"]," }
    if tonics { fields += "\"tonics\":{\"Ionian\":[\(row)],\"Dorian\":[]}," }
    return phase1.replacingOccurrences(of: "\"events\":", with: fields + "\"events\":")
  }

  func testTonicTableWithModesAndTonics() throws {
    let snapshot = try Self.decode(Snapshot.self, Self.withTonicTable(modes: true, tonics: true))
    XCTAssertTrue(snapshot.hasTonicTable)
    XCTAssertEqual(snapshot.tonics(for: "Ionian")?.count, 12)
    XCTAssertEqual(snapshot.tonics(for: "Ionian")?[3].text, "C3")
  }

  func testNoTonicTableWithoutModes() throws {
    XCTAssertFalse(try Self.decode(Snapshot.self, Self.withTonicTable(modes: false, tonics: true)).hasTonicTable)
  }

  func testNoTonicTableWithoutTonics() throws {
    XCTAssertFalse(try Self.decode(Snapshot.self, Self.withTonicTable(modes: true, tonics: false)).hasTonicTable)
  }

  func testNoTonicTableWithNeither() throws {
    XCTAssertFalse(try Self.decode(Snapshot.self, Self.phase1).hasTonicTable)
  }

  func testTonicRowWithoutTwelveEntriesIsNotUsed() throws {
    let snapshot = try Self.decode(Snapshot.self, Self.withTonicTable(modes: true, tonics: true))
    XCTAssertNil(snapshot.tonics(for: "Dorian"))
  }

  func testTonicsForAModeWithoutARowAreNil() throws {
    let snapshot = try Self.decode(Snapshot.self, Self.withTonicTable(modes: true, tonics: true))
    XCTAssertNil(snapshot.tonics(for: "Locrian"))
  }

  // MARK: - Chord relations (plan R6)

  /// `phase2` with the top-level relation fields: `relationMode` and, unless `reference` is nil, a
  /// `relationReference` (`"null"` or an object, written as given).
  private static func withRelations(mode: String?, reference: String?) -> String {
    var fields = ""
    if let mode = mode { fields += "\"relationMode\":\"\(mode)\"," }
    if let reference = reference { fields += "\"relationReference\":\(reference)," }
    return phase2.replacingOccurrences(of: "\"events\":", with: fields + "\"events\":")
  }

  /// One palette cell, with `relation` / `relationText` when `relation` is given.
  private static func relationCell(degree: Int, relation: Int?) -> String {
    let extra = relation.map { ",\"relation\":\($0),\"relationText\":\"2 common tones (C, E) · up a fourth · cost 3\"" } ?? ""
    return "{\"degree\":\(degree),\"tier\":1,\"root\":0,\"type\":\"maj\",\"symbol\":\"C\",\"roman\":\"I\",\"family\":\"Major\"," +
      "\"function\":\"T\",\"tension\":3,\"filled\":true,\"voicing\":[60,64,67],\"outOfKey\":[]\(extra)}"
  }

  /// `json` with its palette replaced by one tier-1 row of `cells`.
  private static func withPaletteCells(_ json: String, _ cells: [String]) -> String {
    let start = json.range(of: "\"palette\":")!.lowerBound
    return String(json[..<start]) + "\"palette\":{\"tiers\":2,\"cells\":[\(cells.joined(separator: ","))]}}"
  }

  func testOlderSnapshotHasNoRelationFields() throws {
    let snapshot = try Self.decode(Snapshot.self, Self.phase2)
    XCTAssertNil(snapshot.relationMode)
    XCTAssertNil(snapshot.relationReference)
    XCTAssertNil(snapshot.palette.cells[0].relation)
    XCTAssertNil(snapshot.palette.cells[0].relationText)
  }

  func testRelationModeAndReferenceObjectDecode() throws {
    let snapshot = try Self.decode(Snapshot.self, Self.withRelations(mode: "commonTones",
                                                                      reference: "{\"eventId\":2,\"symbol\":\"G7\"}"))
    XCTAssertEqual(snapshot.relationMode, "commonTones")
    XCTAssertEqual(snapshot.relationModeValue, .commonTones)
    XCTAssertEqual(snapshot.relationReference, RelationReferenceJSON(eventId: 2, symbol: "G7"))
  }

  func testNullRelationReferenceIsNil() throws {
    let snapshot = try Self.decode(Snapshot.self, Self.withRelations(mode: "blend", reference: "null"))
    XCTAssertEqual(snapshot.relationModeValue, .blend)
    XCTAssertNil(snapshot.relationReference)
  }

  func testAbsentRelationReferenceIsNil() throws {
    let snapshot = try Self.decode(Snapshot.self, Self.withRelations(mode: "off", reference: nil))
    XCTAssertEqual(snapshot.relationModeValue, .off)
    XCTAssertNil(snapshot.relationReference)
  }

  /// A mode this UI does not know still decodes (the snapshot is never lost over it); it has no case.
  func testUnknownRelationModeDecodesWithoutACase() throws {
    let snapshot = try Self.decode(Snapshot.self, Self.withRelations(mode: "tension", reference: "null"))
    XCTAssertEqual(snapshot.relationMode, "tension")
    XCTAssertNil(snapshot.relationModeValue)
  }

  func testCellsWithAndWithoutRelation() throws {
    let json = Self.withPaletteCells(Self.withRelations(mode: "blend", reference: "{\"eventId\":2,\"symbol\":\"C\"}"),
                                     [Self.relationCell(degree: 0, relation: 72), Self.relationCell(degree: 1, relation: nil)])
    let snapshot = try Self.decode(Snapshot.self, json)
    XCTAssertEqual(snapshot.palette.cells.map(\.relation), [72, nil])
    XCTAssertEqual(snapshot.palette.cells[0].relationText, "2 common tones (C, E) · up a fourth · cost 3")
    XCTAssertNil(snapshot.palette.cells[1].relationText)
  }

  func testHomeCellsCarryRelations() throws {
    let entry = Self.relationCell(degree: 0, relation: 100)
      .replacingOccurrences(of: "{\"degree\"", with: "{\"id\":\"home-0\",\"degree\"")
    let snapshot = try Self.decode(Snapshot.self, Self.withHome(Self.phase2, entry))
    XCTAssertEqual(snapshot.homeRow.first?.id, "home-0")
    XCTAssertEqual(snapshot.homeRow.first?.relation, 100)
    XCTAssertEqual(snapshot.homeRow.first?.relationText, "2 common tones (C, E) · up a fourth · cost 3")
  }

  // MARK: - Progression presets (plan P6)

  /// `phase2` with a top-level `presets` value (written as given) before the events.
  private static func withPresets(_ presets: String) -> String {
    phase2.replacingOccurrences(of: "\"events\":", with: "\"presets\":\(presets),\"events\":")
  }

  static let twoPresets = """
    [{"id":"axis","name":"Axis","genre":"Pop","tonality":"Major","numerals":"I – V – vi – IV","fits":true},
     {"id":"andalusian","name":"Andalusian","genre":"Film / Epic","tonality":"Minor","numerals":"i – bVII – bVI – V7","fits":false}]
    """

  func testOlderSnapshotHasNoPresets() throws {
    XCTAssertNil(try Self.decode(Snapshot.self, Self.phase1).presets)
    XCTAssertNil(try Self.decode(Snapshot.self, Self.phase2).presets)
  }

  func testPresetsDecodeInOrderWithEveryField() throws {
    let snapshot = try Self.decode(Snapshot.self, Self.withPresets(Self.twoPresets))
    XCTAssertEqual(snapshot.presets, [
      PresetJSON(id: "axis", name: "Axis", genre: "Pop", tonality: "Major", numerals: "I – V – vi – IV", fits: true),
      PresetJSON(id: "andalusian", name: "Andalusian", genre: "Film / Epic", tonality: "Minor",
                 numerals: "i – bVII – bVI – V7", fits: false),
    ])
    XCTAssertEqual(snapshot.presets?.map(\.id), ["axis", "andalusian"])
  }

  func testEmptyPresetsArrayIsEmptyNotNil() throws {
    XCTAssertEqual(try Self.decode(Snapshot.self, Self.withPresets("[]")).presets, [])
  }

  func testPresetsLeaveTheRestOfTheSnapshotUnaffected() throws {
    let plain = try Self.decode(Snapshot.self, Self.phase2)
    let withPresets = try Self.decode(Snapshot.self, Self.withPresets(Self.twoPresets))
    XCTAssertEqual(withPresets.events, plain.events)
    XCTAssertEqual(withPresets.sections, plain.sections)
    XCTAssertEqual(withPresets.suggestions, plain.suggestions)
  }

  /// A preset missing a required field fails the decode (the engine always sends all six).
  func testPresetWithoutFitsFailsToDecode() {
    let bad = #"[{"id":"axis","name":"Axis","genre":"Pop","tonality":"Major","numerals":"I"}]"#
    XCTAssertThrowsError(try Self.decode(Snapshot.self, Self.withPresets(bad)))
  }

  // MARK: - Applied chords (plan A3, A5)

  /// `phase2` with a top-level `applied` value (written as given) before the events.
  private static func withApplied(_ applied: String) -> String {
    phase2.replacingOccurrences(of: "\"events\":", with: "\"applied\":\(applied),\"events\":")
  }

  /// Two applied entries as the engine writes them in C Ionian: V7/ii (A7, tier 1 over column 1) with a
  /// relation, and vii°7/ii (C#dim7, tier 0) without one.
  static let twoApplied = """
    [{"id":"applied-1-1","degree":1,"tier":1,"root":9,"type":"7","symbol":"A7","roman":"VI7","family":"Major",
      "function":"D","tension":45,"filled":false,"voicing":[57,61,64,67],"outOfKey":[1],
      "relation":64,"relationText":"1 common tone (E) · up a sixth · cost 5","label":"V7/ii","target":"Dm"},
     {"id":"applied-1-0","degree":1,"tier":0,"root":1,"type":"dim7","symbol":"C#dim7","roman":"#Idim7","family":"Other",
      "function":"D","tension":70,"filled":false,"voicing":[49,52,55,58],"outOfKey":[1,10],"label":"vii°7/ii","target":"Dm"}]
    """

  func testOlderSnapshotHasNoApplied() throws {
    XCTAssertNil(try Self.decode(Snapshot.self, Self.phase1).applied)
    XCTAssertNil(try Self.decode(Snapshot.self, Self.phase2).applied)
  }

  func testEmptyAppliedArrayIsEmptyNotNil() throws {
    XCTAssertEqual(try Self.decode(Snapshot.self, Self.withApplied("[]")).applied, [])
  }

  func testAppliedCellsDecodeWithLabelTargetAndEngineIds() throws {
    let applied = try XCTUnwrap(try Self.decode(Snapshot.self, Self.withApplied(Self.twoApplied)).applied)
    XCTAssertEqual(applied.map(\.id), ["applied-1-1", "applied-1-0"])
    XCTAssertEqual(applied.map(\.degree), [1, 1])
    XCTAssertEqual(applied.map(\.tier), [1, 0])
    XCTAssertEqual(applied.map(\.symbol), ["A7", "C#dim7"])
    XCTAssertEqual(applied.map(\.roman), ["VI7", "#Idim7"])
    XCTAssertEqual(applied.map(\.label), ["V7/ii", "vii°7/ii"])
    XCTAssertEqual(applied.map(\.target), ["Dm", "Dm"])
    XCTAssertEqual(applied.map(\.filled), [false, false])
    XCTAssertEqual(applied.map(\.outOfKey), [[1], [1, 10]])
    XCTAssertEqual(applied.map(\.relation), [64, nil])
    XCTAssertEqual(applied[0].relationText, "1 common tone (E) · up a sixth · cost 5")
    XCTAssertNil(applied[1].relationText)
  }

  /// No applied id is a palette or home id, so a held applied cell never lights one of those.
  func testAppliedIdsDifferFromPaletteAndHomeIds() throws {
    let snapshot = try Self.decode(Snapshot.self, Self.withHome(Self.withApplied(Self.twoApplied), Self.homeEntries))
    let applied = Set(try XCTUnwrap(snapshot.applied).map(\.id))
    XCTAssertTrue(applied.isDisjoint(with: snapshot.palette.cells.map(\.id)))
    XCTAssertTrue(applied.isDisjoint(with: snapshot.homeRow.map(\.id)))
  }

  /// Palette and home cells carry no `label` / `target`.
  func testPaletteAndHomeCellsHaveNoLabelOrTarget() throws {
    let snapshot = try Self.decode(Snapshot.self, Self.withHome(Self.withApplied(Self.twoApplied), Self.homeEntries))
    for cell in snapshot.palette.cells + snapshot.homeRow {
      XCTAssertNil(cell.label, "cell \(cell.id)")
      XCTAssertNil(cell.target, "cell \(cell.id)")
    }
  }

  func testAppliedLeavesTheRestOfTheSnapshotUnaffected() throws {
    let plain = try Self.decode(Snapshot.self, Self.phase2)
    let withApplied = try Self.decode(Snapshot.self, Self.withApplied(Self.twoApplied))
    XCTAssertEqual(withApplied.events, plain.events)
    XCTAssertEqual(withApplied.palette, plain.palette)
    XCTAssertEqual(withApplied.suggestions, plain.suggestions)
  }

  /// An applied entry missing a required cell field fails the decode, as a palette cell would.
  func testAppliedCellWithoutSymbolFailsToDecode() {
    let bad = #"[{"id":"applied-1-1","degree":1,"tier":1,"root":9,"type":"7","roman":"VI7","family":"Major","filled":false,"voicing":[57]}]"#
    XCTAssertThrowsError(try Self.decode(Snapshot.self, Self.withApplied(bad)))
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

  /// The files the engine tests write and these tests read; each missing one is a failure.
  static let requiredFixtures = [
    "snapshot-home-empty.json",
    "snapshot-home-progression.json",
    "snapshot-browsing-progression.json",
    "snapshot-legacy-phase1.json",
    "state-progression.json",
  ]

  /// A missing fixture directory or file: thrown from a test, it fails that test (never a skip).
  struct FixtureMissing: Error, CustomStringConvertible {
    let description: String
  }

  /// engine/tests/fixtures from this file's path (ChordLab/Tests/ChordLabUICoreTests/ → repo root).
  static let defaultFixturesDirectory = URL(fileURLWithPath: #filePath)
    .deletingLastPathComponent()   // ChordLabUICoreTests
    .deletingLastPathComponent()   // Tests
    .deletingLastPathComponent()   // ChordLab
    .deletingLastPathComponent()   // repo root
    .appendingPathComponent("engine/tests/fixtures", isDirectory: true)

  /// The fixture directory under the rules in the class comment: `CHORDLAB_UI_FIXTURES_DIR` when set,
  /// else the default unless `CHORDLAB_UI_SKIP_FIXTURES=1` (XCTSkip); a missing directory throws
  /// `FixtureMissing`.
  static func fixturesDirectory() throws -> URL {
    let environment = ProcessInfo.processInfo.environment
    let directory: URL
    if let override = environment["CHORDLAB_UI_FIXTURES_DIR"], !override.isEmpty {
      directory = URL(fileURLWithPath: override, isDirectory: true)
    } else if environment["CHORDLAB_UI_SKIP_FIXTURES"] == "1" {
      throw XCTSkip("fixture tests skipped: CHORDLAB_UI_SKIP_FIXTURES=1")
    } else {
      directory = defaultFixturesDirectory
    }
    var isDirectory: ObjCBool = false
    guard FileManager.default.fileExists(atPath: directory.path, isDirectory: &isDirectory), isDirectory.boolValue else {
      throw FixtureMissing(description: "no fixtures directory at \(directory.path) (set CHORDLAB_UI_SKIP_FIXTURES=1 to skip)")
    }
    return directory
  }

  /// The fixture `name`; throws `FixtureMissing` when it does not exist.
  private func fixtureURL(_ name: String) throws -> URL {
    let url = try Self.fixturesDirectory().appendingPathComponent(name)
    guard FileManager.default.fileExists(atPath: url.path) else {
      throw FixtureMissing(description: "missing fixture \(url.path)")
    }
    return url
  }

  /// The snapshot fixture `name`, decoded.
  private func snapshotFixture(_ name: String) throws -> Snapshot {
    try JSONDecoder().decode(Snapshot.self, from: Data(contentsOf: fixtureURL(name)))
  }

  /// The fixture files whose names start with `prefix` and end in .json, sorted.
  private func fixtures(prefix: String) throws -> [URL] {
    try FileManager.default.contentsOfDirectory(at: Self.fixturesDirectory(), includingPropertiesForKeys: nil)
      .filter { $0.lastPathComponent.hasPrefix(prefix) && $0.pathExtension == "json" }
      .sorted { $0.lastPathComponent < $1.lastPathComponent }
  }

  func testRequiredFixturesArePresent() throws {
    let directory = try Self.fixturesDirectory()
    for name in Self.requiredFixtures {
      XCTAssertTrue(FileManager.default.fileExists(atPath: directory.appendingPathComponent(name).path), "missing \(name)")
    }
  }

  func testEverySnapshotFixtureDecodes() throws {
    let files = try fixtures(prefix: "snapshot-")
    XCTAssertGreaterThanOrEqual(files.count, 4, "the four required snapshot fixtures")
    for url in files {
      let name = url.lastPathComponent
      do {
        let snapshot = try JSONDecoder().decode(Snapshot.self, from: Data(contentsOf: url))
        XCTAssertEqual(snapshot.schema, 1, name)
        XCTAssertGreaterThan(snapshot.palette.tiers, 0, name)
        XCTAssertFalse(snapshot.palette.cells.isEmpty, name)
        XCTAssertEqual(Set(snapshot.palette.cells.map(\.id)).count, snapshot.palette.cells.count, "\(name): cell ids unique")
        XCTAssertEqual(Set(snapshot.events.map(\.id)).count, snapshot.events.count, "\(name): event ids unique")
        if let applied = snapshot.applied {
          XCTAssertEqual(Set(applied.map(\.id)).count, applied.count, "\(name): applied ids unique")
        }
      } catch {
        XCTFail("\(name) does not decode: \(error)")
      }
    }
  }

  /// The legacy Phase 1 fixture predates presets; the generated ones decode with or without them.
  func testLegacyFixtureHasNoPresets() throws {
    XCTAssertNil(try snapshotFixture("snapshot-legacy-phase1.json").presets)
  }

  /// The persistent state (`stateJson`) as far as these tests read it. The UI itself has no type for it
  /// (the plug-in shell stores and loads the text), so this lives here.
  private struct StateFixture: Decodable {
    struct Event: Decodable {
      let id: Int
      let root: Int
      let type: String
      let lengthBeats: Double
      let inversion: Int?
      let manualVoicing: Bool?
    }
    let schema: Int
    let projectKey: KeyJSON
    let browsedKey: KeyJSON
    let autoVoiceLead: Bool?
    let transposeWithKey: Bool?
    let keyHintSeen: Bool?
    let events: [Event]
    let sections: [SectionJSON]?
  }

  func testStateReadsTheKeyTransposeFlags() throws {
    let state = try JSONDecoder().decode(StateFixture.self, from: Data(contentsOf: fixtureURL("state-progression.json")))
    XCTAssertEqual(state.transposeWithKey, false)
    XCTAssertEqual(state.keyHintSeen, false)
    let inline = """
      {"schema":1,"projectKey":{"tonic":{"letter":"C","alter":0},"mode":"Ionian"},
       "browsedKey":{"tonic":{"letter":"C","alter":0},"mode":"Ionian"},
       "transposeWithKey":true,"keyHintSeen":true,"events":[]}
      """
    let on = try Self.decode(StateFixture.self, inline)
    XCTAssertEqual(on.transposeWithKey, true)
    XCTAssertEqual(on.keyHintSeen, true)
    let absent = """
      {"schema":1,"projectKey":{"tonic":{"letter":"C","alter":0},"mode":"Ionian"},
       "browsedKey":{"tonic":{"letter":"C","alter":0},"mode":"Ionian"},"events":[]}
      """
    let none = try Self.decode(StateFixture.self, absent)
    XCTAssertNil(none.transposeWithKey)
    XCTAssertNil(none.keyHintSeen)
  }

  func testEveryStateFixtureIsASchemaOneDocument() throws {
    let files = try fixtures(prefix: "state-")
    XCTAssertFalse(files.isEmpty, "at least state-progression.json")
    for url in files {
      let name = url.lastPathComponent
      do {
        let state = try JSONDecoder().decode(StateFixture.self, from: Data(contentsOf: url))
        XCTAssertEqual(state.schema, 1, name)
        XCTAssertFalse(state.events.isEmpty, "\(name) has events")
      } catch {
        XCTFail("\(name) does not decode: \(error)")
      }
    }
  }

  /// The progression every progression fixture holds: C (4 beats), Am (4, manual, inversion 1), F (2),
  /// G7 (6) in C major, auto voice-leading on, sections "Verse" at 0 (colour 0) and "Chorus" at 2
  /// (colour 3).
  private static let progressionRoots = [0, 9, 5, 7]
  private static let progressionTypes = ["maj", "min", "maj", "7"]
  private static let progressionLengths: [Double] = [4, 4, 2, 6]
  private static let progressionSections = [
    SectionJSON(id: 1, name: "Verse", colourIndex: 0, firstEventIndex: 0),
    SectionJSON(id: 2, name: "Chorus", colourIndex: 3, firstEventIndex: 2),
  ]

  func testStateProgressionFixture() throws {
    let state = try JSONDecoder().decode(StateFixture.self, from: Data(contentsOf: fixtureURL("state-progression.json")))
    XCTAssertEqual(state.projectKey, KeyJSON(tonic: TonicJSON(letter: "C", alter: 0), mode: "Ionian"))
    XCTAssertEqual(state.events.map(\.id), [1, 2, 3, 4])
    XCTAssertEqual(state.events.map(\.root), Self.progressionRoots)
    XCTAssertEqual(state.events.map(\.type), Self.progressionTypes)
    XCTAssertEqual(state.events.map(\.lengthBeats), Self.progressionLengths)
    XCTAssertEqual(state.events.map(\.manualVoicing), [false, true, false, false])
    XCTAssertEqual(state.events.map(\.inversion), [0, 1, 0, 0])
    XCTAssertEqual(state.autoVoiceLead, true)
    XCTAssertEqual(state.sections, Self.progressionSections)
  }

  /// Checks `snapshot` against the progression fixture's document (both progression snapshots share it).
  private func checkProgression(_ snapshot: Snapshot, file: StaticString = #filePath, line: UInt = #line) {
    XCTAssertEqual(snapshot.projectKey, KeyJSON(tonic: TonicJSON(letter: "C", alter: 0), mode: "Ionian"), file: file, line: line)
    XCTAssertEqual(snapshot.events.map(\.id), [1, 2, 3, 4], file: file, line: line)
    XCTAssertEqual(snapshot.events.map(\.symbol), ["C", "Am", "F", "G7"], file: file, line: line)
    XCTAssertEqual(snapshot.events.map(\.root), Self.progressionRoots, file: file, line: line)
    XCTAssertEqual(snapshot.events.map(\.type), Self.progressionTypes, file: file, line: line)
    XCTAssertEqual(snapshot.events.map(\.roman), ["I", "vi", "IV", "V7"], file: file, line: line)
    XCTAssertEqual(snapshot.events.map(\.function), ["T", "T", "PD", "D"], file: file, line: line)
    XCTAssertEqual(snapshot.events.map(\.lengthBeats), Self.progressionLengths, file: file, line: line)
    XCTAssertEqual(snapshot.events.map(\.startBeat), [0, 4, 8, 10], file: file, line: line)
    XCTAssertEqual(snapshot.endBeat, 16, file: file, line: line)
    XCTAssertEqual(snapshot.events.map(\.manualVoicing), [false, true, false, false], "Am is manual", file: file, line: line)
    XCTAssertEqual(snapshot.events.map(\.inversion), [0, 1, 0, 0], file: file, line: line)
    XCTAssertEqual(snapshot.autoVoiceLead, true, file: file, line: line)
    XCTAssertEqual(snapshot.sections, Self.progressionSections, file: file, line: line)
    XCTAssertEqual(snapshot.selectedEventId, 3, file: file, line: line)
    XCTAssertFalse(snapshot.suggestions?.isEmpty ?? true, "suggestions after the selected chord", file: file, line: line)
    guard snapshot.events.count >= 2 else {
      return XCTFail("the why checks need at least two events", file: file, line: line)
    }
    XCTAssertNil(snapshot.events[0].why, "the first event has no why", file: file, line: line)
    for event in snapshot.events.dropFirst() {
      XCTAssertFalse(event.why?.text.isEmpty ?? true, "event \(event.id) has a why sentence", file: file, line: line)
    }
  }

  func testHomeProgressionFixture() throws {
    let snapshot = try snapshotFixture("snapshot-home-progression.json")
    checkProgression(snapshot)
    XCTAssertEqual(snapshot.browsedKey, snapshot.projectKey)
    XCTAssertEqual(snapshot.homeTriads, [], "home: homeTriads present and empty")
    XCTAssertTrue(snapshot.homeRow.isEmpty)
  }

  func testBrowsingProgressionFixtureCarriesTheHomeRow() throws {
    let snapshot = try snapshotFixture("snapshot-browsing-progression.json")
    checkProgression(snapshot)
    XCTAssertEqual(snapshot.browsedKey, KeyJSON(tonic: TonicJSON(letter: "A", alter: 0), mode: "Aeolian"))
    XCTAssertEqual(snapshot.browsedKeyName, "A Aeolian")
    XCTAssertEqual(snapshot.homeRow.map(\.id), (0..<7).map { "home-\($0)" })
    XCTAssertEqual(snapshot.homeRow.map(\.degree), Array(0..<7))
    XCTAssertTrue(snapshot.homeRow.allSatisfy { $0.tier == 1 })
    XCTAssertEqual(snapshot.homeRow.map(\.symbol), ["C", "Dm", "Em", "F", "G", "Am", "Bmb5"], "the project key's triads")
    XCTAssertEqual(snapshot.homeRow.map(\.roman), ["I", "ii", "iii", "IV", "V", "vi", "vii°"])
    XCTAssertEqual(snapshot.palette.cells.filter { $0.tier == 1 }.map(\.symbol), ["Am", "Bmb5", "C", "Dm", "Em", "F", "G"],
                   "the browsed key's triads")
    XCTAssertTrue(Set(snapshot.homeRow.map(\.id)).isDisjoint(with: snapshot.palette.cells.map(\.id)))
  }

  func testHomeEmptyFixture() throws {
    let snapshot = try snapshotFixture("snapshot-home-empty.json")
    XCTAssertTrue(snapshot.events.isEmpty)
    XCTAssertEqual(snapshot.endBeat, 0)
    XCTAssertEqual(snapshot.browsedKey, snapshot.projectKey)
    XCTAssertNil(snapshot.selectedEventId)
    XCTAssertEqual(snapshot.sections, [])
    XCTAssertEqual(snapshot.homeTriads, [])
    XCTAssertFalse(snapshot.suggestions?.isEmpty ?? true, "the \"Start with\" suggestions")
    XCTAssertEqual(snapshot.suggestions?.first?.symbol, "C", "an empty document opens with the tonic triad")
  }

  func testLegacyPhase1FixtureHasNoPhase2Fields() throws {
    let snapshot = try snapshotFixture("snapshot-legacy-phase1.json")
    XCTAssertEqual(snapshot.events.map(\.symbol), ["C", "G7"])
    XCTAssertEqual(snapshot.events.map(\.type), ["maj", "7"])
    XCTAssertEqual(snapshot.events.last?.voicing, [67, 71, 74, 77])
    XCTAssertNil(snapshot.autoVoiceLead)
    XCTAssertNil(snapshot.selectedEventId)
    XCTAssertNil(snapshot.sections)
    XCTAssertNil(snapshot.suggestions)
    XCTAssertNil(snapshot.homeTriads)
    XCTAssertTrue(snapshot.homeRow.isEmpty)
    XCTAssertNil(snapshot.applied)
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
}
