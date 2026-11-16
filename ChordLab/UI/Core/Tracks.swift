import Foundation

/// A lane of the editor (tracks design D13): the open tab is `chords` (suggestions, palette, home row) or one
/// track's piano roll. UI state in `ChordLabState`, never saved; a removed track falls back to `chords`.
enum Lane: Hashable {
  case chords
  case track(Int)

  /// The lane to show given the snapshot's tracks (design D13): `.track(id)` while a track with that id exists,
  /// else `.chords` (a removed track falls back to Chords; so does a track lane while no `tracks` have arrived);
  /// `.chords` stays `.chords`.
  func resolved(in tracks: [TrackJSON]?) -> Lane {
    guard case .track(let id) = self else { return self }
    return tracks?.contains { $0.id == id } == true ? self : .chords
  }
}

/// A track's role (tracks design D2): it sets defaults only (name, register, colour, guide emphasis, and which
/// lines the Generate menu offers: `GenerateOptions.families(for:)`, design D22), never what the user may write or
/// what a command accepts. The raw values are the wire names the commands send (`addTrack`, `patchTrack`) and the
/// snapshot carries.
///
/// Decoding is tolerant: a role this UI does not know (a newer plug-in's) is `.other`, so one unfamiliar track
/// never loses the whole snapshot. Encoding writes the raw value.
enum TrackRole: String, Codable, CaseIterable {
  case bass, lead, other

  init(from decoder: Decoder) throws {
    let text = try decoder.singleValueContainer().decode(String.self)
    self = TrackRole(rawValue: text) ?? .other
  }

  func encode(to encoder: Encoder) throws {
    var container = encoder.singleValueContainer()
    try container.encode(rawValue)
  }

  /// The MIDI notes the roll scrolls to when the track first opens (design D2): Bass E1-G3 (28...55), Lead
  /// C4-C6 (60...84), Other C3-C5 (48...72). A scroll target only; every pitch 0...127 stays writable.
  var register: ClosedRange<Int> {
    switch self {
    case .bass: return 28...55
    case .lead: return 60...84
    case .other: return 48...72
    }
  }

  /// The role's name as the + menu and the Role submenu show it (D13): "Bass", "Lead", "Other". Default track
  /// names come from the engine (D2: Bass, Lead, Track), not from here.
  var displayName: String {
    switch self {
    case .bass: return "Bass"
    case .lead: return "Lead"
    case .other: return "Other"
    }
  }
}

/// One note of a track as the roll uses it, built on demand from `TrackNotesJSON`'s columns. Ticks are
/// integers on the snapshot's `ticksPerBeat` grid; `start` is the derived absolute tick (the engine resolves
/// the anchor, the UI never does); `anchor` is the id of the chord the note belongs to, `offset` the ticks
/// into it. An inactive note (its chord is shorter than its offset) is kept and drawn but not played.
struct RollNote: Equatable, Identifiable {
  let id: Int
  let pitch: Int
  let start: Int
  let length: Int
  let velocity: Int
  let anchor: Int
  let offset: Int
  let active: Bool

  /// The tick the note ends on: `start + length`.
  var end: Int { start + length }
}

extension Snapshot {
  /// The track with this id, nil for none (or when the snapshot has no `tracks`).
  func track(id: Int) -> TrackJSON? { tracks?.first { $0.id == id } }

  /// The tick grid of every tick field: `ticksPerBeat`, or 960 (the engine's `kFeelTicksPerBeat`) when an
  /// older plug-in build sends none.
  var ticksPerBeatOrDefault: Int { ticksPerBeat ?? 960 }

  /// One bar in ticks: `RollGrid.barTicks` of the snapshot's meter at `ticksPerBeatOrDefault`; 4/4 (3840 at
  /// 960) when an older plug-in build sends no meter.
  var barTicksOrDefault: Int {
    RollGrid.barTicks(num: meter?.num ?? 4, den: meter?.den ?? 4, ticksPerBeat: ticksPerBeatOrDefault)
  }

