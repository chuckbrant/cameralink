import Foundation

// Replaces the Pi's saved_cameras.json / saved_recipes.json (now that
// there's no server in the loop) with plain JSON files in this app's own
// Documents directory -- same trust model as before (same device,
// plaintext), just moved from the Pi's disk to this iPad's. Presets are
// static, bundled data (server/main.cpp's kPresetsJson, transcribed once
// to a resource file since it never changes at runtime).
enum LocalStore {
    private static var documentsURL: URL {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
    }

    private static func load<T: Decodable>(_ filename: String, default def: T) -> T {
        let url = documentsURL.appendingPathComponent(filename)
        guard let data = try? Data(contentsOf: url) else { return def }
        return (try? JSONDecoder().decode(T.self, from: data)) ?? def
    }

    private static func save<T: Encodable>(_ value: T, to filename: String) {
        let url = documentsURL.appendingPathComponent(filename)
        guard let data = try? JSONEncoder().encode(value) else { return }
        try? data.write(to: url, options: .atomic)
    }

    // MARK: - Saved Cameras (Quick Connect profiles)

    /// Seeds SavedCamera.homeNetworkDefault on first launch only (no
    /// saved_cameras.json yet) -- after that, the file is the source of
    /// truth, so deleting the seeded entry in the app actually removes it.
    static func loadSavedCameras() -> [SavedCamera] {
        let url = documentsURL.appendingPathComponent("saved_cameras.json")
        if !FileManager.default.fileExists(atPath: url.path) {
            let seeded = [SavedCamera.homeNetworkDefault]
            saveSavedCameras(seeded)
            return seeded
        }
        return load("saved_cameras.json", default: [])
    }
    static func saveSavedCameras(_ cameras: [SavedCamera]) { save(cameras, to: "saved_cameras.json") }

    // MARK: - Saved Recipes (10-slot library)

    static func loadSavedRecipes() -> [SavedRecipeEntry] { load("saved_recipes.json", default: []) }
    static func saveSavedRecipes(_ recipes: [SavedRecipeEntry]) { save(recipes, to: "saved_recipes.json") }

    // MARK: - Film Recipe presets (bundled, read-only)

    static func loadPresets() -> [FilmPreset] {
        guard let url = Bundle.main.url(forResource: "presets", withExtension: "json"),
              let data = try? Data(contentsOf: url),
              let presets = try? JSONDecoder().decode([FilmPreset].self, from: data) else {
            return []
        }
        return presets
    }
}
