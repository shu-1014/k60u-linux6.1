# UFS PMIC dependency closure — work in progress

Baseline: MiCode/Xiaomi_Kernel_OpenSource corot-t-oss commit cb2ab59a621b17aafa4a3f349c84d9f18adf7aaf. This source baseline is not OS4 firmware compatibility proof.

## Real supplies from merged corot DT

UFS host vcc phandle 0x139 points to MT6363 VEMC at SPMI slave4. Node range is 1,200,000–3,500,000 uV, enable ramp 680us, always-on, allowed modes 0/2 and allow-set-load. These are constraints, not an instruction to select a voltage blindly.

UFS host vccq phandle 0x13a points to MT6373 VBUCK7 at SPMI slave5. Node range is 300,000–1,193,750 uV, enable ramp 200us, always-on, allowed modes 0/1/2. Do not substitute the separately named VBUCK4_UFS merely because of its name. Do not assume VUFS12/VUFS18 supply this particular host when its actual phandles point elsewhere.

Path: MT6985 PMIF controller -> SPMI slave4/5 -> PMIC MFD regmap/IRQ domain -> regulator child -> regulator constraints -> UFS host. Isolated regulator object compilation does not make that path functional.

## Source review findings

Vendor MT6363 VEMC selector uses TOP_TRAP to select low/high nibble and unlocks TOP_TMA_KEY_L before programming. Original error paths can return after unlock without attempting relock, and an unsupported trap value falls through without reporting a selector error. This must be corrected and fault-tested before deployment; source provenance alone is not safety validation. Candidate probe remains rejected before accesses.

MT6373 shutdown writes TOP_CFG_ELR5; probe reads an ELR revision field and regulator registration can enforce DT constraints via write-capable callbacks. Thus successful binding is not a read-only audit. Both regulator candidates must retain immutable probe rejection and no boot-parameter bypass.

## Missing closure

The actual MT6985 PMIF match is in vendor spmi-mtk-pmif-core.c, not its similarly named legacy pmif file. Header/framework and IRQ dependencies need an audit before porting. Vendor MFD supports multiple PMIC types and includes their headers unconditionally; copying only two PMIC regulator drivers cannot replace this MFD. No regulator candidate is connected to a live MFD, DTS or boot image.

## PMIF/MFD audit details

- Mainline 6.1 already has SPMI core, REGMAP_SPMI and an MTK PMIF driver. MT6985 needs real caps2 clock wiring and RCS SID interrupt-domain support, not just a compatible alias. Do not compile a second competing spmi-mtk platform driver.
- Vendor core uses main SWINF channel2 at 0x880; caps2 uses six PMIF/SPMI/VLP clock muxes, unlike the old three-clock mainline path. Matching common register offsets does not prove initialization compatibility.
- Interrupt chain is SoC RCS IRQ -> PMIF SID domain -> PMIC grouped domain -> regulator overcurrent IRQ. PMIF event/NACK diagnostic interrupts are not substitutes.
- Each PMIC has its own regmap, but PMIF transactions need a controller-wide lock across slaves. Separate regmap locks do not serialize the shared hardware transaction channel.
- PMIF supports at most four bytes per transfer. REGMAP_SPMI can request larger bulk chunks; constrain or split transfers correctly. Never copy a byte-count mask that silently truncates larger requests.
- MFD regmap is 16-bit address/8-bit value. SWCID read at 0xb is not a full 0x6363/0x6373 chip identity check. Variant currently comes from compatible.
- MT6363 top interrupt status 0x4e, 108 IRQs/6 groups; MT6373 status0x53,72 IRQs/3 groups. No MT6397 alias.
- A scoped future MFD may retain only two variants and regulator cells. Propagate IRQ errors including EPROBE_DEFER; unwind domains on failures and removal. Vendor logs-and-continues paths are not safe templates.
- Vendor full PMIF debug/AEE/sysfs helpers are not required for ordinary register transport. Some helper implementations are absent from local references; do not fabricate them or drag them into the minimal port.

