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

cat > "$REQ" <<'EOF'
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{}}}
{"jsonrpc":"2.0","method":"notifications/initialized"}
{"jsonrpc":"2.0","id":2,"method":"tools/list"}
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"tic.echo","arguments":{"text":"hello"}}}
EOF

"$BIN" --mcp < "$REQ" > "$OUT" 2> "$ERR"

grep -q '"id":1' "$OUT"
grep -q '"protocolVersion":"2025-03-26"' "$OUT"
grep -q '"id":2' "$OUT"
grep -q '"tools"' "$OUT"
grep -q '"id":3' "$OUT"
grep -q 'tic.echo: ok' "$OUT"

echo "MCP stdio smoke test passed."
echo "stdout: $OUT"
echo "stderr: $ERR"
