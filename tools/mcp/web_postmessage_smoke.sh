#!/usr/bin/env bash
set -euo pipefail

if [ "${1:-}" = "" ]; then
  echo "usage: $0 <tic80.js>" >&2
  exit 1
fi

JS_PATH="$1"
SOURCE_PREJS="build/html/prejs.js"

if [ ! -f "$JS_PATH" ]; then
  echo "missing js artifact: $JS_PATH" >&2
  exit 1
fi

if [ ! -f "$SOURCE_PREJS" ]; then
  echo "missing source bridge: $SOURCE_PREJS" >&2
  exit 1
fi

node --check "$SOURCE_PREJS"

grep -q 'value.jsonrpc === "2.0"' "$SOURCE_PREJS"
grep -q 'Request too large' "$SOURCE_PREJS"
grep -q 'moduleObject.tic80McpBridge = bridge' "$SOURCE_PREJS"
grep -q 'hasPendingRequests' "$SOURCE_PREJS"
grep -q 'popNextRequestIntoBuffer' "$SOURCE_PREJS"
grep -q 'sendSerializedResponse' "$SOURCE_PREJS"
grep -q 'postMessage' "$SOURCE_PREJS"
grep -q 'addEventListener("message"' "$SOURCE_PREJS"

grep -q 'tic80McpBridge' "$JS_PATH"
grep -q 'postMessage' "$JS_PATH"
grep -q 'Request too large' "$JS_PATH"

echo "MCP postMessage web smoke test passed."
echo "js: $JS_PATH"
echo "source: $SOURCE_PREJS"
