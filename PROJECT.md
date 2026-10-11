# Dashcam

An in-vehicle dashcam running on a Raspberry Pi with a Pi Camera Module. A USB webcam is used only for development and testing; the target hardware is the Pi Camera Module.

## Goals

Record video in two situations:

1. **Driving** — record while the vehicle is in motion.
2. **Parked** — record when motion is detected while the vehicle is parked.

## Operating environment

The system operates in an **intermittently connected environment**:

- While driving, the Pi is expected to be **offline** (no Wi-Fi). Video is persisted **locally on-device**.
- When the Pi connects to Wi-Fi (e.g., parked at home), it **offloads stored video files to a server** (not yet implemented) and **deletes files that have been successfully offloaded** from local storage.

## CONOPS

1. Continuously monitor for record triggers (driving state, or motion while parked).
2. Write triggered recordings to local storage with timestamps, managing disk space so the card never fills.
3. On network connection, upload pending recordings to the server and delete them locally once confirmed transferred.

## Current status / gaps

- Operating **mode** is an explicit config setting (`mode = driving | persistent`,
  backlog item 10), superseding the pure motion-as-driving proxy:
  `driving` (ignition power) records continuously from boot — power presence
  is the trigger, motion never gates recording; `persistent` (battery) uses
  the motion-as-driving state machine (backlog item 2): motion starts
  recording; recording continues until no motion is seen for a configurable
  timeout (default 120s). Mid-drive fragmentation at stops is accepted.
- Capture runs behind a `Camera` interface with a single production backend:
  libcamera (Pi Camera Module and any libcamera-supported camera, including
  UVC webcams); tests use a simulated camera. Frames flow as native YUV
  (NV12) — motion detection uses the luma plane, no BGR conversion in the hot path.
- Recording uses the Pi's hardware H.264 codec (V4L2 M2M) into fragmented
  MP4 (`encoder = auto|hw|sw`, software fallback automatic).
- Capture runs as a three-stage threaded pipeline (`pipeline.hpp`):
  ingest (frames out of the camera, never blocks) -> motion/control
  (detection + state machine) -> record (owns the VideoWriter and its
  flush-on-close), connected by bounded queues with counted drops.
- Recording is **power-loss-safe**: fixed-length self-closing segments with a
  write sentinel, boot-time quarantine of incomplete files, and free-space checks.
- A **manifest** (`manifest.jsonl`, atomic writes, SHA-256 per file, rebuildable)
  tracks every complete recording and its upload state — the basis for cleanup
  and offload.
- **Storage cleanup** (backlog 5b) bounds the SD card: the oldest
  manifest-registered recordings are deleted to keep `min_free_mb` free and,
  when `retention_days > 0`, anything past the age limit. Runs at boot and
  on-demand when the recorder nears full; the in-progress segment and
  quarantined files are never touched.
- Configuration via `dashcam.conf` (thresholds, timeout N, fps, segment length,
  paths); runtime tuning needs no recompile.
- Frames are orientation-corrected in the capture backend (backlog 7): the
  sensor's reported rotation is applied (or a `camera_orientation` config
  override), so motion detection and the encoder see upright pixels.
  Pending on-device verification.
- Server offload (backlog 5c) — not yet implemented; OpenSSL and the manifest
  are ready for it.
- Secondary driving triggers (GPS, MPU-6050 IMU, ACC-switched power) — backlog.

