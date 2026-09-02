import Foundation
import Observation

enum AppTab: Hashable {
    case custom, filmRecipes, savedRecipes, setup
}

enum ConnectionKind: String, Codable {
    case wifi, cameraHostedAP
}

@Observable
@MainActor
final class AppState {
    // MARK: - Camera connection (this iPad <-> the Sony camera, directly --
    // no server/Pi in the loop)

    private(set) var camera: RecipeCameraClient?
    private(set) var connectionKind: ConnectionKind?
    var connectionKindLabel: String {
        switch connectionKind {
        case .wifi: return "Wi-Fi"
        case .cameraHostedAP: return "Camera Wi-Fi"
        case nil: return "—"
        }
    }

    // The camera self-assigns this IP as the gateway of its own hosted
    // Wi-Fi access point (the same mode used for phone pairing) --
    // confirmed 2026-08-29 against the real a7R V. No SSH, no userId/
    // password needed for this path: the Wi-Fi password is the only gate.
    static let cameraHostedAPIP = "192.168.122.1"
    var connected = false
    var cameraModel = ""
    var cameraInfo: CameraDeviceInfo?

    // MARK: - Navigation

    var selectedTab: AppTab = .custom

    // MARK: - Custom tab working state

    var workingRecipe = Recipe.blank
    /// Set when editing a Saved Recipe slot from the Saved Recipes tab --
    /// switches the Custom tab's "Save Recipe" button to "Update Slot N".
    var editingRecipeSlot: Int?

    // MARK: - Presets / Saved Recipes / Quick Connect

    var presets: [FilmPreset] = []
    var savedRecipes: [SavedRecipeEntry] = []
    var savedCameras: [SavedCamera] = []

    // MARK: - Transient UI state

    var lastError: String?
    /// A brief, non-error confirmation ("Saved to camera", "Loaded from
    /// camera", ...) -- shown as an auto-dismissing toast by RootView.
    var statusMessage: String?
    var isBusy = false

    private func report(_ error: Error) {
        lastError = (error as? LocalizedError)?.errorDescription ?? error.localizedDescription
    }

    private func report(_ message: String) {
        lastError = message
    }

    private func announce(_ message: String) {
        statusMessage = message
    }

    // MARK: - Connect (Wi-Fi)

    @discardableResult
    func connectWifi(ip: String, userId: String, password: String) async -> Bool {
        isBusy = true
        defer { isBusy = false }
        let client = RecipeCameraClient(transport: WifiTransport(ip: ip, userId: userId, password: password))
        do {
            try await client.connect()
            camera = client
            connectionKind = .wifi
            connected = true
            await loadCameraInfo()
            if let recipe = await fetchRecipeFromCamera() { workingRecipe = recipe }
            announce("Connected to \(cameraModel.isEmpty ? "camera" : cameraModel)")
            return true
        } catch {
            connected = false
            report(error)
            return false
        }
    }

    // MARK: - Connect (camera-hosted Wi-Fi AP -- no SSH, no credentials)

    @discardableResult
    func connectCameraHostedAP() async -> Bool {
        isBusy = true
        defer { isBusy = false }
        let client = RecipeCameraClient(transport: DirectWifiTransport(ip: Self.cameraHostedAPIP))
        do {
            try await client.connect()
            camera = client
            connectionKind = .cameraHostedAP
            connected = true
            await loadCameraInfo()
            if let recipe = await fetchRecipeFromCamera() { workingRecipe = recipe }
            announce("Connected to \(cameraModel.isEmpty ? "camera" : cameraModel)")
            return true
        } catch {
            connected = false
            report(error)
            return false
        }
    }

    func disconnect() {
        camera?.disconnect()
        camera = nil
        connectionKind = nil
        connected = false
        cameraModel = ""
        cameraInfo = nil
        announce("Disconnected")
    }

    /// Re-checks the live connection by pinging the camera -- there's no
    /// separate "server" to be reachable or not anymore, just this one
    /// link, so a failure here means the camera link itself is down.
    func refreshStatus() async {
        guard let camera else { connected = false; return }
        if !camera.isConnected {
            connected = false
            cameraModel = ""
            return
        }
        await loadCameraInfo()
    }

    func loadCameraInfo() async {
        guard let camera else { cameraInfo = nil; return }
        do {
            let info = try await camera.deviceInfo()
            cameraInfo = info
            cameraModel = info.model
            connected = true
        } catch {
            cameraInfo = nil
            connected = false
            report(error)
        }
    }

    // MARK: - Recipe: Load from Camera / Save to Camera

    /// Fetches the camera's current recipe without announcing anything --
    /// used internally (e.g. to refresh workingRecipe after pushing a
    /// preset) where "Loaded from camera" would be a misleading message
    /// for what the user actually just did.
    @discardableResult
    private func fetchRecipeFromCamera() async -> Recipe? {
        guard let camera else { return nil }
        do {
            return try await camera.readRecipe()
        } catch {
            report(error)
            return nil
        }
    }

