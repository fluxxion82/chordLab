import Foundation

/// A key press as either native route reports it, reduced to what the shortcuts need: the key
/// (a character, one of the two delete keys, or something else) and the four modifiers.
struct KeyInput: Equatable {
  enum Key: Equatable {
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

/// Decides whether a delivered key acts. A key its route flags as a repeat never acts; nor does the
/// same key delivered again within `window` of its previous delivery — that is an autorepeat on a route
/// without a repeat flag (VST3), or the one press reaching the editor through both routes. Either way
/// the delivery is consumed by the caller. The window is measured from the previous delivery, so a chain
/// of repeats stays suppressed; a deliberate second press is slower than the window (macOS's fastest
/// autorepeat is about 33 ms apart, a double tap 120 ms or more). A character is compared case-folded:
/// with Shift held one route may report "Z" and the other "z" for the same press.
struct KeyRepeatFilter {
  static let window: TimeInterval = 0.1
  private var last: (input: KeyInput, time: TimeInterval)?

  mutating func shouldAct(_ input: KeyInput, at time: TimeInterval, isRepeat: Bool) -> Bool {
    var input = input
    if case .character(let c) = input.key, let folded = c.lowercased().first { input.key = .character(folded) }
    defer { last = (input, time) }
    if isRepeat { return false }
    if let last, last.input == input, time - last.time < Self.window { return false }
    return true
  }
}
