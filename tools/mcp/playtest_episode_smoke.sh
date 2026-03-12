#!/usr/bin/env bash
set -euo pipefail

BIN="${1:-}"
if [ -z "$BIN" ]; then
  echo "usage: $0 /path/to/tic80" >&2
  exit 1
fi

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

cd "$ROOT"
rm -rf playtest

TIMEOUT_BIN=""
if command -v timeout >/dev/null 2>&1; then
  TIMEOUT_BIN="timeout 25s"
fi

$TIMEOUT_BIN python - "$BIN" <<'PY' >"$TMP/out.txt"
import json
import pathlib
import subprocess
import sys

root = pathlib.Path(".").resolve()
bin_path = pathlib.Path(sys.argv[1]).resolve()
cmd = [
    "xvfb-run",
    "--auto-servernum",
    str(bin_path),
    "tools/mcp/fixtures/playtest_episode.lua",
    "--skip",
    "--soft",
    "--mcp",
    "--fs",
    ".",
]

script = "\n".join([
    "log('start')",
    "set_input({right=true})",
    "frameadvance()",
    "end_episode('done','ok')",
])

requests = [
    {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}},
    {"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}},
    {
        "jsonrpc": "2.0",
        "id": 2,
        "method": "tools/call",
        "params": {
            "name": "run_playtest_episode",
            "arguments": {
                "script": script,
                "timeout_seconds": 5,
                "input_overlay": False,
            },
        },
    },
]

p = subprocess.Popen(
    cmd,
    cwd=root,
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    text=True,
)

stdout, stderr = p.communicate("".join(json.dumps(r) + "\n" for r in requests), timeout=15)
print(stdout)
sys.stderr.write(stderr)

lines = [json.loads(line) for line in stdout.splitlines() if line.strip()]
assert len(lines) >= 2, "missing MCP responses"
result = lines[-1]["result"]
assert result["isError"] is False, result
text = result["content"][0]["text"]
assert "status=done" in text, text
assert "frames=1" in text, text
artifact = root / "playtest" / "episode_1"
assert artifact.is_dir(), artifact
assert (artifact / "script.lua").is_file()
assert (artifact / "log.txt").is_file()
assert (artifact / "console.txt").is_file()
assert (artifact / "screenshots" / "000001.png").is_file()
assert "start" in (artifact / "log.txt").read_text(), "missing log.txt payload"
assert "tick 1" in (artifact / "console.txt").read_text(), "missing console trace"
PY

echo "MCP playtest episode smoke test passed."
echo "stdout: $TMP/out.txt"
