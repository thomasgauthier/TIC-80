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

python - "$BIN" "$ROOT" <<'PY'
import json
import os
import pathlib
import signal
import subprocess
import sys
import time
import select

bin_path = pathlib.Path(sys.argv[1]).resolve()
root = pathlib.Path(sys.argv[2]).resolve()

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

tool_names = {
    "sprite_set_sprite",
    "sprite_get_sprite",
    "sprite_set_spritesheet_region",
    "sprite_get_spritesheet_region",
    "sprite_set_palette",
    "sprite_get_palette",
    "map_set_rect",
    "map_get_rect",
    "map_set_chunk",
    "map_get_chunk",
}

sprite_rows = [
    "01234567",
    "89abcdef",
    "00112233",
    "44556677",
    "8899aabb",
    "ccddeeff",
    "13579bdf",
    "2468ace0",
]

region_rows = [
    "00000000",
    "0eeeeee0",
    "0e0000e0",
    "0e0cc0e0",
    "0e0cc0e0",
    "0e0000e0",
    "0eeeeee0",
    "00000000",
]

palette_colors = [
    "000000", "1a1c2c", "5d275d", "b13e53",
    "ef7d57", "ffcd75", "a7f070", "38b764",
    "257179", "29366f", "3b5dc9", "41a6f6",
    "73eff7", "f4f4f4", "94b0c2", "566c86",
]

chunk_tiles = [1, 2, 3, 4, 5, 6]


def send(proc, payload):
    proc.stdin.write(json.dumps(payload) + "\n")
    proc.stdin.flush()


def recv(proc, request_id):
    while True:
        line = proc.stdout.readline()
        if not line:
            stderr = proc.stderr.read()
            raise AssertionError(f"missing response for id={request_id}\nstderr:\n{stderr}")
        response = json.loads(line)
        if response.get("id") == request_id:
            return response


def call_tool(proc, request_id, name, arguments):
    send(proc, {
        "jsonrpc": "2.0",
        "id": request_id,
        "method": "tools/call",
        "params": {
            "name": name,
            "arguments": arguments,
        },
    })
    response = recv(proc, request_id)
    assert "result" in response, response
    return response["result"]


proc = subprocess.Popen(
    cmd,
    cwd=root,
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    text=True,
    bufsize=1,
    start_new_session=True,
)

def stop_proc():
    try:
        os.killpg(proc.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass

    try:
        return proc.communicate(timeout=5)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        return proc.communicate(timeout=5)

send(proc, {
    "jsonrpc": "2.0",
    "id": 1,
    "method": "initialize",
    "params": {
        "protocolVersion": "2025-03-26",
        "capabilities": {},
    },
})
init = recv(proc, 1)
assert init["result"]["serverInfo"]["name"] == "TIC-80", init

send(proc, {"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}})
send(proc, {"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
tool_list = recv(proc, 2)
names = {tool["name"] for tool in tool_list["result"]["tools"]}
missing = tool_names - names
assert not missing, missing

result = call_tool(proc, 10, "sprite_set_sprite", {"id": 3, "rows": sprite_rows})
assert result["isError"] is False, result

result = call_tool(proc, 11, "sprite_get_sprite", {"id": 3})
assert result["isError"] is False, result
structured = result["structuredContent"]
assert structured["id"] == 3, structured
assert structured["bank"] == 0, structured
assert structured["rows"] == sprite_rows, structured

result = call_tool(proc, 12, "sprite_set_spritesheet_region", {
    "bank": 1,
    "x": 2,
    "y": 4,
    "width": 2,
    "height": 1,
    "sprites": [
        {"rows": region_rows},
        {"rows": sprite_rows},
    ],
})
assert result["isError"] is False, result

result = call_tool(proc, 13, "sprite_get_spritesheet_region", {"bank": 1, "x": 2, "y": 4, "width": 2, "height": 1})
assert result["isError"] is False, result
structured = result["structuredContent"]
assert structured["bank"] == 1, structured
assert structured["x"] == 2 and structured["y"] == 4, structured
assert structured["width"] == 2 and structured["height"] == 1, structured
assert structured["sprites"] == [
    {"index": 1 * 256 + 2 + 4 * 16, "rows": region_rows},
    {"index": 1 * 256 + 3 + 4 * 16, "rows": sprite_rows},
], structured

result = call_tool(proc, 14, "sprite_set_palette", {"bank": 1, "vbank": 1, "colors": palette_colors})
assert result["isError"] is False, result

result = call_tool(proc, 15, "sprite_get_palette", {"bank": 1, "vbank": 1})
assert result["isError"] is False, result
structured = result["structuredContent"]
assert structured["bank"] == 1, structured
assert structured["vbank"] == 1, structured
assert structured["colors"] == palette_colors, structured

result = call_tool(proc, 16, "map_set_rect", {"bank": 1, "x": 5, "y": 7, "width": 3, "height": 2, "tile": 9})
assert result["isError"] is False, result

result = call_tool(proc, 17, "map_get_rect", {"bank": 1, "x": 5, "y": 7, "width": 3, "height": 2})
assert result["isError"] is False, result
structured = result["structuredContent"]
assert structured["tiles"] == [9, 9, 9, 9, 9, 9], structured

result = call_tool(proc, 18, "map_set_chunk", {"bank": 1, "x": 10, "y": 12, "width": 3, "height": 2, "tiles": chunk_tiles})
assert result["isError"] is False, result

result = call_tool(proc, 19, "map_get_chunk", {"bank": 1, "x": 10, "y": 12, "width": 3, "height": 2})
assert result["isError"] is False, result
structured = result["structuredContent"]
assert structured["tiles"] == chunk_tiles, structured

result = call_tool(proc, 20, "map_set_rect", {"bank": 1, "x": 239, "y": 0, "width": 2, "height": 1, "tile": 1})
assert result["isError"] is True, result

proc.stdin.close()
proc.stdin = None

deadline = time.time() + 10
while time.time() < deadline:
    if proc.poll() is not None:
        break
    ready, _, _ = select.select([proc.stdout], [], [], 0.25)
    if ready:
        proc.stdout.readline()

stdout, stderr = stop_proc()
return_code = proc.returncode

assert return_code in (0, -15), (return_code, stdout, stderr)
print("MCP visual editor smoke test passed.")
PY
