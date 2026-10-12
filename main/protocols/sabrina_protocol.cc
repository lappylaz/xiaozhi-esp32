#include "sabrina_protocol.h"
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_sntp.h>
#include <esp_timer.h>
#include <algorithm>
#include <cstring>
#include <ctime>
#include "application.h"
#include "audio_service.h"
#include "board.h"
#include "display.h"
#include "sabrina_provisioning.h"
#include "settings.h"

namespace {
constexpr const char* kTag = "SabrinaVoice";
constexpr EventBits_t kWorkerDone = 1;
constexpr size_t kInboxLimit = 1024 * 1024;
constexpr size_t kResponseLimit = 32 * 1024;
constexpr int64_t kSecond = 1000000;
std::string StringField(const cJSON* root, const char* key) {
    const auto* value = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsString(value) ? value->valuestring : "";
}
struct HttpResponse {
    std::string body;
    bool overflow = false;
};
esp_err_t HttpEvent(esp_http_client_event_t* event) {
    auto* response = static_cast<HttpResponse*>(event->user_data);
    if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
        if (static_cast<size_t>(event->data_len) > kResponseLimit - response->body.size()) {
            response->overflow = true;
            return ESP_FAIL;
        }
        // ON_DATA delivers both chunked and Content-Length bodies during perform.
        response->body.append(static_cast<const char*>(event->data), event->data_len);
    }
    return ESP_OK;
}
bool SafeSessionId(const std::string& id) {
    return !id.empty() && id.size() <= 128 &&
           id.find_first_not_of(
               "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") ==
               std::string::npos;
}
}  // namespace

SabrinaProtocol::SabrinaProtocol(AudioService& audio)
    : audio_(audio), events_(xEventGroupCreate()) {}

SabrinaProtocol::~SabrinaProtocol() {
    stopping_ = true;
    pressed_ = false;
    if (worker_)
        xEventGroupWaitBits(events_, kWorkerDone, pdFALSE, pdTRUE, portMAX_DELAY);
    audio_.SetPcmCaptureCallback({});
    vEventGroupDelete(events_);
}

bool SabrinaProtocol::Start() {
    Settings settings("sabrina");
    if (!LoadSabrinaProvisionedConfig(ws_url_, stt_url_, device_key_)) {
        SetError(
            "Provision this AiPi with its scoped Sabrina voice device key and HTTPS/WSS "
            "endpoints.");
        return false;
    }
    session_id_ = settings.GetString("session_id");
    if (!session_id_.empty() && !SafeSessionId(session_id_))
        session_id_.clear();
    if (Board::GetInstance().GetAudioCodec()->output_sample_rate() != 24000) {
        SetError("Sabrina PCM playback requires the AiPi 24000 Hz codec configuration.");
        return false;
    }
    audio_.SetPcmCaptureCallback([this](const std::vector<int16_t>& pcm) {
        std::lock_guard<std::mutex> lock(recording_mutex_);
        if (!recording_.Append(pcm.data(), pcm.size()))
            capture_failed_ = true;
    });
    if (xTaskCreate(
            [](void* arg) {
                auto* self = static_cast<SabrinaProtocol*>(arg);
                self->Run();
                xEventGroupSetBits(self->events_, kWorkerDone);
                vTaskDelete(nullptr);
            },
            "sabrina_voice", 8192, this, 3, &worker_) != pdPASS) {
        worker_ = nullptr;
        SetError("Not enough memory to start Sabrina voice.");
        return false;
    }
    return true;
}

bool SabrinaProtocol::OpenAudioChannel() {
    press_pending_ = true;
    return true;
}
void SabrinaProtocol::CloseAudioChannel(bool) {
    pressed_ = false;
    close_pending_ = true;
}
void SabrinaProtocol::SendStartListening(ListeningMode) {
    pressed_ = true;
    press_pending_ = true;
}
void SabrinaProtocol::SendStopListening() { pressed_ = false; }
void SabrinaProtocol::SendAbortSpeaking(AbortReason) {
    pressed_ = false;
    close_pending_ = true;
}

