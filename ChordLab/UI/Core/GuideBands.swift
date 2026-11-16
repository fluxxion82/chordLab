import CoreGraphics
import Foundation

/// The harmonic guide bands behind the piano roll's notes, laid out once per snapshot revision, zoom and track
/// role (tracks design D14, D9, D6). One `Band` per chord that has a playback span and a guide, in playback order:
/// its span in ticks (`playStart`…`playEnd`, never the authored `startBeat`, so the shading matches what sounds)
/// and in content x, and a 12-entry table by absolute pitch class of the guide role, the fill opacity
/// (`GuideStyle.opacity(role:track:)`; Outside 0, so it draws nothing) and the degree label (the glyph degree on
/// the Root, ChordTone and Tension rows when the rows are at least 12 pt tall, `GuideStyle.showsDegreeLabel`, else
/// empty).
/// No rectangle per row is materialised: the canvas walks the bands that intersect the visible ticks
/// (`indices(intersecting:)`) and, for each visible row, looks its pitch class up in the band's table.
///
/// The key (`Key`) is what the layout depends on: the snapshot's content revision (every chord timing, quality,
/// feel or key change commits a new one), with the event count and `endBeat` as a guard, the zoom, the tick grid
/// and the track role. `GuideBandsCache` holds the last layout and rebuilds it only when the key changes (always
/// when the snapshot has no revision).
///
/// Depends on nothing but CoreGraphics and Foundation; unit-tested by the `ChordLabUICore` package
/// (Tests/ChordLabUICoreTests/GuideBandsTests.swift).
struct GuideBands: Equatable {
  /// What a layout depends on.
  struct Key: Equatable {
    let revision: UInt64?
    let eventCount: Int
    let endBeat: Double
    let pxPerBeat: CGFloat
    let rowHeight: CGFloat
    let ticksPerBeat: Int
    let trackRole: TrackRole
  }

  /// One chord's band.
  struct Band: Equatable {
    /// The chord's index in the snapshot's `events`.
    let eventIndex: Int
    /// The playback span in ticks, half-open.
    let ticks: Range<Int>
    /// The span in content x.
    let minX: CGFloat
    let maxX: CGFloat
    /// By pitch class (C = 0): the guide role, the fill opacity and the degree label ("" for none).
    let roles: [GuideRole]
    let opacities: [Double]
    let labels: [String]
    /// The pitch classes that draw a fill (opacity above 0), ascending.
    let drawnPitchClasses: [Int]
  }

  /// A degree label at the left of a band, on one row.
  struct DegreeLabel: Equatable {
    let pitch: Int
    let text: String
    let role: GuideRole
    /// The band's left edge in content x.
    let x: CGFloat
  }

  let key: Key
  let bands: [Band]

  /// Whether this layout's rows carry labels (rows at least 12 pt tall).
  var showsLabels: Bool { key.rowHeight >= GuideStyle.minLabelRowHeight }

  /// The layout of `snapshot`'s chords for a track of role `trackRole` at `geometry`'s zoom.
  init(snapshot: Snapshot, geometry: RollGeometry, trackRole: TrackRole) {
    self.init(events: snapshot.events, ticksPerBeat: snapshot.ticksPerBeatOrDefault, revision: snapshot.revision,
              endBeat: snapshot.endBeat, geometry: geometry, trackRole: trackRole)
  }

