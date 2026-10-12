"""Run the production worker/cancel bodies against delayed transport boundaries."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def method(source, name):
    # These top-level method bodies end at an unindented closing brace.
    match = re.search(r"(?:void|bool) SabrinaProtocol::" + name + r"\([^\n]*\) \{.*?^\}", source, re.M | re.S)
    if not match:
        raise AssertionError(f"Production method not found: {name}")
    return match.group(0)


class InterruptionTests(unittest.TestCase):
    def test_capture_during_reconnect(self):
        source = (ROOT / 'main/protocols/sabrina_protocol.cc').read_text()
        harness = (ROOT / 'tests/sabrina/interrupt_harness.cc').read_text()
        cancel = method(source, 'Cancel')
        # Accept the original no-argument signature to demonstrate the regression.
        if 'Cancel()' in cancel:
            harness = harness.replace('void Cancel(bool start_recording = false);', 'void Cancel();')
        harness = harness.replace('// PRODUCTION_METHODS', '\n'.join([cancel] + [method(source, name) for name in ['Run', 'Connect', 'PollTranscription', 'ProcessMessage', 'PumpPlayback', 'AcknowledgeAudio']] + [method(source, 'CloseSocket').replace('::CloseSocket()', '::ResetTransport()')]))
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            (path / 'test.cc').write_text(harness)
            subprocess.run(['clang++', '-std=c++17', '-fsanitize=address,undefined', '-g',
                            '-I' + str(ROOT / 'main/protocols'), str(path / 'test.cc'),
                            '-o', str(path / 'test')], check=True, capture_output=True, text=True)
            result = subprocess.run([str(path / 'test')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
