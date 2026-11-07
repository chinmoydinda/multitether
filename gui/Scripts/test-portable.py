#!/usr/bin/env python3
"""Execute lifecycle/workflow/localization assertions when CLT has no XCTest.

This adapts the same test methods to preconditions. It does not claim that the
full XCTest suite or hardware-dependent tests passed.
"""
from pathlib import Path
import os
import re
import subprocess

root = Path(__file__).resolve().parents[2]
target = root / '.test-build'
target.mkdir(exist_ok=True)
code = '''import Foundation
func expectEqual<T: Equatable>(_ lhs: @autoclosure () -> T, _ rhs: @autoclosure () -> T, _ message: String = "", file: StaticString = #file, line: UInt = #line) {
    precondition(lhs() == rhs(), "Equality failed: " + message, file: file, line: line)
}
func expectTrue(_ value: @autoclosure () -> Bool, _ message: String = "", file: StaticString = #file, line: UInt = #line) {
    precondition(value(), "Expected true: " + message, file: file, line: line)
}
func expectFalse(_ value: @autoclosure () -> Bool, _ message: String = "", file: StaticString = #file, line: UInt = #line) {
    precondition(!value(), "Expected false: " + message, file: file, line: line)
}
func expectThrows<T>(_ value: @autoclosure () throws -> T, file: StaticString = #file, line: UInt = #line) {
    do { _ = try value() } catch { return }
    preconditionFailure("Expected an error", file: file, line: line)
}
func fail(_ message: String, file: StaticString = #file, line: UInt = #line) -> Never {
    preconditionFailure(message, file: file, line: line)
}
'''
calls = []
count = 0
for i, name in enumerate(['HelperStartupTests', 'HelperConstantsTests', 'DeviceSessionsTests', 'EnglishStringsTests', 'PhoneConnectionWorkflowTests', 'NetworkOverlapTests', 'PinnedClientRequirementTests', 'UsableNetworkTests']):
    text = (root / f'gui/Tests/MultiTetherIPCTests/{name}.swift').read_text()
    text = text.replace('import XCTest\n', '').replace('@testable import MultiTetherIPC\n', '')
    text = text.replace(': XCTestCase {', ' {')
    for old, new in [('XCTAssertEqual', 'expectEqual'), ('XCTAssertThrowsError', 'expectThrows'),
                     ('XCTAssertTrue', 'expectTrue'), ('XCTAssertFalse', 'expectFalse'), ('XCTFail', 'fail')]:
        text = text.replace(old, new)
    code += text + '\n'
    calls.append(f'let tests{i} = {name}()')
    for method, asynchronous, throwing in re.findall(r'func (test\w+)\(\)( async)?( throws)?', text):
        prefix = ('try ' if throwing else '') + ('await ' if asynchronous else '')
        calls.extend([f'{prefix}tests{i}.{method}()', f'print("PASS {name}.{method}")'])
        count += 1
code += '\n@main struct PortableRunner { static func main() async throws {\n'
code += '\n'.join(calls) + f'\nprint("{count} portable regression cases passed")\n' + '} }\n'
(target / 'Runner.swift').write_text(code)
sdk = os.environ.get('MULTITETHER_SDK') or subprocess.check_output(['xcrun', '--sdk', 'macosx', '--show-sdk-path'], text=True).strip()
args = ['swiftc', '-parse-as-library', '-sdk', sdk, '-module-cache-path', str(target / 'cache')]
args += list(map(str, (root / 'gui/Sources/MultiTetherIPC').glob('*.swift')))
args += [str(target / 'Runner.swift'), '-o', str(target / 'run')]
subprocess.run(args, check=True)
subprocess.run([str(target / 'run')], check=True)
