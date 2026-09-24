#!/usr/bin/env python3
"""Local dashboard server for the paper-trading account.

Serves docs/ as the dashboard and refreshes live account data on a timer by
shelling out to build/Release/app/portfolio_report. Live JSON is written to
cache/ (gitignored) and served under /live/, so the repo stays clean and the
account snapshot is never committed anywhere public.

  ./scripts/dashboard_server.py --port 8800 --interval 60   # loopback only; --bind to widen
"""

import argparse
import json
import os
import subprocess
import threading
import time
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DOCS_DIR = os.path.join(ROOT, "docs")
CACHE_DIR = os.path.join(ROOT, "cache")
REPORT_BIN = os.path.join(ROOT, "build", "Release", "app", "portfolio_report")
LIB_DIR = os.path.join(ROOT, "build", "Release")
SNAPSHOT_PATH = os.path.join(CACHE_DIR, "portfolio.json")
HISTORY_PATH = os.path.join(CACHE_DIR, "portfolio_history.json")
JOURNAL_PATH = os.path.join(ROOT, "data", "trades.jsonl")
TRADES_PATH = os.path.join(CACHE_DIR, "trades.json")
MAX_TRADES = 50
MAX_HISTORY = 5000


def run_report():
    """Run portfolio_report and return its parsed JSON, or None on failure."""
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = LIB_DIR + ":" + env.get("LD_LIBRARY_PATH", "")
    try:
        proc = subprocess.run(
            [REPORT_BIN], cwd=ROOT, env=env, capture_output=True, text=True, timeout=30
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        print(f"[dashboard] portfolio_report failed to run: {exc}", flush=True)
        return None

    if not proc.stdout.strip():
        print(f"[dashboard] portfolio_report produced no output: {proc.stderr.strip()}", flush=True)
        return None

    try:
        return json.loads(proc.stdout)
    except json.JSONDecodeError as exc:
        print(f"[dashboard] portfolio_report output was not JSON: {exc}", flush=True)
        return None


def append_history(snapshot):
    """Append this snapshot to the rolling history (one point per refresh)."""
    history = []
    if os.path.exists(HISTORY_PATH):
        try:
            with open(HISTORY_PATH, encoding="utf-8") as fh:
                history = json.load(fh)
        except (OSError, json.JSONDecodeError):
            history = []

    now = time.time()
    history.append(
        {
            "ts": int(now),
            "time": time.strftime("%Y-%m-%d %H:%M", time.localtime(now)),
            "total_return_pct": snapshot.get("total_return_pct", 0.0),
            "total_eval_krw": snapshot.get("total_eval_krw", 0.0),
        }
    )
    history = history[-MAX_HISTORY:]

    with open(HISTORY_PATH, "w", encoding="utf-8") as fh:
        json.dump(history, fh, ensure_ascii=False)
    return len(history)


def collect_trades():
    """Most recent journal entries, newest first, for the dashboard.

    The journal is append-only JSONL under data/ and grows without bound, so the
    page gets a bounded slice rather than the file. Malformed lines are skipped:
    a process killed mid-write leaves a partial one, and it must not hide the rest.
    """
    if not os.path.exists(JOURNAL_PATH):
        return []

    entries = []
    with open(JOURNAL_PATH, encoding="utf-8") as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            try:
                entries.append(json.loads(line))
            except json.JSONDecodeError:
                continue

    entries.sort(key=lambda e: e.get("ts", 0), reverse=True)
    return entries[:MAX_TRADES]


def refresh_once():
    snapshot = run_report()
    if snapshot is None:
        return
    with open(SNAPSHOT_PATH, "w", encoding="utf-8") as fh:
        json.dump(snapshot, fh, ensure_ascii=False, indent=2)
    if not snapshot.get("success"):
        print(f"[dashboard] snapshot reported failure: {snapshot.get('message', '')}", flush=True)
        return
    count = append_history(snapshot)

    trades = collect_trades()
    with open(TRADES_PATH, "w", encoding="utf-8") as fh:
        json.dump(trades, fh, ensure_ascii=False, indent=2)
    print(
        f"[dashboard] {time.strftime('%H:%M:%S')} "
        f"return={snapshot.get('total_return_pct', 0):+.2f}% "
        f"eval={snapshot.get('total_eval_krw', 0):,.0f} KRW "
        f"holdings={len(snapshot.get('holdings', []))} history={count} trades={len(trades)}",
        flush=True,
    )


def refresh_loop(interval):
    while True:
        try:
            refresh_once()
        except Exception as exc:  # keep the server alive no matter what
            print(f"[dashboard] refresh error: {exc}", flush=True)
        time.sleep(interval)


class DashboardHandler(SimpleHTTPRequestHandler):
    """Serves docs/ at / and the gitignored cache/ at /live/."""

    def translate_path(self, path):
        clean = path.split("?", 1)[0].split("#", 1)[0]
        if clean.startswith("/live/"):
            rel = clean[len("/live/") :].lstrip("/")
            return os.path.join(CACHE_DIR, os.path.basename(rel))
        return super().translate_path(path)

    def end_headers(self):
        self.send_header("Cache-Control", "no-store, max-age=0")
        super().end_headers()

    def log_message(self, fmt, *args):
        pass  # keep the console focused on refresh lines


def main():
    parser = argparse.ArgumentParser(description="Local paper-trading dashboard server")
    parser.add_argument("--port", type=int, default=8800)
    # Loopback by default. This serves the account balance with no authentication,
    # so anything wider than the machine itself is a leak — it was found listening
    # on the LAN address during the 2026-09-25 review. Checking from a phone is
    # the Telegram bot's job, which does check who is asking.
    parser.add_argument("--bind", default="127.0.0.1", help="address to listen on")
    parser.add_argument("--interval", type=int, default=60, help="refresh seconds")
    args = parser.parse_args()

    os.makedirs(CACHE_DIR, exist_ok=True)
    if not os.path.exists(REPORT_BIN):
        raise SystemExit(f"portfolio_report not built: {REPORT_BIN}\nRun ./make.sh first.")

    threading.Thread(target=refresh_loop, args=(args.interval,), daemon=True).start()

    handler = partial(DashboardHandler, directory=DOCS_DIR)
    server = ThreadingHTTPServer((args.bind, args.port), handler)
    print(f"[dashboard] http://localhost:{args.port}  (refresh every {args.interval}s, Ctrl+C to stop)", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[dashboard] stopped", flush=True)


if __name__ == "__main__":
    main()
