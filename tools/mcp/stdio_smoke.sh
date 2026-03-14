#!/usr/bin/env bash
set -euo pipefail

if [ "${1:-}" = "" ]; then
  echo "usage: $0 <tic80-binary>" >&2
  exit 1
fi

BIN="$1"
TMP_DIR="$(mktemp -d)"
REQ_PIPE="$TMP_DIR/req.pipe"
OUT="$TMP_DIR/out.jsonl"
ERR="$TMP_DIR/err.log"
RUN_PID=""

cleanup() {
  if [ -n "$RUN_PID" ] && kill -0 "$RUN_PID" 2>/dev/null; then
    kill "$RUN_PID" 2>/dev/null || true
    wait "$RUN_PID" 2>/dev/null || true
  fi
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

mkfifo "$REQ_PIPE"

set +e
xvfb-run --auto-servernum "$BIN" --mcp --fs . < "$REQ_PIPE" > "$OUT" 2> "$ERR" &
RUN_PID="$!"
set -e

exec 3> "$REQ_PIPE"
printf '%s\n' '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{}}}' >&3
printf '%s\n' '{"jsonrpc":"2.0","method":"notifications/initialized"}' >&3
printf '%s\n' '{"jsonrpc":"2.0","id":2,"method":"tools/list"}' >&3
printf '%s\n' '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"run_command","arguments":{"command":"help commands"}}}' >&3
printf '%s\n' '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"run_command","arguments":{"command":"this_command_does_not_exist"}}}' >&3
printf '%s\n' '{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"run_command","arguments":{"command":"run"}}}' >&3
sleep 5
printf '%s\n' '{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"capture_screenshot","arguments":{"path":"mcp_smoke_capture.png"}}}' >&3
printf '%s\n' "{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"tools/call\",\"params\":{\"name\":\"capture_screenshot\",\"arguments\":{\"path\":\"$TMP_DIR/absolute_capture.png\"}}}" >&3
printf '%s\n' '{"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"run_command","arguments":{"command":"load grid_shooter.lua"}}}' >&3
printf '%s\n' '{"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"run_command","arguments":{"command":"run"}}}' >&3
printf '%s\n' '{"jsonrpc":"2.0","id":10,"method":"tools/call","params":{"name":"capture_screenshot","arguments":{"path":"shots/mcp_smoke_capture.png"}}}' >&3
exec 3>&-

set +e
wait "$RUN_PID"
RUN_STATUS=$?
set -e
RUN_PID=""

if [ "$RUN_STATUS" -ne 0 ]; then
  echo "MCP process failed with exit code $RUN_STATUS" >&2
  echo "stdout: $OUT" >&2
  echo "stderr: $ERR" >&2
  exit 1
fi

# Assert process returned JSON-RPC responses for each request id (no crash).
grep -q '"jsonrpc":"2.0"' "$OUT"
grep -q '"id":1' "$OUT"
grep -q '"protocolVersion":"2025-03-26"' "$OUT"
grep '"id":1' "$OUT" | grep -q '"serverInfo":{'
grep '"id":1' "$OUT" | grep -q '"name":"TIC-80"'
grep -q '"id":2' "$OUT"
grep -q '"name":"run_command"' "$OUT"
grep -q '"name":"capture_screenshot"' "$OUT"
grep -q '"id":3' "$OUT"
grep '"id":3' "$OUT" | grep -q '"isError":false'
grep -q 'Console commands:' "$OUT"
grep -q '"id":4' "$OUT"
grep '"id":4' "$OUT" | grep -q '"isError":true'
grep '"id":4' "$OUT" | grep -qi 'unknown command'
grep -q '"id":5' "$OUT"
grep '"id":5' "$OUT" | grep -q '"isError":'
grep -q '"id":6' "$OUT"
grep '"id":6' "$OUT" | grep -q '"isError":false'
grep '"id":6' "$OUT" | grep -q 'saved screenshot: mcp_smoke_capture.png'
grep -q '"id":7' "$OUT"
grep '"id":7' "$OUT" | grep -q '"isError":true'
grep '"id":7' "$OUT" | grep -q 'path must be relative to the TIC filesystem root'
grep -q '"id":8' "$OUT"
grep '"id":8' "$OUT" | grep -q '"isError":false'
grep '"id":8' "$OUT" | grep -q 'cart grid_shooter.lua loaded'
grep -q '"id":9' "$OUT"
grep '"id":9' "$OUT" | grep -q '"isError":true'
grep '"id":9' "$OUT" | grep -q 'invalid params, btn'
grep -q '"id":10' "$OUT"
grep '"id":10' "$OUT" | grep -q '"isError":true'
grep '"id":10' "$OUT" | grep -q 'relative screenshot directory does not exist: shots'

CAPTURE_PATH="$(sed -n 's/.*"id":6.*saved screenshot: [^)]*(\([^)]*\)).*/\1/p' "$OUT")"
if [ -z "$CAPTURE_PATH" ] || [ ! -s "$CAPTURE_PATH" ]; then
  echo "capture_screenshot did not produce a valid file" >&2
  echo "stdout: $OUT" >&2
  echo "stderr: $ERR" >&2
  exit 1
fi

echo "MCP stdio smoke test passed."
echo "stdout: $OUT"
echo "stderr: $ERR"