void SabrinaProtocol::SetState(State state) {
    state_ = state;
    state_at_ = esp_timer_get_time();
    const char* status = "Connecting to Sabrina";
    DeviceState device_state = kDeviceStateConnecting;
    switch (state) {
        case State::Offline:
            break;
        case State::Ready:
            status = "Hold button to talk";
            device_state = kDeviceStateIdle;
            break;
        case State::Recording:
            status = "Listening";
            device_state = kDeviceStateListening;
            break;
        case State::Transcribing:
            status = "Transcribing";
            device_state = kDeviceStateIdle;
            break;
        case State::Thinking:
            status = "Sabrina is thinking";
            device_state = kDeviceStateIdle;
            break;
        case State::Speaking:
            status = "Sabrina is speaking";
            device_state = kDeviceStateSpeaking;
            break;
        case State::Draining:
            status = "Sabrina is speaking";
            device_state = kDeviceStateSpeaking;
            break;
    }
    Application::GetInstance().Schedule([status, device_state]() {
        auto& app = Application::GetInstance();
        // The upstream enum has no thinking/draining state. The protocol owns
        // those states; this enum is only a status/LED projection for Sabrina.
        app.SetDeviceState(kDeviceStateIdle);
        app.SetDeviceState(device_state);
        auto& board = Board::GetInstance();
        board.SetPowerSaveLevel(device_state == kDeviceStateConnecting
                                    ? PowerSaveLevel::LOW_POWER
                                    : PowerSaveLevel::PERFORMANCE);
        board.GetDisplay()->SetStatus(status);
    });
}

void SabrinaProtocol::SocketEvent(void* arg, esp_event_base_t, int32_t id, void* data) {
    auto* self = static_cast<SabrinaProtocol*>(arg);
    if (id == WEBSOCKET_EVENT_DISCONNECTED || id == WEBSOCKET_EVENT_ERROR) {
        self->disconnected_ = true;
        return;
    }
    if (id != WEBSOCKET_EVENT_DATA || self->ingress_failed_)
        return;
    auto* event = static_cast<esp_websocket_event_data_t*>(data);
    if (event->op_code >= 8)
        return;
    std::lock_guard<std::mutex> lock(self->ingress_mutex_);
    bool complete = false;
    if (event->payload_offset < 0 || event->payload_len < 0 || event->data_len < 0 ||
        event->payload_offset > event->payload_len ||
        !self->assembler_.Add(event->op_code, event->fin, event->payload_len, event->payload_offset,
                              reinterpret_cast<const uint8_t*>(event->data_ptr), event->data_len,
                              complete)) {
        ESP_LOGE(kTag,
                 "Ingress rejected: reason=%s opcode=%d frame_bytes=%d offset=%d event_bytes=%d "
                 "assembled_bytes=%u",
                 event->payload_offset < 0 || event->payload_len < 0 || event->data_len < 0 ||
                         event->payload_offset > event->payload_len
                     ? "invalid_frame_fields"
                     : self->assembler_.failure_reason(),
                 event->op_code, event->payload_len, event->payload_offset, event->data_len,
                 unsigned(self->assembler_.size()));
        self->ingress_failed_ = true;
        return;
    }
    if (!complete)
        return;
    auto bytes = self->assembler_.Take();
    if (bytes.size() > kInboxLimit - self->inbox_bytes_ || self->inbox_.size() >= 128) {
        ESP_LOGE(
            kTag,
            "Ingress rejected: reason=%s message_bytes=%u inbox_bytes=%u inbox_count=%u "
            "frame_bytes=%d offset=%d",
            bytes.size() > kInboxLimit - self->inbox_bytes_ ? "inbox_byte_cap" : "inbox_count_cap",
            unsigned(bytes.size()), unsigned(self->inbox_bytes_), unsigned(self->inbox_.size()),
            event->payload_len, event->payload_offset);
        self->ingress_failed_ = true;
        return;
    }
    self->inbox_bytes_ += bytes.size();
    self->inbox_.push_back({self->assembler_.binary(), std::move(bytes)});
}

