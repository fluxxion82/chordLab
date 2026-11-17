import SwiftUI

/// The follower's window (per-track instruments plan section 5): while this ChordLab plays a lane of another one, this
/// read-only panel replaces the editor (`ContentView`). It draws `FollowerPanelModel` and nothing of its own: the
/// headline with the lane's role colour, the status line, the switch to a copy of the other ChordLab when there is one,
/// the model's `actions` (Change…, the picker as a menu, and Stop following; the status's emphasised one drawn
/// prominent), the refusal sentence of the last choice (`ChordLabState.followNotice`), this instance's own Thru and
/// Panic (the transport row's controls), the status lines the editor's bottom row would show (incomplete blocks, the
/// last error), and the notes: where to edit, that this ChordLab's own chords are kept, and Live's routing reminder.
///
/// The content is centred and at most `maxContentWidth` wide; its texts wrap and never truncate, so from the 700 x 632
/// minimum up nothing is clipped (the render harness checks every mode, ChordLab/Tests/viewharness).
struct FollowerPanelView: View {
  @EnvironmentObject var state: ChordLabState
  let model: FollowerPanelModel

  /// The widest the panel's content gets.
  static let maxContentWidth: CGFloat = 520

  var body: some View {
    VStack(alignment: .leading, spacing: 16) {
      VStack(alignment: .leading, spacing: 6) {
        headline
          .probeFrame("panel.headline")
        wrapped(Text(model.statusLine))
          .foregroundStyle(Color.white.opacity(0.85))
          .probeFrame("panel.status")
      }
      .probeFrame("panel.row.status")

      if let offer = model.switchOffer {
        switchRow(offer)
          .probeFrame("panel.row.switch")
      }

      actionsRow
        .probeFrame("panel.row.actions")

      if let notice = state.followNotice {
        wrapped(Text(notice))
          .foregroundStyle(.orange)
          .probeFrame("panel.row.notice")
      }

      HStack(spacing: 16) {
        ThruToggle()
        PanicButton()
        Spacer(minLength: 0)
      }
      .probeFrame("panel.row.controls")

      if state.incompleteBlocks > 0 || state.lastError != nil {
        VStack(alignment: .leading, spacing: 4) {
          StatusLines()
        }
        .probeFrame("panel.row.statusLines")
      }

      Rectangle()
        .fill(Color.white.opacity(0.12))
        .frame(height: 1)

      VStack(alignment: .leading, spacing: 6) {
        if let note = model.editNote {
          wrapped(Text(note))
            .probeFrame("panel.note.edit")
        }
        if let note = model.keptNote {
          wrapped(Text(note))
            .probeFrame("panel.note.kept")
        }
        wrapped(Text(FollowerPanelModel.routingReminder))
          .probeFrame("panel.note.reminder")
      }
      .font(.callout)
      .foregroundStyle(Color.white.opacity(0.6))
      .probeFrame("panel.row.notes")
    }
    .frame(maxWidth: .infinity, alignment: .leading)
    .padding(20)
    .background(RoundedRectangle(cornerRadius: 10).fill(Color.white.opacity(0.06)))
    .frame(maxWidth: Self.maxContentWidth)
    .probeFrame("panel.content")
    .frame(maxWidth: .infinity, maxHeight: .infinity)
    .padding(12)
    .accessibilityElement(children: .contain)
    .accessibilityLabel(model.headlineText)
  }

  /// A text that wraps onto as many lines as it needs and never truncates.
  private func wrapped(_ text: Text) -> some View {
    text
      .lineLimit(nil)
      .fixedSize(horizontal: false, vertical: true)
      .frame(maxWidth: .infinity, alignment: .leading)
  }

  /// The role's colour mark (none for the chords lane) and the headline, the names in bold.
  private var headline: some View {
    HStack(alignment: .firstTextBaseline, spacing: 8) {
      if let colour = model.roleColor {
        RoundedRectangle(cornerRadius: 2)
          .fill(colour)
          .frame(width: 5, height: 16)
          .alignmentGuide(.firstTextBaseline) { $0[.bottom] - 3 }
          .accessibilityHidden(true)
      }
      wrapped(model.headline.reduce(Text(verbatim: "")) { $0 + Text(verbatim: $1.text).bold($1.bold) })
        .font(.title3)
        .foregroundStyle(.white)
    }
  }

  /// The copy's line and its button: a follow of the matching lane, or a menu over the copy's lanes.
  private func switchRow(_ offer: FollowerSwitchOffer) -> some View {
    VStack(alignment: .leading, spacing: 8) {
      wrapped(Text(offer.line))
        .foregroundStyle(Color.white.opacity(0.85))
        .probeFrame("panel.switch.line")
      Group {
        switch offer.action {
        case .follow(let option):
          Button(offer.button) { state.follow(option) }
        case .choose(let leader):
          Menu(offer.button) { FollowPickerItems(leader: leader) }
            .menuStyle(.button)
        }
      }
      .fixedSize()
      .probeFrame("panel.switch.button")
    }
  }

  /// `model.actions` in order: Change… opens the picker (a menu), Stop following sends the unfollow. The status's
  /// emphasised one sits on an accent fill (`emphasisFill`), drawn by the view rather than `.borderedProminent`, which
  /// greys out while the plug-in window is not the host's key window (the reason for `AccentSwitchToggleStyle`).
  private var actionsRow: some View {
    HStack(spacing: 12) {
      ForEach(model.actions, id: \.repair) { action in
        actionControl(action)
          .fixedSize()
          .background(RoundedRectangle(cornerRadius: 6)
            .fill(model.emphasised == action.repair ? Self.emphasisFill : Color.clear))
          .probeFrame(action.repair == .change ? "panel.action.change" : "panel.action.stop")
      }
      Spacer(minLength: 0)
    }
  }

  /// Behind the emphasised action.
  static let emphasisFill = Color.accentColor.opacity(0.75)

  @ViewBuilder private func actionControl(_ action: FollowerRepairButton) -> some View {
    switch action.repair {
    case .change:
      Menu(action.title) { FollowPickerItems() }
        .menuStyle(.button)
    case .stopFollowing:
      Button(action.title) { state.unfollow() }
    }
  }
}
