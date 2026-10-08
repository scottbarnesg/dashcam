# Backlog

Items are roughly in priority order.

## 1. Pi Camera Module support (build-time optional) — DONE (pending verification on real Pi hardware)

Add capture support for the Pi Camera Module while keeping USB webcam support for desktop dev/testing. Selection happens at build time (e.g., CMake option `USE_LIBCAMERA` vs OpenCV `VideoCapture`), behind a common `Camera` interface so `dashcam.cpp` and the motion detector are backend-agnostic.

- Define a `Camera` abstract interface; refactor `USBCamera` to implement it.
- Implement a `PiCamera` backend — DONE: direct libcamera (Application Writer's Guide flow: CameraManager, VideoRecording role, FrameBufferAllocator, requestCompleted; NV12/I420/MJPEG to BGR).
- libcamera is the only production backend (single code path; USB webcams work through it too). USBCamera/V4L2-direct removed.
- Verify motion detection still works with Pi camera output (resolution/format differences).

## 2. Driving mode — motion-as-driving proxy — DONE

**Decision:** the primary trigger (record while driving) is implemented in software as a motion-as-driving proxy. Because the camera is vehicle-fixed, sustained optical motion implies the vehicle is moving. Mid-drive fragmentation (clips ending at red lights / stopped traffic) is **accepted**; the no-motion stop timeout "N" is deliberately large.

State machine: `IDLE` → first motion starts recording → `RECORDING` → stop only after N consecutive seconds with no motion → back to `IDLE`. Reuses the existing extend-on-motion loop in `recordVideo()`; replaces the fixed 10s window.

- Make N an explicit named constant/config (proposed start: 120s; tune in vehicle). Motion *starts* recording immediately; only *stopping* uses the large N (asymmetric would be a later refinement).
- Fix the existing bug in the extension loop: motion must update "last motion seen" every frame, not reset a duration each hit — with a long N the current logic is fine, but verify timeout math is driven by `motionLastDetected()` which already exists and is unused.
- Frame pacing while recording: current loop captures as fast as possible with no delay; add a max capture rate so driving clips don't run the CPU at 100% (target 30 fps vs the idle loop's 2 fps).
- Threshold sanity while driving: verify `motionThreshold` fires at night and doesn't false-trigger on sun/shadow flicker at speed (tune in vehicle; log detected "fps" and trigger rate).
- Update PROJECT.md "Current status" once implemented.

**Prerequisites:** §3 (power-loss-safe recording — a hard power cut mid-drive must not lose the whole clip) and the `VideoWriter` destructor hang fix pulled forward from §6 (every clip stop constructs/destroys a `VideoWriter`, so a blocked `pop()` would wedge the whole state machine at the end of the first recording).

## 3. Power-loss-safe recording — DONE

A hard power cut (ignition off / yanked lead) must not destroy the footage the device exists to capture. A plain `cv::VideoWriter` MP4 is **entirely unplayable** if power dies before `release()` — the `moov` atom is written on close, so a lost file means a lost drive.

- Record into fixed-length, self-closing segments (proposed 1–5 min): open → write → close → next, continuously while in RECORDING state.
- Prefer fragmented MP4 (fMP4) or equivalent so even the in-progress segment survives a cut; at worst one segment's tail is lost, never the whole recording.
- On boot, detect and quarantine/discard the incomplete final segment; only *complete* segments get registered for cleanup/offload eligibility (ties into §5a manifest).
- Follow-up (see Later): supercap/UPS graceful shutdown to close the in-progress segment cleanly.

## 4. Configuration file — DONE

Replace hardcoded values with a config file read at startup (e.g., `~/.dashcam/dashcam.conf`); prerequisite for in-vehicle tuning of §2/§3, since every tuning iteration is a recompile otherwise.

- Parameters: no-motion timeout N, motion thresholds (separate driving vs parked contexts), idle and recording capture rates, segment length, video directory, camera options.
- Sensible built-in defaults when the file is absent; log invalid values rather than crashing.

## 5. Storage management

### 5a. File naming + manifest schema (design first — prerequisite for 5b and 5c) — DONE

The manifest is the load-bearing artifact: cleanup, offload, and crash recovery all depend on it. Specify before building either.

- Naming: collision-free (current `YYYY-MM-DD_HH_MM_SS.mp4` collides within a second), sortable, and encodes clip context (continuous segment vs event clip).
- Per-recording manifest entry: path, start time, duration, size, SHA-256, type, and state (local / upload-pending / offload-confirmed).
- Manifest must survive power loss: write-temp + atomic rename (or append-only log), and be rebuildable from on-disk files if corrupted.

### 5b. Cleanup of old files

Bound local storage so the SD card never fills:

- Delete oldest recordings when free space drops below a threshold and/or total age exceeds a retention limit (finish/replace the unmerged `feature/delete-old-videos` WIP).
- Ensure writes fail gracefully (no corrupt files/crashes) when disk is full or the card is read-only.

### 5c. Offload to server over the network (future phase — design now)

Connectivity is intermittent: files accumulate while driving (offline), offload when the car returns home and connects to Wi-Fi, then delete locally after successful upload.

- Background offload loop: detect connectivity, upload files (TLS — OpenSSL already linked; certs target exists), verify server acknowledgment, then delete local copy.
- Resume/retry safely after partial uploads and power loss mid-transfer.
- Never delete a file that has not been confirmed offloaded.

## 6. Robustness fixes

- ~~Bounded frame buffer~~ — DONE (bounded at 64 frames, drops counted and logged).
- ~~Empty-frame handling~~ — DONE (dropped in capture, motion, and writer paths).
- ~~`VideoWriter` destructor hang~~ — DONE (`SafeQueue::close()` wakes blocked consumers).
- Fix `configure-service.sh` (appends a literal sed command into the unit file instead of executing it) and `make install` invoking `make build` non-recursively.

## Later / ideas

- Secondary/fallback driving triggers — GPS speed, MPU-6050 IMU (in hand; needs no-solder wiring via Grove SHAT or jumpers for dev), or ACC-switched power.
- Watchdog/heartbeat and graceful shutdown for vehicle power loss.
- SD wear reduction (read-only rootfs, batched writes).
- Hardware H.264 encoding path for low-power Pi targets (libcamera encode-stream role, avoiding CPU decode/re-encode).