bool SabrinaProtocol::Connect() {
    // A button press owns the microphone while transport reconnects.
    if (state_ != State::Recording && state_ != State::Transcribing)
        SetState(State::Offline);
    // Certificate dates are validated. An unset clock is a connection failure,
    // never a reason to disable peer or hostname verification.
    if (time(nullptr) < 1735689600) {
        if (!esp_sntp_enabled()) {
            esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, "pool.ntp.org");
            esp_sntp_init();
        }
        for (int i = 0; i < 150 && !stopping_ && time(nullptr) < 1735689600; ++i)
            vTaskDelay(pdMS_TO_TICKS(100));
        if (time(nullptr) < 1735689600) {
            Fail("Clock sync failed. Check Wi-Fi and NTP access, then press again.");
            return false;
        }
    }
    if (stopping_)
        return false;
    std::string url = ws_url_ + "?audio_format=pcm_24000&audio_flow=ack_v1";
    if (!session_id_.empty())
        url += "&session_id=" + session_id_;
    std::string headers = "X-API-Key: " + device_key_ +
                          "\r\nX-Client-Type: aipi-lite\r\nX-Client-Version: sabrina-direct-1\r\n";
    esp_websocket_client_config_t config = {};
    config.uri = url.c_str();
    config.headers = headers.c_str();
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.disable_auto_reconnect = true;
    config.network_timeout_ms = 10000;
    config.buffer_size = 4096;
    config.task_stack = 6144;
    config.ping_interval_sec = 15;
    disconnected_ = false;
    socket_ = esp_websocket_client_init(&config);
    if (!socket_ ||
        esp_websocket_register_events(socket_, WEBSOCKET_EVENT_ANY, SocketEvent, this) != ESP_OK ||
        esp_websocket_client_start(socket_) != ESP_OK) {
        Fail("Cannot start the secure Sabrina connection.");
        return false;
    }
    connection_at_ = esp_timer_get_time();
    return true;
}

void SabrinaProtocol::CloseSocket() {
    connected_ = false;
    if (socket_) {
        esp_websocket_client_stop(socket_);
        esp_websocket_client_destroy(socket_);
        socket_ = nullptr;
    }
    std::lock_guard<std::mutex> lock(ingress_mutex_);
    inbox_.clear();
    inbox_bytes_ = 0;
    assembler_.Reset();
    ingress_failed_ = false;
    disconnected_ = false;
    last_chunk_id_ = pending_chunk_id_ = 0;
    expect_binary_ = false;
}

bool SabrinaProtocol::SendText(const std::string& text) {
    return socket_ && esp_websocket_client_is_connected(socket_) &&
           esp_websocket_client_send_text(socket_, text.data(), text.size(), pdMS_TO_TICKS(1000)) ==
               static_cast<int>(text.size());
}

void SabrinaProtocol::Cancel(bool start_recording) {
    ++generation_;
    audio_.StopPcmCaptureAndFlush();
    {
        std::lock_guard<std::mutex> lock(recording_mutex_);
        recording_.Freeze();
    }
    audio_.ResetDecoder();
    playback_.clear();
    playback_offset_ = 0;
    expect_binary_ = false;
    turn_active_ = false;
    discard_audio_ = false;
    // Start capture before either the interrupt send or socket teardown, both
    // of which can block. Network readiness must not consume the user's speech.
    if (start_recording && pressed_ && !stt_running_)
        BeginRecording();
    SendText("{\"type\":\"interrupt\"}");
    // PCM frames lack a turn identifier. A new transport is the isolation
    // boundary; retain only the server-issued conversation ID.
    CloseSocket();
    if (state_ != State::Recording || !start_recording)
        SetState(State::Offline);
}

