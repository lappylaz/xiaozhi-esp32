# SabrinaVoice on AiPi-Lite

Implementation branch: `sabrina-voice-direct-20261011`. Upstream foundation:
`78/xiaozhi-esp32` at `49b4380b3faa0ceadd28a3e78aaf0f311c46c57e`.
Tracking: SAB-3967 (foundation/public TLS), SAB-3969 (capture/STT),
SAB-3970 (voice protocol/playback), SAB-3971 (provisioning).

This is a dedicated voice surface using the existing Sabrina backend, identity,
conversation, tools and spoken confirmations. It does not implement a separate
assistant, task/email viewer, or visual HITL interface. It does not include
MicroLink or require Tailscale.

## Build and configuration

Use ESP-IDF 6.1. Append `config/sabrina.defaults` **after** the upstream defaults
and the ESP32-S3 defaults; choose board `xorigin/aipi-lite`, English, wake word
disabled. The opt-in `CONFIG_SABRINA_VOICE` keeps other upstream board behavior
unchanged. A clean upstream AiPi build is a useful baseline before this adapter.

The adapter requires the AiPi's 24 kHz ES8311 codec output. The maintained audio
service resamples capture to 16 kHz mono and performs AFE processing. The original
Xiaozhi OTA activation and vendor protocol discovery are bypassed in Sabrina mode.
Before starting Wi-Fi, Sabrina validates and applies the already-flashed local
asset bundle, prepares the idle audio engine/resampler/tasks, and checks the
running OTA image state. Only an image in `NEW` or `PENDING_VERIFY` is marked valid, and
that API result is checked. Missing/corrupt assets, declared but unloadable
speech models, audio resource failures, or OTA state/acceptance errors abort
startup without certifying the image. Retained bootloaders without rollback support
can leave a running image `NEW`; the same readiness gate accepts that state.
Automatic rollback is not established by this firmware check: recovery depends
on the installed bootloader and the separately verified recovery procedure.
A wake-disabled bundle intentionally lacking `srmodels` is valid: AFE uses its
WebRTC VAD fallback. This local acceptance does not require Wi-Fi, provisioning,
STT, or backend availability, and does not enable capture or prove microphone,
speaker, or conversation acceptance.
TLS uses the system certificate bundle, hostname verification and certificate
date validation. The clock must synchronize via NTP before connecting.

No credential belongs in `sdkconfig`, source, a command argument, or a generated
plaintext NVS image. Provisioning stores one atomic `sabrina/config_v1` JSON value
containing `url`, `stt_url`, and the scoped `sb_voice_` device key. Both HTTPS and
WSS use `X-API-Key`; old shared device keys and JWTs are not consulted. The backend
binds the key to its owner and tenant and can revoke or expire it. There is no
JWT signing secret, global API credential, or token refresh credential on-device.

Create/revoke device credentials through the backend using an enterprise-scoped
owner JWT or an existing broad owner API key. A personal-scoped JWT cannot mint
broader device access. When a key expires, mint a replacement, provision it, and
revoke the previous key. The device never exchanges its key for a general JWT.
Device NVS is not encrypted in this build; physical flash extraction can expose
the scoped credential. The host provisioning tool creates no credential file.

Hold the AiPi talk button (GPIO42) while booting to enter the bounded USB
provisioning window. The companion `tools/provision_sabrina.py` uses raw USB
Serial/JTAG and reads a key without putting it in arguments or a file. Follow
its `--help` for the exact command. The firmware acknowledges only a request ID,
status and fixed code; it never echoes the submitted key or JSON. Provisioning
updates the Sabrina namespace without erasing Wi-Fi/factory NVS. NVS init failure
in Sabrina mode fails instead of invoking the upstream automatic erase path.

Production endpoints:

- `wss://api.sabrinainc.ai/ws/voice`
- `https://api.sabrinainc.ai/api/stt`

The firmware adds `audio_format=pcm_24000` and optional authoritative `session_id`
to WSS and `sample_rate=16000` to STT. Configured URLs must not supply query strings.
The server-issued session ID is stored separately for resume; it is not an
authentication credential. HTTP redirects are disabled for the credentialed STT
request. Backend ownership checks remain authoritative on reconnect.

## Interaction and audio lifecycle

1. Wait for **Hold button to talk**. Press and hold the talk button.
2. **Listening** indicates capture has actually started. Connection establishment
   never means microphone capture has started; a release before readiness cancels
   pending capture.
