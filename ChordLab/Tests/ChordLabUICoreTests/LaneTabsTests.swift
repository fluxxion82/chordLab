import Foundation
import SwiftUI
import XCTest
@testable import ChordLabUICore

/// The lane tab row's rules (UI/Core/LaneTabs.swift, tracks design D13): the tabs a snapshot gives, the + menu,
/// the track menu, the commands the views send, the rename rule, which track opens after an add, and the track
/// colours (`TrackStyle`).
final class LaneTabsTests: XCTestCase {
  // MARK: - Fixtures

  private static let noNotes = TrackNotesJSON(id: [], pitch: [], start: [], length: [], velocity: [], anchor: [],
                                              offset: [], active: [])

  private func track(_ id: Int, _ name: String, _ role: TrackRole, mute: Bool = false, solo: Bool = false,
                     audible: Bool = true) -> TrackJSON {
    TrackJSON(id: id, name: name, role: role, mute: mute, solo: solo, audible: audible, notes: Self.noNotes)
  }

  /// The inline Phase 2 snapshot with these tracks and chords flags; `tracks` nil leaves every tracks key out
  /// (an older engine).
  private func snapshot(tracks: [TrackJSON]?, chordsMuted: Bool? = false, chordsSolo: Bool? = false,
                        chordsAudible: Bool? = true, maxTracks: Int? = 4) throws -> Snapshot {
    var snapshot = try SnapshotTests.decode(Snapshot.self, SnapshotTests.phase2)
    snapshot.tracks = tracks
    if tracks != nil {
      snapshot.chordsMuted = chordsMuted
      snapshot.chordsSolo = chordsSolo
      snapshot.chordsAudible = chordsAudible
      if let maxTracks {
        snapshot.trackLimits = TrackLimitsJSON(maxTracks: maxTracks, maxNotesPerTrack: 1024, minNoteTicks: 30,
                                               maxPolyphony: 8, maxOnsetsPerBeat: 15)
      }
    }
    return snapshot
  }

  private func assertCommand(_ command: [String: Any]?, _ expected: [String: Any],
                             file: StaticString = #filePath, line: UInt = #line) {
    guard let command else { return XCTFail("no command", file: file, line: line) }
    XCTAssertTrue(NSDictionary(dictionary: command).isEqual(to: expected), "\(command) != \(expected)",
                  file: file, line: line)
  }

  // MARK: - Tabs

  func testChordsComesFirstThenTheTracksInSnapshotOrder() throws {
    let s = try snapshot(tracks: [track(3, "Lead", .lead), track(1, "Bass", .bass), track(7, "Pad", .other)])
    let tabs = LaneTabs.tabs(in: s, open: .chords)
    XCTAssertEqual(tabs.map(\.lane), [.chords, .track(3), .track(1), .track(7)])
    XCTAssertEqual(tabs.map(\.label), ["Chords", "Lead", "Bass", "Pad"])
    XCTAssertEqual(tabs.map(\.role), [nil, .lead, .bass, .other])
    XCTAssertEqual(tabs.map(\.trackId), [nil, 3, 1, 7])
    XCTAssertEqual(tabs.map(\.id), tabs.map(\.lane))
  }

  func testTrackTabsCarryTheirMuteSoloAndAudibleAndDimWhenNotAudible() throws {
    let s = try snapshot(tracks: [track(1, "Bass", .bass, mute: true, audible: false),
                                  track(2, "Lead", .lead, solo: true, audible: true)])
    let tabs = LaneTabs.tabs(in: s, open: .chords)
    XCTAssertEqual(tabs[1].muted, true)
    XCTAssertEqual(tabs[1].soloed, false)
    XCTAssertEqual(tabs[1].audible, false)
    XCTAssertTrue(tabs[1].dimmed)
    XCTAssertEqual(tabs[2].muted, false)
    XCTAssertEqual(tabs[2].soloed, true)
    XCTAssertFalse(tabs[2].dimmed)
    XCTAssertTrue(tabs[1].hasMuteSolo && tabs[2].hasMuteSolo)
  }

