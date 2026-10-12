# AiPi-Lite rollout and recovery

Tracking: SAB-3971 and SAB-3972. The operator-approved backend deployment, encrypted
recovery backup and firmware installation completed on 2026-10-11. Initial boot
and Wi-Fi reconnection were observed; device credential provisioning and physical
voice acceptance remain pending. Earlier no-reset/no-flash checkpoints were historical.

## Release artifacts

Keep the firmware source commit, upstream pin, ESP-IDF commit, resolved component
lock, effective sdkconfig, build log, source manifest, and hashes of all flash
artifacts together. `dependencies.lock` is ignored by upstream, so archive the
resolved lock with release evidence. Never include credentials or NVS images in
Git or a public build artifact. Confirm the backend implementing SAB-3968 is
deployed and owner-authenticated enrollment works before installing this client.

Final build receipts and the installation checkpoint are archived in
[the startup-fixed evidence](build-evidence/2026-10-11/startup-fixed/README.md).
Temporary build paths in those receipts retain their original provenance. The
verified full-device backup and binaries are preserved in managed local release
storage; the encrypted backup key is in macOS Keychain. Recovery data is not in Git.

## Inspect before changing the device

1. Obtain rollout approval for the attached USB unit, including the brief reset
   needed to inspect its flash. Match its USB identity before every operation.
2. Read the actual partition table, flash size, encryption/secure-boot status,
   OTA selection, and application identity. Do not infer the flashed layout or
   image from a local build directory. Stop if protections or layout differ
   from the proposed release.
3. Preserve a verified recovery image and its layout before writing. A complete
   flash image contains NVS credentials: stream it to encrypted, access-controlled
   storage with a managed key; do not create a plaintext full-flash/NVS dump.
   Verify the encrypted backup is readable and record hashes without secrets.
4. Confirm recovery tooling and access to physical BOOT/reset controls. A source
   checkout alone is not a tested rollback path.

The actual 16 MiB board partition table was inspected and matched
`partitions/v2/16m.csv`: NVS at `0x9000`, OTA metadata at `0xd000`, `ota_0` at
`0x20000`, `ota_1` at `0x410000`, and shared assets at `0x800000`. The original
application occupied `ota_0`; inactive `ota_1` was selected for this installation.
The build's generated flash set writes bootloader at `0x0`, partition table at
`0x8000`, OTA selection at `0xd000`, application at `0x20000`, and assets at
`0x800000`. A blanket `idf.py flash` therefore replaces more than the application.
Never run it as an assumed app-only update, erase flash, or write an NVS image.

## Install and provision

For the completed installation, app and matched assets were written to `0x410000`
and `0x800000` and read back before writing the second OTA metadata sector at
`0xe000` with sequence 2 / NEW. All write readbacks passed. Bootloader, partition
table, NVS, old `ota_0`, and the first OTA metadata sector were preserved. The
archived bootloader comparison proves an exact match with the historical IDF 5.5.2
bootloader and records its accompanying rollback-enabled configuration. The new
IDF 6.1 application successfully booted on this preserved bootloader.

Choose the smallest compatible write set after inspecting the board and confirming
IDF bootloader compatibility. Record exact offsets and artifact hashes for approval.
If retaining the old bootloader is not proven compatible, treat bootloader/OTA/assets
changes as a full firmware migration and validate recovery first. Preserve Wi-Fi
and factory NVS unless a separately approved repair is necessary.

At first boot, verify the `SabrinaStartup` local-readiness log and, for a newly
selected OTA slot, the checked new/pending-image acceptance log. These occur before
Wi-Fi startup and after local assets and idle audio resources have been checked.
Network or credential setup failures must not make an otherwise healthy local
image fail local acceptance; local assets/audio failures must not mark it valid.
Do not infer automatic rollback support from the image state or acceptance log;
verify the installed bootloader separately and preserve operator recovery. OTA-valid
status is separate from physical voice acceptance.

After the firmware boots, hold GPIO42 during the next boot to open its 60-second
USB provisioning window. The host helper accepts the device credential through
a hidden prompt or managed-secret pipe; it waits for physical-mode readiness,
requires the matching readback receipt, and does not save or display the key.
Create the key using an enterprise-scoped owner session; keep the broader owner
credential off the board. New Sabrina settings are one NVS value and clear the
previous Sabrina session ID. NVS is unencrypted on this build, so revoke the scoped
key if the device is lost or a flash image leaves trusted custody.

## Acceptance and recovery

Run the physical acceptance checklist in README.md and SAB-3972. Record actual
microphone/speaker, ordinary-Wi-Fi TLS, owner identity, spoken tool confirmation,
interruption/reconnection, key revocation, and repeated-turn results separately.

If acceptance fails, preserve nonsecret diagnostic counters, revoke any newly
issued device key, and use the verified recovery image/layout. Restore only the
approved regions and read back hashes before booting. Verify the prior firmware
and Wi-Fi behavior. The assets partition is shared, so restoring the old
application also requires matching original assets; changing only the selected OTA
slot is not a complete rollback. Do not repeatedly flash or erase NVS to mask an
unknown fault.

Current checkpoint: source/build tests pass; backend `b5632d90d` deployed with
live authentication, STT, WebSocket and revocation checks passing. Encrypted
two-read recovery backup, installation readbacks, local assets/ES8311/AFE WebRTC
initialization, checked image acceptance and existing Wi-Fi reconnection passed.
The permanent scoped device key is held in Keychain; USB provisioning awaits the
physical GPIO42 window. Board TLS, authenticated voice and physical acceptance
remain pending. This documentation commit does not change the installed source
commit `79e5a236507bf1692b1dd4f7e20eb3eaa5b0a8c8`.

## Interruption follow-up

Physical listening acceptance passed: Branson reported that the reply sounds as
expected, like the iOS app. Interruption then exposed a lost-recording defect:
the old worker stopped playback, reopened the socket, and waited for `Ready`
before starting capture. A prompt released before the new handshake was never
recorded. Capture now begins before interrupt send/socket teardown, survives
connection-state changes, and retains its transcript until the authenticated
session is ready. The transport is still replaced to exclude old untagged PCM.

`python3 -m unittest discover -s tests -v` includes a sanitizer-enabled host
harness executing the production worker, cancellation, connection, message and
transcription methods with delayed transport boundaries. The pre-fix worker
failed with zero samples; fixed ordering preserves the prompt and rejects stale
transcription results. Real interruption acceptance still requires repeating the
story/time test on the patched board.

For this application-only update, inspect and encrypt the current provisioned
board first. Stage the patched app in `ota_0`, verify it, then select sequence 3
NEW in the first OTA metadata sector. Retain the working Sabrina application in
`ota_1` with sequence 2 VALID and the same shared assets. Bootloader, partition
table, NVS and assets are unchanged. The original vendor application is still
recoverable from the original encrypted full-flash backup; it is no longer the
on-device fallback after staging this update. This update must not use a blanket
flash command.
