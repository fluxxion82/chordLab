import Foundation

/// A key press as either native route reports it, reduced to what the shortcuts need: the key
/// (a character, one of the two delete keys, an arrow, or something else) and the four modifiers.
/// Arrows are always read from the key code or virtual key, never from the text (route A's arrow text is a
/// private-use function-key character, route B's is empty).
struct KeyInput: Hashable {
  enum Key: Hashable {
    case character(Character)
    case delete          // Backspace / the Mac "delete" key
    case forwardDelete   // fn-delete / "⌦"
    case arrow(ArrowDirection)
    case other

    /// The route-A key (`KeyInput(event:)` passes the event's `keyCode` and `charactersIgnoringModifiers`):
    /// macOS hardware key codes for the delete keys (51 = delete, 117 = forward delete) and the arrows (123 ←,
    /// 124 →, 125 ↓, 126 ↑; layout-independent, unlike AppKit's `specialKey`, which is derived from the text),
    /// whatever text comes with them (Live 12 sends arrows with none, tracks design S0; AppKit elsewhere gives
    /// the private-use function-key characters U+F700…U+F703); any other key with exactly one character is that
    /// character (a keypad digit included, and a private-use arrow character on a non-arrow key code too: only
    /// the key code makes an arrow); no text, or more than one character, is `.other`.
    init(macKeyCode: UInt16, charactersIgnoringModifiers: String?) {
      switch macKeyCode {
      case 51: self = .delete
      case 117: self = .forwardDelete
      case 123: self = .arrow(.left)
      case 124: self = .arrow(.right)
      case 125: self = .arrow(.down)
      case 126: self = .arrow(.up)
      default:
        let text = charactersIgnoringModifiers ?? ""
        self = text.count == 1 ? .character(text.first!) : .other
      }
    }
  }

  var key: Key
  var command: Bool
  var shift: Bool
  var option: Bool
  var control: Bool

  init(key: Key, command: Bool = false, shift: Bool = false, option: Bool = false, control: Bool = false) {
    self.key = key; self.command = command; self.shift = shift; self.option = option; self.control = control
  }

  /// The VST3 route: iPlug2's `IKeyPress` carries the key's UTF-8 text, a Windows virtual key and
  /// three modifiers (S, C = command on macOS, A); it drops Control, so the caller supplies it from
  /// the live modifier flags. `kVK_BACK` 0x08 and `kVK_DELETE` 0x2E are the delete keys; `kVK_LEFT` 0x25,
  /// `kVK_UP` 0x26, `kVK_RIGHT` 0x27 and `kVK_DOWN` 0x28 are the arrows, whatever text comes with them
  /// (iPlug2's translation gives them none: VST3's `VirtualKeyCodeToChar` has no character for them); any other
  /// key with exactly one character is that character; a keypad digit (`kVK_NUMPAD0`…`9`, 0x60…0x69) without
  /// text is that digit, because the VST3 translation leaves the text empty for keypad codes it cannot
  /// turn into a character; the rest is `.other`.
  init(characters: String, virtualKey: Int, shift: Bool, command: Bool, option: Bool, control: Bool) {
    let key: Key
    switch virtualKey {
    case 0x08: key = .delete
    case 0x2E: key = .forwardDelete
    case 0x25: key = .arrow(.left)
    case 0x26: key = .arrow(.up)
    case 0x27: key = .arrow(.right)
    case 0x28: key = .arrow(.down)
    case 0x60...0x69 where characters.isEmpty: key = .character(Character(String(virtualKey - 0x60)))
    default: key = characters.count == 1 ? .character(characters.first!) : .other
    }
    self.init(key: key, command: command, shift: shift, option: option, control: control)
  }
}

/// What a key press asks the editor to do.
enum KeyCommand: Equatable {
  case appendSuggestion(Int)   // 1-based chip number
  case deleteSelection
  case undo
  case redo
  case copy        // ⌘C: the selection to the clipboard (strip editing design D5, D6)
  case cut         // ⌘X: copy, then remove the selection
  case paste       // ⌘V: the clipboard's chords after the anchor, else at the end
  case duplicate   // ⌘D: the selection's chords again, after its last block
  case selectAll   // ⌘A: every block, the last the anchor
  /// An arrow with the notes targeted (tracks design D15): ↑/↓ a semitone, or an octave with `octave` (⇧);
  /// ←/→ one grid step. ⇧←/⇧→ carry `octave` too and `RollGroupMath.nudge` ignores it horizontally.
  case nudge(ArrowDirection, octave: Bool)
}

