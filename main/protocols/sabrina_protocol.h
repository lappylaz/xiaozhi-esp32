#ifndef _SABRINA_PROTOCOL_H_
#define _SABRINA_PROTOCOL_H_

// SabrinaProtocol — bridges xiaozhi's audio capture pipeline to Sabrina's
// REST + WebSocket voice ingress.
//
// xiaozhi natively expects a server that:
//   - speaks its hello-exchange protocol
//   - accepts inbound Opus binary frames as the user's audio
//   - returns Opus binary frames for TTS
//
// Sabrina's /ws/voice expects:
//   - JSON {"type":"message","text":"..."} from the client (TEXT, not audio)
//   - returns binary MP3 frames by default, or PCM 24 kHz 16-bit mono when
//     queried with ?audio_format=pcm_24000 (added in sabrina-core branch
//     sabrina-aipi-device-voice, commit b7f8ade2)
//
// Bridging strategy:
//   - The audio service still encodes mic PCM → Opus by default. In
//     SendAudio() we DECODE each Opus packet back to PCM and accumulate
//     into a per-utterance buffer. We do NOT send to the network here.
//   - On SendStopListening() we POST the buffered PCM to /api/stt
//     (Whisper proxy), receive {"text":"..."}, then send that text over
//     the WebSocket as {"type":"message","text":"..."}.
//   - Inbound binary frames on the WS are raw PCM 24 kHz mono — wrap as
//     AudioStreamPacket and call on_incoming_audio_ to feed the speaker.
//
// Auth: the device holds a long-lived JWT (minted out-of-band) AND a
// DEVICE_API_KEY (X-API-Key header for /api/stt). Both stored in NVS.
//
// See vault project page (02 - Projects/ESP32-aipi-MicroPython/README.md
// → "Sabrina Voice Integration") for the full design rationale.

#include "protocol.h"

#include <web_socket.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>

#include <string>
#include <vector>
#include <memory>

class SabrinaProtocol : public Protocol {
public:
    SabrinaProtocol();
    ~SabrinaProtocol();

    bool Start() override;
    bool OpenAudioChannel() override;
    void CloseAudioChannel(bool send_goodbye = true) override;
    bool IsAudioChannelOpened() const override;
    bool SendAudio(std::unique_ptr<AudioStreamPacket> packet) override;

    // Helper-method overrides — these are virtual but non-pure on Protocol.
    void SendStartListening(ListeningMode mode);
    void SendStopListening();
    void SendAbortSpeaking(AbortReason reason);

private:
    // Configuration loaded from Settings("sabrina"):
    //   url        — wss://api.sabrinainc.ai/ws/voice
    //   stt_url    — https://api.sabrinainc.ai/api/stt
    //   jwt        — Sabrina-issued JWT (long-lived for MVP)
    //   device_key — X-API-Key for /api/stt
    std::string ws_url_;
    std::string stt_url_;
    std::string jwt_;
    std::string device_key_;

    std::unique_ptr<WebSocket> websocket_;

    // Per-utterance PCM accumulator. Cleared on SendStartListening,
    // POSTed on SendStopListening.
    std::vector<int16_t> pcm_accumulator_;
    int pcm_sample_rate_ = 16000;  // matches the xiaozhi mic encoder rate

    // Opus decoder for accumulator path. Separate from the audio_service's
    // playback decoder.
    void* utterance_opus_decoder_ = nullptr;

    // Internal helpers
    bool SendText(const std::string& text) override;
    bool ConnectWebSocket();
    bool LoadConfig();
    void DecodeAndAccumulate(const std::vector<uint8_t>& opus_payload);
    void RunStt();  // Spawns FreeRTOS task, POSTs PCM, sends resulting text
};

#endif  // _SABRINA_PROTOCOL_H_
