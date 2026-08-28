import SwiftUI

struct SetupView: View {
    @Environment(AppState.self) private var appState

    @State private var showNetForm = false
    @State private var netIp = ""
    @State private var netMac = ""
    @State private var netUser = ""
    @State private var netPass = ""
    @State private var findStatus = ""
    @State private var showShutdownConfirm = false
    @State private var showServerURLEditor = false
    @State private var editedServerURL = ""

    var body: some View {
        Form {
            Section("Server") {
                Button {
                    editedServerURL = appState.serverURLString
                    showServerURLEditor = true
                } label: {
                    LabeledContent("cameralink server", value: appState.serverURLString)
                }
            }

            Section("Quick Connect") {
                // These two switch which path this iPad uses to reach the
                // Pi (Wi-Fi AP vs. USB gadget cable), then connect using
                // whatever camera profile is already saved on that server --
                // same physical camera either way. No credentials live in
                // this app; they're fetched fresh from the Pi each time.
                Button("Quick Connect (Network)") {
                    Task { await appState.quickConnect(serverURL: AppState.networkServerURL) }
                }
                Button("Quick Connect (USB Gadget)") {
                    Task { await appState.quickConnect(serverURL: AppState.gadgetServerURL) }
                }
                Button("Quick Connect (Synology)") {
                    Task { await appState.quickConnect(serverURL: AppState.synologyServerURL) }
                }
                if appState.savedCameras.isEmpty {
                    Text("No saved cameras yet.").foregroundStyle(.secondary)
                }
                ForEach(appState.savedCameras) { cam in
                    Button(cam.name) {
                        Task {
                            netIp = cam.ip; netMac = cam.mac; netUser = cam.userId; netPass = cam.password
                            await appState.connectNetwork(ip: cam.ip, mac: cam.mac, userId: cam.userId, password: cam.password)
                        }
                    }
                }
            }

            Section {
                Button(showNetForm ? "Hide Connect Form" : "Connect (Network)") {
                    showNetForm.toggle()
                }
                Button("Refresh") {
                    Task {
                        await appState.refreshStatus()
                        await appState.loadCameraInfo()
                        await appState.loadRecipeFromCamera()
                    }
                }
            }

            if showNetForm {
                Section("Connect to Camera") {
                    TextField("Camera IP address, e.g. 10.42.0.50", text: $netIp)
                        .keyboardType(.numbersAndPunctuation)
                        .autocorrectionDisabled()
                        .textInputAutocapitalization(.never)
                    HStack {
                        TextField("MAC address, e.g. aa:bb:cc:dd:ee:ff", text: $netMac)
                            .autocorrectionDisabled()
                            .textInputAutocapitalization(.never)
                        Button("Find") {
                            Task {
                                findStatus = "Searching..."
                                if let ip = await appState.findCameraIP(mac: netMac) {
                                    netIp = ip
                                    findStatus = "Found at \(ip)"
                                } else {
                                    findStatus = "Not found"
                                }
                            }
                        }
                    }
                    if !findStatus.isEmpty {
                        Text(findStatus).font(.caption).foregroundStyle(.secondary)
                    }
                    TextField("User ID (from camera's Access Authen. Info screen)", text: $netUser)
                        .autocorrectionDisabled()
                        .textInputAutocapitalization(.never)
                    SecureField("Password", text: $netPass)
                    Button("Connect") {
                        Task { await appState.connectNetwork(ip: netIp, mac: netMac, userId: netUser, password: netPass) }
                    }
                    .disabled(appState.isBusy || netIp.isEmpty || netMac.isEmpty)
                    Button("Save as Quick Connect") {
                        Task { await appState.saveQuickConnect(name: netIp, ip: netIp, mac: netMac, userId: netUser, password: netPass) }
                    }
                    .disabled(netIp.isEmpty)
                }
            }

            if let info = appState.cameraInfo {
                Section("Camera") {
                    LabeledContent("Model", value: info.modelName ?? "—")
                    LabeledContent("Serial", value: info.bodySerialNumber ?? "—")
                    LabeledContent("Software Ver", value: info.softwareVersion ?? "—")
                    LabeledContent("Battery", value: "\(info.batteryLevel ?? "—") (\(info.batteryRemain.map { "\($0)%" } ?? "—"))")
                }
                Section("Lens") {
                    LabeledContent("Lens", value: info.lensModelName ?? "—")
                    LabeledContent("Lens Ver", value: info.lensVersionNumber ?? "—")
                }
                Section("Memory") {
                    LabeledContent("Slot 1", value: info.mediaSlot1Remaining.map { "\($0) shots" } ?? "—")
                    LabeledContent("Slot 2", value: info.mediaSlot2Remaining.map { "\($0) shots" } ?? "—")
                }
            }

            Section {
                Button("Shut Down Pi", role: .destructive) { showShutdownConfirm = true }
            }
        }
        .navigationTitle("Setup")
        .task {
            await appState.loadSavedCameras()
            await appState.loadCameraInfo()
        }
        .confirmationDialog(
            "This will cleanly power off the Pi. You'll need to unplug and replug power (or press its power button, if wired) to turn it back on.",
            isPresented: $showShutdownConfirm,
            titleVisibility: .visible
        ) {
            Button("Confirm Shutdown", role: .destructive) { Task { await appState.shutdownPi() } }
            Button("Cancel", role: .cancel) {}
        }
        .sheet(isPresented: $showServerURLEditor) {
            NavigationStack {
                Form {
                    Section("cameralink server address") {
                        TextField("http://10.42.0.1:8080", text: $editedServerURL)
                            .keyboardType(.URL)
                            .autocorrectionDisabled()
                            .textInputAutocapitalization(.never)
                    }
                    Text("This is the Pi's address, not the camera's -- e.g. http://10.42.0.1:8080 over the Pi's Wi-Fi access point, or the Pi's USB gadget address when connected by cable.")
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                .navigationTitle("Server")
                .toolbar {
                    ToolbarItem(placement: .cancellationAction) {
                        Button("Cancel") { showServerURLEditor = false }
                    }
                    ToolbarItem(placement: .confirmationAction) {
                        Button("Save") {
                            appState.serverURLString = editedServerURL
                            showServerURLEditor = false
                            Task { await appState.refreshStatus() }
                        }
                    }
                }
            }
        }
    }
}