void SabrinaProtocol::Fail(const char* message) {
    Cancel();
    reconnect_requested_ = false;
    pressed_ = false;
    SetError(message);
}

void SabrinaProtocol::BeginRecording() {
    if (stt_running_)
        return;
    if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) < 16000 * 20 * sizeof(int16_t)) {
        Fail("Not enough contiguous memory to record. Restart the device and retry.");
        return;
    }
    audio_.ResetDecoder();
    audio_.EnableWakeWordDetection(false);
    capture_failed_ = false;
    {
        std::lock_guard<std::mutex> lock(recording_mutex_);
        recording_.Begin();
    }
    release_hangover_.Reset();
    last_seq_ = -1;
    received_bytes_ = queued_bytes_ = 0;
    audio_.EnableVoiceProcessing(true);
    if (!audio_.IsAudioProcessorRunning()) {
        Fail("Microphone initialization failed.");
        return;
    }
    SetState(State::Recording);
    ESP_LOGI(kTag, "Capture started; session_ready=%d", connected_.load());
}

void SabrinaProtocol::EndRecording() {
    audio_.StopPcmCaptureAndFlush();
    {
        std::lock_guard<std::mutex> lock(recording_mutex_);
        upload_ = recording_.Freeze();
    }
    if (capture_failed_) {
        upload_.clear();
        Fail("Recording limit reached. Hold for at most 20 seconds and try again.");
        return;
    }
    ESP_LOGI(kTag, "Capture frozen: samples=%u bytes=%u", unsigned(upload_.size()),
             unsigned(upload_.size() * 2));
    if (upload_.size() < 1600) {
        upload_.clear();
        SetState(State::Ready);
        return;
    }
    SetState(State::Transcribing);
    stt_generation_ = generation_;
    stt_running_ = true;
    {
        std::lock_guard<std::mutex> lock(stt_mutex_);
        stt_done_ = false;
        stt_text_.clear();
        stt_error_.clear();
    }
    if (xTaskCreate(
            [](void* arg) {
                auto* self = static_cast<SabrinaProtocol*>(arg);
                self->Transcribe();
                self->stt_running_ = false;
                vTaskDelete(nullptr);
            },
            "sabrina_stt", 6144, this, 2, nullptr) != pdPASS) {
        stt_running_ = false;
        upload_.clear();
        Fail("Not enough memory for transcription.");
    }
}

void SabrinaProtocol::Transcribe() {
    // The STT task exclusively owns this buffer until it reports completion.
    auto pcm = std::move(upload_);
    HttpResponse response;
    std::string url = stt_url_ + "?sample_rate=16000";
    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.timeout_ms = 20000;
    config.event_handler = HttpEvent;
    config.user_data = &response;
    config.disable_auto_redirect = true;
    auto client = esp_http_client_init(&config);
    std::string transcript, error;
    if (!client) {
        error = "Cannot initialize transcription request.";
    } else {
        esp_http_client_set_method(client, HTTP_METHOD_POST);
        esp_http_client_set_header(client, "X-API-Key", device_key_.c_str());
        esp_http_client_set_header(client, "Content-Type", "application/octet-stream");
        esp_http_client_set_post_field(client, reinterpret_cast<const char*>(pcm.data()),
                                       pcm.size() * sizeof(int16_t));
        auto result = esp_http_client_perform(client);
        int status = esp_http_client_get_status_code(client);
        if (status == 401 || status == 403)
            error = "Voice device key rejected. Re-provision or renew this AiPi key.";
        else if (result != ESP_OK || response.overflow ||
                 !esp_http_client_is_complete_data_received(client))
            error = "Transcription transfer failed or response was incomplete. Press to retry.";
        else if (status != 200)
            error = "Transcription service rejected this recording. Press to retry.";
        else {
            auto* root = cJSON_ParseWithLength(response.body.data(), response.body.size());
            transcript = StringField(root, "text");
            if (transcript.empty())
                error = "No speech recognized. Hold the button and try again.";
            cJSON_Delete(root);
        }
        esp_http_client_cleanup(client);
    }
    std::lock_guard<std::mutex> lock(stt_mutex_);
    stt_text_ = std::move(transcript);
    stt_error_ = std::move(error);
    stt_done_ = true;
}

