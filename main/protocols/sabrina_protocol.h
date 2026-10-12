#pragma once

#include <esp_websocket_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <atomic>
#include <deque>
#include <mutex>
#include "protocol.h"
#include "sabrina_audio_buffer.h"

class AudioService;

// One control worker owns session/transport/turn state. Socket callbacks only
// assemble bounded messages. The separate STT task owns its frozen recording.
class SabrinaProtocol final : public Protocol {
public:
    explicit SabrinaProtocol(AudioService& audio);
    ~SabrinaProtocol() override;
    bool Start() override;
    bool OpenAudioChannel() override;
    void CloseAudioChannel(bool send_goodbye = true) override;
    bool IsAudioChannelOpened() const override { return connected_.load(); }
    bool SendAudio(std::unique_ptr<AudioStreamPacket>) override { return false; }
    void SendStartListening(ListeningMode) override;
    void SendStopListening() override;
    void SendAbortSpeaking(AbortReason) override;
    void SendMcpMessage(const std::string&) override {}  // voice-only surface

private:
    enum class State { Offline, Ready, Recording, Transcribing, Thinking, Speaking, Draining };
    struct Message {
        bool binary;
        std::vector<uint8_t> bytes;
    };
    AudioService& audio_;
    EventGroupHandle_t events_;
    TaskHandle_t worker_ = nullptr;
    esp_websocket_client_handle_t socket_ = nullptr;
    State state_ = State::Offline;
    std::atomic<bool> stopping_{false}, connected_{false}, disconnected_{false};
    std::atomic<bool> pressed_{false}, press_pending_{false}, close_pending_{false};
    std::atomic<bool> ingress_failed_{false}, capture_failed_{false}, stt_running_{false};
    std::atomic<uint32_t> generation_{0};
    std::mutex ingress_mutex_, recording_mutex_, stt_mutex_;
    std::deque<Message> inbox_;
    size_t inbox_bytes_ = 0;
    sabrina::MessageAssembler assembler_{512 * 1024};
    sabrina::Recording recording_{16000 * 20};
    std::vector<int16_t> upload_;
    std::vector<uint8_t> playback_;
    size_t playback_offset_ = 0;
    uint32_t playback_generation_ = 0, stt_generation_ = 0;
    std::string ws_url_, stt_url_, device_key_, stt_text_, stt_error_;
    bool stt_done_ = false, expect_binary_ = false, reconnect_requested_ = true,
         turn_active_ = false, discard_audio_ = false;
    sabrina::ReleaseHangover release_hangover_{240000};
    int64_t state_at_ = 0, connection_at_ = 0, last_ping_ = 0, last_pong_ = 0;
    int last_seq_ = -1;
    static constexpr size_t kAudioChunkBytes = 9600;
    int32_t last_chunk_id_ = 0, pending_chunk_id_ = 0;
    size_t received_bytes_ = 0, queued_bytes_ = 0;

    void Run();
    bool Connect();
    void CloseSocket();
    void Cancel(bool start_recording = false);
    void Fail(const char* message);
    void SetState(State state);
    void BeginRecording();
    void EndRecording();
    void Transcribe();
    void PollTranscription();
    void ProcessMessage(Message&& message);
    void PumpPlayback();
    bool AcknowledgeAudio();
    bool SendText(const std::string& text) override;
    static void SocketEvent(void* arg, esp_event_base_t base, int32_t id, void* data);
};