/// Where `ChordLabState.perform` sends a key command (tracks design D15 "Keyboard", plan Task 20).
enum KeyRoute: Equatable {
  /// Undo and redo: the one history the chords and the notes share, whatever is targeted.
  case shared
  /// The strip's chords (strip editing design D5, D6).
  case chords
  /// That track's notes (`NoteKeys`).
  case notes(trackId: Int)
  /// Nothing: the targeted track's roll is not the open lane (defence in depth; the target resets on every lane
  /// change, so this should never happen), or a digit with the chords targeted while a track is open (the
  /// suggestion chips it would append are not on screen).
  case ignored
}

extension KeyCommand {
  /// The route for this command with `target` while `openLane` is shown: undo and redo are `.shared`; everything else
  /// follows the target, so a key with a track's notes targeted never acts on the chords the open track hides, and one
  /// with the chords targeted never acts on notes. A notes target whose track is not the open lane is `.ignored`, so
  /// nothing ever acts on a track that is not open. With the chords targeted and a track open (the state right after
  /// a tab click, before any click in the roll) the strip is still on screen, so its keys still act on it; the
  /// suggestion chips are not (the roll takes their place, design D13), so a digit is `.ignored` then.
  func route(target: KeyTarget, openLane: Lane) -> KeyRoute {
    if self == .undo || self == .redo { return .shared }
    switch target {
    case .chords:
      if case .appendSuggestion = self, openLane != .chords { return .ignored }
      return .chords
    case .notes(let trackId): return openLane == .track(trackId) ? .notes(trackId: trackId) : .ignored
    }
  }
}

/// What the keyboard edits (tracks design D15 "Keyboard"): the chord strip, or one track's notes in the roll.
/// UI state in `ChordLabState.keyTarget`, set by the last mouse-down (strip, palette, suggestions → chords; the
/// roll → that track's notes), never saved.
enum KeyTarget: Equatable {
  case chords
  case notes(trackId: Int)

  /// The target given the snapshot's tracks: `.notes(id)` while a track with that id exists, else `.chords`
  /// (a removed track's notes fall back to the chords, so Delete never acts on a hidden track; so do notes
  /// while no `tracks` have arrived); `.chords` stays `.chords`. The same rule as `Lane.resolved(in:)`.
  func resolved(in tracks: [TrackJSON]?) -> KeyTarget {
    guard case .notes(let trackId) = self else { return self }
    return tracks?.contains { $0.id == trackId } == true ? self : .chords
  }

  /// The target after the open lane (`ChordLabState.openLane`, design D13) goes from `old` to `new`: `.chords`
  /// whenever the lane changes (opening a track does not target its notes; a mouse-down in its roll does), the
  /// target unchanged when the "change" reopens the same lane.
  func afterOpenLaneChange(from old: Lane, to new: Lane) -> KeyTarget {
    old == new ? self : .chords
  }
}

/// The one key map, for both routes and both targets. With `.chords` targeted: bare digits 1–8 append that
/// suggestion chip; Delete and Forward Delete remove the selected chords; ⌘Z and ⇧⌘Z undo and redo; ⌘C, ⌘X, ⌘V,
/// ⌘D and ⌘A copy, cut, paste, duplicate and select all, without Shift (⇧⌘C and the like stay the host's); every
/// arrow, with any modifiers, is nil (the host keeps it: Live moves its own selection). With `.notes(trackId)`
/// targeted: the same Delete, ⌘Z/⇧⌘Z and ⌘C ⌘X ⌘V ⌘D ⌘A entries (`ChordLabState.perform` sends Delete and
/// ⌘C ⌘X ⌘V ⌘D ⌘A to the notes, `KeyCommand.route`; ⌘Z/⇧⌘Z stay the shared undo);
/// a bare arrow is `.nudge(direction, octave: false)` and a ⇧-arrow `.nudge(direction, octave: true)`; an arrow
/// with ⌘, ⌥ or ⌃ (⇧ or not) is nil; bare digits are nil (the suggestion chips are hidden while a track is open,
/// design D13). For both targets everything else (Space, letters, 9, 0, digits or deletes with a modifier, ⌥ or
/// ⌃ with ⌘, digits of other scripts such as "٢" or "²") is nil: not ours, the host keeps it.
enum KeyCommandMap {
  static func command(for input: KeyInput, target: KeyTarget) -> KeyCommand? {
    let anyModifier = input.command || input.shift || input.option || input.control
    switch input.key {
    case .arrow(let direction):
      guard case .notes = target, !input.command, !input.option, !input.control else { return nil }
      return .nudge(direction, octave: input.shift)
    case .character(let c) where !anyModifier:
      guard target == .chords, c.isASCII, ("1"..."8").contains(c), let n = c.wholeNumberValue else { return nil }
      return .appendSuggestion(n)
    case .character(let c) where input.command && !input.option && !input.control:
      switch c.lowercased() {
      case "z": return input.shift ? .redo : .undo
      case "c" where !input.shift: return .copy
      case "x" where !input.shift: return .cut
      case "v" where !input.shift: return .paste
      case "d" where !input.shift: return .duplicate
      case "a" where !input.shift: return .selectAll
      default: return nil
      }
    case .delete, .forwardDelete:
      return anyModifier ? nil : .deleteSelection
    default:
      return nil
    }
  }
}