    @discardableResult
    func loadRecipeFromCamera() async -> Bool {
        guard let recipe = await fetchRecipeFromCamera() else { return false }
        workingRecipe = recipe
        announce("Loaded from camera")
        return true
    }

    @discardableResult
    func pushWorkingRecipeToCamera() async -> Bool {
        guard let camera else { report("Not connected"); return false }
        do {
            try await camera.writeRecipe(workingRecipe)
            announce("Saved to camera")
            return true
        } catch {
            report(error)
            return false
        }
    }

    @discardableResult
    func pushPresetToCamera(_ preset: FilmPreset) async -> Bool {
        guard let camera else { report("Not connected"); return false }
        do {
            try await camera.writeRecipe(preset.asRecipe)
            if let recipe = await fetchRecipeFromCamera() { workingRecipe = recipe }
            announce("\"\(preset.name)\" sent to camera")
            return true
        } catch {
            report(error)
            return false
        }
    }

    // MARK: - Presets

    func loadPresets() async {
        presets = LocalStore.loadPresets()
    }

    var presetsByGroup: [(group: String, items: [FilmPreset])] {
        var order: [String] = []
        var buckets: [String: [FilmPreset]] = [:]
        for p in presets {
            if buckets[p.group] == nil { order.append(p.group) }
            buckets[p.group, default: []].append(p)
        }
        return order.map { (group: $0, items: buckets[$0] ?? []) }
    }

    // MARK: - Saved Recipes (10-slot CRUD library)

    func loadSavedRecipes() async {
        savedRecipes = LocalStore.loadSavedRecipes()
    }

    func savedRecipe(forSlot slot: Int) -> SavedRecipeEntry? {
        savedRecipes.first { $0.slot == slot }
    }

    private func nextFreeSlot() -> Int {
        let used = Set(savedRecipes.map { $0.slot })
        for slot in 0..<10 where !used.contains(slot) { return slot }
        return 0
    }

    @discardableResult
    func saveWorkingRecipeAsNew(name: String) async -> Bool {
        let slot = nextFreeSlot()
        savedRecipes.removeAll { $0.slot == slot }
        savedRecipes.append(SavedRecipeEntry(slot: slot, name: name, recipe: workingRecipe))
        LocalStore.saveSavedRecipes(savedRecipes)
        announce("Saved \"\(name)\" to slot \(slot + 1)")
        return true
    }

    func editSlotInCustomTab(_ entry: SavedRecipeEntry) {
        workingRecipe = entry.recipe
        editingRecipeSlot = entry.slot
        selectedTab = .custom
    }

    func cancelEditingSlot() {
        editingRecipeSlot = nil
        selectedTab = .savedRecipes
    }

    @discardableResult
    func updateEditingSlot() async -> Bool {
        guard let slot = editingRecipeSlot else { return false }
        let name = savedRecipe(forSlot: slot)?.name ?? "Slot \(slot + 1)"
        savedRecipes.removeAll { $0.slot == slot }
        savedRecipes.append(SavedRecipeEntry(slot: slot, name: name, recipe: workingRecipe))
        LocalStore.saveSavedRecipes(savedRecipes)
        editingRecipeSlot = nil
        selectedTab = .savedRecipes
        announce("Updated slot \(slot + 1)")
        return true
    }

    @discardableResult
    func loadSlotToCamera(_ entry: SavedRecipeEntry) async -> Bool {
        guard let camera else { report("Not connected"); return false }
        do {
            try await camera.writeRecipe(entry.recipe)
            announce("\"\(entry.name)\" sent to camera")
            return true
        } catch {
            report(error)
            return false
        }
    }

    func renameSlot(_ slot: Int, name: String) async {
        guard var entry = savedRecipe(forSlot: slot) else { return }
        entry.name = name
        savedRecipes.removeAll { $0.slot == slot }
        savedRecipes.append(entry)
        LocalStore.saveSavedRecipes(savedRecipes)
        announce("Renamed to \"\(name)\"")
    }

    func deleteSlot(_ slot: Int) async {
        let name = savedRecipe(forSlot: slot)?.name
        savedRecipes.removeAll { $0.slot == slot }
        LocalStore.saveSavedRecipes(savedRecipes)
        announce(name.map { "Deleted \"\($0)\"" } ?? "Deleted")
    }

    // MARK: - Quick Connect (saved Wi-Fi camera profiles)

    func loadSavedCameras() async {
        savedCameras = LocalStore.loadSavedCameras()
    }

    @discardableResult
    func quickConnect(_ cam: SavedCamera) async -> Bool {
        await connectWifi(ip: cam.ip, userId: cam.userId, password: cam.password)
    }

    func saveQuickConnect(name: String, ip: String, userId: String, password: String) async {
        savedCameras.removeAll { $0.name == name }
        savedCameras.append(SavedCamera(name: name, ip: ip, userId: userId, password: password))
        LocalStore.saveSavedCameras(savedCameras)
        announce("Saved Quick Connect \"\(name)\"")
    }

    func deleteSavedCamera(_ cam: SavedCamera) {
        savedCameras.removeAll { $0.name == cam.name }
        LocalStore.saveSavedCameras(savedCameras)
    }
}
