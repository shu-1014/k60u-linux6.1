# Regulator safety repair prerequisites

No runtime activation. This is a scoped next-stage plan, not implemented fixes.

## MT6363 VEMC protected write

Existing set_voltage_sel unlocks two TMA key bytes, reads TOP_TRAP, writes selected VEMC field, then relocks. A failed trap read or selector update currently returns without relocking; an unsupported trap can return success. Repairs must attempt relock even after an unlock failure because regmap single-value splitting can leave the first key byte written. Preserve first operation error, expose relock error if no earlier error, and report both when needed. Use explicit u8[2] key bytes rather than host-endian u16 memory. Reject unsupported trap and out-of-range selectors before changing voltage. A return from PMIF submit-only write is still not hardware confirmation of unlock/relock.

An extracted actual-C fault model should fail each unlock byte/step, trap read, selected update and relock; verify cleanup attempt, field0/field1 selection, error precedence and no silent unsupported trap. This cannot prove PMIC protected-write protocol or correctness of split two-byte transactions.

## Delayed OC work and reprobe

MT6363 initializes delayed work inside an OF parse callback. A missing regulator child node may skip initialization despite an IRQ resource. No cancel_delayed_work_sync remove path is present. Drivers use mutable static regulator arrays and continue after registration/request failures, including swallowing deferred IRQ lookup. Before runtime use: create per-instance data, initialize all work before requesting any IRQ, unwind failures, propagate EPROBE_DEFER, and stop IRQ producers before cancelling work and freeing rdev/data.

A naive managed cancel action ordering can be wrong: cancelling before managed IRQ release lets a running handler queue more work. Conversely freeing rdev before stopping work creates use-after-free. Define a single ownership/lifetime sequence, test probe failures at every registration/request, and include teardown while work is pending. This remains distinct from the controller's parent IRQ/domain lifetime.

## Preserve hardware provenance

Keep real selector tables, regulator names, VEMC trap field behavior and MT6373 VBUCK7 supply identity. Do not substitute VBUCK4_UFS based on name. Do not change the immutable false probe gates or boot-image build. MT6373 needs its own audit rather than mechanically copying MT6363 repairs.