  func testTheChordsTabReadsTheChordsFlags() throws {
    let s = try snapshot(tracks: [track(2, "Lead", .lead, solo: true)], chordsMuted: false, chordsSolo: false,
                         chordsAudible: false)
    let chords = LaneTabs.tabs(in: s, open: .chords)[0]
    XCTAssertEqual(chords.label, "Chords")
    XCTAssertFalse(chords.muted)
    XCTAssertFalse(chords.soloed)
    XCTAssertFalse(chords.audible, "a soloed track silences the chords: the engine's outcome, never derived here")
    XCTAssertTrue(chords.dimmed)
    XCTAssertTrue(chords.hasMuteSolo)

    let mutedAndSoloed = try snapshot(tracks: [], chordsMuted: true, chordsSolo: true, chordsAudible: false)
    let tab = LaneTabs.tabs(in: mutedAndSoloed, open: .chords)[0]
    XCTAssertTrue(tab.muted && tab.soloed && tab.dimmed)
  }

  func testExactlyTheOpenTabIsOpen() throws {
    let s = try snapshot(tracks: [track(1, "Bass", .bass), track(2, "Lead", .lead)])
    XCTAssertEqual(LaneTabs.tabs(in: s, open: .chords).map(\.isOpen), [true, false, false])
    XCTAssertEqual(LaneTabs.tabs(in: s, open: .track(2)).map(\.isOpen), [false, false, true])
  }

  func testAnOpenLaneTheSnapshotLacksShowsChordsOpen() throws {
    let s = try snapshot(tracks: [track(1, "Bass", .bass)])
    XCTAssertEqual(LaneTabs.tabs(in: s, open: .track(9)).map(\.isOpen), [true, false],
                   "the same fallback as Lane.resolved: a removed track opens Chords")
  }

  func testAnEmptyTrackNameShowsTheRoleName() throws {
    let s = try snapshot(tracks: [track(1, "", .bass), track(2, "", .other)])
    XCTAssertEqual(LaneTabs.tabs(in: s, open: .chords).map(\.label), ["Chords", "Bass", "Other"])
  }

  func testAnEmptyTrackListIsTheChordsTabAlone() throws {
    let s = try snapshot(tracks: [])
    XCTAssertEqual(LaneTabs.tabs(in: s, open: .chords).map(\.lane), [.chords])
    XCTAssertTrue(LaneTabs.showsTracks(s))
  }

  func testAnOlderEngineWithoutTracksShowsChordsAloneWithoutMuteSoloOrPlus() throws {
    let s = try snapshot(tracks: nil)
    let tabs = LaneTabs.tabs(in: s, open: .track(1))
    XCTAssertEqual(tabs.map(\.lane), [.chords])
    XCTAssertTrue(tabs[0].isOpen)
    XCTAssertFalse(tabs[0].hasMuteSolo, "no chordsMuted/chordsSolo: the engine would refuse the commands")
    XCTAssertFalse(tabs[0].dimmed, "no chordsAudible: the chords play")
    XCTAssertFalse(LaneTabs.showsTracks(s))
    XCTAssertFalse(LaneTabs.canAddTrack(s))
  }

  func testNoSnapshotYetIsTheChordsTabAlone() {
    let tabs = LaneTabs.tabs(in: nil, open: .chords)
    XCTAssertEqual(tabs.map(\.lane), [.chords])
    XCTAssertFalse(tabs[0].hasMuteSolo)
    XCTAssertFalse(LaneTabs.showsTracks(nil))
  }

  // MARK: - The + menu

  func testThePlusMenuOffersBassLeadOtherEnabledBelowTheLimit() throws {
    let s = try snapshot(tracks: [track(1, "Bass", .bass), track(2, "Lead", .lead), track(3, "Track", .other)])
    let entries = LaneTabs.addEntries(s)
    XCTAssertEqual(entries.map(\.role), [.bass, .lead, .other])
    XCTAssertEqual(entries.map(\.title), ["Bass", "Lead", "Other"])
    XCTAssertEqual(entries.map(\.enabled), [true, true, true])
    XCTAssertTrue(LaneTabs.canAddTrack(s))
  }

  func testAllThreeAreDisabledAtTheEnginesLimit() throws {
    let four = (1...4).map { track($0, "T\($0)", .other) }
    let s = try snapshot(tracks: four)
    XCTAssertEqual(LaneTabs.addEntries(s).map(\.enabled), [false, false, false])
    XCTAssertFalse(LaneTabs.canAddTrack(s))

    let two = try snapshot(tracks: [track(1, "A", .bass), track(2, "B", .lead)], maxTracks: 2)
    XCTAssertFalse(LaneTabs.canAddTrack(two), "the snapshot's limit, never a number of the UI's own")
  }

