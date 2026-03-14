#!/usr/bin/env bash
set -euo pipefail

BIN="${1:-}"
if [ -z "$BIN" ]; then
  echo "usage: $0 /path/to/tic80" >&2
  exit 1
fi

ROOT="$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

cd "$ROOT"

python - "$BIN" <<'PY' >"$TMP/out.txt"
import json
import os
import pathlib
import select
import signal
import subprocess
import sys
import time

root = pathlib.Path(".").resolve()
bin_path = pathlib.Path(sys.argv[1]).resolve()

cmd = [
    "xvfb-run",
    "--auto-servernum",
    str(bin_path),
    "--skip",
    "--soft",
    "--mcp",
    "--fs",
    ".",
]

volume_values = list(range(15, -1, -1)) + [0] * 14
wave_values = [(i * 3) % 16 for i in range(32)]
pitch_values = [0, 1, 2, 3, 4, 5, 6, 7, -8, -7, -6, -5, -4, -3, -2, -1] + [0] * 14

requests = [
    {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-03-26", "capabilities": {}}},
    {"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}},
    {"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {}},
    {"jsonrpc": "2.0", "id": 3, "method": "tools/call", "params": {"name": "sfx_set_wavetable", "arguments": {"sfx": 0, "waveform": 2, "values": wave_values}}},
    {"jsonrpc": "2.0", "id": 4, "method": "tools/call", "params": {"name": "sfx_get_wavetable", "arguments": {"sfx": 0, "waveform": 2}}},
    {"jsonrpc": "2.0", "id": 5, "method": "tools/call", "params": {"name": "sfx_set_volume_envelope", "arguments": {"sfx": 1, "values": volume_values}}},
    {"jsonrpc": "2.0", "id": 6, "method": "tools/call", "params": {"name": "sfx_get_volume_envelope", "arguments": {"sfx": 1}}},
    {"jsonrpc": "2.0", "id": 7, "method": "tools/call", "params": {"name": "sfx_set_pitch_envelope", "arguments": {"sfx": 1, "values": pitch_values, "pitch16x": True}}},
    {"jsonrpc": "2.0", "id": 8, "method": "tools/call", "params": {"name": "sfx_get_pitch_envelope", "arguments": {"sfx": 1}}},
    {"jsonrpc": "2.0", "id": 9, "method": "tools/call", "params": {"name": "sfx_set_panning", "arguments": {"sfx": 1, "left": True, "right": False}}},
    {"jsonrpc": "2.0", "id": 10, "method": "tools/call", "params": {"name": "sfx_get_panning", "arguments": {"sfx": 1}}},
    {"jsonrpc": "2.0", "id": 11, "method": "tools/call", "params": {"name": "sfx_set_speed", "arguments": {"sfx": 1, "speed": -2}}},
    {"jsonrpc": "2.0", "id": 12, "method": "tools/call", "params": {"name": "sfx_get_speed", "arguments": {"sfx": 1}}},
    {"jsonrpc": "2.0", "id": 13, "method": "tools/call", "params": {"name": "sfx_set_loop_points", "arguments": {"sfx": 1, "target": "pitch", "start": 3, "size": 5}}},
    {"jsonrpc": "2.0", "id": 14, "method": "tools/call", "params": {"name": "sfx_get_loop_points", "arguments": {"sfx": 1, "target": "pitch"}}},
]

p = subprocess.Popen(
    cmd,
    cwd=root,
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    text=True,
    start_new_session=True,
)

def stop_proc():
    try:
        os.killpg(p.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass

    try:
        return p.communicate(timeout=5)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(p.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        return p.communicate(timeout=5)

payload = "".join(json.dumps(r) + "\n" for r in requests)
p.stdin.write(payload)
p.stdin.close()
p.stdin = None

lines = []
pending_ids = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}
deadline = time.time() + 20

while pending_ids and time.time() < deadline:
    ready, _, _ = select.select([p.stdout], [], [], 0.5)
    if not ready:
        continue

    line = p.stdout.readline()
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
    stdout, stderr = stop_proc()
    raise SystemExit(
        f"timed out waiting for MCP responses {sorted(pending_ids)}\nstdout:\n{''.join(lines)}{stdout}\nstderr:\n{stderr}"
    )

stdout, stderr = stop_proc()
stdout = "".join(lines) + stdout
print(stdout)
sys.stderr.write(stderr)

parsed = [json.loads(line) for line in lines if line.strip()]
assert len(parsed) >= 10, "missing MCP responses"
responses = {line.get("id"): line for line in parsed if "id" in line}

tool_names = {tool["name"] for tool in responses[2]["result"]["tools"]}
for required in (
    "sfx_set_wavetable",
    "sfx_get_wavetable",
    "sfx_set_volume_envelope",
    "sfx_get_volume_envelope",
    "sfx_set_pitch_envelope",
    "sfx_get_pitch_envelope",
    "sfx_set_panning",
    "sfx_get_panning",
    "sfx_set_speed",
    "sfx_get_speed",
    "sfx_set_loop_points",
    "sfx_get_loop_points",
):
    assert required in tool_names, f"missing {required}"

assert responses[3]["result"]["isError"] is False
assert responses[4]["result"]["isError"] is False
assert responses[4]["result"]["structuredContent"]["waveform"] == 2
assert responses[4]["result"]["structuredContent"]["values"] == wave_values

assert responses[5]["result"]["isError"] is False
assert responses[6]["result"]["isError"] is False
assert responses[6]["result"]["structuredContent"]["values"] == volume_values

assert responses[7]["result"]["isError"] is False
assert responses[8]["result"]["isError"] is False
assert responses[8]["result"]["structuredContent"]["pitch16x"] is True
assert responses[8]["result"]["structuredContent"]["values"] == pitch_values

assert responses[9]["result"]["isError"] is False
assert responses[10]["result"]["isError"] is False
assert responses[10]["result"]["structuredContent"] == {"sfx": 1, "left": True, "right": False}

assert responses[11]["result"]["isError"] is False
assert responses[12]["result"]["isError"] is False
assert responses[12]["result"]["structuredContent"] == {"sfx": 1, "speed": -2}

assert responses[13]["result"]["isError"] is False
assert responses[14]["result"]["isError"] is False
assert responses[14]["result"]["structuredContent"] == {"sfx": 1, "target": "pitch", "start": 3, "size": 5}
PY

echo "MCP SFX tools smoke test passed."
echo "stdout: $TMP/out.txt"
