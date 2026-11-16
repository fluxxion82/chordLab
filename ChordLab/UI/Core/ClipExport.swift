import Foundation

/// A drag-out control (tracks design D12, D13): what the user grabs, before it is resolved against the snapshot.
enum ExportGrip: Equatable {
  /// A tab's grip: that lane over the whole progression, whichever lane is open.
  case lane(Lane)
  /// The All grip at the tab row's trailing end: the chords plus every track with notes, muted ones included (Q4).
  case all
  /// The bottom drag-out: the open lane over the whole progression.
  case openLane
  /// A section handle (♫ in the sections header): the open lane over that section (its stable id).
  case openLaneSection(Int)
}

/// One `kMsgTagExportClip` request (D12): which lanes, over the whole progression (`sectionId` nil) or one section.
struct ClipExportRequest: Equatable {
  enum Lanes: Equatable {
    /// `"lanes":"all"`: the shell's `exportClip` picks the chords and every non-empty track.
    case all
    /// `"lanes":[…]`: these lanes in this order (the grips send one).
    case some([Lane])
  }

  let lanes: Lanes
  let sectionId: Int?

  /// The message's fields without `"schema"` (`ChordLabState` adds it): `{"lanes":["chords"|<trackId>,…]|"all",
  /// "sectionId":<id>|null}`, exactly what `parseClipExportRequest` (ChordLab/bridge/export_request.h) reads.
  var fields: [String: Any] {
    let lanesValue: Any
    switch lanes {
    case .all:
      lanesValue = "all"
    case .some(let list):
      lanesValue = list.map { lane -> Any in
        switch lane {
        case .chords: return "chords"
        case .track(let id): return id
        }
      }
    }
    return ["lanes": lanesValue, "sectionId": sectionId.map { $0 as Any } ?? NSNull()]
  }
}

/// What a grip shows and speaks: whether it can be dragged, its accessibility label and its `.help` text (an inert
/// grip says why).
struct ExportGripText: Equatable {
  let enabled: Bool
  let label: String
  let help: String
}

/// The drag-outs' rules (tracks design D12, D13), so the views only read and call this: the request each grip sends
/// through `kMsgTagExportClip`, whether a lane has anything to export, the grips' texts and the bottom drag-out's
/// title.
///
/// **Enablement over-approximates `exportClip`** (engine/include/chordlab/export.h): a grip is live whenever the engine
/// may write a note, and inert only when it certainly writes none. The chords lane needs chords; a track needs an
/// **active** note that starts before the progression's end and overlaps the span (the whole progression, or the
/// section from its first chord's playback start to the next section's, `RollRibbon.sectionSpans`); All needs chords
/// (the chords lane is always in it). The engine's later steps (the whole-track same-pitch merge, the clip at the end,
/// the cut to the span and the drop of a cut remainder under `kMinNoteTicks`) can only remove notes, so they are not
/// modelled: when they leave nothing (a sliver), the shell answers "nothing to export" (`kMsgTagError`), the drag
/// carries nothing and the drag-out's title stays as it was (`ExportOutcome.failed`, `ClipExportExchange`).
///
/// Depends on nothing but Foundation; unit-tested by the `ChordLabUICore` package
/// (Tests/ChordLabUICoreTests/ClipExportTests.swift).
enum ClipExport {
  /// How many characters of a track's name the bottom drag-out's title shows before an ellipsis.
  static let titleNameLength = 12

  // MARK: - Requests

  /// The request `grip` sends with `snapshot` shown and `open` the open lane; nil when there is nothing to export
  /// (then the grip is inert and sends nothing): no snapshot, a lane or section the snapshot lacks, or a lane with
  /// nothing in its span (`canExport`). The open lane is resolved against the snapshot's tracks first (a removed
  /// track falls back to the chords, `Lane.resolved`).
  static func request(for grip: ExportGrip, in snapshot: Snapshot?, open: Lane) -> ClipExportRequest? {
    guard let snapshot else { return nil }
    switch grip {
    case .all:
      return canExportAll(snapshot) ? ClipExportRequest(lanes: .all, sectionId: nil) : nil
    case .lane(let lane):
      return laneRequest(lane, sectionId: nil, in: snapshot)
    case .openLane:
      return laneRequest(open.resolved(in: snapshot.tracks), sectionId: nil, in: snapshot)
    case .openLaneSection(let id):
      return laneRequest(open.resolved(in: snapshot.tracks), sectionId: id, in: snapshot)
    }
  }

