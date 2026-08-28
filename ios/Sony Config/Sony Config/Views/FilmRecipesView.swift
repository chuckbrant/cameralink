import SwiftUI

struct FilmRecipesView: View {
    @Environment(AppState.self) private var appState

    var body: some View {
        List {
            ForEach(appState.presetsByGroup, id: \.group) { bucket in
                Section(bucket.group) {
                    ForEach(bucket.items) { preset in
                        PresetCard(preset: preset) {
                            Task { await appState.pushPresetToCamera(preset) }
                        }
                    }
                }
            }
        }
        .navigationTitle("Film Recipes")
        .task { if appState.presets.isEmpty { await appState.loadPresets() } }
        .refreshable { await appState.loadPresets() }
    }
}

private struct PresetCard: View {
    let preset: FilmPreset
    let onDownload: () -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(alignment: .firstTextBaseline) {
                Text(preset.name).font(.headline)
                Spacer()
                Text(presetDisplayNames[preset.preset ?? ""] ?? preset.preset ?? "")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            let meta = [preset.baseISO.map { "ISO \($0)" }, preset.whiteBalanceNote].compactMap { $0 }
            if !meta.isEmpty {
                Text(meta.joined(separator: " · "))
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            SpecChipsView(recipe: preset.asRecipe)

            if let notes = preset.notes {
                Text(notes).font(.caption).foregroundStyle(.secondary)
            }

            Button("Save to Camera", action: onDownload)
                .buttonStyle(.borderedProminent)
                .padding(.top, 4)
        }
        .padding(.vertical, 6)
    }
}

// Shared spec-chip breakdown -- mirrors buildSpecsHtml() in the web
// frontend: only shows a field's chip when the recipe actually defines it,
// and always shows ISO/Color Filter (including "0") on Saved Recipes since
// those always have a concrete value for every field, unlike a preset that
// may have no source data for a given control.
struct SpecChipsView: View {
    let recipe: Recipe

    var body: some View {
        FlowChips {
            ForEach(chips, id: \.self) { chip in
                Text(chip)
                    .font(.caption2)
                    .padding(.horizontal, 8)
                    .padding(.vertical, 4)
                    .background(Color.secondary.opacity(0.15))
                    .clipShape(Capsule())
            }
        }
    }

    private var chips: [String] {
        var out: [String] = []
        if let v = recipe.contrast { out.append("Contrast \(v)") }
        if let v = recipe.highlights { out.append("Highlights \(v)") }
        if let v = recipe.shadows { out.append("Shadows \(v)") }
        if let v = recipe.fade { out.append("Fade \(v)") }
        if let v = recipe.saturation { out.append("Saturation \(v)") }
        if let v = recipe.sharpness { out.append("Sharpness \(v)") }
        if let v = recipe.sharpnessRange { out.append("Sharp. Range \(v)") }
        if let v = recipe.clarity { out.append("Clarity \(v)") }
        if let v = recipe.whiteBalanceMode { out.append("WB \(v)") }
        if let v = recipe.whiteBalanceColorTempK { out.append("Kelvin \(v)K") }
        if let v = recipe.colorFilterAB {
            out.append("Filter A-B \(v == 0 ? "0" : (v > 0 ? "A\(v)" : "B\(abs(v))"))")
        }
        if let v = recipe.colorFilterGM {
            out.append("Filter G-M \(v == 0 ? "0" : (v > 0 ? "G\(v)" : "M\(abs(v))"))")
        }
        if let v = recipe.iso { out.append("ISO \(v.displayString)") }
        if let v = recipe.aspectRatio { out.append("Aspect \(v)") }
        if let v = recipe.fileType { out.append("File Type \(v)") }
        return out
    }
}

// A simple wrapping "chip" layout -- SwiftUI's Layout protocol supports
// being used directly as a container view (`FlowChips { content }`), no
// separate View conformance needed.
struct FlowChips: Layout {
    var spacing: CGFloat = 6

    func sizeThatFits(proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) -> CGSize {
        let maxWidth = proposal.width ?? .infinity
        var x: CGFloat = 0, y: CGFloat = 0, rowHeight: CGFloat = 0
        for subview in subviews {
            let size = subview.sizeThatFits(.unspecified)
            if x + size.width > maxWidth, x > 0 {
                x = 0; y += rowHeight + spacing; rowHeight = 0
            }
            x += size.width + spacing
            rowHeight = max(rowHeight, size.height)
        }
        return CGSize(width: maxWidth, height: y + rowHeight)
    }

    func placeSubviews(in bounds: CGRect, proposal: ProposedViewSize, subviews: Subviews, cache: inout ()) {
        var x: CGFloat = bounds.minX, y: CGFloat = bounds.minY, rowHeight: CGFloat = 0
        for subview in subviews {
            let size = subview.sizeThatFits(.unspecified)
            if x + size.width > bounds.maxX, x > bounds.minX {
                x = bounds.minX; y += rowHeight + spacing; rowHeight = 0
            }
            subview.place(at: CGPoint(x: x, y: y), anchor: .topLeading, proposal: .unspecified)
            x += size.width + spacing
            rowHeight = max(rowHeight, size.height)
        }
    }
}
