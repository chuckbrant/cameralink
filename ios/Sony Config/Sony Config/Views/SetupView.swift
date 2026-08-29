import SwiftUI

struct SetupView: View {
    @Environment(AppState.self) private var appState

    @State private var showWifiForm = false
    @State private var wifiIp = ""
    @State private var wifiUser = ""
    @State private var wifiPass = ""
    @State private var quickConnectName = ""

    var body: some View {
        Form {
            Section("Connection") {
                LabeledContent("Status", value: appState.connected ? "Connected" : "Not connected")
                if appState.connected {
                    LabeledContent("Via", value: appState.connectionKindLabel)
                }
                Button("Quick Connect (Camera-hosted AP)") {
                    Task { await appState.connectCameraHostedAP() }
                }
                Button(showWifiForm ? "Hide Wi-Fi Connect Form" : "Connect via Wi-Fi") {
                    showWifiForm.toggle()
                }
                ForEach(appState.savedCameras) { cam in
                    Button(cam.name) {
                        Task { await appState.quickConnect(cam) }
                    }
                }
                .onDelete { offsets in
                    for index in offsets { appState.deleteSavedCamera(appState.savedCameras[index]) }
                }
                if appState.connected {
                    Button("Disconnect", role: .destructive) { appState.disconnect() }
                }
                Button("Refresh") {
                    Task {
                        await appState.refreshStatus()
                        await appState.loadCameraInfo()
                        await appState.loadRecipeFromCamera()
                    }
                }
            }

            if showWifiForm {
                Section("Connect over Wi-Fi") {
                    TextField("Camera IP address, e.g. 192.168.122.1", text: $wifiIp)
                        .keyboardType(.numbersAndPunctuation)
                        .autocorrectionDisabled()
                        .textInputAutocapitalization(.never)
                    TextField("User ID (from camera's Access Authen. Info screen)", text: $wifiUser)
                        .autocorrectionDisabled()
                        .textInputAutocapitalization(.never)
                    SecureField("Password", text: $wifiPass)
                    Button("Connect") {
                        Task { await appState.connectWifi(ip: wifiIp, userId: wifiUser, password: wifiPass) }
                    }
                    .disabled(appState.isBusy || wifiIp.isEmpty || wifiUser.isEmpty)

                    TextField("Name for Quick Connect", text: $quickConnectName)
                        .autocorrectionDisabled()
                    Button("Save as Quick Connect") {
                        Task {
                            await appState.saveQuickConnect(
                                name: quickConnectName.isEmpty ? wifiIp : quickConnectName,
                                ip: wifiIp, userId: wifiUser, password: wifiPass
                            )
                        }
                    }
                    .disabled(wifiIp.isEmpty)
                }
            }

            if let info = appState.cameraInfo {
                Section("Camera") {
                    LabeledContent("Model", value: info.model.isEmpty ? "—" : info.model)
                    LabeledContent("Serial", value: info.serialNumber.isEmpty ? "—" : info.serialNumber)
                    LabeledContent("Software Ver", value: info.version.isEmpty ? "—" : info.version)
                }
                Section("Lens") {
                    LabeledContent("Lens", value: info.lensModel ?? "—")
                    LabeledContent("Lens Ver", value: info.lensVersion ?? "—")
                }
                // Battery level and media slot remaining-shots aren't
                // available -- battery (standard PTP code 0x5001) was
                // confirmed absent from the camera's full property table,
                // and media remaining-shots was never reverse-engineered.
            }
        }
        .navigationTitle("Setup")
        .task {
            await appState.loadSavedCameras()
            await appState.loadCameraInfo()
        }
    }
}
