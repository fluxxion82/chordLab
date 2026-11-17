import SwiftUI

/// The follower's window (per-track instruments plan section 5): while this ChordLab plays a lane of another one, this
/// read-only panel replaces the editor (`ContentView`). It draws `FollowerPanelModel` and nothing of its own: the
/// headline with the lane's role colour, the status line (in orange when something must be repaired, `statusTone`), the switch to a copy of the other ChordLab when there is one,
/// the model's `actions` (Change…, the picker as a menu, and Stop following; the status's emphasised one drawn
/// prominent), the refusal sentence of the last choice (`ChordLabState.followNotice`), this instance's own Thru and
/// Panic (the transport row's controls), the status lines the editor's bottom row would show (incomplete blocks, the
/// last error), and the notes: where to edit, that this ChordLab's own chords are kept, and Live's routing reminder.
///
/// The content is a card centred in the window, at most `maxContentWidth` wide and never wider than the window
/// (`FollowerPanelGeometry`, through `FollowerPanelLayout`, which always lays the card out at a definite width, whatever
/// the panel is proposed); its texts wrap and never truncate, so from the 700 x 632 minimum up nothing is clipped (the
/// render harness checks every mode, ChordLab/Tests/viewharness).
struct FollowerPanelView: View {
  @EnvironmentObject var state: ChordLabState
  let model: FollowerPanelModel

  /// The widest the panel's content gets.
  static let maxContentWidth = FollowerPanelGeometry.maxCardWidth

  var body: some View {
    FollowerPanelLayout {
      card
        .probeFrame("panel.content")
    }
    .accessibilityElement(children: .contain)
    .accessibilityLabel(model.headlineText)
  }

  /// The headline, the rows and the notes on their rounded fill.
  private var card: some View {
    VStack(alignment: .leading, spacing: 16) {
      VStack(alignment: .leading, spacing: 6) {
        headline
          .probeFrame("panel.headline")
        wrapped(Text(model.statusLine))
          .foregroundStyle(model.statusTone == .attention ? Color.orange : Color.white.opacity(0.85))
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

      if EditorRows.showsStatusLines(incompleteBlocks: state.incompleteBlocks, lastError: state.lastError) {
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
      .lineLimit(1)
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

/// Lays the panel out by `FollowerPanelGeometry`: its one subview, the card, centred and measured and placed at the width
/// the geometry gives, which is definite whatever the panel is proposed. A `.frame(maxWidth:)` in its place measured the
/// card without a width whenever the panel was measured without one (its ideal size): one line per text, the card as
/// wide as its longest text, and the panel reported the height of that unwrapped card, shorter than the card it draws.
/// The unwrapped card has the geometry of the band seen in Live.
private struct FollowerPanelLayout: Layout {
  func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
    let card = subviews.first?.sizeThatFits(cardProposal(width: proposal.width, height: proposal.height)) ?? .zero
    return FollowerPanelGeometry.panelSize(proposedWidth: proposal.width, proposedHeight: proposal.height, card: card)
  }

  func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
    subviews.first?.place(at: CGPoint(x: bounds.midX, y: bounds.midY), anchor: .center,
                          proposal: cardProposal(width: bounds.width, height: bounds.height))
  }

  private func cardProposal(width: CGFloat?, height: CGFloat?) -> ProposedViewSize {
    ProposedViewSize(width: FollowerPanelGeometry.cardWidth(panelWidth: width),
                     height: FollowerPanelGeometry.cardHeight(panelHeight: height))
  }
}
