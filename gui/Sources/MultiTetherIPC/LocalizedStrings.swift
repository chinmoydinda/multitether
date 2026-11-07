import Foundation

public enum L10nKey: String, CaseIterable, Sendable {
    case mtDuplicateIP
    case mtOverlappingSubnet
    case mtPersistentDHCPUnavailable
    case mtPersistentDHCPFailed
    case mtInterfaceUpFailed
    case mtDHCPStillWaiting
    case mtDHCPPublishFailed
    case mtLinkLocalOnly
    case mtSubtitle
    case mtHelperUpdating
    case mtCheckingHelper
    case mtInstallingHelper
    case mtInstallHelper
    case mtNetworkingUnavailable
    case mtReady
    case mtConnecting
    case mtConfiguring
    case mtDisconnecting
    case mtConnected
    case mtNeedsSetup
    case mtRetry
    case mtEmptyTitle
    case mtEmptyBody
    case mtUSBConflict
    case mtRefresh
    case mtRemoveHelper
    case mtRemoveConfirm
    case mtHelperUnreachable
    case mtConnectionFailed
    case mtConnectionStopped
    case mtConnectionTimeout
    case mtRequestTimeout
    case mtActiveCount
    case mtQuitFailed
    case mtKeepOpen
    case mtQuitStillConnected
    case mtQuitting
    case mtQuitHint
    case connect
    case disconnect
    case usbDeviceFallbackName
    case authorizationCancelled
    case authorizationDenied
    case authorizationSessionFailed
    case libraryGenericFailure
    case helperInterfaceNotOwned
    case selectDeviceForSession
    case helperShuttingDown
    case helperSessionAlreadyRunning
    case helperRequestDecodeFailed
    case helperReplyEncodeFailed
    case helperOrphansCleaned
    case helperOrphanCleanupFailed
    case helperSigtermReceived
    case helperReady
    case installerNeedsRoot
    case installerUseTerminal
    case installerScriptMissing
    case helperConnectFailed
    case helperReplyUnparsable
    case installerNoPayload
    case installerAPIUnavailable
    case installerLaunchFailed
    case authPromptInstall
    case authPromptUninstall
    case quit

    public var text: String {
        switch self {
        case .mtDuplicateIP: return "Same IP address as %@; independent routing may not work."
        case .mtOverlappingSubnet: return "Shares an overlapping subnet with %@; traffic may use only one connection."
        case .mtPersistentDHCPUnavailable: return "The system persistent DHCP service is unavailable."
        case .mtPersistentDHCPFailed: return "Could not create a persistent DHCP service for %@."
        case .mtInterfaceUpFailed: return "Could not enable interface %@: %@"
        case .mtDHCPStillWaiting: return "The phone has not assigned an IP yet. DHCP is still running in the background. Keep USB tethering enabled, or retry."
        case .mtDHCPPublishFailed: return "The DHCP service for %@ did not publish network state."
        case .mtLinkLocalOnly: return "The phone has not supplied a DHCP lease. A 169.254 address cannot provide internet and is not treated as Connected."
        case .mtSubtitle: return "Multiple phones. One connection panel."
        case .mtHelperUpdating: return "The networking component is being updated. Please retry shortly."
        case .mtCheckingHelper: return "Checking helper…"
        case .mtInstallingHelper: return "Installing helper…"
        case .mtInstallHelper: return "Install Helper"
        case .mtNetworkingUnavailable: return "Install the networking helper to connect your phones. Administrator approval is requested after you click Install Helper."
        case .mtReady: return "Ready to connect"
        case .mtConnecting: return "Connecting…"
        case .mtConfiguring: return "Getting an IP address…"
        case .mtDisconnecting: return "Disconnecting…"
        case .mtConnected: return "Connected"
        case .mtNeedsSetup: return "Waiting for DHCP"
        case .mtRetry: return "Retry"
        case .mtEmptyTitle: return "Plug in a phone"
        case .mtEmptyBody: return "Connect a USB data cable, then enable USB tethering on your phone."
        case .mtUSBConflict: return "Release this phone in any other tethering app, then retry."
        case .mtRefresh: return "Refresh devices"
        case .mtRemoveHelper: return "Remove networking helper…"
        case .mtRemoveConfirm: return "This disconnects all MultiTether connections."
        case .mtHelperUnreachable: return "The networking component was installed but is not responding."
        case .mtConnectionFailed: return "The phone connection failed."
        case .mtConnectionStopped: return "The phone disconnected while connecting."
        case .mtConnectionTimeout: return "Connecting timed out. Check that USB tethering is enabled on the phone."
        case .mtRequestTimeout: return "The networking helper did not respond in time."
        case .mtActiveCount: return "%ld phones connected"
        case .mtQuitFailed: return "Could not disconnect all phones"
        case .mtKeepOpen: return "Keep App Open"
        case .mtQuitStillConnected: return "A phone is still connected. Please retry Quit."
        case .mtQuitting: return "Disconnecting phones…"
        case .mtQuitHint: return "Disconnect all phones and quit."
        case .connect: return "Connect"
        case .disconnect: return "Disconnect"
        case .usbDeviceFallbackName: return "USB device %04lx:%04lx"
        case .authorizationCancelled: return "Authorization cancelled"
        case .authorizationDenied: return "Authorization was denied (%ld)"
        case .authorizationSessionFailed: return "Cannot create an authorization session (%ld)"
        case .libraryGenericFailure: return "The operation failed (error code %ld)"
        case .helperInterfaceNotOwned: return "This interface has no running device session"
        case .selectDeviceForSession: return "Select a USB device first"
        case .helperShuttingDown: return "The helper is shutting down"
        case .helperSessionAlreadyRunning: return "A session is already running"
        case .helperRequestDecodeFailed: return "Cannot decode the request arguments: %@"
        case .helperReplyEncodeFailed: return "Cannot encode the reply: %@"
        case .helperOrphansCleaned: return "Cleaned up %ld leftover virtual interface(s) from a previous run"
        case .helperOrphanCleanupFailed: return "Failed to clean up leftover virtual interfaces: %@"
        case .helperSigtermReceived: return "Received SIGTERM; shutting down"
        case .helperReady: return "MultiTether helper ready: %@"
        case .installerNeedsRoot: return "Error: cannot obtain root (euid=%1$ld, ruid=%2$ld)."
        case .installerUseTerminal: return "Use the terminal instead: sudo %@"
        case .installerScriptMissing: return "Error: script %@ not found"
        case .helperConnectFailed: return "Cannot reach the privileged helper: %@"
        case .helperReplyUnparsable: return "The helper's reply could not be parsed; the versions may not match"
        case .installerNoPayload: return "This copy of MultiTether carries no installer payload (likely a dev build). "
                    + "Run this in a terminal: sudo ./gui/Scripts/install-helper.sh"
        case .installerAPIUnavailable: return "This version of macOS no longer offers the API in-app installation needs. "
                    + "Run this in a terminal: sudo ./gui/Scripts/install-helper.sh"
        case .installerLaunchFailed: return "Cannot launch the install/uninstall program (%ld)"
        case .authPromptInstall: return "MultiTether needs administrator privileges to install the privileged helper "
                    + "(copy it to /Library/PrivilegedHelperTools and register the system "
                    + "service)."
        case .authPromptUninstall: return "MultiTether needs administrator privileges to uninstall the privileged helper "
                    + "(unregister the system service and delete its files under /Library)."
        case .quit: return "Quit"
        }
    }
}

public func L(_ key: L10nKey, _ arguments: CVarArg...) -> String {
    arguments.isEmpty ? key.text : String(format: key.text, arguments: arguments)
}
