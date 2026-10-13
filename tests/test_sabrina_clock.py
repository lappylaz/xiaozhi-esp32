"""Host validation of the AiPi's real timezone and clock formatter."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ClockTests(unittest.TestCase):
    def test_eastern_midnight_and_dst_boundaries(self):
        with tempfile.TemporaryDirectory() as folder:
            executable = Path(folder) / "clock-test"
            subprocess.run([
                "clang++", "-std=c++17", "-fsanitize=address,undefined", "-g",
                "-I" + str(ROOT / "main/boards/xorigin/aipi-lite"),
                str(ROOT / "tests/sabrina/clock_test.cc"), "-o", str(executable),
            ], check=True, capture_output=True, text=True)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