  private static func laneRequest(_ lane: Lane, sectionId: Int?, in snapshot: Snapshot) -> ClipExportRequest? {
    canExport(lane: lane, sectionId: sectionId, in: snapshot) ? ClipExportRequest(lanes: .some([lane]), sectionId: sectionId) : nil
  }

  // MARK: - Enablement

  /// Whether All has anything to export: the snapshot has chords.
  static func canExportAll(_ snapshot: Snapshot?) -> Bool {
    !(snapshot?.events.isEmpty ?? true)
  }

  /// Whether `lane` may have anything to export over the whole progression (`sectionId` nil) or that section: the
  /// chords lane when there are chords (and the section exists); a track when it exists and has an active note that
  /// starts before the progression's end and overlaps the span (see the type's note). False for a track or section
  /// the snapshot lacks.
  static func canExport(lane: Lane, sectionId: Int?, in snapshot: Snapshot?) -> Bool {
    guard let snapshot, let span = span(sectionId: sectionId, in: snapshot) else { return false }
    switch lane {
    case .chords:
      return true
    case .track(let id):
      guard let track = snapshot.track(id: id) else { return false }
      let endTick = endTick(of: snapshot)
      return track.notes.contains { $0.active && $0.start < endTick && overlaps($0, span) }
    }
  }

  /// The span in ticks of the whole progression (`sectionId` nil: `[0, endTick)`) or of that section; nil without
  /// chords, for a section the snapshot lacks, or for an empty span.
  private static func span(sectionId: Int?, in snapshot: Snapshot) -> Range<Int>? {
    guard !snapshot.events.isEmpty else { return nil }
    let span: Range<Int>
    if let sectionId {
      guard let section = RollRibbon.sectionSpans(snapshot).first(where: { $0.id == sectionId }) else { return nil }
      span = section.ticks
    } else {
      span = 0..<max(0, endTick(of: snapshot))
    }
    return span.isEmpty ? nil : span
  }

  private static func endTick(of snapshot: Snapshot) -> Int {
    Int((snapshot.endBeat * Double(snapshot.ticksPerBeatOrDefault)).rounded())
  }

  /// Whether `note` (its whole length, half-open) overlaps `span`.
  private static func overlaps(_ note: RollNote, _ span: Range<Int>) -> Bool {
    note.start < span.upperBound && note.end > span.lowerBound
  }

  /// Whether an inert track still shows notes where the grip looks (any note, active or not): anywhere for the
  /// whole progression (a note past the end is drawn there too), overlapping the section for a section. Such a
  /// grip says the track has no **sounding** notes rather than asking for notes the user can see.
  private static func showsNotes(trackId: Int, sectionId: Int?, in snapshot: Snapshot?) -> Bool {
    guard let snapshot, let track = snapshot.track(id: trackId) else { return false }
    guard let sectionId else { return !track.notes.isEmpty }
    guard let span = span(sectionId: sectionId, in: snapshot) else { return false }
    return track.notes.contains { overlaps($0, span) }
  }

  // MARK: - Texts

