# AGENTS.md

Guidance for AI agents (and humans) working in this repo.

## Remote testing on the Pi (Raspberry Pi Zero 2 W)

The target device is reachable at `ssh scottgbarnes@dashcam` (repo clone at `~/dashcam`).

- NEVER sync code to the Pi with rsync/scp or otherwise modify the remote
  working tree directly. The Pi's checkout must always match a commit.
- Correct workflow:
  1. Work on a feature branch locally; commit and push it to `origin`.
  2. On the Pi, `git fetch && git checkout <branch>` (or `git pull`) so the
     device builds from committed history only.
  3. Build and test on the Pi from that checkout
     (`cmake -S ~/dashcam -B ~/dashcam/build && cmake --build ~/dashcam/build`).
  4. If the Pi worktree is ever found dirty (e.g. from an accidental rsync),
     revert it (`git checkout -- .`) before pulling.
- `sudo` on the Pi requires a password; ask the user to run privileged steps
  (e.g. `make install`, `systemctl enable`) themselves.
- Only one process can own the libcamera pipeline — kill any stray
  `dashcam` process (`pkill -x dashcam`) before test runs.

## Build & test

- Local build: `make build`; tests: `make test` (also run them on the Pi
  before merging Pi-relevant changes). Lint: `make lint`.
- `make install` installs the binary and systemd unit and enables the
  service — only ever run it on the Pi, never on the local dev machine.
