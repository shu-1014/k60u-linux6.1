# RCS/SID IRQ candidate review criteria

Reference: fixed corot-t-oss vendor spmi-mtk-pmif-core.c:1022-1140. Candidate remains permanently runtime-disabled.

## Hardware-source behavior

The master has four EINT words at 0x20,0x24,0x28,0x2c, one byte per SID. Vendor loops SID0..15, reads its byte, writes 0xff to that byte before nested dispatch, and filters dispatch by a software enabled bit. This is not a hardware mask. Acknowledging disabled SIDs can consume their pending notification; enabling later cannot promise replay. MMIO write completion/physical ACK is not validated by a writel return.

## Lock and mapping rules

No controller transaction lock, SID mask spinlock or mutex may remain held across handle_nested_irq: PMIC children perform SPMI operations and IRQ changes. Snapshot software enable with defined READ_ONCE/WRITE_ONCE semantics; this is not a full proof of concurrent mask/removal correctness. Bounds SID to0..15 and skip missing mappings instead of dispatching IRQ0. Parent interrupt wake/type/suspend semantics require a separate board integration review.

## Lifecycle

Create domain and request the parent RCS IRQ before publishing controller children. Request failure removes domain/mappings. Controller-add failure unwinds parent IRQ, then domain, then clocks and controller storage. Removal unregisters children first (transport and RCS still alive), frees/synchronizes parent IRQ, disposes SID mappings and domain, then closes transport and disables clocks before controller_put. Do not let devm callbacks outlive the controller-private state they reference.

Linux6.1 spmi_controller_remove unregisters children and device_del, but caller must still controller_put. This permits orderly IRQ cleanup between remove and put. Existing regulator delayed-work cleanup remains a separate blocker even with correct parent ordering.

## Non-claims

Implementing an IRQ path does not validate the source's clear-before-dispatch policy, pending-event races, firmware arbitration or IRQ storms in hardware. There is no measured interrupt delivery, cold-boot takeover, write completion, runtime/system sleep, or end-to-end UFS power test. No DT or boot-image activation is allowed.
