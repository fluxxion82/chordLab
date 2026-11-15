import Foundation

/// An arrow key's direction. One type for the key (`KeyInput.Key.arrow`), the command (`KeyCommand.nudge`) and
/// the nudge itself (`RollGroupMath.NudgeDirection` is this type), in its own file so the roll's math does not
/// depend on the key map.
enum ArrowDirection: Hashable {
  case left, right, up, down
}
