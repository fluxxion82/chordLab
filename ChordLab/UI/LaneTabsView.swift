import SwiftUI

/// The lane tab row (tracks design D13) between the sequence bar and the suggestions: `Chords · <tracks> · +`, one
/// line `height` tall. Everything shown is `LaneTabs.tabs` of the snapshot and `ChordLabState.openLane`; every
/// change is a command built in Core (`LaneTabs`) and sent through `ChordLabState`, and the answering snapshot is
/// what changes the row (controls never keep their own model).
/// - Click a tab's name → that lane opens (`ChordLabState.open`): Chords shows the suggestions, palette and home
///   row; a track shows its roll in their place (`ContentView`).
/// - M and S → `setChordsMute` / `setChordsSolo` for Chords, `setTrackMute` / `setTrackSolo` for a track, the
///   toggle showing the snapshot's flag; the whole tab dims while the lane is not audible (the engine's outcome).
/// - `+` → Bass, Lead, Other (`addTrack {role}`; the engine names it); disabled at the snapshot's track limit.
///   The new track opens with the snapshot that answers the add.
/// - Right-click a track tab → Rename… (an inline field with the section rename's rules and its `editingText`
///   handshake, as `SectionsHeaderView`: Return commits, Escape cancels, losing focus commits; `patchTrack {id,
///   name}`), Role ▸ (the current one checked; `patchTrack {id, role}`), Move left / Move right (`moveTrack`,
///   each disabled at its end of the row) and Remove (`removeTrack`, undoable, no confirmation). Chords has no
///   menu.
/// A track tab carries a small mark in its role's colour (`TrackStyle`). Names truncate at the tail before any
/// control shrinks, so Chords, four tracks and + stay in one line at the 700-pt minimum width. An engine without
/// tracks shows the Chords tab alone (no M/S, no +), so the row keeps its height. The lane drag grips and the All
/// grip come with the per-lane export (plan Task 21).
struct LaneTabsView: View {
  @EnvironmentObject var state: ChordLabState

  /// The row's height (D13: about 26 pt); a tab is `tabHeight` inside it.
  static let height: CGFloat = 26
  static let tabHeight: CGFloat = 22
  /// The longest a tab's name draws before it truncates.
  static let maxNameWidth: CGFloat = 120
  /// The M and S toggles' square.
  static let toggleSize: CGFloat = 16
  /// M's colour while on (the section palette's amber, `SectionStyle` index 5); S on is the accent colour.
  static let muteOn = Color(red: 0.878, green: 0.659, blue: 0.227)   // #E0A83A

  @State private var renamingId: Int?
  @State private var draft = ""
  @FocusState private var renameFocused: Bool

  var body: some View {
    let snapshot = state.snapshot
    HStack(spacing: 6) {
      ForEach(LaneTabs.tabs(in: snapshot, open: state.openLane)) { tab in
        tabView(tab, menu: LaneTabs.menu(for: tab.lane, in: snapshot))
      }
      if LaneTabs.showsTracks(snapshot) {
        addMenu(snapshot)
      }
      Spacer(minLength: 0)
    }
    .frame(height: Self.height)
    .accessibilityElement(children: .contain)
    .accessibilityLabel("Lanes")
  }

  // MARK: - One tab

  private func tabView(_ tab: LaneTab, menu: TrackTabMenu?) -> some View {
    HStack(spacing: 6) {
      if let menu, renamingId == menu.trackId {
        renameField(menu)
      } else {
        openButton(tab)
      }
      if tab.hasMuteSolo {
        HStack(spacing: 3) {
          toggle("M", on: tab.muted, colour: Self.muteOn,
                 help: tab.muted ? "Unmute \(tab.label)" : "Mute \(tab.label)",
                 label: "Mute \(tab.label)") { state.setMute(tab.lane, on: !tab.muted) }
          toggle("S", on: tab.soloed, colour: .accentColor,
                 help: tab.soloed ? "Stop soloing \(tab.label)" : "Solo \(tab.label): only soloed lanes play",
                 label: "Solo \(tab.label)") { state.setSolo(tab.lane, on: !tab.soloed) }
        }
        .fixedSize()
      }
    }
    .padding(.leading, 8)
    .padding(.trailing, tab.hasMuteSolo ? 3 : 8)
    .frame(height: Self.tabHeight)
    .background(
      RoundedRectangle(cornerRadius: 5)
        .fill(Color.white.opacity(tab.isOpen ? 0.16 : 0.06))
    )
    .overlay(
      RoundedRectangle(cornerRadius: 5)
        .strokeBorder(tab.isOpen ? Color.accentColor.opacity(0.9) : Color.white.opacity(0.12), lineWidth: 1)
    )
    .opacity(tab.dimmed ? 0.45 : 1)
    .contextMenu {
      if let menu { trackMenu(menu) }
    }
    .accessibilityElement(children: .contain)
    .accessibilityLabel(tab.label + " lane")
  }

