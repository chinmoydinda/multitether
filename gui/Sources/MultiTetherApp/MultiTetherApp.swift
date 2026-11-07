import AppKit
import SwiftUI
import MultiTetherIPC

@MainActor
final class MultiTetherAppDelegate: NSObject, NSApplicationDelegate {
    let model = MultiTetherModel()
    private var terminationPending = false

    func applicationDidFinishLaunching(_ notification: Notification) {
        // Probe readiness at launch without presenting administrator authorization.
        NSApp.appearance = nil
        model.start()
    }
    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        guard !terminationPending else { return .terminateLater }
        terminationPending = true
        Task {
            do {
                try await model.prepareForQuit()
                sender.reply(toApplicationShouldTerminate: true)
            } catch {
                // Keep the app available to retry instead of abandoning live connections.
                let alert = NSAlert()
                alert.messageText = L(.mtQuitFailed)
                alert.informativeText = error.localizedDescription
                alert.addButton(withTitle: L(.mtKeepOpen))
                alert.runModal()
                model.cancelQuit()
                terminationPending = false
                sender.reply(toApplicationShouldTerminate: false)
            }
        }
        return .terminateLater
    }
}

@main
@MainActor
struct MultiTetherApplication: App {
    @NSApplicationDelegateAdaptor(MultiTetherAppDelegate.self) private var delegate

    var body: some Scene {
        MenuBarExtra {
            MultiTetherPanel(model: delegate.model)
        } label: {
            Image(nsImage: MenuBarArtwork.image)
                .renderingMode(.template)
                .resizable()
                .scaledToFit()
                .frame(width: 18, height: 18)
                .accessibilityLabel("MultiTether")
                .help("MultiTether")
        }
        .menuBarExtraStyle(.window)
    }
}

@MainActor
enum MenuBarArtwork {
    static let image: NSImage = {
        let image = Bundle.main.url(forResource: "MenuBarIcon", withExtension: "pdf")
            .flatMap { NSImage(contentsOf: $0) }
            ?? NSImage(systemSymbolName: "cable.connector", accessibilityDescription: "MultiTether")!
        image.size = NSSize(width: 18, height: 18)
        image.isTemplate = true
        return image
    }()
}
