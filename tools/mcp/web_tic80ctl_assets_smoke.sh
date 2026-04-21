#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
JS_PATH="$ROOT_DIR/build/webapp/tic80ctl-browser.js"
HTML_PATH="$ROOT_DIR/build/webapp/tic80ctl-demo.html"

if [ ! -f "$JS_PATH" ]; then
  echo "missing browser controller asset: $JS_PATH" >&2
  exit 1
fi

if [ ! -f "$HTML_PATH" ]; then
  echo "missing browser demo asset: $HTML_PATH" >&2
  exit 1
fi

node --input-type=module -e "import('${JS_PATH}').then((m) => { if (typeof m.createTic80CtlBrowser !== 'function') process.exit(1); })"

grep -q 'bindTarget' "$JS_PATH"
grep -q 'openPopupTarget' "$JS_PATH"
grep -q 'run(argv' "$JS_PATH"
grep -q 'tic80ctl start' "$HTML_PATH"
grep -q 'Send \"run\" via MCP' "$HTML_PATH"
grep -q 'createTic80CtlBrowser' "$HTML_PATH"

echo "Browser tic80ctl assets smoke test passed."
echo "js: $JS_PATH"
echo "html: $HTML_PATH"
