import SwiftUI

/// The lane tab row (tracks design D13) between the sequence bar and the suggestions: `Chords · <tracks> · +`, one
/// line `height` tall. Everything shown is `LaneTabs.tabs` of the snapshot and `ChordLabState.openLane`; every
/// change is a command built in Core (`LaneTabs`) and sent through `ChordLabState`, and the answering snapshot is
/// what changes the row (controls never keep their own model).
/// - Click a tab's name → that lane opens (`ChordLabState.open`): Chords shows the suggestions, palette and home
///   row; a track shows its roll in their place (`ContentView`).
/// - M and S → `setChordsMute` / `setChordsSolo` for Chords, `setTrackMute` / `setTrackSolo` for a track, the
///   toggle showing the snapshot's flag in its own fixed colour (`muteOn`, `soloOn`); the tab's mark and name dim
///   while the lane is not audible (the engine's outcome), the toggles never, so an on M or S reads the same on
///   every tab.
/// - `+` → Bass, Lead, Other (`addTrack {role}`; the engine names it); disabled at the snapshot's track limit.
///   The new track opens with the snapshot that answers the add.
/// - Right-click a track tab → Rename… (an inline field with the section rename's rules and its `editingText`
///   handshake, as `SectionsHeaderView`: Return commits, Escape cancels, losing focus commits; `patchTrack {id,
///   name}`), Role ▸ (the current one checked; `patchTrack {id, role}`), Move left / Move right (`moveTrack`,
///   each disabled at its end of the row) and Remove (`removeTrack`, undoable, no confirmation). Chords has no
///   menu.
/// - The ♫ grip at a tab's trailing end → drag that lane to Live as a MIDI clip (D12; `ChordLabState.dragExport
///   (.lane)`: `kMsgTagExportClip` with that lane over the whole progression, whichever lane is open). The "♫ All"
///   grip at the row's trailing end → the chords plus every track with notes, muted ones included (Q4), one clip
///   per lane in Live (S0 item 2). A grip with nothing to export (no chords; a track without sounding notes) is
///   dimmed and inert: it starts no drag and its help says why (`ClipExport.text`).
/// A track tab carries a small mark in its role's colour (`TrackStyle`). Names truncate at the tail before any
/// control shrinks, so Chords, four tracks, their grips, + and All stay in one line at the 700-pt minimum width. An
/// engine without tracks shows the Chords tab alone (no M/S, no +, no All; its grip stays), so the row keeps its
/// height.
struct LaneTabsView: View {
  @EnvironmentObject var state: ChordLabState

  /// The row's height (D13: about 26 pt); a tab is `tabHeight` inside it.
  static let height: CGFloat = 26
  static let tabHeight: CGFloat = 22
  /// The longest a tab's name draws before it truncates.
  static let maxNameWidth: CGFloat = 120
  /// The M and S toggles' square.
  static let toggleSize: CGFloat = 16
  /// A lane grip's width (the tab's full height is its target).
  static let gripWidth: CGFloat = 12
  /// How far a toggle's click target reaches past its drawn square (the 3-pt gap between M and S is split).
  static let toggleHitOutset = CGSize(width: 1.5, height: 2)
  /// What a not-audible tab's mark and name are drawn at.
  static let silentOpacity = 0.45
  /// M's colour while on (the section palette's amber, `SectionStyle` index 5).
  static let muteOn = Color(red: 0.878, green: 0.659, blue: 0.227)   // #E0A83A
  /// S's colour while on: a fixed light cyan, never the host's accent (a Graphite accent would grey it, and the
  /// accent is the open tab's stroke), apart from M's amber, the guide bands (#6E8FD8, #D9904A) and the track
  /// colours (`TrackStyle`).
  static let soloOn = Color(red: 0.310, green: 0.827, blue: 0.910)   // #4FD3E8

  @State private var renamingId: Int?
  @State private var draft = ""
  @FocusState private var renameFocused: Bool

