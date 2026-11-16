import Foundation
import XCTest
@testable import ChordLabUICore

/// The drag-outs' requests and enablement (UI/Core/ClipExport.swift, tracks design D12, D13): every grip builds a
/// `kMsgTagExportClip` payload (a tab's grip its lane, the All grip `"all"`, the bottom drag-out the open lane, a
/// section handle the open lane's section), or nothing when there is nothing to export; the texts the grips show and
/// speak; the bottom drag-out's title.
///
/// The fixture is the engine's snapshot-tracks.json: C [0, 4), Am [4, 8), F [8, 9), G7 [9, 15) beats; sections Verse
/// (id 1, beats 0–8) and Chorus (id 2, beats 8–15); Bass (id 1, muted) with notes at [0, 1) and [4, 6) beats and an
/// inactive one at 9.5; Lead (id 2, soloed) with notes at [1, 1.5) and [9.5, 10.25).
final class ClipExportTests: XCTestCase {
  // MARK: - Fixtures

  private func fixtureObject() throws -> [String: Any] {
    let url = try SnapshotTests.fixturesDirectory().appendingPathComponent("snapshot-tracks.json")
    return try XCTUnwrap(JSONSerialization.jsonObject(with: Data(contentsOf: url)) as? [String: Any])
  }

  private func decode(_ object: [String: Any]) throws -> Snapshot {
    try JSONDecoder().decode(Snapshot.self, from: JSONSerialization.data(withJSONObject: object))
  }

  private func fixture() throws -> Snapshot { try decode(fixtureObject()) }

  /// The fixture with track `index`'s notes replaced by these (columns built from tuples; every note anchored to
  /// chord 1 with offset = start, which the derived `start` column overrides anyway).
  private func fixture(trackIndex: Int, notes: [(start: Int, length: Int, active: Bool)]) throws -> Snapshot {
    var o = try fixtureObject()
    var tracks = try XCTUnwrap(o["tracks"] as? [[String: Any]])
    tracks[trackIndex]["notes"] = [
      "id": notes.indices.map { $0 + 100 }, "pitch": notes.map { _ in 60 }, "start": notes.map(\.start),
      "length": notes.map(\.length), "velocity": notes.map { _ in 100 }, "anchor": notes.map { _ in 1 },
      "offset": notes.map(\.start), "active": notes.map(\.active),
    ]
    o["tracks"] = tracks
    return try decode(o)
  }

  /// The fixture without chords: no events, no sections, no notes (a fresh document with two empty tracks).
  private func emptyFixture() throws -> Snapshot {
    var o = try fixtureObject()
    o["events"] = [Any]()
    o["sections"] = [Any]()
    o["endBeat"] = 0
    var tracks = try XCTUnwrap(o["tracks"] as? [[String: Any]])
    for i in tracks.indices {
      tracks[i]["notes"] = ["id": [], "pitch": [], "start": [], "length": [], "velocity": [], "anchor": [],
                            "offset": [], "active": []]
    }
    o["tracks"] = tracks
    return try decode(o)
  }

  private func assertFields(_ request: ClipExportRequest?, _ expected: [String: Any],
                            file: StaticString = #filePath, line: UInt = #line) {
    guard let request else { return XCTFail("no request", file: file, line: line) }
    XCTAssertTrue(NSDictionary(dictionary: request.fields).isEqual(to: expected), "\(request.fields) != \(expected)",
                  file: file, line: line)
    // What ChordLabState sends is this plus "schema": 1, and it must be valid JSON (NSNull for null).
    var message = request.fields
    message["schema"] = 1
    XCTAssertTrue(JSONSerialization.isValidJSONObject(message), file: file, line: line)
  }

  // MARK: - Requests: a tab's grip

  func testTheChordsGripExportsTheChordsLaneOverTheWholeProgression() throws {
    let s = try fixture()
    let request = ClipExport.request(for: .lane(.chords), in: s, open: .track(2))
    XCTAssertEqual(request, ClipExportRequest(lanes: .some([.chords]), sectionId: nil))
    assertFields(request, ["lanes": ["chords"], "sectionId": NSNull()])
  }