  /// What `grip` shows and speaks (accessibility label and `.help`), enabled exactly when `request` gives a request.
  static func text(for grip: ExportGrip, in snapshot: Snapshot?, open: Lane) -> ExportGripText {
    let enabled = request(for: grip, in: snapshot, open: open) != nil
    switch grip {
    case .all:
      return enabled
        ? ExportGripText(enabled: true, label: "Drag all lanes to Live, one MIDI clip per lane",
                         help: "Drag all lanes to Live, one MIDI clip per lane, muted lanes included")
        : ExportGripText(enabled: false, label: "All lanes: nothing to export", help: "Add chords to export")
    case .lane(let lane):
      switch lane {
      case .chords:
        return enabled
          ? ExportGripText(enabled: true, label: "Drag the chords to Live as a MIDI clip",
                           help: "Drag the chords to Live as a MIDI clip")
          : ExportGripText(enabled: false, label: "Chords: nothing to export", help: "Add chords to export")
      case .track(let id):
        let name = trackLabel(id, in: snapshot)
        return enabled
          ? ExportGripText(enabled: true, label: "Drag the \(name) track to Live as a MIDI clip",
                           help: "Drag the \(name) track to Live as a MIDI clip")
          : ExportGripText(enabled: false, label: "\(name): nothing to export",
                           help: inertTrackHelp(name, trackId: id, in: snapshot))
      }
    case .openLane:
      switch open.resolved(in: snapshot?.tracks) {
      case .chords:
        return enabled
          ? ExportGripText(enabled: true, label: "Drag the progression to Live as a MIDI clip",
                           help: "Drag onto a MIDI track in Live")
          : ExportGripText(enabled: false, label: "Nothing to export", help: "Add chords to export")
      case .track(let id):
        let name = trackLabel(id, in: snapshot)
        return enabled
          ? ExportGripText(enabled: true, label: "Drag the \(name) track to Live as a MIDI clip",
                           help: "Drag the \(name) track onto a MIDI track in Live")
          : ExportGripText(enabled: false, label: "Nothing to export from \(name)",
                           help: inertTrackHelp(name, trackId: id, in: snapshot))
      }
    case .openLaneSection(let sectionId):
      let section = snapshot?.sections?.first { $0.id == sectionId }
      let sectionName = section.map { $0.name.isEmpty ? "the unnamed section" : "the section " + $0.name } ?? "the section"
      switch open.resolved(in: snapshot?.tracks) {
      case .chords:
        return ExportGripText(enabled: enabled,
                              label: enabled ? "Drag \(sectionName) to Live as a MIDI clip" : "Nothing to export in \(sectionName)",
                              help: enabled ? "Drag this section onto a MIDI track in Live" : "Nothing to export in this section")
      case .track(let id):
        let name = trackLabel(id, in: snapshot)
        return enabled
          ? ExportGripText(enabled: true, label: "Drag \(name) in \(sectionName) to Live as a MIDI clip",
                           help: "Drag \(name) in this section onto a MIDI track in Live")
          : showsNotes(trackId: id, sectionId: sectionId, in: snapshot)
            ? ExportGripText(enabled: false, label: "\(name) has no sounding notes in \(sectionName)",
                             help: "\(name) has no sounding notes in this section")
            : ExportGripText(enabled: false, label: "\(name) has no notes in \(sectionName)",
                             help: "\(name) has no notes in this section")
      }
    }
  }

  /// An inert whole-progression track grip's help: "Bass has no sounding notes to export" when the track has notes
  /// (all inactive, or past the end), else "Add notes to Bass to export".
  private static func inertTrackHelp(_ name: String, trackId: Int, in snapshot: Snapshot?) -> String {
    showsNotes(trackId: trackId, sectionId: nil, in: snapshot)
      ? "\(name) has no sounding notes to export" : "Add notes to \(name) to export"
  }

  /// The track's tab label (`LaneTabs.label`: its name, or its role's while the name is empty); "Track" when the
  /// snapshot lacks it.
  private static func trackLabel(_ id: Int, in snapshot: Snapshot?) -> String {
    snapshot?.track(id: id).map(LaneTabs.label(for:)) ?? "Track"
  }

  // MARK: - The bottom drag-out's title

  /// "♫ 8 beats · drag to Live" with the chords open; "♫ Bass · 8 beats" with a track open (the name shortened by
  /// `shortName`; it takes the place of "drag to Live", so the title fits beside the counts line and an error at the
  /// 700-pt minimum width, and the help still says where to drag); "♫ preparing…" ("♫ Bass · preparing…") while a
  /// requested file has not arrived (only for `ExportOutcome.pending`: a failed export keeps the title); "♫ nothing to
  /// export" without chords, "♫ Bass · no notes" for an open track with nothing to export. Whole beats print as
  /// integers, others with two decimals.
  static func dragOutTitle(in snapshot: Snapshot?, open: Lane, preparing: Bool) -> String {
    guard let snapshot, !snapshot.events.isEmpty else { return "♫ nothing to export" }
    let lane = open.resolved(in: snapshot.tracks)
    let enabled = request(for: .openLane, in: snapshot, open: open) != nil
    var prefix = "♫ "
    if case .track(let id) = lane {
      prefix += shortName(trackLabel(id, in: snapshot)) + " · "
      if !enabled { return prefix + "no notes" }
    }
    if preparing { return prefix + "preparing…" }
    let endBeat = snapshot.endBeat
    let beats = endBeat == endBeat.rounded() ? String(Int(endBeat)) : String(format: "%.2f", endBeat)
    return prefix + "\(beats) beats" + (lane == .chords ? " · drag to Live" : "")
  }

  /// `name` cut to `titleNameLength` characters plus "…" when longer (trailing spaces of the cut dropped).
  static func shortName(_ name: String) -> String {
    guard name.count > titleNameLength else { return name }
    var cut = String(name.prefix(titleNameLength))
    while cut.last?.isWhitespace == true { cut.removeLast() }
    return cut + "…"
  }
}
