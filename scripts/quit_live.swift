import Cocoa

// Quit Ableton Live cleanly via Accessibility API.
// No focus stealing, no keystrokes — works entirely in the background.
//
// Handles two dialogs:
//   1. Crash recovery ("recover your work?") → clicks "No"
//   2. Save dialog ("save changes?") → clicks "Don't Save"
//
// Requires: the parent terminal app must have Accessibility permission.

let start = Date()
func log(_ msg: String) {
    let elapsed = Date().timeIntervalSince(start)
    print(String(format: "[%.1fs] %@", elapsed, msg))
}

// --- Helper: find a button by AXDescription inside a specific element ---
func findButtonByDesc(in element: AXUIElement, desc target: String, depth: Int = 0) -> AXUIElement? {
    if depth > 6 { return nil }
    var role: AnyObject?
    AXUIElementCopyAttributeValue(element, kAXRoleAttribute as CFString, &role)
    if let r = role as? String, r == kAXButtonRole as String {
        var desc: AnyObject?
        AXUIElementCopyAttributeValue(element, kAXDescriptionAttribute as CFString, &desc)
        if let d = desc as? String, d == target { return element }
    }
    var children: AnyObject?
    AXUIElementCopyAttributeValue(element, kAXChildrenAttribute as CFString, &children)
    if let childArray = children as? [AXUIElement] {
        for child in childArray {
            if let found = findButtonByDesc(in: child, desc: target, depth: depth + 1) { return found }
        }
    }
    return nil
}

// --- Helper: search ALL windows for a button (uses kAXWindowsAttribute) ---
func findButtonInWindows(axApp: AXUIElement, desc target: String) -> AXUIElement? {
    var windows: AnyObject?
    AXUIElementCopyAttributeValue(axApp, kAXWindowsAttribute as CFString, &windows)
    if let windowList = windows as? [AXUIElement] {
        for window in windowList {
            if let btn = findButtonByDesc(in: window, desc: target) { return btn }
        }
    }
    return nil
}

// --- Helper: check if process is actually running via kill(0) ---
func isProcessRunning(_ pid: pid_t) -> Bool {
    return kill(pid, 0) == 0
}

// --- Find Ableton Live ---
let apps = NSRunningApplication.runningApplications(withBundleIdentifier: "com.ableton.live")
guard let live = apps.first else { log("Live not running"); exit(1) }
let pid = live.processIdentifier
let axApp = AXUIElementCreateApplication(pid)
log("Found Live (pid \(pid))")

for _ in 0..<30 {
    if live.isFinishedLaunching { break }
    Thread.sleep(forTimeInterval: 0.5)
}

// --- Step 1: Dismiss crash recovery dialog if present ---
if let noBtn = findButtonInWindows(axApp: axApp, desc: "No") {
    log("Crash recovery dialog — clicking 'No'")
    AXUIElementPerformAction(noBtn, kAXPressAction as CFString)
    // Poll until main window appears
    let loadDeadline = Date().addingTimeInterval(30)
    while Date() < loadDeadline {
        var windows: AnyObject?
        AXUIElementCopyAttributeValue(axApp, kAXWindowsAttribute as CFString, &windows)
        if let wl = windows as? [AXUIElement] {
            for w in wl {
                var sr: AnyObject?
                AXUIElementCopyAttributeValue(w, kAXSubroleAttribute as CFString, &sr)
                if (sr as? String) == "AXStandardWindow" {
                    log("Main window loaded")
                    break
                }
            }
        }
        if !isProcessRunning(pid) { break }
        Thread.sleep(forTimeInterval: 0.5)
    }
}

// --- Step 2: Send quit ---
log("Sending terminate()")
live.terminate()

// --- Step 3: Single loop — watch for save dialog AND process exit (up to 60s) ---
// Ableton can take 20-30s to shut down its audio engine after windows close.
let deadline = Date().addingTimeInterval(60)
var dismissed = false
var lastWindowCount = -1

while Date() < deadline && isProcessRunning(pid) {
    // Log window changes for diagnostics
    var windows: AnyObject?
    AXUIElementCopyAttributeValue(axApp, kAXWindowsAttribute as CFString, &windows)
    let wc = (windows as? [AXUIElement])?.count ?? 0
    if wc != lastWindowCount {
        log("Windows: \(wc)")
        lastWindowCount = wc
    }

    // Click "Don't Save" if save dialog appears
    if !dismissed, let btn = findButtonInWindows(axApp: axApp, desc: "Don't Save") {
        log("Found 'Don't Save' — clicking")
        AXUIElementPerformAction(btn, kAXPressAction as CFString)
        dismissed = true
    }
    Thread.sleep(forTimeInterval: 0.5)
}

if !isProcessRunning(pid) {
    log(dismissed ? "Quit OK (dismissed save dialog)" : "Quit OK (clean)")
} else {
    log("WARNING: still running after 60s — giving up (NOT killing)")
    exit(1)
}
