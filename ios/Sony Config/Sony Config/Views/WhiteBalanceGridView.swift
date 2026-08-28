import SwiftUI

// Ports wbGridCornerColor()/drawWbGrid()/setupWbGridInteraction() from
// server/public/index.html -- same 15x15 grid, same corner colors, same
// -7...7 axis range, so this matches the camera's own on-screen fine-tune
// grid exactly (see docs/SDK_CAPABILITIES.md for the raw-value calibration
// behind colorFilterAB/GM).
private let wbGridMin = -7
private let wbGridMax = 7
private let wbGridN = wbGridMax - wbGridMin + 1

private func wbLerp(_ a: Double, _ b: Double, _ t: Double) -> Double { a + (b - a) * t }

private func wbCornerColor(xt: Double, yt: Double) -> Color {
    let c00 = (70.0, 190.0, 190.0)   // B, G (top-left)
    let c10 = (190.0, 210.0, 60.0)   // A, G (top-right)
    let c01 = (90.0, 90.0, 220.0)    // B, M (bottom-left)
    let c11 = (210.0, 70.0, 160.0)   // A, M (bottom-right)
    let topR = wbLerp(c00.0, c10.0, xt), topG = wbLerp(c00.1, c10.1, xt), topB = wbLerp(c00.2, c10.2, xt)
    let botR = wbLerp(c01.0, c11.0, xt), botG = wbLerp(c01.1, c11.1, xt), botB = wbLerp(c01.2, c11.2, xt)
    let r = wbLerp(topR, botR, yt), g = wbLerp(topG, botG, yt), b = wbLerp(topB, botB, yt)
    return Color(red: r / 255, green: g / 255, blue: b / 255)
}

struct WhiteBalanceGridView: View {
    var ab: Int
    var gm: Int
    var onSet: (Int, Int) -> Void

    var body: some View {
        VStack(spacing: 8) {
            Text(readoutText).font(.caption).monospaced()

            HStack {
                Spacer()
                Button { step(gmDelta: 1) } label: { Image(systemName: "chevron.up") }
                    .buttonStyle(.bordered)
                Spacer()
            }
            HStack(spacing: 8) {
                Button { step(abDelta: -1) } label: { Image(systemName: "chevron.left") }
                    .buttonStyle(.bordered)

                GeometryReader { geo in
                    let size = min(geo.size.width, geo.size.height)
                    let cell = size / Double(wbGridN)
                    ZStack {
                        Canvas { context, _ in
                            for row in 0..<wbGridN {
                                for col in 0..<wbGridN {
                                    let xt = Double(col) / Double(wbGridN - 1)
                                    let yt = Double(row) / Double(wbGridN - 1)
                                    let rect = CGRect(x: Double(col) * cell, y: Double(row) * cell, width: cell + 1, height: cell + 1)
                                    context.fill(Path(rect), with: .color(wbCornerColor(xt: xt, yt: yt)))
                                }
                            }
                            var gridPath = Path()
                            for i in 0...wbGridN {
                                let p = Double(i) * cell
                                gridPath.move(to: CGPoint(x: p, y: 0)); gridPath.addLine(to: CGPoint(x: p, y: size))
                                gridPath.move(to: CGPoint(x: 0, y: p)); gridPath.addLine(to: CGPoint(x: size, y: p))
                            }
                            context.stroke(gridPath, with: .color(.black.opacity(0.25)), lineWidth: 1)

                            // Odd cell count (15): true center sits mid-cell, not on a
                            // boundary -- offset by half a cell to match.
                            let centerPos = (Double(wbGridN - 1) / 2 + 0.5) * cell
                            var centerPath = Path()
                            centerPath.move(to: CGPoint(x: centerPos, y: 0)); centerPath.addLine(to: CGPoint(x: centerPos, y: size))
                            centerPath.move(to: CGPoint(x: 0, y: centerPos)); centerPath.addLine(to: CGPoint(x: size, y: centerPos))
                            context.stroke(centerPath, with: .color(.white.opacity(0.5)), lineWidth: 1.5)

                            let col = ab - wbGridMin
                            let row = wbGridMax - gm
                            let cx = Double(col) * cell + cell / 2
                            let cy = Double(row) * cell + cell / 2
                            let markerRect = CGRect(x: cx - cell * 0.35, y: cy - cell * 0.35, width: cell * 0.7, height: cell * 0.7)
                            context.fill(Path(ellipseIn: markerRect), with: .color(Color(red: 1, green: 0.23, blue: 0.19)))
                            context.stroke(Path(ellipseIn: markerRect), with: .color(.white), lineWidth: 2)
                        }
                    }
                    .gesture(
                        DragGesture(minimumDistance: 0)
                            .onChanged { value in
                                var col = Int(value.location.x / cell)
                                var row = Int(value.location.y / cell)
                                col = max(0, min(wbGridN - 1, col))
                                row = max(0, min(wbGridN - 1, row))
                                onSet(col + wbGridMin, wbGridMax - row)
                            }
                    )
                }
                .frame(width: 210, height: 210)

                Button { step(abDelta: 1) } label: { Image(systemName: "chevron.right") }
                    .buttonStyle(.bordered)
            }
            HStack {
                Spacer()
                Button { step(gmDelta: -1) } label: { Image(systemName: "chevron.down") }
                    .buttonStyle(.bordered)
                Spacer()
            }

            Button("Pos. Reset") { onSet(0, 0) }
                .buttonStyle(.bordered)
        }
    }

    private var readoutText: String {
        let abLabel = ab == 0 ? "0" : (ab > 0 ? "A\(ab)" : "B\(abs(ab))")
        let gmLabel = gm == 0 ? "0" : (gm > 0 ? "G\(gm)" : "M\(abs(gm))")
        return "A-B: \(abLabel)   G-M: \(gmLabel)"
    }

    private func step(abDelta: Int = 0, gmDelta: Int = 0) {
        let newAB = max(wbGridMin, min(wbGridMax, ab + abDelta))
        let newGM = max(wbGridMin, min(wbGridMax, gm + gmDelta))
        onSet(newAB, newGM)
    }
}
