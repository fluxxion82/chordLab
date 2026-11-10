import AppKit

/// The production `ChordPasteboard` (strip editing design D5): the general pasteboard, with ChordLab's chords and
/// the plain-text symbol line on ONE `NSPasteboardItem`, so both forms are published together or not at all.
/// `write` fails (and the caller removes nothing on a cut) when either representation cannot be set or the
/// pasteboard refuses the item.
extension NSPasteboard: ChordPasteboard {
  func clear() {
    clearContents()
  }

  func write(_ item: ClipboardItem) -> Bool {
    let entry = NSPasteboardItem()
    guard entry.setData(item.chords, forType: NSPasteboard.PasteboardType(ClipboardChords.pasteboardType)),
          entry.setString(item.text, forType: .string) else { return false }
    return writeObjects([entry])
  }

  func data(forType type: String) -> Data? {
    data(forType: NSPasteboard.PasteboardType(type))   // AppKit's overload: the typed name
  }
}
