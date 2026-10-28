#!/usr/bin/env python3
"""Build the real ESP32-S3 sketch with Arduino CLI; never uploads or installs cores."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sketch", type=Path, default=ROOT / "firmware/pv-sensing/siri")
    parser.add_argument("--cli", default=shutil.which("arduino-cli") or
                        "/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli")
    parser.add_argument("--data-dir", type=Path, default=Path.home() / "Library/Arduino15")
    parser.add_argument("--fqbn", default="esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc")
    parser.add_argument("--ctags-path", type=Path, help="optional directory containing native Arduino ctags")
    args = parser.parse_args()
    if not Path(args.cli).is_file() or not args.data_dir.is_dir():
        parser.error("Arduino CLI/core data directory not found; supply --cli and --data-dir")
    build = ROOT / "firmware/pv-sensing/build"
    build.mkdir(parents=True, exist_ok=True)
    settings = {"data": str(args.data_dir.resolve()), "downloads": str(build / "downloads"),
                "user": str(Path.home() / "Documents/Arduino")}
    config = build / "arduino-cli.yaml"
    config.write_text("directories:\n" + "".join(f"  {k}: {json.dumps(v)}\n" for k, v in settings.items()) +
                      f"build_cache:\n  path: {json.dumps(str(build / 'cache'))}\n")
    command = [args.cli, "compile", "--config-file", str(config), "--fqbn", args.fqbn,
               "--warnings", "all", "--jobs", "4", "--build-path", str(build / "sketch")]
    ctags = args.ctags_path or (build / "tools")
    if (ctags / "ctags").is_file():
        command += ["--build-property", f"runtime.tools.ctags.path={ctags.resolve()}"]
    command.append(str(args.sketch.resolve()))
    print("Building", args.sketch, "for", args.fqbn, flush=True)
    with (build / "compile.log").open("w") as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
    print((build / "compile.log").read_text(), end="")
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
