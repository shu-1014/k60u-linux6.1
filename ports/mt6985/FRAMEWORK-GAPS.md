# MT6985 core framework gaps

Reference baseline: MiCode/Xiaomi_Kernel_OpenSource corot-t-oss commit cb2ab59a621b17aafa4a3f349c84d9f18adf7aaf. No board validation.

## PLL/topckgen

Vendor clk-mt6985.c uses PLL_HWV for mainpll, univpll, mmpll and other shared roots. Flags include CLK_USE_HW_VOTER and HWV_CHK_FULL_STA and fields include HWV set/clear/done/status offsets. Mainline 6.1 struct mtk_pll_data and its operations have no matching HWV support. Dropping these fields would not preserve arbitration, status polling or firmware coordination.

Vendor topckgen additionally uses MUX_HWV for shared consumers. Mainline 6.1 mtk_mux only carries direct mux/set/clear/update fields, without HWV regmap/offsets. Real parent factors and update strobes must be preserved, not replaced by guessed fixed-frequency clocks. The newly ported infra gate object does NOT solve these missing roots.

## UFS scpsys domain 2 and 3

Vendor mtk-scpsys-mt6985.c identifies domain 2 ctl=0xe10 and domain 3 ctl=0xe14, SRAM PDN bit8 and ACK bit12. Both use SRAM_ISO and IS_PWR_CON_ON; domain 3 also NON_CPU_RTFF. No explicit UFS subdomain relationship is present; do not invent one.

Mainline gaps verified against mtk-pm-domains.c/.h:

1. Local control ACK: vendor sets ON bit2, waits ctl bit30, delays 50us, sets ON2 bit3, waits ctl bit31. Mainline sets both and waits global status. Vendor clears bit2/waits ACK30 then bit3/waits ACK31; mainline clears ON2 then ON. A data-only table cannot express this.
2. SRAM_ISO down timing: vendor sets CLKISO bit5, clears ISOINT_B bit6, delays 1us; mainline delays between the two operations. Preserve old SoC behavior if introducing an MT6985 quirk.
3. NON_CPU_RTFF: domain3 uses SAVE bit24, NRESTORE bit25 and CLK_DIS bit28 with saved-state tracking between off/on cycles. No matching mainline capability/state exists.
4. Bus protection: host off requires VLP bit7, infra bit4, VLP bit8 in that order; unprotect reverses it. PHY uses UFSCFG_AO bit0. Mainline groups infracfg and smi steps, so it cannot represent cross-map interleaving. Add a typed ordered-step path with VLP and UFS-AO mappings, not a reordered grouped approximation.
5. Vendor bus-set reads enable state, retries writes and unwinds already-applied steps on failure; these semantics need explicit handling/review rather than silently omitting them.
6. Current mainline rejects sta_mask=0 and assumes global status for domain state. Local-ACK support also needs validation/state/init changes, not merely a callback branch.

A scoped future implementation can register only UFS IDs 2/3 with preserved IDs and explicit DT wiring; it need not transplant GPU/APU/MD domains. It must not advertise all original domains as functional.

## New infra gate slice

clk-mt6985-bus.c preserves the official four clocks: CLDMA at bit12, DPMAIF main at bit22, DPMAIF26M at bit25 and AP I3C at bit27. SET=0x80, CLR=0x84, STATUS=0x90. Vendor gate-helper parent-enable behavior is supplied explicitly. Only logging wrapper/module lifecycle is adapted. Missing axi_ck, dpmaif_main_ck, f26m_ck and i2c_ck parents are not fabricated.
