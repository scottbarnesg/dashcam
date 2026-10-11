#!/usr/bin/env bash
# Generate systemd/dashcam.service from the pristine base template by
# substituting the current user/home for the placeholders. Regenerated from
# scratch on every run, so repeated invocations can't accumulate edits.
set -euo pipefail

dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="$dir/dashcam.service"

sed -e "s|__USER__|$USER|g" -e "s|__HOME__|$HOME|g" "$dir/dashcam-base.service" > "$out"
echo "wrote $out (User=$USER)"

if command -v systemd-analyze >/dev/null 2>&1; then
    systemd-analyze verify "$out" || echo "warning: systemd-analyze verify reported issues" >&2
fi
