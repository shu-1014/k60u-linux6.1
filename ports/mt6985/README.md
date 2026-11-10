# MT6985 Linux 6.1 port slices — NOT BOOT VERIFIED

## Source provenance

Vendor source: https://github.com/MiCode/Xiaomi_Kernel_OpenSource/tree/cb2ab59a621b17aafa4a3f349c84d9f18adf7aaf

Branch: `corot-t-oss`. This is a source baseline, not evidence that it matches the phone's current OS4 firmware. File hashes are in [provenance](provenance.json).

## First slice: peripheral/UFS gates

The original MT6985 clock IDs, parents, offsets and gate shifts are retained from `clk-mt6985-peri.c` and `mt6985-clk.h`. The vendor printk-only probe wrapper is replaced with Linux 6.1 `mtk_clk_simple_probe`, `.remove` cleanup and an OF module table. The standalone workflow compiles only this object against pristine Linux 6.1. It does not alter the image-building workflow, DTS, active firmware or phone partitions.

The vendor gate helper implicitly adds `CLK_OPS_PARENT_ENABLE`; the port explicitly supplies this flag in each gate macro because Linux 6.1's helper does not. Plain SET/CLR polarity is unchanged. Runtime-PM association with the provider device differs between frameworks and requires further review before integration.

UFS AO/PDN registers: status 0x4, set 0x8, clear 0xc. AO IDs 0..3 use shifts 0..3 and parents f26m_ck/f26m_ck/f26m_ck/ufs_ck. PDN IDs 0..1 use shifts 0..1 and parents ufs_ck/aes_ufsfde_ck.

Missing parents/topckgen/PLLs, scpsys domains, PMICs and PHY/host changes remain mandatory. A successful object build does not establish working clocks on hardware.

## UFS PHY finding

The original DTS provides `mphy-ver = <1>`, no PHY clocks, a 0x1000 first MMIO resource and a second SPM resource. Mainline 6.1 does not implement the vendor version branch. In the pinned vendor PHY, nonzero `mphy-ver` bypasses legacy `power_on/off` MMIO/clock operations, and runtime/system PM uses firmware calls for MPHY power. The vendor host also coordinates PHY-device runtime PM. Thus expanding MMIO or adding guessed clocks is not a justified fix. Porting only the no-op PHY power callbacks is incomplete. Firmware ABI, hardware revision PM restrictions and generic PHY runtime-PM accounting must be checked before implementing this path.

## Remaining scope

- Real MT6985 topckgen, PLL and HW voter semantics, not MT8195 aliases.
- MT6985 scpsys domain IDs 2/3 and bus protection/reset sequencing.
- MT6985 SPMI, MT6363/MT6373 MFD and regulator support.
- PHY version handling, host runtime-PM interaction and firmware SMC ABI.
- Vendor MCQ support vs legacy queue mode; existing mainline driver ignores vendor MCQ properties.
- Android kernel/vendor module ABI and broader display/input/radio/security enablement.

No startup or OS4 functionality claim is made.
