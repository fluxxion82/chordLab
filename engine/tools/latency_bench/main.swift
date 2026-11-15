// The Swift half of the D6 latency gate (not a test): times decoding a snapshot the engine bench dumped.
// Compiled with the real decoder by decode_bench.sh: swiftc -O ChordLab/UI/Core/*.swift main.swift.
//
// Two numbers per run over the same bytes:
// - `Snapshot`, the full decoder of ChordLab/UI/Core/Snapshot.swift: the tracks (notes column-wise), the per-event
//   guide and play span, the note selection and limits included, so this is the number D6 gates;
// - `JSONSerialization.jsonObject`, the parse alone: the lower bound of any decoder of these bytes.

import Foundation

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
try time("Snapshot (full decoder, tracks included)", runs: runs) {
  try JSONDecoder().decode(Snapshot.self, from: data).events.count
}
try time("JSONSerialization.jsonObject (parse only)", runs: runs) {
  (try JSONSerialization.jsonObject(with: data) as? [String: Any])?.count ?? 0
}
