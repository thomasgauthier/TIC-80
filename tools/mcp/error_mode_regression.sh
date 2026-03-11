#!/usr/bin/env bash
set -euo pipefail

if [ "${1:-}" = "" ]; then
  echo "usage: $0 <tic80-binary>" >&2
  exit 1
fi

BIN="$(CDPATH='' cd -- "$(dirname -- "$1")" && pwd)/$(basename -- "$1")"
ROOT="$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
FIXTURE_DIR="$ROOT/tools/mcp/fixtures"
FIXTURE_CART="mcp_error_mode.lua"
TMP_DIR="$(mktemp -d)"

REQ_PIPE=""
OUT=""
ERR=""
RUN_PID=""

cleanup() {
  if [ -n "$RUN_PID" ] && kill -0 "$RUN_PID" 2>/dev/null; then
    kill "$RUN_PID" 2>/dev/null || true
    wait "$RUN_PID" 2>/dev/null || true
  fi
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

start_server() {
  local name="$1"
  REQ_PIPE="$TMP_DIR/${name}.pipe"
  OUT="$TMP_DIR/${name}.out.jsonl"
  ERR="$TMP_DIR/${name}.err.log"
  mkfifo "$REQ_PIPE"

  set +e
  (
    cd "$FIXTURE_DIR"
    xvfb-run --auto-servernum "$BIN" "$FIXTURE_CART" --fs . --mcp < "$REQ_PIPE" > "$OUT" 2> "$ERR"
  ) &
  RUN_PID="$!"
  set -e

  exec 3> "$REQ_PIPE"
}

stop_server() {
  exec 3>&-

  set +e
  wait "$RUN_PID"
  local status=$?
  set -e
  RUN_PID=""

  if [ "$status" -ne 0 ]; then
    echo "MCP process failed with exit code $status" >&2
    echo "stdout: $OUT" >&2
    echo "stderr: $ERR" >&2
    exit 1
  fi
}

request() {
  printf '%s\n' "$1" >&3
}

extract_capture_path() {
  local out_file="$1"
  local id="$2"
  python - <<'PY' "$out_file" "$id" "$FIXTURE_DIR"
import os
import re
import sys

text = open(sys.argv[1]).read()
request_id = re.escape(sys.argv[2])
fixture_dir = sys.argv[3]
match = re.search(r'"id":%s.*saved screenshot: [^)]*\(([^)]*)\)' % request_id, text)

if not match:
    print("")
else:
    path = match.group(1)
    print(path if os.path.isabs(path) else os.path.join(fixture_dir, path))
PY
}

require_nonempty_file() {
  local path="$1"
  if [ -z "$path" ] || [ ! -s "$path" ]; then
    echo "missing or empty capture file: $path" >&2
    echo "stdout: $OUT" >&2
    echo "stderr: $ERR" >&2
    exit 1
  fi
}

run_common_handshake() {
  request '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{}}}'
  request '{"jsonrpc":"2.0","method":"notifications/initialized"}'
  request '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"run_command","arguments":{"command":"run"}}}'
  sleep 3
}

start_server "triggered"
run_common_handshake
request '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"capture_screenshot","arguments":{"path":"trigger_warmup.png"}}}'
sleep 1
request '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"capture_screenshot","arguments":{"path":"trigger_before.png"}}}'
request "{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\",\"params\":{\"name\":\"run_command\",\"arguments\":{\"command\":\"eval error('mcp eval boom')\"}}}"
request '{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"capture_screenshot","arguments":{"path":"trigger_after.png"}}}'
stop_server

TRIGGERED_OUT="$OUT"
TRIGGERED_ERR="$ERR"

grep -q '"id":1' "$TRIGGERED_OUT"
grep -q '"id":2' "$TRIGGERED_OUT"
grep '"id":2' "$TRIGGERED_OUT" | grep -q '"isError":false'
grep -q '"id":3' "$TRIGGERED_OUT"
grep '"id":3' "$TRIGGERED_OUT" | grep -q '"isError":false'
grep -q '"id":4' "$TRIGGERED_OUT"
grep '"id":4' "$TRIGGERED_OUT" | grep -q '"isError":false'
grep -q '"id":5' "$TRIGGERED_OUT"
grep '"id":5' "$TRIGGERED_OUT" | grep -q '"isError":true'
grep '"id":5' "$TRIGGERED_OUT" | grep -q 'mcp eval boom'
grep -q '"id":6' "$TRIGGERED_OUT"
grep '"id":6' "$TRIGGERED_OUT" | grep -q '"isError":false'

TRIGGERED_BEFORE="$(extract_capture_path "$TRIGGERED_OUT" 4)"
TRIGGERED_AFTER="$(extract_capture_path "$TRIGGERED_OUT" 6)"
require_nonempty_file "$TRIGGERED_BEFORE"
require_nonempty_file "$TRIGGERED_AFTER"

cmp -s "$TRIGGERED_BEFORE" "$TRIGGERED_AFTER" || {
  echo "MCP-triggered eval error changed the captured framebuffer; expected stable run view." >&2
  echo "stdout: $TRIGGERED_OUT" >&2
  echo "stderr: $TRIGGERED_ERR" >&2
  exit 1
}

start_server "spontaneous"
run_common_handshake
request '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"capture_screenshot","arguments":{"path":"spont_warmup.png"}}}'
sleep 1
request '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"capture_screenshot","arguments":{"path":"spont_before.png"}}}'
request '{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"run_command","arguments":{"command":"eval trigger_spontaneous = true"}}}'
sleep 1
request '{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"capture_screenshot","arguments":{"path":"spont_after.png"}}}'
stop_server

SPONT_OUT="$OUT"
SPONT_ERR="$ERR"

grep -q '"id":1' "$SPONT_OUT"
grep -q '"id":2' "$SPONT_OUT"
grep '"id":2' "$SPONT_OUT" | grep -q '"isError":false'
grep -q '"id":3' "$SPONT_OUT"
grep '"id":3' "$SPONT_OUT" | grep -q '"isError":false'
grep -q '"id":4' "$SPONT_OUT"
grep '"id":4' "$SPONT_OUT" | grep -q '"isError":false'
grep -q '"id":5' "$SPONT_OUT"
grep '"id":5' "$SPONT_OUT" | grep -q '"isError":false'
grep -q '"id":6' "$SPONT_OUT"
grep '"id":6' "$SPONT_OUT" | grep -q '"isError":false'

SPONT_BEFORE="$(extract_capture_path "$SPONT_OUT" 4)"
SPONT_AFTER="$(extract_capture_path "$SPONT_OUT" 6)"
require_nonempty_file "$SPONT_BEFORE"
require_nonempty_file "$SPONT_AFTER"

cmp -s "$SPONT_BEFORE" "$SPONT_AFTER" && {
  echo "Spontaneous runtime error did not change the captured framebuffer; expected normal TIC-80 post-error view change." >&2
  echo "stdout: $SPONT_OUT" >&2
  echo "stderr: $SPONT_ERR" >&2
  exit 1
}

echo "MCP error mode regression test passed."
echo "triggered stdout: $TRIGGERED_OUT"
echo "triggered stderr: $TRIGGERED_ERR"
echo "spontaneous stdout: $SPONT_OUT"
echo "spontaneous stderr: $SPONT_ERR"
