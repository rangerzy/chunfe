#!/usr/bin/env python3
"""Run one owned ASR soak process with macOS system and scheduling telemetry.

This records scheduler and memory-pressure proxies, not human perception or GPU
frame rendering. It never changes other applications or drops system caches.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import math
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import threading
import time

MIB = 1024 * 1024


def command_output(argv: list[str]) -> str:
    try:
        return subprocess.check_output(argv, text=True, stderr=subprocess.DEVNULL, timeout=2)
    except (OSError, subprocess.SubprocessError):
        return ""


def snapshot(pid: int | None = None) -> dict:
    row: dict = {}
    raw = command_output(["vm_stat"])
    page_match = re.search(r"page size of (\d+)", raw)
    if page_match:
        page_bytes = int(page_match.group(1))
        counters = {m.group(1): int(m.group(2)) for m in re.finditer(r"^([^:\n]+):\s+(\d+)", raw, re.M)}
        for key, output in (
            ("Swapins", "swapins_mib"), ("Swapouts", "swapouts_mib"),
            ("Compressions", "compressed_mib"), ("Decompressions", "decompressed_mib"),
            ("Pages occupied by compressor", "compressor_resident_mib"),
            ("Pages free", "free_mib"), ("Pages wired down", "wired_mib"),
        ):
            if key in counters:
                row[output] = counters[key] * page_bytes / MIB
    swap = command_output(["sysctl", "-n", "vm.swapusage"])
    match = re.search(r"used\s*=\s*([\d.]+)([KMG])", swap)
    if match:
        row["swap_used_mib"] = float(match.group(1)) * {"K": 1 / 1024, "M": 1, "G": 1024}[match.group(2)]
    pressure = command_output(["sysctl", "-n", "kern.memorystatus_vm_pressure_level"]).strip()
    if pressure.isdigit():
        row["kernel_memory_pressure"] = int(pressure)
    if pid is not None:
        process = command_output(["ps", "-p", str(pid), "-o", "%cpu=,rss="]).split()
        if len(process) == 2:
            row["process_cpu_percent"] = float(process[0])
            row["process_rss_mib"] = int(process[1]) / 1024
    return row


def percentile(values: list[float], fraction: float) -> float:
    return sorted(values)[min(len(values) - 1, max(0, math.ceil(len(values) * fraction) - 1))] if values else 0.0


class SchedulerProbe:
    def __init__(self) -> None:
        self.stop = threading.Event()
        self.lock = threading.Lock()
        self.pending: list[float] = []
        self.all_values: list[float] = []
        self.thread = threading.Thread(target=self.run, name="system-scheduler-probe", daemon=True)

    def run(self) -> None:
        while not self.stop.is_set():
            start = time.monotonic()
            if self.stop.wait(0.05):
                return
            delay = max(0.0, (time.monotonic() - start - 0.05) * 1000)
            with self.lock:
                self.pending.append(delay)
                self.all_values.append(delay)

    def drain(self) -> list[float]:
        with self.lock:
            values, self.pending = self.pending, []
        return values


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=Path, required=True, help="system JSONL telemetry output")
    parser.add_argument("--baseline-seconds", type=int, default=10)
    parser.add_argument("--max-swap-growth-mib", type=float, default=1536)
    parser.add_argument("--max-seconds", type=int, default=1200, help="owned test wall-time limit")
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("provide a test command after --")
    if sys.platform != "darwin":
        parser.error("this observer uses macOS vm_stat and sysctl")
    if args.baseline_seconds < 1 or args.max_seconds < 1 or args.max_swap_growth_mib <= 0:
        parser.error("durations and swap growth limit must be positive")

    args.log.parent.mkdir(parents=True, exist_ok=True)
    start = time.monotonic()
    probe = SchedulerProbe()
    probe.thread.start()
    process: subprocess.Popen | None = None
    aborted = ""
    interrupted = threading.Event()
    previous_handlers = {}
    for sig in (signal.SIGINT, signal.SIGTERM):
        previous_handlers[sig] = signal.signal(sig, lambda signum, frame: interrupted.set())
    previous: dict = {}
    swap_baseline: float | None = None
    high_swap_count = 0
    critical_count = 0
    stall_count = 0
    peak_swap_growth = 0.0
    child_start = 0.0
    samples = 0
    exit_code = 125

    with args.log.open("w", encoding="utf-8") as log:
        def write(row: dict) -> None:
            row["elapsed_s"] = round(time.monotonic() - start, 3)
            row["utc"] = dt.datetime.now(dt.timezone.utc).isoformat(timespec="milliseconds")
            log.write(json.dumps(row, ensure_ascii=False) + "\n")
            log.flush()

        def observe(stage: str) -> dict:
            nonlocal previous, samples
            row = snapshot(process.pid if process is not None else None)
            values = probe.drain()
            row.update({"type": "sample", "stage": stage, "scheduler_p95_ms": percentile(values, .95),
                        "scheduler_max_ms": percentile(values, 1), "scheduler_samples": len(values)})
            for counter in ("swapins_mib", "swapouts_mib", "compressed_mib", "decompressed_mib"):
                if counter in row and counter in previous:
                    row[counter.replace("_mib", "_delta_mib")] = max(0.0, row[counter] - previous[counter])
            previous = row.copy()
            if swap_baseline is not None and "swap_used_mib" in row:
                row["swap_growth_mib"] = row["swap_used_mib"] - swap_baseline
            samples += 1
            write(row)
            return row

        write({"type": "configuration", "observer_pid": os.getpid(), "baseline_seconds": args.baseline_seconds,
               "max_swap_growth_mib": args.max_swap_growth_mib, "max_seconds": args.max_seconds,
               "scope": "system counters and scheduler only; rendering and subjective responsiveness not measured"})
        try:
            for _ in range(args.baseline_seconds):
                row = observe("before_test")
                if interrupted.wait(1):
                    aborted = "observer interrupted before test launch"
                    break
            swap_baseline = row.get("swap_used_mib")
            write({"type": "telemetry_status", "swap_watchdog_available": swap_baseline is not None,
                   "kernel_pressure_watchdog_available": "kernel_memory_pressure" in row,
                   "vm_counters_available": "swapins_mib" in row})
            if not aborted:
                process = subprocess.Popen(command)
                child_start = time.monotonic()
                write({"type": "test_start", "pid": process.pid, "executable": command[0], "swap_baseline_mib": swap_baseline})
                while process.poll() is None:
                    cycle_start = time.monotonic()
                    row = observe("test_running")
                    growth = row.get("swap_growth_mib", 0.0)
                    peak_swap_growth = max(peak_swap_growth, growth)
                    high_swap_count = high_swap_count + 1 if growth > args.max_swap_growth_mib else 0
                    critical_count = critical_count + 1 if row.get("kernel_memory_pressure", 0) & 4 else 0
                    stall_count = stall_count + 1 if row["scheduler_max_ms"] > 2000 else 0
                    if interrupted.is_set():
                        aborted = "observer interrupted"
                    elif high_swap_count >= 3:
                        aborted = f"system swap grew by more than {args.max_swap_growth_mib:g} MiB for three samples"
                    elif critical_count >= 3:
                        aborted = "kernel reported critical memory pressure for three samples"
                    elif stall_count >= 3:
                        aborted = "independent scheduler probe stalled over two seconds for three samples"
                    elif time.monotonic() - child_start > args.max_seconds:
                        aborted = f"owned test exceeded {args.max_seconds} seconds"
                    if aborted:
                        write({"type": "watchdog_abort", "pid": process.pid, "reason": aborted})
                        process.terminate()
                        try:
                            process.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait(timeout=5)
                        break
                    interrupted.wait(max(0.0, 1.0 - (time.monotonic() - cycle_start)))
                exit_code = process.wait()
                for _ in range(5):
                    observe("after_test")
                    time.sleep(1)
        finally:
            if process is not None and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
            probe.stop.set()
            probe.thread.join(timeout=2)
            for sig, handler in previous_handlers.items():
                signal.signal(sig, handler)
            write({"type": "summary", "test_exit_code": exit_code, "aborted": bool(aborted), "abort_reason": aborted,
                   "peak_swap_growth_mib": peak_swap_growth, "samples": samples,
                   "scheduler_p95_ms": percentile(probe.all_values, .95),
                   "scheduler_max_ms": percentile(probe.all_values, 1)})
    return 124 if aborted else (exit_code if exit_code >= 0 else 128 - exit_code)


if __name__ == "__main__":
    raise SystemExit(main())
