#!/usr/bin/env bash
#
# Installs the libyfinance systemd *user* units, so nothing here needs root and
# nothing runs as a system service.
#
#   ./scripts/install_systemd.sh            # install + enable dashboard and daily timer
#   ./scripts/install_systemd.sh --scalp    # also enable the intraday scalping loop
#   ./scripts/install_systemd.sh --uninstall
#
# The units run in dry-run mode: they place no orders until --live is added to
# the ExecStart line. See the note printed at the end.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UNIT_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
UNITS=(libyfinance-dashboard.service libyfinance-daily.service libyfinance-daily.timer libyfinance-scalp.service)

with_scalp=0
uninstall=0
for arg in "$@"; do
    case "$arg" in
        --scalp) with_scalp=1 ;;
        --uninstall) uninstall=1 ;;
        *) echo "Unknown option: $arg" >&2; exit 1 ;;
    esac
done

if ! command -v systemctl >/dev/null 2>&1; then
    echo "systemctl not found — this script only covers systemd hosts." >&2
    exit 1
fi

if [[ "$uninstall" == 1 ]]; then
    systemctl --user disable --now libyfinance-dashboard.service libyfinance-daily.timer \
        libyfinance-scalp.service 2>/dev/null || true
    for unit in "${UNITS[@]}"; do rm -f "$UNIT_DIR/$unit"; done
    systemctl --user daemon-reload
    echo "Removed libyfinance user units."
    exit 0
fi

# The timer fires on wall-clock time, so a host on the wrong timezone would trade
# at the wrong hour — worth failing loudly rather than discovering it at 15:15.
tz="$(timedatectl show -p Timezone --value 2>/dev/null || echo unknown)"
if [[ "$tz" != "Asia/Seoul" ]]; then
    echo "WARNING: system timezone is '$tz', not Asia/Seoul."
    echo "         The daily timer's 15:15 would not be 15:15 KST. Fix the timezone or"
    echo "         edit OnCalendar in libyfinance-daily.timer before enabling it."
fi

if [[ ! -x "$ROOT/build/Release/app/daily_trade" ]]; then
    echo "Build first: cmake -S . -B build/Release -G Ninja && cmake --build build/Release" >&2
    exit 1
fi

mkdir -p "$UNIT_DIR"
for unit in "${UNITS[@]}"; do
    sed "s|__ROOT__|$ROOT|g" "$ROOT/deploy/systemd/$unit" > "$UNIT_DIR/$unit"
    echo "installed $UNIT_DIR/$unit"
done

systemctl --user daemon-reload
systemctl --user enable --now libyfinance-dashboard.service
systemctl --user enable --now libyfinance-daily.timer
if [[ "$with_scalp" == 1 ]]; then
    systemctl --user enable --now libyfinance-scalp.service
fi

# Without lingering, user services stop at logout — the daily timer would then
# only fire while someone is logged in, which is not what "runs by itself" means.
if ! loginctl show-user "$USER" -p Linger --value 2>/dev/null | grep -q yes; then
    echo
    echo "NOTE: lingering is off, so these stop when you log out. Enable it with:"
    echo "  sudo loginctl enable-linger $USER"
fi

cat <<'EOF'

Installed in DRY-RUN mode — no orders will be placed.

To trade for real on the paper account, add --live to the ExecStart line:
  systemctl --user edit --full libyfinance-daily.service
  systemctl --user edit --full libyfinance-scalp.service
then: systemctl --user daemon-reload && systemctl --user restart libyfinance-scalp.service

Status and logs:
  systemctl --user status libyfinance-daily.timer
  systemctl --user list-timers 'libyfinance*'
  journalctl --user -u libyfinance-scalp.service -f
EOF
