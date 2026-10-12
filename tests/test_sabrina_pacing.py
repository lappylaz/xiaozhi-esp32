"""Exercise the production worker against a timed, bounded speaker model."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_sabrina_interrupt import method

ROOT = Path(__file__).resolve().parents[1]


class PacingTests(unittest.TestCase):
    def test_delayed_chunks_do_not_starve_playback(self):
        source = (ROOT / "main/protocols/sabrina_protocol.cc").read_text()
        # The first definition is the Sabrina capacity (or the shared original).
        header = (ROOT / "main/audio/audio_service.h").read_text()
        capacity = re.search(r"#define MAX_PLAYBACK_TASKS_IN_QUEUE (\d+)", header).group(1)
        harness = (ROOT / "tests/sabrina/interrupt_harness.cc").read_text().split("int main() {")[0]
        model, main = (ROOT / "tests/sabrina/pacing_harness.cc").read_text().split("// TIMED_MAIN")
        start, end = harness.index("struct Audio {"), harness.index("class SabrinaProtocol")
        harness = harness[:start] + model.replace("PLAYBACK_QUEUE_CAPACITY", capacity) + harness[end:]
        harness = harness.replace("// PRODUCTION_METHODS", "\n".join(
            [method(source, name) for name in ["Cancel", "Run", "Connect", "PollTranscription",
                                              "ProcessMessage", "PumpPlayback", "AcknowledgeAudio"]]
            + [method(source, "CloseSocket").replace("::CloseSocket()", "::ResetTransport()")]
        )) + main
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            (path / "test.cc").write_text(harness)
            subprocess.run(["clang++", "-std=c++17", "-fsanitize=address,undefined", "-g",
                            "-I" + str(ROOT / "main/protocols"), str(path / "test.cc"),
                            "-o", str(path / "test")], check=True, capture_output=True, text=True)
            result = subprocess.run([str(path / "test")], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