  func testATrackGripExportsThatTrackWhateverLaneIsOpen() throws {
    let s = try fixture()
    for open in [Lane.chords, .track(1), .track(2)] {
      assertFields(ClipExport.request(for: .lane(.track(1)), in: s, open: open), ["lanes": [1], "sectionId": NSNull()])
      assertFields(ClipExport.request(for: .lane(.track(2)), in: s, open: open), ["lanes": [2], "sectionId": NSNull()])
    }
  }

  func testAMutedTrackStillExports() throws {
    let s = try fixture()
    XCTAssertEqual(s.track(id: 1)?.mute, true)
    XCTAssertNotNil(ClipExport.request(for: .lane(.track(1)), in: s, open: .chords), "export what you see (Q4)")
  }

  func testAGripOfATrackTheSnapshotLacksSendsNothing() throws {
    XCTAssertNil(ClipExport.request(for: .lane(.track(9)), in: try fixture(), open: .chords))
  }

  // MARK: - Requests: the All grip

  func testTheAllGripExportsAllOverTheWholeProgression() throws {
    let request = ClipExport.request(for: .all, in: try fixture(), open: .track(1))
    XCTAssertEqual(request, ClipExportRequest(lanes: .all, sectionId: nil))
    assertFields(request, ["lanes": "all", "sectionId": NSNull()])
  }

  func testAllIsInertOnlyWithoutChords() throws {
    XCTAssertNil(ClipExport.request(for: .all, in: try emptyFixture(), open: .chords))
    XCTAssertFalse(ClipExport.canExportAll(try emptyFixture()))
    XCTAssertTrue(ClipExport.canExportAll(try fixture()))
    XCTAssertFalse(ClipExport.canExportAll(nil))
  }

  // MARK: - Requests: the bottom drag-out and the section handles (the open lane)

  func testTheBottomDragOutExportsTheOpenLane() throws {
    let s = try fixture()
    assertFields(ClipExport.request(for: .openLane, in: s, open: .chords), ["lanes": ["chords"], "sectionId": NSNull()])
    assertFields(ClipExport.request(for: .openLane, in: s, open: .track(2)), ["lanes": [2], "sectionId": NSNull()])
    assertFields(ClipExport.request(for: .openLane, in: s, open: .track(1)), ["lanes": [1], "sectionId": NSNull()])
  }

  func testAnOpenTrackTheSnapshotLacksFallsBackToTheChords() throws {
    assertFields(ClipExport.request(for: .openLane, in: try fixture(), open: .track(9)),
                 ["lanes": ["chords"], "sectionId": NSNull()])
  }

  func testASectionHandleExportsTheOpenLanesSection() throws {
    let s = try fixture()
    assertFields(ClipExport.request(for: .openLaneSection(1), in: s, open: .chords), ["lanes": ["chords"], "sectionId": 1])
    assertFields(ClipExport.request(for: .openLaneSection(2), in: s, open: .chords), ["lanes": ["chords"], "sectionId": 2])
    assertFields(ClipExport.request(for: .openLaneSection(1), in: s, open: .track(2)), ["lanes": [2], "sectionId": 1])
    assertFields(ClipExport.request(for: .openLaneSection(2), in: s, open: .track(2)), ["lanes": [2], "sectionId": 2])
    assertFields(ClipExport.request(for: .openLaneSection(1), in: s, open: .track(1)), ["lanes": [1], "sectionId": 1])
    XCTAssertEqual(ClipExport.request(for: .openLaneSection(2), in: s, open: .track(2)),
                   ClipExportRequest(lanes: .some([.track(2)]), sectionId: 2))
  }

  func testASectionTheSnapshotLacksSendsNothing() throws {
    XCTAssertNil(ClipExport.request(for: .openLaneSection(7), in: try fixture(), open: .chords))
    XCTAssertNil(ClipExport.request(for: .openLaneSection(7), in: try fixture(), open: .track(2)))
  }

  func testNothingWithoutASnapshot() {
    for grip in [ExportGrip.lane(.chords), .all, .openLane, .openLaneSection(1)] {
      XCTAssertNil(ClipExport.request(for: grip, in: nil, open: .chords))
    }
  }

