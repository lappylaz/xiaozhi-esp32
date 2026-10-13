import importlib.util
import json
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location(
    "provision_sabrina", Path(__file__).parents[1] / "tools" / "provision_sabrina.py"
)
module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(module)


class FakePort:
    def __init__(self, lines):
        self.lines = iter(lines)
        self.writes = []

    def read_until(self, expected, size):
        assert expected == b"\n" and size == 4097
        return next(self.lines, b"")[:size]

    def write(self, data):
        self.writes.append(data)
        return len(data)

    def flush(self):
        pass


class ProvisionTests(unittest.TestCase):
    def test_refuses_insecure_or_conflicting_endpoint(self):
        for url, stt in [
            ("ws://api.example.com/ws/voice", "https://api.example.com/api/stt"),
            ("wss://api.example.com/ws/voice", "https://other.example.com/api/stt"),
            ("wss://user@api.example.com/ws/voice", "https://api.example.com/api/stt"),
            ("wss://api.example.com/ws/voice?token=x", "https://api.example.com/api/stt"),
        ]:
            with self.assertRaises(ValueError):
                module.make_request(url, stt, "sb_voice_" + "a" * 43, "1" * 32)

    def test_refuses_global_or_malformed_keys_without_echo(self):
        for key in ["secret-admin-key", "sb_voice_abc", "sb_voice_" + "x" * 42 + "\n"]:
            with self.assertRaises(ValueError) as exc:
                module.make_request("wss://api.example.com/ws/voice", "https://api.example.com/api/stt", key, "1" * 32)
            self.assertNotIn(key, str(exc.exception))

    def test_requires_matching_ack_and_preserves_secret_only_in_wire_payload(self):
        req = module.make_request("wss://api.example.com/ws/voice", "https://api.example.com/api/stt", "sb_voice_" + "a" * 43, "1" * 32)
        port = FakePort([
            b"SABRINA_PROVISION_READY v1\n",
            b'SABRINA_PROVISION_RESULT {"request_id":"2222","status":"ok"}\n',
            b'SABRINA_PROVISION_RESULT {"request_id":"11111111111111111111111111111111","status":"ok","code":"stored"}\n',
        ])
        result = module.provision(port, req, timeout=0.2)
        self.assertEqual(result, {"status": "ok", "code": "stored"})
        self.assertEqual(json.loads(port.writes[0]), req)
        self.assertNotIn("device_key", result)

    def test_device_error_does_not_echo_untrusted_payload(self):
        req = module.make_request("wss://api.example.com/ws/voice", "https://api.example.com/api/stt", "sb_voice_" + "a" * 43, "1" * 32)
        port = FakePort([b"SABRINA_PROVISION_READY v1\n", b'SABRINA_PROVISION_RESULT {"request_id":"11111111111111111111111111111111","status":"error","code":"secret text"}\n'])
        with self.assertRaises(RuntimeError) as exc:
            module.provision(port, req, timeout=0.2)
        self.assertNotIn("secret text", str(exc.exception))

    def test_missing_physical_gate_does_not_send_secret(self):
        req = module.make_request("wss://api.example.com/ws/voice", "https://api.example.com/api/stt", "sb_voice_" + "a" * 43, "1" * 32)
        port = FakePort([b"ordinary boot log\n"])
        with self.assertRaises(TimeoutError):
            module.provision(port, req, timeout=0.01)
        self.assertEqual(port.writes, [])


if __name__ == "__main__":
    unittest.main()