  func testTheLimitIsFourWhenTheSnapshotSendsNone() throws {
    XCTAssertEqual(LaneTabs.defaultMaxTracks, 4)
    let three = try snapshot(tracks: (1...3).map { track($0, "T\($0)", .other) }, maxTracks: nil)
    XCTAssertEqual(LaneTabs.maxTracks(three), 4)
    XCTAssertTrue(LaneTabs.canAddTrack(three))
    let four = try snapshot(tracks: (1...4).map { track($0, "T\($0)", .other) }, maxTracks: nil)
    XCTAssertFalse(LaneTabs.canAddTrack(four))
  }

  func testAddSendsTheRoleAloneSoTheEngineNamesTheTrack() {
    assertCommand(LaneTabs.addCommand(role: .bass), ["cmd": "addTrack", "role": "bass"])
    assertCommand(LaneTabs.addCommand(role: .lead), ["cmd": "addTrack", "role": "lead"])
    assertCommand(LaneTabs.addCommand(role: .other), ["cmd": "addTrack", "role": "other"])
  }

  // MARK: - The track menu

  func testChordsHasNoTrackMenu() throws {
    let s = try snapshot(tracks: [track(1, "Bass", .bass)])
    XCTAssertNil(LaneTabs.menu(for: .chords, in: s))
    XCTAssertNil(LaneTabs.menu(for: .track(9), in: s), "no such track")
    XCTAssertNil(LaneTabs.menu(for: .track(1), in: nil))
  }

  func testMoveLeftIsOffForTheFirstTrackAndMoveRightForTheLast() throws {
    let s = try snapshot(tracks: [track(4, "Bass", .bass), track(2, "Lead", .lead), track(9, "Pad", .other)])
    let first = try XCTUnwrap(LaneTabs.menu(for: .track(4), in: s))
    XCTAssertNil(first.moveLeftIndex)
    XCTAssertEqual(first.moveRightIndex, 1)
    let middle = try XCTUnwrap(LaneTabs.menu(for: .track(2), in: s))
    XCTAssertEqual(middle.moveLeftIndex, 0)
    XCTAssertEqual(middle.moveRightIndex, 2)
    let last = try XCTUnwrap(LaneTabs.menu(for: .track(9), in: s))
    XCTAssertEqual(last.moveLeftIndex, 1)
    XCTAssertNil(last.moveRightIndex)

    let alone = try XCTUnwrap(LaneTabs.menu(for: .track(1), in: try snapshot(tracks: [track(1, "Bass", .bass)])))
    XCTAssertNil(alone.moveLeftIndex)
    XCTAssertNil(alone.moveRightIndex)
  }

  func testTheMenuNamesTheTrackAndChecksItsRole() throws {
    let s = try snapshot(tracks: [track(4, "Walking", .bass), track(2, "Top", .lead)])
    let menu = try XCTUnwrap(LaneTabs.menu(for: .track(2), in: s))
    XCTAssertEqual(menu.trackId, 2)
    XCTAssertEqual(menu.name, "Top")
    XCTAssertEqual(menu.role, .lead)
    XCTAssertEqual(TrackRole.allCases.map { menu.isChecked($0) }, [false, true, false])
  }

  func testMoveSendsTheTargetIndexInTheFinalOrder() {
    assertCommand(LaneTabs.moveCommand(trackId: 2, index: 0), ["cmd": "moveTrack", "id": 2, "index": 0])
  }

  func testRemoveSendsTheId() {
    assertCommand(LaneTabs.removeCommand(trackId: 7), ["cmd": "removeTrack", "id": 7])
  }

  func testPickingAnotherRoleSendsPatchTrackAndTheCheckedOneNothing() {
    assertCommand(LaneTabs.roleCommand(trackId: 3, role: .lead, current: .bass),
                  ["cmd": "patchTrack", "id": 3, "role": "lead"])
    XCTAssertNil(LaneTabs.roleCommand(trackId: 3, role: .bass, current: .bass))
  }

  // MARK: - Rename (the section rename's rule)

