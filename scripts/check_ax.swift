import Cocoa

// Check if THIS process (or its responsible parent, i.e. the terminal app)
// has Accessibility permission granted in System Preferences.
let trusted = AXIsProcessTrusted()
print("AXIsProcessTrusted: \(trusted)")

if !trusted {
    print("")
    print("NOT TRUSTED — Accessibility permission is missing.")
    print("")
    print("Fix: System Settings → Privacy & Security → Accessibility")
    print("     → Add and enable the terminal app you're running from.")
    print("     (VS Code, Terminal.app, or iTerm2)")
    print("")
    // Prompt macOS to show the dialog pointing to System Settings
    let opts = [kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String: true] as CFDictionary
    _ = AXIsProcessTrustedWithOptions(opts)
    print("A system dialog should have appeared. Grant permission, then re-run.")
} else {
    print("")
    print("TRUSTED — Accessibility is working. AX APIs should function.")

    // Quick smoke test: list running apps' windows via AX
    let ws = NSWorkspace.shared
    for app in ws.runningApplications {
        if app.activationPolicy == .regular, let name = app.localizedName {
            let axApp = AXUIElementCreateApplication(app.processIdentifier)
            var windows: AnyObject?
            let err = AXUIElementCopyAttributeValue(axApp, kAXWindowsAttribute as CFString, &windows)
            if err == .success, let windowList = windows as? [AXUIElement] {
                print("  \(name): \(windowList.count) window(s)")
            }
        }
    }
}
