// SabrinaProtocol — see sabrina_protocol.h for the full design rationale.
//
// Summary: bridge xiaozhi's audio capture pipeline to Sabrina's REST + WS
// voice ingress. Capture audio is Opus-encoded by the xiaozhi audio service;
// we decode it back to PCM, accumulate per-utterance, then HTTP POST to
// /api/stt on stop, then send the resulting text over the WS as
// {"type":"message","text":"..."}. Inbound binary frames on the WS are raw
// PCM 24kHz mono (per ?audio_format=pcm_24000), pushed straight to the
// AudioService playback queue via PushPcmToPlaybackQueue (skipping the
// Opus decoder entirely).

#include "sabrina_protocol.h"

#include "application.h"
#include "audio/audio_service.h"
#include "board.h"
#include "settings.h"
#include "system_info.h"

#include <cJSON.h>
#include <cstring>
#include <esp_audio_dec.h>
#include <esp_audio_types.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_opus_dec.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "Sabrina"

// xiaozhi's audio capture pipeline encodes mic audio as Opus 16 kHz mono,
// 60 ms frames. Match that for our utterance decoder.
static constexpr int kCaptureSampleRate = 16000;
static constexpr int kCaptureFrameDurationMs = 60;
static constexpr size_t kMaxFrameSamples = (kCaptureSampleRate * kCaptureFrameDurationMs) / 1000;
// 30 seconds of 16 kHz int16 mono = 960 000 bytes worst case. PSRAM-backed.
static constexpr size_t kMaxUtteranceSamples = kCaptureSampleRate * 30;

SabrinaProtocol::SabrinaProtocol() {
    esp_opus_dec_cfg_t cfg = {
        .sample_rate    = ESP_AUDIO_SAMPLE_RATE_16K,
        .channel        = ESP_AUDIO_MONO,
        .frame_duration = ESP_OPUS_DEC_FRAME_DURATION_60_MS,
    };
    auto ret = esp_opus_dec_open(&cfg, sizeof(cfg), &utterance_opus_decoder_);
    if (ret != ESP_AUDIO_ERR_OK || utterance_opus_decoder_ == nullptr) {
        ESP_LOGE(TAG, "esp_opus_dec_open failed: %d", ret);
        utterance_opus_decoder_ = nullptr;
    }
}

SabrinaProtocol::~SabrinaProtocol() {
    if (utterance_opus_decoder_ != nullptr) {
        esp_opus_dec_close(utterance_opus_decoder_);
        utterance_opus_decoder_ = nullptr;
    }
    websocket_.reset();
}

bool SabrinaProtocol::Start() {
    return LoadConfig();
}

bool SabrinaProtocol::LoadConfig() {
    Settings settings("sabrina", false);
    ws_url_     = settings.GetString("url");
    stt_url_    = settings.GetString("stt_url");
    jwt_        = settings.GetString("jwt");
    device_key_ = settings.GetString("device_key");

    if (ws_url_.empty()) {
        ESP_LOGE(TAG, "Settings 'sabrina/url' not set");
        return false;
    }
    if (stt_url_.empty()) {
        ESP_LOGW(TAG, "Settings 'sabrina/stt_url' not set; STT will fail");
    }
    if (device_key_.empty()) {
        ESP_LOGW(TAG, "Settings 'sabrina/device_key' not set; STT auth will fail");
    }
    if (jwt_.empty()) {
        ESP_LOGW(TAG, "Settings 'sabrina/jwt' not set; WS auth will fail");
    }
    return true;
}

bool SabrinaProtocol::OpenAudioChannel() {
    if (IsAudioChannelOpened()) {
        return true;
    }
    return ConnectWebSocket();
}