  func testRenameTrimsAndSendsPatchTrackWithTheName() {
    assertCommand(LaneTabs.renameCommand(trackId: 2, draft: "  Walking bass \n", current: "Bass"),
                  ["cmd": "patchTrack", "id": 2, "name": "Walking bass"])
  }

  func testABlankOrUnchangedDraftSendsNothing() {
    XCTAssertNil(LaneTabs.renameCommand(trackId: 2, draft: "", current: "Bass"))
    XCTAssertNil(LaneTabs.renameCommand(trackId: 2, draft: "   ", current: "Bass"))
    XCTAssertNil(LaneTabs.renameCommand(trackId: 2, draft: "Bass", current: "Bass"))
    XCTAssertNil(LaneTabs.renameCommand(trackId: 2, draft: " Bass ", current: "Bass"))
  }

  func testNamesAreCutTo64BytesOfUTF8AtACharacterBoundary() {
    XCTAssertEqual(LaneTabs.maxTrackNameBytes, 64, "kMaxTrackNameBytes, engine/include/chordlab/tracks.h")
    let ascii = String(repeating: "a", count: 70)
    XCTAssertEqual(LaneTabs.boundedName(ascii), String(repeating: "a", count: 64))
    // 21 three-byte characters are 63 bytes; the 22nd would straddle the bound and is dropped whole.
    let wide = String(repeating: "♫", count: 30)
    XCTAssertEqual(LaneTabs.boundedName(wide), String(repeating: "♫", count: 21))
    XCTAssertEqual(LaneTabs.boundedName("Bass"), "Bass")
    assertCommand(LaneTabs.renameCommand(trackId: 1, draft: ascii, current: "Bass"),
                  ["cmd": "patchTrack", "id": 1, "name": String(repeating: "a", count: 64)])
  }

  // MARK: - Mute and solo

  func testChordsMuteAndSoloSendTheChordsCommands() {
    assertCommand(LaneTabs.muteCommand(lane: .chords, on: true), ["cmd": "setChordsMute", "on": true])
    assertCommand(LaneTabs.soloCommand(lane: .chords, on: false), ["cmd": "setChordsSolo", "on": false])
  }

  func testTrackMuteAndSoloSendTheTrackCommands() {
    assertCommand(LaneTabs.muteCommand(lane: .track(3), on: false), ["cmd": "setTrackMute", "id": 3, "on": false])
    assertCommand(LaneTabs.soloCommand(lane: .track(3), on: true), ["cmd": "setTrackSolo", "id": 3, "on": true])
  }

  // MARK: - The track an add opens

  func testTheOneNewIdIsTheTrackToOpen() {
    XCTAssertEqual(LaneTabs.newTrackId(before: [1, 2], after: [1, 2, 5]), 5)
    XCTAssertEqual(LaneTabs.newTrackId(before: [], after: [1]), 1)
    XCTAssertEqual(LaneTabs.newTrackId(before: [2, 1], after: [3, 1, 2]), 3, "order does not matter")
  }

  func testNoNewIdOrSeveralOpenNothing() {
    XCTAssertNil(LaneTabs.newTrackId(before: [1, 2], after: [1, 2]), "the add was not this snapshot's")
    XCTAssertNil(LaneTabs.newTrackId(before: [1, 2], after: [1]))
    XCTAssertNil(LaneTabs.newTrackId(before: [1], after: [1, 2, 3]), "ambiguous: open nothing")
  }

  // MARK: - Track colours

  func testEachRoleHasItsDocumentedColour() {
    XCTAssertEqual(hex(TrackStyle.color(role: .bass)), "#8FD14F")
    XCTAssertEqual(hex(TrackStyle.color(role: .lead)), "#F07BAE")
    XCTAssertEqual(hex(TrackStyle.color(role: .other)), "#B9A3F0")
  }

  func testTrackColoursAreDistinctFromEachOtherAndTheGuidesAndThePalettes() {
    let tracks = TrackRole.allCases.compactMap { hex(TrackStyle.color(role: $0)) }
    XCTAssertEqual(Set(tracks).count, 3)
    let taken = Set(([GuideStyle.cool, GuideStyle.warm] + SectionStyle.colors).compactMap(hex))
    XCTAssertTrue(taken.isDisjoint(with: tracks))
  }
}
