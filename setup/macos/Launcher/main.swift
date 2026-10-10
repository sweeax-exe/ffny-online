// The app that setup makes on the player's own Mac and that plays the installed game.
//
// It reads the install receipt, holds the play lock for as long as the game runs (the
// lock descriptor survives the exec, so the lock lives exactly as long as the game),
// supplies the data folder and working directory, keeps the session's log and then
// becomes the game. It is built here, never downloaded, so Gatekeeper has nothing to
// say about it. Also: `--make-alias TARGET ALIAS` writes a Finder alias without
// needing Finder automation permission (used for the Desktop shortcut).
import AppKit
import Foundation

struct Receipt: Decodable {
    let source: String
    let data: String
    let executable: String
}

func fail(_ message: String) -> Never {
    NSApplication.shared.setActivationPolicy(.regular)
    NSApp.activate(ignoringOtherApps: true)
    let alert = NSAlert()
    alert.messageText = "Def Jam Recompiled"
    alert.informativeText = message
    alert.alertStyle = .warning
    alert.runModal()
    exit(1)
}

func installRoot() -> URL {
    let bundle = Bundle.main.bundleURL
    // A copy in ~/Applications names the install it belongs to; the one made in the
    // install folder is simply its neighbour.
    if let note = Bundle.main.url(forResource: "install-root", withExtension: "txt"),
       let text = try? String(contentsOf: note, encoding: .utf8) {
        let path = text.trimmingCharacters(in: .whitespacesAndNewlines)
        if !path.isEmpty { return URL(fileURLWithPath: path) }
    }
    return bundle.deletingLastPathComponent()
}

func makeAlias(target: String, alias: String) -> Int32 {
    do {
        let data = try URL(fileURLWithPath: target).bookmarkData(options: .suitableForBookmarkFile)
        try URL.writeBookmarkData(data, to: URL(fileURLWithPath: alias))
        return 0
    } catch {
        FileHandle.standardError.write(Data("Could not create the alias: \(error.localizedDescription)\n".utf8))
        return 4
    }
}

/// Keep the ten newest session logs so a problem report always has the last run.
func openSessionLog() {
    let logs = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Logs/DefJamRecompiled")
    try? FileManager.default.createDirectory(at: logs, withIntermediateDirectories: true)
    let stamp = DateFormatter()
    stamp.dateFormat = "yyyyMMdd-HHmmss"
    stamp.locale = Locale(identifier: "en_US_POSIX")
    let base = "play-" + stamp.string(from: Date())
    let existing = ((try? FileManager.default.contentsOfDirectory(atPath: logs.path)) ?? [])
        .filter { $0.hasPrefix("play-") && $0.hasSuffix(".log") }.sorted()
    for old in existing.dropLast(9) {
        try? FileManager.default.removeItem(at: logs.appendingPathComponent(old))
        try? FileManager.default.removeItem(at: logs.appendingPathComponent(old + ".err"))
    }
    let out = open(logs.appendingPathComponent(base + ".log").path, O_WRONLY | O_CREAT | O_APPEND, 0o644)
    let err = open(logs.appendingPathComponent(base + ".log.err").path, O_WRONLY | O_CREAT | O_APPEND, 0o644)
    if out >= 0 { dup2(out, 1); close(out) }
    if err >= 0 { dup2(err, 2); close(err) }
}

let arguments = CommandLine.arguments
if arguments.count == 4 && arguments[1] == "--make-alias" {
    exit(makeAlias(target: arguments[2], alias: arguments[3]))
}
// The only options handed on to the game: show or skip its launcher window for this start
// (`open "Def Jam Recompiled.app" --args --launcher`). Nothing else is passed through.
let gameFlags = ["--launcher", "--no-launcher"]
if arguments.count > 1 && arguments[1] != "--launch" && !gameFlags.contains(arguments[1])
    && !arguments[1].hasPrefix("-psn") && !arguments[1].hasPrefix("-NS") {
    fail("Double-click this app to start your installed game.")
}

let root = installRoot()
guard let bytes = try? Data(contentsOf: root.appendingPathComponent("installed.json")),
      let receipt = try? JSONDecoder().decode(Receipt.self, from: bytes),
      FileManager.default.isExecutableFile(atPath: receipt.executable) else {
    fail("No completed installation was found. Run Def Jam Setup to install or repair it.")
}

// Held for the life of the game: an update or repair cannot replace a running game.
let lock = open(root.appendingPathComponent("play.lock").path, O_RDWR | O_CREAT, 0o644)
if lock < 0 || flock(lock, LOCK_EX | LOCK_NB) != 0 {
    fail("The game or an update is already running.")
}

setenv("DEFJAM_DATA", receipt.data, 1)
guard chdir(receipt.source) == 0 else { fail("The installed game folder is missing. Run Def Jam Setup to repair it.") }
openSessionLog()
let program = strdup(receipt.executable)
var argv: [UnsafeMutablePointer<CChar>?] = [program] + arguments.dropFirst().filter { gameFlags.contains($0) }.map { strdup($0) } + [nil]
execv(receipt.executable, &argv)
fail("The game could not be started (\(String(cString: strerror(errno)))). Run Def Jam Setup to repair it.")