  /// The tab's name (with the role mark for a track): a click opens the lane.
  private func openButton(_ tab: LaneTab) -> some View {
    Button { state.open(tab.lane) } label: {
      HStack(spacing: 5) {
        if let role = tab.role {
          RoundedRectangle(cornerRadius: 1.5)
            .fill(TrackStyle.color(role: role))
            .frame(width: 4, height: 12)
            .accessibilityHidden(true)
        }
        CappedWidth(maxWidth: Self.maxNameWidth) {
          Text(tab.label)
            .font(.system(size: 12, weight: tab.isOpen ? .semibold : .regular))
            .foregroundStyle(tab.isOpen ? Color.white : Color.white.opacity(0.75))
            .lineLimit(1)
            .truncationMode(.tail)
        }
      }
      .frame(height: Self.tabHeight)
      .contentShape(Rectangle())
    }
    .buttonStyle(.plain)
    .help(tab.role == nil ? "Show the chords: suggestions and palette" : "Show the \(tab.label) piano roll")
    .accessibilityLabel("Open " + tab.label)
    .accessibilityAddTraits(tab.isOpen ? .isSelected : [])
  }

  /// A small square M or S: filled with `colour` while on, translucent while off. Shows `on` (the snapshot's
  /// flag); a click sends the flipped value and the answering snapshot redraws it.
  private func toggle(_ letter: String, on: Bool, colour: Color, help: String, label: String,
                      action: @escaping () -> Void) -> some View {
    Button(action: action) {
      Text(letter)
        .font(.system(size: 10, weight: .bold))
        .foregroundStyle(on ? Color.black.opacity(0.85) : Color.white.opacity(0.7))
        .frame(width: Self.toggleSize, height: Self.toggleSize)
        .background(RoundedRectangle(cornerRadius: 3).fill(on ? colour : Color.white.opacity(0.12)))
        .contentShape(Rectangle())
    }
    .buttonStyle(.plain)
    .help(help)
    .accessibilityLabel(label)
    .accessibilityAddTraits(.isToggle)
    .accessibilityValue(on ? "On" : "Off")
  }

  // MARK: - +

  private func addMenu(_ snapshot: Snapshot?) -> some View {
    let canAdd = LaneTabs.canAddTrack(snapshot)
    return Menu {
      ForEach(LaneTabs.addEntries(snapshot)) { entry in
        Button(entry.title) { state.addTrack(role: entry.role) }
          .disabled(!entry.enabled)
      }
    } label: {
      // A Label, not a bare Image: its title is what the menu button speaks (an Image alone reads "add").
      Label("Add a track", systemImage: "plus")
        .labelStyle(.iconOnly)
        .font(.system(size: 11, weight: .semibold))
    }
    .accessibilityLabel("Add a track")
    .menuStyle(.borderlessButton)
    .menuIndicator(.hidden)
    .fixedSize()
    .frame(width: 26, height: Self.tabHeight)
    .contentShape(Rectangle())
    .disabled(!canAdd)
    .help(canAdd ? "Add a track: Bass, Lead or Other" : "At most \(LaneTabs.maxTracks(snapshot)) tracks")
  }

  // MARK: - Track menu

