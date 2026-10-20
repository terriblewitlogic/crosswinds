#!/usr/bin/env python3
"""Exercise the real upload Make recipe with isolated fake device tools only."""

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[2]
STALE = b"mock-old-v8-app"
FRESH = b"mock-new-v9-app:" + bytes(range(64))
VERIFIED = "upload verified: the app on the device matches the build"
PORT = "MOCK_X4_NO_DEVICE"


def mock_tool(tool, args):
    """Invoked through wrappers whose interpreter is the real sys.executable."""
    folder = Path(os.environ["SH_UPLOAD_GATE_FIXTURE"]).resolve()
    config = json.loads((folder / "fixture.json").read_text())
    build = folder / "build"
    app = build / "crosswinds.ino.bin"
    scenario = config["scenario"]
    if tool == "sleep":
        return 0

    def option(name):
        return args[args.index(name) + 1]

    if tool == "arduino-cli":
        stage = args[0]
        assert stage in ("compile", "upload")
        assert Path(option("--build-path" if stage == "compile" else "--input-dir")).resolve() == build
        if stage == "compile":
            flags = option("--build-property")
            assert "-DSH_X4_VOYAGE_STANDBY=1" in flags
            assert "-DSH_X4_STANDBY_DIAGNOSTICS=1" in flags
        if stage == "upload":
            assert option("-p") == PORT
    elif tool == "python3":
        stage = "backup"
        assert args[0] == "tools/device/backup_x4.py" and option("--port") == PORT
    else:
        assert tool == "esptool" and option("--port") == PORT
        stage = "readback"
        assert args[-4:-2] == ["read-flash", "0x10000"]
        assert Path(args[-1]).resolve() == build / "readback.bin"

    with (folder / "calls.jsonl").open("a") as log:
        log.write(json.dumps({"stage": stage}) + "\n")
    if scenario == stage + "-fail":
        return 7
    if stage == "compile":
        app.write_bytes(FRESH)
    elif stage == "upload":
        # If a stale artifact ever reaches this stage, preserve that evidence.
        (folder / "mock-device-app.bin").write_bytes(app.read_bytes())
    elif stage == "readback":
        data = (folder / "mock-device-app.bin").read_bytes()
        assert int(args[-2]) == len(data)
        if scenario == "readback-mismatch":
            data = bytes([data[0] ^ 1]) + data[1:]
        (build / "readback.bin").write_bytes(data)
    return 0


def run_case(base, name, scenario, ignored, expected, empty_port=False):
    folder = base / name
    fake = folder / "fake-bin"
    build = folder / "build"
    fake.mkdir(parents=True)
    build.mkdir()
    (build / "crosswinds.ino.bin").write_bytes(STALE)
    (folder / "fixture.json").write_text(json.dumps({"scenario": scenario}))
    wrapper = "#!/bin/sh\nexec {} {} --mock-tool \"$0\" \"$@\"\n".format(
        shlex.quote(sys.executable), shlex.quote(str(Path(__file__).resolve())))
    for tool in ("arduino-cli", "python3", "esptool", "sleep"):
        path = fake / tool
        path.write_text(wrapper)
        path.chmod(0o700)
    env = os.environ.copy()
    for key in ("MAKEFLAGS", "MFLAGS", "GNUMAKEFLAGS", "MAKELEVEL"):
        env.pop(key, None)
    env["PATH"] = str(fake) + os.pathsep + env.get("PATH", "")
    env["SH_UPLOAD_GATE_FIXTURE"] = str(folder)
    make = shutil.which("make")
    if not make:
        raise RuntimeError("make is required")
    command = [make, "--no-print-directory", "-f", str(ROOT / "Makefile")]
    if ignored:
        command.append("-i")
    command += ["upload-x4", "PORT=" + ("" if empty_port else PORT),
                "X4_BUILD_DIR=" + str(build), "ESPTOOL=" + str(fake / "esptool"),
                "LGFX=" + str(folder / "unused-libraries"),
                "ARDUINO_LIBRARIES=" + str(folder / "unused-libraries")]
    result = subprocess.run(command, cwd=str(ROOT), env=env, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, timeout=20, check=False)
    calls = folder / "calls.jsonl"
    stages = [json.loads(line)["stage"] for line in calls.read_text().splitlines()] if calls.exists() else []
    success = scenario == "success" and not empty_port
    correct_exit = result.returncode == 0 if success or ignored else result.returncode != 0
    current_app = (build / "crosswinds.ino.bin").read_bytes()
    fresh_used = (success and current_app == FRESH and
                  (folder / "mock-device-app.bin").read_bytes() == FRESH and
                  (build / "readback.bin").read_bytes() == FRESH)
    stale_untouched = current_app == STALE if empty_port or scenario == "compile-fail" else True
    passed = (stages == expected and correct_exit and stale_untouched and
              (VERIFIED in result.stdout) == success and (fresh_used if success else True))
    record = {"name": name, "ignore_make_errors": ignored, "make_exit_code": result.returncode,
              "stages": stages, "verified_message": VERIFIED in result.stdout, "passed": passed}
    if success:
        record["fresh_app_uploaded_and_compared"] = fresh_used
    if empty_port or scenario == "compile-fail":
        record["stale_app_untouched"] = stale_untouched
    if not passed:
        record["output"] = result.stdout
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="write the concise JSON result")
    args = parser.parse_args()
    cases = [
        ("compile-fail-normal", "compile-fail", False, ["compile"], False),
        ("compile-fail-ignore-errors", "compile-fail", True, ["compile"], False),
        ("backup-fail-ignore-errors", "backup-fail", True, ["compile", "backup"], False),
        ("upload-fail-ignore-errors", "upload-fail", True, ["compile", "backup", "upload"], False),
        ("readback-fail-ignore-errors", "readback-fail", True, ["compile", "backup", "upload", "readback"], False),
        ("readback-mismatch-ignore-errors", "readback-mismatch", True, ["compile", "backup", "upload", "readback"], False),
        ("success-fresh-app", "success", False, ["compile", "backup", "upload", "readback"], False),
        ("empty-port-ignore-errors", "success", True, [], True),
    ]
    with tempfile.TemporaryDirectory(prefix="crosswinds-upload-gate-") as tmp:
        results = [run_case(Path(tmp), *case) for case in cases]
    passed = all(case["passed"] for case in results)
    record = {"at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "status": "PASS" if passed else "FAIL", "python": sys.version.split()[0],
              "command": "python3 tools/device/test_upload_gate.py --output docs/generated/voyage-economy/upload-gate-validation.json",
              "source_sha256": {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
                                for name in ("Makefile", "tools/device/test_upload_gate.py")},
              "cases": results, "reader_used": False, "real_device_tools_used": False,
              "firmware_built": False, "mock_files_retained": False,
              "scope": "Real Make recipe and cmp; fake compiler/backup/upload/readback. Backup contents are covered by the separate backup validation, not this mock gate test."}
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(record, indent=2) + "\n")
    print("upload gate: {} ({} isolated mock cases; no reader or real device tools)".format(record["status"], len(results)))
    if not passed:
        print(json.dumps([r for r in results if not r["passed"]], indent=2))
    return 0 if passed else 1


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--mock-tool":
        sys.exit(mock_tool(Path(sys.argv[2]).name, sys.argv[3:]))
    sys.exit(main())
