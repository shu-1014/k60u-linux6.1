# MT6985 PMIF isolated candidate review

Reference: MiCode/Xiaomi_Kernel_OpenSource corot-t-oss cb2ab59a621b17aafa4a3f349c84d9f18adf7aaf and pristine mainline Linux6.1. No runtime activation or boot integration.

## Real-source requirements

Use vendor mt6xxx channel2 SWINF (0x880 block) and actual SPMI master registers, not an MT8195 compatible alias. Caps2 clocks are pmif_clk_mux, spmimst_m_clk_mux, spmimst_p_clk_mux, vlp_pmif_clk_mux, vlp_spmimst_m_clk_mux, vlp_spmimst_p_clk_mux. Acquisition errors including deferred probe must propagate; partially enabled clocks must unwind.

RCS is a distinct parent interrupt path: SID0..15 status bytes in master EINT words 0x20/24/28/2c, clear byte 0xff before nested delivery, then PMIC child handles its own grouped status and RCS_INT_DONE. Diagnostic PMIF event/NACK paths do not replace RCS. Do not dispatch a zero mapping. Any enabled bitmap must have defined synchronization without holding a mutex across nested callbacks which may acquire that same mutex.

## Transaction correctness boundaries

Reject zero length and length above4, SID above15 and null buffers. Initialize write payload to zero before copying fewer than four bytes. Do not use `(bc & 3)+1` to silently truncate transfers. A controller-wide lock must cover data/command submission and FSM polling across every slave; separate regmap locks are insufficient.

Both original vendor write_cmd and 6.1 write_cmd return after submitting the command, without a write-completion acknowledgment. Serialization prevents a later transaction from overwriting a non-idle channel but does not turn the earlier return into proof of PMIC write success. Keep this limitation explicit rather than adding an unproven completion condition. Non-data command polling semantics also require source comparison; no speculative timeout recovery.

Raw spinlock/atomic polling latency, firmware ownership, stale-channel recovery, P-channel routing, reset/init takeover and clock parent readiness are not proven by compilation. Timeout recovery which clears a stale channel is a behavior change requiring its own reasoning and tests.

## Resource lifetime

Controller children must disappear before parent RCS IRQ teardown, IRQ handler synchronization must precede domain/mapping disposal, and clocks/MMIO/controller storage must outlive those users. Avoid a handler holding transaction locks while dispatching child PMIC callbacks which perform SPMI accesses. Runtime probe must refuse before any mapping, clock enabling, domain creation or controller registration.

A unique experimental compatible/name prevents competing with the existing MTK PMIF driver. This does not authorize changing the shipped DT to match it. No regulator/MFD/power candidate is enabled by this work.

## This iteration's explicit omissions

The transport candidate does NOT implement the RCS SID domain/handler. Its source review requirements above describe future work, not current functionality. Non-data SPMI commands return EOPNOTSUPP rather than treating a BUSY-bit observation as proven command completion. P-channel access, PMIF hardware initialization/takeover and suspend/wake routing are also not implemented. These omissions prevent claiming that the MFD parent IRQ or UFS supply chain works. The immutable probe rejection stays in place.
