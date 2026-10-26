import SwiftUI

/// What a hovered view publishes: its bounds anchor, which side it prefers, and the tip to draw.
struct TipItem: Identifiable {
  let id: String
  let anchor: Anchor<CGRect>
  let preferAbove: Bool
  let content: AnyView
}

/// Empty except while one tip shows (the hovered view's).
struct TipPreferenceKey: PreferenceKey {
  static var defaultValue: [TipItem] = []
  static func reduce(value: inout [TipItem], nextValue: () -> [TipItem]) { value += nextValue() }
}

/// A tip that opens after the mouse rests on the view for `delay` and closes on hover exit, drawn by
/// `TipOverlay` at the editor's root — inside the window, never a popover, never hit-testable, so it
/// cannot take a click or key status. It never opens while the mouse button is down on the view, closes
/// on mouse-down, stays closed after a click until the mouse leaves and comes back (so a press, then a
/// drag of the handle, is never covered), closes the moment `suppressed` turns true and is scheduled
/// again when `suppressed` turns false with the mouse still there (entering a block over its handle,
/// then moving onto the block). The palette cells, the sequence blocks and the suggestion chips share
/// this one mechanism.
struct HoverTip<Tip: View>: ViewModifier {
  static var defaultDelay: TimeInterval { 0.6 }

  let id: String
  let delay: TimeInterval
  let suppressed: Bool
  let preferAbove: Bool
  @ViewBuilder let tip: () -> Tip

  @State private var hovering = false
  @State private var showing = false
  @State private var armed = true          // false from a mouse-down until the next hover exit
  @State private var pending: DispatchWorkItem?

  func body(content: Content) -> some View {
    content
      .onHover { over in
        hovering = over
        pending?.cancel()
        if over {
          schedule()
        } else {
          showing = false
          armed = true
        }
      }
      .simultaneousGesture(
        DragGesture(minimumDistance: 0)
          .onChanged { _ in
            armed = false
            pending?.cancel()
            showing = false
          })
      .onChange(of: suppressed) { _, now in
        pending?.cancel()
        if now { showing = false } else { schedule() }
      }
      .onDisappear { pending?.cancel(); showing = false }
      .anchorPreference(key: TipPreferenceKey.self, value: .bounds) { anchor in
        showing ? [TipItem(id: id, anchor: anchor, preferAbove: preferAbove, content: AnyView(tip()))] : []
      }
  }

  private func schedule() {
    guard hovering, armed, !suppressed else { return }
    let work = DispatchWorkItem { if hovering && armed && !suppressed { showing = true } }
    pending = work
    DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: work)
  }
}

extension View {
  /// See `HoverTip`. `id` must be unique among the views that can show a tip at once (a cell id, an
  /// event id, a chip index).
  func hoverTip<Tip: View>(id: String, suppressed: Bool = false, preferAbove: Bool = true,
                           delay: TimeInterval = HoverTip<Tip>.defaultDelay,
                           @ViewBuilder _ tip: @escaping () -> Tip) -> some View {
    modifier(HoverTip(id: id, delay: delay, suppressed: suppressed, preferAbove: preferAbove, tip: tip))
  }
}

/// The root overlay: resolves the first published tip's anchor in the editor's space and hands it to a
/// `TipCard` keyed by the tip's id, so every tip starts with fresh measurement state. Not hit-testable.
struct TipOverlay: View {
  let items: [TipItem]

  var body: some View {
    GeometryReader { proxy in
      if let item = items.first {
        TipCard(item: item, anchor: proxy[item.anchor], container: CGRect(origin: .zero, size: proxy.size))
          .id(item.id)
      }
    }
    .allowsHitTesting(false)
    .accessibilityHidden(true)               // the hovered views carry their own labels
  }
}

/// One tip: measures itself on its first layout pass and is invisible until then, so it never flashes
/// at the wrong place or at a previous tip's size; then sits at `TipGeometry.frame`. The size comes from
/// `onGeometryChange`: a size preference read with `onPreferenceChange` inside the root overlay never
/// reported (seen in the offscreen harness), which left every tip invisible.
private struct TipCard: View {
  static let gap: CGFloat = 6
  let item: TipItem
  let anchor: CGRect
  let container: CGRect
  @State private var size = CGSize.zero

  var body: some View {
    let frame = TipGeometry.frame(anchor: anchor, tipSize: size, in: container,
                                  preferAbove: item.preferAbove, gap: Self.gap)
    item.content
      .modifier(TipChrome())
      .fixedSize()
      .onGeometryChange(for: CGSize.self) { $0.size } action: { size = $0 }
      .position(x: frame.midX, y: frame.midY)
      .opacity(size == .zero ? 0 : 1)
  }
}

/// The look the popovers had: a dark card with a hairline and a shadow.
private struct TipChrome: ViewModifier {
  func body(content: Content) -> some View {
    content
      .padding(12)
      .background(RoundedRectangle(cornerRadius: 8).fill(Color(red: 0.10, green: 0.12, blue: 0.24)))
      .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(Color.white.opacity(0.18), lineWidth: 1))
      .shadow(color: .black.opacity(0.45), radius: 10, y: 4)
  }
}
