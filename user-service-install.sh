#!/bin/sh
set -eu
case ${1:-} in
  --dry-run) dry_run=yes ;;
  '') dry_run=no ;;
  -h|--help) echo "Usage: $0 [--dry-run]"; exit 0 ;;
  *) echo "Unknown option: $1" >&2; exit 1 ;;
esac
if [ "$(id -u)" -eq 0 ]; then
  echo 'Run this installer as the user who owns the PulseAudio session.' >&2
  exit 1
fi
source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
unit_dir=${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user
if [ "$dry_run" = yes ]; then
  echo "Install $source_dir/scripts/shairport-sync.user.service as $unit_dir/shairport-sync.service"
  echo 'Reload and enable the systemd user service. NQPTP and Avahi must already be running.'
  exit 0
fi
install -d "$unit_dir"
install -m 0644 "$source_dir/scripts/shairport-sync.user.service" "$unit_dir/shairport-sync.service"
systemctl --user daemon-reload
systemctl --user enable --now shairport-sync.service