  // MARK: - Enablement per lane

  func testTheChordsLaneNeedsChords() throws {
    XCTAssertTrue(ClipExport.canExport(lane: .chords, sectionId: nil, in: try fixture()))
    XCTAssertTrue(ClipExport.canExport(lane: .chords, sectionId: 2, in: try fixture()))
    XCTAssertFalse(ClipExport.canExport(lane: .chords, sectionId: nil, in: try emptyFixture()))
    XCTAssertNil(ClipExport.request(for: .lane(.chords), in: try emptyFixture(), open: .chords))
    XCTAssertNil(ClipExport.request(for: .openLane, in: try emptyFixture(), open: .chords))
  }

  func testATrackWithoutNotesHasNothingToExport() throws {
    let s = try fixture(trackIndex: 1, notes: [])
    XCTAssertFalse(ClipExport.canExport(lane: .track(2), sectionId: nil, in: s))
    XCTAssertNil(ClipExport.request(for: .lane(.track(2)), in: s, open: .chords))
    XCTAssertNil(ClipExport.request(for: .openLane, in: s, open: .track(2)))
    XCTAssertTrue(ClipExport.canExport(lane: .track(1), sectionId: nil, in: s))
    XCTAssertTrue(ClipExport.canExportAll(s), "All still has the chords")
  }

  func testOnlyActiveNotesCount() throws {
    // The fixture's Bass has, in the Chorus (beats 8–15), only its inactive note at 9.5: exportClip writes nothing.
    let s = try fixture()
    XCTAssertTrue(ClipExport.canExport(lane: .track(1), sectionId: 1, in: s))
    XCTAssertFalse(ClipExport.canExport(lane: .track(1), sectionId: 2, in: s))
    XCTAssertNil(ClipExport.request(for: .openLaneSection(2), in: s, open: .track(1)))
    XCTAssertTrue(ClipExport.canExport(lane: .track(2), sectionId: 1, in: s))
    XCTAssertTrue(ClipExport.canExport(lane: .track(2), sectionId: 2, in: s))
    let inactiveOnly = try fixture(trackIndex: 1, notes: [(start: 960, length: 480, active: false)])
    XCTAssertFalse(ClipExport.canExport(lane: .track(2), sectionId: nil, in: inactiveOnly))
  }

  func testANoteCountsForASectionItIntersects() throws {
    // A Lead note from beat 7 to 9 crosses the Verse/Chorus boundary at beat 8: both sections have it.
    let s = try fixture(trackIndex: 1, notes: [(start: 6720, length: 1920, active: true)])
    XCTAssertTrue(ClipExport.canExport(lane: .track(2), sectionId: 1, in: s))
    XCTAssertTrue(ClipExport.canExport(lane: .track(2), sectionId: 2, in: s))
    // One ending exactly at beat 8 belongs to the Verse only (half-open spans).
    let edge = try fixture(trackIndex: 1, notes: [(start: 6720, length: 960, active: true)])
    XCTAssertTrue(ClipExport.canExport(lane: .track(2), sectionId: 1, in: edge))
    XCTAssertFalse(ClipExport.canExport(lane: .track(2), sectionId: 2, in: edge))
  }

  func testAnyOverlapCountsHoweverShortTheCut() throws {
    // Enablement over-approximates exportClip: no cut threshold. A note reaching 4 ticks into the Chorus counts there
    // (the engine may drop that sliver; then the shell answers "nothing to export" and the drag carries nothing).
    let sliver = try fixture(trackIndex: 1, notes: [(start: 6720, length: 964, active: true)])
    XCTAssertTrue(ClipExport.canExport(lane: .track(2), sectionId: 2, in: sliver))
    let tick = try fixture(trackIndex: 1, notes: [(start: 6720, length: 961, active: true)])
    XCTAssertTrue(ClipExport.canExport(lane: .track(2), sectionId: 2, in: tick))
    XCTAssertNotNil(ClipExport.request(for: .openLaneSection(2), in: tick, open: .track(2)))
  }

