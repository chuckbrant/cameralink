import SwiftUI

@main
struct SonyConfigApp: App {
    @State private var appState = AppState()

    init() {
        // .preferredColorScheme(.dark) below only affects SwiftUI-rendered
        // content -- .alert()'s TextField (e.g. "Name this recipe") is a
        // real UIAlertController/UITextField under the hood, and it does NOT
        // inherit that color scheme. Forcing dark mode globally via
        // UIWindow.appearance() was tried first and made this WORSE: the
        // alert's own chrome stayed light-styled (white field background)
        // while the text field's *text* color picked up dark mode's white
        // label color -- invisible white-on-white while typing, only
        // visible once the surrounding chrome was inspected closely. The
        // field's background here is always plain white regardless of
        // color scheme, so the fix is to pin its text color to black
        // directly, rather than trying to force the whole alert dark.
        UITextField.appearance(whenContainedInInstancesOf: [UIAlertController.self]).textColor = .black
    }

    var body: some Scene {
        WindowGroup {
            RootView()
                .environment(appState)
                .preferredColorScheme(.dark)
        }
    }
}
