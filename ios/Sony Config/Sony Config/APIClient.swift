import Foundation

enum APIError: Error, LocalizedError {
    case invalidURL
    case http(Int)
    case decoding(Error)
    case transport(Error)

    var errorDescription: String? {
        switch self {
        case .invalidURL: return "Invalid server URL"
        case .http(let code): return "Server returned HTTP \(code)"
        case .decoding(let e): return "Couldn't parse response: \(e.localizedDescription)"
        case .transport(let e): return e.localizedDescription
        }
    }
}

// Thin wrapper over the same JSON REST API server/public/index.html
// calls with fetch() -- one client, shared base URL, so the app and the
// web frontend stay interchangeable clients of the exact same backend.
final class APIClient {
    var baseURL: URL

    init(baseURL: URL) {
        self.baseURL = baseURL
    }

    private func makeURL(_ pathAndQuery: String) throws -> URL {
        guard let url = URL(string: pathAndQuery, relativeTo: baseURL) else { throw APIError.invalidURL }
        return url
    }

    private func send(url: URL, method: String, body: Data?, timeout: TimeInterval) async throws -> Data {
        var req = URLRequest(url: url)
        req.httpMethod = method
        req.timeoutInterval = timeout
        if let body {
            req.httpBody = body
            req.setValue("application/json", forHTTPHeaderField: "Content-Type")
        }
        do {
            let (data, response) = try await URLSession.shared.data(for: req)
            if let http = response as? HTTPURLResponse, !(200...299).contains(http.statusCode) {
                throw APIError.http(http.statusCode)
            }
            return data
        } catch let e as APIError {
            throw e
        } catch {
            throw APIError.transport(error)
        }
    }

    private func get<T: Decodable>(_ path: String, timeout: TimeInterval = 10) async throws -> T {
        let url = try makeURL(path)
        let data = try await send(url: url, method: "GET", body: nil, timeout: timeout)
        do { return try JSONDecoder().decode(T.self, from: data) } catch { throw APIError.decoding(error) }
    }

    private func post<Body: Encodable, T: Decodable>(_ path: String, body: Body, timeout: TimeInterval = 10) async throws -> T {
        let url = try makeURL(path)
        let data = try JSONEncoder().encode(body)
        let respData = try await send(url: url, method: "POST", body: data, timeout: timeout)
        do { return try JSONDecoder().decode(T.self, from: respData) } catch { throw APIError.decoding(error) }
    }

    private func postEmpty<T: Decodable>(_ path: String, timeout: TimeInterval = 10) async throws -> T {
        let url = try makeURL(path)
        let respData = try await send(url: url, method: "POST", body: Data("{}".utf8), timeout: timeout)
        do { return try JSONDecoder().decode(T.self, from: respData) } catch { throw APIError.decoding(error) }
    }

    // MARK: - Status / info

    func status() async throws -> StatusResponse { try await get("/api/status") }
    func cameraInfo() async throws -> CameraInfo { try await get("/api/camera-info") }

    // MARK: - Recipe

    func recipe() async throws -> Recipe { try await get("/api/recipe") }
    func pushRecipe(_ recipe: Recipe) async throws -> SimpleSuccess { try await post("/api/recipe", body: recipe) }

    // MARK: - Presets

    func presets() async throws -> [FilmPreset] { try await get("/api/presets") }

    // MARK: - Saved Recipes

    func savedRecipes() async throws -> [SavedRecipeEntry] { try await get("/api/recipes/saved") }

    struct SaveRecipeBody: Encodable {
        var name: String
        var recipe: Recipe
        var slot: Int?
    }
    func saveRecipe(name: String, recipe: Recipe, slot: Int? = nil) async throws -> SimpleSuccess {
        try await post("/api/recipes/save", body: SaveRecipeBody(name: name, recipe: recipe, slot: slot))
    }

    struct RenameBody: Encodable { var slot: Int; var name: String }
    func renameRecipe(slot: Int, name: String) async throws -> SimpleSuccess {
        try await post("/api/recipes/rename", body: RenameBody(slot: slot, name: name))
    }

    struct SlotBody: Encodable { var slot: Int }
    func deleteRecipe(slot: Int) async throws -> SimpleSuccess {
        try await post("/api/recipes/delete", body: SlotBody(slot: slot))
    }

    // MARK: - Connection

    // The camera-pairing handshake (Wi-Fi association + SSH fingerprint
    // retrieval on the Pi's side) can legitimately take much longer than
    // this client's normal 10s default, especially right as the camera is
    // still settling onto the network -- the web UI's plain fetch() has no
    // timeout at all and just waits, which is why it can succeed at a
    // moment this app would otherwise give up on with a spurious timeout
    // error. Give these two calls a generous 45s instead.
    struct ConnectNetworkBody: Encodable { var ip: String; var mac: String; var userId: String; var password: String }
    func connectNetwork(ip: String, mac: String, userId: String, password: String) async throws -> SimpleSuccess {
        try await post("/api/connect/network", body: ConnectNetworkBody(ip: ip, mac: mac, userId: userId, password: password), timeout: 45)
    }

    func connectUSB() async throws -> SimpleSuccess { try await postEmpty("/api/connect/usb", timeout: 45) }
    func disconnect() async throws -> SimpleSuccess { try await postEmpty("/api/disconnect") }

    // MARK: - Quick Connect (saved camera profiles)

    func savedCameras() async throws -> [SavedCamera] { try await get("/api/network/saved") }

    func saveCamera(_ cam: SavedCamera) async throws -> SimpleSuccess { try await post("/api/network/save", body: cam) }

    func findCamera(mac: String) async throws -> FindResponse {
        let encodedMac = mac.addingPercentEncoding(withAllowedCharacters: .urlQueryAllowed) ?? mac
        return try await get("/api/network/find?mac=\(encodedMac)")
    }

    // MARK: - System

    func shutdownPi() async throws -> SimpleSuccess { try await postEmpty("/api/system/shutdown") }
}
