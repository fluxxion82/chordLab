import SwiftUI

/// The Follow picker's entries, inside a `Menu` (the transport row's Follow menu, the follower panel's Change… and its
/// switch, the banner's More…): the disabled notes first (`FollowOptions.menuNotes`: the refusal of the last choice,
/// the warning that tracks playing from this ChordLab will stop, or that there is nothing to pick), then one section
/// per other ChordLab (`FollowOptions.groups`), titled with its label, one entry per lane in the picker's order
/// (`FollowOption.menuTitle`: "  ✓" on the lane this one plays, how many other ChordLabs play it). A click sends that
/// lane's follow through `ChordLabState.follow`, the only way a view sends one. Identity is the leader's id and the
/// lane's key (`FollowGroup.id`, `FollowOption.id`), never a label or a position.
struct FollowPickerItems: View {
  @EnvironmentObject var state: ChordLabState
  /// Only this ChordLab's section (the panel's switch to a copy, `FollowerSwitchAction.choose`); nil for every one.
  var leader: String?

  var body: some View {
    let link = state.linkState
    let groups = FollowOptions.groups(link)
    ForEach(FollowOptions.menuNotes(link, notice: state.followNotice), id: \.self) { note in
      Button(note) {}
        .disabled(true)
    }
    ForEach(leader.map { FollowOptions.group(leader: $0, in: groups).map { [$0] } ?? [] } ?? groups) { group in
      Section(group.title) {
        ForEach(group.options) { option in
          Button(option.menuTitle) { state.follow(option) }
        }
      }
    }
  }
}

/// The transport row's Follow menu (after Length, `TransportBarView`): the picker (`FollowPickerItems`), shown only
/// while it offers something (`FollowOptions.groups` is not empty; never in an AU, the app or a lone instance, which is
/// not an error).
struct FollowMenu: View {
  @EnvironmentObject var state: ChordLabState

  var body: some View {
    if !FollowOptions.groups(state.linkState).isEmpty {
      Menu {
        FollowPickerItems()
      } label: {
        Text(FollowOptions.menuTitle)
      }
      .fixedSize()
      .help(FollowOptions.menuHelp)
      .probeFrame("transport.follow")
    }
  }
}