  var body: some View {
    let snapshot = state.snapshot
    HStack(spacing: 5) {
      ForEach(LaneTabs.tabs(in: snapshot, open: state.openLane)) { tab in
        tabView(tab, menu: LaneTabs.menu(for: tab.lane, in: snapshot))
      }
      if LaneTabs.showsTracks(snapshot) {
        addMenu(snapshot)
      }
      Spacer(minLength: 0)
      if LaneTabs.showsTracks(snapshot) {
        allGrip()
      }
    }
    .frame(height: Self.height)
    .accessibilityElement(children: .contain)
    .accessibilityLabel("Lanes")
  }

  // MARK: - One tab

  private func tabView(_ tab: LaneTab, menu: TrackTabMenu?) -> some View {
    HStack(spacing: 4) {
      if let menu, renamingId == menu.trackId {
        renameField(menu)
      } else {
        openButton(tab)
      }
      HStack(spacing: 3) {
        if tab.hasMuteSolo {
          toggle("M", on: tab.muted, colour: Self.muteOn,
                 help: tab.muted ? "Unmute \(tab.label)" : "Mute \(tab.label)",
                 label: "Mute \(tab.label)") { state.setMute(tab.lane, on: !tab.muted) }
          toggle("S", on: tab.soloed, colour: Self.soloOn,
                 help: tab.soloed ? "Stop soloing \(tab.label)" : "Solo \(tab.label): only soloed lanes play",
                 label: "Solo \(tab.label)") { state.setSolo(tab.lane, on: !tab.soloed) }
        }
        laneGrip(tab)
      }
      .fixedSize()
    }
    .padding(.leading, 8)
    .padding(.trailing, 1)
    .frame(height: Self.tabHeight)
    .background(
      RoundedRectangle(cornerRadius: 5)
        .fill(Color.white.opacity(tab.isOpen ? 0.16 : 0.06))
    )
    .overlay(
      RoundedRectangle(cornerRadius: 5)
        .strokeBorder(tab.isOpen ? Color.accentColor.opacity(0.9) : Color.white.opacity(0.12), lineWidth: 1)
    )
    .contextMenu {
      if let menu { trackMenu(menu) }
    }
    .accessibilityElement(children: .contain)
    .accessibilityLabel(tab.label + " lane")
  }