void SabrinaProtocol::PollTranscription() {
    // Keep the completed result until the new authenticated session is ready.
    if (!connected_)
        return;
    std::string text, error;
    {
        std::lock_guard<std::mutex> lock(stt_mutex_);
        if (!stt_done_ || stt_running_)
            return;
        stt_done_ = false;
        if (stt_generation_ != generation_)
            return;
        text = std::move(stt_text_);
        error = std::move(stt_error_);
    }
    if (!error.empty()) {
        Fail(error.c_str());
        return;
    }
    auto* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", "message");
    cJSON_AddStringToObject(root, "text", text.c_str());
    cJSON_AddStringToObject(root, "session_id", session_id_.c_str());
    char* serialized = cJSON_PrintUnformatted(root);
    bool sent = serialized && SendText(serialized);
    cJSON_free(serialized);
    cJSON_Delete(root);
    if (!sent) {
        Fail("Connection lost before sending your transcript. Press to retry.");
        return;
    }
    turn_active_ = true;
    SetState(State::Thinking);
}

void SabrinaProtocol::ProcessMessage(Message&& message) {
    if (message.binary) {
        if (!connected_ || !expect_binary_ || pending_chunk_id_ == 0 || !playback_.empty() ||
            message.bytes.empty() || message.bytes.size() > kAudioChunkBytes ||
            message.bytes.size() % 2 != 0) {
            Fail("Invalid or unpaired Sabrina PCM audio.");
            return;
        }
        expect_binary_ = false;
        if (discard_audio_) {
            ESP_LOGI(kTag, "User capture superseded unsolicited audio: discarded_bytes=%u",
                     unsigned(message.bytes.size()));
            discard_audio_ = false;
            AcknowledgeAudio();
            return;
        }
        received_bytes_ += message.bytes.size();
        playback_ = std::move(message.bytes);
        playback_offset_ = 0;
        playback_generation_ = audio_.PlaybackGeneration();
        SetState(State::Speaking);
        return;
    }
    auto* root = cJSON_ParseWithLength(reinterpret_cast<const char*>(message.bytes.data()),
                                       message.bytes.size());
    if (!root) {
        Fail("Invalid Sabrina protocol JSON.");
        return;
    }
    auto type = StringField(root, "type");
    if (expect_binary_) {
        cJSON_Delete(root);
        Fail("Missing Sabrina audio payload.");
        return;
    }
    if (type.empty()) {
        cJSON_Delete(root);
        Fail("Sabrina message has no type.");
        return;
    }
    if (type == "connected") {
        if (connected_) {
            cJSON_Delete(root);
            Fail("Unexpected Sabrina session change.");
            return;
        }
        const auto* chunk_bytes = cJSON_GetObjectItemCaseSensitive(root, "audio_chunk_bytes");
        if (StringField(root, "audio_flow") != "ack_v1" || !cJSON_IsNumber(chunk_bytes) ||
            chunk_bytes->valuedouble != kAudioChunkBytes) {
            cJSON_Delete(root);
            Fail(
                "Sabrina backend lacks bounded audio flow. Deploy ack_v1 support before retrying.");
            return;
        }
        auto id = StringField(root, "session_id");
        if (!SafeSessionId(id)) {
            cJSON_Delete(root);
            Fail("Sabrina returned an invalid session ID.");
            return;
        }
        if (id != session_id_) {
            session_id_ = id;
            Settings("sabrina", true).SetString("session_id", id);
        }
        connected_ = true;
        last_pong_ = last_ping_ = esp_timer_get_time();
        error_occurred_ = false;
        if (on_connected_)
            on_connected_();
        if (state_ != State::Recording && state_ != State::Transcribing)
            SetState(State::Ready);
        ESP_LOGI(kTag, "Session ready; recording=%d transcribing=%d", state_ == State::Recording,
                 state_ == State::Transcribing);
    } else if (type == "pong") {
        last_pong_ = esp_timer_get_time();
    } else if (type == "audio" || type == "sentence") {
        const auto* seq = cJSON_GetObjectItemCaseSensitive(root, "seq");
        if (seq && (!cJSON_IsNumber(seq) || seq->valuedouble != seq->valueint ||
                    seq->valueint <= last_seq_)) {
            cJSON_Delete(root);
            Fail("Sabrina audio sequence is invalid.");
            return;
        }
        if (seq)
            last_seq_ = seq->valueint;
        if (type == "audio") {
            const auto* chunk_id = cJSON_GetObjectItemCaseSensitive(root, "chunk_id");
            if (!connected_ || pending_chunk_id_ != 0 || !playback_.empty() ||
                last_chunk_id_ == INT32_MAX || !cJSON_IsNumber(chunk_id) ||
                chunk_id->valuedouble != static_cast<double>(last_chunk_id_) + 1 ||
                StringField(root, "format") != "pcm_24000") {
                cJSON_Delete(root);
                Fail("Unexpected Sabrina audio format or turn state.");
                return;
            }
            // The server can offer followup speech immediately after connected.
            // A physical press takes precedence; consume the paired body without
            // playing it over the user's microphone recording.
            discard_audio_ =
                !turn_active_ && (state_ == State::Recording || state_ == State::Transcribing);
            pending_chunk_id_ = chunk_id->valueint;
            expect_binary_ = true;
        }
    } else if (type == "done") {
        if (!turn_active_) {
            cJSON_Delete(root);
            Fail("Unexpected Sabrina turn completion.");
            return;
        }
        turn_active_ = false;
        SetState(State::Draining);
    } else if (type == "interrupted") {
        Cancel();
        reconnect_requested_ = true;
    } else if (type == "error") {
        cJSON_Delete(root);
        Fail("Sabrina could not complete this turn. Press to reconnect and retry.");
        return;
    }
    // Text/tool status/followup events are deliberately not rendered on this
    // voice-only surface; audio and spoken confirmations use the same session.
    cJSON_Delete(root);
}

