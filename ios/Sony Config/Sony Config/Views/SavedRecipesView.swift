import SwiftUI

struct SavedRecipesView: View {
    @Environment(AppState.self) private var appState
    @State private var renamingSlot: Int?
    @State private var renameText = ""

    private let totalSlots = 10

    var body: some View {
        List {
            Section {
                Text("\(appState.savedRecipes.count) of \(totalSlots) slots used")
                    .foregroundStyle(.secondary)
            }
            ForEach(0..<totalSlots, id: \.self) { slot in
                if let entry = appState.savedRecipe(forSlot: slot) {
                    SavedRecipeCard(
                        entry: entry,
                        onLoad: { Task { await appState.loadSlotToCamera(entry) } },
                        onEdit: { appState.editSlotInCustomTab(entry) },
                        onRename: { renamingSlot = slot; renameText = entry.name },
                        onDelete: { Task { await appState.deleteSlot(slot) } }
                    )
                } else {
                    HStack {
                        Text("Empty").foregroundStyle(.secondary)
                        Spacer()
                        Text("Slot \(slot + 1)").font(.caption).foregroundStyle(.secondary)
                    }
                }
            }
        }
        .navigationTitle("Saved Recipes")
        .task { await appState.loadSavedRecipes() }
        .refreshable { await appState.loadSavedRecipes() }
        .alert("Rename recipe", isPresented: Binding(
            get: { renamingSlot != nil },
            set: { if !$0 { renamingSlot = nil } }
        )) {
            TextField("Name", text: $renameText)
            Button("Save") {
                if let slot = renamingSlot {
                    Task { await appState.renameSlot(slot, name: renameText) }
                }
                renamingSlot = nil
            }
            Button("Cancel", role: .cancel) { renamingSlot = nil }
        }
    }
}

private struct SavedRecipeCard: View {
    let entry: SavedRecipeEntry
    let onLoad: () -> Void
    let onEdit: () -> Void
    let onRename: () -> Void
    let onDelete: () -> Void

    @State private var showDeleteConfirm = false

    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(alignment: .firstTextBaseline) {
                Text(entry.name).font(.headline)
                Spacer()
                Text("\(presetDisplayNames[entry.recipe.preset ?? ""] ?? entry.recipe.preset ?? "Custom") · Slot \(entry.slot + 1)")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            Text(metaLine)
                .font(.caption)
                .foregroundStyle(.secondary)

            SpecChipsView(recipe: entry.recipe)

            HStack {
                Button("Save to Camera", action: onLoad)
                    .buttonStyle(.borderedProminent)
                Button("Edit", action: onEdit)
                    .buttonStyle(.bordered)
                Button("Rename", action: onRename)
                    .buttonStyle(.bordered)
                Button("Delete", role: .destructive) { showDeleteConfirm = true }
                    .buttonStyle(.bordered)
            }
            .padding(.top, 4)
        }
        .padding(.vertical, 6)
        .confirmationDialog("Delete \"\(entry.name)\"?", isPresented: $showDeleteConfirm, titleVisibility: .visible) {
            Button("Delete", role: .destructive, action: onDelete)
            Button("Cancel", role: .cancel) {}
        }
    }

    // Mirrors buildRecipeMetaLine() in the web frontend -- same
    // "ISO 400 · Color Filter A+1 G+1" / "No Color Filter" style.
    private var metaLine: String {
        var parts: [String] = []
        if let iso = entry.recipe.iso { parts.append("ISO \(iso.displayString)") }
        let ab = entry.recipe.colorFilterAB ?? 0
        let gm = entry.recipe.colorFilterGM ?? 0
        if ab == 0 && gm == 0 {
            parts.append("No Color Filter")
        } else {
            var filterParts: [String] = []
            if ab != 0 { filterParts.append(ab > 0 ? "A+\(ab)" : "B+\(abs(ab))") }
            if gm != 0 { filterParts.append(gm > 0 ? "G+\(gm)" : "M+\(abs(gm))") }
            parts.append("Color Filter " + filterParts.joined(separator: " "))
        }
        return parts.joined(separator: " · ")
    }
}
