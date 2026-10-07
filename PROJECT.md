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

- Motion detection and on-motion recording implemented (frame-differencing via OpenCV).
- Driving-state detection (e.g., GPS, accelerometer/IGN signal) — not yet implemented.
- Server offload + delete-after-offload — not yet implemented (OpenSSL is linked in anticipation; a `feature/delete-old-videos` branch has unmerged WIP for storage cleanup).
- Pi Camera Module support — not yet implemented; current capture path is USB webcam only.
