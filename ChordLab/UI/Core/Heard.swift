import Foundation

/// The chord the listener identified in the held notes: `root` is a pitch class, `type` the dictionary id
/// ("m7"), `symbol` and `roman` the engine's ASCII spellings ("Am7", "vi7"; glyphs are the UI's business, see
/// `HeardText`), `inversion` the inversion the bass note implies and `bass` the lowest held MIDI pitch.
struct HeardChordJSON: Codable, Equatable {
  let root: Int
  let type: String
  let symbol: String
  let roman: String
  let inversion: Int
  let bass: Int
}

/// The key the listener's notes best fit: `tonic` is spelled like the snapshot's project key, `mode` is the
/// engine's mode name (display it through `KeyNames.modeDisplayName`), `name` the engine's ASCII
/// "A Aeolian" and `percent` the fit, an integer 0...100.
struct HeardKeyJSON: Codable, Equatable {
  let tonic: TonicJSON
  let mode: String
  let name: String
  let percent: Int
}

/// `kMsgTagHeard` payload (design D14): what the plug-in hears on its MIDI input. `active` is false until
/// the first event of the session and after the user forgets; `held` are the sounding MIDI pitches and
/// `heldNames` their spellings (a tone of the identified chord is spelled as the chord spells it, any other
/// as the project key does); `chord` and `key` are nil (JSON null) until one is identified; `chordHeld` is
/// false once the notes of `chord` have been released (the engine keeps the last chord); `onsets` counts the
/// notes the key estimate is built from.
struct HeardJSON: Codable, Equatable {
  let schema: Int
  let active: Bool
  let held: [Int]
  let heldNames: [String]
  let chord: HeardChordJSON?
  let chordHeld: Bool
  let key: HeardKeyJSON?
  let onsets: Int
}
