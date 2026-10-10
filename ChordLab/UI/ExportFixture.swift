import Foundation

/// M0 spike helper: writes a known-good 1-bar C major SMF (type 0, 480 tpqn, 120 bpm)
/// into the temp dir and hands back its URL for a drag-out. The previous export is
/// deleted on the *next* call, never right after a drop (the host reads the file later).
enum ExportFixture {
  static let cMajorOneBar: [UInt8] = [
    0x4D,0x54,0x68,0x64, 0,0,0,6, 0,0, 0,1, 0x01,0xE0,                 // MThd, type 0, 1 track, 480 tpqn
    0x4D,0x54,0x72,0x6B, 0,0,0,0x2C,                                   // MTrk, 44 bytes
    0,0xFF,0x51,3,0x07,0xA1,0x20,                                       // tempo 500000 us
    0,0xFF,0x58,4,4,2,0x18,8,                                           // 4/4
    0,0x90,0x3C,0x64, 0,0x90,0x40,0x64, 0,0x90,0x43,0x64,               // note ons
    0x8F,0x00,0x80,0x3C,0, 0,0x80,0x40,0, 0,0x80,0x43,0,                // offs 1920 ticks later
    0,0xFF,0x2F,0]                                                      // end of track

  private static var lastExport: URL?

  static func prepareExportFile() -> URL? {
    let dir = URL(fileURLWithPath: NSTemporaryDirectory()).appendingPathComponent("ChordLab", isDirectory: true)
    do {
      try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
      if let old = lastExport { try? FileManager.default.removeItem(at: old) }
      let url = dir.appendingPathComponent("ChordLab-C-major-\(UUID().uuidString.prefix(8)).mid")
      try Data(cMajorOneBar).write(to: url)
      lastExport = url
      return url
    } catch {
      print("ChordLab: export fixture failed: \(error)")
      return nil
    }
  }
}
