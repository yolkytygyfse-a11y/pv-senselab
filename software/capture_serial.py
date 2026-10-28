#!/usr/bin/env python3
"""Append-only capture for the Arduino bench profile (macOS/Linux, standard library).

Writes exact RX bytes, parsed JSON objects with HOST receive time, exact optional
TX command bytes, and a checksum manifest. Does not invent device sample times.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import select
import sys
import termios
import time


def utc_now():
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def reject_constant(value):
    raise ValueError(f"Non-JSON number: {value}")


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate JSON key: {key}")
        result[key] = value
    return result


class Recorder:
    """Owns a new capture directory; never appends to/replaces an older run."""
    def __init__(self, directory, source):
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=False)
        self.raw = (self.directory / "transport.bin").open("xb")
        self.records = (self.directory / "records.jsonl").open("x", encoding="utf-8")
        self.commands = (self.directory / "commands.bin").open("xb")
        self.buffer = bytearray()
        self.digest = hashlib.sha256()
        self.received = self.parsed = self.invalid = self.tail_bytes = 0
        self.long_line = False
        self.last_sync = time.monotonic()
        self.manifest = {"capture_schema": 1, "source": source, "started_utc": utc_now(),
                         "status": "in_progress", "fsync_interval_seconds": 1,
                         "sample_times": "device fields only; host time is receive time"}
        self.write_manifest()

    def write_manifest(self):
        temporary = self.directory / "manifest.tmp"
        with temporary.open("w", encoding="utf-8") as f:
            json.dump(self.manifest, f, ensure_ascii=False, indent=2, allow_nan=False)
            f.write("\n")
            f.flush()
            os.fsync(f.fileno())
        temporary.replace(self.directory / "manifest.json")

    def sync(self):
        for f in (self.raw, self.records, self.commands):
            f.flush()
            os.fsync(f.fileno())
        self.last_sync = time.monotonic()

    def feed(self, chunk):
        self.raw.write(chunk)
        self.digest.update(chunk)
        self.received += len(chunk)
        stamp = utc_now()
        fragments = chunk.split(b"\n")
        for fragment in fragments[:-1]:
            self._append(fragment)
            self._line(stamp)
        self._append(fragments[-1])
        if time.monotonic() - self.last_sync >= 1:
            self.sync()

    def _append(self, fragment):
        self.tail_bytes += len(fragment)
        if self.long_line:
            return
        if len(self.buffer) + len(fragment) > 8192:
            self.long_line = True
            self.buffer.clear()
        else:
            self.buffer.extend(fragment)

    def _line(self, stamp):
        try:
            if self.long_line:
                raise ValueError("overlong JSON line")
            value = json.loads(self.buffer.decode("utf-8"), parse_constant=reject_constant,
                               object_pairs_hook=unique_object)
            if not isinstance(value, dict):
                raise ValueError("expected JSON object")
            self.records.write(json.dumps({"host_received_utc": stamp, "record": value},
                                          ensure_ascii=False, allow_nan=False) + "\n")
            self.parsed += 1
        except (UnicodeDecodeError, json.JSONDecodeError, ValueError):
            self.invalid += 1
        self.buffer.clear()
        self.long_line = False
        self.tail_bytes = 0

    def close(self, status, reason):
        self.sync()
        for f in (self.raw, self.records, self.commands):
            f.close()
        self.manifest.update(status=status, reason=reason, ended_utc=utc_now(),
                             received_bytes=self.received, parsed_records=self.parsed,
                             invalid_lines=self.invalid, partial_tail_bytes=self.tail_bytes,
                             transport_sha256=self.digest.hexdigest())
        for name in ("records.jsonl", "commands.bin"):
            digest = hashlib.sha256()
            with (self.directory / name).open("rb") as f:
                for chunk in iter(lambda: f.read(65536), b""):
                    digest.update(chunk)
            self.manifest[name + "_sha256"] = digest.hexdigest()
        self.write_manifest()


def command_lines(path):
    if path is None:
        return []
    result = []
    for line in Path(path).read_text(encoding="ascii").splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        if len(line) >= 192 or any(ord(c) < 32 or ord(c) > 126 for c in line):
            raise ValueError("command must be a printable ASCII line shorter than 192 bytes")
        result.append((line + "\n").encode("ascii"))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="explicit /dev/cu.* or /dev/tty* serial device")
    parser.add_argument("--output", required=True, type=Path, help="new capture directory (must not exist)")
    parser.add_argument("--commands", type=Path, help="optional explicitly supplied startup commands")
    parser.add_argument("--duration", type=float, default=0, help="seconds; 0 runs until Ctrl-C")
    args = parser.parse_args()
    if not math.isfinite(args.duration) or args.duration < 0:
        parser.error("duration must be finite and nonnegative")
    pending = command_lines(args.commands)
    fd = os.open(args.port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    original = termios.tcgetattr(fd)
    recorder = None
    status, reason = "complete", "user_stop"
    try:
        attrs = termios.tcgetattr(fd)
        attrs[0] = attrs[1] = attrs[3] = 0
        attrs[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        attrs[4] = attrs[5] = termios.B115200
        attrs[6][termios.VMIN] = attrs[6][termios.VTIME] = 0
        termios.tcsetattr(fd, termios.TCSANOW, attrs)
        recorder = Recorder(args.output, args.port)
        start = time.monotonic()
        next_command = start + 1
        offset = 0
        print(f"Capturing {args.port} to {args.output}; Ctrl-C to close and checksum.", file=sys.stderr)
        while not args.duration or time.monotonic() - start < args.duration:
            ready, _, _ = select.select([fd], [], [], 0.05)
            if ready:
                try:
                    chunk = os.read(fd, 4096)
                except BlockingIOError:
                    chunk = None
                if chunk == b"":
                    raise OSError("serial device disconnected")
                if chunk:
                    recorder.feed(chunk)
            if time.monotonic() - recorder.last_sync >= 1:
                recorder.sync()
            if pending and time.monotonic() >= next_command:
                try:
                    count = os.write(fd, pending[0][offset:])
                except BlockingIOError:
                    count = 0
                if count:
                    recorder.commands.write(pending[0][offset:offset + count])
                    offset += count
                if offset == len(pending[0]):
                    pending.pop(0)
                    offset = 0
                    next_command = time.monotonic() + 0.2
        reason = "duration_reached"
    except KeyboardInterrupt:
        pass
    except BaseException as exc:
        status, reason = "failed", str(exc)
        raise
    finally:
        try:
            if recorder:
                recorder.close(status, reason)
        finally:
            try:
                termios.tcsetattr(fd, termios.TCSANOW, original)
            except termios.error:
                pass
            os.close(fd)


if __name__ == "__main__":
    main()
