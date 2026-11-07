import Darwin
import MultiTetherIPC
import Foundation

func runInstallerModeIfRequested() {
    let scriptName: String
    switch CommandLine.arguments.dropFirst().first {
    case "--install": scriptName = "install-helper.sh"
    case "--uninstall": scriptName = "uninstall-helper.sh"
    default: return
    }

    dup2(STDOUT_FILENO, STDERR_FILENO)

    guard setgid(0) == 0, setuid(0) == 0 else {
        print(L(.installerNeedsRoot, Int(geteuid()), Int(getuid()))
              + L(.installerUseTerminal, scriptName))
        exit(1)
    }

    let executable = URL(fileURLWithPath: Bundle.main.executablePath
                                          ?? CommandLine.arguments[0])
    let script = executable.resolvingSymlinksInPath().deletingLastPathComponent()
        .appendingPathComponent(scriptName).path
    guard FileManager.default.isExecutableFile(atPath: script) else {
        print(L(.installerScriptMissing, script))
        exit(1)
    }

    var argv: [UnsafeMutablePointer<CChar>?] = [strdup("/bin/bash"), strdup(script), nil]
    execv("/bin/bash", &argv)
    perror("execv /bin/bash")
    exit(1)
}
