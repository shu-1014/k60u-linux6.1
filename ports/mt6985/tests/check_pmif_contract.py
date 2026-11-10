"""Independent vendor-derived transport candidate checks. No hardware validation."""
from pathlib import Path
import re

def check():
    s=(Path(__file__).resolve().parents[1]/'linux-6.1/drivers/spmi/spmi-mt6985-experimental.c').read_text()
    for value in [0x880,0x884,0x894,0x8a4,0x8a8]:
        assert re.search(r'0x0*'+format(value,'x')+r'\b',s,re.I),hex(value)
    for clk in ['pmif_clk_mux','spmimst_m_clk_mux','spmimst_p_clk_mux','vlp_pmif_clk_mux','vlp_spmimst_m_clk_mux','vlp_spmimst_p_clk_mux']:
        assert '"'+clk+'"' in s,clk
    assert 'runtime_review_approved' in s and 'module_param(' not in s
    assert 'raw_spin_lock_irqsave' in s and 'raw_spin_unlock_irqrestore' in s
    assert 'mediatek,mt6985-spmi-experimental' in s
    assert '(bc & 3)' not in s
    print('PASS vendor channel2 offsets, six clocks, shared lock, unique match and no runtime parameter (static evidence only)')

if __name__=='__main__':
    check()
