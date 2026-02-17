import Cocoa

let start = Date()
func log(_ msg: String) {
    let elapsed = Date().timeIntervalSince(start)
    print(String(format: "[%.1fs] %@", elapsed, msg))
}

// --- Find Ableton Live ---
let apps = NSRunningApplication.runningApplications(withBundleIdentifier: "com.ableton.live")
guard let live = apps.first else { log("Live not running"); exit(1) }
let pid = live.processIdentifier
log("Found Live (pid \(pid))")

// Wait for launch to complete (in case it just started)
for _ in 0..<30 {
    if live.isFinishedLaunching { break }
    Thread.sleep(forTimeInterval: 0.5)
}

// --- Helper: send a keystroke directly to Ableton's PID ---
// CGEvent.postToPid bypasses focus — no need for Ableton to be frontmost.
func sendKey(_ code: UInt16, cmd: Bool, to pid: pid_t) {
    guard let down = CGEvent(keyboardEventSource: nil, virtualKey: code, keyDown: true),
          let up   = CGEvent(keyboardEventSource: nil, virtualKey: code, keyDown: false)
    else { return }
    if cmd {
        down.flags = .maskCommand
        up.flags   = .maskCommand
    }
    down.postToPid(pid)
    up.postToPid(pid)
}

// --- Step 1: Save the project (Cmd+S) ---
// This eliminates unsaved changes so the quit won't trigger a save dialog.
// Key code 1 = 'S' on a US keyboard.
log("Saving project (Cmd+S)")
sendKey(1, cmd: true, to: pid)
Thread.sleep(forTimeInterval: 1.5)  // Give Ableton time to finish saving

// --- Step 2: Quit via Apple Event ---
// With no unsaved changes, this should exit immediately — no dialog.
log("Sending quit")
live.terminate()

// --- Wait for exit ---
let deadline = Date().addingTimeInterval(10)  // 10s should be plenty
while live.isTerminated == false && Date() < deadline {
    Thread.sleep(forTimeInterval: 0.2)
}

if live.isTerminated {
    log("Quit OK")
} else {
    log("Still running after 10s — force killing")
    live.forceTerminate()
}