bool SabrinaProtocol::AcknowledgeAudio() {
    if (pending_chunk_id_ == 0 || !SendText("{\"type\":\"audio_ack\",\"chunk_id\":" +
                                            std::to_string(pending_chunk_id_) + "}")) {
        Fail("Sabrina audio acknowledgement failed. Press to reconnect.");
        return false;
    }
    last_chunk_id_ = pending_chunk_id_;
    pending_chunk_id_ = 0;
    return true;
}

void SabrinaProtocol::PumpPlayback() {
    // Prime the bounded queue promptly, rather than waiting another worker tick
    // per block. At most ten 20 ms blocks belong to the one 9,600-byte chunk;
    // queue backpressure returns immediately so controls remain serviceable.
    while (playback_offset_ < playback_.size()) {
        size_t bytes = std::min<size_t>(960, playback_.size() - playback_offset_);
        // vector<uint8_t> storage is not specified to be aligned for int16_t.
        int16_t pcm[480];
        memcpy(pcm, playback_.data() + playback_offset_, bytes);
        if (!audio_.PushPcmPlayback(pcm, bytes / 2, playback_generation_))
            return;
        playback_offset_ += bytes;
        queued_bytes_ += bytes;
        if (playback_offset_ == playback_.size()) {
            playback_.clear();
            playback_offset_ = 0;
            // The bounded playback queue owns every sample before granting the
            // server permission to send the next chunk. Never ACK on receipt.
            AcknowledgeAudio();
            return;
        }
    }
}

