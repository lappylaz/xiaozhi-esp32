"""Run the production voice-mode worker paths against modeled VAD and transport."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_sabrina_interrupt import method

ROOT = Path(__file__).resolve().parents[1]


class ConversationTests(unittest.TestCase):
    def test_hands_free_voice_mode(self):
        source = (ROOT / 'main/protocols/sabrina_protocol.cc').read_text()
        harness = (ROOT / 'tests/sabrina/interrupt_harness.cc').read_text().split('int main() {')[0]
        names = ['Cancel', 'Connect', 'PollTranscription', 'ProcessMessage', 'PumpPlayback',
                 'AcknowledgeAudio', 'ApplyToggle', 'ServiceListening', 'DiscardRecording']
        # Each test phase re-enters Run. Drop only its shutdown cancel so state
        # carries across phases; every in-loop path stays production code.
        shutdown = '    Cancel();\n    while (stt_running_)'
        run = method(source, 'Run')
        self.assertIn(shutdown, run)
        run = run.replace(shutdown, '    while (stt_running_)')
        harness = harness.replace('// PRODUCTION_METHODS', '\n'.join(
            [run] + [method(source, name) for name in names]
            + [method(source, 'CloseSocket').replace('::CloseSocket()', '::ResetTransport()')]))
        harness += (ROOT / 'tests/sabrina/conversation_harness.cc').read_text()
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