/// Which route delivered a key. Route A (`appKit`) carries the system's repeat flag; route B (`host`,
/// VST3 `onKeyDown`) carries none, but its key-ups arrive through `onKeyUp`.
enum KeySource: Equatable {
  case appKit(isRepeat: Bool)
  case host
}

/// Decides whether a delivered key acts, so that one physical press is one command whichever route, or
/// both, delivers it, and a held key does not repeat. Characters are compared case-folded (with Shift
/// held one route may report "Z" and the other "z"). Two histories, so interleaved keys on two routes
/// keep their own:
/// - Duplicates, per key and modifiers: a delivery within `window` of the previous delivery of the same
///   key with the same modifiers never acts, from either route — the one press reaching the editor
///   through both routes, or an autorepeat chain (measured from the previous delivery, so the chain stays
///   suppressed; macOS's fastest autorepeat is about 33 ms apart, a double tap 120 ms or more). ⌘Z and
///   ⇧⌘Z are different here.
/// - Holds, per physical key (modifiers not part of it: a key-up may come after a modifier was
///   released). A route-A delivery the system flags as a repeat never acts. Route B has no repeat flag,
///   so its deliveries mark the key held until its `keyUp`; a host delivery of a held key is an
///   autorepeat and does not act. Every delivery of a held key refreshes the hold, and a hold ends
///   `heldExpiry` after the key's last delivery: the backstop for a host that sends no key-ups (macOS's
///   slowest key repeat is about 2 s apart), which cannot tell a lost release from a key still held with
///   a slower repeat, so such a key may act again after 2 s. Route A never marks a key held: it has its
///   own repeat flag, and a route-A press must not block a deliberate second press. `clearHolds` drops
///   every hold when the editor loses the keyboard, since the releases may then go elsewhere.
/// Every delivery is consumed by the caller whether or not it acts. The filter also remembers, per
/// physical key, whether the caller consumed route B's last key-down (`noteHostKeyDown`), so its release
/// gets the same answer (`keyUp`) whatever the modifiers or the text gate say by then.
struct KeyRepeatFilter {
  static let window: TimeInterval = 0.1
  static let heldExpiry: TimeInterval = 2

  private var lastDelivery: [KeyInput: TimeInterval] = [:]   // key (folded) and modifiers
  private var holds: [KeyInput.Key: TimeInterval] = [:]      // physical key → its last delivery while held
  private var hostKeyDowns: [KeyInput.Key: Bool] = [:]       // physical key → route B's last key-down consumed

  private static func folded(_ key: KeyInput.Key) -> KeyInput.Key {
    if case .character(let c) = key, let lower = c.lowercased().first { return .character(lower) }
    return key
  }

  private static func folded(_ input: KeyInput) -> KeyInput {
    var input = input
    input.key = folded(input.key)
    return input
  }

  mutating func shouldAct(_ input: KeyInput, at time: TimeInterval, source: KeySource) -> Bool {
    let delivery = Self.folded(input)
    let physical = delivery.key
    let previous = lastDelivery[delivery]
    lastDelivery[delivery] = time
    let hold = holds[physical]
    if hold != nil || source == .host { holds[physical] = time }
    if let previous, time - previous < Self.window { return false }
    switch source {
    case .appKit(let isRepeat):
      return !isRepeat
    case .host:
      if let hold, time - hold < Self.heldExpiry { return false }
      return true
    }
  }

  /// Records whether the caller consumed a route-B key-down, for its release.
  mutating func noteHostKeyDown(_ input: KeyInput, consumed: Bool) {
    hostKeyDowns[Self.folded(input.key)] = consumed
  }

  /// Route B's key-up: the key is no longer held. Returns what the caller answered for the key's last
  /// key-down (false when none is recorded), and forgets it. `time` is not needed by the rules; it keeps
  /// the call sites in step with `shouldAct`.
  @discardableResult
  mutating func keyUp(_ input: KeyInput, at time: TimeInterval) -> Bool {
    let physical = Self.folded(input.key)
    holds[physical] = nil
    return hostKeyDowns.removeValue(forKey: physical) ?? false
  }

  /// Drops every hold and every recorded key-down answer, keeping the duplicate history: the editor lost
  /// the keyboard, and the releases of keys held now may go elsewhere.
  mutating func clearHolds() {
    holds.removeAll()
    hostKeyDowns.removeAll()
  }
}
