#!/usr/bin/env bash
set -euo pipefail

BIN="${1:-}"
if [ -z "$BIN" ]; then
  echo "usage: $0 /path/to/tic80" >&2
  exit 1
fi

ROOT="$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

python - "$ROOT" "$BIN" "$TMP_DIR" <<'PY'
import json
import os
import pathlib
import select
import signal
import subprocess
import sys
import time

root = pathlib.Path(sys.argv[1]).resolve()
bin_path = pathlib.Path(sys.argv[2]).resolve()
fs_root = pathlib.Path(sys.argv[3]).resolve()

cmd = [
    "xvfb-run",
    "--auto-servernum",
    str(bin_path),
    "--skip",
    "--soft",
    "--mcp",
    "--fs",
    str(fs_root),
]

requests = [
    {
        "jsonrpc": "2.0",
        "id": 1,
        "method": "initialize",
        "params": {
            "protocolVersion": "2025-03-26",
            "capabilities": {},
            "clientInfo": {"name": "music-authoring-smoke", "version": "0"},
        },
    },
    {"jsonrpc": "2.0", "method": "notifications/initialized"},
    {"jsonrpc": "2.0", "id": 2, "method": "tools/list"},
    {
        "jsonrpc": "2.0",
        "id": 3,
        "method": "tools/call",
        "params": {
            "name": "music_set_track",
            "arguments": {"track": 0, "tempo": 180, "speed": 9, "rows": 32},
        },
    },
    {
        "jsonrpc": "2.0",
        "id": 4,
        "method": "tools/call",
        "params": {"name": "music_get_track", "arguments": {"track": 0}},
    },
    {
        "jsonrpc": "2.0",
        "id": 5,
        "method": "tools/call",
        "params": {
            "name": "music_set_frame",
            "arguments": {"track": 0, "frame": 0, "patterns": [1, 2, 3, 4]},
        },
    },
    {
        "jsonrpc": "2.0",
        "id": 6,
        "method": "tools/call",
        "params": {"name": "music_get_frame", "arguments": {"track": 0, "frame": 0}},
    },
    {
        "jsonrpc": "2.0",
        "id": 7,
        "method": "tools/call",
        "params": {
            "name": "music_set_pattern_row",
            "arguments": {
                "pattern": 1,
                "row": 5,
                "note": "C#",
                "octave": 3,
                "sfx": 7,
                "command": "V",
                "param1": 2,
                "param2": 9,
            },
        },
    },
    {
        "jsonrpc": "2.0",
        "id": 8,
        "method": "tools/call",
        "params": {"name": "music_get_pattern_row", "arguments": {"pattern": 1, "row": 5}},
    },
    {
        "jsonrpc": "2.0",
        "id": 9,
        "method": "tools/call",
        "params": {
            "name": "music_set_pattern_rows",
            "arguments": {
                "pattern": 1,
                "rows": [
                    {"row": 6, "note": "A-", "octave": 4, "sfx": 8, "command": "D", "param1": 1, "param2": 2},
                    {"row": 9, "note": "OFF", "octave": 0, "sfx": 0, "command": "", "param1": 0, "param2": 0},
                ],
            },
        },
    },
    {
        "jsonrpc": "2.0",
        "id": 10,
        "method": "tools/call",
        "params": {
            "name": "music_get_pattern_rows",
            "arguments": {"pattern": 1, "rows": [5, 6, 9]},
        },
    },
    {
        "jsonrpc": "2.0",
        "id": 11,
        "method": "tools/call",
        "params": {
            "name": "music_set_pattern_row",
            "arguments": {"pattern": 1, "row": 64, "note": "C-", "octave": 3, "sfx": 0},
        },
    },
]

proc = subprocess.Popen(
    cmd,
    cwd=root,
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    text=True,
    start_new_session=True,
)

def stop_proc() -> tuple[str, str]:
    try:
        os.killpg(proc.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass

    try:
        stdout_text, stderr_text = proc.communicate(timeout=5)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        stdout_text, stderr_text = proc.communicate(timeout=5)

    return stdout_text, stderr_text

payload = "".join(json.dumps(req, separators=(",", ":")) + "\n" for req in requests)
proc.stdin.write(payload)
proc.stdin.close()
proc.stdin = None

lines = []
pending_ids = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}
deadline = time.time() + 20

while pending_ids and time.time() < deadline:
    ready, _, _ = select.select([proc.stdout], [], [], 0.5)
    if not ready:
        continue

    line = proc.stdout.readline()
    if not line:
        break

    lines.append(line)
    try:
        message = json.loads(line)
    except json.JSONDecodeError:
        continue

    if "id" in message:
        pending_ids.discard(message["id"])

if pending_ids:
    extra_stdout, stderr = stop_proc()
    stdout = "".join(lines) + extra_stdout
    raise SystemExit(f"timed out waiting for MCP responses {sorted(pending_ids)}\nstdout:\n{stdout}\nstderr:\n{stderr}")

extra_stdout, stderr = stop_proc()
stdout = "".join(lines) + extra_stdout

messages = [json.loads(line) for line in lines if line.strip()]
indexed = {msg.get("id"): msg for msg in messages if "id" in msg}

assert 2 in indexed, "missing tools/list response"
tools = indexed[2]["result"]["tools"]
tool_names = {tool["name"] for tool in tools}
for required in {
    "music_set_track",
    "music_get_track",
    "music_set_frame",
    "music_get_frame",
    "music_set_pattern_row",
    "music_get_pattern_row",
    "music_set_pattern_rows",
    "music_get_pattern_rows",
}:
    assert required in tool_names, f"missing tool {required}"

for response_id in (3, 5, 7, 9):
    result = indexed[response_id]["result"]
    assert result["isError"] is False, (response_id, result)

track = indexed[4]["result"]["structuredContent"]
assert track == {"track": 0, "tempo": 180, "speed": 9, "rows": 32}, track

frame = indexed[6]["result"]["structuredContent"]
assert frame == {"track": 0, "frame": 0, "patterns": [1, 2, 3, 4]}, frame

row = indexed[8]["result"]["structuredContent"]
assert row["pattern"] == 1, row
assert row["row"] == {
    "row": 5,
    "note": "C#",
    "octave": 3,
    "sfx": 7,
    "command": "V",
    "param1": 2,
    "param2": 9,
}, row

rows = indexed[10]["result"]["structuredContent"]
assert rows["pattern"] == 1, rows
assert rows["rows"] == [
    {"row": 5, "note": "C#", "octave": 3, "sfx": 7, "command": "V", "param1": 2, "param2": 9},
    {"row": 6, "note": "A-", "octave": 4, "sfx": 8, "command": "D", "param1": 1, "param2": 2},
    {"row": 9, "note": "OFF", "octave": 0, "sfx": 0, "command": "", "param1": 0, "param2": 0},
], rows

invalid = indexed[11]["error"]
assert invalid["code"] == -32602, invalid

print("MCP music authoring smoke test passed.")
print(stdout)
if stderr:
    print(stderr, file=sys.stderr)
PY
