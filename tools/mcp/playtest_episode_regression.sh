#!/usr/bin/env bash
set -euo pipefail

if [ "${1:-}" = "" ]; then
  echo "usage: $0 <tic80-binary>" >&2
  exit 1
fi

BIN="$(CDPATH='' cd -- "$(dirname -- "$1")" && pwd)/$(basename -- "$1")"
ROOT="$(CDPATH='' cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
TMP_DIR="$(mktemp -d)"

cleanup() {
  rm -rf "$TMP_DIR"
}
trap cleanup EXIT

cd "$ROOT"
rm -rf playtest

python - "$BIN" <<'PY'
import json
import pathlib
import struct
import subprocess
import sys
import time
import zlib

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

def decode_png(path: pathlib.Path):
    data = path.read_bytes()
    assert data.startswith(b"\x89PNG\r\n\x1a\n"), f"not a png: {path}"
    pos = 8
    width = height = color_type = bit_depth = interlace = None
    idat = bytearray()

    while pos < len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        pos += 4
        chunk_type = data[pos:pos + 4]
        pos += 4
        chunk = data[pos:pos + length]
        pos += length + 4  # skip data + crc

        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type, _, _, interlace = struct.unpack(">IIBBBBB", chunk)
        elif chunk_type == b"IDAT":
            idat.extend(chunk)
        elif chunk_type == b"IEND":
            break

    assert bit_depth == 8, f"unsupported bit depth: {bit_depth}"
    assert interlace == 0, f"unsupported interlace: {interlace}"
    if color_type == 6:
        bpp = 4
    elif color_type == 2:
        bpp = 3
    else:
        raise AssertionError(f"unsupported color type: {color_type}")

    raw = zlib.decompress(bytes(idat))
    stride = width * bpp
    rows = []
    prev = bytearray(stride)
    off = 0

    def paeth(a, b, c):
        p = a + b - c
        pa = abs(p - a)
        pb = abs(p - b)
        pc = abs(p - c)
        if pa <= pb and pa <= pc:
            return a
        if pb <= pc:
            return b
        return c

    for _ in range(height):
        filt = raw[off]
        off += 1
        row = bytearray(raw[off:off + stride])
        off += stride

        if filt == 1:
            for i in range(stride):
                row[i] = (row[i] + (row[i - bpp] if i >= bpp else 0)) & 0xFF
        elif filt == 2:
            for i in range(stride):
                row[i] = (row[i] + prev[i]) & 0xFF
        elif filt == 3:
            for i in range(stride):
                left = row[i - bpp] if i >= bpp else 0
                up = prev[i]
                row[i] = (row[i] + ((left + up) // 2)) & 0xFF
        elif filt == 4:
            for i in range(stride):
                left = row[i - bpp] if i >= bpp else 0
                up = prev[i]
                up_left = prev[i - bpp] if i >= bpp else 0
                row[i] = (row[i] + paeth(left, up, up_left)) & 0xFF
        elif filt != 0:
            raise AssertionError(f"unsupported filter: {filt}")

        prev = row
        rows.append(row)

    return width, height, rows, color_type


def pixel(path: pathlib.Path, x: int, y: int):
    width, height, rows, color_type = decode_png(path)
    assert 0 <= x < width and 0 <= y < height
    bpp = 4 if color_type == 6 else 3
    row = rows[y]
    start = x * bpp
    rgb = tuple(row[start:start + 3])
    alpha = row[start + 3] if bpp == 4 else 0xFF
    return rgb + (alpha,)


def send(proc, request):
    proc.stdin.write(json.dumps(request) + "\n")
    proc.stdin.flush()


def recv_response(proc, request_id):
    while True:
        line = proc.stdout.readline()
        if not line:
            stderr = proc.stderr.read()
            raise AssertionError(f"missing response for id={request_id}\nstderr:\n{stderr}")
        obj = json.loads(line)
        if obj.get("id") == request_id:
            return obj


def run_episode(proc, request_id, script, overlay):
    send(proc, {
        "jsonrpc": "2.0",
        "id": request_id,
        "method": "tools/call",
        "params": {
            "name": "run_playtest_episode",
            "arguments": {
                "script": script,
                "timeout_seconds": 5,
                "input_overlay": overlay,
            },
        },
    })
    response = recv_response(proc, request_id)
    result = response["result"]
    assert result["isError"] is False, response
    text = result["content"][0]["text"]
    assert "status=done" in text, text
    assert "frames=1" in text, text
    return text


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
    return recv_response(proc, request_id)["result"]


proc = subprocess.Popen(
    cmd,
    cwd=root,
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    text=True,
    bufsize=1,
)

send(proc, {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-03-26", "capabilities": {}}})
init = recv_response(proc, 1)
assert init["result"]["serverInfo"]["name"] == "TIC-80", init

send(proc, {"jsonrpc": "2.0", "method": "notifications/initialized", "params": {}})

send(proc, {"jsonrpc": "2.0", "id": 2, "method": "tools/list"})
tools = recv_response(proc, 2)
tool_names = {tool["name"] for tool in tools["result"]["tools"]}
assert "run_playtest_episode" in tool_names, tool_names

baseline_script = "\n".join([
    "log('baseline')",
    "frameadvance()",
    "end_episode('done','baseline')",
])

move_script = "\n".join([
    "log('move')",
    "set_input({right=true})",
    "frameadvance()",
    "end_episode('done','move')",
])

overlay_script = "\n".join([
    "log('overlay')",
    "set_input({right=true})",
    "frameadvance()",
    "end_episode('done','overlay')",
])

retention_script = "\n".join([
    "log('retention')",
    "frameadvance()",
    "end_episode('done','retention')",
])

run_episode(proc, 10, baseline_script, False)
episode1 = root / "playtest" / "episode_1"
assert episode1.is_dir(), episode1
assert (episode1 / "log.txt").read_text().find("baseline") >= 0
assert "tick " in (episode1 / "console.txt").read_text()
assert "debug on" in (episode1 / "console.txt").read_text()
baseline_png = episode1 / "screenshots" / "000001.png"
assert baseline_png.is_file(), baseline_png

debug_on_pixel = pixel(baseline_png, 181, 33)
debug_off_pixel = pixel(baseline_png, 175, 33)
assert debug_on_pixel != debug_off_pixel, (debug_on_pixel, debug_off_pixel)

time.sleep(0.2)
post_episode_result = call_tool(proc, 14, "capture_screenshot", {"path": "playtest/post_episode_after_baseline.png"})
assert post_episode_result["isError"] is False, post_episode_result
post_episode_png = root / "playtest" / "post_episode_after_baseline.png"
assert post_episode_png.is_file(), post_episode_png
post_debug_pixel = pixel(post_episode_png, 181, 33)
assert post_debug_pixel != debug_on_pixel, (post_debug_pixel, debug_on_pixel)

run_episode(proc, 11, move_script, False)
episode2 = root / "playtest" / "episode_2"
move_png = episode2 / "screenshots" / "000001.png"
assert move_png.is_file(), move_png
assert (episode2 / "log.txt").read_text().find("move") >= 0
assert "tick " in (episode2 / "console.txt").read_text()

baseline_target = pixel(baseline_png, 17, 60)
baseline_neighbor = pixel(baseline_png, 19, 60)
move_target = pixel(move_png, 17, 60)
move_neighbor = pixel(move_png, 19, 60)

assert baseline_target != move_target, (baseline_target, move_target)
assert baseline_neighbor == move_neighbor, (baseline_neighbor, move_neighbor)

run_episode(proc, 12, overlay_script, True)
episode3 = root / "playtest" / "episode_3"
overlay_png = episode3 / "screenshots" / "000001.png"
assert overlay_png.is_file(), overlay_png
assert move_png.read_bytes() != overlay_png.read_bytes(), "overlay-enabled screenshot should differ from overlay-disabled screenshot"

run_episode(proc, 13, retention_script, False)
episodes = sorted(path.name for path in (root / "playtest").glob("episode_*") if path.is_dir())
assert len(episodes) == 3, episodes
assert "episode_1" in episodes, episodes
assert "retention" in (root / "playtest" / "episode_1" / "log.txt").read_text()

error_script = "\n".join([
    "frameadvance()",
    "error('episode boom')",
])

error_result = call_tool(proc, 15, "run_playtest_episode", {
    "script": error_script,
    "timeout_seconds": 5,
    "input_overlay": False,
})
assert error_result["isError"] is True, error_result
assert "episode boom" in error_result["content"][0]["text"], error_result

debug_state = call_tool(proc, 16, "run_command", {"command": "eval trace(DEBUG_MODE == nil and 'debug off' or 'debug on')"})
assert debug_state["isError"] is False, debug_state
assert "debug off" in debug_state["content"][0]["text"], debug_state

proc.stdin.close()
time.sleep(0.5)

try:
    code = proc.wait(timeout=2)
except subprocess.TimeoutExpired:
    proc.terminate()
    code = proc.wait(timeout=10)

stderr = proc.stderr.read()
assert code == 0, stderr
PY

echo "MCP playtest episode regression test passed."
