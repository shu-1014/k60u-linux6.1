#!/usr/bin/env python3
"""Extract and execute the real candidate transition code against a user-space shim."""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
DEFAULT_SOURCE = HERE.parent / "linux-6.1/drivers/soc/mediatek/mt6985-ufs-power-experimental.c"


def extract(source: str) -> str:
    """Fail closed on changed boundaries; never substitute a Python algorithm."""
    start = source.index("#define UFS_RST_B")
    end = source.index("static int ufs_power_on(", start)
    fragment = source[start:end]
    required = ("ufs_update", "ufs_poll", "ufs_protect", "ufs_protect_all",
                "ufs_state", "ufs_on_sequence", "ufs_off_sequence", "ufs_transition")
    names = re.findall(r"^static (?:void|int) (ufs_\w+)\(", fragment, re.M)
    if tuple(names) != required:
        raise ValueError(f"unexpected function boundaries: {names!r}")
    if "#include" in fragment or "ufs_power_probe" in fragment:
        raise ValueError("extraction unexpectedly includes kernel integration")
    # Keep every definition and every byte of the actual functions intact.
    return fragment


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--cc", default=os.environ.get("CC", "gcc"),
                        help="compiler command (CC supported; default gcc)")
    parser.add_argument("--build-dir", type=Path,
                        help="keep generated C and executable here (must be under tests)")
    parser.add_argument("--generate-only", action="store_true",
                        help="generate C without compiling; requires --build-dir")
    args = parser.parse_args()
    if args.generate_only and args.build_dir is None:
        parser.error("--generate-only requires --build-dir")
    source = args.source.resolve().read_text(encoding="utf-8")
    from check_source_contract import check
    check(source)
    fragment = extract(source)
    template = (HERE / "power_model.c.in").read_text(encoding="utf-8")
    if template.count("/* @CANDIDATE@ */") != 1:
        raise ValueError("expected exactly one candidate insertion marker")
    generated = template.replace("/* @CANDIDATE@ */", fragment)
    digest = hashlib.sha256(source.encode()).hexdigest()
    print(f"candidate: {args.source.resolve()}\nsha256: {digest}", flush=True)

    def run(build: Path) -> None:
        build.mkdir(parents=True, exist_ok=True)
        cfile = build / "power_model.c"
        executable = build / ("power_model.exe" if os.name == "nt" else "power_model")
        cfile.write_text(generated, encoding="utf-8")
        print(f"generated: {cfile}", flush=True)
        if args.generate_only:
            return
        command = shlex.split(args.cc, posix=os.name != "nt") + [
            "-std=gnu11", "-Wall", "-Wextra", "-Werror",
            "-Wno-sign-compare", "-Wno-unused-const-variable",
            "-O0", "-g", str(cfile), "-o", str(executable)]
        print("compile:", shlex.join(command), flush=True)
        subprocess.run(command, check=True)
        subprocess.run([str(executable.resolve())], check=True)

    if args.build_dir:
        build = args.build_dir.resolve()
        if HERE not in build.parents:
            parser.error("--build-dir must be a subdirectory of this tests directory")
        run(build)
    else:
        with tempfile.TemporaryDirectory(prefix=".power-model-", dir=HERE) as tmp:
            run(Path(tmp))


if __name__ == "__main__":
    main()
