#!/usr/bin/env bash
# Generate systemd/dashcam.service from the pristine base template by
# substituting the current user/home for the placeholders. Regenerated from
# scratch on every run, so repeated invocations can't accumulate edits.
set -euo pipefail

dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="$dir/dashcam.service"

# The unit runs as (and WorkingDirectory's into) the invoking user's home.
# Under `sudo make install` the invocation is root: the generated unit would
# point at /root/.dashcam (status=200/CHDIR at service start). The Makefile
# invokes sudo per-step for exactly this reason, so refuse root outright.
if [ "$(id -u)" -eq 0 ]; then
    echo "error: do not run 'sudo make install' (or this script) as root;" >&2
    echo "       run 'make install' as the user the service should run as." >&2
    exit 1
fi

sed -e "s|__USER__|$(id -un)|g" -e "s|__HOME__|${HOME}|g" "$dir/dashcam-base.service" > "$out"
echo "wrote $out (User=$(id -un))"

if command -v systemd-analyze >/dev/null 2>&1; then
    systemd-analyze verify "$out" || echo "warning: systemd-analyze verify reported issues" >&2
fi
