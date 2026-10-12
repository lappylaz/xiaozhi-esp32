// Host boundary harness. The test runner inserts the real protocol worker and
// cancellation methods below; audio/transport delays are controlled here.
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>
#include "sabrina_audio_buffer.h"

constexpr int64_t kSecond = 1000000;
static int64_t clock_us = 1000000;
int64_t esp_timer_get_time() { return clock_us; }
#define pdMS_TO_TICKS(n) (n)
#define ESP_LOGI(...) ((void)0)
static std::function<void()> tick;
void vTaskDelay(int) {
    clock_us += 10000;
    tick();
}
using esp_websocket_client_handle_t = int*;
struct esp_websocket_client_config_t {
    const char* uri;
    const char* headers;
    void (*crt_bundle_attach)();
    bool disable_auto_reconnect;
    int network_timeout_ms, buffer_size, task_stack, ping_interval_sec;
};
constexpr int ESP_OK = 0, SNTP_OPMODE_POLL = 0, WEBSOCKET_EVENT_ANY = 0;
void esp_crt_bundle_attach() {}
bool esp_sntp_enabled() { return true; }
void esp_sntp_setoperatingmode(int) {}
void esp_sntp_setservername(int, const char*) {}
void esp_sntp_init() {}
int transport;
int* esp_websocket_client_init(esp_websocket_client_config_t*) { return &transport; }
int esp_websocket_register_events(int*, int, void (*)(), void*) { return ESP_OK; }
int esp_websocket_client_start(int*) { return ESP_OK; }
struct cJSON {
    std::string type = "connected", id = "session-1";
    int valueint = 0;
    double valuedouble = 0;
};
cJSON* cJSON_ParseWithLength(const char*, size_t) { return new cJSON; }
cJSON* cJSON_CreateObject() { return new cJSON; }
void cJSON_AddStringToObject(cJSON*, const char*, const char*) {}
char* cJSON_PrintUnformatted(cJSON*) { return strdup("transcript"); }
void cJSON_free(void* p) { free(p); }
void cJSON_Delete(cJSON* p) { delete p; }
const cJSON* cJSON_GetObjectItemCaseSensitive(cJSON*, const char*) { return nullptr; }
bool cJSON_IsNumber(const cJSON*) { return false; }
std::string StringField(const cJSON* p, const char* key) {
    return std::string(key) == "type" ? p->type : p->id;
}
bool SafeSessionId(const std::string& s) { return !s.empty(); }
struct Settings {
    Settings(const char*, bool) {}
    void SetString(const char*, const std::string&) {}
};
struct Audio {
    bool capturing = false;
    void StopPcmCaptureAndFlush() { capturing = false; }
    void ResetDecoder() {}
    uint32_t PlaybackGeneration() { return 0; }
    bool IsPlaybackIdle() { return true; }
};
class SabrinaProtocol {
public:
    enum class State { Offline, Ready, Recording, Transcribing, Thinking, Speaking, Draining };
    struct Message {
        bool binary;
        std::vector<uint8_t> bytes;
    };
    Audio audio_;
    std::atomic<bool> stopping_{false}, pressed_{false}, press_pending_{false},
        close_pending_{false};
    std::atomic<bool> connected_{false}, disconnected_{false}, ingress_failed_{false},
        capture_failed_{false}, stt_running_{false};
    std::atomic<uint32_t> generation_{0};
    std::mutex recording_mutex_, ingress_mutex_, stt_mutex_;
    bool stt_done_ = false, error_occurred_ = false;
    std::string stt_text_, stt_error_, session_id_, ws_url_, stt_url_, device_key_;
    static void SocketEvent() {}
    uint32_t stt_generation_ = 0, playback_generation_ = 0;
    int last_seq_ = -1, transcripts_sent = 0;
    std::function<void()> on_connected_;
    std::deque<Message> inbox_;
    sabrina::Recording recording_{320000};
    sabrina::ReleaseHangover release_hangover_{240000};
    State state_ = State::Speaking;
    bool expect_binary_ = false, turn_active_ = true, discard_audio_ = false,
         reconnect_requested_ = false;
    int socket_value = 1;
    int* socket_ = &socket_value;
    int64_t state_at_ = clock_us, connection_at_ = clock_us, last_ping_ = clock_us,
            last_pong_ = clock_us;
    size_t playback_offset_ = 0, inbox_bytes_ = 0, received_bytes_ = 0, queued_bytes_ = 0;
    std::vector<uint8_t> playback_;
    bool capture_before_close = false, capture_before_connect = false, error = false;
    int recorded_samples = 0, frozen_samples = 0, iterations = 0;
    void Run();
    void Cancel(bool start_recording = false);
    void SetState(State s) {
        state_ = s;
        state_at_ = clock_us;
    }
    void BeginRecording() {
        audio_.capturing = true;
        recorded_samples = 0;
        SetState(State::Recording);
    }
    void EndRecording() {
        audio_.capturing = false;
        frozen_samples = recorded_samples;
        SetState(State::Transcribing);
    }
    void CloseSocket() {
        capture_before_close = audio_.capturing;
        // Speech starts while the old socket is being torn down. Release before
        // handshake: the pre-fix firmware captures none of this utterance.
        if (audio_.capturing)
            recorded_samples += 8000;
        pressed_ = false;
        socket_ = nullptr;
        connected_ = false;
    }
    bool SendText(const std::string& s) {
        if (s == "transcript")
            ++transcripts_sent;
        return true;
    }
    bool Connect();
    void Fail(const char*) {
        error = true;
        stopping_ = true;
    }
    void PollTranscription();
    void PumpPlayback() {}
    void ProcessMessage(Message&&);
};
// PRODUCTION_METHODS
int main() {
    SabrinaProtocol p;
    p.pressed_ = true;
    p.press_pending_ = true;
    // Worker runs through a release/hangover before a delayed handshake.
    tick = [&] {
        if (++p.iterations >= 35)
            p.stopping_ = true;
    };
    p.Run();
    if (p.frozen_samples != 8000) {
        std::cerr << "FAIL: interruption lost speech during reconnect: capture_before_connect="
                  << p.capture_before_connect << " frozen_samples=" << p.frozen_samples << '\n';
        return 1;
    }
    SabrinaProtocol during_connect;
    during_connect.BeginRecording();
    assert(during_connect.Connect());
    assert(during_connect.state_ == SabrinaProtocol::State::Recording);
    during_connect.state_ = SabrinaProtocol::State::Transcribing;
    assert(during_connect.Connect());
    assert(during_connect.state_ == SabrinaProtocol::State::Transcribing);

    // A completed transcript must survive until authentication is confirmed.
    SabrinaProtocol pending;
    pending.state_ = SabrinaProtocol::State::Transcribing;
    pending.stt_done_ = true;
    pending.stt_text_ = "tell me the time";
    pending.PollTranscription();
    assert(pending.stt_done_ && pending.transcripts_sent == 0);
    pending.ProcessMessage({false, {'{', '}'}});
    assert(pending.state_ == SabrinaProtocol::State::Transcribing);
    pending.PollTranscription();
    assert(!pending.stt_done_ && pending.transcripts_sent == 1);
    assert(pending.state_ == SabrinaProtocol::State::Thinking);
    pending.PollTranscription();
    assert(pending.transcripts_sent == 1);

    // Connected while still holding must not reset the recording to Ready.
    SabrinaProtocol holding;
    holding.BeginRecording();
    holding.ProcessMessage({false, {'{', '}'}});
    assert(holding.state_ == SabrinaProtocol::State::Recording && holding.audio_.capturing);

    // An invalidated STT completion can never enter a subsequent conversation.
    SabrinaProtocol stale;
    stale.connected_ = true;
    stale.stt_done_ = true;
    stale.stt_text_ = "old request";
    ++stale.generation_;
    stale.PollTranscription();
    assert(stale.transcripts_sent == 0);

    // Ordinary cancellation stops capture instead of rearming it.
    holding.Cancel();
    assert(!holding.audio_.capturing && holding.state_ == SabrinaProtocol::State::Offline);
    std::cout << "PASS: interruption capture, delayed handshake, transcript gating, stale result "
                 "and cancel\n";
}
