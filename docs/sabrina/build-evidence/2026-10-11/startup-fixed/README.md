# Startup-fixed build and installation checkpoint

Firmware source: `79e5a236507bf1692b1dd4f7e20eb3eaa5b0a8c8`.
ESP-IDF 6.1: `fff9895c82d744c7237be8847347bdd1b07c6643`.
Both Sabrina-enabled and stock builds passed. Stock builds compile the upstream
MQTT and WebSocket paths with Sabrina disabled. The earlier evidence in the parent
directory records the historical `170eda6` build, not the installed firmware.

- Installed app: 2,713,392 bytes, SHA256
  `2bc41459fa9da7118c0b82bf43f08d3012518556714d79aee51984c950a9b9ab`.
- Matched assets: 307,037 bytes, SHA256
  `7820c577020e272688330147c3a8d5ee3fee5c84dfa0fc53569470a630e7b2eb`.
- Stock compatibility app: 2,697,792 bytes, SHA256
  `3050ab4b844469b202a2306fd084f3437be583c20483355536fc2d7b63ebecc2`.
- Resolved component lock SHA256:
  `67a99ad0838973849f9cbf7bbdcc36cf960f9c7a9fe8a15944c301d859c11dec`.

The build JSON receipts record exact configure/final commands and ordered defaults.
To reproduce with the pinned IDF environment, copy `components.lock.yaml` to project
`dependencies.lock`, substitute this archived `board.defaults` for its temporary
path, and append `config/sabrina.defaults` only for the Sabrina-enabled build.
`sdkconfig.snapshot` is an unmodified copy of the enabled effective configuration.
The source manifest covers the 1,759 tracked files at the firmware source commit.
This later documentation-only commit adds evidence and does not change or rebuild
the installed firmware. Build logs and receipts retain their original temporary
paths for provenance; build receipt `device_flashing` and `runtime_verification`
fields describe the build operation, not the later hardware installation.

## Hardware checkpoint

`installation-status.json` records the root hardware operator's installation and
first-boot report. The verified, encrypted two-read recovery backup remains in
managed local release storage, with its encryption key in macOS Keychain; neither
NVS nor recovery image is included in Git. App and matched assets were staged and
read back, followed by the inactive OTA selector sector. The original bootloader,
partition table, NVS, old application and first OTA metadata sector were preserved.
The generated build flash offsets are not the executed installation plan: the app
was installed to inactive `ota_1` at `0x410000`, not default `ota_0` at `0x20000`.

`installed-bootloader-compatibility.json` records an authenticated in-memory backup
comparison: the installed IDF 5.5.2 bootloader exactly matches the historical local
binary whose accompanying configuration enables application rollback. The updated
application also accepts NEW through the same checked local-readiness gate as
PENDING_VERIFY, so it does not rely on a particular bootloader state transition.
Successful first boot was observed on that preserved bootloader. Shared assets must
be restored along with the original application when reverting this installation.

Local assets, ES8311, idle AFE WebRTC, checked image acceptance and existing Wi-Fi
reconnection were observed. The backend was deployed and its live authentication,
STT, WebSocket and revocation checks passed separately. The permanent device key is
held in Keychain; USB provisioning awaits the physical GPIO42 window. Board TLS,
authenticated voice, microphone/speaker quality and repeated-turn acceptance remain
pending. Boot and Wi-Fi success do not establish those results.
