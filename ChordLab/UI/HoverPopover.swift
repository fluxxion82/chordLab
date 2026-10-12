import SwiftUI

/// A popover that opens after the mouse rests on the view for `delay` and closes on hover exit. Never
/// opens while `suppressed` (a pressed cell, a block being dragged), and closes the moment that becomes
/// true, so a popover never sits over a gesture in progress. The palette cells, the sequence blocks and
/// the suggestion chips share this one mechanism.
struct HoverPopover<Popover: View>: ViewModifier {
  static var defaultDelay: TimeInterval { 0.25 }

  let delay: TimeInterval
  let suppressed: Bool
  let arrowEdge: Edge
  @ViewBuilder let popover: () -> Popover

  @State private var hovering = false
  @State private var showing = false
  @State private var pending: DispatchWorkItem?

  func body(content: Content) -> some View {
    content
      .onHover { over in
        hovering = over
        pending?.cancel()
        if over && !suppressed {
          let work = DispatchWorkItem { if hovering && !suppressed { showing = true } }
          pending = work
          DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: work)
        } else {
          showing = false
        }
      }
      .onChange(of: suppressed) { _, now in
        if now {
          pending?.cancel()
          showing = false
        }
      }
      .popover(isPresented: $showing, arrowEdge: arrowEdge, content: popover)
  }
}

extension View {
  /// See `HoverPopover`.
  func hoverPopover<Popover: View>(suppressed: Bool = false, arrowEdge: Edge = .top,
                                   delay: TimeInterval = HoverPopover<Popover>.defaultDelay,
                                   @ViewBuilder _ popover: @escaping () -> Popover) -> some View {
    modifier(HoverPopover(delay: delay, suppressed: suppressed, arrowEdge: arrowEdge, popover: popover))
  }
}