  /// The shortest note the engine accepts: `trackLimits.minNoteTicks`, or 30 (`TrackLimitsJSON.defaultMinNoteTicks`)
  /// when an older plug-in build sends no limits.
  var minNoteTicksOrDefault: Int { trackLimits?.minNoteTicks ?? TrackLimitsJSON.defaultMinNoteTicks }
}

/// The tick quantities the roll's snapping, insert and drag rules share, read once from a snapshot so that no
/// caller can pass the grid one meter and the clamp another (tracks design D14, D15). Every field is an integer
/// tick count at `ticksPerBeat`; an older plug-in build that sends none of the keys gets 960 ticks per beat,
/// 4/4 (3840), the minimum 30, and its `endBeat` converted at 960.
struct RollTiming: Equatable {
  /// The snapshot's tick grid (at least 1); 960 in the engine.
  let ticksPerBeat: Int
  /// One bar in ticks, `num × 4 × ticksPerBeat / den` (the engine's `barTicks`): the grid counts from each bar
  /// start (`RollGrid`).
  let barTicks: Int
  /// The progression's end, `round(endBeat × ticksPerBeat)` (a negative or non-finite `endBeat` is 0): where the
  /// hatch starts and what the insert rule and the group clamps keep every start below.
  let endTick: Int
  /// The shortest note: `trackLimits.minNoteTicks`.
  let minNoteTicks: Int
  /// The roll's content end: the later of `endTick` and the latest note end (`start + length`) of ALL tracks. The
  /// engine keeps notes past the progression's end (an inactive note after the last chord was shortened, a long
  /// note left by a deleted chord, a note resized past the end; design D3), and they stay reachable: the roll
  /// scrolls to `contentEndTick` plus one bar (`RollViewState.contentSize`). Never below `endTick`.
  let contentEndTick: Int

  /// `contentEndTick` nil (or below `endTick`) is `endTick`.
  init(ticksPerBeat: Int, barTicks: Int, endTick: Int, minNoteTicks: Int, contentEndTick: Int? = nil) {
    self.ticksPerBeat = ticksPerBeat
    self.barTicks = barTicks
    self.endTick = endTick
    self.minNoteTicks = minNoteTicks
    self.contentEndTick = max(endTick, contentEndTick ?? endTick)
  }

  /// The snapshot's timing; `contentEndTick` walks every track's notes once (O(notes)), so callers that need it
  /// per frame keep the result per snapshot.
  init(snapshot: Snapshot) {
    let rate = max(1, snapshot.ticksPerBeatOrDefault)
    let end = snapshot.endBeat * Double(rate)
    ticksPerBeat = rate
    barTicks = snapshot.barTicksOrDefault
    endTick = end.isFinite && end > 0 ? Int(min(end, 1e15).rounded()) : 0
    minNoteTicks = snapshot.minNoteTicksOrDefault
    contentEndTick = max(endTick, Self.latestNoteEnd(snapshot.tracks ?? []))
  }

  /// The latest `start + length` over every note of `tracks`, at most the engine's `kMaxNoteTick`
  /// (`RollLimits.engineMaxNoteTick`, so a malformed note cannot make the content overflow); 0 without notes.
  static func latestNoteEnd(_ tracks: [TrackJSON]) -> Int {
    var latest = 0
    for track in tracks {
      let notes = track.notes
      for i in 0..<notes.count {
        let (end, overflow) = notes.start[i].addingReportingOverflow(notes.length[i])
        latest = max(latest, overflow ? Int.max : end)
      }
    }
    return min(latest, RollLimits.engineMaxNoteTick)
  }
}

extension TrackJSON {
  /// The ids of this track's selected notes: the selection's `ids` when its `trackId` is this track, else
  /// empty (the selection lives in one track at a time, design D5; nil is no selection).
  func selectedIds(in selection: NoteSelectionJSON?) -> Set<Int> {
    guard let selection, selection.trackId == id else { return [] }
    return Set(selection.ids)
  }
}
