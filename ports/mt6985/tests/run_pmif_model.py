#!/usr/bin/env python3
"""Extract unchanged PMIF callbacks and execute them in a bounded userspace shim."""
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
DEFAULT_SOURCE = HERE.parent / "linux-6.1/drivers/spmi/spmi-mt6985-experimental.c"


def extract(source: str) -> str:
    """Retain exact source bytes between explicitly checked integration boundaries."""
    start = source.index("#define MT6985_PMIF_DELAY_US")
    end = source.index("static int mt6985_pmif_experimental_probe(", start)
    fragment = source[start:end]
    names = re.findall(r"^static (?:void|int) (mt6985_\w+)\(", fragment, re.M)
    expected = ["mt6985_pmif_wait", "mt6985_pmif_read_cmd",
                "mt6985_pmif_write_cmd", "mt6985_pmif_cmd"]
    if names != expected:
        raise ValueError(f"unexpected function boundaries: {names!r}")
    for required in ("enum mt6985_pmif_reg {", "mt6985_pmif_regs[]",
                     "enum mt6985_spmimst_reg {", "mt6985_spmimst_regs[]",
                     "mt6985_pmif_clock_names[]", "struct mt6985_pmif_experimental {"):
        if fragment.count(required) != 1:
            raise ValueError(f"missing or ambiguous declaration: {required}")
    if "#include" in fragment or "platform_driver" in fragment:
        raise ValueError("kernel integration unexpectedly included")
    return fragment


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--cc", default=os.environ.get("CC", "gcc"),
                        help="compiler command; e.g. 'gcc -fsanitize=address,undefined'")
    parser.add_argument("--build-dir", type=Path,
                        help="keep artifacts in a subdirectory of this tests directory")
    parser.add_argument("--generate-only", action="store_true",
                        help="generate without compiling; requires --build-dir")
    args = parser.parse_args()
    if args.generate_only and args.build_dir is None:
        parser.error("--generate-only requires --build-dir")
    source_path = args.source.resolve()
    source = source_path.read_bytes().decode("utf-8")
    fragment = extract(source)
    template = (HERE / "pmif_model.c.in").read_text(encoding="utf-8")
    if template.count("/* @CANDIDATE@ */") != 1:
        raise ValueError("expected exactly one insertion marker")
    generated = template.replace("/* @CANDIDATE@ */", fragment)
    print(f"candidate: {source_path}\nsha256: {hashlib.sha256(source.encode()).hexdigest()}",
          flush=True)

    def run(build: Path) -> None:
        build.mkdir(parents=True, exist_ok=True)
        cfile = build / "pmif_model.c"
        executable = build / ("pmif_model.exe" if os.name == "nt" else "pmif_model")
        cfile.write_text(generated, encoding="utf-8")
        print(f"generated: {cfile}", flush=True)
        if args.generate_only:
            return
        command = shlex.split(args.cc, posix=os.name != "nt") + [
            "-std=gnu11", "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-parameter", "-Wno-unused-const-variable",
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
        with tempfile.TemporaryDirectory(prefix=".pmif-model-", dir=HERE) as tmp:
            run(Path(tmp))


if __name__ == "__main__":
    main()