  func testANoteCrossingTheProgressionsEndCounts() throws {
    // Beat 14.5 to 16 crosses the end at beat 15: exportClip clips it to [14.5, 15), so it counts for the whole
    // progression and for the last section (Chorus), not for the Verse.
    let crossing = try fixture(trackIndex: 1, notes: [(start: 13920, length: 1440, active: true)])
    XCTAssertTrue(ClipExport.canExport(lane: .track(2), sectionId: nil, in: crossing))
    XCTAssertTrue(ClipExport.canExport(lane: .track(2), sectionId: 2, in: crossing))
    XCTAssertFalse(ClipExport.canExport(lane: .track(2), sectionId: 1, in: crossing))
    XCTAssertNotNil(ClipExport.request(for: .lane(.track(2)), in: crossing, open: .chords))
    // One starting a tick before the end still counts.
    let last = try fixture(trackIndex: 1, notes: [(start: 14399, length: 960, active: true)])
    XCTAssertTrue(ClipExport.canExport(lane: .track(2), sectionId: nil, in: last))
  }

  func testANoteStartingAtTheEndIsPastIt() throws {
    // Beat 15 is the progression's end: a note there is clipped away (even though the derived start is active).
    let past = try fixture(trackIndex: 1, notes: [(start: 14400, length: 960, active: true)])
    XCTAssertFalse(ClipExport.canExport(lane: .track(2), sectionId: nil, in: past))
    XCTAssertFalse(ClipExport.canExport(lane: .track(2), sectionId: 2, in: past))
  }

  // MARK: - Texts

  func testTheGripTextsNameTheLane() throws {
    let s = try fixture()
    XCTAssertEqual(ClipExport.text(for: .lane(.chords), in: s, open: .chords),
                   ExportGripText(enabled: true, label: "Drag the chords to Live as a MIDI clip",
                                  help: "Drag the chords to Live as a MIDI clip"))
    XCTAssertEqual(ClipExport.text(for: .lane(.track(1)), in: s, open: .chords),
                   ExportGripText(enabled: true, label: "Drag the Bass track to Live as a MIDI clip",
                                  help: "Drag the Bass track to Live as a MIDI clip"))
    XCTAssertEqual(ClipExport.text(for: .all, in: s, open: .chords),
                   ExportGripText(enabled: true, label: "Drag all lanes to Live, one MIDI clip per lane",
                                  help: "Drag all lanes to Live, one MIDI clip per lane, muted lanes included"))
  }

  func testInertGripsSayWhy() throws {
    let empty = try emptyFixture()
    XCTAssertEqual(ClipExport.text(for: .lane(.chords), in: empty, open: .chords),
                   ExportGripText(enabled: false, label: "Chords: nothing to export", help: "Add chords to export"))
    XCTAssertEqual(ClipExport.text(for: .lane(.track(2)), in: empty, open: .chords),
                   ExportGripText(enabled: false, label: "Lead: nothing to export", help: "Add notes to Lead to export"))
    XCTAssertEqual(ClipExport.text(for: .all, in: empty, open: .chords),
                   ExportGripText(enabled: false, label: "All lanes: nothing to export", help: "Add chords to export"))
  }

  func testTheBottomDragOutTextsKeepTheChordsWordingAndNameAnOpenTrack() throws {
    let s = try fixture()
    XCTAssertEqual(ClipExport.text(for: .openLane, in: s, open: .chords),
                   ExportGripText(enabled: true, label: "Drag the progression to Live as a MIDI clip",
                                  help: "Drag onto a MIDI track in Live"))
    XCTAssertEqual(ClipExport.text(for: .openLane, in: s, open: .track(2)),
                   ExportGripText(enabled: true, label: "Drag the Lead track to Live as a MIDI clip",
                                  help: "Drag the Lead track onto a MIDI track in Live"))
    let empty = try emptyFixture()
    XCTAssertEqual(ClipExport.text(for: .openLane, in: empty, open: .chords),
                   ExportGripText(enabled: false, label: "Nothing to export", help: "Add chords to export"))
    XCTAssertEqual(ClipExport.text(for: .openLane, in: empty, open: .track(2)),
                   ExportGripText(enabled: false, label: "Nothing to export from Lead", help: "Add notes to Lead to export"))
  }

