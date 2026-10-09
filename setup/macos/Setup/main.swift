// Def Jam Recompiled setup for macOS (Apple Silicon): a native wizard and silent entry point.
//
// A thin shell around the same engine the Windows wizard runs. This app only collects the
// choices, unpacks the SHA-256-checked payload it carries, starts the bundled Python engine
// and shows its status. Verification, extraction, the build and the install all happen in
// setup/engine.py, which checks the payload's complete file inventory before running a stage.
// Nothing from the player's dump leaves the machine.
import AppKit
import CryptoKit
import Foundation

let wizardTitle = "Def Jam Recompiled \u{2014} Setup"
let commandLineToolsURL = "https://developer.apple.com/documentation/xcode/installing-the-command-line-tools/"
let fs = FileManager.default
let home = fs.homeDirectoryForCurrentUser

struct SetupFailure: Error { let message: String; let code: Int32 }

// MARK: - Session (scratch, status, cancel and log files)

final class Session {
    let id = UUID().uuidString
    let scratch: URL
    let payload: URL
    let status: URL
    let cancel: URL
    var log: URL

    init(logOverride: String? = nil) {
        scratch = home.appendingPathComponent("Library/Caches/DefJamSetup/\(id)")
        payload = scratch.appendingPathComponent("payload")
        status = scratch.appendingPathComponent("status.txt")
        cancel = scratch.appendingPathComponent("cancel")
        log = home.appendingPathComponent("Library/Logs/DefJamSetup/\(id).log")
        if let override = logOverride { log = URL(fileURLWithPath: override) }
        try? fs.createDirectory(at: log.deletingLastPathComponent(), withIntermediateDirectories: true)
        try? fs.createDirectory(at: scratch, withIntermediateDirectories: true)
        note("Preparing setup files. Diagnostics remain here even if validation fails.")
    }

    func note(_ message: String) {
        guard let data = (message + "\n").data(using: .utf8) else { return }
        if let handle = try? FileHandle(forWritingTo: log) {
            handle.seekToEndOfFile(); handle.write(data); try? handle.close()
        } else {
            try? data.write(to: log)
        }
    }

    /// Verify the embedded payload against the digest compiled into this app, then unpack it.
    func unpack() throws {
        guard let archive = Bundle.main.url(forResource: "payload", withExtension: "zip") else {
            throw SetupFailure(message: "Installer payload missing", code: 4)
        }
        var hasher = SHA256()
        guard let stream = try? FileHandle(forReadingFrom: archive) else {
            throw SetupFailure(message: "Installer payload cannot be read", code: 4)
        }
        while let chunk = try? stream.read(upToCount: 1 << 20), !chunk.isEmpty { hasher.update(data: chunk) }
        try? stream.close()
        let actual = hasher.finalize().map { String(format: "%02x", $0) }.joined()
        guard actual == payloadSHA256 else {
            throw SetupFailure(message: "Installer payload verification failed. Download it again.", code: 4)
        }
        let ditto = Process()
        ditto.executableURL = URL(fileURLWithPath: "/usr/bin/ditto")
        ditto.arguments = ["-x", "-k", archive.path, payload.path]
        try ditto.run()
        ditto.waitUntilExit()
        guard ditto.terminationStatus == 0 else {
            throw SetupFailure(message: "Cannot unpack setup payload. Check the free disk space.", code: 4)
        }
        note("Setup payload prepared.")
    }

    func engineProcess(_ arguments: [String]) -> Process {
        let process = Process()
        process.executableURL = payload.appendingPathComponent("python/bin/python3")
        var all = ["-B", payload.appendingPathComponent("engine/engine.py").path, "--payload", payload.path] + arguments
        if !arguments.contains("--log") { all += ["--log", log.path] }
        process.arguments = all
        process.environment = ["HOME": home.path, "PATH": "/usr/bin:/bin:/usr/sbin:/sbin",
                               "TMPDIR": NSTemporaryDirectory(), "LANG": "en_US.UTF-8"]
        return process
    }
}

// MARK: - Silent mode

