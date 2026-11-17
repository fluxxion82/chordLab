import CoreGraphics

/// Where the follower panel's card goes in the window (`FollowerPanelView`): centred, at most `maxCardWidth` wide and
/// never wider than the window inside its `margin`. The card is always laid out at the width `cardWidth` gives, which
/// is definite whatever the panel is proposed (no width, or no finite one, gives the cap): laid out without a width, its
/// texts would each run on one line and the card would be as wide as its longest text (the routing reminder, about
/// 890 pt), wider than the window. Pure, so it is tested.
enum FollowerPanelGeometry {
  /// The widest the card gets.
  static let maxCardWidth: CGFloat = 520
  /// Between the window's edges and the card.
  static let margin: CGFloat = 12

  /// The card's width in a panel proposed `panelWidth` wide (nil: unspecified).
  static func cardWidth(panelWidth: CGFloat?) -> CGFloat {
    guard let width = panelWidth, width.isFinite else { return maxCardWidth }
    return max(0, min(maxCardWidth, width - 2 * margin))
  }

  /// The height the card is offered in a panel proposed `panelHeight` high: nil (its own height) when none, or no
  /// finite one, is proposed.
  static func cardHeight(panelHeight: CGFloat?) -> CGFloat? {
    guard let height = panelHeight, height.isFinite else { return nil }
    return max(0, height - 2 * margin)
  }

  /// The panel's size for a proposal, given the card's: the proposal along each side it is given on (the panel takes
  /// the window and centres the card in it), the card and its margins along a side it is not (or not finitely).
  static func panelSize(proposedWidth: CGFloat?, proposedHeight: CGFloat?, card: CGSize) -> CGSize {
    func side(_ proposed: CGFloat?, _ cardSide: CGFloat) -> CGFloat {
      if let proposed, proposed.isFinite { return proposed }
      return cardSide + 2 * margin
    }
    return CGSize(width: side(proposedWidth, card.width), height: side(proposedHeight, card.height))
  }
}
