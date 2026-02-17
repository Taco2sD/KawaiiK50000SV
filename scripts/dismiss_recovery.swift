import Cocoa

// Dismiss Ableton's "recover your work?" dialog by clicking "No".
// Polls until the dialog appears (or Ableton finishes loading without one).

let apps = NSRunningApplication.runningApplications(withBundleIdentifier: "com.ableton.live")
guard let live = apps.first else { print("Live not running"); exit(1) }
let axApp = AXUIElementCreateApplication(live.processIdentifier)

let deadline = Date().addingTimeInterval(30)
while Date() < deadline {
    if let noBtn = findButtonByDesc(in: axApp, desc: "No") {
        print("Recovery dialog found — clicking 'No'")
        AXUIElementPerformAction(noBtn, kAXPressAction as CFString)
        exit(0)
    }
    // If Ableton has a normal window (title != empty, not a dialog), no recovery dialog
    var windows: AnyObject?
    AXUIElementCopyAttributeValue(axApp, kAXWindowsAttribute as CFString, &windows)
    if let windowList = windows as? [AXUIElement] {
        for window in windowList {
            var subrole: AnyObject?
            AXUIElementCopyAttributeValue(window, kAXSubroleAttribute as CFString, &subrole)
            if let s = subrole as? String, s == "AXStandardWindow" {
                print("No recovery dialog — Ableton loaded normally")
                exit(0)
            }
        }
    }
    Thread.sleep(forTimeInterval: 0.3)
}
print("Timed out waiting for Ableton")
exit(1)

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
