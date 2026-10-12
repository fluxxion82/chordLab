import SwiftUI
import UniformTypeIdentifiers

extension UTType {
  /// A chord dragged from the palette to the sequence bar. An internal drag (never leaves the editor);
  /// the type is declared in the APP and VST3 Info.plists as well, so the system knows it by name.
  /// Distinct from the M4 file drag, which carries a `.mid` URL.
  static let chordLabChord = UTType(exportedAs: "com.sterlingalbury.chordlab.chord")
}

/// What a palette cell drags: the chord exactly as the engine identifies it, so the drop can send
/// `appendChord {root, type}` without any theory on this side.
struct ChordRef: Codable, Equatable, Transferable {
  let root: Int
  let type: String

  static var transferRepresentation: some TransferRepresentation {
    CodableRepresentation(contentType: .chordLabChord)
  }
}
