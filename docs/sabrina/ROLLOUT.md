# AiPi-Lite rollout and recovery

Tracking: SAB-3971 and SAB-3972. This runbook prepares an operator-approved
rollout; no device has been reset, flashed, or provisioned by this implementation.

## Release artifacts

Keep the firmware source commit, upstream pin, ESP-IDF commit, resolved component
lock, effective sdkconfig, build log, source manifest, and hashes of all flash
artifacts together. `dependencies.lock` is ignored by upstream, so archive the
resolved lock with release evidence. Never include credentials or NVS images in
Git or a public build artifact. Confirm the backend implementing SAB-3968 is
deployed and owner-authenticated enrollment works before installing this client.

The current local build evidence is under `/private/tmp/aipi-build-20261011/`.
Those temporary paths are working artifacts and must be preserved with the
approved release before a rollout. The existing checkout and stale build are
historical references, not a verified recovery image of the attached device.

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

Both the old local configuration and this build select `partitions/v2/16m.csv`;
the source CSVs match. That is not evidence of the board's actual partition map.
The build's generated flash set writes bootloader at `0x0`, partition table at
`0x8000`, OTA selection at `0xd000`, application at `0x20000`, and assets at
`0x800000`. A blanket `idf.py flash` therefore replaces more than the application.
Never run it as an assumed app-only update, erase flash, or write an NVS image.

## Install and provision

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
and Wi-Fi behavior. Do not repeatedly flash or erase NVS to mask an unknown fault.

Current status: source/build tests pass; deployed backend, encrypted recovery
backup, installation, provisioning, and physical acceptance are not yet verified.
