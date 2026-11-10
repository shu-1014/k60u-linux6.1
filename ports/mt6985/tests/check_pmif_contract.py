"""Independent vendor-derived transport candidate checks. No hardware validation."""
from pathlib import Path
import re

def check(source=None):
    s=source if source is not None else (Path(__file__).resolve().parents[1]/'linux-6.1/drivers/spmi/spmi-mt6985-experimental.c').read_text()
    for name,value in [('ACC',0x880),('WDATA',0x884),('RDATA',0x894),('VLD_CLR',0x8a4),('STA',0x8a8)]:
        m=re.search(r'\[MT6985_PMIF_CH2_'+name+r'\]\s*=\s*(0x[0-9a-f]+)',s,re.I)
        assert m and int(m[1],16)==value,(name,value)
    for clk in ['pmif_clk_mux','spmimst_m_clk_mux','spmimst_p_clk_mux','vlp_pmif_clk_mux','vlp_spmimst_m_clk_mux','vlp_spmimst_p_clk_mux']:
        assert '"'+clk+'"' in s,clk
    assert 'static const bool runtime_review_approved = false;' in s and 'module_param(' not in s
    probe=s[s.index('static int mt6985_pmif_experimental_probe('):]
    assert probe.index('if (!runtime_review_approved)') < probe.index('spmi_controller_alloc')
    assert probe.index('if (!runtime_review_approved)') < probe.index('devm_platform_ioremap_resource_byname')
    assert 'raw_spin_lock_irqsave' in s and 'raw_spin_unlock_irqrestore' in s
    assert 'mediatek,mt6985-spmi-experimental' in s
    assert '(bc & 3)' not in s
    remove=s[s.index('static int mt6985_pmif_experimental_remove('):]
    order=['spmi_controller_remove(ctrl);','mt6985_pmif_rcs_exit(pmif);','pmif->online = false;','clk_bulk_disable_unprepare(','spmi_controller_put(ctrl);']
    positions=[remove.index(x) for x in order]
    assert positions==sorted(positions),'remove resource ownership order'
    exit=s[s.index('static void mt6985_pmif_rcs_exit('):s.index('static int mt6985_pmif_rcs_init(')]
    positions=[exit.index(x) for x in ['free_irq(','irq_dispose_mapping(','irq_domain_remove(']]
    assert positions==sorted(positions),'IRQ teardown synchronization before domain disposal'
    init=s[s.index('static int mt6985_pmif_rcs_init('):s.index('static int mt6985_pmif_experimental_probe(')]
    assert 'of_irq_get_byname(pdev->dev.of_node, "rcs_irq")' in init
    assert 'request_threaded_irq' in init and 'IRQF_ONESHOT' in init
    assert 'enable_irq_wake(' not in init and 'IRQF_NO_SUSPEND' not in re.sub(r'/\*.*?\*/','',init,flags=re.S)
    print('PASS vendor offsets/clocks, closed gate, unique match and source teardown ordering (static evidence only)')

if __name__=='__main__':
    check()