  @ViewBuilder private func trackMenu(_ menu: TrackTabMenu) -> some View {
    Button("Rename…") { beginRename(menu) }
    Menu("Role") {
      ForEach(TrackRole.allCases, id: \.self) { role in
        Button(role.displayName + (menu.isChecked(role) ? "  ✓" : "")) {
          state.setTrackRole(id: menu.trackId, role: role)
        }
      }
    }
    Divider()
    Button("Move left") {
      if let index = menu.moveLeftIndex { state.moveTrack(id: menu.trackId, to: index) }
    }
    .disabled(menu.moveLeftIndex == nil)
    Button("Move right") {
      if let index = menu.moveRightIndex { state.moveTrack(id: menu.trackId, to: index) }
    }
    .disabled(menu.moveRightIndex == nil)
    Divider()
    Button("Remove track") { state.removeTrack(id: menu.trackId) }
  }

  // MARK: - Rename (the section rename's field and rules, `SectionsHeaderView`)

  private func renameField(_ menu: TrackTabMenu) -> some View {
    TextField("Name", text: $draft)
      .textFieldStyle(.plain)
      .font(.system(size: 12, weight: .semibold))
      .foregroundStyle(.white)
      .frame(width: Self.maxNameWidth)
      .focused($renameFocused)
      .onChange(of: draft) { _, now in
        let bounded = LaneTabs.boundedName(now)
        if bounded != now { draft = bounded }
      }
      .onSubmit { commitRename(menu) }
      .onExitCommand { cancelRename() }
      .onChange(of: renameFocused) { _, focused in
        if !focused { commitRename(menu) }
      }
      .onDisappear { cancelRename() }
      .accessibilityLabel("Track name")
  }

  private func beginRename(_ menu: TrackTabMenu) {
    draft = menu.name
    renamingId = menu.trackId
    state.editingText = true
    DispatchQueue.main.async { renameFocused = true }   // the field exists on the next pass
  }

  /// Sends the new name through `ChordLabState.renameTrack` (`LaneTabs.renameCommand`: trimmed, bounded, a blank
  /// or unchanged draft is a cancel).
  private func commitRename(_ menu: TrackTabMenu) {
    guard renamingId == menu.trackId else { return }
    renamingId = nil
    state.editingText = false
    state.renameTrack(id: menu.trackId, draft: draft)
  }

  private func cancelRename() {
    guard renamingId != nil else { return }
    renamingId = nil
    state.editingText = false
  }
}

/// Its one subview at its own width, never wider than `maxWidth`: a name keeps its natural width up to the cap and
/// truncates beyond it, and is never stretched (a `.frame(maxWidth:)` would widen every short name to the cap in a
/// row with room).
private struct CappedWidth: Layout {
  let maxWidth: CGFloat

  func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
    guard let subview = subviews.first else { return .zero }
    return subview.sizeThatFits(ProposedViewSize(width: min(proposal.width ?? .infinity, maxWidth),
                                                 height: proposal.height))
  }

  func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
    subviews.first?.place(at: bounds.origin, proposal: ProposedViewSize(bounds.size))
  }
}

/// What a track lane shows in place of the suggestions, palette and home row until the piano roll exists (plan
/// Task 17): a dark panel naming the track. It takes the flexible height the palette viewport took, so it is the
/// row that gives up height.
struct RollAreaPlaceholder: View {
  let name: String

  var body: some View {
    ZStack {
      RoundedRectangle(cornerRadius: 6).fill(Color.black.opacity(0.25))
      RoundedRectangle(cornerRadius: 6).strokeBorder(Color.white.opacity(0.08), lineWidth: 1)
      Text("Piano roll — \(name)")
        .font(.callout)
        .foregroundStyle(.secondary)
    }
    .frame(maxWidth: .infinity, maxHeight: .infinity)
    .accessibilityElement(children: .ignore)
    .accessibilityLabel("Piano roll — \(name)")
    .accessibilityIdentifier("chordlab.rollArea")
  }
}

struct LaneTabsView_Previews: PreviewProvider {
  static var previews: some View {
    LaneTabsView().environmentObject(ChordLabState(numParams: kNumParams))
      .padding().background(PaletteView.background).frame(width: 700)
  }
}
