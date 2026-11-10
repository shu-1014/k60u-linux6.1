#!/usr/bin/env python3
"""Run the verbatim candidate VEMC setter against a split-byte userspace mock."""
from pathlib import Path
import argparse
import re
import shlex
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--cc', default='gcc', help='compiler command with optional flags, e.g. "clang -fsanitize=address,undefined"')
p.add_argument('--generate-only', action='store_true')
p.add_argument('--build-dir', type=Path)
a = p.parse_args()
root = HERE.parent / 'linux-6.1'
source = root / 'drivers/regulator/mt6363-regulator.c'
s = source.read_text(encoding='utf-8')
start = s.index('static int mt6363_vemc_set_voltage_sel(')
end = s.index('static int mt6363_vemc_get_voltage_sel(', start)
functions = s[start:end]
assert functions.count('static int ') == 1
header = (root / 'include/linux/regulator/mt6363-regulator.h').read_text(encoding='utf-8')
defines = []
for name, text in [('MT6363_TMA_UNLOCK_VALUE', s),
                   ('MT6363_TOP_TMA_KEY_L', header),
                   ('MT6363_TOP_TRAP', header)]:
    matches = re.findall(r'^#define[ \t]+' + name + r'[ \t]+[^\n]+', text, re.MULTILINE)
    assert len(matches) == 1, name
    defines.append(matches[0])
text = (HERE / 'vemc_model.c.in').read_text(encoding='utf-8')
for marker, value in [('@VEMC_DEFINES@', '\n'.join(defines)),
                      ('@VEMC_SETTER@', functions)]:
    assert text.count(marker) == 1
    text = text.replace(marker, value)


def run(b):
    b.mkdir(parents=True, exist_ok=True)
    f = b / 'vemc_model.c'
    exe = b / 'vemc_model'
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
    with tempfile.TemporaryDirectory(dir=HERE, prefix='.vemc-model-') as d:
        run(Path(d))