func helpText() -> String {
    "Silent setup:\n  DefJamSetup --silent --dump PATH --install-dir PATH --data-dir PATH "
    + "[--no-shortcuts] [--no-desktop-shortcut] [--hd-textures] [--log PATH]\n"
    + "Remove the app (keeps your dump, saves and settings):\n  DefJamSetup --silent --uninstall --install-dir PATH\n\n"
    + "Exit codes: 0 success, 2 invalid input, 3 prerequisites missing (Xcode Command Line Tools), 4 failure, "
    + "5 busy, 6 cancelled."
}

func runSilent(_ arguments: [String]) -> Int32 {
    var logOverride: String?
    if let index = arguments.firstIndex(of: "--log"), index + 1 < arguments.count { logOverride = arguments[index + 1] }
    let session = Session(logOverride: logOverride)
    do {
        try session.unpack()
        let process = session.engineProcess(arguments.filter { $0 != "--silent" })
        try process.run()
        process.waitUntilExit()
        let code = process.terminationStatus
        if code == 0 { try? fs.removeItem(at: session.scratch) }
        return code
    } catch let failure as SetupFailure {
        session.note("Setup stopped: " + failure.message)
        FileHandle.standardError.write(Data(("Setup stopped: " + failure.message + "\n").utf8))
        return failure.code
    } catch {
        session.note("Setup stopped: \(error.localizedDescription)")
        return 4
    }
}

// MARK: - Wizard

