import Foundation

/// Which parts of the harmonic guide the piano roll draws (the roll toolbar's View menu, tracks design D14): the
/// root's band, the chord tones', the tensions', the avoid notes' and the degree labels. A role that is off draws
/// no band and no label; labels off draws no label on any row. Only the drawing: Snap pitch judges by the guide's
/// roles (`PitchSnap`), whatever is shown.
///
/// Decision (Sterling, 2026-11-16, after trying the roll in Live): every role at once was too busy, so the
/// defaults are calmer, the root, the chord tones and their labels; tensions and avoid notes are a View menu choice.
///
/// An editor-wide preference (the same on every track), UI-only, remembered across sessions in UserDefaults under
/// `storageKey` as `rawValue` (like `LabelMode`), never sent to the engine. The raw form is forward tolerant:
/// `"root=1,chord=1,tension=0,avoid=0,labels=1"`, one `name=1|0` per part; a part it does not mention keeps its
/// default, entries this build does not understand (a later build's part, a malformed entry) are skipped, and a
/// missing or wholly unknown value reads as `defaultDisplay`.
///
/// Depends on nothing but Foundation; unit-tested by the `ChordLabUICore` package
/// (Tests/ChordLabUICoreTests/GuideDisplayTests.swift).
struct GuideDisplay: Equatable {
  /// One part of the guide, in the View menu's order; the raw value is its name in the stored form.
  enum Part: String, CaseIterable, Identifiable {
    case root
    case chordTones = "chord"
    case tensions = "tension"
    case avoid
    case labels

    var id: String { rawValue }

    /// The View menu's title.
    var title: String {
      switch self {
      case .root: return "Root"
      case .chordTones: return "Chord tones"
      case .tensions: return "Tensions"
      case .avoid: return "Avoid notes"
      case .labels: return "Degree labels"
      }
    }
  }

  /// One View menu entry: the part, its title and whether it is shown now.
  struct Entry: Equatable, Identifiable {
    let part: Part
    let title: String
    let isOn: Bool
    var id: String { part.id }
  }

  var root = true
  var chordTones = true
  var tensions = false
  var avoid = false
  var labels = true

  /// The UserDefaults key the preference is stored under.
  static let storageKey = "chordlab.guideDisplay"
  /// What a fresh install shows: the root, the chord tones and their degree labels.
  static let defaultDisplay = GuideDisplay()
  /// Every part shown (the layout unfiltered).
  static let everything = GuideDisplay(root: true, chordTones: true, tensions: true, avoid: true, labels: true)
  /// No part shown.
  static let nothing = GuideDisplay(root: false, chordTones: false, tensions: false, avoid: false, labels: false)

  init() {}

  init(root: Bool, chordTones: Bool, tensions: Bool, avoid: Bool, labels: Bool) {
    self.root = root
    self.chordTones = chordTones
    self.tensions = tensions
    self.avoid = avoid
    self.labels = labels
  }

  /// Whether `part` is shown.
  subscript(part: Part) -> Bool {
    get {
      switch part {
      case .root: return root
      case .chordTones: return chordTones
      case .tensions: return tensions
      case .avoid: return avoid
      case .labels: return labels
      }
    }
    set {
      switch part {
      case .root: root = newValue
      case .chordTones: chordTones = newValue
      case .tensions: tensions = newValue
      case .avoid: avoid = newValue
      case .labels: labels = newValue
      }
    }
  }

  /// Whether a row of guide `role` draws its band (and may carry its label): its part is on; Outside never draws.
  func shows(_ role: GuideRole) -> Bool {
    switch role {
    case .root: return root
    case .chord: return chordTones
    case .tension: return tensions
    case .avoid: return avoid
    case .outside: return false
    }
  }

  /// The View menu's guide entries in order (Root, Chord tones, Tensions, Avoid notes, Degree labels) with their
  /// current state.
  var entries: [Entry] {
    Part.allCases.map { Entry(part: $0, title: $0.title, isOn: self[$0]) }
  }

  /// This display with `part` switched over.
  func toggled(_ part: Part) -> GuideDisplay {
    var copy = self
    copy[part].toggle()
    return copy
  }

  /// What is shown, for the View menu's accessibility value: the shown parts' titles ("Root, Chord tones, Degree
  /// labels"), or "No guides".
  var summary: String {
    let shown = entries.filter(\.isOn).map(\.title)
    return shown.isEmpty ? "No guides" : shown.joined(separator: ", ")
  }

  /// The stored form: every part as `name=1` or `name=0`, in menu order, comma-separated.
  var rawValue: String {
    Part.allCases.map { "\($0.rawValue)=\(self[$0] ? 1 : 0)" }.joined(separator: ",")
  }

  /// Reads a stored form (see the type comment): nil, empty or unknown is the default; each understood
  /// `name=1|0` entry (spaces around either side trimmed) sets its part, and the rest keep their defaults.
  init(rawValue: String?) {
    self.init()
    guard let rawValue else { return }
    for entry in rawValue.split(separator: ",") {
      let pair = entry.split(separator: "=", omittingEmptySubsequences: false)
      guard pair.count == 2,
            let part = Part(rawValue: pair[0].trimmingCharacters(in: .whitespaces)) else { continue }
      switch pair[1].trimmingCharacters(in: .whitespaces) {
      case "1": self[part] = true
      case "0": self[part] = false
      default: continue
      }
    }
  }
}
