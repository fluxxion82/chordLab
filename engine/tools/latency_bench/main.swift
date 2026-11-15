// The Swift half of the D6 latency gate (not a test): times decoding a snapshot the engine bench dumped.
// Compiled with the real decoder by decode_bench.sh: swiftc -O ChordLab/UI/Core/Snapshot.swift main.swift.
//
// Three numbers per run over the same bytes:
// - `Snapshot` (today's decoder, ChordLab/UI/Core/Snapshot.swift), which skips the tracks keys (M2 adds them);
// - `JSONSerialization.jsonObject`, the parse alone: the lower bound of any decoder of these bytes;
// - an M2 estimate: today's `Snapshot` plus mirrors of the tracks keys the design's D6 publishes (tracks with their
//   notes column-wise, the per-event guide and play span, the note selection and limits), decoded from the same
//   decoder. (The keyed-object notes shape the engine published first is gone: the bytes no longer carry it.)

import Foundation

/// A track's notes as the snapshot publishes them: one array per field, all the same length, in note order.
struct BenchNoteColumnsJSON: Decodable {
  let id: [Int]
  let pitch: [Int]
  let start: [Int]
  let length: [Int]
  let velocity: [Int]
  let anchor: [Int]
  let offset: [Int]
  let active: [Bool]
}

struct BenchTrackJSON: Decodable {
  let id: Int
  let name: String
  let role: String
  let mute: Bool
  let solo: Bool
  let audible: Bool
  let notes: BenchNoteColumnsJSON
}

struct BenchGuideJSON: Decodable {
  let roles: [String]
  let scale: Int
  let degrees: [String]
  let names: [String]
}

struct BenchEventExtrasJSON: Decodable {
  let playStart: Double
  let playEnd: Double
  let guide: BenchGuideJSON
}

struct BenchNoteSelectionJSON: Decodable {
  let trackId: Int
  let ids: [Int]
}

struct BenchTrackLimitsJSON: Decodable {
  let maxTracks: Int
  let maxNotesPerTrack: Int
  let minNoteTicks: Int
  let maxPolyphony: Int
  let maxOnsetsPerBeat: Int
}

/// What M2's `Snapshot` will decode: today's fields plus the tracks keys, from one decoder (one parse).
struct M2Snapshot: Decodable {
  let base: Snapshot
  let ticksPerBeat: Int
  let revision: Int
  let tracks: [BenchTrackJSON]
  let chordsMuted: Bool
  let chordsSolo: Bool
  let chordsAudible: Bool
  let noteSelection: BenchNoteSelectionJSON?
  let trackLimits: BenchTrackLimitsJSON
  let eventExtras: [BenchEventExtrasJSON]

  enum CodingKeys: String, CodingKey {
    case ticksPerBeat, revision, tracks, chordsMuted, chordsSolo, chordsAudible, noteSelection, trackLimits, events
  }

  init(from decoder: Decoder) throws {
    base = try Snapshot(from: decoder)
    let c = try decoder.container(keyedBy: CodingKeys.self)
    ticksPerBeat = try c.decode(Int.self, forKey: .ticksPerBeat)
    revision = try c.decode(Int.self, forKey: .revision)
    tracks = try c.decode([BenchTrackJSON].self, forKey: .tracks)
    chordsMuted = try c.decode(Bool.self, forKey: .chordsMuted)
    chordsSolo = try c.decode(Bool.self, forKey: .chordsSolo)
    chordsAudible = try c.decode(Bool.self, forKey: .chordsAudible)
    noteSelection = try c.decodeIfPresent(BenchNoteSelectionJSON.self, forKey: .noteSelection)
    trackLimits = try c.decode(BenchTrackLimitsJSON.self, forKey: .trackLimits)
    eventExtras = try c.decode([BenchEventExtrasJSON].self, forKey: .events)
  }
}

func percentile(_ sorted: [Double], _ q: Double) -> Double {
  let i = Int((q * Double(sorted.count)).rounded(.up)) - 1
  return sorted[max(0, min(i, sorted.count - 1))]
}

func time(_ label: String, runs: Int, _ body: () throws -> Int) rethrows {
  var samples: [Double] = []
  samples.reserveCapacity(runs)
  var sink = 0
  for _ in 0..<runs {
    let t0 = DispatchTime.now().uptimeNanoseconds
    sink &+= try body()
    samples.append(Double(DispatchTime.now().uptimeNanoseconds - t0) / 1e6)
  }
  let s = samples.sorted()
  let mean = samples.reduce(0, +) / Double(samples.count)
  print("    " + label.padding(toLength: 42, withPad: " ", startingAt: 0)
    + String(format: " %8.3f %8.3f %8.3f %8.3f", percentile(s, 0.5), percentile(s, 0.95), s.last!, mean)
    + (sink == 42 ? " " : ""))
}

let args = CommandLine.arguments
guard args.count > 1, let data = FileManager.default.contents(atPath: args[1]) else {
  FileHandle.standardError.write("usage: decode_bench <snapshot.json> [runs]\n".data(using: .utf8)!)
  exit(2)
}
let runs = args.count > 2 ? max(10, Int(args[2]) ?? 200) : 200

print("== Swift decode of \(args[1]) (\(data.count) bytes), \(runs) runs, swiftc -O")
print("    " + "ms".padding(toLength: 42, withPad: " ", startingAt: 0) + "      p50      p95      max     mean")
try time("Snapshot (today's decoder, no tracks keys)", runs: runs) {
  try JSONDecoder().decode(Snapshot.self, from: data).events.count
}
try time("JSONSerialization.jsonObject (parse only)", runs: runs) {
  (try JSONSerialization.jsonObject(with: data) as? [String: Any])?.count ?? 0
}
try time("M2 estimate: Snapshot + tracks keys", runs: runs) {
  let s = try JSONDecoder().decode(M2Snapshot.self, from: data)
  return s.tracks.reduce(0) { $0 + $1.notes.id.count } + s.eventExtras.count
}

/// The M2 estimate's parts, each with its own parse of the whole bytes.
struct TracksOnly: Decodable { let tracks: [BenchTrackJSON] }
struct GuidesOnly: Decodable { let events: [BenchEventExtrasJSON] }
try time("  part: tracks alone (+ parse)", runs: runs) {
  try JSONDecoder().decode(TracksOnly.self, from: data).tracks.count
}
try time("  part: guides and spans alone (+ parse)", runs: runs) {
  try JSONDecoder().decode(GuidesOnly.self, from: data).events.count
}
