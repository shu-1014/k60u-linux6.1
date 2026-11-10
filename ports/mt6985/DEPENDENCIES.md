# UFS dependency audit (source evidence, not hardware validation)

Pinned vendor commit: `cb2ab59a621b17aafa4a3f349c84d9f18adf7aaf` in MiCode/Xiaomi_Kernel_OpenSource, corot-t-oss.

## Device tree / provider closure

| Consumer input | Provider / vendor identity | Status in pristine 6.1 |
| --- | --- | --- |
| ufs_sel 0x2a:0x21 | topckgen CLK_TOP_U_SEL = 33 | Missing MT6985 topckgen |
| min parent 0x2a:0x72 | mainpll_d4_d2 = 114 | Requires real PLL/factor tree |
| max parent 0x2a:0xa4 | mmpll_d6 = 164; frequency not hardware-verified | Requires real PLL/factor tree |
| ufs_fde 0x2a:0x20 | AES_UFSFDE_SEL = 32 | Missing topckgen |
| ufs_mbist 0x2a:0x22 | U_MBIST_SEL = 34 | Missing topckgen |
| unipro clocks 0x4e:0..3 | ufscfg_ao gates | Vendor-derived gate object compiled separately |
| ufshci clocks 0x4d:0..1 | ufscfg_pdn gates | Vendor-derived gate object compiled separately |
| host power domain 0x2c:2 | UFS0_SHUTDOWN | Missing MT6985 scpsys |
| PHY power domain 0x2c:3 | UFS0_PHY_SHUTDOWN | Missing MT6985 scpsys |
| vcc | MT6363 vemc | Missing MT6985 SPMI/MFD/regulator chain |
| vccq | MT6373 vbuck7 | Missing MT6985 SPMI/MFD/regulator chain |

## Actual top muxes

Vendor `clk-mt6985.c` uses CLK_CFG_12 = 0xd0, SET = 0xd4, CLR = 0xd8. AES field shift/width 0/3, gate bit 7; UFS field 8/3, gate 15; MBIST field 16/2, gate 23. Update strobes use CLK_CFG_UPDATE1 and separate vendor-defined shifts. This is not a license to transplant isolated muxes without the correct parent tree/HWV support.

UFS parents, in hardware selector order: tck_26m_mx9_ck, mainpll_d4_d2, mainpll_d4_d4, univpll_d4_d2, mainpll_d6, mainpll_d5, univpll_d6, mmpll_d6. AES and MBIST have different selector lists. No fixed-frequency dummy parents are introduced.

## Actual scpsys domain operations

Vendor `mtk-scpsys-mt6985.c`:

- Host UFS0_SHUTDOWN: SPM control offset 0xe10, SRAM PDN bit 8, ACK bit 12, SRAM_ISO + IS_PWR_CON_ON capabilities. Bus protection sequence uses VLP 0x214/218/210/220 bit 7, infra 0xe4/e8/e0/ec bit 4, then VLP bit 8.
- PHY UFS0_PHY_SHUTDOWN: offset 0xe14, SRAM PDN bit 8, ACK bit 12; AO bus protection 0x54/58/50/5c bit 0. Includes NON_CPU_RTFF retention sequencing in addition to SRAM_ISO and IS_PWR_CON_ON.

These capabilities select vendor framework operations not provided simply by renaming a mainline domain. Do not insert a generic MT8195 alias or leave out retention/bus-protection steps.

## Firmware ABI and PHY ownership

Vendor `ufs-mediatek-sip.h` confirms service MTK_SIP_SMC_CMD(0x276), HOST_PWR_CTRL BIT(5), HOST_PWR_MPHY = 1; arguments are command, option, on. This agrees with the existing 6.1 host private header for this operation. It confirms source ABI constants only, not current OS4 firmware behavior or return semantics.

Version 1 PHY skips legacy direct MPHY access. Runtime/system sleep callbacks invoke firmware power control; vendor host enables/holds/releases provider runtime PM. Generic PHY core decides child runtime-PM behavior at creation time, so moving provider enable earlier can create double-accounting if host explicit gets are copied unchanged. Hardware revision PM-forbid logic must not silently disappear. Implement and review one complete ownership model before enabling this path.

## Gates vs platform completeness

The separate gate-object compile check proves C/API compatibility only. It is intentionally not integrated into the boot workflow. All unresolved providers above, runtime sequencing, MCQ behavior and Android ABI remain prerequisites for a usable kernel.
