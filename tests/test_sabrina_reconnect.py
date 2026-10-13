"""Exercise production reconnect worker, cancellation and failure cleanup."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_sabrina_interrupt import method

ROOT = Path(__file__).resolve().parents[1]

class ReconnectTests(unittest.TestCase):
    def test_transport_recovery(self):
        source = (ROOT / 'main/protocols/sabrina_protocol.cc').read_text()
        harness = (ROOT / 'tests/sabrina/interrupt_harness.cc').read_text().split('int main() {')[0]
        # Use production failure and close behavior, not the interruption harness's
        # stop-on-error / simulated button-release boundary.
        a, b = harness.index('    void CloseSocket() {'), harness.index('    bool SendText(')
        harness = harness[:a] + '    void CloseSocket();\n' + harness[b:]
        a, b = harness.index('    void Fail(const char* text) {'), harness.index('    void PollTranscription();')
        harness = harness[:a] + '''    void Fail(const char*);
    void Recover(const char*);
    void SetError(const char* s) { error = true; error_text = s; }
''' + harness[b:]
        harness = harness.replace('    void Recover(const char* text) { Fail(text); }', '    void Recover(const char*);')
        harness = harness.replace('static void SocketEvent() {}', 'static void SocketEvent(void*, int, int32_t, void*);')
        harness = harness.replace('int esp_websocket_register_events(int*, int, void (*)(), void*)', 'int esp_websocket_register_events(int*, int, void (*)(void*, int, int32_t, void*), void*)')
        harness = harness.replace('int transport;', '''using esp_event_base_t = int;
constexpr int WEBSOCKET_EVENT_DISCONNECTED = 1, WEBSOCKET_EVENT_ERROR = 2,
              WEBSOCKET_EVENT_CLOSED = 3, WEBSOCKET_EVENT_DATA = 4;
constexpr size_t kInboxLimit = 1024 * 1024;
#define ESP_LOGE(...) ((void)0)
struct esp_websocket_event_data_t {
    struct { int esp_ws_handshake_status_code = 0, esp_tls_cert_verify_flags = 0; } error_handle;
    int close_status_code = 0, op_code = 0, payload_offset = 0, payload_len = 0, data_len = 0;
    bool fin = true;
    const char* data_ptr = nullptr;
};
std::vector<int64_t> connection_attempts;
std::vector<std::string> connection_urls;
int transport;''')
        harness = harness.replace('int* esp_websocket_client_init(esp_websocket_client_config_t*) { return &transport; }', '''int* esp_websocket_client_init(esp_websocket_client_config_t* config) {
    connection_attempts.push_back(clock_us);
    connection_urls.push_back(config->uri);
    return &transport;
}''')
        names = ['SocketEvent', 'Cancel', 'Run', 'Connect', 'Fail', 'PollTranscription', 'ProcessMessage',
                 'PumpPlayback', 'AcknowledgeAudio', 'CloseSocket']
        if 'void SabrinaProtocol::Recover(' in source:
            names += ['Recover']
        harness = harness.replace('// PRODUCTION_METHODS', '\n'.join(method(source, n) for n in names))
        harness += (ROOT / 'tests/sabrina/reconnect_harness.cc').read_text()
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            (path/'test.cc').write_text(harness)
            build = subprocess.run(['clang++', '-std=c++17', '-fsanitize=address,undefined', '-g',
                '-I'+str(ROOT/'main/protocols'), str(path/'test.cc'), '-o', str(path/'test')], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(path/'test')], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
