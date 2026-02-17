import Cocoa

// Inspect Ableton Live's accessibility hierarchy.
// Run this while Ableton is open to see what AX elements are visible.

let apps = NSRunningApplication.runningApplications(withBundleIdentifier: "com.ableton.live")
guard let live = apps.first else { print("Live not running"); exit(1) }
let pid = live.processIdentifier
print("Live pid: \(pid)")

let axApp = AXUIElementCreateApplication(pid)

func dump(_ element: AXUIElement, depth: Int = 0) {
    if depth > 6 { return }
    let indent = String(repeating: "  ", count: depth)

    var role: AnyObject?
    var title: AnyObject?
    var subrole: AnyObject?
    var desc: AnyObject?
    AXUIElementCopyAttributeValue(element, kAXRoleAttribute as CFString, &role)
    AXUIElementCopyAttributeValue(element, kAXTitleAttribute as CFString, &title)
    AXUIElementCopyAttributeValue(element, kAXSubroleAttribute as CFString, &subrole)
    AXUIElementCopyAttributeValue(element, kAXDescriptionAttribute as CFString, &desc)

    let r = (role as? String) ?? "?"
    let t = (title as? String) ?? ""
    let s = (subrole as? String) ?? ""
    let d = (desc as? String) ?? ""

    // Print everything (not just specific roles) so we see the full picture
    var info = "\(indent)\(r)"
    if !t.isEmpty { info += " title=\"\(t)\"" }
    if !s.isEmpty { info += " subrole=\"\(s)\"" }
    if !d.isEmpty { info += " desc=\"\(d)\"" }
    print(info)

    var children: AnyObject?
    AXUIElementCopyAttributeValue(element, kAXChildrenAttribute as CFString, &children)
    if let childArray = children as? [AXUIElement] {
        for child in childArray {
            dump(child, depth: depth + 1)
        }
    }
}

print("\n--- Ableton AX hierarchy ---")
dump(axApp)
print("--- end ---")
