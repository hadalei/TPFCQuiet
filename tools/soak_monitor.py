"""Read-only installed-service observation. Never opens EC ports or sends commands."""
import argparse
import csv
import ctypes
import datetime as dt
import io
import json
import os
from pathlib import Path
import subprocess
import time
import hashlib
import re
import winreg


def stamp():
    return dt.datetime.now().astimezone().isoformat(timespec="seconds")


def awake_seconds():
    value = ctypes.c_ulonglong()
    if not ctypes.windll.kernel32.QueryUnbiasedInterruptTime(ctypes.byref(value)):
        raise ctypes.WinError()
    return value.value / 10_000_000


def recent_samples(path):
    with path.open("rb") as f:
        header = f.readline().decode("utf-8-sig").strip()
        f.seek(0, 2)
        size = f.tell()
        f.seek(max(0, size - 65536))
        tail = f.read()
        lines = tail.decode("utf-8", errors="replace").splitlines()
    # A final non-newline-terminated record may still be being written.
    complete = lines if tail.endswith(b"\n") else lines[:-1]
    rows = [next(csv.DictReader(io.StringIO(header + "\n" + line)))
            for line in complete if line.startswith("20")]
    if not rows:
        raise ValueError("no complete telemetry row")
    return rows


def last_sample(path):
    return recent_samples(path)[-1]


def service_executable():
    with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                        r"SYSTEM\CurrentControlSet\Services\TPFCQuiet") as key:
        command, _ = winreg.QueryValueEx(key, "ImagePath")
    match = re.fullmatch(r'"([^"\r\n]+)"\s+--service', os.path.expandvars(command))
    if not match:
        raise ValueError("unexpected service executable command")
    exe = Path(match.group(1)).resolve(strict=True)
    root = (Path(os.environ["ProgramFiles(x86)"]) / "TPFCQuiet").resolve()
    if not exe.is_relative_to(root) or exe.name != "TPFCQuiet.exe":
        raise ValueError("service executable outside installation")
    return exe


def check_state(state, row, age, expected):
    mode = {"bios": 0, "shadow": 1, "smart": 2}[expected]
    paused = expected != "bios" and (state.get("control_paused", state.get("lid_closed")) == 1 or state.get("resume_pending") == 1)
    checks = {
        "service_unavailable": not state.get("connected"),
        "engine_not_alive": state.get("alive") != 1,
        "sample_unhealthy": state.get("healthy") != 1,
        "unexpected_mode": state.get("mode") != (0 if paused else mode),
        "stale_telemetry": age < -5 or age > 15,
        "engine_fault": state.get("fault") != 0 or row.get("fault") != "0",
        "telemetry_mode_mismatch": row.get("mode") != str(state.get("mode")),
        "telemetry_request_mismatch": row.get("requested") != str(state.get("requested")),
    }
    if expected in ("bios", "shadow") or paused:
        checks["fan_readback_not_bios"] = state.get("requested") != 128 or state.get("readback") != [128, 128]
    if expected == "smart" and not paused:
        checks["smart_not_certified"] = state.get("verified") != 1 or state.get("ready") != 1
        checks["named_source_unavailable"] = state.get("vendor_fresh") is not True
        checks["fan_readback_mismatch"] = state.get("readback") != [state.get("requested")] * 2
    return [k for k, v in checks.items() if v], paused


