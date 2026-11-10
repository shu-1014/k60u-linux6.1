# Experimental UFS domain implementation acceptance criteria

This candidate must remain independent from the boot image workflow. It is not a working platform power controller.

## Isolation

Use a new experimental compatible, never alias mt8195 and never claim all mt6985-scpsys domains. Require an explicit enable gate defaulting false. Expose IDs 2/3 only; other onecell slots must be NULL so the standard xlate returns ENOENT. Do not place ERR_PTR values in the array: Linux 6.1 registration iterates every non-NULL entry as a real genpd. No shipping DTS may silently select this driver. Explicit SPM MMIO and infra/VLP/UFS-AO syscon phandles are required. No guessed parent clocks, supply voltages or subdomain relationships.

## Source-backed sequence

Reference: MiCode/Xiaomi_Kernel_OpenSource commit cb2ab59a621b17aafa4a3f349c84d9f18adf7aaf, drivers/soc/mediatek/mtk-scpsys.c and mtk-scpsys-mt6985.c.

On: set ON2? No: set PWR_ON bit2 first, wait local ACK bit30, delay 50us, then set PWR_ON_2ND bit3 and wait ACK bit31. Clear clock-disable bit4, clear isolation bit1, release reset bit0. For a previously saved PHY retention state, assert RTFF clock-disable28, clear/restore NRESTORE25 then clear28. Clear SRAM PDN8, wait ACK12 clear; set SRAM ISOINT_B6, delay1us, clear SRAM CLKISO5. Release bus protection in reverse order.

Off: ordered bus protection set; SRAM CLKISO5 set, ISOINT_B6 clear, delay1us, SRAM PDN8 set and ACK12 wait. PHY retention: set28, pulse SAVE24 high then low, clear28. Set isolation1, clear reset0, set clock-disable4. Clear ON2? No: clear bit2 and wait ACK30 clear, then clear bit3 and wait ACK31 clear. Mark saved retention state only on successful completion.

Host bus steps: VLP set214/clr218/en210/sta220 bit7; infra sete4/clre8/ene0/staec bit4; VLP same registers bit8. PHY bus step: UFS-AO set54/clr58/en50/sta5c bit0. Set checks enable readback up to eleven writes and polls status ACK. Clear ignores status ACK as vendor BUS_PROT_IGN does; write errors still must propagate. Preserve cross-map ordering.

## Failure and takeover policy

Reject mixed ACK states. Do not label unknown hardware state active using only software flags. Initial state needs control, ACK, SRAM and bus-enable consistency checks. Firmware/bootloader already-on is not automatically proven safe to take over. Conservative rejection of bootloader-off PHY is acceptable because retention history is unavailable.

Transition failure is not proof of successful rollback. Best-effort unwind of newly asserted protection must preserve pre-existing bits. Mark uncertain domains faulted, prevent subsequent destructive callbacks, and log the failing phase. Never report success after partial control or failed bus write. Serialize domains if they share controller resources. Cleanup must not free state while genpd consumers can still reference it.

## What compilation cannot validate

Secure-world arbitration, OS4 bootloader initial state, RTFF retention correctness, PM-domain ownership against the PHY firmware SMC path, power loss ordering, bus protection completion semantics, and supplier readiness remain hardware/integration questions. Test cold boot, runtime cycles, system sleep/abort and fault injection only after the rest of the platform and a recovery path are established.
