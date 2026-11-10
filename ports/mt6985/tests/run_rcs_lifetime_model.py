#!/usr/bin/env python3
"""Execute extracted candidate RCS init/exit against serial userspace API mocks."""
from pathlib import Path
import argparse
import shlex
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cc', default='gcc', help='compiler command, including optional flags (e.g. "clang -fsanitize=address,undefined")')
p.add_argument('--generate-only', action='store_true')
p.add_argument('--build-dir', type=Path)
a = p.parse_args()
source = HERE.parent / 'linux-6.1/drivers/spmi/spmi-mt6985-experimental.c'
s = source.read_text(encoding='utf-8')
start = s.index('static void mt6985_pmif_rcs_exit(')
end = s.index('static int mt6985_pmif_experimental_probe(', start)
functions = s[start:end]
assert functions.count('static void mt6985_pmif_rcs_exit(') == 1
assert functions.count('static int mt6985_pmif_rcs_init(') == 1
assert functions.count('static ') == 2
text = (HERE / 'rcs_lifetime.c.in').read_text(encoding='utf-8')
assert text.count('@RCS_LIFETIME_FUNCTIONS@') == 1
text = text.replace('@RCS_LIFETIME_FUNCTIONS@', functions)


def run(b):
    b.mkdir(parents=True, exist_ok=True)
    f = b / 'rcs_lifetime_model.c'
    exe = b / 'rcs_lifetime_model'
    f.write_text(text, encoding='utf-8')
    if a.generate_only:
        return
    subprocess.run(shlex.split(a.cc) + [
        '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-Wno-unused-parameter', '-O0', '-g', str(f), '-o', str(exe)
    ], check=True)
    subprocess.run([str(exe)], check=True)


if a.build_dir:
    b = a.build_dir.resolve()
    if HERE not in b.parents:
        p.error('build dir must be strict tests descendant')
    run(b)
else:
    if a.generate_only:
        p.error('generate-only requires build-dir')
    with tempfile.TemporaryDirectory(dir=HERE, prefix='.rcs-lifetime-') as d:
        run(Path(d))
