#!/usr/bin/env bash
set -euo pipefail

if [ "${1:-}" = "" ]; then
  echo "usage: $0 <tic80-binary>" >&2
  exit 1
fi

BIN="$1"
ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
SESSION_DIR="$(mktemp -d)"
EPISODE_SCRIPT="$SESSION_DIR/episode.lua"

cleanup() {
  XDG_RUNTIME_DIR="$SESSION_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" stop >/dev/null 2>&1 || true
  rm -rf "$SESSION_DIR"
}
trap cleanup EXIT

cat > "$EPISODE_SCRIPT" <<'EOF'
log("smoke")
frameadvance()
end_episode("done", "smoke")
EOF

START_OUT="$(XDG_RUNTIME_DIR="$SESSION_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" start tools/mcp/fixtures/playtest_episode.lua)"
printf '%s\n' "$START_OUT" | grep -q '^started tic80ctl session'

STATUS_OUT="$(XDG_RUNTIME_DIR="$SESSION_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" status)"
printf '%s\n' "$STATUS_OUT" | grep -q '^running pid='

LOAD_JSON="$(XDG_RUNTIME_DIR="$SESSION_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" load --json tools/mcp/fixtures/playtest_episode.lua)"
printf '%s\n' "$LOAD_JSON" | jq -e '.response.result.isError == false' >/dev/null

RUN_JSON="$(XDG_RUNTIME_DIR="$SESSION_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" run --json)"
printf '%s\n' "$RUN_JSON" | jq -e '.response.result.isError == false' >/dev/null

EVAL_OUT="$(XDG_RUNTIME_DIR="$SESSION_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" eval "trace(type(TIC))")"
printf '%s\n' "$EVAL_OUT" | grep -q 'function'

SCREEN_OUT="$(XDG_RUNTIME_DIR="$SESSION_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" screenshot tic80ctl_capture.png)"
printf '%s\n' "$SCREEN_OUT" | grep -q 'saved screenshot: tic80ctl_capture.png'
[ -s "$ROOT/tic80ctl_capture.png" ]

PLAYTEST_OUT="$(XDG_RUNTIME_DIR="$SESSION_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" playtest --script-file "$EPISODE_SCRIPT")"
printf '%s\n' "$PLAYTEST_OUT" | grep -q 'status=done'
printf '%s\n' "$PLAYTEST_OUT" | grep -q 'artifact_path=./playtest/episode_'

GRID_LOAD_OUT="$(XDG_RUNTIME_DIR="$SESSION_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" load grid_shooter.lua)"
printf '%s\n' "$GRID_LOAD_OUT" | grep -q 'cart grid_shooter.lua loaded'

set +e
GRID_RUN_OUT="$(XDG_RUNTIME_DIR="$SESSION_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" run 2>&1)"
GRID_RUN_STATUS=$?
set -e
[ "$GRID_RUN_STATUS" -ne 0 ]
printf '%s\n' "$GRID_RUN_OUT" | grep -q 'invalid params, btn'

set +e
SCREEN_ERR="$(XDG_RUNTIME_DIR="$SESSION_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" screenshot shots/tic80ctl_capture.png 2>&1)"
SCREEN_ERR_STATUS=$?
set -e
[ "$SCREEN_ERR_STATUS" -ne 0 ]
printf '%s\n' "$SCREEN_ERR" | grep -q 'relative screenshot directory does not exist: shots'

STOP_OUT="$(XDG_RUNTIME_DIR="$SESSION_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" stop)"
printf '%s\n' "$STOP_OUT" | grep -q '^stopped tic80ctl session'

STATUS_STOPPED="$(XDG_RUNTIME_DIR="$SESSION_DIR" TIC80CTL_BIN="$BIN" "$ROOT/tic80ctl" status || true)"
printf '%s\n' "$STATUS_STOPPED" | grep -q '^tic80ctl: no active session$'

echo "tic80ctl smoke test passed."