void SabrinaProtocol::Run() {
    while (!stopping_) {
        if (close_pending_.exchange(false)) {
            Cancel();
            reconnect_requested_ = false;
        }
        if (press_pending_.exchange(false)) {
            // Even a press/release between polls cancels the previous release.
            if (state_ == State::Recording)
                release_hangover_.Reset();
            if (state_ != State::Ready && state_ != State::Recording && socket_)
                Cancel(true);
            // Also capture a retry from Offline before starting TLS. A release
            // during the handshake still leaves a complete local recording.
            if (state_ != State::Recording && pressed_ && !stt_running_)
                BeginRecording();
            reconnect_requested_ = true;
        }
        if (!socket_ && reconnect_requested_) {
            reconnect_requested_ = false;
            Connect();
        }
        if (ingress_failed_.exchange(false))
            Fail("Sabrina audio exceeded the safe buffer or frame limit. Press to retry.");
        if (disconnected_.exchange(false))
            Fail("Sabrina connection closed. Check Wi-Fi and device key, then press again.");
        const auto now = esp_timer_get_time();
        if (socket_ && !connected_ && now - connection_at_ > 20 * kSecond)
            Fail("Sabrina did not accept this device. Check provisioning and press again.");
        if (connected_ && now - last_ping_ > 15 * kSecond) {
            last_ping_ = now;
            if (!SendText("{\"type\":\"ping\"}"))
                Fail("Sabrina connection lost. Press to reconnect.");
        }
        if (connected_ && now - last_pong_ > 45 * kSecond)
            Fail("Sabrina heartbeat timed out. Press to reconnect.");
        PollTranscription();
        PumpPlayback();
        // ACK pacing bounds audio independently of reply duration. Keep
        // processing controls/pongs while the playback queue applies pressure.
        {
            Message message;
            bool available = false;
            {
                std::lock_guard<std::mutex> lock(ingress_mutex_);
                if (!inbox_.empty()) {
                    message = std::move(inbox_.front());
                    inbox_.pop_front();
                    inbox_bytes_ -= message.bytes.size();
                    available = true;
                }
            }
            if (available)
                ProcessMessage(std::move(message));
        }
        if (state_ == State::Ready && pressed_ && !stt_running_)
            BeginRecording();
        if (state_ == State::Recording) {
            if (capture_failed_)
                EndRecording();
            // Bounded microphone hangover protects the final syllable. This
            // is explicit additional capture, not a claim of AFE queue drain.
            else if (release_hangover_.ShouldStop(pressed_.load(), now))
                EndRecording();
        }
        bool inbox_empty;
        {
            std::lock_guard<std::mutex> lock(ingress_mutex_);
            inbox_empty = inbox_.empty();
        }
        // Followup offers are unsolicited audio and intentionally have no done
        // event. Their audio still must fully drain before a new recording.
        if ((state_ == State::Draining ||
             (state_ == State::Speaking && !turn_active_ && inbox_empty && !expect_binary_)) &&
            playback_.empty() && audio_.IsPlaybackIdle()) {
            ESP_LOGI(kTag, "Turn audio: received_bytes=%u enqueued_bytes=%u drained=true",
                     unsigned(received_bytes_), unsigned(queued_bytes_));
            if (received_bytes_ == 0)
                Fail("Sabrina returned no spoken audio. Press to retry.");
            else if (received_bytes_ != queued_bytes_)
                Fail("Sabrina audio did not play completely.");
            else
                SetState(State::Ready);
        }
        if ((state_ == State::Thinking || (state_ == State::Speaking && playback_.empty())) &&
            now - state_at_ > 120 * kSecond)
            Fail("Sabrina response timed out. Press to retry.");
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    Cancel();
    while (stt_running_)
        vTaskDelay(pdMS_TO_TICKS(20));
}
