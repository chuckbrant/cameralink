import SwiftUI

struct RootView: View {
    @Environment(AppState.self) private var appStateEnv

    var body: some View {
        @Bindable var appState = appStateEnv

        VStack(spacing: 0) {
            // Two independent links -- this device to the Pi's server, and
            // the Pi to the camera -- shown separately since either can be
            // down while the other is fine, and they need different fixes
            // (reconnect the cable/Wi-Fi vs. reconnect the camera).
            VStack(alignment: .leading, spacing: 2) {
                HStack(spacing: 8) {
                    Circle()
                        .fill(serverDotColor)
                        .frame(width: 10, height: 10)
                    Text(serverStatusText)
                        .foregroundStyle(.secondary)
                    Spacer()
                }
                HStack(spacing: 8) {
                    Circle()
                        .fill(appState.connected ? Color.green : Color.gray)
                        .frame(width: 10, height: 10)
                    Text(appState.connected ? appState.cameraModel : "Camera not connected")
                        .foregroundStyle(.secondary)
                    Spacer()
                }
            }
            .font(.footnote)
            .padding(.horizontal)
            .padding(.top, 8)
            .padding(.bottom, 4)

            TabView(selection: $appState.selectedTab) {
                CustomView()
                    .tabItem { Label("Custom", systemImage: "slider.horizontal.3") }
                    .tag(AppTab.custom)
                SavedRecipesView()
                    .tabItem { Label("Saved Recipes", systemImage: "tray.full") }
                    .tag(AppTab.savedRecipes)
                FilmRecipesView()
                    .tabItem { Label("Film Recipes", systemImage: "film") }
                    .tag(AppTab.filmRecipes)
                SetupView()
                    .tabItem { Label("Setup", systemImage: "gearshape") }
                    .tag(AppTab.setup)
            }
        }
        .task { await appState.refreshStatus() }
        .alert("Error", isPresented: Binding(
            get: { appState.lastError != nil },
            set: { if !$0 { appState.lastError = nil } }
        )) {
            Button("OK", role: .cancel) { appState.lastError = nil }
        } message: {
            Text(appState.lastError ?? "")
        }
        .overlay(alignment: .top) {
            if let message = appState.statusMessage {
                ToastBanner(message: message)
                    .padding(.top, 8)
                    .transition(.move(edge: .top).combined(with: .opacity))
            }
        }
        .animation(.spring(duration: 0.3), value: appState.statusMessage)
        .onChange(of: appState.statusMessage) { _, newValue in
            guard let newValue else { return }
            Task {
                try? await Task.sleep(for: .seconds(2))
                if appState.statusMessage == newValue {
                    appState.statusMessage = nil
                }
            }
        }
    }

    private var serverDotColor: Color {
        switch appStateEnv.serverReachable {
        case .some(true): return .green
        case .some(false): return .red
        case .none: return .gray
        }
    }

    private var serverStatusText: String {
        switch appStateEnv.serverReachable {
        case .some(true): return "Server: \(appStateEnv.serverURLString)"
        case .some(false): return "Server unreachable (\(appStateEnv.serverURLString))"
        case .none: return "Checking server..."
        }
    }
}

// A brief, auto-dismissing confirmation banner ("Saved to camera", "Loaded
// from camera", ...) -- shown for successful actions, complementing the
// error alert above which only covers failures.
private struct ToastBanner: View {
    let message: String

    var body: some View {
        HStack(spacing: 8) {
            Image(systemName: "checkmark.circle.fill")
                .foregroundStyle(.green)
            Text(message)
                .font(.subheadline.weight(.medium))
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 10)
        .background(.regularMaterial, in: Capsule())
        .shadow(radius: 4)
    }
}
