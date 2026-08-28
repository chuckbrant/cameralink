import Foundation
import Observation

enum AppTab: Hashable {
    case custom, filmRecipes, savedRecipes, setup
}

@Observable
@MainActor
final class AppState {
    // MARK: - Server connection (Sony Config <-> the cameralink Pi)

    var serverURLString: String {
        didSet {
            UserDefaults.standard.set(serverURLString, forKey: "serverURLString")
            rebuildClient()
        }
    }
    private(set) var api: APIClient

    // MARK: - Two independent links that both have to be up:
    // 1) this iPad <-> the Pi's web server (over USB gadget or Wi-Fi)
    // 2) the Pi <-> the Sony camera (always over Wi-Fi/CameraBrdg)
    // A dead server link and a dead camera link look identical from the
    // Custom/Film Recipes/Saved Recipes tabs (every request just fails) --
    // Setup shows them separately so it's obvious which one to fix.

    /// nil = not checked yet. Sette by any successful/failed call to
    /// refreshStatus() -- reflects whether *this device* can reach the Pi's
    /// server at all right now, independent of whether a camera is paired.
    var serverReachable: Bool?
    var connected = false
    var cameraModel = ""
    var cameraInfo: CameraInfo?

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

    init() {
        let saved = UserDefaults.standard.string(forKey: "serverURLString")
        // Default matches the address this whole project was developed and
        // tested against all session: the Pi's own Wi-Fi access point.
        // Reachable from the simulator via USB gadget mode or Wi-Fi, same
        // as any other client on the Mac's network.
        let initial = saved ?? "http://10.42.0.1:8080"
        self.serverURLString = initial
        self.api = APIClient(baseURL: URL(string: initial) ?? URL(string: "http://10.42.0.1:8080")!)
    }

    private func rebuildClient() {
        guard let url = URL(string: serverURLString) else { return }
        api = APIClient(baseURL: url)
    }

    private func report(_ error: Error) {
        lastError = (error as? LocalizedError)?.errorDescription ?? error.localizedDescription
    }

    private func report(_ message: String) {
        lastError = message
    }

    private func announce(_ message: String) {
        statusMessage = message
    }

    // MARK: - Status / camera info

    func refreshStatus() async {
        do {
            let status = try await api.status()
            serverReachable = true
            connected = status.connected
            cameraModel = status.model
        } catch {
            // A failure here is ambiguous by itself -- it could mean this
            // device can't reach the Pi at all (wrong serverURLString, USB
            // gadget not up, Wi-Fi not joined), or the Pi is up but just
            // hasn't got a camera connected. Since GET /api/status only
            // fails on a real network/transport error (a reachable server
            // always returns 200 with connected:false), treat any failure
            // here as "server unreachable" -- the camera fields become
            // meaningless once we don't even know if the Pi answered.
            serverReachable = false
            connected = false
            cameraModel = ""
            report(error)
        }
    }

