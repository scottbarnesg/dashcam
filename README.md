# Dashcam

An in-vehicle dashcam for the Raspberry Pi (target: Pi Zero 2 W + Pi Camera
Module). It records video in two situations: while the vehicle is moving
(driving), and on motion while parked. Recordings are written to the SD card
as power-loss-safe, fixed-length fragments and tracked in a manifest, ready
for offload to a server when the Pi next comes online (offload not yet
implemented).

See [PROJECT.md](PROJECT.md) for goals/CONOPS and [BACKLOG.md](BACKLOG.md)
for status and planned work.

## Hardware

- Raspberry Pi (Zero 2 W tested) with a Pi Camera Module enabled in
  `raspi-config`. Any libcamera-supported camera (including UVC webcams)
  works.
- Hardware H.264 encoding uses the Pi's V4L2 M2M codec (`/dev/video11`);
  a software encoder fallback is automatic.

## Dependencies

Debian/Raspberry Pi OS:

```sh
make install-deps   # or: sudo apt install -y build-essential cmake pkg-config \
                    #     libssl-dev libopencv-dev libcamera-dev
```

`libcamera` is required at build time. Optional: FFmpeg development packages
(`libavformat`/`libavcodec`/`libavutil`) enable the fragmented-MP4 muxer for
the hardware encoder path; without them the software encoder is used.

## Build

```sh
make build              # configure + compile into ./build
make build JOBS=8       # faster on a big machine (default JOBS=2: unbounded
                        # -j OOMs on the Pi Zero 2 W's 512 MB)
```

## Test & lint

```sh
make test               # googletest suite via ctest (downloads gtest at configure)
make lint               # cppcheck
```

Pi-relevant changes should also be built and tested on the device.

## Configuration

The binary reads `dashcam.conf` from its working directory (or a path given
as the first argument). `dashcam.conf.example` documents every key with its
default; the service uses `~/.dashcam/dashcam.conf`. Everything is optional —
missing file/keys fall back to built-in defaults, and invalid values are
logged and ignored.

Key settings: motion threshold and no-motion timeout (driving trigger),
idle/recording fps, `segment_length_seconds` (power-loss blast radius),
`encoder = auto|hw|sw`, bitrate/GOP, and `camera_orientation` override.

## Run manually

```sh
./build/dashcam                          # reads ./dashcam.conf
./build/dashcam path/to/dashcam.conf
```

Ctrl-C (SIGINT) or SIGTERM shuts down gracefully, flushing the in-progress
segment. Only one process can own the camera — stop the service first
(`sudo systemctl stop dashcam`).

## Install as a boot service

Run this **on the Pi**:

```sh
make install
```

This builds the binary, installs it to `/usr/local/bin/dashcam`, generates
the systemd unit from `systemd/dashcam-base.service` (substituting your user
and home), installs it to `/etc/systemd/system/`, creates `~/.dashcam/` with
a starter config if absent, and enables + starts the service so it runs on
every boot. The service restarts automatically on failure and shuts down
gracefully on stop/reboot (enough time is allowed to flush the current
segment).

Useful service commands:

```sh
systemctl status dashcam
journalctl -u dashcam -f        # follow logs
sudo systemctl stop dashcam     # e.g. before a manual test run
```

Recordings land in `~/.dashcam/videos/` (per `video_dir`), alongside
`manifest.jsonl`.

## Uninstall the service

```sh
make uninstall
```

Stops and disables the service, removes the unit file and the installed
binary. Your config and recordings in `~/.dashcam/` are left untouched.

## Layout

- `src/` — capture (libcamera), orientation, motion detection, driving state
  machine, encoder/muxer, segmented writer, manifest, boot recovery, and the
  three-stage capture pipeline.
- `run/dashcam.cpp` — production entry point.
- `tests/` — googletest unit tests (simulated camera; no hardware needed).
- `systemd/` — unit template + generator script.
