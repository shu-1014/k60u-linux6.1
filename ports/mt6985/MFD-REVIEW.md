# MT6363/MT6373 SPMI MFD candidate review

This candidate is not a functioning phone PMIC stack. Probe must reject before creating a regmap, writing IRQ registers, creating domains or adding regulator children. No boot argument or DT flag may bypass that gate.

## Scope

Only MT6363 and MT6373, their true grouped interrupt tables, and regulator cells/overcurrent resources are retained. The original SWCID byte at 0xb is read as a revision byte, not validated as a complete chip identity. Other PMICs and auxiliary functional cells are excluded. Parent MT6985 PMIF and RCS/SID IRQ support remain missing.

## Linux 6.1 transfer-limit finding

With a regmap bus present, regmap.c:833-838 takes max_raw_read/write from the bus, ignoring the configuration fields. Therefore setting config.max_raw_read/write=4 on devm_regmap_init_spmi_ext does NOT enforce a four-byte PMIF limit in this kernel.

The isolated candidate uses use_single_read/write=true, REGCACHE_NONE and no multi-write. In normal raw/bulk paths, regmap.c:2070-2073 and 2961-2964 then select one value per transaction; 8-bit values yield one-byte transfers. This avoids oversized ordinary transfers but changes transaction grouping. In particular, a two-byte PMIC unlock becomes two bus transactions and is NOT established hardware-equivalent. No no-increment or async API is promised. PMIF controller-level serialization is still required across slaves and cannot be replaced by per-PMIC regmap locking.

## Interrupt and lifetime criteria

- Parent IRQ acquisition failure, including EPROBE_DEFER, must stop probe before publishing children.
- Check register access failures during initialization and handler ACKs; do not report successful initialization after an I/O failure.
- irq_bus_sync_unlock has no return value: log failed enable writes and keep cache unchanged rather than pretending hardware accepted a change. This does not solve interrupt recovery.
- Child removal precedes IRQ release; managed free_irq synchronizes the threaded handler before domain disposal. Domain mappings must be removed before the domain itself. Do not add an unbalanced disable_irq to the parent descriptor.
- Managed actions are LIFO: register domain cleanup before requesting the managed parent IRQ, and add managed MFD children last. Account for IRQ mappings created before a partial mfd_add failure. This ordering is a source review, not a tested reprobe/IRQ-controller lifecycle guarantee.
- Linux 6.1 MFD core creates IRQ mappings without checking for zero at mfd-core.c:270. Pre-create/check regulator OC mappings before child publication; domain cleanup must dispose all created mappings even after partial creation failure.
- No wakeup enablement should be silently introduced or left unbalanced.

Compilation can establish API/type compatibility and table reproduction, not real IRQ delivery, secure-world ownership, electrically safe writes, or a complete removal contract for currently unsafe regulator children. Keep probe closed until those are separately validated.
