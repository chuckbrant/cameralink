import SwiftUI

struct CustomView: View {
    @Environment(AppState.self) private var appState
    @State private var showSaveRecipeAlert = false
    @State private var newRecipeName = ""

    var body: some View {
        @Bindable var appState = appState
        Form {
            Section("Preset") {
                Picker("Preset", selection: Binding(
                    get: { appState.workingRecipe.preset ?? "ST" },
                    set: { appState.workingRecipe.preset = $0 }
                )) {
                    ForEach(presetOptions, id: \.value) { opt in
                        Text(opt.label).tag(opt.value)
                    }
                }
            }

            Section("Tone Curve") {
                ToneCurveView(recipe: $appState.workingRecipe)
            }

            Section("Detail") {
                sliderRow("Saturation", value: Binding(
                    get: { appState.workingRecipe.saturation ?? 0 },
                    set: { appState.workingRecipe.saturation = $0 }
                ), range: -9...9)
                sliderRow("Sharpness", value: Binding(
                    get: { appState.workingRecipe.sharpness ?? 0 },
                    set: { appState.workingRecipe.sharpness = $0 }
                ), range: 0...9)
                sliderRow("Sharpness Range", value: Binding(
                    get: { appState.workingRecipe.sharpnessRange ?? 1 },
                    set: { appState.workingRecipe.sharpnessRange = $0 }
                ), range: 1...5)
                sliderRow("Clarity", value: Binding(
                    get: { appState.workingRecipe.clarity ?? 0 },
                    set: { appState.workingRecipe.clarity = $0 }
                ), range: 0...9)
            }

            Section("White Balance") {
                Picker("Mode", selection: Binding(
                    get: { appState.workingRecipe.whiteBalanceMode ?? "AWB" },
                    set: { appState.workingRecipe.whiteBalanceMode = $0 }
                )) {
                    ForEach(wbModeOptions, id: \.value) { opt in
                        Text(opt.label).tag(opt.value)
                    }
                }
                HStack {
                    Text("Color Temp (K)")
                    Spacer()
                    TextField("", value: Binding(
                        get: { appState.workingRecipe.whiteBalanceColorTempK },
                        set: { appState.workingRecipe.whiteBalanceColorTempK = $0 }
                    ), format: .number)
                    .keyboardType(.numberPad)
                    .multilineTextAlignment(.trailing)
                    .frame(width: 100)
                }

                WhiteBalanceGridView(
                    ab: appState.workingRecipe.colorFilterAB ?? 0,
                    gm: appState.workingRecipe.colorFilterGM ?? 0,
                    onSet: { ab, gm in
                        appState.workingRecipe.colorFilterAB = ab
                        appState.workingRecipe.colorFilterGM = gm
                    }
                )
                .frame(maxWidth: .infinity)
                .padding(.vertical, 8)
            }

            Section("ISO") {
                Picker("Sensitivity", selection: Binding(
                    get: { appState.workingRecipe.iso ?? .auto },
                    set: { appState.workingRecipe.iso = $0 }
                )) {
                    ForEach(ISOValue.selectable, id: \.self) { iso in
                        Text(iso.displayString).tag(iso)
                    }
                }
            }

            Section("Other Settings") {
                Picker("Aspect Ratio", selection: Binding(
                    get: { appState.workingRecipe.aspectRatio ?? "3:2" },
                    set: { appState.workingRecipe.aspectRatio = $0 }
                )) {
                    ForEach(aspectRatioOptions, id: \.self) { Text($0).tag($0) }
                }
                Picker("File Type", selection: Binding(
                    get: { appState.workingRecipe.fileType ?? "RAW" },
                    set: { appState.workingRecipe.fileType = $0 }
                )) {
                    ForEach(fileTypeOptions, id: \.self) { Text($0).tag($0) }
                }
            }

            Section {
                Button("Load from Camera") {
                    Task { await appState.loadRecipeFromCamera() }
                }
                Button("Save to Camera") {
                    Task { await appState.pushWorkingRecipeToCamera() }
                }
                .fontWeight(.semibold)

                if let slot = appState.editingRecipeSlot {
                    Button("Update Slot \(slot + 1)") {
                        Task { await appState.updateEditingSlot() }
                    }
                    .fontWeight(.semibold)
                    Button("Cancel Edit", role: .cancel) {
                        appState.cancelEditingSlot()
                    }
                } else {
                    Button("Save Recipe") {
                        newRecipeName = ""
                        showSaveRecipeAlert = true
                    }
                }
            }
        }
        .navigationTitle("Custom")
        .task {
            if appState.workingRecipe == .blank {
                await appState.loadRecipeFromCamera()
            }
        }
        .alert("Name this recipe", isPresented: $showSaveRecipeAlert) {
            TextField("Recipe name", text: $newRecipeName)
            Button("Save") {
                Task { await appState.saveWorkingRecipeAsNew(name: newRecipeName) }
            }
            Button("Cancel", role: .cancel) {}
        }
    }

    @ViewBuilder
    private func sliderRow(_ label: String, value: Binding<Int>, range: ClosedRange<Int>) -> some View {
        HStack {
            Text(label).frame(width: 130, alignment: .leading)
            Slider(value: Binding(
                get: { Double(value.wrappedValue) },
                set: { value.wrappedValue = Int($0.rounded()) }
            ), in: Double(range.lowerBound)...Double(range.upperBound), step: 1)
            Text("\(value.wrappedValue)").frame(width: 30, alignment: .trailing).foregroundStyle(.secondary)
        }
    }
}