  func testTheSectionHandleTexts() throws {
    let s = try fixture()
    XCTAssertEqual(ClipExport.text(for: .openLaneSection(1), in: s, open: .chords),
                   ExportGripText(enabled: true, label: "Drag the section Verse to Live as a MIDI clip",
                                  help: "Drag this section onto a MIDI track in Live"))
    XCTAssertEqual(ClipExport.text(for: .openLaneSection(1), in: s, open: .track(1)),
                   ExportGripText(enabled: true, label: "Drag Bass in the section Verse to Live as a MIDI clip",
                                  help: "Drag Bass in this section onto a MIDI track in Live"))
    // Bass's only note in the Chorus is inactive (testATrackWithOnlyInactiveNotesSaysItHasNoSoundingNotes).
    XCTAssertFalse(ClipExport.text(for: .openLaneSection(2), in: s, open: .track(1)).enabled)
  }

  func testATrackWithOnlyInactiveNotesSaysItHasNoSoundingNotes() throws {
    // The fixture's Bass has, in the Chorus, only its inactive (dashed) note: the user sees a note there.
    let s = try fixture()
    XCTAssertEqual(ClipExport.text(for: .openLaneSection(2), in: s, open: .track(1)),
                   ExportGripText(enabled: false, label: "Bass has no sounding notes in the section Chorus",
                                  help: "Bass has no sounding notes in this section"))
    // A Lead whose only note is inactive: every inert text says "no sounding notes", none asks to add notes.
    let inactiveOnly = try fixture(trackIndex: 1, notes: [(start: 960, length: 480, active: false)])
    XCTAssertEqual(ClipExport.text(for: .lane(.track(2)), in: inactiveOnly, open: .chords),
                   ExportGripText(enabled: false, label: "Lead: nothing to export",
                                  help: "Lead has no sounding notes to export"))
    XCTAssertEqual(ClipExport.text(for: .openLane, in: inactiveOnly, open: .track(2)),
                   ExportGripText(enabled: false, label: "Nothing to export from Lead",
                                  help: "Lead has no sounding notes to export"))
    XCTAssertEqual(ClipExport.text(for: .openLaneSection(1), in: inactiveOnly, open: .track(2)),
                   ExportGripText(enabled: false, label: "Lead has no sounding notes in the section Verse",
                                  help: "Lead has no sounding notes in this section"))
    // The Chorus has none of Lead's notes at all: "no notes" there.
    XCTAssertEqual(ClipExport.text(for: .openLaneSection(2), in: inactiveOnly, open: .track(2)),
                   ExportGripText(enabled: false, label: "Lead has no notes in the section Chorus",
                                  help: "Lead has no notes in this section"))
    XCTAssertEqual(ClipExport.dragOutTitle(in: inactiveOnly, open: .track(2), preparing: false), "♫ Lead · no notes")
  }

  func testATrackWhoseOnlyNoteIsPastTheEndHasNoSoundingNotes() throws {
    // A note at beat 15 (the end) is drawn past the end in the roll and never exported.
    let past = try fixture(trackIndex: 1, notes: [(start: 14400, length: 960, active: true)])
    XCTAssertEqual(ClipExport.text(for: .openLane, in: past, open: .track(2)).help, "Lead has no sounding notes to export")
  }

  func testATrackWithoutAnyNotesIsAskedForNotes() throws {
    let none = try fixture(trackIndex: 1, notes: [])
    XCTAssertEqual(ClipExport.text(for: .openLane, in: none, open: .track(2)),
                   ExportGripText(enabled: false, label: "Nothing to export from Lead", help: "Add notes to Lead to export"))
    XCTAssertEqual(ClipExport.text(for: .lane(.track(2)), in: none, open: .chords).help, "Add notes to Lead to export")
    XCTAssertEqual(ClipExport.text(for: .openLaneSection(1), in: none, open: .track(2)),
                   ExportGripText(enabled: false, label: "Lead has no notes in the section Verse",
                                  help: "Lead has no notes in this section"))
  }

