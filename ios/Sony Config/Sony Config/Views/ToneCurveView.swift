import SwiftUI

// Ports the exact curve math and drag behavior from
// server/public/index.html's drawToneCurve()/setupToneCurveInteraction()
// so the native widget matches the web one point for point.
private func tcContrastCurve(_ x: Double, _ contrast: Double) -> Double {
    let k = (contrast / 9) * 4
    if abs(k) < 0.001 { return x }
    func sig(_ v: Double) -> Double { 1 / (1 + exp(-k * (v - 0.5))) }
    return (sig(x) - sig(0)) / (sig(1) - sig(0))
}

private func tcBump(_ x: Double, _ center: Double, _ width: Double) -> Double {
    let d = (x - center) / width
    return exp(-d * d * 4)
}

private func toneCurveY(_ x: Double, contrast: Double, shadows: Double, highlights: Double, fade: Double) -> Double {
    var y = tcContrastCurve(x, contrast)
    y += (shadows / 9) * 0.18 * tcBump(x, 0.28, 0.28)
    y += (highlights / 9) * 0.18 * tcBump(x, 0.72, 0.28)
    let fadeAmt = (fade / 9) * 0.22
    y = y * (1 - fadeAmt) + fadeAmt * tcBump(x, 0, 0.5)
    return max(0, min(1, y))
}

private struct TCPoint {
    let key: WritableKeyPath<Recipe, Int?>
    let x: Double
    let min: Int
    let max: Int
    let label: String
}

private let tcPoints: [TCPoint] = [
    TCPoint(key: \.fade, x: 0.0, min: 0, max: 9, label: "Blacks"),
    TCPoint(key: \.shadows, x: 0.28, min: -9, max: 9, label: "Shadows"),
    TCPoint(key: \.highlights, x: 0.72, min: -9, max: 9, label: "Highlights"),
]

struct ToneCurveView: View {
    @Binding var recipe: Recipe
    var onChangeCommitted: () -> Void = {}

    @State private var dragPointIndex: Int?
    @State private var dragStartY: CGFloat = 0
    @State private var dragStartVal: Int = 0

    private var contrast: Double { Double(recipe.contrast ?? 0) }
    private var shadows: Double { Double(recipe.shadows ?? 0) }
    private var highlights: Double { Double(recipe.highlights ?? 0) }
    private var fade: Double { Double(recipe.fade ?? 0) }

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            GeometryReader { geo in
                let w = geo.size.width, h = geo.size.height
                Canvas { context, size in
                    context.fill(Path(CGRect(origin: .zero, size: size)), with: .color(.black))

                    var gridPath = Path()
                    for i in 1..<4 {
                        let gx = w * Double(i) / 4, gy = h * Double(i) / 4
                        gridPath.move(to: CGPoint(x: gx, y: 0)); gridPath.addLine(to: CGPoint(x: gx, y: h))
                        gridPath.move(to: CGPoint(x: 0, y: gy)); gridPath.addLine(to: CGPoint(x: w, y: gy))
                    }
                    context.stroke(gridPath, with: .color(.white.opacity(0.12)), lineWidth: 1)
                    context.stroke(Path(CGRect(x: 0.5, y: 0.5, width: w - 1, height: h - 1)), with: .color(.white.opacity(0.35)))

                    var curvePath = Path()
                    for i in 0...100 {
                        let x = Double(i) / 100
                        let y = toneCurveY(x, contrast: contrast, shadows: shadows, highlights: highlights, fade: fade)
                        let p = CGPoint(x: x * w, y: h - y * h)
                        if i == 0 { curvePath.move(to: p) } else { curvePath.addLine(to: p) }
                    }
                    context.stroke(curvePath, with: .color(.white), lineWidth: 2)

                    // Fixed "Whites" anchor -- Sony has no independent whites control.
                    context.fill(Path(ellipseIn: CGRect(x: w - 5, y: -5, width: 10, height: 10)), with: .color(.gray))

                    for p in tcPoints {
                        let y = toneCurveY(p.x, contrast: contrast, shadows: shadows, highlights: highlights, fade: fade)
                        let px = p.x * w, py = h - y * h
                        let dotRect = CGRect(x: px - 7, y: py - 7, width: 14, height: 14)
                        context.fill(Path(ellipseIn: dotRect), with: .color(.white))
                        context.stroke(Path(ellipseIn: dotRect), with: .color(.black), lineWidth: 1.5)
                    }
                }
                .gesture(
                    DragGesture(minimumDistance: 0)
                        .onChanged { value in
                            if dragPointIndex == nil {
                                // Nearest point to the touch-down x, matching findPoint()'s
                                // 40px hit-test radius in the web version (scaled to width).
                                var closest = 0
                                var closestDist = Double.infinity
                                for (i, p) in tcPoints.enumerated() {
                                    let d = abs(value.startLocation.x - p.x * w)
                                    if d < closestDist { closestDist = d; closest = i }
                                }
                                guard closestDist < 40 else { return }
                                dragPointIndex = closest
                                dragStartY = value.startLocation.y
                                dragStartVal = recipe[keyPath: tcPoints[closest].key] ?? 0
                            }
                            guard let idx = dragPointIndex else { return }
                            let point = tcPoints[idx]
                            let dy = dragStartY - value.location.y
                            let range = Double(point.max - point.min)
                            var newVal = Double(dragStartVal) + (dy / h) * range * 1.6
                            newVal = min(Double(point.max), max(Double(point.min), newVal.rounded()))
                            recipe[keyPath: point.key] = Int(newVal)
                        }
                        .onEnded { _ in
                            dragPointIndex = nil
                            onChangeCommitted()
                        }
                )
            }
            .frame(height: 220)
            .background(Color.black)
            .clipShape(RoundedRectangle(cornerRadius: 8))

            HStack {
                Text("Blacks / Shadows").font(.caption).foregroundStyle(.secondary)
                Spacer()
                Text("Highlights / Whites").font(.caption).foregroundStyle(.secondary)
            }

            Text("Contrast \(recipe.contrast ?? 0) · Highlights \(recipe.highlights ?? 0) · Shadows \(recipe.shadows ?? 0) · Blacks/Fade \(recipe.fade ?? 0)")
                .font(.caption)
                .foregroundStyle(.secondary)

            Slider(value: Binding(
                get: { Double(recipe.contrast ?? 0) },
                set: { recipe.contrast = Int($0.rounded()); onChangeCommitted() }
            ), in: -9...9, step: 1)

            Text("Drag the white dots on the curve to adjust Blacks/Fade, Shadows, and Highlights. Contrast bends the whole curve.")
                .font(.caption)
                .foregroundStyle(.secondary)

            ScrollView(.horizontal, showsIndicators: false) {
                HStack {
                    Button("Increase Contrast") { applyPreset(6, 0, 0, 0) }
                    Button("Lifted Shadows") { applyPreset(0, 0, 0, 6) }
                    Button("Increase Highlights") { applyPreset(0, 6, 0, 0) }
                    Button("Increase Shadows") { applyPreset(0, 0, 6, 0) }
                    Button("Reset", role: .destructive) { applyPreset(0, 0, 0, 0) }
                }
                .buttonStyle(.bordered)
            }
        }
    }

    private func applyPreset(_ contrast: Int, _ highlights: Int, _ shadows: Int, _ fade: Int) {
        recipe.contrast = contrast
        recipe.highlights = highlights
        recipe.shadows = shadows
        recipe.fade = fade
        onChangeCommitted()
    }
}
