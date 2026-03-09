#!/usr/bin/env bash
set -euo pipefail

if [ "${1:-}" = "" ]; then
  echo "usage: $0 <tic80-binary>" >&2
  exit 1
fi

BIN="$1"
TMP_DIR="$(mktemp -d)"
REQ="$TMP_DIR/req.jsonl"
OUT="$TMP_DIR/out.jsonl"
ERR="$TMP_DIR/err.log"

cleanup() {
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

cat > "$REQ" <<'EOF'
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{}}}
{"jsonrpc":"2.0","method":"notifications/initialized"}
{"jsonrpc":"2.0","id":2,"method":"tools/list"}
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"run_command","arguments":{"command":"help commands"}}}
{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"run_command","arguments":{"command":"this_command_does_not_exist"}}}
{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"run_command","arguments":{"command":"dir"}}}
EOF

set +e
xvfb-run --auto-servernum "$BIN" --mcp < "$REQ" > "$OUT" 2> "$ERR"
RUN_STATUS=$?
set -e

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
grep -q '"id":2' "$OUT"
grep -q '"name":"run_command"' "$OUT"
grep -q '"id":3' "$OUT"
grep '"id":3' "$OUT" | grep -q '"isError":false'
grep -q 'Console commands:' "$OUT"
grep -q '"id":4' "$OUT"
grep '"id":4' "$OUT" | grep -q '"isError":true'
grep '"id":4' "$OUT" | grep -qi 'unknown command'
grep -q '"id":5' "$OUT"
grep '"id":5' "$OUT" | grep -q '"isError":false'

echo "MCP stdio smoke test passed."
echo "stdout: $OUT"
echo "stderr: $ERR"
