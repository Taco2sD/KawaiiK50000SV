import Cocoa

// Diagnostic: trigger quit and observe the save dialog's AX elements.
// Polls every 0.3s for up to 15s to see when/what appears.

let start = Date()
func ts() -> String { String(format: "[%.1fs]", Date().timeIntervalSince(start)) }

let apps = NSRunningApplication.runningApplications(withBundleIdentifier: "com.ableton.live")
guard let live = apps.first else { print("Live not running"); exit(1) }
let pid = live.processIdentifier
print("\(ts()) Live pid \(pid), sending terminate()")

let axApp = AXUIElementCreateApplication(pid)

// Snapshot the current window count so we can detect new windows/sheets
var baseWindowCount = 0
do {
    var windows: AnyObject?
    AXUIElementCopyAttributeValue(axApp, kAXWindowsAttribute as CFString, &windows)
    baseWindowCount = (windows as? [AXUIElement])?.count ?? 0
    print("\(ts()) Base window count: \(baseWindowCount)")
}

// Send quit
live.terminate()

// Poll for changes
let deadline = Date().addingTimeInterval(15)
var found = false

while Date() < deadline && !live.isTerminated {
    var windows: AnyObject?
    AXUIElementCopyAttributeValue(axApp, kAXWindowsAttribute as CFString, &windows)
    let windowList = (windows as? [AXUIElement]) ?? []

    // Check each window for buttons (especially new dialog/sheet)
    for window in windowList {
        // Check for sheets (save dialog is often a sheet)
        var sheets: AnyObject?
        AXUIElementCopyAttributeValue(window, "AXSheets" as CFString, &sheets)
        if let sheetList = sheets as? [AXUIElement], !sheetList.isEmpty {
            print("\(ts()) SHEET found in window!")
            for sheet in sheetList {
                dumpElement(sheet, depth: 0)
            }
            found = true
        }

        // Also check for any buttons directly in the window (modal dialog style)
        var children: AnyObject?
        AXUIElementCopyAttributeValue(window, kAXChildrenAttribute as CFString, &children)
        if let childList = children as? [AXUIElement] {
            for child in childList {
                var role: AnyObject?
                AXUIElementCopyAttributeValue(child, kAXRoleAttribute as CFString, &role)
                if let r = role as? String, r == kAXButtonRole as String {
                    var title: AnyObject?
                    var desc: AnyObject?
                    AXUIElementCopyAttributeValue(child, kAXTitleAttribute as CFString, &title)
                    AXUIElementCopyAttributeValue(child, kAXDescriptionAttribute as CFString, &desc)
                    let t = (title as? String) ?? ""
                    let d = (desc as? String) ?? ""
                    if !t.isEmpty || !d.isEmpty {
                        print("\(ts()) BUTTON in window: title=\"\(t)\" desc=\"\(d)\"")
                    }
                }
            }
        }
    }

    // Check if window count changed (new dialog window)
    if windowList.count != baseWindowCount {
        print("\(ts()) Window count changed: \(baseWindowCount) → \(windowList.count)")
        baseWindowCount = windowList.count
        // Dump all windows
        for (i, window) in windowList.enumerated() {
            print("\(ts()) Window \(i):")
            dumpElement(window, depth: 1)
        }
        found = true
    }

    if found { break }
    Thread.sleep(forTimeInterval: 0.3)
}

if live.isTerminated {
    print("\(ts()) Process exited (no dialog appeared)")
} else if !found {
    print("\(ts()) Timed out — no dialog detected. Force killing.")
    live.forceTerminate()
} else {
    // Wait a bit more to see if it quits after we found the dialog
    let exitDeadline = Date().addingTimeInterval(5)
    while !live.isTerminated && Date() < exitDeadline {
        Thread.sleep(forTimeInterval: 0.2)
    }
    print("\(ts()) Final state: isTerminated=\(live.isTerminated)")
    if !live.isTerminated {
        live.forceTerminate()
        print("\(ts()) Force killed")
    }
}

// --- Helper to dump an AX element tree ---
func dumpElement(_ element: AXUIElement, depth: Int) {
    if depth > 4 { return }
    let indent = String(repeating: "  ", count: depth)

    var role: AnyObject?
    var title: AnyObject?
    var desc: AnyObject?
    AXUIElementCopyAttributeValue(element, kAXRoleAttribute as CFString, &role)
    AXUIElementCopyAttributeValue(element, kAXTitleAttribute as CFString, &title)
    AXUIElementCopyAttributeValue(element, kAXDescriptionAttribute as CFString, &desc)

    let r = (role as? String) ?? "?"
    let t = (title as? String) ?? ""
    let d = (desc as? String) ?? ""
    var info = "\(indent)\(r)"
    if !t.isEmpty { info += " title=\"\(t)\"" }
    if !d.isEmpty { info += " desc=\"\(d)\"" }
    print(info)

    var children: AnyObject?
    AXUIElementCopyAttributeValue(element, kAXChildrenAttribute as CFString, &children)
    if let childArray = children as? [AXUIElement] {
        for child in childArray {
            dumpElement(child, depth: depth + 1)
        }
    }
}
