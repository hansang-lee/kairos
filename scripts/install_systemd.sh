#!/usr/bin/env bash
#
# Installs the kairos systemd *user* units, so nothing here needs root and
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
UNITS=(kairos-dashboard.service kairos-daily.service kairos-daily.timer kairos-scalp.service
       kairos-collector.service kairos-collector.timer)

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
    systemctl --user disable --now kairos-dashboard.service kairos-daily.timer \
        kairos-scalp.service kairos-collector.timer 2>/dev/null || true
    # The collector was called kairos-collect before; clean it up so a rename does
    # not leave an orphaned unit still firing on the old schedule.
    systemctl --user disable --now kairos-collect.timer 2>/dev/null || true
    rm -f "$UNIT_DIR/kairos-collect.service" "$UNIT_DIR/kairos-collect.timer"
    for unit in "${UNITS[@]}"; do rm -f "$UNIT_DIR/$unit"; done
    systemctl --user daemon-reload
    echo "Removed kairos user units."
    exit 0
fi

# The timer fires on wall-clock time, so a host on the wrong timezone would trade
# at the wrong hour — worth failing loudly rather than discovering it at 15:15.
tz="$(timedatectl show -p Timezone --value 2>/dev/null || echo unknown)"
if [[ "$tz" != "Asia/Seoul" ]]; then
    echo "WARNING: system timezone is '$tz', not Asia/Seoul."
    echo "         The daily timer's 15:15 would not be 15:15 KST. Fix the timezone or"
    echo "         edit OnCalendar in kairos-daily.timer before enabling it."
fi

if [[ ! -x "$ROOT/build/Release/app/daily_trade" ]]; then
    echo "Build first: cmake -S . -B build/Release -G Ninja && cmake --build build/Release" >&2
    exit 1
fi

# Remove units from before the collector was renamed, or both would run.
systemctl --user disable --now kairos-collect.timer 2>/dev/null || true
rm -f "$UNIT_DIR/kairos-collect.service" "$UNIT_DIR/kairos-collect.timer"

mkdir -p "$UNIT_DIR"
for unit in "${UNITS[@]}"; do
    sed "s|__ROOT__|$ROOT|g" "$ROOT/deploy/systemd/$unit" > "$UNIT_DIR/$unit"
    echo "installed $UNIT_DIR/$unit"
done

systemctl --user daemon-reload
systemctl --user enable --now kairos-dashboard.service
systemctl --user enable --now kairos-daily.timer
# Collection places no orders and is independent of whether trading is live, so it
# is enabled unconditionally — the data window closes whether or not you trade.
systemctl --user enable --now kairos-collector.timer
if [[ "$with_scalp" == 1 ]]; then
    systemctl --user enable --now kairos-scalp.service
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
  systemctl --user edit --full kairos-daily.service
  systemctl --user edit --full kairos-scalp.service
then: systemctl --user daemon-reload && systemctl --user restart kairos-scalp.service

Collection runs weekly regardless of trading: Yahoo keeps about a month of
5-minute bars, and that history cannot be bought back later.

Status and logs:
  systemctl --user status kairos-daily.timer
  systemctl --user list-timers 'kairos*'
  journalctl --user -u kairos-scalp.service -f
  journalctl --user -u kairos-collector.service
EOF