  func testTheChordsSectionStaysExportableWhileAnOpenTrackHasNothingThere() throws {
    // Bass open, Chorus: the handle exports Bass's part (nothing: inert), never a fallback to the chords; the chords
    // lane over that section is still exportable on its own.
    let s = try fixture()
    XCTAssertNil(ClipExport.request(for: .openLaneSection(2), in: s, open: .track(1)))
    XCTAssertTrue(ClipExport.canExport(lane: .chords, sectionId: 2, in: s))
    XCTAssertTrue(ClipExport.canExport(lane: .chords, sectionId: 1, in: s))
    XCTAssertFalse(ClipExport.canExport(lane: .chords, sectionId: 7, in: s), "a section the snapshot lacks")
  }

  func testASectionHandleWithARemovedOpenTrackExportsTheChords() throws {
    // The open track (9) is gone from the snapshot: the handle resolves to the chords, request and texts alike.
    let s = try fixture()
    assertFields(ClipExport.request(for: .openLaneSection(2), in: s, open: .track(9)), ["lanes": ["chords"], "sectionId": 2])
    XCTAssertEqual(ClipExport.text(for: .openLaneSection(2), in: s, open: .track(9)),
                   ExportGripText(enabled: true, label: "Drag the section Chorus to Live as a MIDI clip",
                                  help: "Drag this section onto a MIDI track in Live"))
    XCTAssertEqual(ClipExport.dragOutTitle(in: s, open: .track(9), preparing: false), "♫ 15 beats · drag to Live")
  }

  func testAnUnnamedSectionIsCalledThatInTheTexts() throws {
    var o = try fixtureObject()
    o["sections"] = [["id": 1, "name": "", "colourIndex": 0, "firstEventIndex": 0]]
    let s = try decode(o)
    XCTAssertEqual(ClipExport.text(for: .openLaneSection(1), in: s, open: .chords).label,
                   "Drag the unnamed section to Live as a MIDI clip")
  }

  // MARK: - The bottom drag-out's title

  func testTheDragOutTitle() throws {
    let s = try fixture()
    XCTAssertEqual(ClipExport.dragOutTitle(in: s, open: .chords, preparing: false), "♫ 15 beats · drag to Live")
    XCTAssertEqual(ClipExport.dragOutTitle(in: s, open: .track(1), preparing: false), "♫ Bass · 15 beats",
                   "with a track open the name takes the place of \"drag to Live\" (the 700-pt bottom row)")
    XCTAssertEqual(ClipExport.dragOutTitle(in: s, open: .chords, preparing: true), "♫ preparing…")
    XCTAssertEqual(ClipExport.dragOutTitle(in: s, open: .track(1), preparing: true), "♫ Bass · preparing…",
                   "a pending export keeps the track's prefix")
    XCTAssertEqual(ClipExport.dragOutTitle(in: try emptyFixture(), open: .chords, preparing: false), "♫ nothing to export")
    XCTAssertEqual(ClipExport.dragOutTitle(in: try fixture(trackIndex: 1, notes: []), open: .track(2), preparing: false),
                   "♫ Lead · no notes")
    XCTAssertEqual(ClipExport.dragOutTitle(in: nil, open: .chords, preparing: false), "♫ nothing to export")
  }

  func testTheDragOutTitleWritesFractionalBeatsAndShortensLongNames() throws {
    var o = try fixtureObject()
    o["endBeat"] = 14.5
    var tracks = try XCTUnwrap(o["tracks"] as? [[String: Any]])
    tracks[0]["name"] = "Walking bass line for the verse"
    o["tracks"] = tracks
    let s = try decode(o)
    XCTAssertEqual(ClipExport.dragOutTitle(in: s, open: .chords, preparing: false), "♫ 14.50 beats · drag to Live")
    XCTAssertEqual(ClipExport.dragOutTitle(in: s, open: .track(1), preparing: false),
                   "♫ Walking bass… · 14.50 beats")
    XCTAssertEqual(ClipExport.shortName("Walking bass line for the verse"), "Walking bass…")
    XCTAssertEqual(ClipExport.shortName("Bass 2"), "Bass 2")
    XCTAssertEqual(ClipExport.shortName("123456789012"), "123456789012", "twelve characters fit")
    XCTAssertEqual(ClipExport.shortName("1234567890123"), "123456789012…")
  }
}