def interval_valid(rows, start, end, mode):
    """All logged samples must cover and satisfy the interval, not just endpoints."""
    if mode is None:
        return False
    expected = str({"bios": 0, "shadow": 1, "smart": 2}[mode])
    parsed = [(dt.datetime.strptime(r["time"], "%Y-%m-%d %H:%M:%S").timestamp(), r) for r in rows]
    points = [(t, r) for t, r in parsed if start - 15 <= t <= end + 1]
    if not points or points[0][0] > start or points[-1][0] < end - 15:
        return False
    previous = points[0][0]
    for t, r in points:
        if t - previous > 15:
            return False
        previous = t
        if t < start - 1:
            continue
        if r.get("mode") != expected or r.get("healthy") != "1" or r.get("fault") != "0" or r.get("demo") != "0":
            return False
        request = r.get("requested")
        if r.get("readback1") != request or r.get("readback2") != request:
            return False
        if mode == "smart":
            if r.get("verified") != "1" or r.get("vendor_fresh") != "1":
                return False
        elif request != "128":
            return False
    return True


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--hours", type=float, default=8)
    p.add_argument("--interval", type=float, default=30)
    p.add_argument("--once", action="store_true")
    p.add_argument("--mode", choices=("bios", "shadow", "smart"), default="bios")
    args = p.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    exe = service_executable()
    digest = hashlib.sha256(exe.read_bytes()).hexdigest()
    telemetry = Path(os.environ["ProgramData"]) / "TPFCQuiet/telemetry-v3.csv"
    summary = {"pid": os.getpid(), "started": stamp(), "kind": args.mode + " observation",
               "executable": str(exe), "sha256": digest, "paused_checks": 0,
               "target_valid_seconds": args.hours * 3600, "valid_seconds": 0,
               "samples": 0, "invalid_checks": 0, "gaps": 0,
               "status": "running", "smart_acceptance": False,
               "sleep_wake_acceptance": "requires independently observed sleep/wake"}
    previous_wall = None
    previous_awake = None
    previous_good = False
    deadline = time.time() + 7 * 86400
    while time.time() < deadline:
        now = time.time()
        awake = awake_seconds()
        record = {"observed": stamp(), "problems": []}
        paused = False
        status_file = args.output / "current-service.json"
        try:
            if service_executable() != exe or hashlib.sha256(exe.read_bytes()).hexdigest() != digest:
                raise ValueError("installed version changed; restart observation")
            status_file.unlink(missing_ok=True)
            result = subprocess.run([str(exe), "--status", "--output", str(status_file)],
                                    timeout=8, creationflags=subprocess.CREATE_NO_WINDOW)
            state = json.loads(status_file.read_text(encoding="utf-8"))
            record["state"] = state
            rows = recent_samples(telemetry)
            row = rows[-1]
            record["telemetry"] = row
            age = (dt.datetime.now() - dt.datetime.strptime(row["time"], "%Y-%m-%d %H:%M:%S")).total_seconds()
            record["telemetry_age_seconds"] = age
            record["problems"], paused = check_state(state, row, age, args.mode)
            if result.returncode:
                record["problems"].append("status_exit_failure")
            record["paused"] = paused
        except Exception as e:
            record["problems"].append(type(e).__name__ + ": " + str(e))
        good = not record["problems"] and not paused
        if previous_wall is not None:
            elapsed = now - previous_wall
            awake_elapsed = awake - previous_awake
            if elapsed > args.interval * 3 or elapsed < 0 or elapsed - awake_elapsed > 2:
                summary["gaps"] += 1
                record["gap_seconds"] = elapsed
                record["awake_seconds_in_gap"] = awake_elapsed
            elif previous_good and good:
                if interval_valid(rows, previous_wall, now, args.mode):
                    summary["valid_seconds"] += max(0, min(elapsed, awake_elapsed))
                else:
                    record["interval_excluded"] = "mixed, unhealthy or incomplete telemetry interval"
        summary["samples"] += 1
        summary["invalid_checks"] += bool(record["problems"])
        summary["paused_checks"] += paused
        summary["updated"] = record["observed"]
        summary["latest_problems"] = record["problems"]
        if summary["valid_seconds"] >= summary["target_valid_seconds"]:
            summary["status"] = "completed_" + args.mode + "_observation"
            summary["smart_acceptance"] = args.mode == "smart" and summary["invalid_checks"] == 0
        if args.once:
            summary["status"] = "single_check_only"
        with (args.output / "observations.jsonl").open("a", encoding="utf-8") as f:
            f.write(json.dumps(record, ensure_ascii=False) + "\n")
        temporary = args.output / "summary.tmp"
        temporary.write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
        temporary.replace(args.output / "summary.json")
        if args.once or summary["status"] != "running":
            return
        previous_wall, previous_awake, previous_good = now, awake, good
        time.sleep(max(1, args.interval - (time.time() - now)))
    summary["status"] = "expired_incomplete"
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
