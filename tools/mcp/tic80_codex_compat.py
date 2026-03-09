#!/usr/bin/env python3
import json
import os
import subprocess
import sys
import threading


def _patch_initialize_response(line: str) -> str:
    try:
        msg = json.loads(line)
    except Exception:
        return line

    if not isinstance(msg, dict):
        return line

    result = msg.get("result")
    if not isinstance(result, dict):
        return line

    # TIC-80 currently returns {"protocolVersion", "capabilities"} only.
    # Codex expects MCP initialize result to include serverInfo.
    if "protocolVersion" in result and "capabilities" in result and "serverInfo" not in result:
        result["serverInfo"] = {"name": "tic80", "version": "local"}
        msg["result"] = result
        return json.dumps(msg, separators=(",", ":"))

    return line


def _pump_stdin(proc: subprocess.Popen) -> None:
    try:
        for line in sys.stdin:
            proc.stdin.write(line)
            proc.stdin.flush()
    finally:
        try:
            proc.stdin.close()
        except Exception:
            pass


def _pump_stdout(proc: subprocess.Popen) -> None:
    for raw in proc.stdout:
        line = raw.rstrip("\n")
        patched = _patch_initialize_response(line)
        sys.stdout.write(patched + "\n")
        sys.stdout.flush()


def _pump_stderr(proc: subprocess.Popen) -> None:
    for line in proc.stderr:
        sys.stderr.write(line)
        sys.stderr.flush()


def main() -> int:
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    bin_path = os.path.join(repo_root, "build", "bin", "tic80")
    cmd = ["xvfb-run", "--auto-servernum", bin_path, "--mcp"]

    proc = subprocess.Popen(
        cmd,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,
    )

    threads = [
        threading.Thread(target=_pump_stdin, args=(proc,), daemon=True),
        threading.Thread(target=_pump_stdout, args=(proc,), daemon=True),
        threading.Thread(target=_pump_stderr, args=(proc,), daemon=True),
    ]
    for t in threads:
        t.start()

    return proc.wait()


if __name__ == "__main__":
    raise SystemExit(main())
