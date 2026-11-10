"""Independent source contract checks; no hardware validity claim."""
from pathlib import Path
import re

SOURCE = Path(__file__).resolve().parents[1] / 'linux-6.1/drivers/soc/mediatek/mt6985-ufs-power-experimental.c'

def check(source=None):
    s = source if source is not None else SOURCE.read_text(encoding='utf-8')
    expected = {
        'ufs_host_protection': [
            ('UFS_BUS_VLP', 0x214, 0x218, 0x210, 0x220, 7),
            ('UFS_BUS_IFR', 0xe4, 0xe8, 0xe0, 0xec, 4),
            ('UFS_BUS_VLP', 0x214, 0x218, 0x210, 0x220, 8),
        ],
        'ufs_phy_protection': [('UFS_BUS_AO', 0x54, 0x58, 0x50, 0x5c, 0)],
    }
    for name, want in expected.items():
        table = re.search(r'\b' + name + r'\[\]\s*=\s*\{(.*?)\n\};', s, re.S)
        assert table, name
        rows = re.findall(r'\{\s*(UFS_BUS_\w+),\s*(0x\w+),\s*(0x\w+),\s*(0x\w+),\s*(0x\w+),\s*BIT\((\d+)\)\s*\}', table[1])
        got = [(r[0], *(int(x, 16) for x in r[1:5]), int(r[5])) for r in rows]
        assert got == want, (name, got, want)
    assert 'static const bool runtime_review_approved;' in s
    assert 'module_param(' not in s
    probe = s[s.index('static int ufs_power_probe('):]
    assert probe.index('if (!runtime_review_approved)') < probe.index('devm_platform_ioremap_resource')
    assert probe.index('if (!runtime_review_approved)') < probe.index('pm_genpd_init')
    print('PASS independent vendor bus-table contract and immutable probe rejection')

if __name__ == '__main__':
    check()
