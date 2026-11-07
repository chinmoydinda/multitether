import Foundation
import MultiTetherCore
import MultiTetherIPC

runInstallerModeIfRequested()

func writeToStandardError(_ message: String) {
    FileHandle.standardError.write(Data((message + "\n").utf8))
}

do {
    let removed = try MultiTetherLibrary.cleanupOrphanInterfaces()
    if removed > 0 {
        writeToStandardError(L(.helperOrphansCleaned, Int(removed)))
    }
} catch {
    writeToStandardError(L(.helperOrphanCleanupFailed, error.localizedDescription))
}

let service = HelperService()
let delegate = HelperListenerDelegate(service: service)

signal(SIGTERM, SIG_IGN)
let terminationSource = DispatchSource.makeSignalSource(signal: SIGTERM, queue: .main)
terminationSource.setEventHandler {
    writeToStandardError(L(.helperSigtermReceived))
    service.shutdown()
    exit(0)
}
terminationSource.resume()

let listener = NSXPCListener(machServiceName: HelperConstants.machServiceName)
listener.delegate = delegate
listener.resume()

writeToStandardError(L(.helperReady, HelperConstants.buildVersion))

dispatchMain()
