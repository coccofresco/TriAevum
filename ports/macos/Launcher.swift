import AppKit

// The launcher owns writable user data; the application bundle stays read-only.
final class Launcher: NSObject, NSApplicationDelegate {
    var window: NSWindow!
    var status: NSTextField!
    var importButton: NSButton!
    var playButton: NSButton!
    var process: Process?
    let resources = Bundle.main.resourceURL!
    let data = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
        .appendingPathComponent("TriAevum", isDirectory: true)
    var profile: URL { data.appendingPathComponent("TriAevum.launch.json") }
    var runtime: URL { resources.appendingPathComponent("runtime", isDirectory: true) }

    func applicationDidFinishLaunching(_ notification: Notification) {
        try? FileManager.default.createDirectory(at: data, withIntermediateDirectories: true)
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 520, height: 290),
                          styleMask: [.titled, .closable, .miniaturizable], backing: .buffered, defer: false)
        window.title = "TriAevum"
        window.center()
        let content = window.contentView!
        let title = NSTextField(labelWithString: "TriAevum")
        title.font = .systemFont(ofSize: 30, weight: .semibold)
        title.frame = NSRect(x: 32, y: 222, width: 450, height: 40)
        content.addSubview(title)
        let subtitle = NSTextField(wrappingLabelWithString:
            "Ocarina of Time 3D for Apple Silicon\nSelect your decrypted .3ds or .cci ROM to set up the game.")
        subtitle.frame = NSRect(x: 32, y: 164, width: 456, height: 48)
        content.addSubview(subtitle)
        status = NSTextField(wrappingLabelWithString: "")
        status.frame = NSRect(x: 32, y: 78, width: 456, height: 65)
        status.textColor = .secondaryLabelColor
        content.addSubview(status)
        importButton = NSButton(title: "Choose ROM…", target: self, action: #selector(chooseROM))
        importButton.bezelStyle = .rounded
        importButton.frame = NSRect(x: 26, y: 25, width: 145, height: 32)
        content.addSubview(importButton)
        let folder = NSButton(title: "Data Folder", target: self, action: #selector(openData))
        folder.bezelStyle = .rounded
        folder.frame = NSRect(x: 180, y: 25, width: 130, height: 32)
        content.addSubview(folder)
        playButton = NSButton(title: "Play", target: self, action: #selector(play))
        playButton.bezelStyle = .rounded
        playButton.keyEquivalent = "\r"
        playButton.frame = NSRect(x: 360, y: 25, width: 130, height: 32)
        content.addSubview(playButton)
        refresh()
        let menu = NSMenu()
        let appItem = NSMenuItem()
        menu.addItem(appItem)
        let appMenu = NSMenu()
        appMenu.addItem(withTitle: "Quit TriAevum", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        appItem.submenu = appMenu
        NSApplication.shared.mainMenu = menu
        window.makeKeyAndOrderFront(nil)
        NSApplication.shared.activate(ignoringOtherApps: true)
    }

    func refresh() {
        playButton.isEnabled = FileManager.default.fileExists(atPath: profile.path) && process == nil
        importButton.isEnabled = process == nil
        status.stringValue = playButton.isEnabled ? "Ready to play. F1 opens settings." : "Setup downloads the verified TopScreen texture package. Your ROM stays on this Mac."
    }

    @objc func openData() { NSWorkspace.shared.open(data) }

    @objc func chooseROM() {
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = false
        panel.allowedFileTypes = ["3ds", "cci"]
        panel.beginSheetModal(for: window) { response in
            guard response == .OK, let rom = panel.url else { return }
            self.run(self.resources.appendingPathComponent("forge/TriAevumForge"),
                     [rom.path, "--root", self.runtime.path, "--data", self.data.path], game: false)
        }
    }

    @objc func play() {
        do {
            // Refresh bundle-relative paths after moving or updating the app.
            guard var document = try JSONSerialization.jsonObject(with: Data(contentsOf: profile)) as? [String: Any],
                  var arguments = document["arguments"] as? [String] else {
                throw NSError(domain: "TriAevum", code: 1, userInfo: [NSLocalizedDescriptionKey: "Invalid launch profile; import the ROM again."])
            }
            for (key, path) in [("--title-plugin", runtime.appendingPathComponent("triaevum_title_aot.dylib").path),
                                ("--resource-root", runtime.appendingPathComponent("resources").path)] {
                guard let index = arguments.firstIndex(of: key), index + 1 < arguments.count else {
                    throw NSError(domain: "TriAevum", code: 1, userInfo: [NSLocalizedDescriptionKey: "Invalid launch profile; import the ROM again."])
                }
                arguments[index + 1] = path
            }
            // Adopt alpha 2 shaders when updating an existing alpha 1 install.
            // These publisher artifacts are portable; driver caches stay local.
            for (key, path) in [("--pica-aot-shader-pack", runtime.appendingPathComponent("forge/shader-corpus/portable.o3ps").path),
                                ("--renderer-cache-directory", data.appendingPathComponent("cache/renderer").path)] {
                if let index = arguments.firstIndex(of: key), index + 1 < arguments.count {
                    arguments[index + 1] = path
                } else {
                    arguments += [key, path]
                }
            }
            document["arguments"] = arguments
            try JSONSerialization.data(withJSONObject: document, options: [.prettyPrinted]).write(to: profile, options: .atomic)
            run(runtime.appendingPathComponent("TriAevum"), ["--launch-profile", profile.path], game: true)
        } catch { status.stringValue = error.localizedDescription }
    }

    func run(_ executable: URL, _ arguments: [String], game: Bool) {
        guard process == nil else { return }
        let task = Process()
        task.executableURL = executable
        task.arguments = arguments
        task.currentDirectoryURL = data
        var environment = ProcessInfo.processInfo.environment
        environment["SDL_VULKAN_LIBRARY"] = runtime.appendingPathComponent("lib/libvulkan.1.dylib").path
        environment["VK_DRIVER_FILES"] = runtime.appendingPathComponent("lib/MoltenVK_icd.json").path
        task.environment = environment
        let pipe = Pipe()
        task.standardOutput = pipe
        task.standardError = pipe
        let logURL = data.appendingPathComponent(game ? "runtime.log" : "forge.log")
        FileManager.default.createFile(atPath: logURL.path, contents: nil)
        let log = try? FileHandle(forWritingTo: logURL)
        pipe.fileHandleForReading.readabilityHandler = { handle in
            let bytes = handle.availableData
            if bytes.isEmpty { return }
            try? log?.write(contentsOf: bytes)
            if !game, let text = String(data: bytes, encoding: .utf8), let line = text.split(separator: "\n").last {
                DispatchQueue.main.async { self.status.stringValue = String(line) }
            }
        }
        task.terminationHandler = { task in
            pipe.fileHandleForReading.readabilityHandler = nil
            try? log?.close()
            DispatchQueue.main.async {
                self.process = nil
                self.refresh()
                if task.terminationStatus != 0 {
                    self.status.stringValue = "Could not complete the operation. See \(logURL.lastPathComponent) in Data Folder for details."
                }
                self.window.deminiaturize(nil)
                self.window.makeKeyAndOrderFront(nil)
            }
        }
        do {
            process = task
            try task.run()
            importButton.isEnabled = false
            playButton.isEnabled = false
            status.stringValue = game ? "Game is running." : "Preparing your game…"
            if game { window.miniaturize(nil) }
        } catch {
            process = nil
            refresh()
            status.stringValue = error.localizedDescription
        }
    }

    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        if process?.isRunning == true {
            window.deminiaturize(nil)
            status.stringValue = "Close the game or wait for setup to finish before quitting."
            return .terminateCancel
        }
        return .terminateNow
    }
}

let app = NSApplication.shared
let delegate = Launcher()
app.delegate = delegate
app.setActivationPolicy(.regular)
app.run()