  init(events: [EventJSON], ticksPerBeat: Int, revision: UInt64?, endBeat: Double, geometry: RollGeometry,
       trackRole: TrackRole) {
    key = Key(revision: revision, eventCount: events.count, endBeat: endBeat, pxPerBeat: geometry.pxPerBeat,
              rowHeight: geometry.rowHeight, ticksPerBeat: ticksPerBeat, trackRole: trackRole)
    var bands: [Band] = []
    bands.reserveCapacity(events.count)
    for (index, event) in events.enumerated() {
      guard let ticks = event.playTicks(ticksPerBeat: ticksPerBeat), let guide = event.guide else { continue }
      var roles: [GuideRole] = []
      var opacities: [Double] = []
      var labels: [String] = []
      var drawn: [Int] = []
      for pc in 0..<12 {
        let role = guide.role(pc: pc)
        let opacity = GuideStyle.opacity(role: role, track: trackRole)
        roles.append(role)
        opacities.append(opacity)
        labels.append(GuideStyle.showsDegreeLabel(role: role, rowHeight: geometry.rowHeight) ? guide.displayDegree(pc: pc) : "")
        if opacity > 0 { drawn.append(pc) }
      }
      bands.append(Band(eventIndex: index, ticks: ticks, minX: geometry.x(tick: ticks.lowerBound),
                        maxX: geometry.x(tick: ticks.upperBound), roles: roles, opacities: opacities, labels: labels,
                        drawnPitchClasses: drawn))
    }
    self.bands = bands
  }

  /// The key a layout of these inputs would have.
  static func key(snapshot: Snapshot, geometry: RollGeometry, trackRole: TrackRole) -> Key {
    Key(revision: snapshot.revision, eventCount: snapshot.events.count, endBeat: snapshot.endBeat,
        pxPerBeat: geometry.pxPerBeat, rowHeight: geometry.rowHeight, ticksPerBeat: snapshot.ticksPerBeatOrDefault,
        trackRole: trackRole)
  }

  /// The bands whose span intersects the half-open tick `range`, as indices into `bands` (the same rule as
  /// `PlaySpans.eventIndices(intersecting:in:ticksPerBeat:)`: a band touches `[a, b)` when it starts before `b`
  /// and ends after `a`), by binary search.
  func indices(intersecting range: Range<Int>) -> Range<Int> {
    guard !range.isEmpty else { return 0..<0 }
    let first = partition { $0.ticks.upperBound > range.lowerBound }
    let last = partition { $0.ticks.lowerBound >= range.upperBound }
    return first..<max(first, last)
  }

  /// The degree labels of `band` on the rows of `pitches` (clamped to 0...127), ascending by pitch: every row whose
  /// pitch class has a label. Empty under 12-pt rows.
  func labels(in band: Band, pitches: ClosedRange<Int>) -> [DegreeLabel] {
    let low = max(pitches.lowerBound, 0)
    let high = min(pitches.upperBound, RollGeometry.pitchCount - 1)
    guard showsLabels, low <= high else { return [] }
    var result: [DegreeLabel] = []
    for pitch in low...high {
      let pc = pitch % 12
      let text = band.labels[pc]
      if !text.isEmpty { result.append(DegreeLabel(pitch: pitch, text: text, role: band.roles[pc], x: band.minX)) }
    }
    return result
  }

  private func partition(_ condition: (Band) -> Bool) -> Int {
    var low = 0
    var high = bands.count
    while low < high {
      let middle = (low + high) / 2
      if condition(bands[middle]) { high = middle } else { low = middle + 1 }
    }
    return low
  }
}

/// The last guide band layout (tracks design D14: "cached per snapshot revision and zoom"): `bands` returns it while
/// the key is unchanged and builds a new one otherwise; a snapshot without a revision (an older plug-in build)
/// always builds, since its key cannot tell a chord edit from no change. A reference type so the roll view can keep
/// it across body evaluations without publishing anything; main-thread use only.
final class GuideBandsCache {
  private(set) var current: GuideBands?
  /// How many layouts were built (tests and the harness read it).
  private(set) var builds = 0

  init() {}

  func bands(snapshot: Snapshot, geometry: RollGeometry, trackRole: TrackRole) -> GuideBands {
    let key = GuideBands.key(snapshot: snapshot, geometry: geometry, trackRole: trackRole)
    // Without a revision (an older plug-in build) a chord edit that keeps the count and the end would look like
    // the same key: never reuse such a layout.
    if key.revision != nil, let current, current.key == key { return current }
    let layout = GuideBands(snapshot: snapshot, geometry: geometry, trackRole: trackRole)
    current = layout
    builds += 1
    return layout
  }
}
