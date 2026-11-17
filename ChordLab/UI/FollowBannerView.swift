import SwiftUI

/// The Follow banner under the key bar (per-track instruments plan sections 2 and 5): a fresh ChordLab, or a copy of
/// another one, offers to play one track of the other ChordLab. It draws `FollowOffer` and nothing of its own: the
/// sentence, one button per lane of the first ChordLab in the picker's order (with its role's colour) as far as they
/// fit on its line, More… (the whole picker, a menu) when there are more, and Not now (`ChordLabState.dismissFollowBanner`: not again in this
/// document). A click sends that lane's follow through `ChordLabState.follow`. The buttons show `FollowOption.label` (a
/// name bounded by `FollowLabels`) and, when another ChordLab already plays that track, `FollowOption.tooltip`. The
/// refusal sentence of the last choice (`ChordLabState.followNotice`) shows under it until the next choice, or in a
/// short window in the sentence's place (`noticeInline`).
///
/// In a window shorter than `FollowOffer.compactHeight` it takes its compact form (`FollowOffer.text(compact:)`,
/// callout text, small buttons, less padding) on one line, so the palette keeps a full row in view at the 700 x 632
/// minimum (measured by the render harness, ChordLab/Tests/viewharness). The line keeps as many buttons as fit and
/// leaves the rest to More…; only when not even one fits do the buttons go under the sentence.
struct FollowBannerView: View {
  @EnvironmentObject var state: ChordLabState
  let offer: FollowOffer
  var compact = false
  /// The refusal sentence takes the sentence's place on the one line (`EditorRows.noticeInline`), instead of a line
  /// of its own under the banner.
  var noticeInline = false

  var body: some View {
    VStack(alignment: .leading, spacing: compact ? 2 : 6) {
      // One line with as many buttons as fit (in the picker's order; More… then covers the rest), else the
      // sentence above every button.
      ViewThatFits(in: .horizontal) {
        ForEach(Array(stride(from: offer.buttons.count, through: 1, by: -1)), id: \.self) { shown in
          HStack(spacing: 8) {
            sentence
            buttons(shown: shown)
            Spacer(minLength: 0)
            dismiss
          }
        }
        VStack(alignment: .leading, spacing: compact ? 4 : 6) {
          sentence
          HStack(spacing: 8) {
            buttons(shown: offer.buttons.count)
            Spacer(minLength: 0)
            dismiss
          }
        }
      }
      if let notice = state.followNotice, !noticeInline {
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
    let notice = noticeInline ? state.followNotice : nil
    return Text(notice ?? offer.text(compact: compact))
      .foregroundStyle(notice == nil ? Color.white : Color.orange)
      .lineLimit(1)
      .fixedSize()
      .probeFrame("banner.sentence")
  }

  /// The first `shown` offered lanes, one button each, then More… when the picker holds more
  /// (`FollowOffer.showsMore(shown:)`).
  private func buttons(shown: Int) -> some View {
    HStack(spacing: 6) {
      ForEach(offer.buttons.prefix(shown)) { option in
        Button { state.follow(option) } label: {
          HStack(spacing: 5) {
            if let role = option.role {
              RoundedRectangle(cornerRadius: 1.5)
                .fill(TrackStyle.color(role: role))
                .frame(width: 4, height: compact ? 10 : 12)
                .accessibilityHidden(true)
            }
            Text(option.label)
              .lineLimit(1)
          }
        }
        .fixedSize()
        .help(option.tooltip ?? "")
      }
      if offer.showsMore(shown: shown) {
        Menu(FollowOffer.moreTitle) { FollowPickerItems() }
          .menuStyle(.button)
          .fixedSize()
          .probeFrame("banner.more")
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