    func loadCameraInfo() async {
        do {
            let info = try await api.cameraInfo()
            cameraInfo = info.error == nil ? info : nil
        } catch {
            cameraInfo = nil
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
        do {
            let recipe = try await api.recipe()
            if let err = recipe.error { report(err); return nil }
            return recipe
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
        do {
            let result = try await api.pushRecipe(workingRecipe)
            if result.success { announce("Saved to camera") }
            else { report(result.error ?? "Save failed") }
            return result.success
        } catch {
            report(error)
            return false
        }
    }

    @discardableResult
    func pushPresetToCamera(_ preset: FilmPreset) async -> Bool {
        do {
            let result = try await api.pushRecipe(preset.asRecipe)
            if result.success {
                if let recipe = await fetchRecipeFromCamera() { workingRecipe = recipe }
                announce("\"\(preset.name)\" sent to camera")
            } else {
                report(result.error ?? "Push failed")
            }
            return result.success
        } catch {
            report(error)
            return false
        }
    }

    // MARK: - Presets

    func loadPresets() async {
        do { presets = try await api.presets() } catch { report(error) }
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
        do { savedRecipes = try await api.savedRecipes() } catch { report(error) }
    }

    func savedRecipe(forSlot slot: Int) -> SavedRecipeEntry? {
        savedRecipes.first { $0.slot == slot }
    }

    @discardableResult
    func saveWorkingRecipeAsNew(name: String) async -> Bool {
        do {
            let result = try await api.saveRecipe(name: name, recipe: workingRecipe)
            if result.success {
                await loadSavedRecipes()
                announce("Saved \"\(name)\" to slot \((result.slot ?? 0) + 1)")
            } else {
                report(result.error ?? "Save failed")
            }
            return result.success
        } catch {
            report(error)
            return false
        }
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
        do {
            let result = try await api.saveRecipe(name: name, recipe: workingRecipe, slot: slot)
            if result.success {
                await loadSavedRecipes()
                editingRecipeSlot = nil
                selectedTab = .savedRecipes
                announce("Updated slot \(slot + 1)")
            } else {
                report(result.error ?? "Update failed")
            }
            return result.success
        } catch {
            report(error)
            return false
        }
    }

    @discardableResult
    func loadSlotToCamera(_ entry: SavedRecipeEntry) async -> Bool {
        do {
            let result = try await api.pushRecipe(entry.recipe)
            if result.success { announce("\"\(entry.name)\" sent to camera") }
            else { report(result.error ?? "Push failed") }
            return result.success
        } catch {
            report(error)
            return false
        }
    }

    func renameSlot(_ slot: Int, name: String) async {
        do {
            let result = try await api.renameRecipe(slot: slot, name: name)
            if result.success {
                await loadSavedRecipes()
                announce("Renamed to \"\(name)\"")
            } else {
                report(result.error ?? "Rename failed")
            }
        } catch { report(error) }
    }

    func deleteSlot(_ slot: Int) async {
        let name = savedRecipe(forSlot: slot)?.name
        do {
            let result = try await api.deleteRecipe(slot: slot)
            if result.success {
                await loadSavedRecipes()
                announce(name.map { "Deleted \"\($0)\"" } ?? "Deleted")
            } else {
                report(result.error ?? "Delete failed")
            }
        } catch { report(error) }
    }

    // MARK: - Quick Connect / manual connect

    // The Pi's two well-known addresses for this transport -- not a secret
    // (no credentials here, just which network path to use). Real camera
    // credentials (ip/mac/userId/password) are never hardcoded in this app;
    // they live only in the Pi's own saved_cameras.json (gitignored) and are
    // fetched fresh below.
    static let networkServerURL = "http://10.42.0.1:8080"
    static let gadgetServerURL = "http://192.168.7.2:8080"

    func loadSavedCameras() async {
        do { savedCameras = try await api.savedCameras() } catch { report(error) }
    }

    /// Switches to the given server address (Wi-Fi AP vs. USB gadget), then
    /// connects using whichever camera profile is already saved on THAT
    /// server -- same physical camera either way, just a different path
    /// from this iPad to the Pi. Named "Quick Connect (Network)"/"(USB
    /// Gadget)" in the UI.
    @discardableResult
    func quickConnect(serverURL: String) async -> Bool {
        serverURLString = serverURL
        await refreshStatus()
        guard serverReachable == true else {
            report("Can't reach the Pi at \(serverURL). Check the cable/Wi-Fi connection.")
            return false
        }
        await loadSavedCameras()
        guard let cam = savedCameras.first else {
            report("No saved camera profile on this server yet -- use \"Save as Quick Connect\" from the Connect form first.")
            return false
        }
        return await connectNetwork(ip: cam.ip, mac: cam.mac, userId: cam.userId, password: cam.password)
    }

    @discardableResult
    func connectNetwork(ip: String, mac: String, userId: String, password: String) async -> Bool {
        isBusy = true
        defer { isBusy = false }
        do {
            let result = try await api.connectNetwork(ip: ip, mac: mac, userId: userId, password: password)
            if result.success {
                await refreshStatus()
                await loadCameraInfo()
                if let recipe = await fetchRecipeFromCamera() { workingRecipe = recipe }
                announce("Connected to \(cameraModel.isEmpty ? "camera" : cameraModel)")
            } else {
                report(result.error ?? "Connect failed")
            }
            return result.success
        } catch {
            report(error)
            return false
        }
    }

    func saveQuickConnect(name: String, ip: String, mac: String, userId: String, password: String) async {
        do {
            let result = try await api.saveCamera(SavedCamera(name: name, ip: ip, mac: mac, userId: userId, password: password))
            if result.success {
                await loadSavedCameras()
                announce("Saved Quick Connect \"\(name)\"")
            } else {
                report(result.error ?? "Save failed")
            }
        } catch { report(error) }
    }

    func findCameraIP(mac: String) async -> String? {
        do {
            let result = try await api.findCamera(mac: mac)
            return result.found ? result.ip : nil
        } catch {
            report(error)
            return nil
        }
    }

    func disconnect() async {
        do {
            _ = try await api.disconnect()
            await refreshStatus()
            announce("Disconnected")
        } catch { report(error) }
    }

    func shutdownPi() async {
        do {
            _ = try await api.shutdownPi()
            announce("Shutting down the Pi...")
        } catch { report(error) }
    }
}
