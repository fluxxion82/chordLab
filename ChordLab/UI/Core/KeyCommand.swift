import Foundation

/// A key press as either native route reports it, reduced to what the shortcuts need: the key
/// (a character, one of the two delete keys, or something else) and the four modifiers.
struct KeyInput: Hashable {
  enum Key: Hashable {
    case character(Character)
    case delete          // Backspace / the Mac "delete" key
    case forwardDelete   // fn-delete / "⌦"
    case other
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
  /// the live modifier flags. `kVK_BACK` 0x08 and `kVK_DELETE` 0x2E are the delete keys; any other key
  /// with exactly one character is that character; a keypad digit (`kVK_NUMPAD0`…`9`, 0x60…0x69) without
  /// text is that digit, because the VST3 translation leaves the text empty for keypad codes it cannot
  /// turn into a character; the rest is `.other`.
  init(characters: String, virtualKey: Int, shift: Bool, command: Bool, option: Bool, control: Bool) {
    let key: Key
    switch virtualKey {
    case 0x08: key = .delete
    case 0x2E: key = .forwardDelete
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
}

/// The one key map, for both routes: bare digits 1–8 append that suggestion chip; Delete and Forward
/// Delete remove the selected chord; ⌘Z and ⇧⌘Z undo and redo. Everything else (Space, letters, 9, 0,
/// digits or deletes with a modifier, digits of other scripts such as "٢" or "²") is nil: not ours, the
/// host keeps it.
enum KeyCommandMap {
  static func command(for input: KeyInput) -> KeyCommand? {
    let anyModifier = input.command || input.shift || input.option || input.control
    switch input.key {
    case .character(let c) where !anyModifier:
      guard c.isASCII, ("1"..."8").contains(c), let n = c.wholeNumberValue else { return nil }
      return .appendSuggestion(n)
    case .character(let c) where input.command && !input.option && !input.control:
      guard c == "z" || c == "Z" else { return nil }
      return input.shift ? .redo : .undo
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
