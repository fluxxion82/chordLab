import SwiftUI

/// The Follow banner under the key bar (per-track instruments plan sections 2 and 5): a fresh ChordLab, or a copy of
/// another one, offers to play one track of the other ChordLab. It draws `FollowOffer` and nothing of its own: the
/// sentence, one button per lane of the first ChordLab in the picker's order (with its role's colour), More… (the
/// whole picker, a menu) when there are more, and Not now (`ChordLabState.dismissFollowBanner`: not again in this
/// document). A click sends that lane's follow through `ChordLabState.follow`. The refusal sentence of the last choice
/// (`ChordLabState.followNotice`) shows under it until the next choice.
///
/// In a window shorter than `FollowOffer.compactHeight` it takes its compact form (`FollowOffer.text(compact:)`,
/// callout text, small buttons, less padding) on one line, so the palette keeps a full row in view at the 700 x 632
/// minimum (measured by the render harness, ChordLab/Tests/viewharness). Where the line does not fit (long track
/// names) the buttons go under the sentence rather than past the window's edge.
struct FollowBannerView: View {
  @EnvironmentObject var state: ChordLabState
  let offer: FollowOffer
  var compact = false

  var body: some View {
    VStack(alignment: .leading, spacing: compact ? 2 : 6) {
      ViewThatFits(in: .horizontal) {
        HStack(spacing: 8) {
          sentence
          buttons
          Spacer(minLength: 0)
          dismiss
        }
        VStack(alignment: .leading, spacing: compact ? 4 : 6) {
          sentence
          HStack(spacing: 8) {
            buttons
            Spacer(minLength: 0)
            dismiss
          }
        }
      }
      if let notice = state.followNotice {
        Text(notice)
          .foregroundStyle(.orange)
          .lineLimit(nil)
          .fixedSize(horizontal: false, vertical: true)
          .probeFrame("banner.notice")
      }
    }
    .font(compact ? .callout : .body)
    .controlSize(compact ? .small : .regular)
    .padding(.horizontal, 10)
    .padding(.vertical, compact ? 3 : 6)
    .background(RoundedRectangle(cornerRadius: 6).fill(Color.white.opacity(0.07)))
    .accessibilityElement(children: .contain)
    .accessibilityLabel(offer.text(compact: false))
    .probeFrame("banner")
  }

  private var sentence: some View {
    Text(offer.text(compact: compact))
      .foregroundStyle(.white)
      .lineLimit(1)
      .fixedSize()
      .probeFrame("banner.sentence")
  }

  /// One button per offered lane, then More… when the picker holds more.
  private var buttons: some View {
    HStack(spacing: 6) {
      ForEach(offer.buttons) { option in
        Button { state.follow(option) } label: {
          HStack(spacing: 5) {
            if let role = option.role {
              RoundedRectangle(cornerRadius: 1.5)
                .fill(TrackStyle.color(role: role))
                .frame(width: 4, height: compact ? 10 : 12)
                .accessibilityHidden(true)
            }
            Text(option.name)
              .lineLimit(1)
          }
        }
        .fixedSize()
      }
      if offer.showsMore {
        Menu(FollowOffer.moreTitle) { FollowPickerItems() }
          .menuStyle(.button)
          .fixedSize()
      }
    }
    .probeFrame("banner.buttons")
  }

  private var dismiss: some View {
    Button(FollowOffer.dismissTitle) { state.dismissFollowBanner() }
      .buttonStyle(.borderless)
      .foregroundStyle(Color.white.opacity(0.7))
      .fixedSize()
  }
}