  /// The tab's name (with the role mark for a track), dimmed while the lane is not audible: a click opens the lane.
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
      .opacity(tab.dimmed ? Self.silentOpacity : 1)
      .frame(height: Self.tabHeight)
      .contentShape(Rectangle())
    }
    .buttonStyle(.plain)
    .help(tab.role == nil ? "Show the chords: suggestions and palette" : "Show the \(tab.label) piano roll")
    .accessibilityLabel(tab.role.map { "Open \(tab.label), \($0.displayName) track" } ?? "Open " + tab.label)
    .accessibilityAddTraits(tab.isOpen ? .isSelected : [])
    .accessibilityValue(tab.dimmed ? "Silent" : "")   // on the button: the tab's group drops a value
  }

  /// A small square M or S: filled with `colour` while on, translucent while off. Shows `on` (the snapshot's
  /// flag); a click sends the flipped value and the answering snapshot redraws it. The click target reaches
  /// `toggleHitOutset` past the drawn square, inside the tab, without changing its layout.
  private func toggle(_ letter: String, on: Bool, colour: Color, help: String, label: String,
                      action: @escaping () -> Void) -> some View {
    Button(action: action) {
      Text(letter)
        .font(.system(size: 10, weight: .bold))
        .foregroundStyle(on ? Color.black.opacity(0.85) : Color.white.opacity(0.7))
        .frame(width: Self.toggleSize, height: Self.toggleSize)
        .background(RoundedRectangle(cornerRadius: 3).fill(on ? colour : Color.white.opacity(0.12)))
        .contentShape(Rectangle().path(in: CGRect(x: 0, y: 0, width: Self.toggleSize, height: Self.toggleSize)
          .insetBy(dx: -Self.toggleHitOutset.width, dy: -Self.toggleHitOutset.height)))
    }
    .buttonStyle(.plain)
    .help(help)
    .accessibilityLabel(label)
    .accessibilityAddTraits(.isToggle)
    .accessibilityValue(on ? "On" : "Off")
  }

  // MARK: - Grips

  /// The tab's ♫ grip: drags its lane over the whole progression (`ChordLabState.dragExport(.lane)`); dimmed and
  /// inert (no drag) with nothing to export.
  private func laneGrip(_ tab: LaneTab) -> some View {
    let grip = ExportGrip.lane(tab.lane)
    let text = ClipExport.text(for: grip, in: state.snapshot, open: state.openLane)
    return Text("♫")
      .font(.system(size: 10))
      .foregroundStyle(Color.white.opacity(text.enabled ? 0.8 : 0.25))
      .frame(width: Self.gripWidth, height: Self.tabHeight)
      .contentShape(Rectangle())
      .exportDragSource(enabled: text.enabled) { state.dragExport(grip).provider }
      .help(text.help)
      .accessibilityLabel(text.label)
  }

  /// "♫ All" at the row's trailing end: drags the chords and every track with notes, muted ones included
  /// (`ChordLabState.dragExport(.all)`); dimmed and inert without chords.
  private func allGrip() -> some View {
    let text = ClipExport.text(for: .all, in: state.snapshot, open: state.openLane)
    // One Text (a static text to accessibility, like the lane grips), so its label and help stay its own.
    return (Text("♫ ").font(.system(size: 10)) + Text("All").font(.system(size: 11, weight: .semibold)))
    .foregroundStyle(Color.white.opacity(text.enabled ? 0.85 : 0.3))
    .padding(.horizontal, 6)
    .frame(height: Self.tabHeight)
    .background(RoundedRectangle(cornerRadius: 5).fill(Color.white.opacity(0.06)))
    .overlay(RoundedRectangle(cornerRadius: 5).strokeBorder(Color.white.opacity(0.12), lineWidth: 1))
    .fixedSize()
    .contentShape(Rectangle())
    .exportDragSource(enabled: text.enabled) { state.dragExport(.all).provider }
    .help(text.help)
    .accessibilityLabel(text.label)
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
      .onSubmit { commitRename(menu.trackId) }
      .onExitCommand { cancelRename(menu.trackId) }
      .onChange(of: renameFocused) { _, focused in
        if !focused { commitRename(menu.trackId) }
      }
      .onDisappear { cancelRename(menu.trackId) }
      .accessibilityLabel("Track name")
  }

  /// Opens the field on `menu`'s track. A rename of another track still in progress is committed first (its
  /// field's disappearance then cancels nothing, `cancelRename` being for its own id); Rename on the track being
  /// renamed keeps the draft and only refocuses. The focus is cleared with the hand-over, so the new field never
  /// sees the old one's focus going as its own loss (which would commit it at once).
  private func beginRename(_ menu: TrackTabMenu) {
    if let current = renamingId, current != menu.trackId {
      commitRename(current)
      renameFocused = false
    }
    if renamingId != menu.trackId {
      draft = menu.name
      renamingId = menu.trackId
    }
    state.editingText = true
    DispatchQueue.main.async { renameFocused = true }   // the field exists on the next pass
  }

  /// Sends the new name of track `id` through `ChordLabState.renameTrack` (`LaneTabs.renameCommand`: trimmed,
  /// bounded, a blank or unchanged draft is a cancel), while `id` is the track being renamed.
  private func commitRename(_ id: Int) {
    guard renamingId == id else { return }
    renamingId = nil
    state.editingText = false
    state.renameTrack(id: id, draft: draft)
  }

  /// Closes the field of track `id` without sending, while `id` is the track being renamed (a stale field's
  /// Escape or disappearance never closes another track's rename).
  private func cancelRename(_ id: Int) {
    guard renamingId == id else { return }
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

struct LaneTabsView_Previews: PreviewProvider {
  static var previews: some View {
    LaneTabsView().environmentObject(ChordLabState(numParams: kNumParams))
      .padding().background(PaletteView.background).frame(width: 700)
  }
}