3. Release to submit. The microphone remains active for an explicit 240 ms
   hangover to protect the last syllable. The input/feed boundary is joined,
   processed AFE partial output is flushed under its output mutex, and the owned
   buffer is frozen. Unprocessed AFE input is invalidated at this boundary; the
   hangover is not claimed to drain every internal AFE sample.
4. STT runs in a separate task so voice control and heartbeat remain responsive.
   The request owns its immutable PCM buffer. Both chunked and length-delimited
   response bodies are collected during the HTTP operation and checked complete.
5. The recognized text goes to the existing Sabrina voice session. No local
   Opus encode/decode round trip occurs.
6. Sabrina audio metadata must be followed by a binary PCM message. The adapter
   handles WebSocket frame/event fragmentation, checks format and optional
   sequence numbers, and feeds 20 ms PCM chunks with backpressure into the
   maintained speaker queue. **Speaking** continues until the queue and its
   in-flight output drain, even after the server sends `done`.
7. Pressing during speech or thinking cancels the old turn, flushes pending local
   audio, closes the old socket, resumes the authoritative session on a fresh
   connection, and starts the next recording only when ready and still held.
   An already executing speaker write may finish its 20 ms chunk. Reconnection
   isolates late binary frames, which do not carry turn IDs in the backend
   protocol. A canceled STT result is rejected by generation.

Capture is limited to 20 seconds / 640,000 PCM bytes. A recording that exceeds
the cap fails explicitly, rather than submitting a truncated sentence. Socket
messages are capped at 512 KiB and the inbox at 1 MiB / 128 messages. Overflow
fails the turn and makes the next press reconnect; speech is never evicted from
the queue to make room. Long or rapidly synthesized sentences can hit these
limits and are an acceptance-test item. Counters report captured samples and
received/enqueued playback bytes. No transcript or credential is logged.

Server followup offers also play and drain. If an offer arrives after the user
has already started recording, its paired audio is intentionally discarded with
an explicit byte count, so Sabrina cannot speak into the microphone. This is
button-controlled half-duplex operation. It does not claim acoustic echo-free
barge-in or hands-free operation; the board has no established playback-reference
channel for AEC. Triple-click enters Wi-Fi setup; a long PTT no longer triggers
the upstream long-press Wi-Fi reset behavior.

## Verification and remaining device acceptance

Recorded source commit `79e5a236507bf1692b1dd4f7e20eb3eaa5b0a8c8` builds in
both Sabrina-enabled and stock modes on ESP-IDF 6.1. Final receipts, source manifest,
logs, effective config, board defaults, resolved components and installation checkpoint
are in [startup-fixed evidence](build-evidence/2026-10-11/startup-fixed/README.md).
The parent evidence directory retains the historical `170eda6` build. Stock mode
compiles upstream MQTT and WebSocket sources with the new Sabrina sources excluded.
The installed enabled image is 2,713,392 bytes, SHA256
`2bc41459fa9da7118c0b82bf43f08d3012518556714d79aee51984c950a9b9ab`.

The operator completed encrypted recovery backup, verified installation to inactive
`ota_1`, and observed local assets/audio initialization, checked image acceptance and
existing Wi-Fi reconnection. Permanent credential provisioning awaits the physical
GPIO42 window. Board TLS and physical voice acceptance remain pending. To reproduce,
use the recorded ordered defaults, substituting the archived `board.defaults` for
its temporary build path and copying `components.lock.yaml` to `dependencies.lock`.

Portable buffer tests:

```sh
c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I main/protocols tests/sabrina/audio_buffer_test.cc -o /tmp/aipi-buffer-test
/tmp/aipi-buffer-test
```

These tests verify frozen-recording ownership, overflow handling, late capture
callbacks, WebSocket event fragmentation, continuation frames, interleaved
control frames, malformed offsets, nested messages and message-size rejection.
They do not certify microphone quality, TLS connectivity on the board, or any
live Sabrina turn. Build results and the deployed backend must be recorded
separately before flashing is approved.

Required physical acceptance:

- Independent microphone capture and known PCM speaker test.
- Public DNS/TLS from ordinary Wi-Fi, with no device VPN, valid clock and key.
- Short/long holds, release during connection, rapid re-press and recording cap.
- Multi-sentence playback under bursty delivery, correct final drain and no gaps.
- Press interruption during thinking, STT and playback; no late old speech.
- Wi-Fi loss and next-press reconnect; expired/revoked key rejects both routes.
- Multi-turn session continuity and spoken tool confirmations under the owner.
- Sustained repeated conversations, with memory and received/enqueued counters.

Nothing in the source/build verification alone constitutes device acceptance,
backend deployment, or authorization to flash/reset the attached unit.