bool SabrinaProtocol::ConnectWebSocket() {
    auto network = Board::GetInstance().GetNetwork();
    websocket_ = network->CreateWebSocket(1);
    if (websocket_ == nullptr) {
        ESP_LOGE(TAG, "CreateWebSocket failed");
        return false;
    }

    error_occurred_ = false;

    // Build the URL: append audio_format=pcm_24000 + (optionally) token=<jwt>.
    std::string full_url = ws_url_;
    full_url += (ws_url_.find('?') == std::string::npos) ? "?" : "&";
    full_url += "audio_format=pcm_24000";
    if (!jwt_.empty()) {
        full_url += "&token=";
        full_url += jwt_;
    }

    websocket_->OnData([this](const char* data, size_t len, bool binary) {
        if (binary) {
            // Sabrina sends raw PCM 24 kHz mono int16 LE binary frames.
            // Push directly to the playback queue, skipping the Opus decoder.
            auto& audio_service = Application::GetInstance().GetAudioService();
            audio_service.PushPcmToPlaybackQueue(
                reinterpret_cast<const uint8_t*>(data), len,
                /*sample_rate=*/24000, /*timestamp=*/0, /*wait=*/false);
        } else {
            // JSON message — parse and forward to Application via on_incoming_json_.
            auto root = cJSON_ParseWithLength(data, len);
            if (root != nullptr && on_incoming_json_ != nullptr) {
                on_incoming_json_(root);
            }
            cJSON_Delete(root);
        }
        last_incoming_time_ = std::chrono::steady_clock::now();
    });

    websocket_->OnDisconnected([this]() {
        ESP_LOGI(TAG, "Sabrina WS disconnected");
        if (on_audio_channel_closed_ != nullptr) {
            on_audio_channel_closed_();
        }
    });

    ESP_LOGI(TAG, "Connecting Sabrina WS: %s", ws_url_.c_str());
    if (!websocket_->Connect(full_url.c_str())) {
        ESP_LOGE(TAG, "Sabrina WS connect failed, code=%d", websocket_->GetLastError());
        SetError("Sabrina WS connect failed");
        return false;
    }

    if (on_audio_channel_opened_ != nullptr) {
        on_audio_channel_opened_();
    }
    return true;
}

void SabrinaProtocol::CloseAudioChannel(bool send_goodbye) {
    (void)send_goodbye;  // Sabrina /ws/voice has no goodbye message.
    websocket_.reset();
}

bool SabrinaProtocol::IsAudioChannelOpened() const {
    return websocket_ != nullptr && websocket_->IsConnected() && !error_occurred_ && !IsTimeout();
}

bool SabrinaProtocol::SendAudio(std::unique_ptr<AudioStreamPacket> packet) {
    // Don't send to network — Sabrina's /ws/voice expects text. Decode the
    // Opus payload back to PCM and accumulate; we'll POST it on stop.
    if (packet == nullptr || packet->payload.empty()) return true;
    DecodeAndAccumulate(packet->payload);
    return true;
}

void SabrinaProtocol::DecodeAndAccumulate(const std::vector<uint8_t>& opus_payload) {
    if (utterance_opus_decoder_ == nullptr) return;
    if (pcm_accumulator_.size() >= kMaxUtteranceSamples) {
        // Hard cap: drop further audio for this utterance.
        return;
    }

    int16_t out_buf[kMaxFrameSamples];
    esp_audio_dec_in_raw_t raw = {
        .buffer        = (uint8_t*)opus_payload.data(),
        .len           = (uint32_t)opus_payload.size(),
        .consumed      = 0,
        .frame_recover = ESP_AUDIO_DEC_RECOVERY_NONE,
    };
    esp_audio_dec_out_frame_t out_frame = {
        .buffer       = (uint8_t*)out_buf,
        .len          = (uint32_t)sizeof(out_buf),
        .decoded_size = 0,
    };
    esp_audio_dec_info_t dec_info = {};
    auto ret = esp_opus_dec_decode(utterance_opus_decoder_, &raw, &out_frame, &dec_info);
    if (ret != ESP_AUDIO_ERR_OK) {
        ESP_LOGW(TAG, "Opus decode failed: %d", ret);
        return;
    }
    size_t sample_count = out_frame.decoded_size / sizeof(int16_t);
    pcm_accumulator_.insert(pcm_accumulator_.end(), out_buf, out_buf + sample_count);
}

bool SabrinaProtocol::SendText(const std::string& text) {
    if (websocket_ == nullptr || !websocket_->IsConnected()) {
        ESP_LOGE(TAG, "SendText: WS not connected");
        return false;
    }
    if (!websocket_->Send(text)) {
        ESP_LOGE(TAG, "SendText failed");
        SetError("WS send failed");
        return false;
    }
    return true;
}

