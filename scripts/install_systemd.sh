#!/usr/bin/env bash
#
# Installs the kairos systemd *user* units, so nothing here needs root and
# nothing runs as a system service.
#
#   ./scripts/install_systemd.sh            # install + enable trader, collector, dashboard
#   ./scripts/install_systemd.sh --uninstall
#
# The units run in dry-run mode: they place no orders until --live is added to
# the ExecStart line. See the note printed at the end.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UNIT_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
UNITS=(kairos-dashboard.service kairos-trader.service kairos-trader.timer
       kairos-collector.service kairos-collector.timer
       kairos-bot.service kairos-bot.timer)

# Units from earlier layouts. Left behind they would keep firing alongside their
# replacements, which neither unit file would reveal.
LEGACY_UNITS=(kairos-daily.service kairos-daily.timer kairos-scalp.service
              kairos-collect.service kairos-collect.timer)

uninstall=0
live=0
for arg in "$@"; do
    case "$arg" in
        --uninstall) uninstall=1 ;;
        --live) live=1 ;;
        *) echo "Unknown option: $arg" >&2; exit 1 ;;
    esac
done

if ! command -v systemctl >/dev/null 2>&1; then
    echo "systemctl not found — this script only covers systemd hosts." >&2
    exit 1
fi

if [[ "$uninstall" == 1 ]]; then
    systemctl --user disable --now kairos-dashboard.service kairos-trader.timer \
        kairos-trader.service kairos-collector.timer 2>/dev/null || true
    for unit in "${LEGACY_UNITS[@]}"; do
        systemctl --user disable --now "$unit" 2>/dev/null || true
    done
    for unit in "${UNITS[@]}" "${LEGACY_UNITS[@]}"; do rm -f "$UNIT_DIR/$unit"; done
    systemctl --user daemon-reload
    echo "Removed kairos user units."
    exit 0
fi

# Market hours and the once-a-day time are read as wall-clock, so a host on the
# wrong timezone would trade at the wrong hour — worth saying loudly rather than
# discovering it at 15:15.
tz="$(timedatectl show -p Timezone --value 2>/dev/null || echo unknown)"
if [[ "$tz" != "Asia/Seoul" ]]; then
    echo "WARNING: system timezone is '$tz', not Asia/Seoul."
    echo "         --daily-at 1515 would not mean 15:15 KST, and the market-hours gate"
    echo "         would be wrong too. Fix the timezone before enabling the trader."
fi

if [[ ! -x "$ROOT/build/Release/app/trader" ]]; then
    echo "Build first: cmake -S . -B build/Release -G Ninja && cmake --build build/Release" >&2
    exit 1
fi

# Drop superseded units before writing the new ones, or both would run.
for unit in "${LEGACY_UNITS[@]}"; do
    systemctl --user disable --now "$unit" 2>/dev/null || true
    rm -f "$UNIT_DIR/$unit"
done

mkdir -p "$UNIT_DIR"
for unit in "${UNITS[@]}"; do
    sed "s|__ROOT__|$ROOT|g" "$ROOT/deploy/systemd/$unit" > "$UNIT_DIR/$unit"
    echo "installed $UNIT_DIR/$unit"
done

# Trading for real is a flag here rather than an edit to the installed unit, so
# rebuilding a machine from the repository reproduces it. It used to live only in
# the installed copy, which meant a reinstall silently dropped back to dry-run and
# said so only in a log line nobody reads.
if [[ "$live" == 1 ]]; then
    sed -i 's|\(ExecStart=.*app/trader .*\)$|\1 --live|' "$UNIT_DIR/kairos-trader.service"
    echo "enabled LIVE trading in $UNIT_DIR/kairos-trader.service"
fi

systemctl --user daemon-reload
systemctl --user enable --now kairos-dashboard.service
systemctl --user enable --now kairos-trader.timer
# Collection places no orders and is independent of whether trading is live, so it
# is enabled unconditionally — the data window closes whether or not you trade.
systemctl --user enable --now kairos-collector.timer
# The bot answers questions and cannot place an order, so it is safe to enable
# whatever the trading mode is. It exits immediately when the Telegram keys are
# unset, so enabling it on a machine that has none costs a few milliseconds a
# minute and nothing else.
systemctl --user enable --now kairos-bot.timer
# Without lingering, user services stop at logout — the trader would then only run
# while someone is logged in, which is not what "runs by itself" means.
if ! loginctl show-user "$USER" -p Linger --value 2>/dev/null | grep -q yes; then
    echo
    echo "NOTE: lingering is off, so these stop when you log out. Enable it with:"
    echo "  sudo loginctl enable-linger $USER"
fi

if [[ "$live" == 1 ]]; then
cat <<'EOF'

Installed in LIVE mode — the trader will place real orders on the configured
account. Check which account that is:
  grep KIS_MODE .env
EOF
else
cat <<'EOF'

Installed in DRY-RUN mode — no orders will be placed.

To trade for real, reinstall with the flag, so the setting lives in the repository
rather than only in the installed unit:
  ./scripts/install_systemd.sh --live
then: systemctl --user daemon-reload

The trader runs once a day under a timer, which suits once-a-day strategies. To
run a minute-bar ('scalp') profile it must become a loop instead: set Type=simple,
drop --once, add Restart=on-failure, and enable the .service rather than the
.timer. The binary supports both.

Collection runs weekly regardless of trading: Yahoo keeps about a month of
5-minute bars, and that history cannot be bought back later.

Status and logs:
  systemctl --user status kairos-trader
  systemctl --user list-timers 'kairos*'
  journalctl --user -u kairos-trader -f
  journalctl --user -u kairos-collector
EOF
fi
