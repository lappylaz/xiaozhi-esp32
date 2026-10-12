// Host boundary harness. The test runner inserts the real protocol worker and
// cancellation methods below; audio/transport delays are controlled here.
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <deque>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <regex>
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
void esp_websocket_client_stop(int*) {}
void esp_websocket_client_destroy(int*) {}
struct cJSON {
    std::map<std::string, cJSON> fields;
    std::string string;
    int valueint = 0;
    double valuedouble = 0;
    bool number = false;
};
cJSON* cJSON_ParseWithLength(const char* data, size_t length) {
    auto* root = new cJSON;
    std::string json(data, length);
    const std::regex field(R"re("([^"]+)"\s*:\s*(?:"([^"]*)"|(-?[0-9]+(?:\.[0-9]+)?)))re");
    for (std::sregex_iterator it(json.begin(), json.end(), field), end; it != end; ++it) {
        auto& item = root->fields[(*it)[1]];
        item.number = (*it)[3].matched;
        if (item.number) {
            item.valuedouble = std::stod((*it)[3]);
            item.valueint = item.valuedouble > INT32_MAX   ? INT32_MAX
                            : item.valuedouble < INT32_MIN ? INT32_MIN
                                                           : static_cast<int>(item.valuedouble);
        } else
            item.string = (*it)[2];
    }
    return root;
}
cJSON* cJSON_CreateObject() { return new cJSON; }
void cJSON_AddStringToObject(cJSON*, const char*, const char*) {}
char* cJSON_PrintUnformatted(cJSON*) { return strdup("transcript"); }
void cJSON_free(void* p) { free(p); }
void cJSON_Delete(cJSON* p) { delete p; }
const cJSON* cJSON_GetObjectItemCaseSensitive(const cJSON* root, const char* key) {
    auto found = root->fields.find(key);
    return found == root->fields.end() ? nullptr : &found->second;
}
bool cJSON_IsNumber(const cJSON* item) { return item && item->number; }
std::string StringField(const cJSON* p, const char* key) {
    auto* item = cJSON_GetObjectItemCaseSensitive(p, key);
    return item ? item->string : "";
}
bool SafeSessionId(const std::string& s) { return !s.empty(); }
struct Settings {
    Settings(const char*, bool) {}
    void SetString(const char*, const std::string&) {}
};
struct Audio {
    bool capturing = false, accept_playback = true;
    size_t played_samples = 0;
    bool PushPcmPlayback(const int16_t*, size_t size, uint32_t) {
        if (!accept_playback)
            return false;
        played_samples += size;
        return true;
    }
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
    static constexpr size_t kAudioChunkBytes = 9600;
    int32_t last_chunk_id_ = 0, pending_chunk_id_ = 0;
    std::vector<std::string> sent;
    std::string error_text;
    std::function<void()> on_connected_;
    std::deque<Message> inbox_;
    sabrina::Recording recording_{320000};
    sabrina::MessageAssembler assembler_{512 * 1024};
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
    bool capture_before_close = false, capture_before_connect = false, error = false,
         send_ok = true;
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
    void ResetTransport();
    void CloseSocket() {
        capture_before_close = audio_.capturing;
        // Speech starts while the old socket is being torn down. Release before
        // handshake: the pre-fix firmware captures none of this utterance.
        if (audio_.capturing)
            recorded_samples += 8000;
        pressed_ = false;
        ResetTransport();
    }
    bool SendText(const std::string& s) {
        sent.push_back(s);
        if (s == "transcript")
            ++transcripts_sent;
        return send_ok;
    }
    bool Connect();
    void Fail(const char* text) {
        error_text = text;
        error = true;
        stopping_ = true;
    }
    void PollTranscription();
    void PumpPlayback();
    bool AcknowledgeAudio();
    void ProcessMessage(Message&&);
};
SabrinaProtocol::Message json(const std::string& s) { return {false, {s.begin(), s.end()}}; }
void connected(SabrinaProtocol& p) {
    p.ProcessMessage(json(
        R"({"type":"connected","session_id":"session-1","audio_flow":"ack_v1","audio_chunk_bytes":9600})"));
}
void audio_chunk(SabrinaProtocol& p, int id, size_t bytes) {
    p.ProcessMessage(json(
        "{\"type\":\"audio\",\"format\":\"pcm_24000\",\"chunk_id\":" + std::to_string(id) + "}"));
    if (!p.error)
        p.ProcessMessage({true, std::vector<uint8_t>(bytes, 0x2a)});
}
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
    connected(pending);
    assert(pending.state_ == SabrinaProtocol::State::Transcribing);
    pending.PollTranscription();
    assert(!pending.stt_done_ && pending.transcripts_sent == 1);
    assert(pending.state_ == SabrinaProtocol::State::Thinking);
    pending.PollTranscription();
    assert(pending.transcripts_sent == 1);

    // Connected while still holding must not reset the recording to Ready.
    SabrinaProtocol holding;
    holding.BeginRecording();
    connected(holding);
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
    // Five minutes, including split original audio events and a partial tail,
    // must use constant memory and ACK only after every queued sample is accepted.
    SabrinaProtocol stream;
    connected(stream);
    constexpr int chunks = 1500;
    for (int id = 1; id <= chunks; ++id) {
        size_t size = id == chunks ? 146 : 9600;
        size_t before = stream.sent.size();
        audio_chunk(stream, id, size);
        assert(!stream.error && stream.playback_.size() == size);
        stream.audio_.accept_playback = false;
        stream.PumpPlayback();
        assert(stream.sent.size() == before && stream.playback_offset_ == 0);
        stream.audio_.accept_playback = true;
        while (!stream.playback_.empty()) {
            stream.PumpPlayback();
            if (!stream.playback_.empty())
                assert(stream.sent.size() == before);
        }
        assert(stream.sent.size() == before + 1);
        assert(stream.sent.back() ==
               "{\"type\":\"audio_ack\",\"chunk_id\":" + std::to_string(id) + "}");
    }
    assert(stream.received_bytes_ == stream.queued_bytes_);
    assert(stream.queued_bytes_ == (chunks - 1) * 9600 + 146);
    // JSON control messages can be serviced while playback is backpressured.
    audio_chunk(stream, chunks + 1, 9600);
    stream.last_pong_ = 0;
    stream.ProcessMessage(json(R"({"type":"pong"})"));
    assert(stream.last_pong_ == clock_us && !stream.error);

    for (size_t invalid_size : {0, 1, 9601, 9602}) {
        SabrinaProtocol invalid;
        connected(invalid);
        audio_chunk(invalid, 1, invalid_size);
        assert(invalid.error && invalid.sent.empty());
    }
    for (const std::string& id : {"0", "-1", "2", "1.5", "2147483648", "\"1\"", "null"}) {
        SabrinaProtocol invalid;
        connected(invalid);
        invalid.ProcessMessage(
            json("{\"type\":\"audio\",\"format\":\"pcm_24000\",\"chunk_id\":" + id + "}"));
        assert(invalid.error);
    }
    SabrinaProtocol duplicate;
    connected(duplicate);
    audio_chunk(duplicate, 1, 2);
    duplicate.PumpPlayback();
    audio_chunk(duplicate, 1, 2);
    assert(duplicate.error);
    SabrinaProtocol too_early;
    connected(too_early);
    audio_chunk(too_early, 1, 9600);
    audio_chunk(too_early, 2, 9600);
    assert(too_early.error);
    SabrinaProtocol controls;
    connected(controls);
    audio_chunk(controls, 1, 9600);
    controls.audio_.accept_playback = false;
    controls.last_pong_ = 0;
    auto pong = json(R"({"type":"pong"})");
    controls.inbox_bytes_ = pong.bytes.size();
    controls.inbox_.push_back(std::move(pong));
    auto pong_time = clock_us;
    tick = [&] { controls.stopping_ = true; };
    controls.Run();
    assert(controls.last_pong_ == pong_time);
    SabrinaProtocol ack_failure;
    connected(ack_failure);
    audio_chunk(ack_failure, 1, 2);
    ack_failure.send_ok = false;
    ack_failure.PumpPlayback();
    assert(ack_failure.error && ack_failure.last_chunk_id_ == 0);
    SabrinaProtocol boundary;
    connected(boundary);
    boundary.last_chunk_id_ = INT32_MAX - 1;
    audio_chunk(boundary, INT32_MAX, 2);
    boundary.PumpPlayback();
    assert(!boundary.error && boundary.last_chunk_id_ == INT32_MAX);
    audio_chunk(boundary, INT32_MAX, 2);
    assert(boundary.error);
    // One original event can span multiple chunks without repeating its seq.
    SabrinaProtocol sequenced;
    connected(sequenced);
    sequenced.ProcessMessage(json(R"({"type":"audio","format":"pcm_24000","seq":0,"chunk_id":1})"));
    sequenced.ProcessMessage({true, std::vector<uint8_t>(9600)});
    while (!sequenced.playback_.empty())
        sequenced.PumpPlayback();
    audio_chunk(sequenced, 2, 146);
    sequenced.PumpPlayback();
    assert(!sequenced.error && sequenced.last_seq_ == 0);
    SabrinaProtocol legacy;
    legacy.ProcessMessage(json(R"({"type":"connected","session_id":"session-1"})"));
    assert(legacy.error && !legacy.connected_);
    for (const std::string& size : {"9602", "9600.5", "\"9600\""}) {
        SabrinaProtocol bad_handshake;
        bad_handshake.ProcessMessage(
            json("{\"type\":\"connected\",\"session_id\":\"session-1\",\"audio_flow\":\"ack_v1\","
                 "\"audio_chunk_bytes\":" +
                 size + "}"));
        assert(bad_handshake.error);
    }
    SabrinaProtocol unsolicited;
    unsolicited.turn_active_ = false;
    unsolicited.BeginRecording();
    connected(unsolicited);
    audio_chunk(unsolicited, 1, 9600);
    assert(!unsolicited.error && unsolicited.playback_.empty());
    assert(unsolicited.audio_.capturing && unsolicited.sent.size() == 1);
    assert(unsolicited.sent.back() == R"({"type":"audio_ack","chunk_id":1})");
    unsolicited.Cancel();
    assert(unsolicited.pending_chunk_id_ == 0 && unsolicited.last_chunk_id_ == 0);
    connected(unsolicited);
    audio_chunk(unsolicited, 1, 2);
    assert(!unsolicited.error);
    std::cout << "PASS: interruption plus bounded five-minute ACK playback, backpressure, "
                 "invalid chunks, handshake, cancellation and discarded followup ACK\n";
}