void SabrinaProtocol::SendStartListening(ListeningMode mode) {
    (void)mode;
    pcm_accumulator_.clear();
    pcm_accumulator_.reserve(kCaptureSampleRate);  // ~1 s pre-allocation
    // Pre-warm the WS so the only stop-time latency is the STT round-trip.
    if (!IsAudioChannelOpened()) {
        ConnectWebSocket();
    }
}

void SabrinaProtocol::SendStopListening() {
    // Spawn a FreeRTOS task to POST to /api/stt; don't block the audio task.
    // 8 KB stack is enough for the HTTP client + small JSON parsing.
    auto task_fn = [](void* arg) {
        auto self = static_cast<SabrinaProtocol*>(arg);
        self->RunStt();
        vTaskDelete(NULL);
    };
    BaseType_t ok = xTaskCreate(task_fn, "sabrina_stt", 8192, this,
                                tskIDLE_PRIORITY + 5, nullptr);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Failed to spawn STT task");
    }
}

void SabrinaProtocol::RunStt() {
    if (pcm_accumulator_.empty()) {
        ESP_LOGW(TAG, "RunStt: no audio accumulated; nothing to send");
        return;
    }
    if (stt_url_.empty() || device_key_.empty()) {
        ESP_LOGE(TAG, "RunStt: stt_url or device_key not configured");
        return;
    }

    std::string url = stt_url_;
    url += (stt_url_.find('?') == std::string::npos) ? "?" : "&";
    url += "sample_rate=";
    url += std::to_string(kCaptureSampleRate);

    esp_http_client_config_t cfg = {};
    cfg.url               = url.c_str();
    cfg.method            = HTTP_METHOD_POST;
    cfg.timeout_ms        = 30000;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == nullptr) {
        ESP_LOGE(TAG, "RunStt: esp_http_client_init failed");
        return;
    }

    esp_http_client_set_header(client, "X-API-Key", device_key_.c_str());
    esp_http_client_set_header(client, "Content-Type", "application/octet-stream");

    size_t body_size = pcm_accumulator_.size() * sizeof(int16_t);
    esp_http_client_set_post_field(
        client, reinterpret_cast<const char*>(pcm_accumulator_.data()), body_size);

    auto err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RunStt: perform failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        pcm_accumulator_.clear();
        return;
    }

    int status         = esp_http_client_get_status_code(client);
    int content_length = esp_http_client_get_content_length(client);
    if (status != 200 || content_length <= 0) {
        ESP_LOGE(TAG, "RunStt: HTTP status=%d content_length=%d", status, content_length);
        esp_http_client_cleanup(client);
        pcm_accumulator_.clear();
        return;
    }

    std::string body;
    body.resize(content_length);
    int read_len = esp_http_client_read(client, &body[0], content_length);
    esp_http_client_cleanup(client);
    if (read_len < 0) {
        ESP_LOGE(TAG, "RunStt: read response failed");
        pcm_accumulator_.clear();
        return;
    }
    body.resize(read_len);

    // Parse {"text":"..."}
    auto json = cJSON_Parse(body.c_str());
    std::string text;
    if (json != nullptr) {
        auto text_item = cJSON_GetObjectItem(json, "text");
        if (cJSON_IsString(text_item) && text_item->valuestring != nullptr) {
            text = text_item->valuestring;
        }
        cJSON_Delete(json);
    } else {
        ESP_LOGE(TAG, "RunStt: invalid JSON response");
    }

    pcm_accumulator_.clear();

    if (text.empty()) {
        ESP_LOGW(TAG, "RunStt: empty transcription");
        return;
    }
    ESP_LOGI(TAG, "STT result: %.50s", text.c_str());

    // Send {"type":"message","text":"..."} on the WS.
    cJSON* msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "type", "message");
    cJSON_AddStringToObject(msg, "text", text.c_str());
    char* msg_str = cJSON_PrintUnformatted(msg);
    if (msg_str != nullptr) {
        SendText(msg_str);
        cJSON_free(msg_str);
    }
    cJSON_Delete(msg);
}

void SabrinaProtocol::SendAbortSpeaking(AbortReason reason) {
    (void)reason;
    if (!IsAudioChannelOpened()) return;
    SendText(R"({"type":"interrupt"})");
}