final class Wizard: NSObject, NSApplicationDelegate, NSWindowDelegate {
    let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 740, height: 520),
                          styleMask: [.titled, .closable, .miniaturizable], backing: .buffered, defer: false)
    let dumpField = NSTextField(string: "")
    let installField = NSTextField(string: "")
    let dataField = NSTextField(string: "")
    let hdUpscale = NSButton(checkboxWithTitle: "Apply HD texture upscale during install (increases install time)", target: nil, action: nil)
    let desktopShortcut = NSButton(checkboxWithTitle: "Create a desktop shortcut", target: nil, action: nil)
    let status = NSTextField(wrappingLabelWithString: "Choose your dump and destinations, then select Install / Repair.")
    let launchInfo = NSTextField(wrappingLabelWithString: "After setup, open Def Jam Recompiled from your install folder, Applications or the Desktop.")
    let installButton = NSButton(title: "Install / Repair", target: nil, action: nil)
    let logsButton = NSButton(title: "Open logs", target: nil, action: nil)
    let playButton = NSButton(title: "Play", target: nil, action: nil)
    let closeButton = NSButton(title: "Close", target: nil, action: nil)
    var controls: [NSControl] = []
    var session: Session?
    var process: Process?
    var timer: Timer?
    var running = false
    var ticks = 0
    var preparationTicks = 0
    var smokeMode: String?
    var smokeResult: Int32 = 4

    override init() {
        super.init()
        build()
    }

    func label(_ text: String) -> NSTextField {
        let field = NSTextField(labelWithString: text)
        field.lineBreakMode = .byWordWrapping
        return field
    }

    func row(_ field: NSTextField, _ buttons: [NSButton]) -> NSStackView {
        field.translatesAutoresizingMaskIntoConstraints = false
        field.widthAnchor.constraint(greaterThanOrEqualToConstant: 400).isActive = true
        let stack = NSStackView(views: [field] + buttons)
        stack.orientation = .horizontal
        stack.spacing = 8
        return stack
    }

    func build() {
        window.title = wizardTitle
        window.delegate = self
        window.isReleasedWhenClosed = false
        let support = home.appendingPathComponent("Library/Application Support/DefJamRecompiled")
        installField.stringValue = support.appendingPathComponent("app").path
        dataField.stringValue = support.appendingPathComponent("data").path
        desktopShortcut.state = .on
        hdUpscale.state = .off
        launchInfo.isSelectable = true

        let heading = label("Build and play from your own USA Xbox dump")
        heading.font = .boldSystemFont(ofSize: 15)
        let intro = label("Setup builds the game on this Mac from your dump. The first setup can take about an hour and needs no internet connection.")
        let tools = NSTextField(labelWithString: "")
        tools.allowsEditingTextAttributes = true
        tools.isSelectable = true
        let text = NSMutableAttributedString(string: "Requires the Xcode Command Line Tools. If they are missing, setup stops and tells you; ")
        text.append(NSAttributedString(string: "how to install them", attributes: [.link: URL(string: commandLineToolsURL)!]))
        text.append(NSAttributedString(string: "."))
        tools.attributedStringValue = text

        let imageButton = NSButton(title: "Image\u{2026}", target: self, action: #selector(chooseImage))
        let folderButton = NSButton(title: "Folder\u{2026}", target: self, action: #selector(chooseDumpFolder))
        let installBrowse = NSButton(title: "Browse\u{2026}", target: self, action: #selector(chooseInstall))
        let dataBrowse = NSButton(title: "Browse\u{2026}", target: self, action: #selector(chooseData))
        installButton.target = self; installButton.action = #selector(start); installButton.keyEquivalent = "\r"
        logsButton.target = self; logsButton.action = #selector(openLogs)
        playButton.target = self; playButton.action = #selector(play); playButton.isEnabled = false
        closeButton.target = self; closeButton.action = #selector(closePressed)
        controls = [dumpField, installField, dataField, imageButton, folderButton, installBrowse, dataBrowse, hdUpscale, desktopShortcut, installButton]
        let hdNote = label("Adds about 6.2 GB; needs extra working space. No GPU needed.")
        hdNote.textColor = .secondaryLabelColor
        hdNote.font = .systemFont(ofSize: 11)

        let buttons = NSStackView(views: [installButton, logsButton, playButton, NSView(), closeButton])
        buttons.orientation = .horizontal
        buttons.spacing = 10
        let stack = NSStackView(views: [
            heading, intro, tools,
            label("Your ISO/XISO or extracted dump"), row(dumpField, [imageButton, folderButton]),
            label("Install location"), row(installField, [installBrowse]),
            label("Data location (dump copy, saves and settings; preserved across updates)"), row(dataField, [dataBrowse]),
            hdUpscale, hdNote, desktopShortcut, status, launchInfo, buttons])
        stack.orientation = .vertical
        stack.alignment = .leading
        stack.spacing = 10
        stack.edgeInsets = NSEdgeInsets(top: 20, left: 24, bottom: 20, right: 24)
        stack.translatesAutoresizingMaskIntoConstraints = false
        for view in [intro, status, launchInfo, buttons] {
            view.translatesAutoresizingMaskIntoConstraints = false
            view.widthAnchor.constraint(equalToConstant: 692).isActive = true
        }
        let content = NSView()
        content.addSubview(stack)
        NSLayoutConstraint.activate([
            stack.leadingAnchor.constraint(equalTo: content.leadingAnchor),
            stack.topAnchor.constraint(equalTo: content.topAnchor),
            stack.trailingAnchor.constraint(equalTo: content.trailingAnchor),
            stack.bottomAnchor.constraint(equalTo: content.bottomAnchor)])
        window.contentView = content
        window.setContentSize(stack.fittingSize)
        window.center()
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        let menu = NSMenu()
        let appItem = NSMenuItem()
        menu.addItem(appItem)
        let appMenu = NSMenu()
        appMenu.addItem(withTitle: "Quit Def Jam Setup", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        appItem.submenu = appMenu
        NSApp.mainMenu = menu
        if smokeMode == "preparation" {
            dumpField.stringValue = "missing.iso"
            let probe = home.appendingPathComponent("Library/Caches/DefJamSetup/probe")
            installField.stringValue = probe.path
            dataField.stringValue = probe.appendingPathComponent("data").path
            start()
            return
        }
        window.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }

    func windowShouldClose(_ sender: NSWindow) -> Bool {
        guard running else { return true }
        confirmCancel()
        return false
    }

    // MARK: actions

    func choose(files: Bool, folders: Bool, into field: NSTextField) {
        let panel = NSOpenPanel()
        panel.canChooseFiles = files
        panel.canChooseDirectories = folders
        panel.canCreateDirectories = folders && !files
        panel.allowsMultipleSelection = false
        if panel.runModal() == .OK, let url = panel.url { field.stringValue = url.path }
    }

    @objc func chooseImage() { choose(files: true, folders: false, into: dumpField) }
    @objc func chooseDumpFolder() { choose(files: false, folders: true, into: dumpField) }
    @objc func chooseInstall() { choose(files: false, folders: true, into: installField) }
    @objc func chooseData() { choose(files: false, folders: true, into: dataField) }

    func setBusy(_ value: Bool) {
        running = value
        controls.forEach { $0.isEnabled = !value }
        playButton.isEnabled = false
        closeButton.title = value ? "Cancel" : "Close"
    }

    func alert(_ message: String, link: Bool = false) {
        if smokeMode != nil { return }
        let box = NSAlert()
        box.messageText = "Setup"
        box.informativeText = message
        box.alertStyle = .warning
        if link { box.addButton(withTitle: "OK"); box.addButton(withTitle: "How to install") }
        if box.runModal() == .alertSecondButtonReturn, link { NSWorkspace.shared.open(URL(string: commandLineToolsURL)!) }
    }

    @objc func start() {
        guard !running else { return }
        let dump = dumpField.stringValue.trimmingCharacters(in: .whitespaces)
        if dump.isEmpty { alert("Choose your Xbox dump first."); return }
        let session = Session()
        self.session = session
        var arguments = ["--dump", dump, "--install-dir", installField.stringValue, "--data-dir", dataField.stringValue,
                         "--status-file", session.status.path, "--cancel-file", session.cancel.path]
        if desktopShortcut.state != .on { arguments.append("--no-desktop-shortcut") }
        if hdUpscale.state == .on { arguments.append("--hd-textures") }
        setBusy(true)
        ticks = 0
        status.stringValue = "Preparing setup files\u{2026} You can move the window or cancel."
        timer = Timer.scheduledTimer(withTimeInterval: 0.4, repeats: true) { [weak self] _ in self?.tick() }
        DispatchQueue.global().async {
            do {
                try session.unpack()
                if fs.fileExists(atPath: session.cancel.path) {
                    session.note("Setup cancelled during preparation.")
                    throw SetupFailure(message: "Setup cancelled.", code: 6)
                }
                let process = session.engineProcess(arguments)
                process.terminationHandler = { finished in
                    DispatchQueue.main.async { self.finish(code: finished.terminationStatus) }
                }
                try process.run()
                DispatchQueue.main.async { self.process = process; self.preparationTicks = self.ticks }
            } catch let failure as SetupFailure {
                session.note("Setup stopped: " + failure.message)
                DispatchQueue.main.async { self.fail(failure.message, code: failure.code) }
            } catch {
                session.note("Setup stopped: \(error.localizedDescription)")
                DispatchQueue.main.async { self.fail(error.localizedDescription, code: 4) }
            }
        }
    }

    func tick() {
        ticks += 1
        guard process != nil, let session = session,
              let text = try? String(contentsOf: session.status, encoding: .utf8), !text.isEmpty else { return }
        status.stringValue = text
    }

    func fail(_ message: String, code: Int32) {
        timer?.invalidate()
        setBusy(false)
        let path = session?.log.path ?? ""
        status.stringValue = message + "\n\nLog: " + path
        if smokeMode != nil { smokeResult = 4; NSApp.terminate(nil); return }
        alert(message + "\n\nLog: " + path)
    }

    func finish(code: Int32) {
        timer?.invalidate()
        process = nil
        setBusy(false)
        guard let session = session else { return }
        if smokeMode == "preparation" {
            let log = (try? String(contentsOf: session.log, encoding: .utf8)) ?? ""
            smokeResult = code == 2 && preparationTicks >= 3 && log.contains("separate, non-nested") ? 0 : 4
            NSApp.terminate(nil)
            return
        }
        if code == 0 {
            let launcher = URL(fileURLWithPath: installField.stringValue).appendingPathComponent("Def Jam Recompiled.app")
            playButton.isEnabled = true
            launchInfo.stringValue = "To play, select Play or open this app:\n" + launcher.path
            status.stringValue = "Setup complete. Your game is ready to launch."
            try? fs.removeItem(at: session.scratch)
            return
        }
        let last = (try? String(contentsOf: session.status, encoding: .utf8)) ?? ""
        let reason = last.isEmpty ? "Setup stopped (exit \(code))." : last
        status.stringValue = reason + "\n\nLog: " + session.log.path
        alert(reason + "\n\nLog: " + session.log.path + "\n\nSelect Open logs for details.", link: code == 3)
    }

    func confirmCancel() {
        let box = NSAlert()
        box.messageText = "Cancel setup? Completed steps can be resumed."
        box.addButton(withTitle: "Cancel setup")
        box.addButton(withTitle: "Keep going")
        if box.runModal() == .alertFirstButtonReturn, let session = session {
            fs.createFile(atPath: session.cancel.path, contents: Data("cancel".utf8))
            status.stringValue = "Cancelling the current step\u{2026}"
        }
    }

    @objc func closePressed() {
        if running { confirmCancel() } else { window.close() }
    }

    @objc func openLogs() {
        let url = session?.log ?? home.appendingPathComponent("Library/Logs/DefJamSetup")
        try? fs.createDirectory(at: url.hasDirectoryPath ? url : url.deletingLastPathComponent(), withIntermediateDirectories: true)
        NSWorkspace.shared.open(url)
    }

    @objc func play() {
        let launcher = URL(fileURLWithPath: installField.stringValue).appendingPathComponent("Def Jam Recompiled.app")
        NSWorkspace.shared.openApplication(at: launcher, configuration: NSWorkspace.OpenConfiguration())
    }

    /// Draw the window into a PNG without showing it (documentation and layout checks).
    func snapshot(to path: String) -> Int32 {
        guard let view = window.contentView else { return 4 }
        view.layoutSubtreeIfNeeded()
        guard let image = view.bitmapImageRepForCachingDisplay(in: view.bounds) else { return 4 }
        view.cacheDisplay(in: view.bounds, to: image)
        guard let png = image.representation(using: .png, properties: [:]) else { return 4 }
        return (try? png.write(to: URL(fileURLWithPath: path))) == nil ? 4 : 0
    }

    /// Hardware-free CI probe: every control exists, with the right defaults, and the
    /// wizard needed no Python, compiler or payload extraction merely to open.
    func uiSmoke() -> Int32 {
        let valid = window.title == wizardTitle && !installField.stringValue.isEmpty && !dataField.stringValue.isEmpty
            && desktopShortcut.state == .on && hdUpscale.state == .off && installButton.isEnabled
            && dumpField.stringValue.isEmpty && !playButton.isEnabled && controls.count == 10
        return valid ? 0 : 4
    }
}

// MARK: - Entry point

let arguments = Array(CommandLine.arguments.dropFirst())
if arguments.contains("--help") {
    print(helpText())
    exit(0)
}
if arguments.contains("--silent") {
    exit(runSilent(arguments))
}
if !arguments.isEmpty && !arguments[0].hasPrefix("-psn") && !arguments[0].hasPrefix("-NS")
    && !["--ui-smoke", "--ui-preparation-smoke", "--ui-snapshot"].contains(arguments[0]) {
    FileHandle.standardError.write(Data("Command-line installation requires --silent; use --help for options.\n".utf8))
    exit(2)
}
let application = NSApplication.shared
application.setActivationPolicy(.regular)
let wizard = Wizard()
if arguments.first == "--ui-smoke" { exit(wizard.uiSmoke()) }
if arguments.count == 2 && arguments[0] == "--ui-snapshot" { exit(wizard.snapshot(to: arguments[1])) }
if arguments.first == "--ui-preparation-smoke" { wizard.smokeMode = "preparation" }
application.delegate = wizard
application.run()
exit(wizard.smokeMode == nil ? 0 : wizard.smokeResult)
